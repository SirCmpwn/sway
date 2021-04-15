# sway

sway - это [i3]-совместимый композитор [Wayland].
Больше информации в [FAQ]. Присоединяйтесь к
[IRC-каналу][IRC channel] (#sway на
irc.freenode.net).

## Подписи релизов

Релизы подписываются ключом [E88F5E48] и публикуются [на GitHub][GitHub releases].

## Установка

### Из репозиториев

Sway доступен во многих дистрибутивах. Попробуйте установить пакет "sway".

Если вас интересует создание пакета sway для вашего дистрибутива, зайдите на [IRC-канал][IRC channel]
или отправьте письмо на sir@cmpwn.com за советом.

### Сборка из исходников

Посетите [эту страницу на вики][Development setup], если вы хотите построить последнюю версию
sway и wlroots для тестирования или разработки. 

Установите зависимости:

* meson \*
* [wlroots]
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (опционально: для работы трея)
* [scdoc] (опционально: для man-страниц) \*
* git (опционально: для информации о версии) \*

_\*Зависимости для сборки_

Выполните эти команды:

    meson build
    ninja -C build
    sudo ninja -C build install

На системах без logind вам понадобится добавить suid к файлу программы sway:

    sudo chmod a+s /usr/local/bin/sway

sway сбросит root-права при запуске.

## Настройка

Если вы уже используете i3, скопируйте ваш конфигурационный файл i3 в `~/.config/sway/config`, и
он сразу же заработает. В противном случае, скопируйте образец конфигурационного файла в
`~/.config/sway/config`. Обычно он располагается в `/etc/sway/config`.
Запустите `man 5 sway` для изучения информации о настройке.

## Запуск

Выполните команду `sway` прямо из TTY. Некоторые дисплейные менеджеры могут работать, но они не поддерживаются со стороны
sway (gdm работает довольно неплохо).

[i3]: https://i3wm.org/
[Wayland]: http://wayland.freedesktop.org/
[FAQ]: https://github.com/swaywm/sway/wiki
[IRC channel]: http://webchat.freenode.net/?channels=sway&uio=d4
[E88F5E48]: https://keys.openpgp.org/search?q=34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48
[GitHub releases]: https://github.com/swaywm/sway/releases
[Development setup]: https://github.com/swaywm/sway/wiki/Development-Setup
[wlroots]: https://github.com/swaywm/wlroots
[scdoc]: https://git.sr.ht/~sircmpwn/scdoc
