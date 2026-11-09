#!/bin/sh
# Стенд для сборки VSH-плагина на Apple Silicon.
#
# Зачем виртуалка. Компилятор Cell SDK — win32-бинарь с вырезанными релокациями,
# он обязан лечь по ImageBase=0x400000. Под user-mode эмуляцией (Rosetta, qemu-user,
# докер с --platform) этот адрес занят самим эмулятором, и wine падает с
# STATUS_CONFLICTING_ADDRESSES. В полноценной x86_64-виртуалке гостевое ядро
# распоряжается адресным пространством само, и конфликта нет.
#
# Требования на хосте: colima, qemu, lima-additional-guestagents (brew).
#
#   ./scripts/vm-setup.sh
set -eu

PROFILE=x86
SDK_HOST="${CELL_SDK_HOST:-$HOME/ps3sdk/cell}"

[ -d "$SDK_HOST/host-win32" ] || {
	echo "не вижу Cell SDK в $SDK_HOST (см. docs/DEVELOPMENT.ru.md, раздел «Cell SDK»)" >&2
	exit 1
}

if ! colima list 2>/dev/null | awk -v p="$PROFILE" '$1==p && $2=="Running"{f=1} END{exit !f}'; then
	echo "== поднимаю x86_64-виртуалку (первый запуск долгий: это эмуляция) =="
	colima start --profile "$PROFILE" --arch x86_64 --vm-type qemu \
		--cpu 4 --memory 6 --disk 40
fi

echo "== ставлю wine в виртуалку =="
colima ssh --profile "$PROFILE" -- sudo sh -c '
	export DEBIAN_FRONTEND=noninteractive
	if ! command -v wine >/dev/null 2>&1; then
		dpkg --add-architecture i386
		apt-get update -qq
		apt-get install -y -qq --no-install-recommends wine wine32:i386 make
	fi
	wine --version'

echo "== настраиваю SDK =="
colima ssh --profile "$PROFILE" -- sudo sh -c "
	set -eu
	SDK='$SDK_HOST'

	# wine отдаёт пути в нижнем регистре (z:/users/...), а Linux регистрозависим —
	# без этой ссылки компилятор не находит даже собственные заголовки.
	[ -e /users ] || ln -s /Users /users

	# sdk.makedef.mk под Linux ищет host-linux с бинарниками без расширения.
	# Собираем такой каталог из шелл-обёрток над .exe.
	mkdir -p /opt/cell/host-linux/bin /opt/cell/host-linux/ppu/bin
	ln -sfn \$SDK/target /opt/cell/target
	ln -sfn \$SDK/samples /opt/cell/samples

	# WINEPATH нужен, чтобы gcc находил ppu-lv2-prx-libgen и прочие свои утилиты:
	# он ищет их по виндовому PATH внутри wine, а не по PATH хоста.
	WP='Z:\\users\\$USER\\ps3sdk\\cell\\host-win32\\ppu\\bin;Z:\\users\\$USER\\ps3sdk\\cell\\host-win32\\bin'

	wrap() {
		for e in \"\$1\"/*.exe; do
			[ -e \"\$e\" ] || continue
			n=\$(basename \"\$e\" .exe)
			{
				printf '#!/bin/sh\n'
				printf 'export WINEDEBUG=-all\n'
				printf 'export WINEPREFIX=\"\${WINEPREFIX:-\$HOME/.wine}\"\n'
				printf 'export WINEPATH=\"%s\"\n' \"\$WP\"
				printf 'exec wine \"%s\" \"\$@\"\n' \"\$e\"
			} > \"\$2/\$n\"
			chmod +x \"\$2/\$n\"
		done
	}

	wrap \$SDK/host-win32/bin     /opt/cell/host-linux/bin
	wrap \$SDK/host-win32/ppu/bin /opt/cell/host-linux/ppu/bin
	echo \"обёрток: \$(ls /opt/cell/host-linux/bin /opt/cell/host-linux/ppu/bin | wc -l)\"
"

echo "== проверка =="
colima ssh --profile "$PROFILE" -- sh -c '
	export PATH=/opt/cell/host-linux/ppu/bin:$PATH
	ppu-lv2-gcc --version 2>/dev/null | head -1'

echo
echo "Готово. Сборка: make sprx"
