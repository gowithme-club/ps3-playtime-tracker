# Приложение-экран состояния. Собирается открытым тулчейном (PSL1GHT), а не
# Cell SDK: тут обычный homebrew, а не vsh-плагин, и win32-компилятор в
# виртуалке ему не нужен.
#
#   make toolchain     — если тулчейна ещё нет (из корня проекта)
#   make -C app        — EBOOT.BIN и .pkg
#
# TITLE_ID выдуман, но по форме как у homebrew: девять символов, дальше
# по нему консоль заводит каталог в /dev_hdd0/game/. Он же попадает в детект
# игры плагина, поэтому вынесен в один заметный макрос — плагин должен уметь
# не считать наш собственный экран запущенной игрой.

ifeq ($(strip $(PSL1GHT)),)
export PSL1GHT := $(HOME)/ps3dev
endif

# PS3DEV нужен ppu_rules, чтобы найти portlibs (там лежит NoRSX). Без него
# сборка падает на «NoRSX.h: No such file or directory» — но только когда
# что-то реально компилируется, а не берётся готовый .elf.
ifeq ($(strip $(PS3DEV)),)
export PS3DEV := $(HOME)/ps3dev
endif

# Тулчейн в PATH: ppu-g++ лежит в ppu/bin, а pkg/sfo/make_self_npdrm — в bin.
export PATH := $(PS3DEV)/bin:$(PS3DEV)/ppu/bin:$(PATH)

include $(PSL1GHT)/ppu_rules

# Экспортированного PATH хватает компилятору (его зовёт вложенный make через
# shell), но не рецептам здесь: простую команду без метасимволов make запускает
# мимо shell и ищет её в PATH, с которым запустился он сам. Отсюда
# «make: ppu-strip: No such file or directory» при живом тулчейне. Поэтому
# инструменты пакета — абсолютными путями.
STRIP		:=	$(PS3DEV)/ppu/bin/ppu-strip
SPRX		:=	$(PS3DEV)/bin/sprxlinker
SELF_NPDRM	:=	$(PS3DEV)/bin/make_self_npdrm
SFO		:=	$(PS3DEV)/bin/sfo
PKG		:=	$(PS3DEV)/bin/pkg

TARGET		:=	gwm_stats_app
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data
INCLUDES	:=	include

# Версия одна на весь проект и живёт в plugin/version.h. Она попадает
# и в имя пакета, и в подпись под иконкой в XMB: установка — это два клика
# вслепую, и после них надо уметь сказать, что именно сейчас стоит.
# Путь берётся от самого Makefile, а не от $(CURDIR): на втором проходе make
# уходит в build/, и относительный путь из него уже не ведёт к version.h.
VERSION		:=	$(shell sed -n 's/.*GWM_VERSION "\(.*\)".*/\1/p' \
			$(dir $(firstword $(MAKEFILE_LIST)))../plugin/version.h)

TITLE		:=	GWM Stats $(VERSION)
APPID		:=	GWMSTATS0
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
# ICON0.PNG — 320×176, фирменный знак сервиса из gwm-stats/src/public/favicon.svg
# (маджента #FF3AEB, белый «GWM» с подчёркиванием). Исходник лежит рядом,
# в icon0.svg; PNG пересобирается из него так:
#
#   { echo '<html><head><style>html,body{margin:0;background:transparent}</style>\
#     </head><body>'; cat app/icon0.svg; echo '</body></html>'; } > /tmp/icon0.html
#   "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless \
#     --force-device-scale-factor=1 --window-size=320,176 \
#     --default-background-color=00000000 \
#     --screenshot=app/ICON0.PNG file:///tmp/icon0.html
#
# Chrome здесь вместо rsvg/inkscape просто потому, что он есть на машине,
# а они — нет. qlmanage не годится: дополняет картинку до квадрата.
#
# Полотно 320×176, но рисунок — квадрат 176×176 по центру, поля прозрачные.
# Подпись XMB кладёт на фиксированном отступе, и знак, залитый во всю ширину,
# уезжает под текст. У PKGi, который лежит в той же колонке «Сеть», ровно так:
# непрозрачный квадрат 174×174 по центру и прозрачные края.
ICON0		:=	$(CURDIR)/ICON0.PNG
PKGFILE		:=	$(TARGET)-$(VERSION).pkg

# Свой шаблон PARAM.SFO: отличается от штатного одним полем, CATEGORY = CB
# вместо HG. Зачем — написано в самом sfo.xml и в docs/INSTALL.md.
SFOXML		:=	$(CURDIR)/sfo.xml

# EXTRA прокидывает флаги из командной строки, не затирая всё остальное:
# `make -C app probe` собирает через него -DGWM_PROBE. Отдельной цели-сборки
# у пробника нет намеренно — он должен пройти ровно тот же путь упаковки
# и подписи, что и приложение, иначе проверял бы не то.
EXTRA		?=

CFLAGS		:=	-O2 -Wall -Wextra -mcpu=cell $(MACHDEP) $(INCLUDE) $(EXTRA)
CXXFLAGS	:=	$(CFLAGS)
LDFLAGS		:=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS		:=	-lNoRSX -lfreetype -lpngdec -ljpgdec -lz -lnet \
			-lgcm_sys -lrsx -lsysutil -lsysmodule -lio -llv2 -lm

LIBDIRS		:=	$(PORTLIBS) $(PSL1GHT)/ppu

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))
export BUILDDIR	:=	$(CURDIR)/$(BUILD)
export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))

export LD	:=	$(CXX)

export OFILES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o)

# freetype2 кладёт заголовки на уровень глубже, а ft2build.h включает их
# без этого префикса — без явного пути NoRSX/Font.h не собирается.
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(PORTLIBS)/include/freetype2 \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean pkg probe udp hello

# Пробник собирается тем же правилом pkg и тем же тулчейном — иначе проверял бы
# не тот путь, каким приезжает приложение. Своими остаются только каталог сборки
# и имя цели, чтобы .elf приложения не затирался.
#
# APPID при этом НЕ меняется, и это принципиально: под другим Title ID плагин
# посчитал бы пробник запущенной игрой и отправил бы на боевой сервер сессию
# с игрой «пробник». Пробник ставится поверх приложения — оно всё равно
# не работает.
probe:
	@$(MAKE) --no-print-directory -C $(CURDIR) pkg \
		EXTRA=-DGWM_PROBE BUILD=build-probe TARGET=gwm_stats_probe \
		TITLE="GWM Probe $(VERSION)"

# Сетевой пробник: ни графики, ни файлов — только UDP на стенд разработки.
udp:
	@$(MAKE) --no-print-directory -C $(CURDIR) pkg \
		EXTRA=-DGWM_UDP BUILD=build-udp TARGET=gwm_stats_udp \
		TITLE="GWM Udp $(VERSION)"

# Тот же приём для hello world: NoRSX и строка на экране, файлов не трогает.
hello:
	@$(MAKE) --no-print-directory -C $(CURDIR) pkg \
		EXTRA=-DGWM_HELLO BUILD=build-hello TARGET=gwm_stats_hello \
		TITLE="GWM Hello $(VERSION)"

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

# Пакет собирается из .elf в обход штатного правила %.pkg: оно тянет за собой
# %.self, а CEX-подпись (make_self) на macOS падает с Bus error. Для homebrew
# она и не нужна — в PKG кладётся NPDRM-версия, которую делает make_self_npdrm,
# и штатное правило точно так же берёт для неё .elf, а не .self.
#
# Но два шага перед подписью повторить обязательно, и оба берутся из штатного
# правила %.self: `strip` и `sprxlinker`. Подписывается при этом промежуточный
# $(BUILD)/$(TARGET).elf, а не свежесобранный, — и пока эти шаги отсутствовали,
# правило подписывало файл, которого само не создавало. Он оставался с первой
# сборки, и три версии подряд уехали на консоль с одним и тем же старым EBOOT:
# менялась только иконка, то есть ровно то, что лежит в пакете рядом.
pkg: $(BUILD)
	@rm -rf $(BUILD)/pkg && mkdir -p $(BUILD)/pkg/USRDIR
	@cp $(ICON0) $(BUILD)/pkg/ICON0.PNG
	@$(STRIP) $(OUTPUT).elf -o $(BUILD)/$(TARGET).elf
	@$(SPRX) $(BUILD)/$(TARGET).elf
	@$(SELF_NPDRM) $(BUILD)/$(TARGET).elf $(BUILD)/pkg/USRDIR/EBOOT.BIN \
		$(CONTENTID) > /dev/null
	@$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) \
		$(BUILD)/pkg/PARAM.SFO
	@$(PKG) --contentid $(CONTENTID) $(BUILD)/pkg/ $(PKGFILE) > /dev/null
	@ls -la $(PKGFILE)

clean:
	@echo clean ...
	@rm -rf $(BUILD) build-probe build-udp build-hello $(TARGET).elf $(TARGET).self \
		gwm_stats_probe.elf gwm_stats_udp.elf gwm_stats_hello.elf \
		$(TARGET).pkg $(TARGET).gnpdrm.pkg \
		$(TARGET).fake.self

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

# Цель по умолчанию — только .elf. Штатные %.self и %.pkg не подключаем:
# первый из них падает на macOS, а пакет собирает цель pkg снаружи.
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
