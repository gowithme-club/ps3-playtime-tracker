/* Хостовый тест HTTP-клиента: сборка запроса и разбор строки статуса.
 *
 * Сверяется с сырым примером из docs/ingest-api-v1-client.md.
 *
 *   make test-host
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "http.h"

static int failures = 0;

static void check(const char *what, const char *got, const char *want)
{
	if(strcmp(got, want) == 0) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s\n    получено:\n%s\n    ожидалось:\n%s\n", what, got, want);
	failures++;
}

static void check_int(const char *what, int got, int want)
{
	if(got == want) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s: получено %d, ожидалось %d\n", what, got, want);
	failures++;
}

int main(void)
{
	char buf[GWM_HTTP_HEAD_MAX];

	printf("сборка запроса:\n");

	gwm_build_request(buf, sizeof(buf), "/ingest/v1/heartbeat", "gowithme.club",
	                  80, "dev_7f3a_secret", 187);
	check("пример из документа", buf,
	      "POST /ingest/v1/heartbeat HTTP/1.1\r\n"
	      "Host: gowithme.club\r\n"
	      "Authorization: Bearer dev_7f3a_secret\r\n"
	      "User-Agent: ps3-plugin/" GWM_VERSION "\r\n"
	      "X-Client-Version: " GWM_VERSION "\r\n"
	      "Content-Type: application/json\r\n"
	      "Content-Length: 187\r\n"
	      "Connection: close\r\n\r\n");

	gwm_build_request(buf, sizeof(buf), "/ingest/v1/sessions", "192.168.1.10",
	                  8080, "tok", 42);
	check("нестандартный порт попадает в Host", buf,
	      "POST /ingest/v1/sessions HTTP/1.1\r\n"
	      "Host: 192.168.1.10:8080\r\n"
	      "Authorization: Bearer tok\r\n"
	      "User-Agent: ps3-plugin/" GWM_VERSION "\r\n"
	      "X-Client-Version: " GWM_VERSION "\r\n"
	      "Content-Type: application/json\r\n"
	      "Content-Length: 42\r\n"
	      "Connection: close\r\n\r\n");

	/* Требование сервера: значение ≤ 32 символов и только печатаемый ASCII,
	 * иначе оно будет порезано и в UI попадёт мусор. Проверяем саму строку,
	 * а не то, как мы её вписали. */
	{
		const char *id = GWM_CLIENT_ID;
		size_t n = strlen(id);
		int printable = 1;

		for(size_t i = 0; i < n; i++)
			if(id[i] < 0x20 || id[i] > 0x7E) printable = 0;

		check_int("значение X-Client-Version не длиннее 32 символов", n <= 32, 1);
		check_int("и состоит только из печатаемого ASCII", printable, 1);
	}

	printf("разбор ответа:\n");

	check_int("200", gwm_parse_status("HTTP/1.1 200 OK\r\n\r\n", 19), 200);
	check_int("401", gwm_parse_status("HTTP/1.1 401 Unauthorized\r\n", 27), 401);
	check_int("429", gwm_parse_status("HTTP/1.1 429 Too Many Requests\r\n", 32), 429);
	check_int("500", gwm_parse_status("HTTP/1.0 500 Internal Server Error\r\n", 36), 500);

	check_int("мусор вместо ответа", gwm_parse_status("nonsense", 8), GWM_HTTP_ERR_PARSE);
	check_int("обрезанный ответ", gwm_parse_status("HTTP/1.1", 8), GWM_HTTP_ERR_PARSE);
	check_int("пустой ответ", gwm_parse_status("", 0), GWM_HTTP_ERR_PARSE);
	check_int("код не из трёх цифр", gwm_parse_status("HTTP/1.1 20 OK\r\n", 16), GWM_HTTP_ERR_PARSE);

	/* Заголовки не должны вылезать за буфер при длинном токене. */
	{
		enum { LIMIT = 80, GUARD = 8 };
		char small[LIMIT + GUARD];
		char token[256];

		memset(small, 0x7F, sizeof(small));
		memset(token, 'x', sizeof(token) - 1);
		token[sizeof(token) - 1] = 0;

		size_t n = gwm_build_request(small, LIMIT, "/ingest/v1/sessions",
		                             "gowithme.club", 80, token, 100);

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
