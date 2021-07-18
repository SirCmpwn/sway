# sway

Sway یک کامپوزیتور [Wayland] سازگار با [i3] است. سوالات متداول ([FAQ]) را بخوانید. عضو کانال IRC [IRC channel] \(#sway در irc.libera.chat) .شوید

## امضا عرضه ها

عرضه are signed with [E88F5E48] and published [on GitHub][GitHub releases].
عرضه ها با [E88F5E48] امضا و در گیت‌هاب ([Github]) منتشر شده اند

## نصب

### توسط پکیج ها

Sway در بسیاری از توزیع ها موجود است. نصب پکیج را با نام "sway" امتحان کنید

اگر به اضافه کردن پکیج sway به توزیع خود علاقه مند هستید در کانال IRC اطلاع دهید یا یک ایمیل به sir@cmpwn.com ارسال کنید.
برای مشاوره به کانال sir@cmpwn.com کانال بزنید یا یک ایمیل ارسال کنید.

### کامپایل از سورس

 اگر میخواهید از سورس کامپایل کنید [این صفحه ویکی][Development setup] را ببینید 

وابستگی های نصب:

* meson \*
* [wlroots]
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (اختیاری: system tray)
* [scdoc] (اختیاری: صفحه های man) \*
* git (اختیاری: اطلاعات ورژن) \*

_\*وابستگی های زمان-کامپایل
این دستور ها را اجرا کنید:

    meson build/
    ninja -C build/
    sudo ninja -C build/ install

در سیستم هایی که بدون وارد شدن هستند باید به sway suid بدهید:

    sudo chmod a+s /usr/local/bin/sway

Sway دسترسی روت را بعد از شروع از دست میدهد.

## پیکربندی
اگر در حال حاظر از i3 استفاده میکنید، پس پیکربندی i3 خود را در `~/.config/sway/config` کپی کنید و به حتم کار میکند. در غیر این صورت پیکربندی نمونه را که در `/etc/sway/config`
کپی کنید. برای اطلاعات بیشتر `man 5 sway` را اجرا کنید. 

## اجرا کردن

`sway` را در حالت TTY اجرا کنید. بعضی مدیر صفحه ها احتمالا کار کنند ولی sway توسط sway پشتیبانی نشده‌اند (gdm به خوبی کار می کند)

[i3]: https://i3wm.org/
[Wayland]: http://wayland.freedesktop.org/
[FAQ]: https://github.com/swaywm/sway/wiki
[IRC channel]: https://web.libera.chat/?channels=#sway
[E88F5E48]: https://keys.openpgp.org/search?q=34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48
[Development setup]: https://github.com/swaywm/sway/wiki/Development-Setup
[wlroots]: https://github.com/swaywm/wlroots
[scdoc]: https://git.sr.ht/~sircmpwn/scdoc
