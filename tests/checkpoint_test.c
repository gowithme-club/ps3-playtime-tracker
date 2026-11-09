/* Хостовый тест снимка открытой сессии.
 *
 * Снимок читается при загрузке после выключения посреди игры — то есть
 * ровно тогда, когда запись могла оборваться на полуслове. Битый снимок
 * в очередь попасть не должен: сервер отбракует весь батч, а с ним и
 * соседние целые сессии. Проверяется здесь же и перевод исправленного
 * времени в сырое время нового запуска: туда и обратно метки обязаны
 * вернуться байт в байт.
 *
 *   make test-host
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "isotime.h"
#include "checkpoint.h"

static int failures = 0;

static void ok(int cond, const char *what)
{
	printf("  %s   %s\n", cond ? "ok  " : "FAIL", what);
	if(!cond) failures++;
}

#define LINE "{\"account\":\"SamplePlayer\",\"titleId\":\"BLES01393\"," \
             "\"title\":\"ACE COMBAT ASSAULT HORIZON\",\"platform\":\"ps3\"," \
             "\"startedAt\":\"2026-10-11T12:45:53Z\"," \
             "\"endedAt\":\"2026-10-11T15:38:07Z\",\"seconds\":10334}\n"

static size_t parse(const char *buf, char *marker)
{
	return gwm_checkpoint_parse(buf, strlen(buf), marker);
}

int main(void)
{
	char marker = 0;

	printf("разбор снимка:\n");
	{
		size_t len = parse("c" LINE, &marker);
		ok(len == strlen(LINE) && marker == 'c', "исправленный снимок: строка целиком");

		len = parse("r" LINE, &marker);
		ok(len == strlen(LINE) && marker == 'r', "сырой снимок: строка целиком");
	}

	{
		char cut[sizeof("c" LINE)];

		strcpy(cut, "c" LINE);
		cut[strlen(cut) - 1] = 0;
		ok(!parse(cut, &marker), "без перевода строки — запись оборвалась");

		strcpy(cut, "c" LINE);
		cut[40] = 0;
		ok(!parse(cut, &marker), "обрезан посередине");
	}

	ok(!parse("", &marker), "пустой файл");
	ok(!parse("x" LINE, &marker), "неизвестный признак");
	ok(!parse(LINE, &marker), "строка без признака");
	ok(!parse("c" LINE LINE, &marker), "две строки вместо одной");

	{
		/* Нули вместо хвоста — так выглядит файл, место под который
		 * выделили, а записать не успели. */
		char buf[] = "c{\"account\":\"x\"\0\0\0}\n";
		ok(!gwm_checkpoint_parse(buf, sizeof(buf) - 1, &marker), "нули посреди строки");
	}

	printf("время между запусками:\n");
	{
		char line[] = LINE;
		const int64_t offset = -90LL * 1000000LL; /* часы спешили на полторы минуты */

		/* Восстановление: из исправленного времени в сырое нового запуска… */
		gwm_times_shift(line, strlen(line), -offset);
		ok(strstr(line, "\"startedAt\":\"2026-10-11T12:47:23Z\"") != NULL &&
		   strstr(line, "\"endedAt\":\"2026-10-11T15:39:37Z\"") != NULL,
		   "в очередь ложится сырое время нового запуска");
		ok(strstr(line, "\"seconds\":10334}") != NULL, "длительность не тронута");

		/* …а отправка добавляет поправку обратно. */
		gwm_times_shift(line, strlen(line), offset);
		ok(strcmp(line, LINE) == 0, "на сервер уходит то же, что было в снимке");
	}

	printf(failures ? "ЕСТЬ ПРОВАЛЫ\n" : "снимок сессии: OK\n");
	return failures;
}
