/* Строковые примитивы.
 *
 * libc в плагине нет: он собирается с -nodefaultlibs, и всё, что обычно берут
 * из <string.h>, приходится иметь своё. Вынесено в отдельный заголовок, чтобы
 * не зависеть от порядка включений — им пользуются почти все остальные.
 */

#ifndef GWM_STR_H
#define GWM_STR_H

#include <stddef.h>

static size_t gwm_strlen(const char *s)
{
	const char *p = s;
	while(*p) p++;
	return (size_t)(p - s);
}

static int gwm_strncmp(const char *a, const char *b, size_t n)
{
	for(size_t i = 0; i < n; i++)
	{
		if(a[i] != b[i]) return (int)((unsigned char)a[i] - (unsigned char)b[i]);
		if(!a[i]) break;
	}
	return 0;
}

static int gwm_strcmp(const char *a, const char *b)
{
	while(*a && *a == *b) { a++; b++; }
	return (int)((unsigned char)*a - (unsigned char)*b);
}

static void gwm_copy_str(char *dst, size_t dst_size, const char *src)
{
	size_t i = 0;
	for(; i + 1 < dst_size && src[i]; i++) dst[i] = src[i];
	dst[i] = 0;
}

/* Дописывает в буфер с учётом его конца, возвращает новую позицию. */
static char *gwm_append(char *dst, const char *end, const char *src)
{
	while(*src && dst < end - 1) *dst++ = *src++;
	*dst = 0;
	return dst;
}

#endif /* GWM_STR_H */
