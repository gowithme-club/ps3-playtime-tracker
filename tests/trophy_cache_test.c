/* Хостовый тест отметок об уже разобранных наборах трофеев.
 *
 * Ошибка здесь не громкая: набор молча перестанет перечитываться, и выбитый
 * трофей не уедет никогда — ни ошибки в логе, ни повторной попытки. Поэтому
 * проверяется не только «совпало — пропускаем», но и все случаи, когда
 * отметку ставить нельзя.
 */

#define GWM_HOST_TEST 1

#include <stdio.h>

#include "trophy_cache.h"

static int failures = 0;

static void check(const char *what, int got, int want)
{
	if(got == want) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s: получено %d, ожидалось %d\n", what, got, want);
	failures++;
}

/* Время «сейчас» для отметок: файл считается устоявшимся, если он старше
 * GWM_TROPHY_SETTLE_SEC. Берём заведомо давнее прошлое. */
#define NOW   1786000000
#define OLD   (NOW - 60)

int main(void)
{
	static gwm_trophy_cache_t c;

	printf("отметки о наборах трофеев:\n");

	check("новый набор не отмечен",
	      gwm_trophy_cache_seen(&c, 5, "NPWR00331_00", 12128, OLD), 0);

	check("отметка поставилась",
	      gwm_trophy_cache_mark(&c, 5, "NPWR00331_00", 12128, OLD, NOW), 1);

	check("отмеченный набор пропускается",
	      gwm_trophy_cache_seen(&c, 5, "NPWR00331_00", 12128, OLD), 1);

	/* Выбит новый трофей: файл переписан, mtime свежее. */
	check("изменившийся mtime отменяет отметку",
	      gwm_trophy_cache_seen(&c, 5, "NPWR00331_00", 12128, OLD + 5), 0);

	/* Размер по живым файлам живёт своей жизнью и меняется не всегда,
	 * но если изменился — это тоже другой файл. */
	check("изменившийся размер отменяет отметку",
	      gwm_trophy_cache_seen(&c, 5, "NPWR00331_00", 12160, OLD), 0);

	/* Тот же setId у другого профиля — другой файл. У автора NPWR03885_00
	 * есть и у SamplePlayer, и у Eplring; общая отметка выдала бы чужой
	 * набор за отправленный. */
	check("тот же setId у другого профиля не отмечен",
	      gwm_trophy_cache_seen(&c, 4, "NPWR00331_00", 12128, OLD), 0);

	gwm_trophy_cache_mark(&c, 4, "NPWR00331_00", 999, OLD, NOW);
	check("профили не путаются между собой",
	      gwm_trophy_cache_seen(&c, 5, "NPWR00331_00", 12128, OLD), 1);

	/* Главное место. mtime с точностью до секунды: приз, выбитый в ту же
	 * секунду, в которую мы дочитали файл, оставил бы ту же пару
	 * (размер, mtime) — и набор больше не перечитался бы никогда. */
	{
		static gwm_trophy_cache_t fresh;

		check("файл этой же секунды не отмечается",
		      gwm_trophy_cache_mark(&fresh, 5, "NPWR01216_00", 12960, NOW, NOW), 0);

		check("и потому будет перечитан",
		      gwm_trophy_cache_seen(&fresh, 5, "NPWR01216_00", 12960, NOW), 0);

		check("на границе выдержки ещё не отмечается",
		      gwm_trophy_cache_mark(&fresh, 5, "NPWR01216_00", 12960,
		                            NOW - GWM_TROPHY_SETTLE_SEC + 1, NOW), 0);

		check("отлежавшийся отмечается",
		      gwm_trophy_cache_mark(&fresh, 5, "NPWR01216_00", 12960,
		                            NOW - GWM_TROPHY_SETTLE_SEC, NOW), 1);
	}

	/* Повторная отметка обновляет запись, а не добавляет вторую. */
	{
		static gwm_trophy_cache_t reuse;

		gwm_trophy_cache_mark(&reuse, 5, "NPWR00180_00", 12848, OLD, NOW);
		gwm_trophy_cache_mark(&reuse, 5, "NPWR00180_00", 12848, OLD + 5, NOW);

		check("запись обновилась, а не задвоилась", (int)reuse.count, 1);
		check("отметка теперь на новом состоянии",
		      gwm_trophy_cache_seen(&reuse, 5, "NPWR00180_00", 12848, OLD + 5), 1);
	}

	/* Переполнение: лишние наборы просто продолжат разбираться каждый круг,
	 * но уже отмеченные не должны от этого пострадать. */
	{
		static gwm_trophy_cache_t full;
		char id[GWM_TROPHY_SET_ID_MAX];

		for(int i = 0; i < GWM_TROPHY_CACHE_MAX; i++)
		{
			/* NPWR<четыре цифры>_00 — как настоящие. */
			gwm_copy_str(id, sizeof(id), "NPWR0000_00");
			id[4] = (char)('0' + (i / 100) % 10);
			id[5] = (char)('0' + (i / 10) % 10);
			id[6] = (char)('0' + i % 10);

			gwm_trophy_cache_mark(&full, 5, id, 100 + (uint64_t)i, OLD, NOW);
		}

		check("таблица заполнена", (int)full.count, GWM_TROPHY_CACHE_MAX);

		check("сверх ёмкости отметка не ставится",
		      gwm_trophy_cache_mark(&full, 5, "NPWR9999_00", 1, OLD, NOW), 0);

		check("и такой набор будет разбираться каждый круг",
		      gwm_trophy_cache_seen(&full, 5, "NPWR9999_00", 1, OLD), 0);

		check("уже отмеченные не пострадали",
		      gwm_trophy_cache_seen(&full, 5, "NPWR0000_00", 100, OLD), 1);

		/* Место кончилось, но обновлять существующие никто не мешает. */
		check("существующая запись обновляется и на полной таблице",
		      gwm_trophy_cache_mark(&full, 5, "NPWR0000_00", 200, OLD, NOW), 1);
	}

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
