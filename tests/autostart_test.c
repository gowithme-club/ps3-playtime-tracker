/* Хостовый тест самопрописи в автозагрузку.
 *
 * Плагин переписывает /dev_hdd0/autoexec.bat — файл, который ему не
 * принадлежит: там живут чужие плагины, и ошибка стоит человеку
 * автозагрузки целиком. Поэтому сборка нового содержимого проверяется
 * здесь, а не на консоли.
 *
 *   make test-host
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "autostart.h"

static int failures = 0;

static void check(const char *what, const char *got, const char *want)
{
	if(strcmp(got, want) == 0) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s\n    получено:  «%s»\n    ожидалось: «%s»\n", what, got, want);
	failures++;
}

static const char *build(char *out, size_t size, const char *existing)
{
	size_t n = gwm_autostart_build(out, size, existing, strlen(existing));

	out[n] = 0;
	return out;
}

int main(void)
{
	char buf[GWM_AUTOEXEC_MAX];

	printf("файла нет:\n");

	check("пишем полный блок с ожиданием XMB",
	      build(buf, sizeof(buf), ""),
	      "wait xmb\r\nwait 5\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx\r\n");

	printf("файл с чужими плагинами:\n");

	check("дописываемся в конец, чужое на месте",
	      build(buf, sizeof(buf),
	            "wait xmb\r\nwait 5\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/playtime_mk.sprx\r\n"),
	      "wait xmb\r\nwait 5\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/playtime_mk.sprx\r\n"
	      "wait 3\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx\r\n");

	check("нет последнего перевода строки — добавляем свой",
	      build(buf, sizeof(buf),
	            "wait xmb\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/playtime_mk.sprx"),
	      "wait xmb\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/playtime_mk.sprx\r\n"
	      "wait 3\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx\r\n");

	/* Без ожидания XMB команда может выполниться раньше, чем система готова,
	 * а ранняя загрузка плагина роняет консоль. */
	check("в чужом файле нет wait xmb — добавляем своё ожидание",
	      build(buf, sizeof(buf), "/loadprx.ps3?prx=/dev_hdd0/plugins/playtime_mk.sprx\r\n"),
	      "/loadprx.ps3?prx=/dev_hdd0/plugins/playtime_mk.sprx\r\n"
	      "wait xmb\r\nwait 5\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx\r\n");

	printf("строка уже есть:\n");

	{
		const char *same =
			"wait xmb\r\nwait 5\r\n/loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx\r\n";

		if(gwm_autostart_build(buf, sizeof(buf), same, strlen(same)) == 0)
			printf("  ok   второй раз не дописываемся\n");
		else { printf("  FAIL продублировали строку\n"); failures++; }
	}

	{
		/* Закомментированная строка тоже считается: человек её выключил
		 * намеренно, и возвращать её без спроса нельзя. */
		const char *off = "# /loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx\r\n";

		if(gwm_autostart_build(buf, sizeof(buf), off, strlen(off)) == 0)
			printf("  ok   выключенную строку не воскрешаем\n");
		else { printf("  FAIL переписали выключенную строку\n"); failures++; }
	}

	printf("границы:\n");

	{
		/* Не влезаем — не трогаем вовсе: обрезанный autoexec.bat означает
		 * потерю чужой автозагрузки. */
		char big[GWM_AUTOEXEC_MAX];
		size_t fill = sizeof(big) - 40;

		memset(big, 'x', fill);
		big[fill] = 0;

		if(gwm_autostart_build(buf, sizeof(buf), big, fill) == 0)
			printf("  ok   слишком большой файл не переписываем\n");
		else { printf("  FAIL обрезали чужой файл\n"); failures++; }
	}

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
