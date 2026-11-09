/* Поправка часов по времени сервера.
 *
 * На CFW-консолях часы врут: батарейка, отсутствие синхронизации, ручная
 * установка «чтобы игра запустилась». Сервер такие записи отбракует (таймстемп
 * из будущего), а клиент об этом не узнает — тело ответа он не разбирает.
 *
 * Поэтому время берём у сервера. Причём не из тела: в каждом HTTP-ответе уже
 * есть стандартный заголовок Date, его шлёт любой сервер. Достаточно найти
 * подстроку в том, что и так прочитано, — ни парсера JSON, ни изменений
 * в контракте.
 *
 * Смещение считается один раз на первом ответе и применяется ко всем меткам.
 * Разбор даты отделён от RTC и проверяется хостовым тестом.
 */

#ifndef GWM_CLOCK_H
#define GWM_CLOCK_H

#include <stdint.h>
#include <stddef.h>

#include "str.h"
#include "isotime.h"

typedef struct
{
	unsigned year, month, day, hour, minute, second;
} gwm_time_t;

static int gwm_month_by_name(const char *m)
{
	static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

	for(int i = 0; i < 12; i++)
		if(m[0] == names[i * 3] && m[1] == names[i * 3 + 1] && m[2] == names[i * 3 + 2])
			return i + 1;

	return 0;
}

static unsigned gwm_num(const char *s, int len)
{
	unsigned v = 0;
	for(int i = 0; i < len; i++)
	{
		if(s[i] < '0' || s[i] > '9') return 0;
		v = v * 10 + (unsigned)(s[i] - '0');
	}
	return v;
}

/* Ищет заголовок Date в ответе и раскладывает его.
 * Формат по RFC 7231: "Date: Sat, 16 Aug 2026 21:45:00 GMT".
 * Возвращает 1 при успехе. */
static int gwm_parse_http_date(const char *resp, size_t len, gwm_time_t *out)
{
	/* Ищем начало строки заголовка, а не любое вхождение слова. */
	const char *p = NULL;

	for(size_t i = 0; i + 6 < len; i++)
	{
		int line_start = (i == 0) || (resp[i - 1] == '\n');
		if(!line_start) continue;

		if((resp[i] == 'D' || resp[i] == 'd') &&
		   (resp[i + 1] == 'a' || resp[i + 1] == 'A') &&
		   (resp[i + 2] == 't' || resp[i + 2] == 'T') &&
		   (resp[i + 3] == 'e' || resp[i + 3] == 'E') &&
		   resp[i + 4] == ':')
		{
			p = resp + i + 5;
			break;
		}
	}

	if(!p) return 0;

	while(*p == ' ') p++;

	/* Пропускаем день недели с запятой: "Sat, " */
	while(*p && *p != ',' && *p != ' ') p++;
	if(*p == ',') p++;
	while(*p == ' ') p++;

	/* Дальше строго: DD Mon YYYY HH:MM:SS */
	if((size_t)(p - resp) + 20 > len) return 0;

	out->day = gwm_num(p, 2);
	int month = gwm_month_by_name(p + 3);
	out->month = (unsigned)month;
	out->year = gwm_num(p + 7, 4);
	out->hour = gwm_num(p + 12, 2);
	out->minute = gwm_num(p + 15, 2);
	out->second = gwm_num(p + 18, 2);

	if(!month || !out->day || out->day > 31 || out->year < 2000 ||
	   out->hour > 23 || out->minute > 59 || out->second > 60)
		return 0;

	return 1;
}

/* Смещение часов консоли относительно сервера, в микросекундах.
 * Знаковое: часы могут и отставать, и убегать. */
static int64_t gwm_clock_offset = 0;
static int gwm_clock_synced = 0;

/* И заголовок Date, и часы консоли имеют разрешение в одну секунду, а
 * замеряются с зазором. Поэтому даже идеально синхронные часы дают смещение,
 * скачущее между двумя соседними целыми — в зависимости от того, где каждые
 * из них оказались внутри своей секунды.
 *
 * Принимать этот дребезг нельзя: startedAt и endedAt форматируются в разные
 * моменты, и на короткой сессии конец может оказаться раньше начала — сервер
 * такую строку отбракует. На Switch этот же дребезг приводил к дублю сессии.
 *
 * Дрейф при этом не теряется: сравнение идёт с принятым значением, а не с
 * предыдущим замером, поэтому реально убегающие часы копят разницу, пока она
 * не перешагнёт порог, и тогда поправка берётся целиком.
 *
 * Приём заимствован из gwm-stats-switch.
 */
#define GWM_CLOCK_HYSTERESIS_USEC 1000000

static void gwm_clock_adopt(int64_t new_offset)
{
	if(!gwm_clock_synced)
	{
		gwm_clock_offset = new_offset;
		gwm_clock_synced = 1;
		return;
	}

	int64_t delta = new_offset - gwm_clock_offset;
	if(delta < 0) delta = -delta;

	if(delta <= GWM_CLOCK_HYSTERESIS_USEC) return;

	gwm_clock_offset = new_offset;
}

#ifndef GWM_HOST_TEST

static uint64_t gwm_tick_from_time(const gwm_time_t *t)
{
	CellRtcDateTime d;
	CellRtcTick tick;

	d.year = (unsigned short)t->year;
	d.month = (unsigned short)t->month;
	d.day = (unsigned short)t->day;
	d.hour = (unsigned short)t->hour;
	d.minute = (unsigned short)t->minute;
	d.second = (unsigned short)t->second;
	d.microsecond = 0;

	cellRtcGetTick(&d, &tick);
	return tick.tick;
}

/* Вызывается с телом ответа сервера. */
static void gwm_clock_sync(const char *resp, size_t len)
{
	gwm_time_t t;

	if(!gwm_parse_http_date(resp, len, &t)) return;

	uint64_t server_tick = gwm_tick_from_time(&t);
	uint64_t local_tick = gwm_tick_raw();

	gwm_clock_adopt((int64_t)server_tick - (int64_t)local_tick);
}

/* Поправка, применённая к произвольному тику. Отдельно от gwm_tick_now,
 * потому что править приходится и время из прошлого: сессия, закрытая
 * в офлайне, записана сырыми часами, а поправка появляется только когда
 * консоль снова увидит сервер. */
static uint64_t gwm_tick_correct(uint64_t raw)
{
	if(!gwm_clock_synced) return raw;

	int64_t corrected = (int64_t)raw + gwm_clock_offset;
	return (corrected > 0) ? (uint64_t)corrected : raw;
}

/* Время с поправкой — им пользуется весь остальной код. */
static uint64_t gwm_tick_now(void)
{
	return gwm_tick_correct(gwm_tick_raw());
}

#endif /* GWM_HOST_TEST */

#endif /* GWM_CLOCK_H */
