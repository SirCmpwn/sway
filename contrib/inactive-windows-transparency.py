#!/usr/bin/python

# This script requires i3ipc-python package (install it from a system package manager
# or pip).
# It makes inactive windows transparent. Use `transparency_val` variable to control
# transparency strength in range of 0…1 or use the command line argument -o.

import argparse
import i3ipc
import signal
import sys
import re
from functools import partial

def on_window_focus(not_focused_opacity, focused_opacity, focused_regex, ipc, event):
    global prev_focused
    global prev_workspace

    focused = event.container
    workspace = ipc.get_tree().find_focused().workspace().num

    if focused.id != prev_focused.id:  # https://github.com/swaywm/sway/issues/2859
        if isinstance(focused.app_id, str) and re.match(focused_regex, focused.app_id):
            focused.command("opacity " + focused_opacity)
        else:
            focused.command("opacity 1")

        if workspace == prev_workspace:
            prev_focused.command("opacity " + not_focused_opacity)
        prev_focused = focused
        prev_workspace = workspace


def restore_opacity(ipc):
    for workspace in ipc.get_tree().workspaces():
        for w in workspace:
            w.command("opacity 1")
    ipc.main_quit()
    sys.exit(0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="This script allows you to set the transparency of unfocused windows in sway."
    )
    parser.add_argument(
        "--opacity",
        "-o",
        type=str,
        default="0.80",
        help="set unfocused window opacity value in range 0...1",
    )
    parser.add_argument(
        "--focused-opacity",
        "-f",
        type=str,
        default="1.00",
        help="set focused window opacity value in range 0...1",
    )
    parser.add_argument(
        "--focused-appid",
        type=str,
        default=".*",
        help="set focused window opacity only for app_ids which match this regexp",
    )
    args = parser.parse_args()

    ipc = i3ipc.Connection()
    prev_focused = None
    prev_workspace = ipc.get_tree().find_focused().workspace().num

    for window in ipc.get_tree():
        if window.focused:
            prev_focused = window
        else:
            window.command("opacity " + args.opacity)
    for sig in [signal.SIGINT, signal.SIGTERM]:
        signal.signal(sig, lambda signal, frame: restore_opacity(ipc))
    ipc.on("window::focus", partial(on_window_focus, args.opacity, args.focused_opacity, args.focused_appid))
    ipc.main()
