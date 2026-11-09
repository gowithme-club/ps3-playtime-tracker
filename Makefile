PS3DEV        ?= $(HOME)/ps3dev
CELL_SDK_HOST ?= $(HOME)/ps3sdk/cell
VM_PROJECT_DIR ?= $(CURDIR)
IMAGE         := gwm-ps3-toolchain
SDK_IMAGE     := gwm-cellsdk

# Версия живёт в plugin/version.h и оттуда же попадает в имя артефакта.
VERSION       := $(shell sed -n 's/.*GWM_VERSION "\(.*\)".*/\1/p' plugin/version.h)

# Запускать только на настоящем x86_64 — под user-mode эмуляцией wine не поднимет
# win32-компилятор (см. docs/DEVELOPMENT.ru.md, «Билд-хост»). На Apple Silicon:
#   colima start --profile x86 --arch x86_64 --vm-type qemu
DOCKER_SDK := docker run --rm \
	-v $(CURDIR):/src \
	-v $(CELL_SDK_HOST):/opt/cell-win:ro \
	$(SDK_IMAGE)

.PHONY: toolchain tools-check docker-toolchain docker-shell sdk-image sdk-check sprx sdk-shell test-host vm vm-shell signing-tools

# Тесты, которые не требуют ни PS3, ни Cell SDK — гоняются нативно на хосте.
test-host:
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_game_detect_test tests/game_detect_test.c
	@/tmp/gwm_game_detect_test && echo "детект игры: OK"
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_record_test tests/record_test.c
	@/tmp/gwm_record_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_http_test tests/http_test.c
	@/tmp/gwm_http_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_clock_test tests/clock_test.c
	@/tmp/gwm_clock_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_trophy_test tests/trophy_test.c
	@/tmp/gwm_trophy_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_trophy_cache_test tests/trophy_cache_test.c
	@/tmp/gwm_trophy_cache_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_offline_time_test tests/offline_time_test.c
	@/tmp/gwm_offline_time_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_sha1_test tests/sha1_test.c
	@/tmp/gwm_sha1_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_msg_test tests/msg_test.c
	@/tmp/gwm_msg_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_config_test tests/config_test.c
	@/tmp/gwm_config_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_autostart_test tests/autostart_test.c
	@/tmp/gwm_autostart_test
	@cc -std=gnu99 -Wall -Wextra -Iplugin -o /tmp/gwm_checkpoint_test tests/checkpoint_test.c
	@/tmp/gwm_checkpoint_test

# Стенд для сборки плагина: x86_64-виртуалка с wine и Cell SDK.
vm:
	./scripts/vm-setup.sh

# Подписывающие утилиты: scetool из исходников + ключи.
signing-tools:
	./scripts/get-signing-tools.sh

# Аргументы подписи — те же, что webMAN использует для своих vsh-плагинов.
SCETOOL       := vendor/scetool-src/scetool
SCETOOL_FLAGS := -0 SELF -1 TRUE -s FALSE -2 0A -3 1070000052000001 -4 01000002 \
                 -5 APP -6 0003004000000000 -A 0001000000000000 \
                 --self-ctrl-flags 4000000000000000000000000000000000000000000000000000000000000002

# Компиляция идёт в виртуалке (нужен win32-компилятор SDK), подпись — на хосте
# нативным scetool. Каталог проекта примонтирован в виртуалку, так что
# артефакты появляются здесь же.
sprx:
	colima ssh --profile x86 -- sh -c '\
		cd $(VM_PROJECT_DIR)/plugin && \
		export CELL_SDK=/opt/cell PATH=/opt/cell/host-linux/bin:/opt/cell/host-linux/ppu/bin:$$PATH && \
		make' 2>&1 | grep -viE 'fixme:|winediag|wine: created|has been updated'
	@test -x $(SCETOOL) || { echo "нет scetool — сделай: make signing-tools"; exit 1; }
	@cd plugin && ln -sfn ../vendor/data data && \
		../$(SCETOOL) $(SCETOOL_FLAGS) -e gwm.prx gwm.sprx | tail -2
	@mkdir -p dist && cp plugin/gwm.sprx dist/gwm-$(VERSION).sprx
	@ls -la plugin/gwm.prx dist/gwm-$(VERSION).sprx

vm-shell:
	colima ssh --profile x86

# Образ с Cell SDK под wine — для тех, у кого настоящий x86_64 Linux.
# На Apple Silicon не работает, см. docs/DEVELOPMENT.ru.md.
sdk-image:
	docker build --platform linux/amd64 -f docker/Dockerfile.cellsdk -t $(SDK_IMAGE) docker/

sdk-check:
	$(DOCKER_SDK) sh -c 'ppu-lv2-gcc --version | head -2'

sdk-shell:
	docker run --rm -it -v $(CURDIR):/src -v $(CELL_SDK_HOST):/opt/cell-win:ro $(SDK_IMAGE)

# Открытый тулчейн, нативно (macOS arm64/x64, Linux x64).
toolchain:
	PS3DEV=$(PS3DEV) ./scripts/get-toolchain.sh

# Что умеет установленный тулчейн, и чего в нём нет для VSH-плагина.
tools-check:
	@export PATH="$(PS3DEV)/bin:$(PS3DEV)/ppu/bin:$$PATH"; \
	printf '%-16s %s\n' compiler "$$(ppu-gcc --version 2>/dev/null | head -1)"; \
	for t in ppu-gcc ppu-ld sprxlinker fself make_self_npdrm pkg sfo; do \
	  printf '%-16s %s\n' $$t "$$(command -v $$t || echo MISSING)"; done; \
	printf '%-16s ' '-mprx'; \
	echo 'int f(){return 0;}' > /tmp/gwm_prx_probe.c; \
	if ppu-gcc -mprx -c /tmp/gwm_prx_probe.c -o /tmp/gwm_prx_probe.o 2>/dev/null; \
	  then echo "есть"; else echo "НЕТ (ожидаемо: нужен Cell SDK)"; fi; \
	rm -f /tmp/gwm_prx_probe.c /tmp/gwm_prx_probe.o

# Альтернатива для тех, кто не хочет ставить тулчейн в систему.
# На Apple Silicon работает через эмуляцию, то есть заметно медленнее нативного.
docker-toolchain:
	docker build --platform linux/amd64 -t $(IMAGE) docker/

docker-shell:
	docker run --rm -it -v $(CURDIR):/src -w /src $(IMAGE)
