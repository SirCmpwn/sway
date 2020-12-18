# sway

[**English**](https://github.com/swaywm/sway/blob/master/README.md#sway--) - [日本語](https://github.com/swaywm/sway/blob/master/README.ja.md#sway--) - [Français](https://github.com/swaywm/sway/blob/master/README.fr.md#sway--) - [Українська](https://github.com/swaywm/sway/blob/master/README.uk.md#sway--) - [Español](https://github.com/swaywm/sway/blob/master/README.es.md#sway--) - [Polski](https://github.com/swaywm/sway/blob/master/README.pl.md#sway--) - [中文-简体](https://github.com/swaywm/sway/blob/master/README.zh-CN.md#sway--) - [Deutsch](https://github.com/swaywm/sway/blob/master/README.de.md#sway--) - [Nederlands](https://github.com/swaywm/sway/blob/master/README.nl.md#sway--) - [Русский](https://github.com/swaywm/sway/blob/master/README.ru.md#sway--)- [中文-繁體](https://github.com/swaywm/sway/blob/master/README.zh-TW.md#sway--) - [Português](https://github.com/swaywm/sway/blob/master/README.pt.md#sway--) - [Danish](https://github.com/swaywm/sway/blob/master/README.dk.md#sway--) - [한국어](https://github.com/swaywm/sway/blob/master/README.ko.md#sway--) - [Română](https://github.com/swaywm/sway/blob/master/README.ro.md#sway--) - [فارسی](https://github.com/swaywm/sway/blob/master/README.ir.md#sway--)

sway is an [i3](https://i3wm.org/)-compatible [Wayland](http://wayland.freedesktop.org/) compositor.
Read the [FAQ](https://github.com/swaywm/sway/wiki). Join the [IRC
channel](http://webchat.freenode.net/?channels=sway&uio=d4) (#sway on
irc.freenode.net).

If you'd like to support sway development, please contribute to [SirCmpwn's
Patreon page](https://patreon.com/sircmpwn).

## Release Signatures

Releases are signed with [E88F5E48](https://keys.openpgp.org/search?q=34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48)
and published [on GitHub](https://github.com/swaywm/sway/releases).

## Installation

### From Packages

Sway is available in many distributions. Try installing the "sway" package for
yours.

If you're interested in packaging sway for your distribution, stop by the IRC
channel or shoot an email to sir@cmpwn.com for advice.

### Compiling from Source

Check out [this wiki page](https://github.com/swaywm/sway/wiki/Development-Setup) if you want to build the HEAD of sway and wlroots for testing or development.

Install dependencies:

* meson \*
* [wlroots](https://github.com/swaywm/wlroots)
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (optional: system tray)
* [scdoc](https://git.sr.ht/~sircmpwn/scdoc) (optional: man pages) \*
* git (optional: version info) \*

_\*Compile-time dep_

Run these commands:

    meson build
    ninja -C build
    sudo ninja -C build install

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
