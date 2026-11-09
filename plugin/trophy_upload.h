/* Выгрузка трофеев: обход сетов профиля и отправка состояния каждого.
 *
 * По документу шлётся полное состояние сета, а не разница — сервер сам
 * диффует с тем, что уже знает, и повторная отправка безопасна. Поэтому
 * никакого локального состояния «что именно отправлено» держать не нужно.
 *
 * А вот состояние «этот файл уже разобран» держать стоит: без него каждый
 * проход открывал все TROPUSR.DAT профиля и шёл по всем блокам, чтобы
 * отправить ровно то же самое. Отметки живут в trophy_cache.h, и проход по
 * неизменившимся наборам стоит одного cellFsStat на набор.
 *
 * Файл читается только когда он выглядит устоявшимся: размер и время
 * изменения совпали в двух проверках подряд. TROPUSR.DAT пишется в момент
 * получения трофея, и прочитать его на середине записи — реальный риск.
 */

#ifndef GWM_TROPHY_UPLOAD_H
#define GWM_TROPHY_UPLOAD_H

#include <cell/cell_fs.h>
#include <stdint.h>
#include <stddef.h>

#include "trophy.h"
#include "trophy_cache.h"
#include "record.h"
#include "http.h"
#include "account.h"

#define GWM_PATH_TROPHIES "/ingest/v1/trophies"

/* Тело на сотню трофеев — около четырёх килобайт, берём с запасом. */
#define GWM_TROPHY_BODY_MAX 8192

/* Заголовок с таблицей записей. */
#define GWM_TROPHY_HEAD_MAX (GWM_TROP_HEADER_SIZE + GWM_TROP_MAX_RECORDS * GWM_TROP_RECORD_SIZE)

static char gwm_trophy_body[GWM_TROPHY_BODY_MAX];

/* Сырые часы консоли в секундах — ровно те, в которых файловая система отдаёт
 * mtime. Поправку по серверу здесь применять нельзя: сравниваются время файла
 * и время консоли, и врут они одинаково. */
static int64_t gwm_fs_now_sec(void)
{
	return (int64_t)(gwm_tick_raw() / GWM_USEC_PER_SEC) -
	       (int64_t)GWM_DAYS_0001_TO_1970 * (int64_t)GWM_SEC_PER_DAY;
}

static void gwm_trophy_path(char *out, size_t out_size, const char *user_dir,
                            const char *set_id)
{
	char *p = out;
	const char *end = out + out_size;

	p = gwm_lit(p, end, GWM_HOME_DIR "/");
	p = gwm_lit(p, end, user_dir);
	p = gwm_lit(p, end, "/trophy/");
	p = gwm_lit(p, end, set_id);
	gwm_lit(p, end, "/TROPUSR.DAT");
}

/* Файл считается устоявшимся, если размер и mtime не изменились между
 * двумя взглядами. Возвращает 1 и заполняет size/mtime. */
static int gwm_trophy_stable(const char *path, uint64_t *size, int64_t *mtime)
{
	CellFsStat a, b;

	if(cellFsStat(path, &a) != CELL_FS_SUCCEEDED) return 0;

	sys_timer_sleep(1);

	if(cellFsStat(path, &b) != CELL_FS_SUCCEEDED) return 0;
	if(a.st_size != b.st_size || a.st_mtime != b.st_mtime) return 0;

	*size = b.st_size;
	*mtime = (int64_t)b.st_mtime;
	return 1;
}

/* Отправляет состояние одного сета. Возвращает HTTP-код, 0 — отправлять
 * было нечего (нет выбитых трофеев или файл не читается).
 *
 * Отметка ставится не только на удачную отправку, но и когда отправлять
 * оказалось нечего: файл в этом состоянии разобран, и до его изменения
 * открывать его снова незачем. А вот на ошибках открытия и чтения не
 * ставится — они преходящие, и набор надо будет перечитать. */
static int gwm_upload_trophy_set(const gwm_config_t *cfg, const char *account,
                                 int user, const char *user_dir,
                                 const char *set_id, const char *title)
{
	char path[192];
	uint8_t head[GWM_TROPHY_HEAD_MAX];
	gwm_trophy_block_t blk;
	int fd;
	uint64_t got = 0, fsize = 0;
	int64_t mtime = 0;

	gwm_trophy_path(path, sizeof(path), user_dir, set_id);

	if(!gwm_trophy_stable(path, &fsize, &mtime)) return 0;

	if(cellFsOpen(path, CELL_FS_O_RDONLY, &fd, NULL, 0) != CELL_FS_SUCCEEDED) return 0;

	if(cellFsRead(fd, head, sizeof(head), &got) != CELL_FS_SUCCEEDED)
	{
		cellFsClose(fd);
		return 0;
	}

	/* Разобрать не вышло — это свойство содержимого, а не случайность:
	 * отмечаем, иначе битый файл будет перечитываться каждый цикл. */
	if(!gwm_trophy_find_block(head, (size_t)got, &blk))
	{
		cellFsClose(fd);
		gwm_trophy_cache_mark(&gwm_trophy_cache, user, set_id, fsize, mtime,
		                      gwm_fs_now_sec());
		return 0;
	}

	size_t len = gwm_trophies_begin(gwm_trophy_body, sizeof(gwm_trophy_body),
	                                account, set_id, title);
	int count = 0;

	for(uint32_t i = 0; i < blk.count; i++)
	{
		uint8_t entry[GWM_TROP_ENTRY_SIZE];
		uint64_t pos = 0, n = 0;
		uint32_t id;
		uint64_t tick;
		char earned[GWM_ISO_LEN];

		if(cellFsLseek(fd, (int64_t)(blk.offset + (uint64_t)i * GWM_TROP_ENTRY_SIZE),
		               CELL_FS_SEEK_SET, &pos) != CELL_FS_SUCCEEDED) break;

		if(cellFsRead(fd, entry, sizeof(entry), &n) != CELL_FS_SUCCEEDED ||
		   n != sizeof(entry)) break;

		if(!gwm_trophy_decode(entry, &id, &tick)) continue;

		/* Время записано часами консоли, поэтому к нему применима та же
		 * поправка, что и к нашим меткам. */
		gwm_iso_from_tick(earned, (uint64_t)((int64_t)tick + gwm_clock_offset));

		/* Не влезаем в буфер — отправляем то, что собрали: сервер принимает
		 * неполное состояние без вреда, а остальное уедет следующим проходом. */
		if(len + 96 > sizeof(gwm_trophy_body)) break;

		len = gwm_trophies_add(gwm_trophy_body, sizeof(gwm_trophy_body), len,
		                       count == 0, id, earned);
		count++;
	}

	cellFsClose(fd);

	if(!count)
	{
		gwm_trophy_cache_mark(&gwm_trophy_cache, user, set_id, fsize, mtime,
		                      gwm_fs_now_sec());
		return 0;
	}

	len = gwm_trophies_end(gwm_trophy_body, sizeof(gwm_trophy_body), len);

	int code = gwm_http_post(cfg, GWM_PATH_TROPHIES, gwm_trophy_body, len);

	gwm_status_trophies(code, set_id);

	/* Те же коды, что и у очереди сессий (upload.h): 200 — принято, 400 и 413
	 * повторять бессмысленно, тело не станет лучше. Разница в том, что здесь
	 * «выбросить» означает отметить набор разобранным: иначе сет, который
	 * сервер стабильно не принимает, перечитывался бы вечно.
	 * Остальное (401, 429, 5xx, сеть) оставляем неотмеченным — уедет позже. */
	if(code == 200 || code == 400 || code == 413)
		gwm_trophy_cache_mark(&gwm_trophy_cache, user, set_id, fsize, mtime,
		                      gwm_fs_now_sec());

	return code;
}

/* С какого места продолжать обход. Курсор нужен не для темпа, а для
 * справедливости: набор, который не удалось отправить, остаётся неотмеченным,
 * и без курсора он выбирался бы снова и снова, а остальные не доходили бы
 * до сервера вовсе. */
static uint32_t gwm_trophy_scan_pos = 0;

/* Пауза после неудачной отправки, в циклах опроса. Минута — как повторы
 * очереди сессий, они идут по тику heartbeat.
 *
 * Без неё частый опрос выходит боком: при лежащем сервере или неверном токене
 * набор остаётся неотмеченным, тут же выбирается снова, и плагин бьёт
 * запросом каждый цикл вместо прежнего раза в полчаса. */
#define GWM_TROPHY_RETRY_POLLS 6

static uint32_t gwm_trophy_retry_wait = 0;

/* Уведомления включаются только после первого полного круга.
 *
 * Отметки trophy_cache живут в памяти, поэтому после каждой загрузки плагина
 * заново разбираются и отправляются все наборы — по одному за цикл. На четырёх
 * десятках сетов это попап каждые десять секунд минут на семь, причём ни один
 * из них не про свежий трофей. До конца первого круга молчим; дальше отправка
 * набора означает, что он изменился, то есть что-то выбито. */
static int gwm_trophy_swept = 0;

/* Отказ показываем один раз за серию: неотмеченный набор пробуется снова
 * раз в минуту, и при протухшем токене попап шёл бы всю игровую сессию. */
static int gwm_trophy_failed = 0;

/* Один проход по трофейному каталогу текущего профиля.
 *
 * Каждый набор проверяется одним cellFsStat, и обрабатывается ровно один —
 * первый изменившийся начиная с курсора. Полный разбор четырёх десятков
 * наборов — это десятки секунд с проверками устойчивости файла и запросами;
 * делать его целиком в потоке опроса нельзя, иначе плагин на это время
 * перестанет замечать смену игры. Раньше по одному набору за цикл шёл сам
 * обход, из-за чего полный круг занимал минуты; теперь за цикл идёт круг
 * из stat, а дорогая часть по-прежнему одна.
 *
 * Имя профиля берётся здесь же, из того самого каталога, откуда читаются
 * трофеи. Раньше оно приходило снимком, сделанным один раз при загрузке
 * плагина, — и после смены профиля плагин читал файлы одного человека,
 * а подписывал их именем другого. Сессии тем временем шли под правильным
 * именем, так что на сервере расходились игра и выбитая в ней платина.
 *
 * Возвращает 1, если какой-то набор обрабатывался.
 */
static int gwm_trophy_scan(const gwm_config_t *cfg)
{
	char dir[128];
	char user_dir[16];
	char account[GWM_ACCOUNT_MAX];
	char set_id[GWM_TROPHY_SET_ID_MAX];
	char first_id[GWM_TROPHY_SET_ID_MAX];
	char path[192];
	int fd;
	CellFsDirent ent;
	uint64_t nread = 0;
	uint32_t index = 0;
	uint32_t chosen = 0, first_pos = 0;
	int found = 0, first_found = 0;

	if(gwm_trophy_retry_wait) { gwm_trophy_retry_wait--; return 0; }

	int user = gwm_current_user_number();
	if(!user) return 0;

	gwm_user_dir(user_dir, sizeof(user_dir), user);

	/* Явное имя из конфига главнее — так же, как у сессий (см. account.h). */
	if(cfg->account[0])
		gwm_copy_str(account, sizeof(account), cfg->account);
	else if(!gwm_read_localusername(user_dir, account, sizeof(account)))
		return 0; /* чьи это трофеи — неизвестно, отправлять нельзя */

	char *p = gwm_lit(dir, dir + sizeof(dir), GWM_HOME_DIR "/");
	p = gwm_lit(p, dir + sizeof(dir), user_dir);
	gwm_lit(p, dir + sizeof(dir), "/trophy");

	if(cellFsOpendir(dir, &fd) != CELL_FS_SUCCEEDED) return 0;

	set_id[0] = first_id[0] = 0;

	while(cellFsReaddir(fd, &ent, &nread) == CELL_FS_SUCCEEDED && nread)
	{
		CellFsStat st;

		/* Каталоги сетов называются NPWR....... */
		if(ent.d_name[0] != 'N' || ent.d_name[1] != 'P' ||
		   ent.d_name[2] != 'W' || ent.d_name[3] != 'R') continue;

		uint32_t pos = index++;

		gwm_trophy_path(path, sizeof(path), user_dir, ent.d_name);

		if(cellFsStat(path, &st) != CELL_FS_SUCCEEDED) continue;

		if(gwm_trophy_cache_seen(&gwm_trophy_cache, user, ent.d_name,
		                         (uint64_t)st.st_size, (int64_t)st.st_mtime))
			continue;

		/* Первый изменившийся за курсором — он и нужен, дальше не смотрим. */
		if(pos >= gwm_trophy_scan_pos)
		{
			gwm_copy_str(set_id, sizeof(set_id), ent.d_name);
			chosen = pos;
			found = 1;
			break;
		}

		/* До курсора — запоминаем на случай, если за ним ничего нет. */
		if(!first_found)
		{
			gwm_copy_str(first_id, sizeof(first_id), ent.d_name);
			first_pos = pos;
			first_found = 1;
		}
	}

	cellFsClosedir(fd);

	/* Круг закончился, ничего нового за курсором — начинаем сначала. */
	if(!found && first_found)
	{
		gwm_copy_str(set_id, sizeof(set_id), first_id);
		chosen = first_pos;
		found = 1;
	}

	if(!found)
	{
		gwm_trophy_scan_pos = 0;

		/* Ничего не изменилось за целый круг — состояние устоялось,
		 * дальше отправки можно показывать. */
		gwm_trophy_swept = 1;

		return 0;
	}

	gwm_trophy_scan_pos = chosen + 1;

	/* Название игры не подставляем: соответствие набора игре нам неизвестно,
	 * и подпись текущей игрой пометила бы чужие наборы неверно. Сервер
	 * дообогащает данные через PSN по setId. */
	int code = gwm_upload_trophy_set(cfg, account, user, user_dir, set_id, NULL);

	/* 0 — отправлять было нечего, это не отказ. */
	if(code && code != 200) gwm_trophy_retry_wait = GWM_TROPHY_RETRY_POLLS;

	/* Показываем удачу и первый отказ в серии; повторы молчат. Названий
	 * трофеев в сообщении нет и быть не может: TROPUSR.DAT отдаёт числовые
	 * id, а какой игре принадлежит набор, знает только сервер. */
	if(gwm_trophy_swept && code && !(code != 200 && gwm_trophy_failed))
	{
		char msg[GWM_MSG_MAX];

		gwm_msg_trophies(msg, sizeof(msg), code);
		show_msg(msg);
	}

	if(code) gwm_trophy_failed = (code != 200);

	return 1;
}

#endif /* GWM_TROPHY_UPLOAD_H */
