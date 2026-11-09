/* Машина состояний сессий.
 *
 * На каждом опросе сравниваем, что запущено сейчас, с тем, что было. Закрытие
 * сессии кладёт в очередь готовый объект для POST /ingest/v1/sessions —
 * ровно в том виде, в каком он уйдёт на сервер, чтобы при отправке ничего
 * не пересобирать.
 *
 * Текущая незакрытая сессия в очередь не попадает: по документу она видна
 * серверу через heartbeat.
 *
 * Длительность считается разностью тиков RTC, а не разностью дат: часы на
 * CFW-консолях нередко сбиты и могут скакнуть, а тик монотонен.
 *
 * Время сессии хранится **сырым тиком**, а не готовой меткой: поправка часов
 * появляется только после ответа сервера, и консоль, неделю простоявшая без
 * сети, иначе записала бы весь улов кривым временем навсегда. Метка собирается
 * в момент использования — в heartbeat с поправкой, в очередь без неё
 * (там её добавят при отправке).
 */

#ifndef GWM_SESSION_H
#define GWM_SESSION_H

#include <stdint.h>

#include "record.h"
#include "queue.h"
#include "account.h"

typedef struct
{
	char     title_id[GWM_TITLE_ID_LEN];
	char     title[GWM_TITLE_LEN];
	/* Профиль запоминается на старте сессии: сменить его можно без
	 * перезагрузки, и тогда закрывать сессию чужим именем нельзя. */
	char     account[GWM_ACCOUNT_MAX];
	/* Сырой тик начала, без поправки. */
	uint64_t start_tick;
	int      active;
} gwm_session_t;

/* Последняя закрытая сессия — только для уведомления в XMB.
 *
 * Закрытие затирает сессию целиком (gwm_session_init), а сообщение собирается
 * уже в цикле опроса, где ни названия, ни длительности к тому моменту нет.
 * Флаг снимает тот, кто прочитал: показывать по возвращаемому значению
 * gwm_session_update нельзя, оно истинно ещё и на старте игры. */
typedef struct
{
	char     title[GWM_TITLE_LEN];
	uint32_t seconds;
	int      valid;
} gwm_closed_session_t;

static gwm_closed_session_t gwm_last_closed;

static void gwm_session_init(gwm_session_t *s)
{
	s->title_id[0] = 0;
	s->title[0] = 0;
	s->account[0] = 0;
	s->start_tick = 0;
	s->active = 0;
}

/* tick — сырой, как и start_tick. Обе метки уходят в очередь без поправки. */
static void gwm_session_close(gwm_session_t *s, uint64_t tick)
{
	char line[GWM_RECORD_MAX];
	char started_at[GWM_ISO_LEN];
	char ended_at[GWM_ISO_LEN];

	if(!s->active) return;

	gwm_iso_from_tick(started_at, s->start_tick);
	gwm_iso_from_tick(ended_at, tick);

	uint32_t seconds = (uint32_t)((tick - s->start_tick) / 1000000ULL);

	/* Название может не определиться — тогда в сообщении хотя бы title id. */
	gwm_copy_str(gwm_last_closed.title, sizeof(gwm_last_closed.title),
	             s->title[0] ? s->title : s->title_id);
	gwm_last_closed.seconds = seconds;
	gwm_last_closed.valid = 1;

	size_t len = gwm_build_session(line, sizeof(line) - 1, s->account,
	                               s->title_id, s->title,
	                               started_at, ended_at, seconds);
	line[len++] = '\n';

	gwm_queue_append(line, len);

	gwm_session_init(s);
}

/* Вызывается на каждом опросе. Возвращает 1, если состояние изменилось. */
static int gwm_session_update(gwm_session_t *s, const gwm_game_t *game,
                              int playing, const char *account_override)
{
	uint64_t tick = gwm_tick_raw();
	const char *id = playing ? game->title_id : "";

	if(s->active && gwm_strcmp(s->title_id, id) == 0) return 0; /* та же игра */
	if(!s->active && !playing) return 0;                        /* и раньше XMB */

	gwm_session_close(s, tick);

	if(playing)
	{
		if(account_override && *account_override)
			gwm_copy_str(s->account, sizeof(s->account), account_override);
		else
			gwm_account_detect(s->account, sizeof(s->account));

		gwm_copy_str(s->title_id, sizeof(s->title_id), game->title_id);
		gwm_copy_str(s->title, sizeof(s->title), game->title);
		s->start_tick = tick;
		s->active = 1;
	}

	return 1;
}

#endif /* GWM_SESSION_H */
