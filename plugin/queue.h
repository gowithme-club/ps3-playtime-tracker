/* Очередь закрытых сессий на диске.
 *
 * Сессии копятся в файл и уезжают на сервер батчами. Консоль часто выключают
 * кнопкой и держат без сети, поэтому неотправленное обязано пережить и то,
 * и другое. Строка удаляется только после подтверждения сервером.
 *
 * Формат — JSON Lines: одна закрытая сессия на строку, дописывание в конец.
 * При обрыве питания худшее, что теряется, — последняя строка.
 *
 * Метки времени в строках — **сырые часы консоли, без поправки**. В офлайне
 * поправку взять неоткуда: она приходит с заголовком Date в ответе сервера.
 * Поэтому строка пишется как есть, а сдвигается уже при отправке, когда
 * поправка известна (gwm_batch_fix_times в upload.h).
 */

#ifndef GWM_QUEUE_H
#define GWM_QUEUE_H

#include <cell/cell_fs.h>
#include <stdint.h>
#include <stddef.h>

#define GWM_QUEUE_DIR  "/dev_hdd0/tmp/gwm"
#define GWM_QUEUE_FILE GWM_QUEUE_DIR "/queue.jsonl"
#define GWM_QUEUE_TMP  GWM_QUEUE_DIR "/queue.tmp"

/* Страховка от бесконечного роста, если сервер недоступен неделями.
 * 512 КБ — это порядка двух с половиной тысяч сессий, то есть годы игры:
 * упереться в потолок можно только если отправка сломана насовсем. */
#define GWM_QUEUE_MAX_BYTES (512 * 1024)

/* Кусок, которым переписывается хвост файла. На стеке не лежит. */
#define GWM_QUEUE_CHUNK 4096

static uint64_t gwm_queue_size(void)
{
	CellFsStat st;
	if(cellFsStat(GWM_QUEUE_FILE, &st) != CELL_FS_SUCCEEDED) return 0;
	return st.st_size;
}

/* Дойдя до потолка, перестаём принимать новое, а не выбрасываем старое:
 * накопленное уже пережило выключения консоли и заслуживает доставки
 * больше, чем очередная строка. Молча — показывать это некому. */
static int gwm_queue_append(const char *line, size_t len)
{
	int fd;
	uint64_t written = 0;

	if(gwm_queue_size() + len > GWM_QUEUE_MAX_BYTES) return -1;

	cellFsMkdir(GWM_QUEUE_DIR, 0777); /* уже есть — вернёт ошибку, это нормально */

	if(cellFsOpen(GWM_QUEUE_FILE,
	              CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_APPEND,
	              &fd, NULL, 0) != CELL_FS_SUCCEEDED)
		return -1;

	int ret = cellFsWrite(fd, line, len, &written);
	cellFsClose(fd);

	if(ret != CELL_FS_SUCCEEDED || written != len) return -1;
	return 0;
}

/* Читает начало очереди. Обрезает по последнему полному переводу строки:
 * недописанная строка (например, после выдёргивания питания) в батч не пойдёт. */
static uint64_t gwm_queue_read(char *buf, size_t size)
{
	int fd;
	uint64_t got = 0;

	if(cellFsOpen(GWM_QUEUE_FILE, CELL_FS_O_RDONLY, &fd, NULL, 0) != CELL_FS_SUCCEEDED)
		return 0;

	int ret = cellFsRead(fd, buf, size - 1, &got);
	cellFsClose(fd);

	if(ret != CELL_FS_SUCCEEDED || got == 0) return 0;

	while(got && buf[got - 1] != '\n') got--;
	buf[got] = 0;

	return got;
}

/* Выбрасывает из очереди первые consumed байт, сохраняя всё, что дописалось
 * позже. Хвост переносится через временный файл, чтобы обрыв питания посреди
 * операции не порезал ещё не отправленное. */
static int gwm_queue_consume(uint64_t consumed)
{
	int src, dst;
	char chunk[GWM_QUEUE_CHUNK];
	uint64_t pos = 0;

	if(!consumed) return 0;

	if(consumed >= gwm_queue_size())
	{
		cellFsUnlink(GWM_QUEUE_FILE);
		return 0;
	}

	if(cellFsOpen(GWM_QUEUE_FILE, CELL_FS_O_RDONLY, &src, NULL, 0) != CELL_FS_SUCCEEDED)
		return -1;

	if(cellFsLseek(src, (int64_t)consumed, CELL_FS_SEEK_SET, &pos) != CELL_FS_SUCCEEDED)
	{
		cellFsClose(src);
		return -1;
	}

	if(cellFsOpen(GWM_QUEUE_TMP,
	              CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_TRUNC,
	              &dst, NULL, 0) != CELL_FS_SUCCEEDED)
	{
		cellFsClose(src);
		return -1;
	}

	for(;;)
	{
		uint64_t got = 0, put = 0;

		if(cellFsRead(src, chunk, sizeof(chunk), &got) != CELL_FS_SUCCEEDED || !got) break;
		if(cellFsWrite(dst, chunk, got, &put) != CELL_FS_SUCCEEDED || put != got) break;
	}

	cellFsClose(src);
	cellFsClose(dst);

	cellFsUnlink(GWM_QUEUE_FILE);
	cellFsRename(GWM_QUEUE_TMP, GWM_QUEUE_FILE);

	return 0;
}

#endif /* GWM_QUEUE_H */
