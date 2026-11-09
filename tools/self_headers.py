#!/usr/bin/env python3
"""Заголовки SELF/ELF, без расшифровки — чтобы сравнивать наш EBOOT с чужим.

Контейнер SELF хранит копию `Elf64_Ehdr` и таблицу сегментов открытым текстом:
lv2 читает их до расшифровки тела, поэтому и мы можем. Ключи и scetool
для такой сверки не нужны, а значит она стоит секунды и делается на любой
машине.

Зачем: расследование в docs/app-pkg-debug.md упиралось в «процесс есть,
main() не выполняется», и заголовки — первое место, где такое видно.
Скрипт печатает точку входа, OS/ABI и все сегменты, а для sys_process_param
(`PT_PROC_PARAM`, тип 0x60000001) разбирает поля: размер стека и приоритет
главной нити. Нули там объяснили бы симптом целиком — но у рабочего PKGi
сегмент пуст ровно так же, чем гипотеза и убита.

    python3 tools/self_headers.py app/build-udp/gwm_stats_udp.elf pkgi_EBOOT.BIN

Чужой EBOOT берётся с консоли по FTP, которым webMAN раздаёт диск:

    curl -o pkgi_EBOOT.BIN ftp://192.168.1.38/dev_hdd0/game/NP00PKGI3/USRDIR/EBOOT.BIN

Голый .elf (до подписи) разбирается тоже — у него содержимое сегментов
доступно, поэтому поля sys_process_param печатаются значениями, а не
пометкой «зашифровано».
"""

import struct
import sys

# Типы сегментов, которые PS3 добавляет к обычным: параметры процесса
# и параметры PRX. Всё остальное печатается номером.
PT_NAMES = {
    1: "LOAD",
    6: "PHDR",
    7: "TLS",
    0x60000001: "PROC_PARAM",
    0x60000002: "PRX_PARAM",
}


def read_headers(data):
    """Вернуть (смещение ELF, смещение таблицы сегментов, разобранный Ehdr).

    Для SELF оба смещения лежат в его собственном заголовке — таблица
    сегментов вынесена из ELF и продублирована отдельно.
    """
    if data[:4] == b"SCE\0":
        key_revision, header_type = struct.unpack(">HH", data[8:12])
        elf_off, phdr_off = struct.unpack(">QQ", data[0x30:0x40])
        kind = f"SELF: ревизия ключей {key_revision}, тип заголовка {header_type}"
    elif data[:4] == b"\x7fELF":
        elf_off, phdr_off, kind = 0, None, "голый ELF"
    else:
        raise ValueError("не SELF и не ELF")

    elf = data[elf_off:]
    if elf[:4] != b"\x7fELF":
        raise ValueError("по смещению из заголовка SELF нет ELF")

    e_type, _ = struct.unpack(">HH", elf[16:20])
    e_entry, e_phoff = struct.unpack(">QQ", elf[24:40])
    e_phentsize, e_phnum = struct.unpack(">HH", elf[54:58])
    if phdr_off is None:
        phdr_off = elf_off + e_phoff

    print(f"  {kind}")
    print(f"  тип {e_type}, OS/ABI 0x{elf[7]:02x}, точка входа 0x{e_entry:x},"
          f" сегментов {e_phnum}")
    return elf_off, phdr_off, e_phentsize, e_phnum


def segments(data, phdr_off, phentsize, phnum):
    """Напечатать таблицу сегментов и вернуть её же словарём по типу."""
    found = {}
    for i in range(phnum):
        base = phdr_off + i * phentsize
        p_type, p_flags = struct.unpack(">II", data[base:base + 8])
        p_off, _, _, p_filesz, p_memsz, _ = struct.unpack(
            ">QQQQQQ", data[base + 8:base + 56])
        name = PT_NAMES.get(p_type, f"0x{p_type:08x}")
        print(f"  [{i}] {name:<11} смещение 0x{p_off:<9x}"
              f" в файле 0x{p_filesz:<9x} в памяти 0x{p_memsz:<9x}"
              f" флаги 0x{p_flags:x}")
        found[p_type] = (p_off, p_filesz)
    return found


def process_param(data, elf_off, found):
    """Разобрать sys_process_param, если он есть и доступен."""
    print("  sys_process_param:", end=" ")
    seg = found.get(0x60000001)
    if seg is None:
        print("сегмента нет")
        return
    off, filesz = seg
    if filesz == 0:
        print("сегмент есть, содержимого нет")
        return
    if elf_off != 0:
        print(f"есть, 0x{filesz:x} байт — внутри SELF зашифрован")
        return

    blob = data[off:off + filesz].ljust(32, b"\0")
    size, magic, version, sdk_version, prio, stack, pagesize, _ = \
        struct.unpack(">8I", blob[:32])
    print()
    print(f"    размер 0x{size:x}, сигнатура 0x{magic:x}, версия {version},"
          f" SDK 0x{sdk_version:x}")
    print(f"    приоритет главной нити {prio}, её стек 0x{stack:x},"
          f" страница malloc 0x{pagesize:x}")


def main(paths):
    for path in paths:
        print(f"=== {path} ===")
        try:
            data = open(path, "rb").read()
            elf_off, phdr_off, phentsize, phnum = read_headers(data)
            found = segments(data, phdr_off, phentsize, phnum)
            process_param(data, elf_off, found)
        except (OSError, ValueError, struct.error) as err:
            print(f"  не разобрать: {err}")
        print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
