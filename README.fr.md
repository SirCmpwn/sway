# sway

Sway est un compositeur [Wayland] compatible avec [i3]. Lisez la
[FAQ]. Rejoignez le [canal IRC] (#sway sur irc.freenode.net).

## Aide en français

[abdelq] fournit du support en français sur IRC et Github, dans le fuseau
horaire UTC-4 (EST).

## Signatures de nouvelles versions

Les nouvelles versions sont signées avec [E88F5E48] et publiées
[sur GitHub][versions GitHub].

## Installation

### À partir de paquets

Sway est disponible sur beaucoup de distributions. Essayez d'installer le
paquet "sway" pour la vôtre.

Si vous êtes intéressé à maintenir Sway pour votre distribution, passez sur le
canal IRC ou envoyez un e-mail à sir@cmpwn.com (en anglais seulement) pour des
conseils.

### Compilation depuis les sources

Consultez [cette page wiki][Configuration de développement] si vous souhaitez
compiler la révision HEAD de sway et wlroots pour tester ou développer.

Installez les dépendances :

* meson \*
* [wlroots]
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (optionnel : system tray)
* [scdoc] (optionnel : requis pour les pages man) \*
* git (optionnel : information de version) \*

_\* Requis uniquement pour la compilation_

Exécutez ces commandes :

    meson build
    ninja -C build
    sudo ninja -C build install

Sur les systèmes sans logind, vous devez suid le binaire de sway :

    sudo chmod a+s /usr/local/bin/sway

Sway se débarassera des permissions *root* peu de temps après le démarrage.

## Configuration

Si vous utilisez déjà i3, copiez votre configuration i3 vers
`~/.config/sway/config` et sway fonctionnera directement. Sinon, copiez
l'exemple de fichier de configuration vers `~/.config/sway/config`. Il se
trouve généralement dans `/etc/sway/config`. Exécutez `man 5 sway` pour lire la
documentation pour la configuration de sway.

## Exécution

Exécutez `sway` à partir d'un TTY. Certains gestionnaires d'affichage peuvent
fonctionner, mais ne sont pas supportés par Sway (gdm est réputé pour assez
bien fonctionner).

[Wayland]: http://wayland.freedesktop.org/
[i3]: https://i3wm.org/
[FAQ]: https://github.com/swaywm/sway/wiki
[canal IRC]: http://webchat.freenode.net/?channels=sway&uio=d4
[abdelq]: https://github.com/abdelq
[E88F5E48]: https://keys.openpgp.org/search?q=34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48
[versions GitHub]: https://github.com/swaywm/sway/releases
[Configuration de développement]: https://github.com/swaywm/sway/wiki/Development-Setup
[wlroots]: https://github.com/swaywm/wlroots
[scdoc]: https://git.sr.ht/~sircmpwn/scdoc
