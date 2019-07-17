# sway

[**English**](https://github.com/swaywm/sway/blob/master/README.md#sway--) - [日本語](https://github.com/swaywm/sway/blob/master/README.ja.md#sway--) - [Français](https://github.com/swaywm/sway/blob/master/README.fr.md#sway--) - [Українська](https://github.com/swaywm/sway/blob/master/README.uk.md#sway--) - [Español](https://github.com/swaywm/sway/blob/master/README.es.md#sway--) - [Polski](https://github.com/swaywm/sway/blob/master/README.pl.md#sway--) - [中文-简体](https://github.com/swaywm/sway/blob/master/README.zh-CN.md#sway--) - [Deutsch](https://github.com/swaywm/sway/blob/master/README.de.md#sway--) - [Italiano](https://github.com/swaywm/sway/blob/master/README.it.md#sway--)

sway è un compositor [Wayland](http://wayland.freedesktop.org/) compatibile con [i3](https://i3wm.org/).
Leggi le [FAQ](https://github.com/swaywm/sway/wiki). Unisciti al [canale
IRC](http://webchat.freenode.net/?channels=sway&uio=d4) (#sway su
irc.freenode.net).

Se vuoi supportare lo sviluppo di sway, puoi contribuire sulla [pagina Patreon di SirCmpwn](https://patreon.com/sircmpwn).

## Firme delle release

Le release sono firmate con [B22DA89A](http://pgp.mit.edu/pks/lookup?op=vindex&search=0x52CB6609B22DA89A)
e pubblicate [su GitHub](https://github.com/swaywm/sway/releases).

## Installazione

### Da un pacchetto

Sway è disponibile in molte distribuzioni. Prova ad installare il pacchetto "sway" per la tua. Se non è disponibile, consulta [questa pagina wiki](https://github.com/swaywm/sway/wiki/Unsupported-packages)
per informazioni sull'installazione per la tua distribuzione.

Se sei interessato a impacchettare sway per la tua distribuzione, entra nel canale IRC o invia una mail sir@cmpwn.com per eventuali consigli.

### Compilare dal codice sorgente

Installa le dipendenze:

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
* git \*

_\*Dipendenza necessaria durante la compilazione_

Esegui questi comandi:

    meson build
    ninja -C build
    sudo ninja -C build install

Sui sistemi senza logind, è necessario impostare il permesso `setuid` per il binario sway:

    sudo chmod a+s /usr/local/bin/sway

Sway disabiliterà i permessi di root subito dopo l'avvio.

## Configurazione

Se usi già i3, copia il tuo file di configurazione in `~/.config/sway/config` e sarà pronto all'uso. Altrimenti, copia il file di configurazione già incluso in
`~/.config/sway/config`. Di solito si trova in `/etc/sway/config`.
Esegui `man 5 sway` per informazioni sulla configurazione.

## Esecuzione

Esegui `sway` da un TTY. Alcuni display manager potrebbero funzionare ma non sono supportati da sway (gdm funziona abbastanza bene).
