/* Самопропись в автозагрузку.
 *
 * Единственный пункт установки, где человеку приходилось править системный
 * файл руками, — строка в /dev_hdd0/autoexec.bat. Плагин живёт внутри vsh
 * и до этого файла дотягивается сам, так что дописывает себя туда.
 *
 * Три правила, из которых всё остальное следует:
 *
 * 1. **Прописываемся только после подтверждённой работы** — после первого
 *    ответа 200 от сервера. К этому моменту доказано, что плагин поднялся,
 *    токен верный и сеть есть. Прописать себя раньше значит рискнуть тем,
 *    что при каждой загрузке консоли будет подниматься заведомо бесполезный
 *    (а в худшем случае — роняющий XMB) модуль.
 *
 * 2. **Чужое не трогаем.** В файле обычно уже живут другие плагины; наша
 *    строка дописывается к ним в конец, а не вместо. Если своя строка уже
 *    есть — не делаем ничего, сколько бы раз ни перезапускались.
 *
 * 3. **Не понимаем — не трогаем.** Файл больше буфера мы не переписываем
 *    вовсе: лучше не прописаться и оставить это человеку, чем обрезать
 *    чужую автозагрузку по границе буфера.
 *
 * Переводы строк — CRLF: именно так выглядит рабочий файл на консоли.
 * Запись идёт через временный файл с переименованием, как в queue.h, —
 * оборванная запись autoexec.bat стоила бы человеку автозагрузки целиком.
 * Если переименование всё же не прошло, старый файл к этому моменту уже
 * удалён, поэтому новое содержимое (в нём и чужие строки целиком) пишется
 * на место напрямую.
 *
 * Сборка нового содержимого отделена от файловых операций и проверяется
 * хостовым тестом (tests/autostart_test.c).
 */

#ifndef GWM_AUTOSTART_H
#define GWM_AUTOSTART_H

#include <stdint.h>
#include <stddef.h>

#include "str.h"
#include "json.h"   /* gwm_lit */

#define GWM_AUTOEXEC_FILE "/dev_hdd0/autoexec.bat"
#define GWM_AUTOEXEC_TMP  "/dev_hdd0/autoexec.gwm"

#define GWM_PLUGIN_PATH   "/dev_hdd0/plugins/gwm.sprx"
#define GWM_LOAD_LINE     "/loadprx.ps3?prx=" GWM_PLUGIN_PATH

/* Сколько готовы прочитать и переписать. Реальные файлы — сотни байт. */
#define GWM_AUTOEXEC_MAX  2048

/* Есть ли подстрока. Своего strstr у нас нет — libc в плагине не линкуется. */
static int gwm_has_substr(const char *hay, size_t hay_len, const char *needle)
{
	size_t n = gwm_strlen(needle);

	if(n > hay_len) return 0;

	for(size_t i = 0; i + n <= hay_len; i++)
		if(gwm_strncmp(hay + i, needle, n) == 0) return 1;

	return 0;
}

/* Собирает новое содержимое autoexec.bat. Возвращает его длину или 0, если
 * делать ничего не надо (строка уже есть) либо нельзя (не влезаем).
 *
 * existing может быть пустым — это случай «файла нет». Тогда пишем полный
 * блок с ожиданием XMB. Если файл есть, но ожидания в нём нет, блок тоже
 * пишем целиком: без «wait xmb» команда может выполниться раньше, чем XMB
 * будет готов, а ранняя загрузка плагина роняет консоль.
 */
static size_t gwm_autostart_build(char *out, size_t out_size,
                                  const char *existing, size_t existing_len)
{
	static const char head[] = "wait xmb\r\nwait 5\r\n";
	static const char gap[]  = "wait 3\r\n";
	static const char line[] = GWM_LOAD_LINE "\r\n";

	char *p = out;
	const char *end = out + out_size;

	if(gwm_has_substr(existing, existing_len, GWM_PLUGIN_PATH)) return 0;

	/* Считаем заранее: обрезанный autoexec.bat хуже неприписанного. */
	size_t need = existing_len + gwm_strlen(line) + 2;
	need += gwm_has_substr(existing, existing_len, "wait xmb") ? gwm_strlen(gap)
	                                                           : gwm_strlen(head);
	if(need >= out_size) return 0;

	for(size_t i = 0; i < existing_len; i++) *p++ = existing[i];

	if(existing_len)
	{
		/* Дописываемся с новой строки, чем бы ни кончался чужой файл. */
		if(p[-1] != '\n') { *p++ = '\r'; *p++ = '\n'; }

		p = gwm_lit(p, end, gwm_has_substr(existing, existing_len, "wait xmb")
		                    ? gap : head);
	}
	else p = gwm_lit(p, end, head);

	p = gwm_lit(p, end, line);

	return (size_t)(p - out);
}

#ifndef GWM_HOST_TEST

#include <cell/cell_fs.h>

static char gwm_autoexec_buf[GWM_AUTOEXEC_MAX];
static char gwm_autoexec_new[GWM_AUTOEXEC_MAX];

/* Возвращает 1, если строка была дописана именно сейчас. */
static int gwm_autostart_ensure(void)
{
	int fd;
	uint64_t got = 0, put = 0;

	if(cellFsOpen(GWM_AUTOEXEC_FILE, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		if(cellFsRead(fd, gwm_autoexec_buf, sizeof(gwm_autoexec_buf), &got)
		   != CELL_FS_SUCCEEDED) got = 0;

		cellFsClose(fd);

		/* Файл не поместился целиком — переписывать его нельзя. */
		if(got >= sizeof(gwm_autoexec_buf)) return 0;
	}

	size_t len = gwm_autostart_build(gwm_autoexec_new, sizeof(gwm_autoexec_new),
	                                 gwm_autoexec_buf, (size_t)got);
	if(!len) return 0;

	if(cellFsOpen(GWM_AUTOEXEC_TMP,
	              CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_TRUNC,
	              &fd, NULL, 0) != CELL_FS_SUCCEEDED) return 0;

	int ret = cellFsWrite(fd, gwm_autoexec_new, len, &put);
	cellFsClose(fd);

	/* Записалось не всё — оставляем автозагрузку как была. */
	if(ret != CELL_FS_SUCCEEDED || put != len)
	{
		cellFsUnlink(GWM_AUTOEXEC_TMP);
		return 0;
	}

	cellFsUnlink(GWM_AUTOEXEC_FILE);

	if(cellFsRename(GWM_AUTOEXEC_TMP, GWM_AUTOEXEC_FILE) == CELL_FS_SUCCEEDED)
		return 1;

	/* Переименование не прошло, а старого файла уже нет. Новое содержимое
	 * включает в себя и чужие строки целиком, поэтому кладём его на место
	 * напрямую: остаться без autoexec.bat человек из-за нас не должен. */
	if(cellFsOpen(GWM_AUTOEXEC_FILE,
	              CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_TRUNC,
	              &fd, NULL, 0) != CELL_FS_SUCCEEDED) return 0;

	put = 0;
	ret = cellFsWrite(fd, gwm_autoexec_new, len, &put);
	cellFsClose(fd);

	return (ret == CELL_FS_SUCCEEDED && put == len);
}

#endif /* GWM_HOST_TEST */

#endif /* GWM_AUTOSTART_H */
