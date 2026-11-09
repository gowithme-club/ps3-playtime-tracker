/* gwm-stats: экран состояния плагина.
 *
 * Плагин живёт в vsh.self и снаружи невидим: ни логов, ни окна, ни способа
 * спросить «ты работаешь?». Пока он слал в заглушку на ноутбуке, это сходило
 * с рук. На боевом сервере не видно уже ничего, и выяснение простейшего
 * вопроса — дошёл ли трофей — превращается в раскопки по FTP.
 *
 * Приложение и плагин — разные процессы, влезть в чужую память нельзя.
 * Канал — файл: плагин каждый цикл переписывает /dev_hdd0/tmp/gwm/status.txt
 * парами «ключ = значение», мы его читаем и рисуем.
 *
 * Признак жизни берём не из поля updated, а из mtime файла. Разница
 * принципиальная: updated записан временем **с поправкой на сервер**, а
 * time(NULL) здесь отдаёт сырые часы консоли, и на консоли, спешащей на
 * минуту, свежий файл выглядел бы устаревшим на эту минуту. mtime и time(NULL)
 * меряются одними и теми же часами, поэтому их разность честная.
 *
 * Подписи латиницей намеренно: кириллица через FreeType в NoRSX не проверена,
 * а экран, который не отрисовался, бесполезнее экрана без русских букв.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

/* sysLv2FsOpen и соседи — inline-обёртки над syscall'ами lv2 прямо
 * в заголовке: ни newlib, ни библиотечных стабов они не тянут. Зачем
 * это здесь — в комментарии к trace() ниже. */
#include <sys/file.h>

#ifdef GWM_UDP
#include <net/net.h>
#include <net/socket.h>
#include <netinet/in.h>
#include <sysmodule/sysmodule.h>
#endif

/* Пробник (GWM_PROBE) собирается без графики намеренно: NoRSX не включается
 * и не линкуется, поэтому из libNoRSX.a и freetype в бинарь не попадает
 * ни одного объекта — а значит и ни одного их глобального конструктора.
 * Это единственный способ проверить «доходит ли дело до main()» кодом,
 * который заведомо не может подвесить консоль. */
#if !defined(GWM_PROBE) && !defined(GWM_UDP)
#include <NoRSX.h>
#endif

#define STATUS_PATH "/dev_hdd0/tmp/gwm/status.txt"
#define LOG_PATH    "/dev_hdd0/tmp/gwm/app.log"

/* Обязан совпадать с APPID из app/Makefile: по нему установщик заводит
 * каталог приложения. Он же стоит в GWM_OWN_TITLE_ID плагина, чтобы тот
 * не считал наш экран запущенной игрой. */
#define APP_ID      "GWMSTATS0"

#define TRACE_SELF  "/dev_hdd0/game/" APP_ID "/USRDIR/trace.log"
#define TRACE_TMP   "/dev_hdd0/tmp/gwm/trace.log"
#define STDIO_SELF  "/dev_hdd0/game/" APP_ID "/USRDIR/stdio.log"

/* Плагин пишет статус каждый цикл опроса (10 секунд). Три пропущенных цикла —
 * уже не «не успел», а «не работает». */
#define STALE_SEC 30

#define MAX_STATUS 2048
#define MAX_FIELDS 24
#define MAX_KEY    32
#define MAX_VAL    192

struct Field
{
	char key[MAX_KEY];
	char val[MAX_VAL];
};

/* Метка шага в обход stdio.
 *
 * app.log не появлялся ни разу — а писался он через fopen. Снаружи это
 * значит сразу две несовместимые вещи: либо до main() дело не доходит,
 * либо доходит, но не открывается файл. Различить их обычным журналом
 * нельзя, он сам сделан из того, что проверяется.
 *
 * Поэтому метки идут прямым syscall'ом lv2. Это самый ранний след,
 * который вообще можно оставить из кода: между ним и инструкцией `sc`
 * нет ни newlib, ни динамического загрузчика.
 *
 * Пишем в два места сразу, и это тоже проверка. USRDIR приложение
 * получило от установщика и права на него имеет заведомо; а пускают ли
 * обычный процесс в /dev_hdd0/tmp/gwm, куда пишет плагин из vsh, —
 * как раз и предстоит выяснить. Файл открывается и закрывается на каждой
 * строке: упавший процесс ничего не дописывает, и последняя строка
 * показывает, докуда дошли. */
static void trace_write(const char *path, const char *msg)
{
	s32 fd = -1;

	if(sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND,
	                &fd, 0666, NULL, 0) != 0) return;

	u64 done = 0;
	sysLv2FsWrite(fd, msg, strlen(msg), &done);
	sysLv2FsWrite(fd, "\n", 1, &done);
	sysLv2FsClose(fd);
}

static void trace(const char *msg)
{
	trace_write(TRACE_SELF, msg);
	trace_write(TRACE_TMP, msg);
}

/* Журнал по шагам. У приложения на консоли нет ни stdout, ни отладчика:
 * если оно падает, снаружи видно только чёрный экран и возврат в XMB.
 * Файл открывается и закрывается на каждой строке намеренно — упавший
 * процесс ничего не дописывает, и последняя строка в файле показывает,
 * докуда дошли. */
static void logf(const char *fmt, ...)
{
	FILE *f = fopen(LOG_PATH, "a");
	if(!f) return;

	fprintf(f, "%ld ", (long)time(NULL));

	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);

	fputc('\n', f);
	fclose(f);
}

static Field fields[MAX_FIELDS];
static int   field_count;

static void copy_trim(char *dst, size_t size, const char *src, size_t len)
{
	while(len && (*src == ' ' || *src == '\t')) { src++; len--; }
	while(len && (src[len - 1] == ' ' || src[len - 1] == '\t' ||
	              src[len - 1] == '\r')) len--;

	if(len > size - 1) len = size - 1;

	memcpy(dst, src, len);
	dst[len] = 0;
}

/* Разбор того же формата, что у gwm.cfg: одна пара на строку, разделитель `=`.
 * Строки без `=` пропускаются молча — файл может оказаться недописанным,
 * плагин переписывает его целиком и мы вполне можем попасть в этот момент. */
static void parse(const char *buf)
{
	field_count = 0;

	while(*buf && field_count < MAX_FIELDS)
	{
		const char *nl = strchr(buf, '\n');
		size_t line_len = nl ? (size_t)(nl - buf) : strlen(buf);
		const char *eq = (const char *)memchr(buf, '=', line_len);

		if(eq)
		{
			copy_trim(fields[field_count].key, MAX_KEY, buf, (size_t)(eq - buf));
			copy_trim(fields[field_count].val, MAX_VAL, eq + 1,
			          line_len - (size_t)(eq - buf) - 1);
			if(fields[field_count].key[0]) field_count++;
		}

		if(!nl) break;
		buf = nl + 1;
	}
}

static const char *value(const char *key)
{
	for(int i = 0; i < field_count; i++)
		if(!strcmp(fields[i].key, key)) return fields[i].val;

	return "-";
}

/* Возвращает 1, если файл прочитан. age — сколько секунд назад его писали. */
static int read_status(char *buf, size_t size, long *age)
{
	struct stat st;

	if(stat(STATUS_PATH, &st) != 0) return 0;

	*age = (long)(time(NULL) - st.st_mtime);

	FILE *f = fopen(STATUS_PATH, "rb");
	if(!f) return 0;

	size_t got = fread(buf, 1, size - 1, f);
	fclose(f);

	buf[got] = 0;
	return 1;
}

/* Код ответа сервера стоит вторым словом в полях heartbeat/sessions/trophies:
 * "2026-08-17T15:04:38Z 200". Ноль — отправки ещё не было. */
static int code_of(const char *v)
{
	const char *sp = strchr(v, ' ');
	return sp ? atoi(sp + 1) : 0;
}

#if !defined(GWM_PROBE) && !defined(GWM_HELLO) && !defined(GWM_UDP)
static u32 code_color(const char *v)
{
	int code = code_of(v);

	if(!code)        return COLOR_GREY;   /* ещё не отправляли */
	if(code == 200)  return COLOR_GREEN;
	if(code == 401 || code == 400 || code == 413) return COLOR_RED;
	return COLOR_YELLOW;                  /* 429, 5xx, сетевые — само пройдёт */
}
#endif /* !GWM_PROBE && !GWM_HELLO */

#if defined(GWM_HELLO)

/* Hello world: проверка вывода как такового.
 *
 * Диск как канал наблюдения отпал — пробник 0.10.9 отработал положенную
 * паузу и вышел штатно, не создав при этом ни одного файла ни одним
 * из способов. Значит main() работает, а пишет приложение в никуда.
 * Остаётся телевизор, и здесь нет ни одного обращения к файлам:
 * только NoRSX, прямоугольник и строка. Что видно на экране — то и есть
 * результат, других показаний не нужно.
 *
 * Прямоугольник рисуется раньше текста намеренно: он не требует ни одного
 * глифа. Есть полоса, нет букв — виноват шрифт, а не вывод вообще.
 *
 * Выход по кнопке PS, но не только: цикл кончается сам через минуту.
 * Один раз консоль уже пришлось гасить кнопкой питания, и если сломан
 * именно ввод, второй раз этого делать не придётся. */
#define HELLO_SECONDS 60

int main(void)
{
	NoRSX *gfx = new NoRSX();

	Background bg(gfx);
	Object shapes(gfx);
	Font font(LATIN2, gfx);

	const u32 left = gfx->width / 12;
	const u32 top  = gfx->height / 3;

	for(unsigned i = 0; i < HELLO_SECONDS && gfx->GetAppStatus(); i++)
	{
		bg.Mono(COLOR_BLACK);

		shapes.Rectangle(left, top - gfx->height / 8,
		                 gfx->width / 6, gfx->height / 20, COLOR_GREEN);

		font.Printf(left, top, COLOR_WHITE, gfx->height / 12, "HELLO WORLD");

		gfx->Flip();
		sleep(1);
	}

	gfx->NoRSX_Exit();
	delete gfx;

	return 0;
}

#elif defined(GWM_PROBE)

/* Пробник записи: где и как приложению вообще можно писать.
 *
 * Прошлый пробник выдержал паузу и вышел штатно, не создав при этом
 * ни одного файла. Значит main() работает, а запись — нет, и до сих пор
 * это было единственным, что мы про неё знаем. Но проверялся тогда один
 * режим открытия: дозапись. Её использовали все журналы проекта разом —
 * `fopen(..., "a")` в app.log и SYS_O_APPEND в метках, — так что молчание
 * всех версий начиная с 0.10.4 объясняется и одной неподдержанной
 * константой, и полным запретом записи. Различить их можно только перебором.
 *
 * Отсюда матрица: пять каталогов на семь способов открытия. Показание —
 * само наличие файла, а имя говорит, каким режимом он создан; ни одной
 * записи в журнал для этого не нужно, что кстати, потому что журнала у нас
 * и нет. Пустой результат тоже ответ: писать нельзя ничем, и наблюдать
 * придётся по сети, как это делает pkgi через dbglogger. */
#define PROBE_SECONDS 12

struct RawMode
{
	const char *name;
	s32         flags;
	s32         mode;
};

int main(void)
{
	static const char *dirs[] = {
		"/dev_hdd0/game/" APP_ID "/USRDIR/",
		"/dev_hdd0/tmp/gwm/",
		"/dev_hdd0/tmp/",
		"/dev_hdd0/",
		"/dev_hdd0/packages/",
	};

	/* mode0 отличается от trunc только правами: в рабочем homebrew
	 * встречаются оба варианта, и какой из них lv2 здесь устроит —
	 * вопрос ровно того же порядка, что и сами флаги. */
	static const RawMode modes[] = {
		{ "trunc",  SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC,  0777 },
		{ "creat",  SYS_O_WRONLY | SYS_O_CREAT,                0777 },
		{ "rdwr",   SYS_O_RDWR   | SYS_O_CREAT,                0777 },
		{ "append", SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND, 0777 },
		{ "mode0",  SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC,  0    },
	};

	const unsigned ndirs  = sizeof(dirs)  / sizeof(dirs[0]);
	const unsigned nmodes = sizeof(modes) / sizeof(modes[0]);

	char path[256];

	for(unsigned d = 0; d < ndirs; d++)
	{
		for(unsigned m = 0; m < nmodes; m++)
		{
			snprintf(path, sizeof(path), "%sp_%s.log", dirs[d], modes[m].name);

			s32 fd = -1;
			if(sysLv2FsOpen(path, modes[m].flags, &fd,
			                modes[m].mode, NULL, 0) != 0) continue;

			u64 done = 0;
			sysLv2FsWrite(fd, "ok\n", 3, &done);
			sysLv2FsClose(fd);
		}

		/* stdio отдельно: у него свой слой поверх тех же syscall'ов,
		 * и именно им писался app.log. */
		snprintf(path, sizeof(path), "%sp_stdio_w.log", dirs[d]);
		FILE *f = fopen(path, "w");
		if(f) { fputs("ok\n", f); fclose(f); }

		snprintf(path, sizeof(path), "%sp_stdio_a.log", dirs[d]);
		f = fopen(path, "a");
		if(f) { fputs("ok\n", f); fclose(f); }
	}

	/* Пауза — признак того, что дело вообще дошло до конца main():
	 * при пустой матрице только она и отличает «писать нельзя»
	 * от «пробник не запустился». */
	sleep(PROBE_SECONDS);

	return 0;
}

#elif defined(GWM_UDP)

/* Пробник по сети. Единственный канал, который остался честным.
 *
 * Диск молчит: пять каталогов на семь режимов открытия дали ноль файлов.
 * Экран молчит: голый hello world показал чёрный кадр. А таймер обманул —
 * и 12-секундная пауза, и 60-секундный цикл одинаково обрываются примерно
 * на десятой секунде, то есть измерялся системный сторож, а не наш код.
 * Вывод «main() работает, раз пауза выдержана» на этом и развалился.
 *
 * Пакет UDP так обмануть нельзя: он либо пришёл на компьютер, либо нет.
 * Пришёл — main() работает, и сеть же дальше станет журналом (так делает
 * pkgi через dbglogger). Не пришёл — значит не работает разом ни один
 * канал вывода, и вопрос уже не про наш код.
 *
 * Шлём с повтором: одиночный пакет мог бы потеряться, а потеря выглядела
 * бы как отрицательный ответ — самая дорогая из возможных ошибок. */
#define UDP_HOST_IP  0xC0A8010DU    /* 192.168.1.13 — стенд разработки */
#define UDP_PORT     18194
#define UDP_ROUNDS   120

static s32 udp_fd = -1;
static struct sockaddr_in udp_dst;

static void udp_send(const char *msg)
{
	if(udp_fd < 0) return;

	netSendTo(udp_fd, msg, strlen(msg), 0,
	          (struct sockaddr *)&udp_dst, sizeof(udp_dst));
}

int main(void)
{
	/* Порядок байт не переставляем намеренно: PowerPC big-endian,
	 * сетевой порядок тот же, htons был бы тождественной операцией. */
	memset(&udp_dst, 0, sizeof(udp_dst));
	udp_dst.sin_len         = sizeof(udp_dst);
	udp_dst.sin_family      = AF_INET;
	udp_dst.sin_port        = UDP_PORT;
	udp_dst.sin_addr.s_addr = UDP_HOST_IP;

	sysModuleLoad(SYSMODULE_NET);
	netInitialize();

	udp_fd = netSocket(AF_INET, SOCK_DGRAM, 0);

	char line[128];

	/* Две минуты жизни — не запас, а измерение. Плагин опрашивает
	 * запущенную игру раз в десять секунд, и различить «отработал
	 * положенное» и «убит сторожем» можно только длительностью, заметно
	 * большей его шага. Десяток опросов подряд с нашим Title ID — ответ,
	 * который не подделать; один-два — тоже ответ, противоположный. */
	for(unsigned i = 0; i < UDP_ROUNDS; i++)
	{
		snprintf(line, sizeof(line), "gwm udp probe: круг %u, сокет %d\n",
		         i, (int)udp_fd);
		udp_send(line);

		/* Заодно раз в десять кругов — попытка записи на диск.
		 * Проверялась она до сих пор только в первые миллисекунды
		 * работы; вдруг дело во времени, а не в правах. */
		if(i % 10 == 0)
		{
			snprintf(line, sizeof(line),
			         "/dev_hdd0/game/GWMPROBE2/USRDIR/late_%u.log", i);

			s32 fd = -1;
			if(sysLv2FsOpen(line, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC,
			                &fd, 0777, NULL, 0) == 0)
			{
				u64 done = 0;
				sysLv2FsWrite(fd, "ok\n", 3, &done);
				sysLv2FsClose(fd);
			}
		}

		sleep(1);
	}

	return 0;
}
#else

int main(void)
{
	trace("01 main");

	/* stdio проверяется отдельно и в двух местах: именно им писался
	 * app.log, которого мы так и не увидели. Результат — не побочная
	 * запись, а показание. */
	FILE *probe = fopen(STDIO_SELF, "a");
	if(probe) { fputs("ok\n", probe); fclose(probe); trace("02 stdio USRDIR ok"); }
	else                                             trace("02 stdio USRDIR FAIL");

	probe = fopen(LOG_PATH, "a");
	if(probe) { fputs("ok\n", probe); fclose(probe); trace("03 stdio tmp ok"); }
	else                                             trace("03 stdio tmp FAIL");

	logf("=== start ===");

	trace("04 перед NoRSX");
	NoRSX *gfx = new NoRSX();
	trace("05 NoRSX создан");
	logf("NoRSX создан: %ux%u", gfx->width, gfx->height);

	Background bg(gfx);
	trace("06 Background готов");
	logf("Background готов");

	Object shapes(gfx);
	trace("07 Object готов");
	logf("Object готов");

	Font font(LATIN2, gfx);
	trace("08 Font готов");
	logf("Font готов");

	char buf[MAX_STATUS];

	/* Раскладка от ширины экрана, чтобы не считать в пикселях под 720p и 1080p
	 * отдельно. Телевизоры заметно режут края, поэтому поля щедрые. */
	const u32 left   = gfx->width / 12;
	const u32 col    = gfx->width / 3;
	const u32 top    = gfx->height / 8;
	const u32 line   = gfx->height / 20;
	const u32 size_h = gfx->height / 18;
	const u32 size_t_= gfx->height / 26;

	logf("раскладка: left=%u col=%u top=%u line=%u", left, col, top, line);

	unsigned frame = 0;

	while(gfx->GetAppStatus())
	{
		long age = 0;
		int  have = read_status(buf, sizeof(buf), &age);

		if(have) parse(buf);
		else field_count = 0;

		int alive = have && age >= 0 && age < STALE_SEC;

		if(frame == 0)
		{
			trace("09 первый кадр: статус прочитан");
			logf("кадр 1: статус have=%d age=%ld полей=%d",
			     have, age, field_count);
		}

		bg.Mono(COLOR_BLACK);

		/* Признак жизни без единого глифа: если на экране есть эта полоса,
		 * а текста нет, значит рисование работает и виноват шрифт. */
		shapes.Rectangle(left, top / 2, gfx->width / 6, top / 4, COLOR_GREEN);

		if(frame == 0)
		{
			trace("10 фон и прямоугольник нарисованы");
			logf("кадр 1: фон и прямоугольник нарисованы");
		}

		font.Printf(left, top, COLOR_WHITE, size_h, "GWM Stats");

		if(frame == 0)
		{
			trace("11 первый Printf прошёл");
			logf("кадр 1: первый Printf прошёл");
		}

		if(!have)
			font.Printf(left, top + line * 2, COLOR_RED, size_t_,
			            "plugin not running (no status file)");
		else if(!alive)
			font.Printf(left, top + line * 2, COLOR_RED, size_t_,
			            "plugin not responding (%ld s since last write)", age);
		else
			font.Printf(left, top + line * 2, COLOR_GREEN, size_t_,
			            "running, last write %ld s ago", age);

		if(have)
		{
			u32 y = top + line * 3;

			struct { const char *label; const char *key; int is_code; } rows[] = {
				{ "version",   "version",   0 },
				{ "account",   "account",   0 },
				{ "server",    "server",    0 },
				{ "token",     "token",     0 },
				{ "clock",     "clock",     0 },
				{ "started",   "started",   0 },
				{ "game",      "game",      0 },
				{ "since",     "since",     0 },
				{ "heartbeat", "heartbeat", 1 },
				{ "sessions",  "sessions",  1 },
				{ "trophies",  "trophies",  1 },
				{ "queue",     "queue",     0 },
				{ "sets",      "sets",      0 },
			};

			for(unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
			{
				const char *v = value(rows[i].key);
				u32 c = rows[i].is_code ? code_color(v) : COLOR_WHITE;

				font.Printf(left, y, COLOR_GREY, size_t_, "%s", rows[i].label);
				font.Printf(left + col, y, c, size_t_, "%s", v);

				y += line;
			}
		}

		font.Printf(left, gfx->height - top / 2, COLOR_GREY, size_t_,
		            "PS button to exit");

		gfx->Flip();

		if(frame == 0)
		{
			trace("12 первый Flip прошёл");
			logf("кадр 1: Flip прошёл");
		}
		else if(frame % 10 == 0) logf("кадр %u, have=%d age=%ld", frame, have, age);

		frame++;

		/* Плагин пишет раз в десять секунд, чаще перечитывать нечего.
		 * Секунда — компромисс: и «возраст» на экране идёт без рывков,
		 * и файл не дёргаем зря. */
		sleep(1);
	}

	trace("13 цикл кончился");
	logf("цикл кончился на кадре %u: GetAppStatus вернул %d",
	     frame, gfx->GetAppStatus());

	gfx->NoRSX_Exit();
	delete gfx;

	trace("14 выход штатный");
	logf("=== выход штатный ===");

	return 0;
}

#endif /* GWM_PROBE */
