/* Хостовый тест офлайнового времени.
 *
 * Проверяется то, ради чего вообще затевалась правка: сессия, закрытая без
 * сети, записывается сырыми часами консоли, а поправка применяется к ней
 * позже — при отправке.
 *
 * Ключевой риск здесь — эпоха. Тик RTC считается от 0001-01-01, и чистая
 * арифметика обязана давать ровно те же значения, что cellRtc на консоли.
 * Поэтому первым делом проверяются настоящие тики, снятые с TROPUSR.DAT
 * живой консоли: их разбор уже подтверждён и питоном, и самим плагином.
 *
 *   make test-host
 */

#define GWM_HOST_TEST

#include <stdio.h>
#include <string.h>

#include "str.h"
#include "isotime.h"

static int failed = 0;

static void ok(int cond, const char *what)
{
	printf("  %s   %s\n", cond ? "ok  " : "ПРОВАЛ", what);
	if(!cond) failed = 1;
}

/* Настоящие пары «тик → метка» из трофейного файла консоли. */
static const struct { unsigned long long tick; const char *iso; } anchors[] = {
	{ 63922512032000000ULL, "2026-08-16T21:20:32Z" },
	{ 63922512742000000ULL, "2026-08-16T21:32:22Z" },
	{ 63922512906000000ULL, "2026-08-16T21:35:06Z" },
	{ 63922512966000000ULL, "2026-08-16T21:36:06Z" },
	{ 63922513099000000ULL, "2026-08-16T21:38:19Z" },
	{ 63922513332000000ULL, "2026-08-16T21:42:12Z" },
};

/* То же, что делает gwm_batch_fix_times с известной поправкой. */
static void fix_times(char *buf, size_t len, long long offset)
{
	gwm_times_shift(buf, len, offset);
}

int main(void)
{
	char iso[GWM_ISO_LEN];

	printf("эпоха тика RTC (настоящие значения с консоли):\n");
	{
		int all_fwd = 1, all_back = 1;

		for(unsigned i = 0; i < sizeof(anchors) / sizeof(anchors[0]); i++)
		{
			gwm_iso_from_tick_pure(iso, anchors[i].tick);
			if(strcmp(iso, anchors[i].iso) != 0)
			{
				printf("      тик %llu дал %s, ждали %s\n",
				       anchors[i].tick, iso, anchors[i].iso);
				all_fwd = 0;
			}

			if(gwm_tick_from_iso(anchors[i].iso) != anchors[i].tick)
				all_back = 0;
		}

		ok(all_fwd, "тик разворачивается в ту же метку, что на консоли");
		ok(all_back, "метка сворачивается обратно в тот же тик");
	}

	printf("календарь:\n");
	{
		/* Високосный год, конец месяца, конец года, переход суток. */
		static const struct { unsigned long long tick; const char *iso; } cases[] = {
			{ 63087379200000000ULL, "2000-02-29T00:00:00Z" }, /* високосный  */
			{ 63844847999000000ULL, "2024-02-29T23:59:59Z" }, /* и ещё один  */
			{ 63087292800000000ULL, "2000-02-28T00:00:00Z" },
			{ 62135596800000000ULL, "1970-01-01T00:00:00Z" }, /* якорь эпохи */
		};
		int good = 1;

		for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
		{
			gwm_iso_from_tick_pure(iso, cases[i].tick);
			if(strcmp(iso, cases[i].iso) != 0)
			{
				printf("      %llu дал %s, ждали %s\n",
				       cases[i].tick, iso, cases[i].iso);
				good = 0;
			}
		}
		ok(good, "високосные даты и граница эпохи");
	}

	{
		/* Круговой проход: год с лишним по часу — все даты обязаны
		 * сворачиваться и разворачиваться без потерь. */
		int good = 1;
		unsigned long long t = 63922512032000000ULL;

		for(int i = 0; i < 10000; i++)
		{
			gwm_iso_from_tick_pure(iso, t);
			if(gwm_tick_from_iso(iso) != t) { good = 0; break; }
			t += 3600ULL * 1000000ULL;
		}
		ok(good, "десять тысяч часов подряд сворачиваются без потерь");
	}

	printf("разбор мусора:\n");
	{
		ok(gwm_tick_from_iso("2026-08-16 21:20:32Z") == 0, "не тот разделитель");
		ok(gwm_tick_from_iso("2026-08-16T21:20:32+") == 0, "нет Z на конце");
		ok(gwm_tick_from_iso("20x6-08-16T21:20:32Z") == 0, "буква вместо цифры");
		ok(gwm_tick_from_iso("2026-13-16T21:20:32Z") == 0, "тринадцатый месяц");
		ok(gwm_tick_from_iso("2026-08-16T24:20:32Z") == 0, "двадцать четвёртый час");
	}

	printf("сдвиг метки на месте:\n");
	{
		char line[] = "{\"startedAt\":\"2026-08-16T21:20:32Z\"}";

		gwm_iso_shift(line + 14, -60ULL * 1000000LL);
		ok(strcmp(line, "{\"startedAt\":\"2026-08-16T21:19:32Z\"}") == 0,
		   "минута назад, кавычка на месте");

		gwm_iso_shift(line + 14, 60ULL * 1000000LL);
		ok(strcmp(line, "{\"startedAt\":\"2026-08-16T21:20:32Z\"}") == 0,
		   "и обратно, посимвольно то же самое");
	}

	printf("правка батча очереди:\n");
	{
		/* Две сессии, как они лежат в queue.jsonl, плюс поле, которое
		 * трогать нельзя. */
		char buf[] =
			"{\"account\":\"SamplePlayer\",\"titleId\":\"BLES00652\","
			"\"title\":\"Midnight Club\",\"platform\":\"ps3\","
			"\"startedAt\":\"2026-08-16T22:14:18Z\","
			"\"endedAt\":\"2026-08-16T22:20:18Z\",\"seconds\":360}\n"
			"{\"account\":\"Eplring\",\"titleId\":\"BCES00609\","
			"\"title\":\"inFamous\",\"platform\":\"ps3\","
			"\"startedAt\":\"2026-08-16T23:00:00Z\","
			"\"endedAt\":\"2026-08-16T23:01:50Z\",\"seconds\":110}\n";

		const char *want =
			"{\"account\":\"SamplePlayer\",\"titleId\":\"BLES00652\","
			"\"title\":\"Midnight Club\",\"platform\":\"ps3\","
			"\"startedAt\":\"2026-08-16T22:13:18Z\","
			"\"endedAt\":\"2026-08-16T22:19:18Z\",\"seconds\":360}\n"
			"{\"account\":\"Eplring\",\"titleId\":\"BCES00609\","
			"\"title\":\"inFamous\",\"platform\":\"ps3\","
			"\"startedAt\":\"2026-08-16T22:59:00Z\","
			"\"endedAt\":\"2026-08-16T23:00:50Z\",\"seconds\":110}\n";

		fix_times(buf, strlen(buf), -60LL * 1000000LL);
		ok(strcmp(buf, want) == 0, "обе сессии сдвинулись, остальное не тронуто");
	}

	{
		/* Поправки нет — буфер обязан остаться байт в байт. */
		char buf[]  = "{\"startedAt\":\"2026-08-16T22:14:18Z\",\"seconds\":360}\n";
		char same[] = "{\"startedAt\":\"2026-08-16T22:14:18Z\",\"seconds\":360}\n";

		fix_times(buf, strlen(buf), 0);
		ok(strcmp(buf, same) == 0, "без поправки не трогаем ничего");
	}

	{
		/* Обрезанная метка в конце буфера: сдвигать нечего, портить нельзя. */
		char buf[]  = "{\"startedAt\":\"2026-08-16T22:1";
		char same[] = "{\"startedAt\":\"2026-08-16T22:1";

		fix_times(buf, strlen(buf), -60LL * 1000000LL);
		ok(strcmp(buf, same) == 0, "обрезанная метка остаётся как есть");
	}

	{
		/* Мусор на месте метки — не наше дело его чинить. */
		char buf[]  = "{\"endedAt\":\"не-дата-а-ерунда!!\",\"seconds\":1}";
		char same[] = "{\"endedAt\":\"не-дата-а-ерунда!!\",\"seconds\":1}";

		fix_times(buf, strlen(buf), -60LL * 1000000LL);
		ok(strcmp(buf, same) == 0, "мусор вместо метки не трогается");
	}

	printf(failed ? "ЕСТЬ ПРОВАЛЫ\n" : "все проверки прошли\n");
	return failed;
}
