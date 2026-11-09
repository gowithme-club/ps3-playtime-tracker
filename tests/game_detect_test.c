/* Хостовый тест детекта игры.
 *
 * Цикл проверки на самой PS3 очень дорогой: собрать в эмулируемой виртуалке,
 * подписать, закинуть по FTP, перезагрузить консоль. Поэтому всё, что можно
 * проверить без железа, проверяется здесь — обычным clang на Mac.
 *
 * vsh подменяется заглушками: имитируется запущенная God of War HD.
 *
 *   make test-host
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Заглушки вместо vsh: имитируем game_plugin с God of War HD */
static char fake_info[0x114];
static int32_t fake_gameInfo(void *out) { memcpy(out, fake_info, sizeof(fake_info)); return 0; }
static void *fake_vtable[16];
static int  fake_running_mode(void) { return 1; }
static int  fake_view_find(const char *n) { return strcmp(n, "game_plugin") ? 0 : 42; }
static void *fake_get_iface(int v, int i) { (void)v; (void)i; return fake_vtable; }

static void *getNIDfunc(const char *mod, uint32_t nid)
{
    if (!strcmp(mod, "vshmain") && nid == 0xEB757101) return (void*)fake_running_mode;
    if (!strcmp(mod, "paf") && nid == 0xF21655F3)     return (void*)fake_view_find;
    if (!strcmp(mod, "paf") && nid == 0x23AFB290)     return (void*)fake_get_iface;
    return NULL;
}

#include "game.h"

int main(void)
{
    fake_vtable[GWM_GAMEINFO_VTABLE_INDEX] = (void*)fake_gameInfo;
    strcpy(fake_info + GWM_GAMEINFO_ID_OFF, "NPUA80490");
    strcpy(fake_info + GWM_GAMEINFO_TT_OFF, "God of War HD");

    gwm_game_t g;
    int r = gwm_get_game(&g);
    printf("detected=%d title_id=%s title=%s\n", r, g.title_id, g.title);
    return !(r == 1 && !strcmp(g.title_id, "NPUA80490") && !strcmp(g.title, "God of War HD"));
}
