# Билд-окружение для PS3-плагина.
#
# База — официальный образ ps3dev (ps3toolchain + PSL1GHT + ps3libraries).
# Он собран под linux/amd64, на Apple Silicon поедет через эмуляцию: медленно
# на старте, но для плагина в пару сотен килобайт это некритично.
#
# Проверено: образ собирается и работает под эмуляцией на Apple Silicon.
# Внутри есть ppu-gcc 7.2.0, ppu-ld, sprxlinker, fself.py, make_self_npdrm, pkg.py, sfo.py.
#
# ЧЕГО В НЁМ НЕТ (и почему одного его не хватит):
#   - ppu-gcc без поддержки -mprx
#   - нет prx-стартапов ecrti.o / ecrtn.o и ppu-lv2-prx-exportpickup
#   - нет vsh/paf export-стабов
# То есть собрать homebrew .self этим можно, а модуль VSH-плагина — нет.
# Для PRX нужен Cell SDK, подключается через CELL_SDK (см. docs/DEVELOPMENT.ru.md, раздел «Сборка»).
FROM --platform=linux/amd64 ps3dev/ps3dev:submodules

ENV PS3DEV=/usr/local/ps3dev
ENV PSL1GHT=${PS3DEV}
ENV PATH=${PS3DEV}/bin:${PS3DEV}/ppu/bin:${PS3DEV}/spu/bin:${PATH}

WORKDIR /src
CMD ["/bin/bash"]
