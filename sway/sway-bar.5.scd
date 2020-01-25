sway-bar(5)

# NAME

sway-bar - bar configuration file and commands

# DESCRIPTION

Sway allows configuring swaybar in the sway configuration file.

# COMMANDS

The following commands may only be used in the configuration file.

*id* <bar_id>
	Sets the ID of the bar.

*swaybar_command* <command>
	Executes custom bar command. Default is _swaybar_.

The following commands may be used either in the configuration file or at
runtime.

*bindcode* [--release] <event-code> <command>
	Executes _command_ when the mouse button has been pressed (or if _released_
	is given, when the button has been released). The buttons can be given as
	an event code, which can be obtaining from *libinput debug-events*. To
	disable the default behavior for a button, use the command _nop_.

*bindsym* [--release] button[1-9]|<event-name> <command>
	Executes _command_ when the mouse button has been pressed (or if _released_
	is given, when the button has been released). The buttons can be given as a
	x11 button number or an event name, which can be obtained from *libinput
	debug-events*. To disable the default behavior for a button, use the
	command _nop_.

	Special events SWIPE\_{3,4}\_{UP,DOWN,LEFT,RIGHT} respond to three- and
	four-finger swipe gestures respectively. Gestures have to happen while
	the pointer is above a bar to be considered. Gesture bindings for other
	contexts can be configured similarly in sway itself. The binding to
	execute is selected based on the number of fingers used and ultimate
	overall direction of the gesture. Because the latter cannot be
	determined before the gesture has actually finished, bindings are only
	ever executed on release. Therefore bindings with flag _--release_ are
	ignored if there is an on-press binding as well because they would be
	executed at the same time anyway.

*binding_mode_indicator* yes|no
	Enable or disable binding mode indicator. Default is _yes_.

*font* <font>
	Specifies the font to be used in the bar. _font_ should be specified as a
	pango font description. For more information on pango font descriptions,
	see https://developer.gnome.org/pango/stable/pango-Fonts.html#pango-font-description-from-string

*gaps* <all> | <horizontal> <vertical> | <top> <right> <bottom> <left>
	Sets the gaps from the edge of the screen for the bar. Gaps can either be
	set all at once, per direction, or per side. Note that only sides that
	touch an edge of the screen can have gaps. For the side that does not
	touch an edge of the screen, per-side outer gaps for workspaces may be of
	use.

*height* <height>
	Sets the height of the bar. Default height (0) will match the font size.

*hidden_state* hide|show [<bar-id>]
	Specifies the behaviour of the bar when it is in _hide_ mode. When the
	hidden state is _hide_, then it is normally hidden, and only unhidden by
	pressing the modifier key or in case of urgency hints. When the hidden
	state is _show_, then it is permanently visible, drawn on top of the
	currently visible workspace. Default is _hide_.

	For compatibility with i3, _bar hidden_state hide|show [<bar-id>]_ is
	supported along with the sway only _bar <bar-id> hidden_state hide|show_
	syntax. When using the i3 syntax, if _bar-id_ is omitted, the hidden_state
	will be changed for all bars. Attempting to use _bar <bar-id1>
	hidden_state hide|show <bar-id2>_ will result in an error due to
	conflicting bar ids.

*mode* dock|hide|invisible|overlay [<bar-id>]
	Specifies the visibility of the bar. In _dock_ mode, it is permanently
	visible at one edge of the screen. In _hide_ mode, it is hidden unless the
	modifier key is pressed, though this behaviour depends on the hidden state.
	In _invisible_ mode, it is permanently hidden. In _overlay_ mode, it is
	permanently visible on top of other windows. (In _overlay_ mode the bar is
	transparent to input events.) Default is _dock_.

	For compatibility with i3, _bar mode <mode> [<bar-id>]_ syntax is supported
	along with the sway only _bar <bar-id> mode <mode>_ syntax. When using the
	i3 syntax, if _bar-id_ is omitted, the mode will be changed for all bars.
	Attempting to use _bar <bar-id1> mode <mode> <bar-id2>_ will result in an
	error due to conflicting bar ids.

*modifier* <Modifier>|none
	Specifies the modifier key that shows a hidden bar. Default is _Mod4_.

*output* <output>|\*
	Restrict the bar to a certain output, can be specified multiple times. If
	the output command is omitted, the bar will be displayed on all outputs. _\*_
	can be given at any point to reset it back to all outputs.

*pango_markup* enabled|disabled
	Enables or disables pango markup for status lines. This has no effect on
	status lines using the i3bar JSON protocol.

*position* top|bottom
	Sets position of the bar. Default is _bottom_.

*separator_symbol* <symbol>
	Specifies the separator symbol to separate blocks on the bar.

*status_command* <status command>
	Executes the bar _status command_ with _sh -c_. Each line of text printed
	to stdout from this command will be displayed in the status area of the
	bar. You may also use swaybar's JSON status line protocol. See
	*swaybar-protocol*(7) for more information on the protocol

	If running this command via IPC, you can disable a running status command by
	setting the command to a single dash: _swaybar bar bar-0 status\_command -_

*status_edge_padding* <padding>
	Sets the padding that is used when the status line is at the right edge of
	the bar. This value will be multiplied by the output scale. The default is
	_3_.

*status_padding* <padding>
	Sets the vertical padding that is used for the status line. The default is
	_1_. If _padding_ is _0_, blocks will be able to take up the full height of
	the bar. This value will be multiplied by the output scale.

*strip_workspace_name* yes|no
	If set to _yes_, then workspace names will be omitted from the workspace
	button and only the custom number will be shown. Default is _no_.

*strip_workspace_numbers* yes|no
	If set to _yes_, then workspace numbers will be omitted from the workspace
	button and only the custom name will be shown. Default is _no_.

*unbindcode* [--release] <event-code>
	Removes the binding with the given <event-code>.

*unbindsym* [--release] button[1-9]|<event-name>
	Removes the binding with the given <button> or <event-name>.

*wrap_scroll* yes|no
	Enables or disables wrapping when scrolling through workspaces with the
	scroll wheel. Default is _no_.

*workspace_buttons* yes|no
	Enables or disables workspace buttons on the bar. Default is _yes_.

*workspace_min_width* <px> [px]
	Specifies the minimum width for the workspace buttons on the bar. Default is _0_.

	This setting also applies to the current binding mode indicator.

## TRAY

Swaybar provides a system tray where third-party applications may place icons.
The following commands configure the tray.

*tray_bindcode* <event-code>
ContextMenu|Activate|SecondaryActivate|ScrollDown|ScrollLeft|ScrollRight|ScrollUp|nop
	Executes the action when the mouse button has been pressed. The buttons can
	be given as an event code, which can be obtained from *libinput debug-events*.
	To disable the default behavior for a button, use the command _nop_.

*tray_bindsym* button[1-9]|<event-name>
ContextMenu|Activate|SecondaryActivate|ScrollDown|ScrollLeft|ScrollRight|ScrollUp|nop
	Executes the action when the mouse button has been pressed. The buttons can
	be given as a x11 button number or an event name, which can be obtained
	from *libinput debug-events*. Use the command _nop_ to disable the default
	action (Activate for button1, ContextMenu for button2 and SecondaryActivate
	for button3).

*tray_padding* <px> [px]
	Sets the pixel padding of the system tray. This padding will surround the
	tray on all sides and between each item. The default value for _px_ is 2.

*tray_output* none|<output>|\*
	Restrict the tray to a certain output, can be specified multiple times. If
	omitted, the tray will be displayed on all outputs. Unlike i3bar, swaybar
	can show icons on any number of bars and outputs without races. _\*_ can be
	given at any point to reset it to display on all outputs.

*icon_theme* <name>
	Sets the icon theme that sway will look for item icons in. This option has
	no default value, because sway will always default to the fallback theme,
	hicolor.

## COLORS

Colors are defined within a _colors { }_ block inside a _bar { }_ block. Colors
must be defined in hex: _#RRGGBB_ or _#RRGGBBAA_.

*background* <color>
	Background color of the bar.

*statusline* <color>
	Text color to be used for the statusline.

*separator* <color>
	Text color to be used for the separator.

*focused_background* <color>
	Background color of the bar on the currently focused monitor output. If not
	used, the color will be taken from _background_.

*focused_statusline* <color>
	Text color to be used for the statusline on the currently focused monitor
	output. If not used, the color will be taken from _statusline_.

*focused_separator* <color>
	Text color to be used for the separator on the currently focused monitor
	output. If not used, the color will be taken from _separator_.

*focused_workspace* <border> <background> <text>
	Border, background and text color for a workspace button when the workspace
	has focus.

*active_workspace* <border> <background> <text>
	Border, background and text color for a workspace button when the workspace
	is active (visible) on some output, but the focus is on another one. You
	can only tell this apart from the focused workspace when you are using
	multiple monitors.

*inactive_workspace* <border> <background> <text>
	Border, background and text color for a workspace button when the workspace
	does not have focus and is not active (visible) on any output. This will be
	the case for most workspaces.

*urgent_workspace* <border> <background> <text>
	Border, background and text color for a workspace button when the workspace
	contains a window with the urgency hint set.

*binding_mode* <border> <background> <text>
	Border, background and text color for the binding mode indicator. If not used,
	the colors will be taken from _urgent_workspace_.

# SEE ALSO

*sway*(5) *swaybar-protocol*(7)
