/* Хостовый тест разбора TROPUSR.DAT.
 *
 * Проверяется на файле, собранном здесь же по той же раскладке, что и настоящий.
 * Дополнительно можно скормить реальный файл с консоли:
 *
 *   make test-host
 *   /tmp/gwm_trophy_test путь/к/TROPUSR.DAT
 *
 * Эталон, на котором раскладка проверялась: NPWR00331_00 профиля SamplePlayer —
 * 51 трофей в сете, 6 выбитых.
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "trophy.h"

static int failures = 0;

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void put64(uint8_t *p, uint64_t v)
{
	put32(p, (uint32_t)(v >> 32));
	put32(p + 4, (uint32_t)v);
}

/* Тик RTC: микросекунды от 0001-01-01. */
#define EPOCH_SEC 62135596800ULL
static uint64_t tick_of(uint64_t unix_sec) { return (unix_sec + EPOCH_SEC) * 1000000ULL; }

/* Собирает файл: 3 записи в таблице, блоки трофеев с заданным числом выбитых. */
static size_t build_file(uint8_t *buf, size_t size, uint32_t trophies,
                         const uint32_t *unlocked_ids, uint32_t unlocked_count)
{
	memset(buf, 0, size);

	uint64_t block_off = GWM_TROP_HEADER_SIZE + 3 * GWM_TROP_RECORD_SIZE;

	put64(buf, 0x818F54AD00010000ULL);
	put32(buf + 8, 3);

	/* Две посторонние записи и одна нужная — чтобы проверить поиск. */
	uint8_t *r = buf + GWM_TROP_HEADER_SIZE;
	put32(r, 1); put32(r + 4, 48); put32(r + 12, 1); put64(r + 16, 0x170);

	r += GWM_TROP_RECORD_SIZE;
	put32(r, 4); put32(r + 4, 80); put32(r + 12, trophies); put64(r + 16, 0x430);

	r += GWM_TROP_RECORD_SIZE;
	put32(r, GWM_TROP_TIME_BLOCK_ID);
	put32(r + 4, GWM_TROP_ENTRY_PAYLOAD);
	put32(r + 12, trophies);
	put64(r + 16, block_off);

	for(uint32_t i = 0; i < trophies; i++)
	{
		uint8_t *e = buf + block_off + (size_t)i * GWM_TROP_ENTRY_SIZE;

		put32(e, GWM_TROP_TIME_BLOCK_ID);
		put32(e + 4, GWM_TROP_ENTRY_PAYLOAD);
		put32(e + 8, i);
		put32(e + 16, i);       /* номер трофея */

		for(uint32_t k = 0; k < unlocked_count; k++)
			if(unlocked_ids[k] == i)
			{
				put32(e + 20, 1);                                  /* выбит */
				put32(e + 24, 0x2000);                             /* состояние синка */
				put64(e + 32, tick_of(1786000000ULL + i * 60));    /* время */
			}
	}

	return (size_t)block_off + (size_t)trophies * GWM_TROP_ENTRY_SIZE;
}

static void scan_real_file(const char *path)
{
	uint8_t head[GWM_TROP_HEADER_SIZE + GWM_TROP_MAX_RECORDS * GWM_TROP_RECORD_SIZE];
	gwm_trophy_block_t blk;

	FILE *f = fopen(path, "rb");
	if(!f) { printf("не открылся: %s\n", path); return; }

	size_t got = fread(head, 1, sizeof(head), f);

	if(!gwm_trophy_find_block(head, got, &blk))
	{
		printf("блок трофеев не найден\n");
		fclose(f);
		return;
	}

	printf("блок трофеев: смещение 0x%llx, записей %u\n",
	       (unsigned long long)blk.offset, blk.count);

	int unlocked = 0;
	for(uint32_t i = 0; i < blk.count; i++)
	{
		uint8_t e[GWM_TROP_ENTRY_SIZE];
		uint32_t id;
		uint64_t tick;

		if(fseek(f, (long)(blk.offset + (uint64_t)i * GWM_TROP_ENTRY_SIZE), SEEK_SET)) break;
		if(fread(e, 1, sizeof(e), f) != sizeof(e)) break;

		if(!gwm_trophy_decode(e, &id, &tick)) continue;

		unlocked++;
		printf("  id=%-3u earnedAt=%llu\n", id, (unsigned long long)(tick / 1000000ULL - EPOCH_SEC));
	}

	printf("выбито: %d из %u\n", unlocked, blk.count);
	fclose(f);
}

int main(int argc, char **argv)
{
	if(argc > 1) { scan_real_file(argv[1]); return 0; }

	static uint8_t buf[64 * 1024];
	const uint32_t unlocked_ids[] = {1, 26, 31, 34, 37, 44};
	gwm_trophy_block_t blk;

	printf("разбор TROPUSR.DAT:\n");

	size_t len = build_file(buf, sizeof(buf), 51, unlocked_ids, 6);

	if(gwm_trophy_find_block(buf, len, &blk) && blk.count == 51)
		printf("  ok   блок трофеев найден, записей 51\n");
	else { printf("  FAIL блок не найден\n"); failures++; return 1; }

	int found = 0, mismatch = 0;
	for(uint32_t i = 0; i < blk.count; i++)
	{
		uint32_t id;
		uint64_t tick;

		if(!gwm_trophy_decode(buf + blk.offset + (size_t)i * GWM_TROP_ENTRY_SIZE, &id, &tick))
			continue;

		int expected = 0;
		for(int k = 0; k < 6; k++) if(unlocked_ids[k] == id) expected = 1;
		if(!expected) mismatch++;

		if(tick / 1000000ULL - EPOCH_SEC != 1786000000ULL + id * 60) mismatch++;
		found++;
	}

	if(found == 6 && !mismatch) printf("  ok   выбитые трофеи и времена совпали\n");
	else { printf("  FAIL найдено %d, расхождений %d\n", found, mismatch); failures++; }

	/* Заблокированный трофей не должен попадать в выгрузку. */
	{
		uint32_t id; uint64_t tick;
		const uint8_t *locked = buf + blk.offset; /* id=0, он не в списке */
		if(!gwm_trophy_decode(locked, &id, &tick))
			printf("  ok   заблокированный трофей пропускается\n");
		else { printf("  FAIL заблокированный трофей принят за выбитый\n"); failures++; }
	}

	/* Битые данные не должны приниматься. */
	{
		uint8_t junk[GWM_TROP_HEADER_SIZE + 32];
		gwm_trophy_block_t b;
		memset(junk, 0xAA, sizeof(junk));

		if(!gwm_trophy_find_block(junk, sizeof(junk), &b) &&
		   !gwm_trophy_find_block(buf, 8, &b))
			printf("  ok   мусор и обрезанный файл отвергаются\n");
		else { printf("  FAIL мусор принят\n"); failures++; }
	}

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
