/* Проверка SHA-1 и HMAC-SHA1 тестовыми векторами из RFC 3174 и RFC 2202.
 *
 * Свою криптографию положено сверять с эталоном, иначе сервер будет молча
 * отбрасывать подписи, а искать причину придётся на консоли.
 *
 *   make test-host
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "sha1.h"

static int failures = 0;

static void check(const char *what, const char *got, const char *want)
{
	if(strcmp(got, want) == 0) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s\n    получено:  %s\n    ожидалось: %s\n", what, got, want);
	failures++;
}

static void sha1_hex(const void *msg, size_t len, char *out)
{
	gwm_sha1_t c;
	uint8_t d[GWM_SHA1_SIZE];

	gwm_sha1_init(&c);
	gwm_sha1_update(&c, msg, len);
	gwm_sha1_final(&c, d);
	gwm_hex(d, sizeof(d), out);
}

static void hmac_hex(const void *key, size_t klen, const void *msg, size_t mlen, char *out)
{
	uint8_t d[GWM_SHA1_SIZE];
	gwm_hmac_sha1(key, klen, msg, mlen, d);
	gwm_hex(d, sizeof(d), out);
}

int main(void)
{
	char hex[GWM_SHA1_SIZE * 2 + 1];

	printf("SHA-1 (RFC 3174):\n");

	sha1_hex("abc", 3, hex);
	check("abc", hex, "a9993e364706816aba3e25717850c26c9cd0d89d");

	sha1_hex("", 0, hex);
	check("пустая строка", hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709");

	sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
	check("две блока", hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

	/* Миллион 'a' — ловит ошибки в счётчике длины и в переносе через блоки. */
	{
		gwm_sha1_t c;
		uint8_t d[GWM_SHA1_SIZE];
		char chunk[1000];

		memset(chunk, 'a', sizeof(chunk));
		gwm_sha1_init(&c);
		for(int i = 0; i < 1000; i++) gwm_sha1_update(&c, chunk, sizeof(chunk));
		gwm_sha1_final(&c, d);
		gwm_hex(d, sizeof(d), hex);
		check("миллион 'a'", hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
	}

	printf("HMAC-SHA1 (RFC 2202):\n");

	{
		uint8_t key[20];
		memset(key, 0x0b, sizeof(key));
		hmac_hex(key, sizeof(key), "Hi There", 8, hex);
		check("вектор 1", hex, "b617318655057264e28bc0b6fb378c8ef146be00");
	}

	hmac_hex("Jefe", 4, "what do ya want for nothing?", 28, hex);
	check("вектор 2", hex, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");

	{
		uint8_t key[20], data[50];
		memset(key, 0xaa, sizeof(key));
		memset(data, 0xdd, sizeof(data));
		hmac_hex(key, sizeof(key), data, sizeof(data), hex);
		check("вектор 3", hex, "125d7342b9ac11cd91a39af48aa17b4f63f175d3");
	}

	{
		/* Ключ длиннее блока — должен сворачиваться хешем. */
		uint8_t key[80];
		memset(key, 0xaa, sizeof(key));
		hmac_hex(key, sizeof(key), "Test Using Larger Than Block-Size Key - Hash Key First", 54, hex);
		check("длинный ключ", hex, "aa4ae5e15272d00e95705637ce8a3b55ed402112");
	}

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
