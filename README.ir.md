<div dir="rtl">

# sway

sway یک کامپوزیتور الهام گرفته از [i3](https://i3wm.org/) بر روی [Wayland](http://wayland.freedesktop.org/) است. [سوال‌های متداول](https://github.com/swaywm/sway/wiki) را بخوانید. در [کانال
IRC](http://webchat.freenode.net/?channels=sway&uio=d4) عضو شوید (#sway sur
irc.freenode.net).

برای حمایت از تیم توسعه sway به [صفحه
Patreon با نام کاربری SirCmpwn](https://patreon.com/sircmpwn) مراجعه کنید.

## امضای نسخه‌ها

امضای نسخه‌ها با [B22DA89A](http://pgp.mit.edu/pks/lookup?op=vindex&search=0x52CB6609B22DA89A) در [GitHub](https://github.com/swaywm/sway/releases) منتشر می‌شود.

## شیوه نصب

### از بسته‌های رسمی

sway در بسته‌های رسمی توزیع‌های مختلف وجود دارد. بسته «sway» را نصب کنید. در صورتی که بسته رسمی وجود نداشت، برای آگاهی بیشتر درباره نصب روی توزیعتان به این [صفحه راهنما](https://github.com/swaywm/sway/wiki/Unsupported-packages) مراجعه کنید.

اگر به ایجاد بسته sway برای توزیعتان علاقه‌مند هستید، از کانال IRC استفاده کنید یا به sir@cmpwn.com ایمیل بزنید.

### کامپایل کردن کد

چنانچه می‌خواهید آخرین نسخه کد sway و wlroots را برای آزمایش یا توسعه بسازید به این [صفحه راهنما](https://github.com/swaywm/sway/wiki/Development-Setup) مراجعه کنید.

بسته‌های مورد نیاز:

* meson \*
* [wlroots](https://github.com/swaywm/wlroots)
* wayland
* wayland-protocols \*
* pcre
* json-c
* pango
* cairo
* gdk-pixbuf2 (انتخابی: برای system tray)
* [scdoc](https://git.sr.ht/~sircmpwn/scdoc) (انتخابی: برای صفحه‌های راهنما) \*
* git (انتخابی: برای اطلاع در خصوص نسخه‌ها) \*

_\*نیازمندی‌های زمان کامپایل برنامه_

این فرمان‌ها را اجرا کنید:
</div>

    meson build
    ninja -C build
    sudo ninja -C build install

<div dir="rtl">

روی سیستم‌های بدون logind، باید فرمان زیر را برای suid کردن باینری sway اجرا کنید:
</div>

    sudo chmod a+s /usr/local/bin/sway

<div dir="rtl">
sway پس از startup مجوزهای دسترسی root را رها می‌کند.

### شخصی سازی و تنظیمات

اگر در حال حاضر از i3 استفاده می‌کنید، تنظیمات i3 خودتان را در فایل `~/.config/sway/config` کپی کنید و بدون نیاز به تغییر کار خواهد کرد. در غیر این‌صورت، فایل نمونه تنظیمات را استفاده کنید. این فایل عموما در `/etc/sway/config` قرار دارد. برای آگاهی بیشتر `man 5 sway` را اجرا کنید.

## اجرا

در محیط TTY کافیست `sway` را اجرا کنید. ممکن است ابزارهای مدیریت نمایشگری نیز برای این کار وجود داشته باشند اما از طرف sway پشتیبانی نمی‌شوند (gdm عملکرد خوبی در این زمینه دارد).  

</div>
