/* Время в формате ISO 8601 — его ждёт сервер (docs/ingest-api-v1-client.md).
 *
 * Форматирование отделено от RTC намеренно: gwm_iso_format работает с обычными
 * числами и потому проверяется хостовым тестом, а тонкая обёртка над
 * cellRtcSetTick живёт отдельно и на хосте не собирается.
 *
 * Ниже есть ещё и обратное преобразование — из метки обратно в тик, целиком
 * на целочисленной арифметике. Оно нужно, чтобы сдвигать уже записанные
 * в очередь метки на поправку часов, выученную позже (см. upload.h).
 * Собирается и на хосте, поэтому проверяется тестом на настоящих значениях,
 * снятых с консоли.
 */

#ifndef GWM_ISOTIME_H
#define GWM_ISOTIME_H

#include <stdint.h>
#include <stddef.h>

/* "2026-07-05T18:30:00Z" + терминатор */
#define GWM_ISO_LEN 21

static void gwm_pad2(char *out, unsigned v)
{
	out[0] = (char)('0' + (v / 10) % 10);
	out[1] = (char)('0' + v % 10);
}

/* Часы консоли отдают UTC, поэтому суффикс всегда Z. */
static void gwm_iso_format(char out[GWM_ISO_LEN],
                           unsigned year, unsigned month, unsigned day,
                           unsigned hour, unsigned minute, unsigned second)
{
	out[0] = (char)('0' + (year / 1000) % 10);
	out[1] = (char)('0' + (year / 100) % 10);
	out[2] = (char)('0' + (year / 10) % 10);
	out[3] = (char)('0' + year % 10);
	out[4] = '-';
	gwm_pad2(out + 5, month);
	out[7] = '-';
	gwm_pad2(out + 8, day);
	out[10] = 'T';
	gwm_pad2(out + 11, hour);
	out[13] = ':';
	gwm_pad2(out + 14, minute);
	out[16] = ':';
	gwm_pad2(out + 17, second);
	out[19] = 'Z';
	out[20] = 0;
}

/* Тик RTC — микросекунды от 0001-01-01. Суток от этой даты до 1970-01-01
 * ровно 719162, отсюда и константа: 719162 * 86400 = 62135596800 секунд.
 * Проверено на настоящих трофейных метках с консоли, см. хостовый тест. */
#define GWM_DAYS_0001_TO_1970 719162
#define GWM_USEC_PER_SEC      1000000ULL
#define GWM_SEC_PER_DAY       86400ULL

/* Дней от 1970-01-01 до y-m-d в пролептическом григорианском календаре.
 * Алгоритм Хиннанта: без таблиц и без циклов по годам. */
static int64_t gwm_days_from_civil(int64_t y, unsigned m, unsigned d)
{
	y -= (m <= 2);

	int64_t era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);                      /* 0..399   */
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* 0..365   */
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          /* 0..146096 */

	return era * 146097 + (int64_t)doe - 719468;
}

/* Обратное преобразование, тот же алгоритм. */
static void gwm_civil_from_days(int64_t z, int64_t *y, unsigned *m, unsigned *d)
{
	z += 719468;

	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp  = (5 * doy + 2) / 153;

	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = (int64_t)yoe + era * 400 + (*m <= 2);
}

/* Разбирает нашу же метку. Формат фиксированный, поэтому читаем по смещениям
 * и проверяем разделители — мусор в очереди сдвигать нельзя. */
static int gwm_iso_parse(const char *s, unsigned out[6])
{
	static const int pos[6] = { 0, 5, 8, 11, 14, 17 };
	static const int wid[6] = { 4, 2, 2, 2, 2, 2 };

	if(s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
	   s[13] != ':' || s[16] != ':' || s[19] != 'Z') return 0;

	for(int f = 0; f < 6; f++)
	{
		unsigned v = 0;
		for(int i = 0; i < wid[f]; i++)
		{
			char c = s[pos[f] + i];
			if(c < '0' || c > '9') return 0;
			v = v * 10 + (unsigned)(c - '0');
		}
		out[f] = v;
	}

	if(!out[1] || out[1] > 12 || !out[2] || out[2] > 31 ||
	   out[3] > 23 || out[4] > 59 || out[5] > 60) return 0;

	return 1;
}

/* Метка → тик. Возвращает 0, если метка не разобралась. */
static uint64_t gwm_tick_from_iso(const char *s)
{
	unsigned f[6];

	if(!gwm_iso_parse(s, f)) return 0;

	int64_t days = gwm_days_from_civil((int64_t)f[0], f[1], f[2]) +
	               GWM_DAYS_0001_TO_1970;

	uint64_t secs = (uint64_t)days * GWM_SEC_PER_DAY +
	                (uint64_t)f[3] * 3600 + (uint64_t)f[4] * 60 + f[5];

	return secs * GWM_USEC_PER_SEC;
}

/* Тик → метка, без обращения к RTC. */
static void gwm_iso_from_tick_pure(char out[GWM_ISO_LEN], uint64_t tick)
{
	uint64_t secs = tick / GWM_USEC_PER_SEC;
	int64_t  days = (int64_t)(secs / GWM_SEC_PER_DAY) - GWM_DAYS_0001_TO_1970;
	unsigned rem  = (unsigned)(secs % GWM_SEC_PER_DAY);

	int64_t y;
	unsigned m, d;

	gwm_civil_from_days(days, &y, &m, &d);

	gwm_iso_format(out, (unsigned)y, m, d, rem / 3600, (rem / 60) % 60, rem % 60);
}

/* Сдвигает метку на месте. Ширина метки от сдвига не меняется, поэтому
 * правка идёт прямо в буфере. Возвращает 0, если метка не разобралась —
 * тогда буфер остаётся нетронутым.
 *
 * Метка может стоять посреди чужой строки (в JSON за ней идёт кавычка),
 * а форматтер дописывает терминатор двадцать первым байтом. Поэтому байт
 * за меткой сохраняется и возвращается на место. */
static int gwm_iso_shift(char *iso, int64_t usec)
{
	uint64_t tick = gwm_tick_from_iso(iso);

	if(!tick) return 0;

	int64_t moved = (int64_t)tick + usec;
	if(moved <= 0) return 0;

	char after = iso[GWM_ISO_LEN - 1];
	gwm_iso_from_tick_pure(iso, (uint64_t)moved);
	iso[GWM_ISO_LEN - 1] = after;

	return 1;
}

#ifndef GWM_HOST_TEST

#include <cell/rtc.h>

/* Сырые часы консоли. Пользоваться напрямую не надо: они могут врать на годы,
 * поправку по времени сервера добавляет clock.h. */
static uint64_t gwm_tick_raw(void)
{
	CellRtcTick t;
	cellRtcGetCurrentTick(&t); /* UTC */
	return t.tick;
}

static void gwm_iso_from_tick(char out[GWM_ISO_LEN], uint64_t tick)
{
	CellRtcTick t;
	CellRtcDateTime d;

	t.tick = tick;
	cellRtcSetTick(&d, &t);

	gwm_iso_format(out, d.year, d.month, d.day, d.hour, d.minute, d.second);
}

#endif /* GWM_HOST_TEST */

#endif /* GWM_ISOTIME_H */
