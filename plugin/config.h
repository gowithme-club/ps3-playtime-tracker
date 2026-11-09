/* Конфигурация плагина.
 *
 * Лежит рядом с самим плагином: /dev_hdd0/plugins/gwm.cfg
 *
 *   token   = ...
 *   account = Player      # обязателен, если на консоли больше одного профиля
 *   popups  = true        # уведомления в XMB; false — работать молча
 *   autostart = true      # сам пропишется в autoexec.bat; false — не трогать
 *   host    = gowithme.club
 *   port    = 80
 *
 * Без файла или без токена плагин не делает ни одного сетевого запроса —
 * это важное свойство для штуки, которая раздаётся комьюнити: пока человек
 * сам не вписал токен, наружу не уходит вообще ничего.
 *
 * Разбор строк отделён от файловых операций и проверяется хостовым тестом;
 * чтение самого файла под GWM_HOST_TEST не собирается.
 */

#ifndef GWM_CONFIG_H
#define GWM_CONFIG_H

#include <stdint.h>
#include <stddef.h>

#include "str.h"

#define GWM_CONFIG_FILE "/dev_hdd0/plugins/gwm.cfg"

#define GWM_TOKEN_MAX 128
#define GWM_ACCOUNT_MAX 64
#define GWM_HOST_MAX  96

typedef struct
{
	char     token[GWM_TOKEN_MAX];
	char     account[GWM_ACCOUNT_MAX];
	char     host[GWM_HOST_MAX];
	uint16_t port;
	/* Показывать ли уведомления в XMB. По умолчанию да: человек, поставивший
	 * плагин, должен увидеть, что тот жив, не заглядывая в status.txt. */
	int      popups;
	/* Дописывать ли себя в autoexec.bat после первого успешного ответа
	 * сервера. Кому автозагрузка не нужна или у кого свой порядок загрузки
	 * плагинов — ставит false, и файл остаётся нетронутым. */
	int      autostart;
	int      valid;
} gwm_config_t;

static void gwm_cfg_defaults(gwm_config_t *c)
{
	c->token[0] = 0;
	c->account[0] = 0;
	gwm_copy_str(c->host, sizeof(c->host), "gowithme.club");
	c->port = 80;
	c->popups = 1;
	c->autostart = 1;
	c->valid = 0;
}

/* Обрезка пробелов и кавычек по краям значения. */
static void gwm_trim(char *s)
{
	char *b = s;
	while(*b == ' ' || *b == '\t' || *b == '"') b++;

	char *e = b;
	while(*e) e++;
	while(e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' ||
	                e[-1] == '\n' || e[-1] == '"')) e--;
	*e = 0;

	if(b != s) { char *d = s; while(*b) *d++ = *b++; *d = 0; }
}

/* Сравнение без учёта регистра: в конфиге пишут и true, и True, и TRUE. */
static int gwm_strcmp_ci(const char *a, const char *b)
{
	for(;; a++, b++)
	{
		char x = *a, y = *b;

		if(x >= 'A' && x <= 'Z') x = (char)(x + 32);
		if(y >= 'A' && y <= 'Z') y = (char)(y + 32);

		if(x != y) return (int)((unsigned char)x - (unsigned char)y);
		if(!x) return 0;
	}
}

/* true/false, yes/no, on/off, 1/0. Непонятное значение оставляет как было:
 * опечатка в конфиге не должна молча выключать то, чего человек не просил. */
static int gwm_cfg_bool(const char *val, int fallback)
{
	if(gwm_strcmp_ci(val, "true") == 0 || gwm_strcmp_ci(val, "yes") == 0 ||
	   gwm_strcmp_ci(val, "on")   == 0 || gwm_strcmp(val, "1") == 0) return 1;

	if(gwm_strcmp_ci(val, "false") == 0 || gwm_strcmp_ci(val, "no") == 0 ||
	   gwm_strcmp_ci(val, "off")   == 0 || gwm_strcmp(val, "0") == 0) return 0;

	return fallback;
}

/* Разбор одной строки вида "ключ = значение". Комментарии с # и ; игнорируются. */
static void gwm_cfg_line(gwm_config_t *c, char *line)
{
	while(*line == ' ' || *line == '\t') line++;
	if(!*line || *line == '#' || *line == ';') return;

	char *eq = line;
	while(*eq && *eq != '=') eq++;
	if(!*eq) return;

	*eq = 0;
	char *key = line, *val = eq + 1;

	gwm_trim(key);
	gwm_trim(val);

	if(gwm_strcmp(key, "token") == 0)     gwm_copy_str(c->token, sizeof(c->token), val);
	else if(gwm_strcmp(key, "account") == 0) gwm_copy_str(c->account, sizeof(c->account), val);
	else if(gwm_strcmp(key, "popups") == 0) c->popups = gwm_cfg_bool(val, c->popups);
	else if(gwm_strcmp(key, "autostart") == 0) c->autostart = gwm_cfg_bool(val, c->autostart);
	else if(gwm_strcmp(key, "host") == 0) gwm_copy_str(c->host,  sizeof(c->host),  val);
	else if(gwm_strcmp(key, "port") == 0)
	{
		uint32_t p = 0;
		for(const char *q = val; *q >= '0' && *q <= '9'; q++) p = p * 10 + (uint32_t)(*q - '0');
		if(p > 0 && p < 65536) c->port = (uint16_t)p;
	}
}

#ifndef GWM_HOST_TEST

#include <cell/cell_fs.h>

static int gwm_config_load(gwm_config_t *c)
{
	int fd;
	uint64_t got = 0;
	char buf[512];

	gwm_cfg_defaults(c);

	if(cellFsOpen(GWM_CONFIG_FILE, CELL_FS_O_RDONLY, &fd, NULL, 0) != CELL_FS_SUCCEEDED)
		return 0;

	int ret = cellFsRead(fd, buf, sizeof(buf) - 1, &got);
	cellFsClose(fd);

	if(ret != CELL_FS_SUCCEEDED || got == 0) return 0;
	buf[got] = 0;

	char *line = buf;
	while(*line)
	{
		char *nl = line;
		while(*nl && *nl != '\n') nl++;

		int last = (*nl == 0);
		*nl = 0;
		gwm_cfg_line(c, line);

		if(last) break;
		line = nl + 1;
	}

	c->valid = (c->token[0] != 0);
	return c->valid;
}

#endif /* GWM_HOST_TEST */

#endif /* GWM_CONFIG_H */
