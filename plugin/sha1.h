/* SHA-1 и HMAC-SHA1.
 *
 * Нужны, чтобы подписывать тело запроса: транспорт у нас обычный HTTP
 * (TLS-стека на PS3 нет), поэтому токен не должен ходить по проводу вовсе.
 * Наружу уходит идентификатор устройства и подпись, см. docs/PROTOCOL.md.
 *
 * Реализация своя и намеренно маленькая: libc в плагине нет, а тащить
 * зависимость ради одного хеша не хочется. Проверяется тестовыми векторами
 * RFC 3174 и RFC 2202 на хосте.
 */

#ifndef GWM_SHA1_H
#define GWM_SHA1_H

#include <stdint.h>
#include <stddef.h>

#define GWM_SHA1_SIZE  20
#define GWM_SHA1_BLOCK 64

typedef struct
{
	uint32_t state[5];
	uint64_t count;              /* обработано байт */
	uint8_t  buf[GWM_SHA1_BLOCK];
} gwm_sha1_t;

static uint32_t gwm_rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void gwm_sha1_block(gwm_sha1_t *c, const uint8_t *p)
{
	uint32_t w[80];

	for(int i = 0; i < 16; i++)
		w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
		       ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];

	for(int i = 16; i < 80; i++)
		w[i] = gwm_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	uint32_t a = c->state[0], b = c->state[1], d = c->state[2];
	uint32_t e = c->state[3], f = c->state[4];

	for(int i = 0; i < 80; i++)
	{
		uint32_t k, t;

		if(i < 20)      { k = 0x5A827999; t = (b & d) | (~b & e); }
		else if(i < 40) { k = 0x6ED9EBA1; t = b ^ d ^ e; }
		else if(i < 60) { k = 0x8F1BBCDC; t = (b & d) | (b & e) | (d & e); }
		else            { k = 0xCA62C1D6; t = b ^ d ^ e; }

		t += gwm_rol(a, 5) + f + k + w[i];
		f = e; e = d; d = gwm_rol(b, 30); b = a; a = t;
	}

	c->state[0] += a; c->state[1] += b; c->state[2] += d;
	c->state[3] += e; c->state[4] += f;
}

static void gwm_sha1_init(gwm_sha1_t *c)
{
	c->state[0] = 0x67452301; c->state[1] = 0xEFCDAB89;
	c->state[2] = 0x98BADCFE; c->state[3] = 0x10325476;
	c->state[4] = 0xC3D2E1F0;
	c->count = 0;
}

static void gwm_sha1_update(gwm_sha1_t *c, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;
	size_t have = (size_t)(c->count % GWM_SHA1_BLOCK);

	c->count += len;

	if(have)
	{
		size_t need = GWM_SHA1_BLOCK - have;
		if(len < need)
		{
			for(size_t i = 0; i < len; i++) c->buf[have + i] = p[i];
			return;
		}
		for(size_t i = 0; i < need; i++) c->buf[have + i] = p[i];
		gwm_sha1_block(c, c->buf);
		p += need; len -= need;
	}

	while(len >= GWM_SHA1_BLOCK) { gwm_sha1_block(c, p); p += GWM_SHA1_BLOCK; len -= GWM_SHA1_BLOCK; }

	for(size_t i = 0; i < len; i++) c->buf[i] = p[i];
}

static void gwm_sha1_final(gwm_sha1_t *c, uint8_t out[GWM_SHA1_SIZE])
{
	uint64_t bits = c->count * 8;
	size_t have = (size_t)(c->count % GWM_SHA1_BLOCK);

	c->buf[have++] = 0x80;

	if(have > GWM_SHA1_BLOCK - 8)
	{
		while(have < GWM_SHA1_BLOCK) c->buf[have++] = 0;
		gwm_sha1_block(c, c->buf);
		have = 0;
	}
	while(have < GWM_SHA1_BLOCK - 8) c->buf[have++] = 0;

	for(int i = 7; i >= 0; i--) c->buf[have++] = (uint8_t)(bits >> (i * 8));
	gwm_sha1_block(c, c->buf);

	for(int i = 0; i < GWM_SHA1_SIZE; i++)
		out[i] = (uint8_t)(c->state[i / 4] >> (24 - (i % 4) * 8));
}

static void gwm_hmac_sha1(const void *key, size_t key_len,
                          const void *msg, size_t msg_len,
                          uint8_t out[GWM_SHA1_SIZE])
{
	uint8_t k[GWM_SHA1_BLOCK], pad[GWM_SHA1_BLOCK], inner[GWM_SHA1_SIZE];
	gwm_sha1_t c;

	for(size_t i = 0; i < GWM_SHA1_BLOCK; i++) k[i] = 0;

	/* Длинный ключ сворачивается хешем, короткий дополняется нулями. */
	if(key_len > GWM_SHA1_BLOCK)
	{
		gwm_sha1_init(&c);
		gwm_sha1_update(&c, key, key_len);
		gwm_sha1_final(&c, k);
	}
	else
	{
		const uint8_t *kp = (const uint8_t *)key;
		for(size_t i = 0; i < key_len; i++) k[i] = kp[i];
	}

	for(size_t i = 0; i < GWM_SHA1_BLOCK; i++) pad[i] = k[i] ^ 0x36;
	gwm_sha1_init(&c);
	gwm_sha1_update(&c, pad, GWM_SHA1_BLOCK);
	gwm_sha1_update(&c, msg, msg_len);
	gwm_sha1_final(&c, inner);

	for(size_t i = 0; i < GWM_SHA1_BLOCK; i++) pad[i] = k[i] ^ 0x5C;
	gwm_sha1_init(&c);
	gwm_sha1_update(&c, pad, GWM_SHA1_BLOCK);
	gwm_sha1_update(&c, inner, GWM_SHA1_SIZE);
	gwm_sha1_final(&c, out);
}

static void gwm_hex(const uint8_t *in, size_t len, char *out)
{
	static const char hex[] = "0123456789abcdef";
	for(size_t i = 0; i < len; i++)
	{
		out[i * 2]     = hex[(in[i] >> 4) & 0xF];
		out[i * 2 + 1] = hex[in[i] & 0xF];
	}
	out[len * 2] = 0;
}

#endif /* GWM_SHA1_H */
