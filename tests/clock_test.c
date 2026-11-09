/* Хостовый тест разбора заголовка Date из ответа сервера.
 *
 * По нему правятся часы консоли, поэтому ошибка здесь означает сессии
 * с неверным временем у всех, у кого сбит RTC.
 *
 *   make test-host
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "clock.h"

static int failures = 0;

static void check_time(const char *what, const char *resp, int want_ok,
                       unsigned y, unsigned mo, unsigned d,
                       unsigned h, unsigned mi, unsigned se)
{
	gwm_time_t t;
	memset(&t, 0, sizeof(t));

	int ok = gwm_parse_http_date(resp, strlen(resp), &t);

	if(ok != want_ok)
	{
		printf("  FAIL %s: разбор %s, ожидался %s\n", what,
		       ok ? "удался" : "не удался", want_ok ? "успех" : "отказ");
		failures++;
		return;
	}

	if(!want_ok) { printf("  ok   %s\n", what); return; }

	if(t.year == y && t.month == mo && t.day == d &&
	   t.hour == h && t.minute == mi && t.second == se)
	{
		printf("  ok   %s\n", what);
		return;
	}

	printf("  FAIL %s\n    получено:  %04u-%02u-%02u %02u:%02u:%02u\n"
	       "    ожидалось: %04u-%02u-%02u %02u:%02u:%02u\n",
	       what, t.year, t.month, t.day, t.hour, t.minute, t.second, y, mo, d, h, mi, se);
	failures++;
}

int main(void)
{
	printf("заголовок Date:\n");

	check_time("обычный ответ",
	           "HTTP/1.1 200 OK\r\n"
	           "Server: nginx\r\n"
	           "Date: Sat, 16 Aug 2026 21:45:00 GMT\r\n"
	           "Content-Length: 42\r\n\r\n",
	           1, 2026, 8, 16, 21, 45, 0);

	check_time("Date первым заголовком",
	           "HTTP/1.1 200 OK\r\nDate: Mon, 01 Jan 2035 00:00:01 GMT\r\n\r\n",
	           1, 2035, 1, 1, 0, 0, 1);

	check_time("нижний регистр",
	           "HTTP/1.1 200 OK\r\ndate: Tue, 31 Dec 2024 23:59:59 GMT\r\n\r\n",
	           1, 2024, 12, 31, 23, 59, 59);

	/* Значение чужого заголовка не должно приниматься за Date. */
	check_time("Last-Modified не путается с Date",
	           "HTTP/1.1 200 OK\r\n"
	           "Last-Modified: Sat, 01 Jan 2000 00:00:00 GMT\r\n"
	           "Date: Sat, 16 Aug 2026 21:45:00 GMT\r\n\r\n",
	           1, 2026, 8, 16, 21, 45, 0);

	check_time("нет заголовка", "HTTP/1.1 200 OK\r\nServer: nginx\r\n\r\n", 0, 0,0,0,0,0,0);
	check_time("битый месяц",
	           "HTTP/1.1 200 OK\r\nDate: Sat, 16 Zzz 2026 21:45:00 GMT\r\n\r\n", 0, 0,0,0,0,0,0);
	check_time("обрезанный заголовок",
	           "HTTP/1.1 200 OK\r\nDate: Sat, 16 Aug", 0, 0,0,0,0,0,0);
	check_time("год из прошлого века",
	           "HTTP/1.1 200 OK\r\nDate: Sat, 16 Aug 1999 21:45:00 GMT\r\n\r\n", 0, 0,0,0,0,0,0);

	printf("месяцы:\n");
	{
		static const char *names[] = {"Jan","Feb","Mar","Apr","May","Jun",
		                              "Jul","Aug","Sep","Oct","Nov","Dec"};
		int ok = 1;
		for(int i = 0; i < 12; i++)
			if(gwm_month_by_name(names[i]) != i + 1) ok = 0;
		if(gwm_month_by_name("Xyz") != 0) ok = 0;

		if(ok) printf("  ok   все двенадцать и отказ на мусоре\n");
		else { printf("  FAIL разбор месяцев\n"); failures++; }
	}

	printf("гистерезис смещения:\n");
	{
		int ok = 1;

		/* Первое измерение принимается как есть. */
		gwm_clock_offset = 0; gwm_clock_synced = 0;
		gwm_clock_adopt(-59000000);
		if(!gwm_clock_synced || gwm_clock_offset != -59000000) ok = 0;

		/* Дребезг в секунду игнорируется — это и есть защита от дублей. */
		gwm_clock_adopt(-59000000 + 900000);
		if(gwm_clock_offset != -59000000) ok = 0;
		gwm_clock_adopt(-59000000 - 1000000);
		if(gwm_clock_offset != -59000000) ok = 0;

		/* Настоящий уход принимается целиком. */
		gwm_clock_adopt(-62000000);
		if(gwm_clock_offset != -62000000) ok = 0;

		if(ok) printf("  ok   дребезг гасится, реальный уход принимается\n");
		else { printf("  FAIL гистерезис: смещение %lld\n", (long long)gwm_clock_offset); failures++; }
	}

	{
		/* Медленный дрейф не теряется: сравнение идёт с принятым значением,
		 * поэтому разница копится и однажды перешагивает порог. */
		gwm_clock_offset = 0; gwm_clock_synced = 0;
		gwm_clock_adopt(0);

		int64_t last = 0;
		for(int i = 1; i <= 4; i++) { last = (int64_t)i * 400000; gwm_clock_adopt(last); }

		/* Смысл не в том, чтобы догнать последний замер, а в том, что ошибка
		 * ограничена порогом: поправка берётся, как только накопленная разница
		 * его перешагнёт. Медленный уход часов при этом не теряется. */
		int64_t err = last - gwm_clock_offset;
		if(err < 0) err = -err;

		if(gwm_clock_offset > 0 && err <= GWM_CLOCK_HYSTERESIS_USEC)
			printf("  ok   дрейф подхватывается, ошибка не больше порога\n");
		else { printf("  FAIL дрейф: смещение %lld, отставание %lld\n",
		              (long long)gwm_clock_offset, (long long)err); failures++; }
	}

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
