/* Минимальный HTTP-клиент.
 *
 * Ровно то, что нужно по docs/ingest-api-v1-client.md: POST с телом JSON,
 * заголовком Authorization и обязательным Content-Length. HTTP/1.1,
 * `Connection: close`, без keep-alive и без chunked. TLS не нужен — сервер
 * принимает обычный HTTP, а TLS-стека на PS3 всё равно нет.
 *
 * Плюс X-Client-Version на каждом запросе. Заголовок опционален, сервер
 * снимает его с heartbeat и показывает в строке устройства на /health.
 * Плагин уедет на консоли, до которых нам не дотянуться, и когда придёт
 * баг-репорт, это единственный способ понять, какая там сборка. Значение —
 * голая версия: платформу сервер и так знает по токену устройства и пишет
 * её в той же строке, так что имя клиента там повторяло бы её. Имя клиента
 * живёт в User-Agent, где его больше взять неоткуда — для HTTP-логов.
 *
 * Сборка запроса и разбор строки статуса отделены от сокетов и проверяются
 * хостовым тестом; сама сетевая часть под GWM_HOST_TEST не собирается.
 */

#ifndef GWM_HTTP_H
#define GWM_HTTP_H

#include <stdint.h>
#include <stddef.h>

#include "json.h"
#include "version.h"

/* Значение X-Client-Version — сама версия из version.h, то есть из того же
 * места, что и в CHANGELOG. Сервер режет строку до 32 символов и оставляет
 * только печатаемый ASCII, поэтому она короткая и без кириллицы. */
#define GWM_CLIENT_ID GWM_VERSION

/* User-Agent: имя клиента в той же форме, что у Switch-модуля
 * (switch-plugin/<версия>). */
#define GWM_USER_AGENT "ps3-plugin/" GWM_VERSION

/* Заголовки с запасом: хост и токен ограничены конфигом. */
#define GWM_HTTP_HEAD_MAX 512

/* Сколько ждём сеть. Консоль может быть за медленным Wi-Fi, но и подвешивать
 * поток надолго нельзя — он же опрашивает игру. */
#define GWM_HTTP_TIMEOUT_SEC 15

/* Отрицательные коды — наши, не сервера. */
#define GWM_HTTP_ERR_SOCKET  (-1)
#define GWM_HTTP_ERR_RESOLVE (-2)
#define GWM_HTTP_ERR_CONNECT (-3)
#define GWM_HTTP_ERR_SEND    (-4)
#define GWM_HTTP_ERR_RECV    (-5)
#define GWM_HTTP_ERR_PARSE   (-6)

static size_t gwm_build_request(char *out, size_t out_size,
                                const char *path, const char *host,
                                uint16_t port, const char *token,
                                size_t body_len)
{
	char *p = out;
	const char *end = out + out_size;

	p = gwm_lit(p, end, "POST ");
	p = gwm_lit(p, end, path);
	p = gwm_lit(p, end, " HTTP/1.1\r\nHost: ");
	p = gwm_lit(p, end, host);

	/* Нестандартный порт положено указывать в Host. */
	if(port != 80)
	{
		p = gwm_lit(p, end, ":");
		p = gwm_u64(p, end, port);
	}

	p = gwm_lit(p, end, "\r\nAuthorization: Bearer ");
	p = gwm_lit(p, end, token);
	p = gwm_lit(p, end, "\r\nUser-Agent: " GWM_USER_AGENT);
	p = gwm_lit(p, end, "\r\nX-Client-Version: " GWM_CLIENT_ID);
	p = gwm_lit(p, end, "\r\nContent-Type: application/json\r\nContent-Length: ");
	p = gwm_u64(p, end, body_len);
	p = gwm_lit(p, end, "\r\nConnection: close\r\n\r\n");

	return (size_t)(p - out);
}

/* Из "HTTP/1.1 200 OK" достаём 200. Меньше 12 байт — ответ не наш. */
static int gwm_parse_status(const char *resp, size_t len)
{
	if(len < 12) return GWM_HTTP_ERR_PARSE;
	if(resp[0] != 'H' || resp[1] != 'T' || resp[2] != 'T' || resp[3] != 'P')
		return GWM_HTTP_ERR_PARSE;

	const char *p = resp;
	const char *stop = resp + len;

	while(p < stop && *p != ' ') p++;
	while(p < stop && *p == ' ') p++;

	int code = 0, digits = 0;
	while(p < stop && *p >= '0' && *p <= '9' && digits < 3)
	{
		code = code * 10 + (*p++ - '0');
		digits++;
	}

	return (digits == 3) ? code : GWM_HTTP_ERR_PARSE;
}

#ifndef GWM_HOST_TEST

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#include "config.h"
#include "clock.h"

/* Отправляет тело и возвращает HTTP-код ответа либо GWM_HTTP_ERR_*.
 * Тело ответа не разбирается: по контракту хватает кода, см. docs/DEVELOPMENT.ru.md. */
static int gwm_http_post(const gwm_config_t *cfg, const char *path,
                         const char *body, size_t body_len)
{
	struct sockaddr_in sin;
	struct timeval tv;
	char head[GWM_HTTP_HEAD_MAX];
	char resp[256];

	int s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0) return GWM_HTTP_ERR_SOCKET;

	for(size_t i = 0; i < sizeof(sin); i++) ((char *)&sin)[i] = 0;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(cfg->port);

	unsigned int ip = inet_addr(cfg->host);
	if(ip != (unsigned int)-1)
	{
		sin.sin_addr.s_addr = ip;
	}
	else
	{
		struct hostent *hp = gethostbyname(cfg->host);
		if(!hp) { socketclose(s); return GWM_HTTP_ERR_RESOLVE; }

		sin.sin_family = hp->h_addrtype;
		for(int i = 0; i < hp->h_length && i < 4; i++)
			((char *)&sin.sin_addr)[i] = hp->h_addr[i];
	}

	tv.tv_sec = GWM_HTTP_TIMEOUT_SEC;
	tv.tv_usec = 0;
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if(connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
	{
		socketclose(s);
		return GWM_HTTP_ERR_CONNECT;
	}

	size_t head_len = gwm_build_request(head, sizeof(head), path, cfg->host,
	                                    cfg->port, cfg->token, body_len);

	if(send(s, head, head_len, 0) < 0 ||
	   (body_len && send(s, body, body_len, 0) < 0))
	{
		socketclose(s);
		return GWM_HTTP_ERR_SEND;
	}

	int got = recv(s, resp, sizeof(resp) - 1, 0);
	socketclose(s);

	if(got <= 0) return GWM_HTTP_ERR_RECV;

	/* Заголовок Date есть в любом ответе — по нему правим часы консоли. */
	gwm_clock_sync(resp, (size_t)got);

	return gwm_parse_status(resp, (size_t)got);
}

#endif /* GWM_HOST_TEST */

#endif /* GWM_HTTP_H */
