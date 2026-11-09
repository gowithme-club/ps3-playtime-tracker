/* Хостовый тест текстов уведомлений XMB.
 *
 * Сообщение видит человек у телевизора, а проверить его на консоли стоит
 * перезагрузки — поэтому формат ловится здесь, до сборки плагина.
 *
 *   make test-host
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "msg.h"

/* GWM_TITLE_LEN из game.h. Сам game.h сюда не включить — он тянет vsh,
 * а ради одной константы городить заглушки незачем. */
#define TITLE_MAX 64

static int failures = 0;

static void check(const char *what, const char *got, const char *want)
{
	if(strcmp(got, want) == 0) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s\n    получено: %s\n    ожидалось: %s\n", what, got, want);
	failures++;
}

static const char *session(char *buf, size_t size, const char *title,
                           uint32_t seconds, int net_ready, int code)
{
	gwm_msg_session(buf, size, title, seconds, net_ready, code);
	return buf;
}

int main(void)
{
	char buf[GWM_MSG_MAX];

	printf("закрытая сессия:\n");

	check("отправлена",
	      session(buf, sizeof(buf), "Demon's Souls", 2820, 1, 200),
	      "gwm-stats: Demon's Souls\nSession 47 min, sent");

	check("без токена — в очередь",
	      session(buf, sizeof(buf), "Demon's Souls", 2820, 0, 0),
	      "gwm-stats: Demon's Souls\nSession 47 min, queued (no token)");

	check("сеть не дала отправить",
	      session(buf, sizeof(buf), "Demon's Souls", 2820, 1,
	              GWM_HTTP_ERR_CONNECT),
	      "gwm-stats: Demon's Souls\nSession 47 min, queued: no connect (-3)");

	check("токен не принят",
	      session(buf, sizeof(buf), "Demon's Souls", 2820, 1, 401),
	      "gwm-stats: Demon's Souls\nSession 47 min, queued: bad token (401)");

	/* 400 и 413 очищают очередь (upload.h) — данных больше нет нигде,
	 * и «queued» тут был бы враньём. */
	check("сервер не принял — данные потеряны",
	      session(buf, sizeof(buf), "Demon's Souls", 2820, 1, 400),
	      "gwm-stats: Demon's Souls\nSession 47 min, dropped: bad request (400)");

	check("очередь не разошлась целиком",
	      session(buf, sizeof(buf), "Demon's Souls", 2820, 1, 0),
	      "gwm-stats: Demon's Souls\nSession 47 min, queued");

	printf("длительность:\n");

	check("меньше минуты — секундами, а не «0 min»",
	      session(buf, sizeof(buf), "Tekken 6", 45, 1, 200),
	      "gwm-stats: Tekken 6\nSession 45 sec, sent");

	check("ровно минута",
	      session(buf, sizeof(buf), "Tekken 6", 60, 1, 200),
	      "gwm-stats: Tekken 6\nSession 1 min, sent");

	check("часы и минуты",
	      session(buf, sizeof(buf), "Tekken 6", 8220, 1, 200),
	      "gwm-stats: Tekken 6\nSession 2 h 17 min, sent");

	check("ровно час",
	      session(buf, sizeof(buf), "Tekken 6", 3600, 1, 200),
	      "gwm-stats: Tekken 6\nSession 1 h 0 min, sent");

	printf("название игры:\n");

	/* В PARAM.SFO живёт настоящий перенос строки — своей раскладкой
	 * сообщения распоряжаемся мы, а не редактор PARAM.SFO. */
	check("перенос из PARAM.SFO не растит третью строку",
	      session(buf, sizeof(buf),
	              "Uncharted 3: Drake's Deception\nGame of the Year", 2820, 1, 200),
	      "gwm-stats: Uncharted 3: Drake's Deception Game of the Year\n"
	      "Session 47 min, sent");

	check("название не определилось — хотя бы title id",
	      session(buf, sizeof(buf), "BLES00932", 2820, 1, 200),
	      "gwm-stats: BLES00932\nSession 47 min, sent");

	printf("трофеи:\n");

	gwm_msg_trophies(buf, sizeof(buf), 200);
	check("набор уехал", buf, "gwm-stats: trophies\nSent to server");

	gwm_msg_trophies(buf, sizeof(buf), GWM_HTTP_ERR_RECV);
	check("сервер не ответил", buf,
	      "gwm-stats: trophies\nNot sent: no reply (-5)");

	gwm_msg_trophies(buf, sizeof(buf), 503);
	check("любой 5xx — одним словом", buf,
	      "gwm-stats: trophies\nNot sent: server error (503)");

	printf("коды без названия:\n");

	gwm_msg_trophies(buf, sizeof(buf), 418);
	check("неизвестный код печатается числом", buf,
	      "gwm-stats: trophies\nNot sent: code 418");

	gwm_msg_trophies(buf, sizeof(buf), -99);
	check("неизвестный отрицательный — со знаком", buf,
	      "gwm-stats: trophies\nNot sent: code -99");

	printf("границы буфера:\n");

	{
		/* Название максимальной длины плюс самый длинный хвост: строка
		 * обязана обрезаться по буферу, а не залезть за него. */
		enum { LIMIT = 64, GUARD = 16 };
		char small[LIMIT + GUARD];
		char title[TITLE_MAX];

		memset(small, 0x7F, sizeof(small));
		memset(title, 'W', sizeof(title) - 1);
		title[sizeof(title) - 1] = 0;

		size_t n = gwm_msg_session(small, LIMIT, title, 359999, 1,
		                           GWM_HTTP_ERR_RESOLVE);

		int guard_ok = 1;
		for(int i = LIMIT; i < LIMIT + GUARD; i++) if(small[i] != 0x7F) guard_ok = 0;

		if(n < LIMIT && guard_ok && small[n] == 0)
			printf("  ok   не пишет за границу буфера\n");
		else { printf("  FAIL вышли за буфер: n=%zu\n", n); failures++; }
	}

	{
		/* Самое длинное реальное сообщение обязано влезать в GWM_MSG_MAX
		 * целиком: обрезанный хвост — это потерянный код ошибки. */
		char title[TITLE_MAX];

		memset(title, 'W', sizeof(title) - 1);
		title[sizeof(title) - 1] = 0;

		size_t n = gwm_msg_session(buf, sizeof(buf), title, 359999, 1,
		                           GWM_HTTP_ERR_RESOLVE);

		if(n < sizeof(buf) - 1 &&
		   strstr(buf, "queued: host not found (-2)"))
			printf("  ok   длинное название не съедает код ошибки (%zu из %d)\n",
			       n, GWM_MSG_MAX);
		else { printf("  FAIL хвост обрезан: %s\n", buf); failures++; }
	}

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
