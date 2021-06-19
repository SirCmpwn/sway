sway(5)

# NAME

sway - configuration file and commands

# DESCRIPTION

A sway configuration file is a list of sway commands that are executed by sway
on startup.  These commands usually consist of setting your preferences and
setting key bindings. An example config is likely present in /etc/sway/config
for you to check out.

Lines in the configuration file might be extended through multiple lines by
adding a '\\' character at the end of line. e.g.:

```
bindsym Shift+XF86AudioRaiseVolume exec \\
	pactl set-sink-volume @DEFAULT_SINK@ -1%
```

Commands can also be given as a block in the form *command { <subcommands...>
}*. Anything before the opening *{* will be prepended to the lines inside the
block. For example:

```
output eDP-1 {
	background ~/wallpaper.png fill
	resolution 1920x1080
}
```

is identical to

```
output eDP-1 background ~/wallpaper.png fill
output eDP-1 resolution 1920x1080
```

These commands can be executed in your config file, via *swaymsg*(1), or via
the bindsym command.

# COMMAND CONVENTIONS

Commands are split into several arguments using spaces. You can enclose
arguments with quotation marks (*"..."* or *'...'*) to add spaces to a single
argument. You may also run several commands in order by separating each with
*,* or *;*. Criteria is retained across commands separated by *,*, but will be
reset (and allow for new criteria, if desired) for commands separated by a *;*.

Throughout the documentation, *|* is used to distinguish between arguments for
which you may only select one. *[...]* is used for optional arguments, and
*<...>* for arguments where you are expected to supply some value.

# COMMANDS

This section only lists general commands. For input and output commands, refer
to *sway-input*(5) and *sway-output*(5).

The following commands may only be used in the configuration file.

*bar* [<bar-id>] <bar-subcommands...>
	For details on bar subcommands, see *sway-bar*(5).

*default_orientation* horizontal|vertical|auto
	Sets the default container layout for tiled containers.

*include* <path>
	Includes another file from _path_. _path_ can be either a full path or a
	path relative to the parent config, and expands shell syntax (see
	*wordexp*(3) for details). The same include file can only be included once;
	subsequent attempts will be ignored.

*swaybg_command* <command>
	Executes custom background _command_. Default is _swaybg_. Refer to
	*sway-output*(5) for more information.

	It can be disabled by setting the command to a single dash:
	_swaybg\_command -_

*swaynag_command* <command>
	Executes custom command for _swaynag_. Default is _swaynag_. Additional
	arguments may be appended to the end. This should only be used to either
	direct sway to call swaynag from a custom path or to provide additional
	arguments. This should be placed at the top of the config for the best
	results.

	It can be disabled by setting the command to a single dash:
	_swaynag\_command -_

*workspace_layout* default|stacking|tabbed
	Specifies the initial layout for new containers in an empty workspace.

*xwayland* enable|disable|force
	Enables or disables Xwayland support, which allows X11 applications to be
	used. _enable_ will lazily load Xwayland so Xwayland will not be launched
	until the first client attempts to connect. In some cases, such as slower
	machines, it may be desirable to have Xwayland started immediately by
	using _force_ instead of _enable_.

The following commands cannot be used directly in the configuration file.
They are expected to be used with *bindsym* or at runtime through *swaymsg*(1).

*border* none|normal|csd|pixel [<n>]
	Set border style for focused window. _normal_ includes a border of
	thickness _n_ and a title bar. _pixel_ is a border without title bar _n_
	pixels thick. Default is _normal_ with border thickness 2. _csd_ is short
	for client-side-decorations, which allows the client to draw its own
	decorations.

*border* toggle
	Cycles through the available border styles.

*exit*
	Exit sway and end your Wayland session.

*floating* enable|disable|toggle
	Make focused view floating, non-floating, or the opposite of what it is now.

<criteria> *focus*
	Moves focus to the container that matches the specified criteria.

*focus* up|right|down|left
	Moves focus to the next container in the specified direction.

*focus* prev|next [sibling]
	Moves focus to the previous or next container in the current layout. By default,
	the last active child of the newly focused container will be focused. The _sibling_
	option indicates not to immediately focus a child of the container.

*focus* child
	Moves focus to the last-focused child of the focused container.

*focus* parent
	Moves focus to the parent of the focused container.

*focus* output up|right|down|left
	Moves focus to the next output in the specified direction.

*focus* output <name>
	Moves focus to the named output.

*focus* tiling
	Sets focus to the last focused tiling container.

*focus* floating
	Sets focus to the last focused floating container.

*focus* mode_toggle
	Moves focus between the floating and tiled layers.

*fullscreen* [enable|disable|toggle] [global]
	Makes focused view fullscreen, non-fullscreen, or the opposite of what it
	is now. If no argument is given, it does the same as _toggle_. If _global_
	is specified, the view will be fullscreen across all outputs.

*gaps* inner|outer|horizontal|vertical|top|right|bottom|left all|current
set|plus|minus <amount>
	Changes the _inner_ or _outer_ gaps for either _all_ workspaces or the
	_current_ workspace. _outer_ gaps can be altered per side with _top_,
	_right_, _bottom_, and _left_ or per direction with _horizontal_ and
	_vertical_.

*inhibit_idle* focus|fullscreen|open|none|visible
	Set/unset an idle inhibitor for the view. _focus_ will inhibit idle when
	the view is focused by any seat. _fullscreen_ will inhibit idle when the
	view is fullscreen (or a descendant of a fullscreen container) and is
	visible. _open_ will inhibit idle until the view is closed (or the
	inhibitor is unset/changed). _visible_ will inhibit idle when the view is
	visible on any output. _none_ will remove any existing idle inhibitor for
	the view.

	This can also be used with criteria to set an idle inhibitor for any
	existing view or with _for_window_ to set idle inhibitors for future views.

*layout* default|splith|splitv|stacking|tabbed
	Sets the layout mode of the focused container.

*layout* toggle [split|all]
	Cycles the layout mode of the focused container though a preset list of
	layouts. If no argument is given, then it cycles through stacking, tabbed
	and the last split layout. If _split_ is given, then it cycles through
	splith and splitv. If _all_ is given, then it cycles through every layout.

*layout* toggle [split|tabbed|stacking|splitv|splith] [split|tabbed|stacking|splitv|splith]...
	Cycles the layout mode of the focused container through a list of layouts.

*max_render_time* off|<msec>
	Controls when the relevant application is told to render this window, as a
	positive number of milliseconds before the next time sway composites the
	output. A smaller number leads to fresher rendered frames being composited
	by sway and lower perceived input latency, but if set too low, the
	application may not finish rendering before sway composites the output,
	leading to delayed frames.

	When set to off, the relevant application is told to render this window
	immediately after display refresh. How much time is left for rendering
	before sway composites the output at that point depends on the output
	*max_render_time* setting.

	To set this up for optimal latency:
	. Set up *output max_render_time* (see *sway-output*(5)).
	. Put the target application in _full-screen_ and have it continuously
	  render something.
	. Start by setting *max_render_time 1*. If the application drops
	  frames, increment by *1*.

	This setting only has an effect if a per-output *max_render_time* is in
	effect on the output the window is currently on. See *sway-output*(5) for
	further details.

*move* left|right|up|down [<px> px]
	Moves the focused container in the direction specified. If the container,
	the optional _px_ argument specifies how many pixels to move the container.
	If unspecified, the default is 10 pixels. Pixels are ignored when moving
	tiled containers.

*move* [absolute] position <pos_x> [px|ppt] <pos_y> [px|ptt]
	Moves the focused container to the specified position in the workspace.
	The position can be specified in pixels or percentage points, omitting
	the unit defaults to pixels. If	_absolute_ is used, the position is
	relative to all outputs. _absolute_ can not be used with percentage points.

*move* [absolute] position center
	Moves the focused container to be centered on the workspace. If _absolute_
	is used, it is moved to the center of all outputs.

*move* position cursor|mouse|pointer
	Moves the focused container to be centered on the cursor.

*move* [container|window] [to] mark <mark>
	Moves the focused container to the specified mark.

*move* [--no-auto-back-and-forth] [container|window] [to] workspace [number] <name>
	Moves the focused container to the specified workspace. The string _number_
	is optional and is used to match a workspace with the same number, even if
	it has a different name.

*move* [container|window] [to] workspace prev|next|current
	Moves the focused container to the previous, next or current workspace on
	this output, or if no workspaces remain, the previous or next output.

*move* [container|window] [to] workspace prev_on_output|next_on_output
	Moves the focused container to the previous or next workspace on this
	output, wrapping around if already at the first or last workspace.

*move* [container|window] [to] workspace back_and_forth
	Moves the focused container to previously focused workspace.

*move* [container|window] [to] output <name-or-id>|current
	Moves the focused container to the specified output.

*move* [container|window] [to] output up|right|down|left
	Moves the focused container to next output in the specified
	direction.

*move* [container|window] [to] scratchpad
	Moves the focused container to the scratchpad.

*move* workspace [to] output <name-or-id>|current
	Moves the focused workspace to the specified output.

*move* workspace to [output] <name-or-id>|current
	Moves the focused workspace to the specified output.

*move* workspace [to] output up|right|down|left
	Moves the focused workspace to next output in the specified direction.

*move* workspace to [output] up|right|down|left
	Moves the focused workspace to next output in the specified direction.

*nop* <comment>
	A no operation command that can be used to override default behaviour. The
	optional comment argument is ignored, but logged for debugging purposes.

*reload*
	Reloads the sway config file and applies any changes. The config file is
	located at path specified by the command line arguments when started,
	otherwise according to the priority stated in *sway*(1).

*rename* workspace [<old_name>] to <new_name>
	Rename either <old_name> or the focused workspace to the <new_name>

*resize* shrink|grow width|height [<amount> [px|ppt]]
	Resizes the currently focused container by _amount_, specified in pixels or
	percentage points. If the units are omitted, floating containers are resized
	in px and tiled containers by ppt. _amount_ will default to 10 if omitted.

*resize* set height <height> [px|ppt]
	Sets the height of the container to _height_, specified in pixels or
	percentage points. If the units are omitted, floating containers are
	resized in px and tiled containers by ppt. If _height_ is 0, the container
	will not be resized.

*resize* set [width] <width> [px|ppt]
	Sets the width of the container to _width_, specified in pixels or
	percentage points. If the units are omitted, floating containers are
	resized in px and tiled containers by ppt. If _width_ is 0, the container
	will not be resized.

*resize* set [width] <width> [px|ppt] [height] <height> [px|ppt]
	Sets the width and height of the container to _width_ and _height_,
	specified in pixels or percentage points. If the units are omitted,
	floating containers are resized in px and tiled containers by ppt. If
	_width_ or _height_ is 0, the container will not be resized on that axis.

*scratchpad* show
	Shows a window from the scratchpad. Repeatedly using this command will
	cycle through the windows in the scratchpad.

*shortcuts_inhibitor* enable|disable
	Enables or disables the ability of clients to inhibit keyboard
	shortcuts for a view. This is primarily useful for virtualization and
	remote desktop software. It affects either the currently focused view
	or a set of views selected by criteria. Subcommand _disable_
	additionally deactivates any active inhibitors for the given view(s).
	Criteria are particularly useful with the *for_window* command to
	configure a class of views differently from the per-seat defaults
	established by the *seat* subcommand of the same name. See
	*sway-input*(5) for more ways to affect inhibitors.

*split* vertical|v|horizontal|h|toggle|t
	Splits the current container, vertically or horizontally. When _toggle_ is
	specified, the current container is split opposite to the parent
	container's layout.

*splith*
	Equivalent to *split horizontal*

*splitv*
	Equivalent to *split vertical*

*splitt*
	Equivalent to *split toggle*

*sticky* enable|disable|toggle
	"Sticks" a floating window to the current output so that it shows up on all
	workspaces.

*swap* container with id|con_id|mark <arg>
	Swaps the position, geometry, and fullscreen status of two containers. The
	first container can be selected either by criteria or focus. The second
	container can be selected by _id_, _con_id_, or _mark_. _id_ can only be
	used with xwayland views. If the first container has focus, it will retain
	focus unless it is moved to a different workspace or the second container
	becomes fullscreen on the same workspace as the first container. In either
	of those cases, the second container will gain focus.

*title_format* <format>
	Sets the format of window titles. The following placeholders may be used:

		%title - The title supplied by the window ++
		%app_id - The wayland app ID (applicable to wayland windows only) ++
		%class - The X11 classname (applicable to xwayland windows only) ++
		%instance - The X11 instance (applicable to xwayland windows only) ++
		%shell - The protocol the window is using (typically xwayland or
			xdg_shell)

	This command is typically used with *for_window* criteria. For example:

		for_window [title="."] title_format "<b>%title</b> (%app_id)"

	Note that markup requires pango to be enabled via the *font* command.

	The default format is "%title".

The following commands may be used either in the configuration file or at
runtime.

*assign* <criteria> [→] [workspace] [number] <workspace>
	Assigns views matching _criteria_ (see *CRITERIA* for details) to
	_workspace_. The → (U+2192) is optional and cosmetic. This command is
	equivalent to:

		for_window <criteria> move container to workspace <workspace>

*assign* <criteria> [→] output left|right|up|down|<name>
	Assigns views matching _criteria_ (see *CRITERIA* for details) to the
	specified output. The → (U+2192) is optional and cosmetic. This command is
	equivalent to:

		for_window <criteria> move container to output <output>

*bindsym* [--whole-window] [--border] [--exclude-titlebar] [--release] [--locked] \
[--to-code] [--input-device=<device>] [--no-warn] [--no-repeat] [Group<1-4>+]<key combo> \
<command>
	Binds _key combo_ to execute the sway command _command_ when pressed. You
	may use XKB key names here (*wev*(1) is a good tool for discovering these).
	With the flag _--release_, the command is executed when the key combo is
	released. If _input-device_ is given, the binding will only be executed for
	that input device and will be executed instead of any binding that is
	generic to all devices. If a group number is given, then the binding will
	only be available for that group. By default, if you overwrite a binding,
	swaynag will give you a warning. To silence this, use the _--no-warn_ flag.

	Unless the flag _--locked_ is set, the command will not be run when a
	screen locking program is active. If there is a matching binding with
	and without _--locked_, the one with will be preferred when locked and the
	one without will be preferred when unlocked. If there are matching bindings
	and one has both _--input-device_ and _--locked_ and the other has neither,
	the former will be preferred even when unlocked.

	Unless the flag _--inhibited_ is set, the command will not be run when
	a keyboard shortcuts inhibitor is active for the currently focused
	window. Such inhibitors are usually requested by remote desktop and
	virtualization software to enable the user to send keyboard shortcuts
	to the remote or virtual session. The _--inhibited_ flag allows to
	define bindings which will be exempt from pass-through to such
	software. The same preference logic as for _--locked_ applies.

	Unless the flag _--no-repeat_ is set, the command will be run
	repeatedly when the key is held, according to the repeat
	settings specified in the input configuration.

	Bindings to keysyms are layout-dependent. This can be changed with the
	_--to-code_ flag. In this case, the keysyms will be translated into the
	corresponding keycodes in the first configured layout.

	Mouse bindings operate on the container under the cursor instead of the
	container that has focus. Mouse buttons can either be specified in the form
	_button[1-9]_ or by using the name of the event code (ex _BTN\_LEFT_ or
	_BTN\_RIGHT_). For the former option, the buttons will be mapped to their
	values in X11 (1=left, 2=middle, 3=right, 4=scroll up, 5=scroll down,
	6=scroll left, 7=scroll right, 8=back, 9=forward). For the latter option,
	you can find the event names using _libinput debug-events_.

	The priority for matching bindings is as follows: input device, group,
	and locked state.

	_--whole-window_, _--border_, and _--exclude-titlebar_ are mouse-only options
	which affect the region in which the mouse bindings can be triggered.  By
	default, mouse bindings are only triggered when over the title bar. With the
	_--border_ option, the border of the window will be included in this region.
	With the _--whole-window_ option, the cursor can be anywhere over a window
	including the title, border, and content. _--exclude-titlebar_ can be used in
	conjunction with any other option to specify that the titlebar should be
	excluded from the region of consideration.

	If _--whole-window_ is given, the command can be triggered when the cursor
	is over an empty workspace. Using a mouse binding over a layer surface's
	exclusive region is not currently possible.

	Example:
```
		# Execute firefox when alt, shift, and f are pressed together
		bindsym Mod1+Shift+f exec firefox
```

	*bindcode* [--whole-window] [--border] [--exclude-titlebar] [--release] \
[--locked] [--input-device=<device>] [--no-warn] [Group<1-4>+]<code> <command>
	is also available for binding with key/button codes instead of key/button names.

*bindswitch* [--locked] [--no-warn] [--reload] <switch>:<state> <command>
	Binds <switch> to execute the sway command _command_ on state changes.
	Supported switches are _lid_ (laptop lid) and _tablet_ (tablet mode)
	switches. Valid values for _state_ are _on_, _off_ and _toggle_. These
	switches are on when the device lid is shut and when tablet mode is active
	respectively. _toggle_ is also supported to run a command both when the
	switch is toggled on or off.

	Unless the flag _--locked_ is set, the command will not be run when a
	screen locking program is active. If there is a matching binding with
	and without _--locked_, the one with will be preferred when locked and the
	one without will be preferred when unlocked.

	If the _--reload_ flag is given, the binding will also be executed when
	the config is reloaded. _toggle_ bindings will not be executed on reload.
	The _--locked_ flag will operate as normal so if the config is reloaded
	while locked and _--locked_ is not given, the binding will not be executed.

	By default, if you overwrite a binding, swaynag will give you a warning. To
	silence this, use the _--no-warn_ flag.

	Example:
```
		# Show the virtual keyboard when tablet mode is entered.
		bindswitch tablet:on busctl call --user sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b true

		# Log a message when the laptop lid is opened or closed.
		bindswitch lid:toggle exec echo "Lid moved"
```

*client.background* <color>
	This command is ignored and is only present for i3 compatibility.

*client.<class>* <border> <background> <text> [<indicator> [<child_border>]]
	Configures the color of window borders and title bars. The first three
	colors are required. When omitted _indicator_ will use a sane default and
	_child_border_ will use the color set for _background_. Colors may be
	specified in hex, either as _#RRGGBB_ or _#RRGGBBAA_.

	The available classes are:

	*client.focused*
		The window that has focus.

	*client.focused_inactive*
		The most recently focused view within a container which is not focused.

	*client.placeholder*
		Ignored (present for i3 compatibility).

	*client.unfocused*
		A view that does not have focus.

	*client.urgent*
		A view with an urgency hint. *Note*: Native Wayland windows do not
		support urgency. Urgency only works for Xwayland windows.

	The meaning of each color is:

	_border_
		The border around the title bar.

	_background_
		The background of the title bar.

	_text_
		The text color of the title bar.

	_indicator_
		The color used to indicate where a new view will open. In a tiled
		container, this would paint the right border of the current view if a
		new view would be opened to the right.

	_child_border_
		The border around the view itself.

The default colors are:

[- *class*
:[ _border_
:[ _background_
:[ _text_
:[ _indicator_
:[ _child_border_
|[ *background*
:  n/a
:  #ffffff
:  n/a
:  n/a
:  n/a
|  *focused*
:  #4c7899
:  #285577
:  #ffffff
:  #2e9ef4
:  #285577
|  *focused_inactive*
:  #333333
:  #5f676a
:  #ffffff
:  #484e50
:  #5f676a
|  *unfocused*
:  #333333
:  #222222
:  #888888
:  #292d2e
:  #222222
|  *urgent*
:  #2f343a
:  #900000
:  #ffffff
:  #900000
:  #900000
|  *placeholder*
:  #000000
:  #0c0c0c
:  #ffffff
:  #000000
:  #0c0c0c


*default_border* normal|none|pixel [<n>]
	Set default border style for new tiled windows.

*default_floating_border* normal|none|pixel [<n>]
	Set default border style for new floating windows. This only applies to
	windows that are spawned in floating mode, not windows that become floating
	afterwards.

*exec* <shell command>
	Executes _shell command_ with sh.

*exec_always* <shell command>
	Like *exec*, but the shell command will be executed _again_ after *reload*.

*floating_maximum_size* <width> x <height>
	Specifies the maximum size of floating windows. -1 x -1 removes the upper
	limit. The default is 0 x 0, which will use the width and height of the
	entire output layout as the maximums

*floating_minimum_size* <width> x <height>
	Specifies the minimum size of floating windows. The default is 75 x 50.

*floating_modifier* <modifier> [normal|inverse]
	When the _modifier_ key is held down, you may hold left click to move
	windows, and right click to resize them. Setting _modifier_ to _none_
	disables this feature. If _inverse_ is specified, left click is used for
	resizing and right click for moving.

*focus_follows_mouse* yes|no|always
	If set to _yes_, moving your mouse over a window will focus that window. If
	set to _always_, the window under the cursor will always be focused, even
	after switching between workspaces.

*focus_on_window_activation* smart|urgent|focus|none
	This option determines what to do when an xwayland client requests
	window activation. If set to _urgent_, the urgent state will be set
	for that window. If set to _focus_, the window will become focused.
	If set to _smart_, the window will become focused only if it is already
	visible, otherwise the urgent state will be set. Default is _urgent_.

*focus_wrapping* yes|no|force|workspace
	This option determines what to do when attempting to focus over the edge
	of a container. If set to _no_, the focused container will retain focus,
	if there are no other containers in the direction. If set to _yes_, focus
	will be wrapped to the opposite edge of the container, if there are no
	other containers in the direction. If set to _force_, focus will be wrapped
	to the opposite edge of the container, even if there are other containers
	in the direction. If set to _workspace_, focus will wrap like in the _yes_
	case and additionally wrap when moving outside of workspaces boundaries.
	Default is _yes_.

*font* [pango:]<font>
	Sets font to use for the title bars. To enable support for pango markup,
	preface the font name with _pango:_. For example, _monospace 10_ is the
	default font. To enable support for pango markup, _pango:monospace 10_
	should be used instead. Regardless of whether pango markup is enabled,
	_font_ should be specified as a pango font description. For more
	information on pango font descriptions, see
	https://developer.gnome.org/pango/stable/pango-Fonts.html#pango-font-description-from-string

*force_display_urgency_hint* <timeout> [ms]
	If an application on another workspace sets an urgency hint, switching to this
	workspace may lead to immediate focus of the application, which also means the
	window decoration color would be immediately resetted to *client.focused*. This
	may make it unnecessarily hard to tell which window originally raised the
	event. This option allows to set a _timeout_ in ms to delay the urgency hint reset.

*titlebar_border_thickness* <thickness>
	Thickness of the titlebar border in pixels

*titlebar_padding* <horizontal> [<vertical>]
	Padding of the text in the titlebar. _horizontal_ value affects horizontal
	padding of the text while _vertical_ value affects vertical padding (space
	above and below text). Padding includes titlebar borders so their value
	should be greater than titlebar_border_thickness. If _vertical_ value is
	not specified it is set to the _horizontal_ value.

*titlebar_v_height* <height>
	Height of the titlebar binded to _height_ in pixels. _height_ of -1 turns
	off binding.

*for_window* <criteria> <command>
	Whenever a window that matches _criteria_ appears, run list of commands.
	See *CRITERIA* for more details.

*gaps* inner|outer|horizontal|vertical|top|right|bottom|left <amount>
	Sets default _amount_ pixels of _inner_ or _outer_ gap, where the inner
	affects spacing around each view and outer affects the spacing around each
	workspace. Outer gaps are in addition to inner gaps. To reduce or remove
	outer gaps, outer gaps can be set to a negative value. _outer_ gaps can
	also be specified per side with _top_, _right_, _bottom_, and _left_ or
	per direction with _horizontal_ and _vertical_.

	This affects new workspaces only, and is used when the workspace doesn't
	have its own gaps settings (see: workspace <ws> gaps ...).

*hide_edge_borders* [--i3] none|vertical|horizontal|both|smart|smart_no_gaps
	Hides window borders adjacent to the screen edges. Default is _none_. The
	_--i3_ option enables i3-compatible behavior to hide the title bar on
	tabbed and stacked containers with one child. The _smart_|_smart_no_gaps_
	options are equivalent to setting _smart_borders_ smart|no_gaps and
	_hide_edge_borders_ none.

*input* <input_device> <input-subcommands...>
	For details on input subcommands, see *sway-input*(5).

	\* may be used in lieu of a specific device name to configure all input
	devices. A list of input device names may be obtained via *swaymsg -t
	get_inputs*.

*seat* <seat> <seat-subcommands...>
	For details on seat subcommands, see *sway-input*(5).

*kill*
	Kills (closes) the currently focused container and all of its children.

*smart_borders* on|no_gaps|off
	If smart_borders are _on_, borders will only be enabled if the workspace
	has more than one visible child. If smart_borders is set to _no_gaps_,
	borders will only be enabled if the workspace has more than one visible
	child and gaps equal to zero.

*smart_gaps* on|off
	If smart_gaps are _on_ gaps will only be enabled if a workspace has more
	than one child.

*mark* --add|--replace [--toggle] <identifier>
	Marks are arbitrary labels that can be used to identify certain windows and
	then jump to them at a later time. Each _identifier_ can only be set on a
	single window at a time since they act as a unique identifier. By default,
	*mark* sets _identifier_ as the only mark on a window. _--add_ will instead
	add _identifier_ to the list of current marks for that window. If _--toggle_
	is specified mark will remove _identifier_ if it is already marked.

*mode* <mode>
	Switches to the specified mode. The default mode is _default_.

*mode* [--pango_markup] <mode> <mode-subcommands...>
	The only valid _mode-subcommands..._ are *bindsym*, *bindcode*,
	*bindswitch*, and *set*. If _--pango_markup_ is given, then _mode_ will be
	interpreted as pango markup.

*mouse_warping* output|container|none
	If _output_ is specified, the mouse will be moved to new outputs as you
	move focus between them. If _container_ is specified, the mouse will be
	moved to the middle of the container on switch. Default is _output_.

*no_focus* <criteria>
	Prevents windows matching <criteria> from being focused automatically when
	they're created. This has no effect on the first window in a workspace.

*output* <output_name> <output-subcommands...>
	For details on output subcommands, see *sway-output*(5).

	\* may be used in lieu of a specific output name to configure all outputs.
	A list of output names may be obtained via *swaymsg -t get_outputs*.

*popup_during_fullscreen* smart|ignore|leave_fullscreen
	Determines what to do when a fullscreen view opens a dialog.
	If _smart_ (the default), the dialog will be displayed. If _ignore_, the
	dialog will not be rendered. If _leave_fullscreen_, the view will exit
	fullscreen mode and the dialog will be rendered.

*set* $<name> <value>
	Sets variable $_name_ to _value_. You can use the new variable in the
	arguments of future commands. When the variable is used, it can be escaped
	with an additional $ (ie $$_name_) to have the replacement happen at run
	time instead of when reading the config. However, it does not always make
	sense for the variable to be replaced at run time since some arguments do
	need to be known at config time.

*show_marks* yes|no
	If *show_marks* is yes, marks will be displayed in the window borders.
	Any mark that starts with an underscore will not be drawn even if
	*show_marks* is yes. The default is _yes_.

*opacity* [set|plus|minus] <value>
	Adjusts the opacity of the window between 0 (completely transparent) and
	1 (completely opaque). If the operation is omitted, _set_ will be used.

*tiling_drag*  enable|disable|toggle
	Sets whether or not tiling containers can be dragged with the mouse. If
	_enabled_ (default), the _floating_mod_ can be used to drag tiling, as well
	as floating, containers. Using the left mouse button on title bars without
	the _floating_mod_ will also allow the container to be dragged. _toggle_
	should not be used in the config file.

*tiling_drag_threshold* <threshold>
	Sets the threshold that must be exceeded for a container to be dragged by
	its titlebar. This has no effect if _floating_mod_ is used or if
	_tiling_drag_ is set to _disable_.  Once the threshold has been exceeded
	once, the drag starts and the cursor can come back inside the threshold
	without stopping the drag.  _threshold_ is multiplied by the scale of the
	output that the cursor on.  The default is 9.

*title_align* left|center|right
	Sets the title alignment. If _right_ is selected and _show_marks_ is set
	to _yes_, the marks will be shown on the _left_ side instead of the
	_right_ side.

*unbindswitch* <switch>:<state>
	Removes a binding for when <switch> changes to <state>.

*unbindsym* [--whole-window] [--border] [--exclude-titlebar] [--release] [--locked] \
[--to-code] [--input-device=<device>] <key combo>
	Removes the binding for _key combo_ that was previously bound with the
	given flags.  If _input-device_ is given, only the binding for that
	input device will be unbound.

	*unbindcode* [--whole-window] [--border] [--exclude-titlebar] [--release] \
[--locked] [--input-device=<device>] <code>
	is also available for unbinding with key/button codes instead of key/button names.

*unmark* [<identifier>]
	*unmark* will remove _identifier_ from the list of current marks on a
	window. If _identifier_ is omitted, all marks are removed.

*urgent* enable|disable|allow|deny
	Using _enable_ or _disable_ manually sets or unsets the window's urgent
	state. Using _allow_ or _deny_ controls the window's ability to set itself
	as urgent. By default, windows are allowed to set their own urgency.

*workspace* [--no-auto-back-and-forth] [number] <[num:]name>
	Switches to the specified workspace. The _num:_ portion of the name is
	optional and will be used for ordering. If _num:_ is not given and
	_name_ is a number, then it will be also be used for ordering.

	If the _no-auto-back-and-forth_ option is given, then this command will
	not perform a back-and-forth operation when the workspace is already
	focused and _workspace_auto_back_and_forth_ is enabled.

	If the _number_ keyword is specified and a workspace with the number
	already exists, then the workspace with the number will be used. If a
	workspace with the number does not exist, a new workspace will be created
	with the name _name_.

*workspace* prev|next
	Switches to the next workspace on the current output or on the next output
	if currently on the last workspace.

*workspace* prev_on_output|next_on_output
	Switches to the next workspace on the current output.

*workspace* back_and_forth
	Switches to the previously focused workspace.

*workspace* <name> gaps inner|outer|horizontal|vertical|top|right|bottom|left
<amount>
	Specifies that workspace _name_ should have the given gaps settings when it
	is created.

	This command does not affect existing workspaces. To alter the gaps of an
	existing workspace, use the _gaps_ command.

*workspace* <name> output <outputs...>
	Specifies that workspace _name_ should be shown on the specified _outputs_.
	Multiple outputs can be listed and the first available will be used. If the
	workspace gets placed on an output further down the list and an output that
	is higher on the list becomes available, the workspace will be moved to the
	higher priority output.

	This command does not affect existing workspaces. To move an existing
	workspace, use the _move_ command in combination with the _workspace_
	criteria (non-empty workspaces only) or _workspace_ command (to switch
	to the workspace before moving).

*workspace_auto_back_and_forth* yes|no
	When _yes_, repeating a workspace switch command will switch back to the
	prior workspace. For example, if you are currently on workspace 1,
	switch to workspace 2, then invoke the *workspace 2* command again, you
	will be returned to workspace 1. Default is _no_.

# CRITERIA

A criteria is a string in the form of, for example:

```
[class="[Rr]egex.*" title="some title"]
```

The string contains one or more (space separated) attribute/value pairs. They
are used by some commands to choose which views to execute actions on. All
attributes must match for the criteria to match. Criteria is retained across
commands separated by a *,*, but will be reset (and allow for new criteria, if
desired) for commands separated by a *;*.

Criteria may be used with either the *for_window* or *assign* commands to
specify operations to perform on new views. A criteria may also be used to
perform specific commands (ones that normally act upon one window) on all views
that match that criteria. For example:

Focus on a window with the mark "IRC":

```
[con_mark="IRC"] focus
```

Kill all windows with the title "Emacs":

```
[class="Emacs"] kill
```

You may like to use swaymsg -t get_tree for finding the values of these
properties in practice for your applications.

The following attributes may be matched with:

*app_id*
	Compare value against the app id. Can be a regular expression. If value is
	\_\_focused\_\_, then the app id must be the same as that of the currently
	focused window. _app_id_ are specific to Wayland applications.

*class*
	Compare value against the window class. Can be a regular expression. If
	value is \_\_focused\_\_, then the window class must be the same as that of
	the currently focused window. _class_ are specific to X11 applications.

*con_id*
	Compare against the internal container ID, which you can find via IPC. If
	value is \_\_focused\_\_, then the id must be the same as that of the
	currently focused window.

*con_mark*
	Compare against the window marks. Can be a regular expression.

*floating*
	Matches floating windows.

*id*
	Compare value against the X11 window ID. Must be numeric.

*instance*
	Compare value against the window instance. Can be a regular expression. If
	value is \_\_focused\_\_, then the window instance must be the same as that
	of the currently focused window.

*pid*
	Compare value against the window's process ID. Must be numeric.

*shell*
	Compare value against the window shell, such as "xdg_shell" or "xwayland".
	Can be a regular expression. If value is \_\_focused\_\_, then the shell
	must be the same as that of the currently focused window.

*tiling*
	Matches tiling windows.

*title*
	Compare against the window title. Can be a regular expression. If value is
	\_\_focused\_\_, then the window title must be the same as that of the
	currently focused window.

*urgent*
	Compares the urgent state of the window. Can be _first_, _last_, _latest_,
	_newest_, _oldest_ or _recent_.

*window_role*
	Compare against the window role (WM_WINDOW_ROLE). Can be a regular
	expression. If value is \_\_focused\_\_, then the window role must be the
	same as that of the currently focused window.

*window_type*
	Compare against the window type (\_NET_WM_WINDOW_TYPE). Possible values
	are normal, dialog, utility, toolbar, splash, menu, dropdown_menu,
	popup_menu, tooltip and notification.

*workspace*
	Compare against the workspace name for this view. Can be a regular
	expression. If the value is \_\_focused\_\_, then all the views on the
	currently focused workspace matches.

# SEE ALSO

*sway*(1) *sway-input*(5) *sway-output*(5) *sway-bar*(5) *sway-ipc*(7)
