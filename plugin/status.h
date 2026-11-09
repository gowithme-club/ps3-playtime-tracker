/* Файл статуса: что плагин делает прямо сейчас.
 *
 * До него у плагина не было на консоли никаких следов вообще. Пока он слал
 * в заглушку на ноутбуке, это сходило с рук — лог был там. Стоило переключить
 * его на боевой сервер, и снаружи стало не видно ничего: жив ли он, дошёл ли
 * трофей, что ответил сервер. Один вечер ушёл на выяснение того, что должно
 * читаться с экрана за секунду.
 *
 * Отсюда и формат: `ключ = значение`, как в gwm.cfg. Разбирается тремя
 * строками и читается человеком прямо по FTP, без всякого приложения.
 * Приложение gwm-stats-app рисует ровно эти же поля на телевизоре.
 *
 * Файл переписывается целиком каждый цикл опроса. Поле updated и есть признак
 * жизни: если оно отстало больше чем на десяток секунд, поток не крутится.
 *
 * Значения только ASCII. Файл читает приложение на PS3, и шрифт с кириллицей
 * там появится не завтра; подписи оно рисует свои.
 *
 * Токен сюда не попадает. Файл виден по FTP и показывается на экране —
 * пишем только сам факт, что он задан.
 */

#ifndef GWM_STATUS_H
#define GWM_STATUS_H

#include <cell/cell_fs.h>
#include <stdint.h>
#include <stddef.h>

#include "version.h"
#include "json.h"
#include "clock.h"
#include "config.h"
#include "queue.h"
#include "session.h"
#include "account.h"
#include "trophy_cache.h"

#define GWM_STATUS_FILE GWM_QUEUE_DIR "/status.txt"

/* Названия игр бывают длинными, плюс полтора десятка коротких полей. */
#define GWM_STATUS_MAX 1024

typedef struct
{
	/* Сырой тик, как start_tick у сессии, а не готовая метка. Момент старта
	 * наступает до первого ответа сервера, то есть до того, как выучена
	 * поправка часов, — и готовая метка осталась бы сырой навсегда. На консоли,
	 * спешащей на минуту, это давало started позже updated: время старта
	 * в будущем относительно последней записи. Поправка добавляется при выводе,
	 * когда она уже известна. */
	uint64_t started_tick;

	char hb_at[GWM_ISO_LEN];
	int  hb_code;

	char sess_at[GWM_ISO_LEN];
	int  sess_code;

	char trop_at[GWM_ISO_LEN];
	int  trop_code;
	char trop_set[GWM_TROPHY_SET_ID_MAX];
} gwm_status_t;

static gwm_status_t gwm_status;
static char gwm_status_body[GWM_STATUS_MAX];

/* Отметки о последних отправках. Ставятся там, где отправка происходит, —
 * иначе статус пришлось бы собирать по возвращаемым значениям через три
 * уровня вызовов. */
static void gwm_status_hb(int code)
{
	gwm_iso_from_tick(gwm_status.hb_at, gwm_tick_now());
	gwm_status.hb_code = code;
}

static void gwm_status_sessions(int code)
{
	gwm_iso_from_tick(gwm_status.sess_at, gwm_tick_now());
	gwm_status.sess_code = code;
}

static void gwm_status_trophies(int code, const char *set_id)
{
	gwm_iso_from_tick(gwm_status.trop_at, gwm_tick_now());
	gwm_status.trop_code = code;
	gwm_copy_str(gwm_status.trop_set, sizeof(gwm_status.trop_set), set_id);
}

/* Значение в одну строку. Управляющие символы заменяются пробелом: в названии
 * игры живёт настоящий перенос строки (PS3 переносит длинное имя в PARAM.SFO,
 * чтобы оно легло в две строки на XMB — «Uncharted 3: Drake's Deception\nGame
 * of the Year»), и сырым его писать нельзя, иначе файл развалится на два поля. */
static char *gwm_status_value(char *p, const char *end, const char *src)
{
	while(*src && p < end - 1)
	{
		unsigned char c = (unsigned char)*src++;
		*p++ = (c < 0x20) ? ' ' : (char)c;
	}
	*p = 0;
	return p;
}

static char *gwm_status_key(char *p, const char *end, const char *key)
{
	p = gwm_lit(p, end, key);
	return gwm_lit(p, end, " = ");
}

/* Метка или прочерк, если события ещё не было. */
static char *gwm_status_stamp(char *p, const char *end, const char *iso)
{
	return gwm_lit(p, end, iso[0] ? iso : "-");
}

static void gwm_status_write(const gwm_config_t *cfg, const gwm_session_t *s,
                             int net_ready)
{
	char *p = gwm_status_body;
	const char *end = gwm_status_body + sizeof(gwm_status_body);
	char now[GWM_ISO_LEN];
	char account[GWM_ACCOUNT_MAX];
	int fd;
	uint64_t written = 0;

	gwm_iso_from_tick(now, gwm_tick_now());

	/* Имя профиля определяется здесь же, каждый цикл. Раньше сюда приходил
	 * снимок, сделанный при загрузке плагина, — и после смены профиля статус
	 * показывал того, кто вошёл первым, пока трофеи уже уезжали от другого.
	 * Это ровно то поле, по которому такое расхождение и ловят, врать ему
	 * нельзя. Правило то же, что у сессий и трофеев: явное имя из конфига
	 * главнее (см. account.h).
	 *
	 * Прочерк вместо имени — тоже показание: значит, текущего профиля нет
	 * (экран выбора пользователя), и обход трофеев в это время простаивает. */
	if(cfg->account[0]) gwm_copy_str(account, sizeof(account), cfg->account);
	else gwm_account_detect(account, sizeof(account));

	p = gwm_status_key(p, end, "version");
	p = gwm_lit(p, end, GWM_VERSION);

	p = gwm_status_key(p, end, "\nupdated");
	p = gwm_lit(p, end, now);

	p = gwm_status_key(p, end, "\nstarted");
	if(gwm_status.started_tick)
	{
		char started[GWM_ISO_LEN];
		gwm_iso_from_tick(started, gwm_tick_correct(gwm_status.started_tick));
		p = gwm_lit(p, end, started);
	}
	else p = gwm_lit(p, end, "-");

	p = gwm_status_key(p, end, "\naccount");
	p = gwm_status_value(p, end, account[0] ? account : "-");

	p = gwm_status_key(p, end, "\nserver");
	p = gwm_status_value(p, end, cfg->host[0] ? cfg->host : "-");
	p = gwm_lit(p, end, ":");
	p = gwm_u64(p, end, (uint64_t)cfg->port);

	/* Не сам токен, только факт. */
	p = gwm_status_key(p, end, "\ntoken");
	p = gwm_lit(p, end, net_ready ? "set" : "missing");

	p = gwm_status_key(p, end, "\nclock");
	if(gwm_clock_synced)
	{
		int64_t off = gwm_clock_offset / 1000000;
		p = gwm_lit(p, end, off < 0 ? "-" : "+");
		p = gwm_u64(p, end, (uint64_t)(off < 0 ? -off : off));
		p = gwm_lit(p, end, "s");
	}
	else p = gwm_lit(p, end, "not synced");

	p = gwm_status_key(p, end, "\ngame");
	if(s->active)
	{
		p = gwm_status_value(p, end, s->title_id);
		p = gwm_lit(p, end, " ");
		p = gwm_status_value(p, end, s->title);
	}
	else p = gwm_lit(p, end, "-");

	p = gwm_status_key(p, end, "\nsince");
	if(s->active)
	{
		char started[GWM_ISO_LEN];
		gwm_iso_from_tick(started, gwm_tick_correct(s->start_tick));
		p = gwm_lit(p, end, started);
	}
	else p = gwm_lit(p, end, "-");

	/* Дальше — что и когда ответил сервер. Ради этих трёх строк всё и затевалось. */
	p = gwm_status_key(p, end, "\nheartbeat");
	p = gwm_status_stamp(p, end, gwm_status.hb_at);
	p = gwm_lit(p, end, " ");
	p = gwm_u64(p, end, (uint64_t)gwm_status.hb_code);

	p = gwm_status_key(p, end, "\nsessions");
	p = gwm_status_stamp(p, end, gwm_status.sess_at);
	p = gwm_lit(p, end, " ");
	p = gwm_u64(p, end, (uint64_t)gwm_status.sess_code);

	p = gwm_status_key(p, end, "\ntrophies");
	p = gwm_status_stamp(p, end, gwm_status.trop_at);
	p = gwm_lit(p, end, " ");
	p = gwm_u64(p, end, (uint64_t)gwm_status.trop_code);
	p = gwm_lit(p, end, " ");
	p = gwm_lit(p, end, gwm_status.trop_set[0] ? gwm_status.trop_set : "-");

	p = gwm_status_key(p, end, "\nqueue");
	p = gwm_u64(p, end, gwm_queue_size());

	/* Сколько наборов уже разобрано и не перечитывается. */
	p = gwm_status_key(p, end, "\nsets");
	p = gwm_u64(p, end, (uint64_t)gwm_trophy_cache.count);

	p = gwm_lit(p, end, "\n");

	cellFsMkdir(GWM_QUEUE_DIR, 0777); /* уже есть — вернёт ошибку, это нормально */

	if(cellFsOpen(GWM_STATUS_FILE,
	              CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_TRUNC,
	              &fd, NULL, 0) != CELL_FS_SUCCEEDED)
		return;

	cellFsWrite(fd, gwm_status_body, (uint64_t)(p - gwm_status_body), &written);
	cellFsClose(fd);
}

#endif /* GWM_STATUS_H */
