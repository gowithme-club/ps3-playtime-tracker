#!/bin/sh
# Переходник между win32-бинарниками Cell SDK и его же системой сборки.
#
# sdk.makedef.mk выбирает хост по `uname`: под Linux он идёт в $(CELL_SDK)/host-linux
# и ждёт исполняемые файлы без расширения. В установщике таких сборок нет — только
# host-win32. Поэтому собираем поверх смонтированного SDK свой каталог host-linux,
# где на каждый .exe лежит шелл-обёртка, вызывающая его через wine.
#
# Оригинальный SDK монтируется только на чтение и не меняется.
set -eu

SDK_WIN="${CELL_SDK_WIN:-/opt/cell-win}"
OUT="${CELL_SDK:-/opt/cell}"

[ -d "$SDK_WIN/host-win32" ] || {
	echo "Cell SDK не смонтирован: нет $SDK_WIN/host-win32" >&2
	echo "запускай с -v \$HOME/ps3sdk/cell:$SDK_WIN:ro" >&2
	exit 1
}

mkdir -p "$OUT/host-linux/bin" "$OUT/host-linux/ppu/bin"
ln -sfn "$SDK_WIN/target"  "$OUT/target"
[ -d "$SDK_WIN/samples" ] && ln -sfn "$SDK_WIN/samples" "$OUT/samples"

wrap_dir() {
	src="$1"; dst="$2"
	[ -d "$src" ] || return 0
	for exe in "$src"/*.exe; do
		[ -e "$exe" ] || continue
		name=$(basename "$exe" .exe)
		printf '#!/bin/sh\nexec wine "%s" "$@"\n' "$exe" > "$dst/$name"
		chmod +x "$dst/$name"
	done
}

wrap_dir "$SDK_WIN/host-win32/bin"     "$OUT/host-linux/bin"
wrap_dir "$SDK_WIN/host-win32/ppu/bin" "$OUT/host-linux/ppu/bin"

export CELL_SDK="$OUT"
export PATH="$OUT/host-linux/bin:$OUT/host-linux/ppu/bin:$PATH"

exec "$@"
