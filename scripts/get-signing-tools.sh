#!/bin/sh
# Готовит подпись .sprx: собирает scetool из исходников и забирает ключи.
#
# Ключи и кривые берутся из публичного репозитория webMAN MOD (GPLv3). В наш
# репозиторий они не кладутся: vendor/ в .gitignore, а сборка остаётся
# воспроизводимой.
#
# Готовый scetool.exe из webMAN не используется намеренно — он собран под Cygwin
# и требует cygwin1.dll/cygz.dll, которых там нет. Исходники самодостаточны
# (свои aes, sha1, ec, ecdsa), из внешнего нужен только zlib, поэтому проще
# собрать нативно — заодно не нужен wine.
#
#   ./scripts/get-signing-tools.sh
set -eu

DEST="${1:-vendor}"
RAW="https://raw.githubusercontent.com/aldostools/webMAN-MOD/master"
SRC_URL="https://github.com/Zarh/scetool/archive/refs/heads/master.tar.gz"

mkdir -p "$DEST/data"

echo "== ключи (webMAN MOD) =="
for f in keys ldr_curves vsh_curves; do
	curl -sSfL -o "$DEST/data/$f" "$RAW/data/$f"
	printf '  %-12s %s байт\n' "$f" "$(wc -c < "$DEST/data/$f" | tr -d ' ')"
done

echo "== scetool =="
if [ ! -d "$DEST/scetool-src" ]; then
	mkdir -p "$DEST/scetool-src"
	curl -sSfL "$SRC_URL" | tar xz -C "$DEST/scetool-src" --strip-components=1
fi

# -static из их makefile не годится для macOS.
( cd "$DEST/scetool-src" && make CC=cc LDFLAGS="-lz" >/dev/null 2>&1 )

"$DEST/scetool-src/scetool" 2>&1 | head -1

echo
echo "Готово: $DEST"
