/* Примитивы сборки JSON.
 *
 * Намеренно без файлов и сокетов: чистая работа со строками, которую можно
 * гонять обычным clang на хосте (tests/), а не проверять на консоли, где цикл
 * отладки стоит перезагрузки.
 *
 * libc в плагине нет, поэтому snprintf взять неоткуда — всё вручную.
 */

#ifndef GWM_JSON_H
#define GWM_JSON_H

#include <stdint.h>
#include <stddef.h>

#include "str.h"

static char *gwm_u64(char *dst, const char *end, uint64_t v)
{
	char tmp[21];
	int n = 0;

	do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while(v);

	while(n && dst < end - 1) *dst++ = tmp[--n];
	*dst = 0;
	return dst;
}

/* Экранирование по JSON. Названия игр приходят из PARAM.SFO и вполне могут
 * содержать кавычки — без этого сервер получит битую строку. */
static char *gwm_json_str(char *dst, const char *end, const char *src)
{
	while(*src && dst < end - 7)
	{
		unsigned char c = (unsigned char)*src++;

		if(c == '"' || c == '\\')      { *dst++ = '\\'; *dst++ = (char)c; }
		else if(c == '\n')             { *dst++ = '\\'; *dst++ = 'n'; }
		else if(c == '\r')             { *dst++ = '\\'; *dst++ = 'r'; }
		else if(c == '\t')             { *dst++ = '\\'; *dst++ = 't'; }
		else if(c < 0x20)
		{
			static const char hex[] = "0123456789abcdef";
			*dst++ = '\\'; *dst++ = 'u'; *dst++ = '0'; *dst++ = '0';
			*dst++ = hex[(c >> 4) & 0xF]; *dst++ = hex[c & 0xF];
		}
		else *dst++ = (char)c;  /* UTF-8 пропускаем как есть */
	}
	*dst = 0;
	return dst;
}

static char *gwm_lit(char *dst, const char *end, const char *src)
{
	while(*src && dst < end - 1) *dst++ = *src++;
	*dst = 0;
	return dst;
}

/* Пара «"ключ":"значение"» с экранированием значения. */
static char *gwm_json_kv(char *dst, const char *end, const char *key, const char *val)
{
	dst = gwm_lit(dst, end, "\"");
	dst = gwm_lit(dst, end, key);
	dst = gwm_lit(dst, end, "\":\"");
	dst = gwm_json_str(dst, end, val);
	return gwm_lit(dst, end, "\"");
}

#endif /* GWM_JSON_H */
