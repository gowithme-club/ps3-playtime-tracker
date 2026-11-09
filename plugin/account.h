/* Имя локального профиля консоли — поле account в протоколе.
 *
 * Порядок определения:
 *   1. значение из gwm.cfg, если задано явно — оно всегда главнее;
 *   2. текущий профиль через vsh (xusers()->GetCurrentUserNumber());
 *   3. единственный профиль на консоли — из файловой системы.
 *
 * Второй пункт важен именно для консолей с несколькими профилями: играть
 * могут разные люди, а account по протоколу — ключ привязки сессии к человеку.
 * Фиксированное имя в конфиге на такой консоли просто врёт.
 *
 * Имя определяется в момент старта сессии, а не один раз при загрузке:
 * профиль можно сменить без перезагрузки консоли.
 */

#ifndef GWM_ACCOUNT_H
#define GWM_ACCOUNT_H

#include <cell/cell_fs.h>
#include <stdint.h>
#include <stddef.h>

#include "str.h"
#include "json.h"

#define GWM_HOME_DIR    "/dev_hdd0/home"
#define GWM_ACCOUNT_MAX 64

/* Индекс GetCurrentUserNumber в таблице методов xusers. */
#define GWM_XUSERS_CURRENT_USER 12

/* Номер текущего профиля, 0 — если определить не удалось. */
static int gwm_current_user_number(void)
{
	void **(*xusers)(void) = getNIDfunc("xsetting", 0xCC56EB2D);
	if(!xusers) return 0;

	void **iface = xusers();
	if(!iface) return 0;

	int32_t (*GetCurrentUserNumber)(void) =
		(int32_t (*)(void))iface[GWM_XUSERS_CURRENT_USER];
	if(!GetCurrentUserNumber) return 0;

	int32_t n = GetCurrentUserNumber();
	return (n > 0) ? (int)n : 0;
}

/* Каталог профиля называется восемью цифрами с ведущими нулями. */
static void gwm_user_dir(char *out, size_t out_size, int user)
{
	char digits[9];

	for(int i = 7; i >= 0; i--) { digits[i] = (char)('0' + (user % 10)); user /= 10; }
	digits[8] = 0;

	gwm_copy_str(out, out_size, digits);
}

static int gwm_read_localusername(const char *dir, char *out, size_t out_size)
{
	char path[128];
	char *p = path;
	const char *end = path + sizeof(path);
	int fd;
	uint64_t got = 0;

	p = gwm_lit(p, end, GWM_HOME_DIR "/");
	p = gwm_lit(p, end, dir);
	gwm_lit(p, end, "/localusername");

	if(cellFsOpen(path, CELL_FS_O_RDONLY, &fd, NULL, 0) != CELL_FS_SUCCEEDED) return 0;

	int ret = cellFsRead(fd, out, out_size - 1, &got);
	cellFsClose(fd);

	if(ret != CELL_FS_SUCCEEDED || got == 0) return 0;

	out[got] = 0;

	/* В файле бывает перевод строки в конце. */
	for(size_t i = 0; i < (size_t)got; i++)
		if(out[i] == '\n' || out[i] == '\r') { out[i] = 0; break; }

	return out[0] != 0;
}

/* Запасной путь без vsh: если профиль на консоли ровно один, он и есть текущий. */
static int gwm_account_single(char *out, size_t out_size)
{
	int fd;
	CellFsDirent ent;
	uint64_t nread = 0;
	char found[GWM_ACCOUNT_MAX];
	int profiles = 0;

	if(cellFsOpendir(GWM_HOME_DIR, &fd) != CELL_FS_SUCCEEDED) return 0;

	while(cellFsReaddir(fd, &ent, &nread) == CELL_FS_SUCCEEDED && nread)
	{
		if(ent.d_name[0] < '0' || ent.d_name[0] > '9') continue; /* . .. и прочее */

		char name[GWM_ACCOUNT_MAX];
		if(!gwm_read_localusername(ent.d_name, name, sizeof(name))) continue;

		profiles++;
		if(profiles == 1) gwm_copy_str(found, sizeof(found), name);
	}

	cellFsClosedir(fd);

	if(profiles != 1) return 0; /* угадывать нельзя */

	gwm_copy_str(out, out_size, found);
	return 1;
}

/* Возвращает 1, если имя удалось определить. */
static int gwm_account_detect(char *out, size_t out_size)
{
	out[0] = 0;

	int user = gwm_current_user_number();
	if(user)
	{
		char dir[16];
		gwm_user_dir(dir, sizeof(dir), user);
		if(gwm_read_localusername(dir, out, out_size)) return 1;
	}

	return gwm_account_single(out, out_size);
}

#endif /* GWM_ACCOUNT_H */
