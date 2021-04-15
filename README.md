# sway

**[English][en]** - [日本語][ja] - [Français][fr] - [Українська][uk] - [Español][es] - [Polski][pl] - [中文-简体][zh-CN] - [Deutsch][de] - [Nederlands][nl] - [Русский][ru] - [中文-繁體][zh-TW] - [Português][pt] - [Danish][dk] - [한국어][ko] - [Română][ro] - [Magyar][hu]

sway is an [i3]-compatible [Wayland] compositor. Read the [FAQ]. Join the
[IRC channel] \(#sway on irc.freenode.net).

## Release Signatures

Releases are signed with [E88F5E48] and published [on GitHub][GitHub releases].

## Installation

### From Packages

Sway is available in many distributions. Try installing the "sway" package for
yours.

If you're interested in packaging sway for your distribution, stop by the IRC
channel or shoot an email to sir@cmpwn.com for advice.

### Compiling from Source

Check out [this wiki page][Development setup] if you want to build the HEAD of
sway and wlroots for testing or development.

Install dependencies:

* meson \*
* [wlroots]
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (optional: system tray)
* [scdoc] (optional: man pages) \*
* git (optional: version info) \*

_\*Compile-time dep_

Run these commands:

    meson build/
    ninja -C build/
    sudo ninja -C build/ install

On systems without logind, you need to suid the sway binary:

    sudo chmod a+s /usr/local/bin/sway

Sway will drop root permissions shortly after startup.

## Configuration

If you already use i3, then copy your i3 config to `~/.config/sway/config` and
it'll work out of the box. Otherwise, copy the sample configuration file to
`~/.config/sway/config`. It is usually located at `/etc/sway/config`.
Run `man 5 sway` for information on the configuration.

## Running

Run `sway` from a TTY. Some display managers may work but are not supported by
sway (gdm is known to work fairly well).

[en]: https://github.com/swaywm/sway#readme
[ja]: https://github.com/swaywm/sway/blob/master/README.ja.md
[fr]: https://github.com/swaywm/sway/blob/master/README.fr.md
[uk]: https://github.com/swaywm/sway/blob/master/README.uk.md
[es]: https://github.com/swaywm/sway/blob/master/README.es.md
[pl]: https://github.com/swaywm/sway/blob/master/README.pl.md
[zh-CN]: https://github.com/swaywm/sway/blob/master/README.zh-CN.md
[de]: https://github.com/swaywm/sway/blob/master/README.de.md
[nl]: https://github.com/swaywm/sway/blob/master/README.nl.md
[ru]: https://github.com/swaywm/sway/blob/master/README.ru.md
[zh-TW]: https://github.com/swaywm/sway/blob/master/README.zh-TW.md
[pt]: https://github.com/swaywm/sway/blob/master/README.pt.md
[dk]: https://github.com/swaywm/sway/blob/master/README.dk.md
[ko]: https://github.com/swaywm/sway/blob/master/README.ko.md
[ro]: https://github.com/swaywm/sway/blob/master/README.ro.md
[hu]: https://github.com/swaywm/sway/blob/master/README.hu.md
[i3]: https://i3wm.org/
[Wayland]: http://wayland.freedesktop.org/
[FAQ]: https://github.com/swaywm/sway/wiki
[IRC channel]: http://webchat.freenode.net/?channels=sway&uio=d4
[E88F5E48]: https://keys.openpgp.org/search?q=34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48
[GitHub releases]: https://github.com/swaywm/sway/releases
[Development setup]: https://github.com/swaywm/sway/wiki/Development-Setup
[wlroots]: https://github.com/swaywm/wlroots
[scdoc]: https://git.sr.ht/~sircmpwn/scdoc
