#!/usr/bin/env python

# This script keeps track of active keyboard layouts per window.
#
# This script requires i3ipc-python package (install it from a system package
# manager or pip).

import i3ipc

def on_window_focus(ipc: i3ipc.connection.Connection, event: i3ipc.events.WindowEvent):
    global windows, prev_focused

    # Save current layout
    layouts = {input.identifier : input.xkb_active_layout_index
               for input in ipc.get_inputs()}
    windows[prev_focused] = layouts

    # Restore layout of the newly focused window
    if event.container.id in windows:
        for (kdb_id, layout_index) in windows[event.container.id].items():
            if layout_index != layouts[kdb_id]:
                ipc.command(f"input \"{kdb_id}\" xkb_switch_layout {layout_index}")
                break

    prev_focused = event.container.id

def on_window_close(ipc: i3ipc.connection.Connection, event: i3ipc.events.WindowEvent):
    global windows
    if event.container.id in windows:
        del(windows[event.container.id])

def on_window(ipc: i3ipc.connection.Connection, event: i3ipc.events.WindowEvent):
    if event.change == "focus":
        on_window_focus(ipc, event)
    elif event.change == "close":
        on_window_close(ipc, event)

if __name__ == "__main__":
    ipc = i3ipc.Connection()
    focused = ipc.get_tree().find_focused()
    if focused:
        prev_focused = focused.id
    else:
        prev_focused = None
    windows = {}

    ipc.on("window", on_window)
    ipc.main()
