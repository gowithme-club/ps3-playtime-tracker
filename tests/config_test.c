/* Хостовый тест разбора gwm.cfg.
 *
 * Конфиг правит человек в блокноте, а ошибку в нём видно только на консоли:
 * плагин молча уедет на чужой хост или выключит уведомления, которых никто
 * не просил выключать. Поэтому разбор проверяется здесь.
 *
 *   make test-host
 */

#define GWM_HOST_TEST 1

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

static int failures = 0;

static void check_str(const char *what, const char *got, const char *want)
{
	if(strcmp(got, want) == 0) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s: получено «%s», ожидалось «%s»\n", what, got, want);
	failures++;
}

static void check_int(const char *what, int got, int want)
{
	if(got == want) { printf("  ok   %s\n", what); return; }

	printf("  FAIL %s: получено %d, ожидалось %d\n", what, got, want);
	failures++;
}

/* Гоняет строки конфига через разбор, как это делает gwm_config_load. */
static void parse(gwm_config_t *c, const char *text)
{
	char buf[512];
	char *line = buf;

	gwm_cfg_defaults(c);
	snprintf(buf, sizeof(buf), "%s", text);

	while(*line)
	{
		char *nl = line;
		while(*nl && *nl != '\n') nl++;

		int last = (*nl == 0);
		*nl = 0;
		gwm_cfg_line(c, line);

		if(last) break;
		line = nl + 1;
	}
}

int main(void)
{
	gwm_config_t c;

	printf("умолчания:\n");

	gwm_cfg_defaults(&c);
	check_str("хост", c.host, "gowithme.club");
	check_int("порт", c.port, 80);
	check_int("уведомления включены", c.popups, 1);
	check_int("самопропись включена", c.autostart, 1);
	check_str("токена нет", c.token, "");

	printf("минимальный конфиг пользователя:\n");

	parse(&c, "token = abc123\n");
	check_str("токен прочитан", c.token, "abc123");
	check_str("хост остался умолчанием", c.host, "gowithme.club");
	check_int("уведомления остались включены", c.popups, 1);
	check_int("самопропись осталась включённой", c.autostart, 1);

	printf("popups:\n");

	parse(&c, "token = t\npopups = false\n");
	check_int("false выключает", c.popups, 0);

	parse(&c, "token = t\npopups = False\n");
	check_int("регистр не важен", c.popups, 0);

	parse(&c, "token = t\npopups = 0\n");
	check_int("ноль тоже выключает", c.popups, 0);

	parse(&c, "token = t\npopups = off\n");
	check_int("off выключает", c.popups, 0);

	parse(&c, "token = t\npopups = true\n");
	check_int("true включает", c.popups, 1);

	/* Опечатка не должна молча выключать то, чего человек не просил. */
	parse(&c, "token = t\npopups = fasle\n");
	check_int("опечатка оставляет как было", c.popups, 1);

	/* Этим ключом человек отказывается от автозагрузки — и им же не даёт
	 * вернуть строку, убранную руками после падения XMB (TROUBLESHOOTING.md).
	 * Молча не разобранный autostart стоил бы ему повторного падения. */
	printf("autostart:\n");

	parse(&c, "token = t\nautostart = false\n");
	check_int("false выключает", c.autostart, 0);
	check_int("уведомления при этом не тронуты", c.popups, 1);

	parse(&c, "token = t\nautostart = OFF\n");
	check_int("off в верхнем регистре тоже", c.autostart, 0);

	parse(&c, "token = t\nautostart = 0\n");
	check_int("ноль тоже выключает", c.autostart, 0);

	parse(&c, "token = t\nautostart = true\n");
	check_int("true включает", c.autostart, 1);

	parse(&c, "token = t\nautostart = nope\n");
	check_int("опечатка оставляет как было", c.autostart, 1);

	parse(&c, "token = t\npopups = false\n");
	check_int("popups не выключает самопропись", c.autostart, 1);

	printf("разное:\n");

	parse(&c, "# комментарий\ntoken = \"t\"\n; ещё комментарий\nport = 8081\n");
	check_str("кавычки снимаются", c.token, "t");
	check_int("порт перекрыт", c.port, 8081);

	parse(&c, "token = t\nport = 99999\n");
	check_int("невозможный порт игнорируется", c.port, 80);

	parse(&c, "  token   =   t   \r\naccount = SamplePlayer\r\n");
	check_str("пробелы и CRLF", c.token, "t");
	check_str("профиль", c.account, "SamplePlayer");

	if(failures) { printf("провалено: %d\n", failures); return 1; }
	printf("все проверки прошли\n");
	return 0;
}
