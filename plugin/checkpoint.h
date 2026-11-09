/* Снимок открытой сессии на диске.
 *
 * Сессия попадает в очередь только при выходе из игры в XMB (session.h).
 * Если консоль выключили прямо из игры, перехода в XMB плагин не застаёт,
 * и сессия, жившая только в памяти, пропадала целиком.
 * На сервере такие вечера видны лишь по live-статусу, который гаснет через
 * пять минут тишины, — трофеи за вечер есть, наигранного времени нет.
 *
 * Поэтому каждым циклом опроса открытая сессия пишется сюда — готовой строкой
 * очереди, закрытой «сейчас». Обычное закрытие файл удаляет. Если же при
 * загрузке плагина файл на месте, прошлый запуск кончился посреди игры: строка
 * уходит в очередь как есть, с концом на последнем снимке. Потеря — не больше
 * одного цикла опроса, и длительность остаётся измеренной, а не выдуманной.
 *
 * Время. Очередь хранит сырое время консоли, а поправка добавляется при
 * отправке (queue.h). Снимок же всегда переживает перезагрузку, и если часы
 * при отключении питания сбросились (севшая CR2032), сырые метки прошлого
 * запуска к поправке нового отношения не имеют. Поэтому снимок пишется
 * исправленным временем, если поправка в том запуске была выучена, — первым
 * байтом стоит 'c', иначе 'r' и сырое время. При восстановлении исправленная
 * строка переводится в сырое время нового запуска, и отправка вернёт её
 * ровно на место. Если новый запуск поправку ещё не выучил, строка ложится
 * как есть: ошибка тогда — разница поправок, на живых часах это секунды.
 *
 * Запись идёт через временный файл. Окно между удалением старого снимка и
 * переименованием нового восстановление закрывает, читая временный файл,
 * когда основного нет или он битый.
 *
 * Разбор прочитанного отделён от файловых операций и проверяется хостовым
 * тестом (tests/checkpoint_test.c).
 */

#ifndef GWM_CHECKPOINT_H
#define GWM_CHECKPOINT_H

#include <stdint.h>
#include <stddef.h>

#define GWM_CHECKPOINT_RAW       'r'
#define GWM_CHECKPOINT_CORRECTED 'c'

/* Проверяет прочитанный снимок: байт-признак, затем одна строка-объект,
 * заканчивающаяся переводом строки. Возвращает длину строки вместе с
 * переводом (она начинается с buf + 1) или 0, если снимок битый, — например,
 * питание пропало посреди записи. */
static size_t gwm_checkpoint_parse(const char *buf, size_t len, char *marker)
{
	if(len < 4) return 0;
	if(buf[0] != GWM_CHECKPOINT_RAW && buf[0] != GWM_CHECKPOINT_CORRECTED) return 0;
	if(buf[1] != '{' || buf[len - 2] != '}' || buf[len - 1] != '\n') return 0;

	for(size_t i = 1; i < len - 1; i++)
		if(buf[i] == '\n' || !buf[i]) return 0;

	*marker = buf[0];
	return len - 1;
}

#ifndef GWM_HOST_TEST

#include <cell/cell_fs.h>

#include "queue.h"
#include "record.h"

#define GWM_CHECKPOINT_FILE GWM_QUEUE_DIR "/open.jsonl"
#define GWM_CHECKPOINT_TMP  GWM_QUEUE_DIR "/open.tmp"

/* Признак, строка сессии с переводом строки и байт запаса: прочитанное
 * целиком заполнение буфера значит «файл длиннее, чем мы пишем». */
static char gwm_checkpoint_buf[GWM_RECORD_MAX + 2];

static void gwm_checkpoint_clear(void)
{
	cellFsUnlink(GWM_CHECKPOINT_FILE);
	cellFsUnlink(GWM_CHECKPOINT_TMP);
}

/* Пишет снимок целиком: признак уже стоит в buf[0]. */
static void gwm_checkpoint_write(const char *buf, size_t len)
{
	int fd;
	uint64_t put = 0;

	cellFsMkdir(GWM_QUEUE_DIR, 0777); /* уже есть — вернёт ошибку, это нормально */

	if(cellFsOpen(GWM_CHECKPOINT_TMP,
	              CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_TRUNC,
	              &fd, NULL, 0) != CELL_FS_SUCCEEDED)
		return;

	int ret = cellFsWrite(fd, buf, len, &put);
	cellFsClose(fd);

	/* Недописанный снимок не должен вытеснить прошлый целый. */
	if(ret != CELL_FS_SUCCEEDED || put != len)
	{
		cellFsUnlink(GWM_CHECKPOINT_TMP);
		return;
	}

	cellFsUnlink(GWM_CHECKPOINT_FILE);
	cellFsRename(GWM_CHECKPOINT_TMP, GWM_CHECKPOINT_FILE);
}

/* Вызывается один раз при загрузке, до первого снимка нового запуска, и после
 * первого heartbeat — чтобы поправка часов уже была выучена. Возвращает 1,
 * если сессия прошлого запуска легла в очередь. */
static int gwm_checkpoint_recover(void)
{
	static const char *paths[2] = { GWM_CHECKPOINT_FILE, GWM_CHECKPOINT_TMP };
	int recovered = 0;

	for(int i = 0; i < 2 && !recovered; i++)
	{
		int fd;
		uint64_t got = 0;
		char marker;

		if(cellFsOpen(paths[i], CELL_FS_O_RDONLY, &fd, NULL, 0) != CELL_FS_SUCCEEDED)
			continue;

		int ret = cellFsRead(fd, gwm_checkpoint_buf, sizeof(gwm_checkpoint_buf), &got);
		cellFsClose(fd);

		if(ret != CELL_FS_SUCCEEDED || got >= sizeof(gwm_checkpoint_buf)) continue;

		size_t len = gwm_checkpoint_parse(gwm_checkpoint_buf, (size_t)got, &marker);
		if(!len) continue;

		char *line = gwm_checkpoint_buf + 1;

		/* Метки поправлены ещё прошлым запуском, а очередь ждёт сырое время
		 * этого: отправка добавит текущую поправку обратно. */
		if(marker == GWM_CHECKPOINT_CORRECTED && gwm_clock_synced)
			gwm_times_shift(line, len, -gwm_clock_offset);

		gwm_queue_append(line, len);
		recovered = 1;
	}

	gwm_checkpoint_clear();

	return recovered;
}

#endif /* GWM_HOST_TEST */

#endif /* GWM_CHECKPOINT_H */
