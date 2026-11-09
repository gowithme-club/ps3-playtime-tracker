/* Отправка на сервер.
 *
 * Две вещи из docs/ingest-api-v1-client.md:
 *   - POST /ingest/v1/sessions — батч закрытых сессий из очереди;
 *   - POST /ingest/v1/heartbeat — «кто играет прямо сейчас», раз в ~60 секунд.
 *
 * Тело ответа не разбирается: по контракту достаточно кода. GET /sync и
 * дозаливка по watermark не реализованы намеренно — источник истины у нас
 * файл очереди на HDD, а сервер идемпотентен. Подробности в docs/DEVELOPMENT.ru.md.
 */

#ifndef GWM_UPLOAD_H
#define GWM_UPLOAD_H

#include <stdint.h>
#include <stddef.h>

#include "config.h"
#include "http.h"
#include "queue.h"
#include "session.h"

#define GWM_PATH_SESSIONS  "/ingest/v1/sessions"
#define GWM_PATH_HEARTBEAT "/ingest/v1/heartbeat"

/* Сколько очереди отправляем за раз. Лимиты сервера (500 сессий, 1 МБ)
 * сильно выше, но держать такой буфер в плагине незачем. */
#define GWM_BATCH_READ 4096
#define GWM_BATCH_BODY 5120

static char gwm_batch_lines[GWM_BATCH_READ];
static char gwm_batch_body[GWM_BATCH_BODY];
static char gwm_hb_body[GWM_RECORD_MAX];

/* Сдвигает метки времени в прочитанном куске очереди на текущую поправку часов.
 *
 * На диск сессии ложатся сырым временем консоли: в офлайне поправку взять
 * неоткуда — она приходит с заголовком Date, то есть только вместе с ответом
 * сервера. К моменту отправки поправка уже есть, и применить её надо в том
 * числе к сессиям недельной давности.
 *
 * Ширина метки фиксирована (20 символов), поэтому правка идёт на месте, без
 * пересборки строки и без разбора JSON: ищем имя поля, сдвигаем то, что за ним.
 * Случай «часы прыгнули, пока консоль стояла без сети» этим не лечится —
 * такой информации на консоли просто нет.
 */
static void gwm_batch_fix_times(char *buf, size_t len)
{
	static const char *keys[2] = { "\"startedAt\":\"", "\"endedAt\":\"" };
	static const size_t key_len[2] = { 13, 11 };

	if(!gwm_clock_synced || !gwm_clock_offset) return;

	for(size_t i = 0; i < len; i++)
	{
		for(int k = 0; k < 2; k++)
		{
			size_t kl = key_len[k];

			if(i + kl + GWM_ISO_LEN - 1 > len) continue;
			if(gwm_strncmp(buf + i, keys[k], kl) != 0) continue;

			gwm_iso_shift(buf + i + kl, gwm_clock_offset);

			i += kl + GWM_ISO_LEN - 2; /* цикл добавит ещё единицу */
			break;
		}
	}
}

/* Склеивает строки очереди в {"sessions":[...]}.
 * Возвращает число сессий в батче. */
static int gwm_batch_build(const char *lines, size_t lines_len,
                           char *out, size_t out_size, size_t *out_len)
{
	char *p = out;
	const char *end = out + out_size;
	int count = 0;

	p = gwm_lit(p, end, "{\"sessions\":[");

	const char *line = lines;
	const char *stop = lines + lines_len;

	while(line < stop)
	{
		const char *nl = line;
		while(nl < stop && *nl != '\n') nl++;

		if(nl > line)
		{
			if(count) p = gwm_lit(p, end, ",");

			/* Строка уже готовый JSON-объект — копируем как есть. */
			for(const char *q = line; q < nl && p < end - 1; q++) *p++ = *q;
			*p = 0;
			count++;
		}

		line = nl + 1;
	}

	p = gwm_lit(p, end, "]}");
	*out_len = (size_t)(p - out);

	return count;
}

/* Возвращает HTTP-код или GWM_HTTP_ERR_*. 0 — отправлять было нечего. */
static int gwm_upload_sessions(const gwm_config_t *cfg)
{
	size_t body_len = 0;

	uint64_t len = gwm_queue_read(gwm_batch_lines, sizeof(gwm_batch_lines));
	if(!len) return 0;

	/* В очереди сырое время консоли — поправку добавляем здесь. */
	gwm_batch_fix_times(gwm_batch_lines, (size_t)len);

	int count = gwm_batch_build(gwm_batch_lines, (size_t)len,
	                            gwm_batch_body, sizeof(gwm_batch_body), &body_len);
	if(!count) return 0;

	int code = gwm_http_post(cfg, GWM_PATH_SESSIONS, gwm_batch_body, body_len);

	gwm_status_sessions(code);

	/* 200 — принято. 400 и 413 повторять бессмысленно: тело не станет лучше,
	 * а вечный ретрай намертво заклинит очередь. Всё остальное (сеть, 429,
	 * 5xx, 401) оставляем в очереди и пробуем позже. */
	if(code == 200 || code == 400 || code == 413) gwm_queue_consume(len);

	return code;
}

static int gwm_send_heartbeat(const gwm_config_t *cfg, const gwm_session_t *s)
{
	char sent_at[GWM_ISO_LEN];
	char started_at[GWM_ISO_LEN];
	size_t len;

	gwm_iso_from_tick(sent_at, gwm_tick_now());

	/* Сессия хранит сырой тик: поправку добавляем здесь, чтобы heartbeat
	 * всегда уходил с самой свежей из выученных. */
	gwm_iso_from_tick(started_at, gwm_tick_correct(s->start_tick));

	len = gwm_build_heartbeat(gwm_hb_body, sizeof(gwm_hb_body), sent_at,
	                          s->active, s->account, s->title_id, s->title,
	                          started_at);

	int code = gwm_http_post(cfg, GWM_PATH_HEARTBEAT, gwm_hb_body, len);

	gwm_status_hb(code);

	return code;
}

#endif /* GWM_UPLOAD_H */
