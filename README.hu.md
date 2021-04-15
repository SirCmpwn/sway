# sway

A Sway egy [i3]-kompatibilis [Wayland] kompozitor. Olvasd el a [Gyarkan Ismételt Kérdéseket][FAQ]. Csatlakozz az [IRC csatornához][IRC channel] \(`#sway` az `irc.freenode.net`-en).

## Csomag aláírások

A kiadott csomagok az [E88F5E48] kulccsal vannak aláírva és [GitHub-on][GitHub releases] publikálva.

## Telepítés

### Csomagból

A Sway sok disztribúció csomagkezelőjéből elérhető, próbáld meg a "sway"
csomagot telepíteni az általad használt eszközzel.

Ha szeretnél csomagot készíteni a saját disztribúciódhoz, ugorj be az IRC
csatornára, vagy küldj levelet a sir@cmpwn.com címre tanácsokért.

### Fordítás forráskódból

Olvasd el [ezt a wiki oldalt][Development setup], ha szeretnéd tesztelési vagy
fejlesztési célokból lefordítani az aktuális (HEAD) állapotát a `sway`-nek és a
`wlroots`-nak.

Telepítsd a függőségeket:

* meson \*
* [wlroots]
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (opcionális: system tray)
* [scdoc] (opcionális: man pages) \*
* git (opcionális: version info) \*

_\*Fordításidejű függőség_

Futtasd ezeket a parancsokat:

    meson build
    ninja -C build
    sudo ninja -C build install

Ha `logind` nélküli rendszert használsz, akkor be kell állítanod a `suid` bitet
a futtaható állományon:

    sudo chmod a+s /usr/local/bin/sway

A Sway indulás után nem sokkal el fogja engedni a root jogosultságait.

## Konfiguráció

Ha előzőleg i3-mat használtál, akkor átmásolhatod az i3 beállításaidat a
`~/.config/sway/config` file-ba és ugyanúgy működni fognak. Egyéb esetben másold
le kiindulási alapnak a mintát, ami általában az `etc/sway/config` elérési
útvonalon található.
Futtasd a `man 5 sway` parancsot további információért a konfigurációval
kapcsolatban.

## Futtatás

Futtasd a `sway` parancsot egy TTY felületről. Néhány bejelentkezéskezelő
(display manager) működhet, de alapvetően nem támogatottak a sway által. (A
gdm-ről ismeretes, hogy egész jól működik.)

[i3]: https://i3wm.org/
[Wayland]: http://wayland.freedesktop.org/
[FAQ]: https://github.com/swaywm/sway/wiki
[IRC channel]: http://webchat.freenode.net/?channels=sway&uio=d4
[E88F5E48]: https://keys.openpgp.org/search?q=34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48
[GitHub releases]: https://github.com/swaywm/sway/releases
[Development setup]: https://github.com/swaywm/sway/wiki/Development-Setup
[wlroots]: https://github.com/swaywm/wlroots
[scdoc]: https://git.sr.ht/~sircmpwn/scdoc
