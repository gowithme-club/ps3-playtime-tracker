/* Определение запущенной игры.
 *
 * Работает через интерфейс vsh-плагина game_plugin: он отдаёт и Title ID,
 * и человекочитаемое название, то есть PARAM.SFO разбирать не нужно.
 * Раскладка структуры и NID'ы взяты из webMAN MOD (GPLv3).
 *
 * Ничего из этого не линкуется статически — адреса ищутся в рантайме,
 * см. getNIDfunc в main.c.
 */

#ifndef GWM_GAME_H
#define GWM_GAME_H

#include <stdint.h>
#include <stddef.h>

#include "str.h"

#define GWM_TITLE_ID_LEN 10 /* 9 символов + терминатор */
#define GWM_TITLE_LEN    64

/* Буфер, который заполняет gameInfo: 0x114 байт,
 * +0x04 — Title ID, +0x14 — название. */
#define GWM_GAMEINFO_SIZE   0x114
#define GWM_GAMEINFO_ID_OFF 0x04
#define GWM_GAMEINFO_TT_OFF 0x14

typedef struct
{
	char title_id[GWM_TITLE_ID_LEN];
	char title[GWM_TITLE_LEN];
} gwm_game_t;

/* Индекс gameInfo в таблице методов game_plugin_interface. */
#define GWM_GAMEINFO_VTABLE_INDEX 8

/* Наш собственный экран состояния (app/) — такой же homebrew, и детект видит
 * его ровно как игру. Без этого исключения человек, открывший статус, чтобы
 * проверить, работает ли плагин, получил бы на сервере сессию с игрой
 * «gwm-stats» — и тем длиннее, чем дольше он в неё смотрит.
 *
 * Значение обязано совпадать с APPID из app/Makefile. */
#define GWM_OWN_TITLE_ID "GWMSTATS0"

/* Возвращает 1, если игра запущена, и заполняет out. */
static int gwm_get_game(gwm_game_t *out)
{
	int (*GetCurrentRunningMode)(void);
	int (*View_Find)(const char *);
	void *(*plugin_GetInterface)(int, int);

	out->title_id[0] = out->title[0] = 0;

	GetCurrentRunningMode = getNIDfunc("vshmain", 0xEB757101);
	if(!GetCurrentRunningMode) return 0;

	/* 0 — мы на XMB. Проверка обязательна: во время установки PKG
	 * game_plugin тоже находится, и без неё детект даёт ложное срабатывание. */
	if(GetCurrentRunningMode() == 0) return 0;

	View_Find = getNIDfunc("paf", 0xF21655F3);
	plugin_GetInterface = getNIDfunc("paf", 0x23AFB290);
	if(!View_Find || !plugin_GetInterface) return 0;

	int view = View_Find("game_plugin");
	if(!view) return 0;

	void **iface = (void **)plugin_GetInterface(view, 1);
	if(!iface) return 0;

	int32_t (*gameInfo)(void *) = (int32_t (*)(void *))iface[GWM_GAMEINFO_VTABLE_INDEX];
	if(!gameInfo) return 0;

	char info[GWM_GAMEINFO_SIZE];
	for(size_t i = 0; i < sizeof(info); i++) info[i] = 0;

	if(gameInfo(info) < 0) return 0;

	gwm_copy_str(out->title_id, sizeof(out->title_id), info + GWM_GAMEINFO_ID_OFF);
	gwm_copy_str(out->title,    sizeof(out->title),    info + GWM_GAMEINFO_TT_OFF);

	/* Свой экран состояния игрой не считаем — отвечаем так же, как на XMB. */
	if(gwm_strcmp(out->title_id, GWM_OWN_TITLE_ID) == 0)
	{
		out->title_id[0] = out->title[0] = 0;
		return 0;
	}

	return out->title_id[0] ? 1 : 0;
}

#endif /* GWM_GAME_H */
