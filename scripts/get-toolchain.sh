#!/bin/sh
# Ставит открытый тулчейн ps3dev (ps3toolchain + PSL1GHT + ps3libraries).
# Готовые сборки берутся из nightly-релизов ps3dev/ps3dev — компилировать
# gcc из исходников не нужно.
#
# Этим тулчейном собирается всё, кроме самого VSH-плагина: установочный PKG
# и homebrew-приложение конфигуратора. Для .sprx нужен Cell SDK (см. docs/DEVELOPMENT.ru.md).
#
#   ./scripts/get-toolchain.sh              # в ~/ps3dev
#   PS3DEV=/opt/ps3dev ./scripts/get-toolchain.sh
set -eu

PS3DEV="${PS3DEV:-$HOME/ps3dev}"
REPO="ps3dev/ps3dev"

case "$(uname -s)-$(uname -m)" in
	Darwin-arm64)  ASSET="ps3dev-macos-ARM64.tar.gz" ;;
	Darwin-x86_64) ASSET="ps3dev-macos-X64.tar.gz" ;;
	Linux-x86_64)  ASSET="ps3dev-linux-X64.tar.gz" ;;
	*) echo "нет готовой сборки для $(uname -s)-$(uname -m); собери из исходников: https://github.com/$REPO" >&2; exit 1 ;;
esac

echo "платформа : $(uname -s)-$(uname -m) -> $ASSET"

# ps3dev публикует только nightly-предрелизы, поэтому /releases/latest отдаёт 404 —
# берём первый (самый свежий) из общего списка.
URL=$(curl -sSf "https://api.github.com/repos/$REPO/releases?per_page=1" \
	| sed -n 's/.*"browser_download_url": *"\([^"]*'"$ASSET"'\)".*/\1/p' | head -1)
[ -n "$URL" ] || { echo "не нашёл $ASSET в последнем релизе $REPO" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "качаю    : $URL"
curl -sSfL -o "$TMP/ps3dev.tar.gz" "$URL"

# В архиве один каталог ps3dev/ — распаковываем его содержимое в $PS3DEV.
mkdir -p "$PS3DEV"
tar xzf "$TMP/ps3dev.tar.gz" -C "$TMP"
[ -d "$TMP/ps3dev" ] || { echo "неожиданная структура архива" >&2; exit 1; }
tar cf - -C "$TMP/ps3dev" . | tar xf - -C "$PS3DEV"

export PS3DEV PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH"
echo "проверка : $(ppu-gcc --version | head -1)"

cat <<EOF

Готово: $PS3DEV
Добавь в свой профиль:

  export PS3DEV=$PS3DEV
  export PSL1GHT=\$PS3DEV
  export PATH=\$PS3DEV/bin:\$PS3DEV/ppu/bin:\$PATH
EOF
