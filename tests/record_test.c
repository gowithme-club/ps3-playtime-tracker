/* Хостовый тест сборки тел запросов и форматирования времени.
 *
 * Сверяется с примерами из docs/ingest-api-v1-client.md — если сервер
 * поменяет контракт, ломаться должно здесь, а не на консоли.
 *
 *   make test-host
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "record.h"

static int failures = 0;

static void check(const char *what, const char *got, const char *want)
{
	if(strcmp(got, want) == 0) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s\n    получено:  %s\n    ожидалось: %s\n", what, got, want);
	failures++;
}

int main(void)
{
	char buf[GWM_RECORD_MAX];
	char iso[GWM_ISO_LEN];

	printf("ISO 8601:\n");

	gwm_iso_format(iso, 2026, 7, 5, 18, 30, 0);
	check("пример из документа", iso, "2026-07-05T18:30:00Z");

	gwm_iso_format(iso, 2026, 12, 31, 23, 59, 59);
	check("конец года", iso, "2026-12-31T23:59:59Z");

	gwm_iso_format(iso, 2026, 1, 2, 3, 4, 5);
	check("однозначные значения", iso, "2026-01-02T03:04:05Z");

	printf("сессия:\n");

	gwm_build_session(buf, sizeof(buf), "Eplring", "BLES01138", "Demon's Souls",
	                  "2026-07-04T21:00:00Z", "2026-07-04T23:12:30Z", 7950);
	check("пример из документа", buf,
	      "{\"account\":\"Eplring\",\"titleId\":\"BLES01138\",\"title\":\"Demon's Souls\","
	      "\"platform\":\"ps3\",\"startedAt\":\"2026-07-04T21:00:00Z\","
	      "\"endedAt\":\"2026-07-04T23:12:30Z\",\"seconds\":7950}");

	/* Реальный случай с нашей консоли: кириллица и кавычка в названии. */
	gwm_build_session(buf, sizeof(buf), "Player", "BLES00951",
	                  "DRIVER \"Сан-Франциско\"",
	                  "2026-08-16T21:20:03Z", "2026-08-16T21:25:03Z", 300);
	check("кириллица и кавычки", buf,
	      "{\"account\":\"Player\",\"titleId\":\"BLES00951\","
	      "\"title\":\"DRIVER \\\"Сан-Франциско\\\"\",\"platform\":\"ps3\","
	      "\"startedAt\":\"2026-08-16T21:20:03Z\",\"endedAt\":\"2026-08-16T21:25:03Z\","
	      "\"seconds\":300}");

	/* Тоже реальный случай, и куда опаснее кавычки: PS3 переносит длинное имя
	 * в PARAM.SFO, чтобы оно легло в две строки на XMB, и в названии живёт
	 * настоящий 0x0A. Сырой перенос внутри строки — невалидный JSON, сервер
	 * ответил бы 400, а 400 у нас выбрасывает батч: сессии исчезли бы молча.
	 * Имя приводится как есть, нормализацией занимается сервер. */
	gwm_build_session(buf, sizeof(buf), "SamplePlayer", "BCES01670",
	                  "Uncharted 3: Drake's Deception™\nGame of the Year",
	                  "2026-08-17T11:48:57Z", "2026-08-17T11:53:57Z", 300);
	check("перенос строки в названии игры", buf,
	      "{\"account\":\"SamplePlayer\",\"titleId\":\"BCES01670\","
	      "\"title\":\"Uncharted 3: Drake's Deception™\\nGame of the Year\","
	      "\"platform\":\"ps3\",\"startedAt\":\"2026-08-17T11:48:57Z\","
	      "\"endedAt\":\"2026-08-17T11:53:57Z\",\"seconds\":300}");

	printf("heartbeat:\n");

	gwm_build_heartbeat(buf, sizeof(buf), "2026-07-05T18:30:05Z", 1, "Eplring",
	                    "BLES01138", "Demon's Souls", "2026-07-05T17:58:11Z");
	check("в игре", buf,
	      "{\"sentAt\":\"2026-07-05T18:30:05Z\",\"current\":[{\"account\":\"Eplring\","
	      "\"titleId\":\"BLES01138\",\"titleName\":\"Demon's Souls\",\"platform\":\"ps3\","
	      "\"startedAt\":\"2026-07-05T17:58:11Z\"}]}");

	gwm_build_heartbeat(buf, sizeof(buf), "2026-07-05T18:30:05Z", 0, "", "", "", "");
	check("никто не играет", buf,
	      "{\"sentAt\":\"2026-07-05T18:30:05Z\",\"current\":[]}");

	printf("трофеи:\n");
	{
		size_t n = gwm_trophies_begin(buf, sizeof(buf), "SamplePlayer",
		                              "NPWR00331_00", "DRIVER Сан-Франциско");
		n = gwm_trophies_add(buf, sizeof(buf), n, 1, 44, "2026-08-16T21:20:32Z");
		n = gwm_trophies_add(buf, sizeof(buf), n, 0, 1,  "2026-08-16T21:32:22Z");
		gwm_trophies_end(buf, sizeof(buf), n);

		check("состояние сета", buf,
		      "{\"account\":\"SamplePlayer\",\"platform\":\"ps3\","
		      "\"setId\":\"NPWR00331_00\",\"title\":\"DRIVER Сан-Франциско\","
		      "\"trophies\":[{\"id\":44,\"earnedAt\":\"2026-08-16T21:20:32Z\"},"
		      "{\"id\":1,\"earnedAt\":\"2026-08-16T21:32:22Z\"}]}");
	}
	{
		/* Название неизвестно — поле просто не включается. */
		size_t n = gwm_trophies_begin(buf, sizeof(buf), "Kate", "NPWR00132_00", NULL);
		n = gwm_trophies_add(buf, sizeof(buf), n, 1, 0, "2026-01-01T00:00:00Z");
		gwm_trophies_end(buf, sizeof(buf), n);

		check("без названия игры", buf,
		      "{\"account\":\"Kate\",\"platform\":\"ps3\",\"setId\":\"NPWR00132_00\","
		      "\"trophies\":[{\"id\":0,\"earnedAt\":\"2026-01-01T00:00:00Z\"}]}");
	}

	/* Переполнение: за границей выделенного размера ничего не меняется. */
	{
		enum { LIMIT = 60, GUARD = 8 };
		char small[LIMIT + GUARD];
		memset(small, 0x7F, sizeof(small));

		size_t n = gwm_build_session(small, LIMIT, "Player", "BLES00951",
		                             "очень длинное название игры на кириллице",
		                             "2026-08-16T21:20:03Z", "2026-08-16T21:25:03Z", 300);

		int guard_ok = 1;
		for(int i = LIMIT; i < LIMIT + GUARD; i++) if(small[i] != 0x7F) guard_ok = 0;

		if(n < LIMIT && guard_ok && small[n] == 0)
			printf("  ok   не пишет за границу буфера\n");
		else { printf("  FAIL вышли за буфер: n=%zu\n", n); failures++; }
	}

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
