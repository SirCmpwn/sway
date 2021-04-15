---
name: Bugs
about: Crashes and other bugs
labels: 'bug'

---

### Please read the following before submitting:
- Please do NOT submit bug reports for questions. Ask questions on IRC at #sway on irc.freenode.net.
- Proprietary graphics drivers, including nvidia, are not supported. Please use the open source equivalents, such as nouveau, if you would like to use Sway.
- Please do NOT submit issues for information from the github wiki. The github wiki is community maintained and therefore may contain outdated information, scripts that don't work or obsolete workarounds.
  If you fix a script or find outdated information, don't hesitate to adjust the wiki page.

### Please fill out the following:
- **Sway Version:**
  - `swaymsg -t get_version` or `sway -v`

- **Debug Log:**
  - Run `sway -d 2> ~/sway.log` from a TTY and upload it to a pastebin, such as gist.github.com.
  - This will record information about sway's activity. Please try to keep the reproduction as brief as possible and exit sway.

- **Configuration File:**
  - Please try to produce with the default configuration.
  - If you cannot reproduce with the default configuration, please try to find the minimal configuration to reproduce.
  - Upload the config to a pastebin such as gist.github.com.

- **Stack Trace:**
  - This is only needed if sway crashes.
  - If you use systemd, you should be able to open the coredump of the most recent crash with gdb with
    `coredumpctl gdb sway` and then `bt full` to obtain the stack trace.
  - If the lines mentioning sway or wlroots have `??` for the location, your binaries were built without debug symbols. Please compile both sway and wlroots from source and try to reproduce.

- **Description:**
  - The steps you took in plain English to reproduce the problem.
