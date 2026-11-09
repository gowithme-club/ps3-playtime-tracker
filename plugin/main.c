/* gwm-stats-ps3 — резидентный VSH-плагин.
 *
 * Опрашивает, что запущено, собирает закрытые сессии в очередь на диске
 * и отправляет их на сервер, плюс heartbeat раз в минуту.
 * Протокол: docs/ingest-api-v1-client.md.
 *
 * Каркас модуля повторяет wm_vsh_menu из webMAN MOD (GPLv3) буквально.
 * Это не стилистика, а требование: module_start исполняется как поток внутри
 * vsh и обязан завершить себя через _sys_ppu_thread_exit. Если просто вернуть
 * значение, XMB зависает намертво.
 */

#include <sys/syscall.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>

#include <stddef.h>
#include <stdint.h>

#include "version.h"
#include "str.h"

SYS_MODULE_INFO(gwm_stats, 0, 1, 0);
SYS_MODULE_START(gwm_start);
SYS_MODULE_STOP(gwm_stop);

int gwm_start(uint64_t arg);
int gwm_stop(void);

#define GWM_THREAD_NAME      "gwm_stats_thread"
#define GWM_STOP_THREAD_NAME "gwm_stats_stop_thread"

/* Приоритет и стек — как у poll_thread в webMAN, а не как у vsh_menu.
 * Это принципиально: gameInfo уходит вглубь paf, и на 8 КБ стека,
 * которых хватает оверлею vsh_menu, консоль виснет на первом же опросе.
 * webMAN на тот же вызов отводит 48 КБ. */
#define GWM_THREAD_PRIO   0xA00
#define GWM_THREAD_STACK  0xC000

/* Стоп-поток ничего тяжёлого не делает, ему хватает малого стека. */
#define GWM_STOP_STACK    0x2000

/* Ждём, пока поднимется XMB: слишком раннее уведомление не покажется.
 * Десяти секунд хватает — autoexec.bat грузит плагин уже после «wait xmb». */
#define GWM_BOOT_DELAY_SEC 10

/* Как часто спрашиваем, что запущено. */
#define GWM_POLL_SEC 10

/* Heartbeat раз в 60 секунд, как просит документ сервера. */
#define GWM_HEARTBEAT_POLLS 6

/* Перечитываем конфиг раз в 5 минут, пока его нет. */
#define GWM_CONFIG_RECHECK_POLLS 30

/* В заголовках SDK такой константы нет, vsh_menu определяет её у себя. */
#define GWM_THREAD_NONE ((sys_ppu_thread_t)-1)

static sys_ppu_thread_t gwm_tid = GWM_THREAD_NONE;
static volatile int gwm_running = 0;

/* Рабочий поток выставляет это последним действием. Нужен, чтобы не
 * освобождать память модуля, пока в ней ещё исполняется код: join сам по себе
 * доверия не заслуживает, если tid успел устареть. */
static volatile int gwm_thread_done = 0;

/* Сколько ждём подтверждения выхода, прежде чем сдаться и всё равно
 * освобождать память: висеть в выгрузке вечно хуже, чем рискнуть. */
#define GWM_STOP_WAIT_SEC 5

/* Выход из потока напрямую сисколлом — так делает vsh_menu. */
static inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(41, val);
}

static inline sys_prx_id_t prx_get_module_id_by_address(void *addr)
{
	system_call_1(461, (uint64_t)(uint32_t)addr);
	return (int32_t)p1;
}

/* Таблица экспортов vsh лежит по фиксированному адресу внутри процесса vsh.self.
 * Идём по ней, находим модуль по имени и функцию по её NID. */
static void *getNIDfunc(const char *vsh_module, uint32_t fnid)
{
	uint32_t table = (*(uint32_t *)0x1008C) + 0x984;

	while(*(uint32_t *)table)
	{
		uint32_t *export_stru_ptr = (uint32_t *)*(uint32_t *)table;
		const char *lib_name_ptr = (const char *)*(uint32_t *)((char *)export_stru_ptr + 0x10);

		/* Сравниваем имя целиком: у wm_proxy тут strncmp по длине найденного
		 * имени, и более короткое имя может совпасть с чужим модулем. */
		if(gwm_strcmp(vsh_module, lib_name_ptr) == 0)
		{
			uint32_t lib_fnid_ptr = *(uint32_t *)((char *)export_stru_ptr + 0x14);
			uint32_t lib_func_ptr = *(uint32_t *)((char *)export_stru_ptr + 0x18);
			uint16_t count = *(uint16_t *)((char *)export_stru_ptr + 6);

			for(uint16_t i = 0; i < count; i++)
			{
				if(fnid == *(uint32_t *)((char *)lib_fnid_ptr + i * 4))
					return (void **)*((uint32_t *)(lib_func_ptr) + i);
			}
		}
		table += 4;
	}
	return NULL;
}

/* Показывать ли уведомления. Выставляется из конфига (popups), до его чтения
 * считаем, что да: молчащий по умолчанию плагин выглядит как неработающий. */
static int gwm_popups = 1;

/* vshtask: уведомление в правом верхнем углу XMB */
static void show_msg(const char *msg)
{
	if(!gwm_popups) return;

	int (*vshtask_notify)(int, const char *) = getNIDfunc("vshtask", 0xA02D46E7);
	if(vshtask_notify) vshtask_notify(0, msg);
}

#include "game.h"
#include "account.h"
#include "session.h"
#include "status.h"
#include "upload.h"
#include "autostart.h"
#include "msg.h"
#include "trophy_upload.h"

/* Диагностика: показывает код ответа прямо на экране. Плагин по умолчанию
 * молчит о результатах отправки, и без этого причину сбоя видно только
 * по логам сервера — которых может и не быть. */
static void gwm_show_code(const char *what, int code)
{
	char msg[80], *p = msg;
	const char *end = msg + sizeof(msg);

	p = gwm_append(p, end, "gwm-stats ");
	p = gwm_append(p, end, what);
	p = gwm_append(p, end, ": ");

	if(code < 0)
	{
		p = gwm_append(p, end, "err -");
		gwm_u64(p, end, (uint64_t)(-code));
	}
	else gwm_u64(p, end, (uint64_t)code);

	show_msg(msg);
}

/* Сон дольками по секунде с проверкой флага.
 *
 * Одним куском спать нельзя: выгрузка ждёт выхода рабочего потока, а поток
 * замечает флаг только когда просыпается. Десятисекундный сон превращал
 * unloadprx в десятисекундную паузу внутри vsh — и это ещё лучший случай. */
static void gwm_sleep(unsigned seconds)
{
	for(unsigned i = 0; i < seconds && gwm_running; i++) sys_timer_sleep(1);
}

/* Отложенное уведомление о закрытой сессии: текст готов, показать его надо
 * следующим циклом, когда XMB уже нарисован. */
static int  gwm_notify_pending = 0;
static char gwm_notify_msg[GWM_MSG_MAX];

/* Пропись в автозагрузку — при первом же успешном ответе сервера, в какой бы
 * момент он ни случился. Не только на старте: сеть на консоли может подняться
 * позже плагина, а токен — появиться в конфиге уже после загрузки, и тогда
 * единственная попытка на старте не состоялась бы вовсе.
 *
 * Попытка одна на запуск. Ответ 200 означает, что плагин поднялся, токен
 * верный и сеть есть, — раньше этого прописываться нельзя. А после — файл
 * под нами не меняется: строку либо дописали, либо она там уже была, либо мы
 * сознательно не тронули файл. Перечитывать его каждую минуту незачем.
 */
static int gwm_autostart_pending = 1;

static void gwm_autostart_try(int code, const gwm_config_t *cfg)
{
	if(code != 200 || !cfg->autostart || !gwm_autostart_pending) return;

	if(gwm_autostart_ensure()) show_msg("gwm-stats\nAutostart enabled");

	gwm_autostart_pending = 0;
}

static void gwm_thread(uint64_t arg)
{
	(void)arg;

	gwm_sleep(GWM_BOOT_DELAY_SEC);

	gwm_config_t cfg;
	gwm_session_t session;
	char account[GWM_ACCOUNT_MAX];

	gwm_session_init(&session);

	/* Выгрузили, пока мы ждали XMB, — уходим не начиная работу: иначе
	 * впереди ещё конфиг, уведомление и heartbeat со своими таймаутами. */
	if(!gwm_running) { gwm_thread_done = 1; sys_ppu_thread_exit(0); }

	int net_ready = gwm_config_load(&cfg);
	gwm_popups = cfg.popups;

	/* Имя профиля определяется на старте каждой сессии, а не один раз.
	 * Здесь — только для уведомления о загрузке: кого плагин видит текущим
	 * в эту секунду. Дальше по коду этот снимок не расходится, статус
	 * перечитывает имя сам каждым циклом (см. status.h). */
	if(cfg.account[0]) gwm_copy_str(account, sizeof(account), cfg.account);
	else gwm_account_detect(account, sizeof(account));

	{
		char msg[96], *p = msg;
		const char *end = msg + sizeof(msg);

		p = gwm_append(p, end, "gwm-stats " GWM_VERSION);
		p = gwm_append(p, end, net_ready ? " loaded: " : " offline (no token): ");
		gwm_append(p, end, account[0] ? account : "no account");

		show_msg(msg);
	}

	/* Момент старта в статусе: по нему видно, пережил ли плагин перезагрузку
	 * консоли и когда его последний раз поднимали. Тик сырой — поправку
	 * status.h добавит при выводе, когда она будет выучена. */
	gwm_status.started_tick = gwm_tick_raw();

	/* Синхронизируем часы до входа в цикл: иначе сессия, закрывшаяся в первые
	 * полминуты, уедет с некорректированными метками. Ответ на этот запрос
	 * и приносит поправку. */
	if(net_ready)
	{
		int code = gwm_send_heartbeat(&cfg, &session);
		gwm_show_code("hb", code);
		gwm_autostart_try(code, &cfg);
		gwm_show_code(gwm_clock_synced ? "clock offset sec" : "clock NOT synced",
		              (int)(gwm_clock_offset / 1000000));
	}

	uint32_t polls_since_hb = 0;
	uint32_t polls_since_cfg = 0;

	while(gwm_running)
	{
		/* Секунда сверху к паузе цикла: перехода игра→XMB десяти секунд
		 * хватает не всегда, а тост, попавший в заставку, теряется целиком. */
		if(gwm_notify_pending)
		{
			sys_timer_sleep(1);
			show_msg(gwm_notify_msg);
			gwm_notify_pending = 0;
		}

		gwm_game_t game;
		int playing = gwm_get_game(&game);

		if(gwm_session_update(&session, &game, playing, cfg.account))
		{
			int code = 0;

			if(net_ready && gwm_running)
			{
				/* Закрытая сессия уже в очереди — отправляем сразу. */
				code = gwm_upload_sessions(&cfg);

				/* И сразу же heartbeat: иначе дашборд узнает о смене игры
				 * только через минуту. Приём из gwm-stats-switch. */
				gwm_send_heartbeat(&cfg, &session);

				/* «Отправлено» — только если очередь после этого пуста.
				 * Батч ограничен четырьмя килобайтами (upload.h), и при
				 * накопленном офлайне закрытая только что сессия могла
				 * в него не попасть: код 200 тогда про чужие записи. */
				if(code == 200 && gwm_queue_size()) code = 0;
			}

			/* Уведомление не показывается здесь, а откладывается на
			 * следующий цикл: показ в момент закрытия сессии проигрывает
			 * гонку с чёрной заставкой перехода игра→XMB, и тост пропадает
			 * незамеченным. Проверено на консоли 18 августа 2026, и тем же
			 * кончилось у PS3PlaytimeTracker — там уведомление точно так же
			 * кладётся в буфер и выстреливает циклом позже.
			 *
			 * По возвращаемому значению gwm_session_update показывать нельзя,
			 * оно истинно и на старте игры; признак закрытия ставит сама
			 * сессия (session.h). */
			if(gwm_last_closed.valid)
			{
				gwm_msg_session(gwm_notify_msg, sizeof(gwm_notify_msg),
				                gwm_last_closed.title, gwm_last_closed.seconds,
				                net_ready, code);
				gwm_notify_pending = 1;

				gwm_last_closed.valid = 0;
			}

			polls_since_hb = 0;
		}

		if(net_ready && gwm_running && ++polls_since_hb >= GWM_HEARTBEAT_POLLS)
		{
			gwm_autostart_try(gwm_send_heartbeat(&cfg, &session), &cfg);

			/* Очередь могла не уехать раньше из-за сети — пробуем снова. */
			if(gwm_queue_size()) gwm_upload_sessions(&cfg);

			polls_since_hb = 0;
		}

		/* Трофеи проверяем каждый цикл: круг по неизменившимся наборам — это
		 * один cellFsStat на набор, дешевле одного heartbeat. Отдельного
		 * расписания больше нет, и «контрольный проход раз в полчаса» тоже:
		 * при таком опросе выбитый трофей уезжает за десяток секунд, а не
		 * ждёт своей очереди. Дорогая часть по-прежнему одна за цикл. */
		if(net_ready && gwm_running) gwm_trophy_scan(&cfg);

		/* Статус пишется каждым циклом и без сети тоже: без токена он —
		 * единственный способ понять, что плагин вообще поднялся. Поле
		 * updated и есть признак жизни. */
		gwm_status_write(&cfg, &session, net_ready);

		/* Конфиг мог появиться уже после загрузки плагина: перечитываем,
		 * чтобы не требовать перезагрузки консоли ради вписанного токена. */
		if(!net_ready && ++polls_since_cfg >= GWM_CONFIG_RECHECK_POLLS)
		{
			net_ready = gwm_config_load(&cfg);
			gwm_popups = cfg.popups;

			if(net_ready) show_msg("gwm-stats: config loaded");

			polls_since_cfg = 0;
		}

		gwm_sleep(GWM_POLL_SEC);
	}

	gwm_thread_done = 1;
	sys_ppu_thread_exit(0);
}

static void gwm_stop_thread(uint64_t arg)
{
	(void)arg;

	gwm_running = 0;

	uint64_t exit_code;
	if(gwm_tid != GWM_THREAD_NONE) sys_ppu_thread_join(gwm_tid, &exit_code);

	/* Join мог вернуться и по устаревшему tid. Ждём подтверждения от самого
	 * потока, но не бесконечно: зависшая выгрузка вешает vsh так же надёжно,
	 * как всё остальное. */
	for(int i = 0; i < GWM_STOP_WAIT_SEC && !gwm_thread_done; i++)
		sys_timer_sleep(1);

	sys_ppu_thread_exit(0);
}

/* Освобождение памяти модуля перед выгрузкой. */
static void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = prx_get_module_id_by_address(finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (uint64_t)(uint32_t)meminfo);
}

int gwm_start(uint64_t arg)
{
	gwm_running = 1;

	sys_ppu_thread_create(&gwm_tid, gwm_thread, arg,
	                      GWM_THREAD_PRIO, GWM_THREAD_STACK, 1, GWM_THREAD_NAME);

	/* Обязательно: этот поток должен завершиться сам, иначе vsh встаёт колом. */
	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}

int gwm_stop(void)
{
	sys_ppu_thread_t t;
	uint64_t exit_code;

	int ret = sys_ppu_thread_create(&t, gwm_stop_thread, 0,
	                                0, GWM_STOP_STACK, 1, GWM_STOP_THREAD_NAME);
	if(ret == 0) sys_ppu_thread_join(t, &exit_code);

	/* Между sys_ppu_thread_exit рабочего потока и фактическим освобождением
	 * его стека проходит ненулевое время, а finalize_module отдаёт память
	 * модуля — вместе с кодом, который в этот момент ещё доисполняется.
	 * Секунда паузы стоит дёшево, отладка зависшего XMB — дорого. */
	sys_timer_sleep(1);

	finalize_module();

	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}
