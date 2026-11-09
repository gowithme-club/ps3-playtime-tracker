/* Разбор TROPUSR.DAT — выбитые трофеи и время их получения.
 *
 * Файл лежит в /dev_hdd0/home/<профиль>/trophy/<NPWR...>/TROPUSR.DAT
 * и не зашифрован. Структуры совпадают с описанием из apollo-ps3 (GPLv3),
 * проверены на настоящих файлах с консоли.
 *
 * Раскладка:
 *   0x00  магия (8) + число записей (4) + выравнивание (36)  = 48 байт
 *   0x30  таблица записей по 32 байта: id, размер, ?, число блоков, смещение
 *   ...   блоки; нужный нам — с id = 6, по одному на каждый трофей
 *
 * Блок трофея (tropTimeInfo_t, 112 байт):
 *   +0   тип (6) | размер (96) | номер | ?
 *   +16  номер трофея | признак получения | состояние синхронизации | ?
 *   +32  время получения — тик RTC, микросекунды от 0001-01-01
 *
 * Важно: размер в заголовке блока не включает сами 16 байт заголовка,
 * поэтому шаг между блоками равен размер + 16.
 *
 * TROPCONF.SFM разбирать не нужно: названия, грейды и иконки сервер
 * дообогащает сам через PSN по setId.
 */

#ifndef GWM_TROPHY_H
#define GWM_TROPHY_H

#include <stdint.h>
#include <stddef.h>

#include "isotime.h"

#define GWM_TROP_HEADER_SIZE   48
#define GWM_TROP_RECORD_SIZE   32
#define GWM_TROP_TIME_BLOCK_ID 6
#define GWM_TROP_ENTRY_PAYLOAD 96
#define GWM_TROP_ENTRY_SIZE    (GWM_TROP_ENTRY_PAYLOAD + 16)

/* Верхняя граница на число записей в таблице — защита от битого файла. */
#define GWM_TROP_MAX_RECORDS 64

typedef struct
{
	uint64_t offset; /* где начинаются блоки трофеев */
	uint32_t count;  /* сколько их */
} gwm_trophy_block_t;

static uint32_t gwm_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t gwm_be64(const uint8_t *p)
{
	return ((uint64_t)gwm_be32(p) << 32) | (uint64_t)gwm_be32(p + 4);
}

/* Ищет в таблице запись с блоками трофеев.
 * buf — начало файла, len — сколько прочитано (заголовок с таблицей).
 * Возвращает 1, если запись нашлась. */
static int gwm_trophy_find_block(const uint8_t *buf, size_t len,
                                 gwm_trophy_block_t *out)
{
	if(len < GWM_TROP_HEADER_SIZE) return 0;

	uint32_t records = gwm_be32(buf + 8);
	if(!records || records > GWM_TROP_MAX_RECORDS) return 0;

	for(uint32_t i = 0; i < records; i++)
	{
		size_t off = GWM_TROP_HEADER_SIZE + (size_t)i * GWM_TROP_RECORD_SIZE;
		if(off + GWM_TROP_RECORD_SIZE > len) return 0;

		uint32_t id    = gwm_be32(buf + off);
		uint32_t size  = gwm_be32(buf + off + 4);
		uint32_t used  = gwm_be32(buf + off + 12);
		uint64_t start = gwm_be64(buf + off + 16);

		if(id != GWM_TROP_TIME_BLOCK_ID) continue;
		if(size != GWM_TROP_ENTRY_PAYLOAD || !used) return 0;

		out->offset = start;
		out->count = used;
		return 1;
	}

	return 0;
}

/* Разбирает один блок трофея. Возвращает 1, если трофей выбит,
 * и тогда заполняет id и тик времени получения. */
static int gwm_trophy_decode(const uint8_t *e, uint32_t *id, uint64_t *tick)
{
	if(gwm_be32(e) != GWM_TROP_TIME_BLOCK_ID) return 0;
	if(gwm_be32(e + 4) != GWM_TROP_ENTRY_PAYLOAD) return 0;

	uint32_t unlocked = gwm_be32(e + 20);
	uint64_t when = gwm_be64(e + 32);

	if(!unlocked || !when) return 0;

	*id = gwm_be32(e + 16);
	*tick = when;
	return 1;
}

#endif /* GWM_TROPHY_H */
