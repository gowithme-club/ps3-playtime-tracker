<p align="center">
  <a href="https://gowithme.club"><img src=".github/assets/banner.png" alt="Трекер игрового времени для PS3 от gowithme.club: сессии, часы и трофеи с PS3 на HEN или CFW" width="100%"></a>
</p>

<p align="center">
  <a href="README.md">English</a> · <b>Русский</b>
</p>

<p align="center">
  <a href="https://github.com/gowithme-club/ps3-playtime-tracker/releases/latest"><img alt="Последний релиз" src="https://img.shields.io/github/v/release/gowithme-club/ps3-playtime-tracker?style=flat-square&labelColor=131C16&color=C7F547&label=release"></a>
  <img alt="PS3 с HEN или CFW" src="https://img.shields.io/badge/PS3-HEN%20%7C%20CFW-C7F547?style=flat-square&labelColor=131C16">
  <a href="LICENSE"><img alt="Лицензия MIT" src="https://img.shields.io/badge/license-MIT-C7F547?style=flat-square&labelColor=131C16"></a>
  <a href="https://gowithme.club"><img alt="gowithme.club" src="https://img.shields.io/badge/profile-gowithme.club-C7F547?style=flat-square&labelColor=131C16"></a>
</p>

# Трекер игрового времени для PS3

VSH-плагин для PS3 на HEN или CFW: записывает, во что и сколько вы играете,
подхватывает выбитые трофеи и отправляет всё это в ваш профиль на
[gowithme.club](https://gowithme.club) — кросс-платформенном трекере игровой
статистики. Живёт в системной оболочке, переживает запуск игры, аккаунт PSN не
нужен.

Исходный код открыт, чтобы было видно, что именно работает на вашей консоли и
что с неё уходит.

## Что вы получите

- **Время в каждой игре**: с диска, с HDD или из PSN — каждая сессия с началом,
  концом и длительностью.
- **Трофеи**: читаются из профиля на консоли, поэтому появляются и без PSN.
- **«Сейчас играет»**: друзья видят, во что вы играете, а при выходе из игры XMB
  покажет, сколько вы играли.
- **Без сети тоже**: очередь отчётов лежит на HDD и переживает офлайн и
  выключение консоли.
- **Запускается сам**: после первого успешного запуска плагин прописывает себя в
  автозагрузку.
- **Один профиль на все консоли**: PS3, PS5, Switch, Steam, Xbox и PSN в одном
  месте на gowithme.club.

## Как это устроено

```text
PS3 ── gwm.sprx внутри XMB определяет игру и читает файлы трофеев
         │
         └── HTTP ──► gowithme.club ──► ваш профиль, друзья, отчёты в Telegram
```

Плагин только сообщает сырые события. Названия игр, обложки, итоги и устранение
дублей — на сервере gowithme.club.

## Установка

Нужна PS3 с CFW или с HFW и PS3HEN, установленный webMAN, компьютер с
FTP-клиентом на время установки и бесплатный аккаунт на
[gowithme.club](https://gowithme.club). Пошаговая инструкция — на gowithme.club
в **Настройки → PS3 (CFW)**, коротко:

1. На gowithme.club выпустите токен для консоли и скачайте оба файла:
   `gwm.sprx` и `gwm.cfg`, в котором токен уже записан.
2. Положите оба файла по FTP в `/dev_hdd0/plugins/`.
3. Дождитесь XMB, подождите ещё пару минут и откройте в браузере
   `http://<ip-консоли>/loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx`.
4. В углу XMB появится уведомление с версией плагина и именем профиля. Дальше
   плагин будет загружаться вместе с консолью сам.

Плагин лежит и на [странице релизов](https://github.com/gowithme-club/ps3-playtime-tracker/releases/latest).
Без токена в `gwm.cfg` он не делает ни одного сетевого запроса. Если что-то
пошло не так — [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Что отправляется

Title ID, название и версия игры, откуда она запущена, длительность сессии,
выбитые трофеи, имя профиля на консоли, версии прошивки и плагина. Консоль
опознаётся солёным хешем: PSID и IDPS её не покидают.

## Где проверено

PS3 на 4.92 HFW с PS3HEN 3.5.0. Проверенные версии и что именно проверялось на
живой консоли — в [CHANGELOG.md](CHANGELOG.md). Отчёты с CFW и других прошивок
очень пригодятся.

## Сборка из исходников

Плагин — это PRX, а открытый тулчейн PSL1GHT собрать его не может, поэтому для
сборки нужен ещё Cell SDK 4.75 от Sony. Он проприетарный и в репозиторий не
входит.

```bash
make toolchain     # открытый тулчейн ps3dev в ~/ps3dev
make tools-check
make sprx          # нужен Cell SDK в ~/ps3sdk/cell
```

Какие пакеты SDK нужны, билд-хост, подпись и что измерено на консоли —
[docs/DEVELOPMENT.ru.md](docs/DEVELOPMENT.ru.md).

## Другие консоли

| Консоль | Репозиторий | Что ставится |
| --- | --- | --- |
| PS5 с джейлбрейком | [ps5-playtime-tracker](https://github.com/gowithme-club/ps5-playtime-tracker) | пейлоад |
| PS3 с HEN или CFW | **ps3-playtime-tracker** (этот) | VSH-плагин |
| Switch с Atmosphere | [switch-playtime-tracker](https://github.com/gowithme-club/switch-playtime-tracker) | sysmodule |

Обычные консоли, Steam, Xbox и Discord подключаются на
[gowithme.club](https://gowithme.club) без установки чего-либо.

## Помощь и участие

Вопросы и ошибки — в [поддержку в Telegram](https://t.me/gowithme_club_support)
или в [issues на GitHub](https://github.com/gowithme-club/ps3-playtime-tracker/issues).
Пулреквесты приветствуются.

## Лицензия

[MIT](LICENSE).

Это неофициальный homebrew, не связанный с Sony Interactive Entertainment и не
одобренный ею. PlayStation и PS3 — товарные знаки своих владельцев. Запуск
homebrew на консоли — на ваш риск.
