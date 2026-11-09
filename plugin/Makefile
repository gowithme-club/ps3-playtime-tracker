# Сборка VSH-плагина. Запускается внутри образа gwm-cellsdk, где win32-бинарники
# Cell SDK обёрнуты в wine (см. docker/cellsdk-entrypoint.sh).
#
# Схема сборки повторяет webMAN MOD: там она проверена годами, а любые отличия
# в CRT или флагах на PS3 вылезают не ошибкой компиляции, а фризом консоли.

CELL_MK_DIR = $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

BUILD_TYPE = release

CRT_HEAD += $(shell ppu-lv2-gcc -print-file-name'='ecrti.o)
CRT_HEAD += $(shell ppu-lv2-gcc -print-file-name'='crtbegin.o)
CRT_TAIL += $(shell ppu-lv2-gcc -print-file-name'='crtend.o)
CRT_HEAD += $(shell ppu-lv2-gcc -print-file-name'='ecrtn.o)

PEXPORTPICKUP = ppu-lv2-prx-exportpickup

PPU_SRCS            = main.c
PPU_PRX_LDLIBS      = -lfs_stub -lrtc_stub -lnet_stub
PPU_PRX_TARGET      = gwm.prx
PPU_PRX_STRIP_FLAGS = -s

# По умолчанию спеки gcc уводят линковку на ps3ppuld — линкер SN Systems из
# ProDG, которого в офлайн-установщике SDK нет ни в одном пакете. Ключ
# -mno-sn-ld переключает на GNU-линкер ppu-lv2-ld, который в тулчейне есть.
#
# webMAN добавляет здесь ещё --strip-unused-data, но это опция линкера SN:
# GNU-линкер её не знает и падает. Ценой чуть большего размера обходимся без неё.
PPU_PRX_LDFLAGS    += -mno-sn-ld

PPU_CFLAGS += -Os -ffunction-sections -fdata-sections \
              -fno-builtin-printf -nodefaultlibs -std=gnu99 \
              -Wno-shadow -Wno-unused-parameter

# Здесь собирается только .prx. Подпись — отдельным шагом снаружи
# (scetool на хосте, см. корневой Makefile, цель sprx): make_fself даёт
# fake self, а плагин должен быть подписан ключами, как это делает webMAN.
all:
	$(MAKE) $(PPU_OBJS_DEPENDS)
	$(PPU_PRX_STRIP) --strip-debug --strip-section-header $(PPU_PRX_TARGET)

include $(CELL_MK_DIR)/sdk.target.mk
