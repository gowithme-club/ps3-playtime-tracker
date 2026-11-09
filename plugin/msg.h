/* Тексты уведомлений XMB.
 *
 * Вынесено из main.c отдельным заголовком по одной причине: это единственная
 * часть плагина, которую читает человек у телевизора, а проверка её на
 * консоли стоит перезагрузки. Здесь только сборка строк, без vsh, — значит
 * формат ловится хостовым тестом (tests/msg_test.c), а на консоль уезжает
 * уже проверенным.
 *
 * Формат — две строки: что произошло и чем кончилось. Системный тост
 * переносит текст по '\n' и растягивает коробку под содержимое: ровно так
 * представляется webMAN на загрузке («webMAN 1.47.48q MOD» / «PS3HEN 3.5.0
 * Включено»). Ширина при этом не бесконечна, поэтому вторая строка держится
 * короткой, а длинное — название игры — уходит в первую.
 *
 * Коды ответа печатаются словом и числом сразу: слово нужно человеку у
 * телевизора, число — когда он придёт с этим в issue.
 */

#ifndef GWM_MSG_H
#define GWM_MSG_H

#include <stdint.h>
#include <stddef.h>

#include "str.h"
#include "json.h"
#include "http.h"

/* Название игры (64) плюс самый длинный хвост про доставку. */
#define GWM_MSG_MAX 160

/* Слово вместо голого числа. NULL — названия нет, печатаем код как есть.
 *
 * Отрицательные коды наши собственные и занимают весь диапазон -1..-6
 * (http.h), так что перевести их в слова можно без остатка. Из положительных
 * названы те, где число само по себе ничего не объясняет: 401 — самый
 * частый реальный отказ, 404 обычно значит, что host с портом указывают
 * на чужой сервер. */
static const char *gwm_code_word(int code)
{
	switch(code)
	{
		case GWM_HTTP_ERR_SOCKET:  return "no socket";
		case GWM_HTTP_ERR_RESOLVE: return "host not found";
		case GWM_HTTP_ERR_CONNECT: return "no connect";
		case GWM_HTTP_ERR_SEND:    return "send failed";
		case GWM_HTTP_ERR_RECV:    return "no reply";
		case GWM_HTTP_ERR_PARSE:   return "bad reply";

		case 400: return "bad request";
		case 401: return "bad token";
		case 403: return "forbidden";
		case 404: return "bad path";
		case 413: return "too big";
		case 429: return "rate limited";
	}

	if(code >= 500 && code <= 599) return "server error";

	return NULL;
}

/* «bad token (401)», «no connect (-3)» или «code 418», если слова нет. */
static char *gwm_code_text(char *p, const char *end, int code)
{
	const char *word = gwm_code_word(code);

	if(word)
	{
		p = gwm_append(p, end, word);
		p = gwm_lit(p, end, " (");
	}
	else p = gwm_lit(p, end, "code ");

	if(code < 0)
	{
		p = gwm_lit(p, end, "-");
		p = gwm_u64(p, end, (uint64_t)(-(int64_t)code));
	}
	else p = gwm_u64(p, end, (uint64_t)code);

	if(word) p = gwm_lit(p, end, ")");

	return p;
}

/* Длительность словами. Меньше минуты не округляем до нуля: «0 min» читается
 * как потерянная сессия, хотя сессия честно уехала. */
static char *gwm_dur_text(char *p, const char *end, uint32_t seconds)
{
	if(seconds < 60)
	{
		p = gwm_u64(p, end, seconds);
		return gwm_lit(p, end, " sec");
	}

	uint32_t minutes = seconds / 60;

	if(minutes >= 60)
	{
		p = gwm_u64(p, end, minutes / 60);
		p = gwm_lit(p, end, " h ");
		minutes %= 60;
	}

	p = gwm_u64(p, end, minutes);

	return gwm_lit(p, end, " min");
}

/* Название игры в одну строку. В PARAM.SFO живёт настоящий перенос строки —
 * PS3 так укладывает длинное имя в две строки на XMB («Uncharted 3: Drake's
 * Deception\nGame of the Year»), — и в тосте он растащил бы сообщение на три.
 * Раскладкой распоряжаемся мы, а не редактор PARAM.SFO. Та же причина, что
 * у чистки значений в status.h. */
static char *gwm_title_text(char *p, const char *end, const char *title)
{
	while(*title && p < end - 1)
	{
		unsigned char c = (unsigned char)*title++;
		*p++ = (c < 0x20) ? ' ' : (char)c;
	}
	*p = 0;

	return p;
}

/* Сообщение о закрытой сессии. Возвращает длину.
 *
 * net_ready == 0 — токена нет, отправки не было вовсе. Иначе code: 200 —
 * принято; 400 и 413 — очередь очищена, но сервер данные не взял, и «sent»
 * там писать нельзя; остальное осталось в очереди и уедет позже (upload.h).
 * 0 — отправлять было нечего. */
static size_t gwm_msg_session(char *out, size_t out_size, const char *title,
                              uint32_t seconds, int net_ready, int code)
{
	char *p = out;
	const char *end = out + out_size;

	p = gwm_lit(p, end, "gwm-stats: ");
	p = gwm_title_text(p, end, title);

	p = gwm_lit(p, end, "\nSession ");
	p = gwm_dur_text(p, end, seconds);

	if(!net_ready)    p = gwm_lit(p, end, ", queued (no token)");
	else if(!code)    p = gwm_lit(p, end, ", queued");
	else if(code == 200) p = gwm_lit(p, end, ", sent");
	else
	{
		/* Разница важная: «dropped» — данных больше нет нигде,
		 * «queued» — лежат на диске и поедут снова. */
		p = gwm_lit(p, end, (code == 400 || code == 413) ? ", dropped: "
		                                                 : ", queued: ");
		p = gwm_code_text(p, end, code);
	}

	return (size_t)(p - out);
}

/* Сообщение об отправке трофейного набора. Названий трофеев у нас нет —
 * TROPUSR.DAT отдаёт только числовые id, а setId вида NPWR01234_00 человеку
 * ничего не говорит, — поэтому сообщается сам факт отправки. */
static size_t gwm_msg_trophies(char *out, size_t out_size, int code)
{
	char *p = out;
	const char *end = out + out_size;

	p = gwm_lit(p, end, "gwm-stats: trophies\n");

	if(code == 200) p = gwm_lit(p, end, "Sent to server");
	else
	{
		p = gwm_lit(p, end, "Not sent: ");
		p = gwm_code_text(p, end, code);
	}

	return (size_t)(p - out);
}

#endif /* GWM_MSG_H */
