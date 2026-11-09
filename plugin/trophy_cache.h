/* Отметки об уже разобранных наборах трофеев.
 *
 * Раньше каждый проход открывал TROPUSR.DAT всех наборов и шёл по всем блокам:
 * у профиля на двенадцать наборов это 622 чтения по 112 байт, чтобы в итоге
 * отправить ровно то же, что и полчаса назад. Набор без единого выбитого
 * трофея разбирался целиком и не отправлялся вообще.
 *
 * Отметка — пара (размер, mtime) на момент последнего полного разбора. Совпало
 * с тем, что показывает cellFsStat, — файл не трогали, открывать нечего.
 * Своего флага «этот приз загружен» не заводим: состояние набора шлётся целиком
 * и диффует его сервер (см. trophy_upload.h), а пометки на каждом призе
 * потребовали бы хранить и согласовывать состояние на каждую пару
 * «набор + номер», ничего не сэкономив на разборе файла.
 *
 * Живёт только в памяти. После перезагрузки плагина таблица пуста, и стартовый
 * проход разбирает все наборы заново — он и так задуман полным, чтобы забрать
 * трофеи, выбитые пока плагин не работал.
 *
 * Ключ — профиль и набор вместе: один и тот же setId бывает у разных профилей
 * (у автора NPWR03885_00 есть и у SamplePlayer, и у Eplring), и общая отметка
 * выдала бы чужой набор за отправленный.
 */

#ifndef GWM_TROPHY_CACHE_H
#define GWM_TROPHY_CACHE_H

#include <stdint.h>
#include <stddef.h>

#include "str.h"

/* У автора 83 набора на четырёх профилях — берём с запасом, но так, чтобы
 * таблица оставалась мелочью на фоне буфера тела (8 КБ). */
#define GWM_TROPHY_CACHE_MAX 128

/* Отметку ставим, только если файл не трогали столько секунд.
 *
 * mtime на PS3 с точностью до секунды, а размер меняться не обязан. Значит
 * приз, выбитый в ту же секунду, в которую мы дочитали файл, дал бы ровно ту
 * же пару (размер, mtime) — и набор считался бы отправленным навсегда.
 * Свежий файл просто не отмечаем: перечитаем его следующим циклом. Цена —
 * один лишний разбор, зато щель закрыта целиком. */
#define GWM_TROPHY_SETTLE_SEC 2

/* "NPWR00331_00" — двенадцать символов, остальное запас. */
#define GWM_TROPHY_SET_ID_MAX 20

typedef struct
{
	int      user;                          /* номер профиля консоли */
	char     set_id[GWM_TROPHY_SET_ID_MAX];
	uint64_t size;
	int64_t  mtime;
} gwm_trophy_mark_t;

typedef struct
{
	gwm_trophy_mark_t marks[GWM_TROPHY_CACHE_MAX];
	uint32_t count;
} gwm_trophy_cache_t;

/* Экземпляр живёт здесь же, а не рядом с выгрузкой: его читает ещё и
 * status.h, чтобы показать, сколько наборов уже разобрано. */
static gwm_trophy_cache_t gwm_trophy_cache;

static gwm_trophy_mark_t *gwm_trophy_cache_find(gwm_trophy_cache_t *c,
                                                int user, const char *set_id)
{
	for(uint32_t i = 0; i < c->count; i++)
		if(c->marks[i].user == user && !gwm_strcmp(c->marks[i].set_id, set_id))
			return &c->marks[i];

	return NULL;
}

/* 1 — набор в этом же состоянии уже разобран, открывать файл незачем. */
static int gwm_trophy_cache_seen(gwm_trophy_cache_t *c, int user,
                                 const char *set_id, uint64_t size, int64_t mtime)
{
	gwm_trophy_mark_t *m = gwm_trophy_cache_find(c, user, set_id);

	return m && m->size == size && m->mtime == mtime;
}

/* Отмечает набор разобранным. now — сырые часы консоли в секундах, те же,
 * в которых файловая система отдаёт mtime.
 *
 * Возвращает 1, если отметка поставлена. 0 — файл слишком свежий (вернёмся
 * к нему позже) или таблица переполнена. Переполнение не вытесняет старые
 * записи: лишний набор просто продолжит разбираться каждый проход, как до
 * появления кэша, а уже отмеченные наборы от этого не пострадают. */
static int gwm_trophy_cache_mark(gwm_trophy_cache_t *c, int user,
                                 const char *set_id, uint64_t size,
                                 int64_t mtime, int64_t now)
{
	if(now - mtime < GWM_TROPHY_SETTLE_SEC) return 0;

	gwm_trophy_mark_t *m = gwm_trophy_cache_find(c, user, set_id);

	if(!m)
	{
		if(c->count >= GWM_TROPHY_CACHE_MAX) return 0;

		m = &c->marks[c->count++];
		m->user = user;
		gwm_copy_str(m->set_id, sizeof(m->set_id), set_id);
	}

	m->size = size;
	m->mtime = mtime;

	return 1;
}

#endif /* GWM_TROPHY_CACHE_H */
