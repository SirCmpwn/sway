# Sway

Sway er en [i3](https://i3wm.org/)-kompatibel [Wayland](http://wayland.freedesktop.org/) compositor.
Læs [Ofte stillede spørgsmål](https://github.com/swaywm/sway/wiki).
Deltag på [IRC kanalen](http://webchat.freenode.net/?channels=sway&uio=d4) (#sway på irc.freenode.net).

## Udgivelses Signaturer

Udgivelser er signeret med [B22DA89A](http://pgp.mit.edu/pks/lookup?op=vindex&search=0x52CB6609B22DA89A)
og publiseret på [GitHub](https://github.com/swaywm/sway/releases).

## Installation

### Fra Pakker

Sway er tilgængelig i mange distributioner. Prøv at installere pakken "sway". Hvis den ikke er tilgængelig, så tjek [denne wiki-side](https://github.com/swaywm/sway/wiki/Unsupported-packages)
for information om installation til din(e) distribution(er).

Hvis du er interesseret i at lave en Sway pakke til din distribution, burde du besøge IRC
kanalen eller sende en e-mail til sir@cmpwn.com for rådgivning.

### Kompilering fra kildekode

Installation afhænger af følgende programmer:

* meson \*
* [wlroots](https://github.com/swaywm/wlroots)
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (valgfrit tillæg: system tray)
* [scdoc](https://git.sr.ht/~sircmpwn/scdoc) (valgfrit tillæg: man pages) \*
* git \*

_\*Kompiler krav_

Kør følgende kommandoer:

    meson build
    ninja -C build
    sudo ninja -C build install

På systemer uden 'logind', behøver du at sætte ejerens bruger-id for Sways eksekverbare filer - såkaldt SUID (Set owner User ID):

    sudo chmod a+s /usr/local/bin/sway

Sway vil frasige sig 'root' tilladelser kort efter opstart

## Konfiguration

Hvis du allerede bruger i3, bør du kopiere din i3-konfiguration til `~/.config/sway/config` og
det vil bare fungerer. Ellers skal du kopiere eksempel konfigurations filen til
`~/.config/sway/config`. Den er normalt placeret i `/etc/sway/config`.
Kør `man 5 sway` for at få oplysninger om konfigurationen.

## Kører

Kør `sway` fra en TTY. Nogle display managers fungerer muligvis, men understøttes ikke af
Sway (gdm er kendt for at fungere temmelig godt).

