sway-input(5)

# NAME

sway-input - input configuration file and commands

# DESCRIPTION

Sway allows for configuration of devices within the sway configuration file.
To obtain a list of available device identifiers, run *swaymsg -t get_inputs*.
Settings can also be applied to all input devices by using the wildcard, _\*_,
in place of _\<identifier\>_ in the commands below. In addition, the settings
can be applied to a type of device, by using _type:\<input_type\>_ in place
of _\<identifier\>_.

In the configuration file, settings with a more specific selector take
precedence over more general ones: _\<identifier\>_ \> _type:\<input_type\>_ \>
_\*_.  When executing input commands, however, the settings are applied to all
matching input devices!  This means that _type:\<input_type\>_ can override
previously set _\<identifier\>_ settings, even though in a configuration file
they would take precedence.  Similarly _\*_ can override both _\<identifier\>_
and _type:\<input_type\>_ settings, if applied later.

Tip: If the configuration settings do not appear to be taking effect, you could
try using _\*_ instead of _\<identifier\>_. If it works with the wildcard, try
using a different identifier from *swaymsg -t get_inputs* until you find the
correct input device.

Current available input types are:

- touchpad
- pointer
- keyboard
- touch
- tablet_tool
- tablet_pad
- switch

Note: The type configurations are applied as the devices appear and get applied
on top of the existing device configurations.

# INPUT COMMANDS

## KEYBOARD CONFIGURATION

*input* <identifier> repeat_delay <milliseconds>
	Sets the amount of time a key must be held before it starts repeating.

*input* <identifier> repeat_rate <characters per second>
	Sets the frequency of key repeats once the repeat_delay has passed.

For more information on these xkb configuration options, see
*xkeyboard-config*(7).

*input* <identifier> xkb_file <file_name>
	Sets all xkb configurations from a complete .xkb file. This file can be
	dumped from _xkbcomp $DISPLAY keymap.xkb_. This setting overrides
	xkb_layout, xkb_model, xkb_options, xkb_rules, and xkb_variant settings.

*input* <identifier> xkb_layout <layout_name>
	Sets the layout of the keyboard like _us_ or _de_.

	Multiple layouts can be specified by separating them with commas.

*input* <identifier> xkb_model <model_name>
	Sets the model of the keyboard. This has an influence for some extra keys
	your keyboard might have.

*input* <identifier> xkb_options <options>
	Sets extra xkb configuration options for the keyboard.

	Multiple options can be specified by separating them with commas.

*input* <identifier> xkb_rules <rules>
	Sets files of rules to be used for keyboard mapping composition.

*input* <identifier> xkb_switch_layout <index>|next|prev
	Changes the active keyboard layout to <index> counting from zero or to
	next or previous layout on the list. If there is no next or previous
	layout, this command hops to the other end of the list.

	This can be used when multiple layouts are configured with *xkb_layout*.
	A list of layouts you can switch between can be obtained with
	*swaymsg -t get_inputs*.

*input* <identifier> xkb_variant <variant>
	Sets the variant of the keyboard like _dvorak_ or _colemak_.

The following commands may only be used in the configuration file.

*input* <identifier> xkb_capslock enabled|disabled
	Initially enables or disables CapsLock on startup, the default is disabled.

*input* <identifier> xkb_numlock enabled|disabled
	Initially enables or disables NumLock on startup, the default is disabled.

## TABLET CONFIGURATION

*input* <identifier> tool_mode <tool> <absolute|relative>
	Sets whether movement of a tablet tool should be treated as absolute or
	relative; the default is absolute.

	Valid values for _\<tool\>_ are currently "pen", "eraser", "brush",
	"pencil", "airbrush", and the wildcard _\*_, which matches all tools.

	Mouse and lens tools ignore this setting and are always treated as relative.

## MAPPING CONFIGURATION

*input* <identifier> map_to_output <identifier>
	Maps inputs from this device to the specified output. Only meaningful if the
	device is a pointer, touch, or drawing tablet device.

	The wildcard _\*_ can be used to map the input device to the whole desktop
	layout.

*input* <identifier> map_to_region <X> <Y> <width> <height>
	Maps inputs from this device to the specified region of the global output
	layout. Only meaningful if the device is a pointer, touch, or drawing tablet
	device.

*input* <identifier> map_from_region <X1xY1> <X2xY2>
	Ignores inputs from this device that do not occur within the specified
	region. Can be in millimeters (e.g. 10x20mm 20x40mm) or in terms of 0..1
	(e.g. 0.5x0.5 0.7x0.7). Not all devices support millimeters. Only meaningful
	if the device is not a keyboard and provides events in absolute terms (such
	as a drawing tablet or touch screen - most pointers provide events relative
	to the previous frame).

## LIBINPUT CONFIGURATION

*input* <identifier> accel_profile adaptive|flat
	Sets the pointer acceleration profile for the specified input device.

*input* <identifier> calibration_matrix <6 space-separated floating point values>
	Sets the calibration matrix.

*input* <identifier> click_method none|button_areas|clickfinger
	Changes the click method for the specified device.

*input* <identifier> drag enabled|disabled
	Enables or disables tap-and-drag for specified input device.

*input* <identifier> drag_lock enabled|disabled
	Enables or disables drag lock for specified input device.

*input* <identifier> dwt enabled|disabled
	Enables or disables disable-while-typing for the specified input device.

*input* <identifier> events enabled|disabled|disabled_on_external_mouse|toggle [<toggle-modes>]
	Enables or disables send_events for specified input device. Disabling
	send_events disables the input device.

	The _toggle_ option cannot be used in the config. If no toggle modes are
	listed, all supported modes for the device will be toggled through in the
	order: enabled,	disabled_on_external_mouse, disabled, (loop back). If
	toggle modes are listed, they will be cycled through, defaulting to the
	first mode listed if the current mode is not in the list. They will also
	not be checked to see if they are supported for the device and may fail.

*input* <identifier> left_handed enabled|disabled
	Enables or disables left handed mode for specified input device.

*input* <identifier> middle_emulation enabled|disabled
	Enables or disables middle click emulation.

*input* <identifier> natural_scroll enabled|disabled
	Enables or disables natural (inverted) scrolling for the specified input
	device.

*input* <identifier> pointer_accel [<-1|1>]
	Changes the pointer acceleration for the specified input device.

*input* <identifier> scroll_button disable|button[1-3,8,9]|<event-code-or-name>
	Sets the button used for scroll_method on_button_down. The button can
	be given as an event name or code, which can be obtained from *libinput
	debug-events*, or as a x11 mouse button (button[1-3,8,9]). If set to
	_disable_, it disables the scroll_method on_button_down.

*input* <identifier> scroll_factor <floating point value>
	Changes the scroll factor for the specified input device. Scroll speed will
	be scaled by the given value, which must be non-negative.

*input* <identifier> scroll_method none|two_finger|edge|on_button_down
	Changes the scroll method for the specified input device.

*input* <identifier> tap enabled|disabled
	Enables or disables tap for specified input device.

*input* <identifier> tap_button_map lrm|lmr
	Specifies which button mapping to use for tapping. _lrm_ treats 1 finger as
	left click, 2 fingers as right click, and 3 fingers as middle click. _lmr_
	treats 1 finger as left click, 2 fingers as middle click, and 3 fingers as
	right click.

## SEAT CONFIGURATION

Configure options for multiseat mode.

A *seat* is a collection of input devices that act independently of each other.
Seats are identified by name and the default seat is _seat0_ if no seats are
configured. While sway is running, _-_ (hyphen) can be used as an alias for the
current seat. Each seat has an independent keyboard focus and a separate cursor
that is controlled by the pointer devices of the seat. This is useful for
multiple people using the desktop at the same time with their own devices (each
sitting in their own "seat"). The wildcard character, _\*_, can also be used in
place of _\<identifier\>_ to change settings for all seats.

Tip: If the configuration settings do not appear to be taking effect, you could
try using _\*_ instead of _\<identifier\>_. If it works with the wildcard, try
using a different identifier from *swaymsg -t get_seats* until you find the
correct seat.

*seat* <name> attach <input_identifier>
	Attach an input device to this seat by its input identifier. A special
	value of "\*" will attach all devices to the seat.

*seat* <seat> cursor move|set <x> <y>
	Move specified seat's cursor relative to current position or wrap to
	absolute coordinates (with respect to the global coordinate space).
	Specifying either value as 0 will not update that coordinate.

*seat* <seat> cursor press|release button[1-9]|<event-name-or-code>
	Simulate pressing (or releasing) the specified mouse button on the
	specified seat. The button can either be provided as a button event name or
	event code, which can be obtained from *libinput debug-events*, or as an x11
	mouse button (button[1-9]). If using button[4-7], which map to axes, an axis
	event will be simulated, however _press_ and _release_ will be ignored and
	both will occur.

*seat* <name> fallback true|false
	Set this seat as the fallback seat. A fallback seat will attach any device
	not explicitly attached to another seat (similar to a "default" seat).

*seat* <name> hide_cursor <timeout>|when-typing [enable|disable]
	Hides the cursor image after the specified event occured.

	If _timeout_ is specified, then the cursor will be hidden after _timeout_
	(in milliseconds) has elapsed with no activity on the cursor. A timeout of 0
	(default) disables hiding the cursor. The minimal timeout is 100 and any
	value less than that (aside from 0), will be increased to 100.

	If _when-typing_ is enabled, then the cursor will be hidden whenever a key
	is pressed.

*seat* <name> idle_inhibit <sources...>
	Sets the set of input event sources which can prevent the seat from
	becoming idle, as a space separated list of source names. Valid names are
	"keyboard", "pointer", "touchpad", "touch", "tablet_pad", "tablet_tool",
	and "switch". The default behavior is to prevent idle on any event.

*seat* <name> idle_wake <sources...>
	Sets the set of input event sources which can wake the seat from
	its idle state, as a space separated list of source names. Valid names are
	"keyboard", "pointer", "touchpad", "touch", "tablet_pad", "tablet_tool",
	and "switch". The default behavior is to wake from idle on any event.

*seat* <name> keyboard_grouping none|smart
	Set how the keyboards in the seat are grouped together. Currently, there
	are two options. _none_ will disable all keyboard grouping. This will make
	it so each keyboard device has its own isolated state. _smart_ will
	group the keyboards in the seat by their keymap and repeat info. This is
	useful for when the keyboard appears as multiple separate input devices.
	In this mode, the effective layout is synced between the keyboards in the
	group. The default is _smart_. To restore the behavior of older versions
	of sway, use _none_.

*seat* <name> pointer_constraint enable|disable|escape
	Enables or disables the ability for clients to capture the cursor (enabled
	by default) for the seat. This is primarily useful for video games. The
	"escape" command can be used at runtime to escape from a captured client.

*seat* <name> shortcuts_inhibitor enable|disable|activate|deactivate|toggle
	Enables or disables the ability of clients to inhibit keyboard
	shortcuts for the seat. This is primarily useful for virtualization and
	remote desktop software. Subcommands _enable_ and _disable_ affect
	whether future inhibitors are honoured by default, i.e. activated
	automatically, the default being _enable_. When used at runtime,
	_disable_ also disables any currently active inhibitors. _activate_,
	_deactivate_ and _toggle_ are only useable at runtime and change the
	state of a potentially existing inhibitor on the currently focused
	window. This can be used with the current seat alias (_-_) to affect
	only the currently focused window of the current seat. Subcommand
	_deactivate_ is particularly useful in an _--inhibited_ *bindsym* to
	escape a state where shortcuts are inhibited and the client becomes
	uncooperative. It is worth noting that whether disabled or deactivated
	inhibitors are removed is entirely up to the client. Depending on the
	client it may therefore be possible to (re-)activate them later. Any
	visual indication that an inhibitor is present is currently left to the
	client as well.

*seat* <name> xcursor_theme <theme> [<size>]
	Override the system default XCursor theme. The default seat's
	(_seat0_) theme is also used as the default cursor theme in
	XWayland, and exported through the _XCURSOR_THEME_ and
	_XCURSOR_SIZE_ environment variables.

# SEE ALSO

*sway*(5) *sway-output*(5) *xkeyboard-config*(7)
