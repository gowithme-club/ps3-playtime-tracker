/* Сборка тел запросов к серверу — по docs/ingest-api-v1-client.md.
 *
 * Две штуки:
 *   - объект закрытой сессии; такие строки копятся в очереди на диске
 *     и потом склеиваются в батч POST /ingest/v1/sessions;
 *   - тело heartbeat со списком того, во что играют прямо сейчас.
 *
 * Всё это чистые строки, поэтому проверяется на хосте.
 */

#ifndef GWM_RECORD_H
#define GWM_RECORD_H

#include <stdint.h>
#include <stddef.h>

#include "json.h"
#include "isotime.h"
#include "clock.h"

#define GWM_PLATFORM "ps3"

/* Одна сессия из очереди помещается сюда с запасом на длинное название. */
#define GWM_RECORD_MAX 512

/* Объект закрытой сессии. Обязательные поля по документу: account, title,
 * platform, startedAt, endedAt; titleId желателен. */
static size_t gwm_build_session(char *out, size_t out_size,
                                const char *account,
                                const char *title_id, const char *title,
                                const char *started_at, const char *ended_at,
                                uint32_t seconds)
{
	char *p = out;
	const char *end = out + out_size;

	p = gwm_lit(p, end, "{");
	p = gwm_json_kv(p, end, "account", account);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "titleId", title_id);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "title", title);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "platform", GWM_PLATFORM);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "startedAt", started_at);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "endedAt", ended_at);
	p = gwm_lit(p, end, ",\"seconds\":");
	p = gwm_u64(p, end, seconds);
	p = gwm_lit(p, end, "}");

	return (size_t)(p - out);
}

/* Тело heartbeat. current пустой, если никто не играет, — так и задумано
 * документом: это признак «консоль включена, но игры нет». */
static size_t gwm_build_heartbeat(char *out, size_t out_size,
                                  const char *sent_at,
                                  int playing,
                                  const char *account,
                                  const char *title_id, const char *title,
                                  const char *started_at)
{
	char *p = out;
	const char *end = out + out_size;

	p = gwm_lit(p, end, "{");
	p = gwm_json_kv(p, end, "sentAt", sent_at);
	p = gwm_lit(p, end, ",\"current\":[");

	if(playing)
	{
		p = gwm_lit(p, end, "{");
		p = gwm_json_kv(p, end, "account", account);
		p = gwm_lit(p, end, ",");
		p = gwm_json_kv(p, end, "titleId", title_id);
		p = gwm_lit(p, end, ",");
		p = gwm_json_kv(p, end, "titleName", title);
		p = gwm_lit(p, end, ",");
		p = gwm_json_kv(p, end, "platform", GWM_PLATFORM);
		p = gwm_lit(p, end, ",");
		p = gwm_json_kv(p, end, "startedAt", started_at);
		p = gwm_lit(p, end, "}");
	}

	p = gwm_lit(p, end, "]}");

	return (size_t)(p - out);
}

/* Тело POST /ingest/v1/trophies — полное состояние одного сета.
 * Собирается в три приёма, потому что список трофеев читается из файла
 * поштучно и целиком в памяти не лежит. */
static size_t gwm_trophies_begin(char *out, size_t out_size,
                                 const char *account, const char *set_id,
                                 const char *title)
{
	char *p = out;
	const char *end = out + out_size;

	p = gwm_lit(p, end, "{");
	p = gwm_json_kv(p, end, "account", account);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "platform", GWM_PLATFORM);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "setId", set_id);

	/* Название знаем только если сет принадлежит текущей игре: из TROPUSR.DAT
	 * его не достать, а TROPCONF.SFM по документу разбирать не нужно —
	 * сервер дообогащает данные через PSN по setId. */
	if(title && *title)
	{
		p = gwm_lit(p, end, ",");
		p = gwm_json_kv(p, end, "title", title);
	}

	p = gwm_lit(p, end, ",\"trophies\":[");
	return (size_t)(p - out);
}

static size_t gwm_trophies_add(char *out, size_t out_size, size_t len,
                               int first, uint32_t id, const char *earned_at)
{
	char *p = out + len;
	const char *end = out + out_size;

	if(!first) p = gwm_lit(p, end, ",");

	p = gwm_lit(p, end, "{\"id\":");
	p = gwm_u64(p, end, id);
	p = gwm_lit(p, end, ",");
	p = gwm_json_kv(p, end, "earnedAt", earned_at);
	p = gwm_lit(p, end, "}");

	return (size_t)(p - out);
}

static size_t gwm_trophies_end(char *out, size_t out_size, size_t len)
{
	char *p = gwm_lit(out + len, out + out_size, "]}");
	return (size_t)(p - out);
}

#endif /* GWM_RECORD_H */
