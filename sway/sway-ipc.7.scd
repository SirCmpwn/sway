sway-ipc(7)

# NAME

sway-ipc - IPC protocol for sway

# DESCRIPTION

This details the interprocess communication (IPC) protocol for *sway*(1). This
IPC protocol can be used to control or obtain information from a sway process.

The IPC protocol uses a UNIX socket as the method of communication. The path
to the socket is stored in the environment variable _SWAYSOCK_ and, for
backwards compatibility with i3, _I3SOCK_. You can also retrieve the socket
path by calling _sway --get-socketpath_.

# MESSAGE AND REPLY FORMAT

The format for messages and replies is:
	<magic-string> <payload-length> <payload-type> <payload>
Where++
	<magic-string> is _i3-ipc_, for compatibility with i3++
	<payload-length> is a 32-bit integer in native byte order++
	<payload-type> is a 32-bit integer in native byte order

For example, sending the _exit_ command would look like the following hexdump:
```
00000000 | 69 33 2d 69 70 63 04 00 00 00 00 00 00 00 65 78 |i3-ipc........ex|
00000010 | 69 74                                           |it              |
```

The payload for replies will be a valid serialized JSON data structure.

# MESSAGES AND REPLIES

The following message types and their corresponding reply types are currently
supported. *For all replies, any properties not listed are subject to removal.*

[- *TYPE NUMBER*
:- *MESSAGE NAME*
:- *PURPOSE*
|- 0
:  RUN_COMMAND
:[ Runs the payload as sway commands
|- 1
:  GET_WORKSPACES
:  Get the list of current workspaces
|- 2
:  SUBSCRIBE
:  Subscribe the IPC connection to the events listed in the payload
|- 3
:  GET_OUTPUTS
:  Get the list of current outputs
|- 4
:  GET_TREE
:  Get the node layout tree
|- 5
:  GET_MARKS
:  Get the names of all the marks currently set
|- 6
:  GET_BAR_CONFIG
:  Get the specified bar config or a list of bar config names
|- 7
:  GET_VERSION
:  Get the version of sway that owns the IPC socket
|- 8
:  GET_BINDING_MODES
:  Get the list of binding mode names
|- 9
:  GET_CONFIG
:  Returns the config that was last loaded
|- 10
:  SEND_TICK
:  Sends a tick event with the specified payload
|- 11
:  SYNC
:  Replies failure object for i3 compatibility
|- 12
:  GET_BINDING_STATE
:  Request the current binding state, e.g. the currently active binding mode name.
|- 100
:  GET_INPUTS
:  Get the list of input devices
|- 101
:  GET_SEATS
:  Get the list of seats

## 0. RUN_COMMAND

*MESSAGE*++
Parses and runs the payload as sway commands

*REPLY*++
An array of objects corresponding to each command that was parsed. Each object
has the property _success_, which is a boolean indicating whether the command
was successful. The object may also contain the properties _error_ and _parse\_error_.
The _error_ property is a human readable error message while _parse\_error_ is a
boolean indicating whether the reason the command failed was because the command
was unknown or not able to be parsed.

*Example Reply:*
```
[
	{
		"success": true
	},
	{
		"success": false,
		"parse_error": true,
		"error": "Invalid/unknown command"
	}
]
```

## 1. GET_WORKSPACES

*MESSAGE*++
Retrieves the list of workspaces.

*REPLY*++
The reply is an array of objects corresponding to each workspace. Each object
has the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- num
:  integer
:[ The workspace number or -1 for workspaces that do not start with a number
|- name
:  string
:  The name of the workspace
|- visible
:  boolean
:  Whether the workspace is currently visible on any output
|- focused
:  boolean
:  Whether the workspace is currently focused by the default seat (_seat0_)
|- urgent
:  boolean
:  Whether a view on the workspace has the urgent flag set
|- rect
:  object
:  The bounds of the workspace. It consists of _x_, _y_, _width_, and _height_
|- output
:  string
:  The name of the output that the workspace is on


*Example Reply:*
```
[
	{
		"num": 1,
		"name": "1",
		"visible": true,
		"focused": true,
		"rect": {
			"x": 0,
			"y": 23,
			"width": 1920,
			"height": 1057
		},
		"output": "eDP-1"
	},
]
```

## 2. SUBSCRIBE

*MESSAGE*++
Subscribe this IPC connection to the event types specified in the message
payload. The payload should be a valid JSON array of events. See the _EVENTS_
section for the list of supported events.

*REPLY*++
A single object that contains the property _success_, which is a boolean value
indicating whether the subscription was successful or not.

*Example Reply:*
```
{
	"success": true
}
```

## 3. GET_OUTPUTS

*MESSAGE*++
Retrieve the list of outputs

*REPLY*++
An array of objects corresponding to each output. Each object has the
following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- name
:  string
:[ The name of the output. On DRM, this is the connector
|- make
:  string
:  The make of the output
|- model
:  string
:  The model of the output
|- serial
:  string
:  The output's serial number as a hexadecimal string
|- active
:  boolean
:  Whether this output is active/enabled
|- dpms
:  boolean
:  Whether this output is on/off (via DPMS)
|- primary
:  boolean
:  For i3 compatibility, this will be false. It does not make sense in Wayland
|- scale
:  float
:  The scale currently in use on the output or _-1_ for disabled outputs
|- subpixel_hinting
:  string
:  The subpixel hinting current in use on the output. This can be _rgb_, _bgr_, _vrgb_, _vbgr_, or _none_
|- transform
:  string
:  The transform currently in use for the output. This can be _normal_, _90_,
   _180_, _270_, _flipped-90_, _flipped-180_, or _flipped-270_
|- current_workspace
:  string
:  The workspace currently visible on the output or _null_ for disabled outputs
|- modes
:  array
:  An array of supported mode objects. Each object contains _width_, _height_,
   and _refresh_
|- current_mode
:  object
:  An object representing the current mode containing _width_, _height_, and
   _refresh_
|- rect
:  object
:  The bounds for the output consisting of _x_, _y_, _width_, and _height_


*Example Reply:*
```
[
	{
		"name": "HDMI-A-2",
		"make": "Unknown",
		"model": "NS-19E310A13",
		"serial": "0x00000001",
		"active": true,
		"primary": false,
		"scale": 1.0,
		"subpixel_hinting": "rgb",
		"transform": "normal",
		"current_workspace": "1",
		"modes": [
			{
				"width": 640,
				"height": 480,
				"refresh": 59940
			},
			{
				"width": 800,
				"height": 600,
				"refresh": 60317
			},
			{
				"width": 1024,
				"height": 768,
				"refresh": 60004
			},
			{
				"width": 1920,
				"height": 1080,
				"refresh": 60000
			}
		],
		"current_mode": {
			"width": 1920,
			"height": 1080,
			"refresh": 60000
		}
	}
]
```

## 4. GET_TREE

*MESSAGE*++
Retrieve a JSON representation of the tree

*REPLY*++
An array of object the represent the current tree. Each object represents one
node and will have the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- id
:  integer
:[ The internal unique ID for this node
|- name
:  string
:  The name of the node such as the output name or window title. For the
   scratchpad, this will be _\_\_i3\_scratch_ for compatibility with i3.
|- type
:  string
:  The node type. It can be _root_, _output_, _workspace_, _con_, or
   _floating\_con_
|- border
:  string
:  The border style for the node. It can be _normal_, _none_, _pixel_, or _csd_
|- current_border_width
:  integer
:  Number of pixels used for the border width
|- layout
:  string
:  The node's layout. It can either be _splith_, _splitv_, _stacked_,
   _tabbed_, or _output_
|- orientation
:  string
:  The node's orientation. It can be _vertical_, _horizontal_, or _none_
|- percent
:  float
:  The percentage of the node's parent that it takes up or _null_ for the root
   and other special nodes such as the scratchpad
|- rect
:  object
:  The absolute geometry of the node. The window decorations are excluded from
   this, but borders are included.
|- window_rect
:  object
:  The geometry of the contents inside the node. The window decorations are
   excluded from this calculation, but borders are included.
|- deco_rect
:  object
:  The geometry of the decorations for the node relative to the parent node
|- geometry
:  object
:  The natural geometry of the contents if it were to size itself
|- urgent
:  boolean
:  Whether the node or any of its descendants has the urgent hint set. Note:
   This may not exist when compiled without _xwayland_ support
|- sticky
:  boolean
:  Whether the node is sticky (shows on all workspaces)
|- marks
:  array
:  List of marks assigned to the node
|- focused
:  boolean
:  Whether the node is currently focused by the default seat (_seat0_)
|- focus
:  array
:  Array of child node IDs in the current focus order
|- nodes
:  array
:  The tiling children nodes for the node
|- floating_nodes
:  array
:  The floating children nodes for the node
|- representation
:  string
:  (Only workspaces) A string representation of the layout of the workspace
   that can be used as an aid in submitting reproduction steps for bug reports
|- fullscreen_mode
:  integer
:  (Only containers and views) The fullscreen mode of the node. 0 means none, 1 means 
   full workspace, and 2 means global fullscreen
|- app_id
:  string
:  (Only views) For an xdg-shell view, the name of the application, if set.
   Otherwise, _null_
|- pid
:  integer
:  (Only views) The PID of the application that owns the view
|- visible
:  boolean
:  (Only views) Whether the node is visible
|- shell
:  string
:  (Only views) The shell of the view, such as _xdg\_shell_ or _xwayland_
|- inhibit_idle
:  boolean
:  (Only views) Whether the view is inhibiting the idle state
|- idle_inhibitors
:  object
:  (Only views) An object containing the state of the _application_ and _user_ idle inhibitors.
    _application_ can be _enabled_ or _none_.
    _user_ can be _focus_, _fullscreen_, _open_, _visible_ or _none_.
|- window
:  integer
:  (Only xwayland views) The X11 window ID for the xwayland view
|- window_properties
:  object
:  (Only xwayland views) An object containing the _title_, _class_, _instance_,
   _window\_role_, _window\_type_, and _transient\_for_ for the view


*Example Reply:*
```
{
	"id": 1,
	"name": "root",
	"rect": {
		"x": 0,
		"y": 0,
		"width": 1920,
		"height": 1080
	},
	"focused": false,
	"focus": [
		3
	],
	"border": "none",
	"current_border_width": 0,
	"layout": "splith",
	"orientation": "horizontal",
	"percent": null,
	"window_rect": {
		"x": 0,
		"y": 0,
		"width": 0,
		"height": 0
	},
	"deco_rect": {
		"x": 0,
		"y": 0,
		"width": 0,
		"height": 0
	},
	"geometry": {
		"x": 0,
		"y": 0,
		"width": 0,
		"height": 0
	},
	"window": null,
	"urgent": false,
	"floating_nodes": [
	],
	"sticky": false,
	"type": "root",
	"nodes": [
		{
			"id": 2147483647,
			"name": "__i3",
			"rect": {
				"x": 0,
				"y": 0,
				"width": 1920,
				"height": 1080
			},
			"focused": false,
			"focus": [
				2147483646
			],
			"border": "none",
			"current_border_width": 0,
			"layout": "output",
			"orientation": "horizontal",
			"percent": null,
			"window_rect": {
				"x": 0,
				"y": 0,
				"width": 0,
				"height": 0
			},
			"deco_rect": {
				"x": 0,
				"y": 0,
				"width": 0,
				"height": 0
			},
			"geometry": {
				"x": 0,
				"y": 0,
				"width": 0,
				"height": 0
			},
			"window": null,
			"urgent": false,
			"floating_nodes": [
			],
			"sticky": false,
			"type": "output",
			"nodes": [
				{
					"id": 2147483646,
					"name": "__i3_scratch",
					"rect": {
						"x": 0,
						"y": 0,
						"width": 1920,
						"height": 1080
					},
					"focused": false,
					"focus": [
					],
					"border": "none",
					"current_border_width": 0,
					"layout": "splith",
					"orientation": "horizontal",
					"percent": null,
					"window_rect": {
						"x": 0,
						"y": 0,
						"width": 0,
						"height": 0
					},
					"deco_rect": {
						"x": 0,
						"y": 0,
						"width": 0,
						"height": 0
					},
					"geometry": {
						"x": 0,
						"y": 0,
						"width": 0,
						"height": 0
					},
					"window": null,
					"urgent": false,
					"floating_nodes": [
					],
					"sticky": false,
					"type": "workspace"
				}
			]
		},
		{
			"id": 3,
			"name": "eDP-1",
			"rect": {
				"x": 0,
				"y": 0,
				"width": 1920,
				"height": 1080
			},
			"focused": false,
			"focus": [
				4
			],
			"border": "none",
			"current_border_width": 0,
			"layout": "output",
			"orientation": "none",
			"percent": 1.0,
			"window_rect": {
				"x": 0,
				"y": 0,
				"width": 0,
				"height": 0
			},
			"deco_rect": {
				"x": 0,
				"y": 0,
				"width": 0,
				"height": 0
			},
			"geometry": {
				"x": 0,
				"y": 0,
				"width": 0,
				"height": 0
			},
			"window": null,
			"urgent": false,
			"floating_nodes": [
			],
			"sticky": false,
			"type": "output",
			"active": true,
			"primary": false,
			"make": "Unknown",
			"model": "0x38ED",
			"serial": "0x00000000",
			"scale": 1.0,
			"transform": "normal",
			"current_workspace": "1",
			"modes": [
				{
					"width": 1920,
					"height": 1080,
					"refresh": 60052
				}
			],
			"current_mode": {
				"width": 1920,
				"height": 1080,
				"refresh": 60052
			},
			"nodes": [
				{
					"id": 4,
					"name": "1",
					"rect": {
						"x": 0,
						"y": 23,
						"width": 1920,
						"height": 1057
					},
					"focused": false,
					"focus": [
						6,
						5
					],
					"border": "none",
					"current_border_width": 0,
					"layout": "splith",
					"orientation": "horizontal",
					"percent": null,
					"window_rect": {
						"x": 0,
						"y": 0,
						"width": 0,
						"height": 0
					},
					"deco_rect": {
						"x": 0,
						"y": 0,
						"width": 0,
						"height": 0
					},
					"geometry": {
						"x": 0,
						"y": 0,
						"width": 0,
						"height": 0
					},
					"window": null,
					"urgent": false,
					"floating_nodes": [
					],
					"sticky": false,
					"num": 1,
					"output": "eDP-1",
					"type": "workspace",
					"representation": "H[URxvt termite]",
					"nodes": [
						{
							"id": 5,
							"name": "urxvt",
							"rect": {
								"x": 0,
								"y": 23,
								"width": 960,
								"height": 1057
							},
							"focused": false,
							"focus": [
							],
							"border": "normal",
							"current_border_width": 2,
							"layout": "none",
							"orientation": "none",
							"percent": 0.5,
							"window_rect": {
								"x": 2,
								"y": 0,
								"width": 956,
								"height": 1030
							},
							"deco_rect": {
								"x": 0,
								"y": 0,
								"width": 960,
								"height": 25
							},
							"geometry": {
								"x": 0,
								"y": 0,
								"width": 1124,
								"height": 422
							},
							"window": 4194313,
							"urgent": false,
							"floating_nodes": [
							],
							"sticky": false,
							"type": "con",
							"fullscreen_mode": 0,
							"pid": 23959,
							"app_id": null,
							"visible": true,
							"shell": "xwayland",
							"inhibit_idle": true,
							"idle_inhibitors": {
								"application": "none",
								"user": "visible",
							},
							"window_properties": {
								"class": "URxvt",
								"instance": "urxvt",
								"title": "urxvt",
								"transient_for": null
							},
							"nodes": [
							]
						},
						{
							"id": 6,
							"name": "",
							"rect": {
								"x": 960,
								"y": 23,
								"width": 960,
								"height": 1057
							},
							"focused": true,
							"focus": [
							],
							"border": "normal",
							"current_border_width": 2,
							"layout": "none",
							"orientation": "none",
							"percent": 0.5,
							"window_rect": {
								"x": 2,
								"y": 0,
								"width": 956,
								"height": 1030
							},
							"deco_rect": {
								"x": 0,
								"y": 0,
								"width": 960,
								"height": 25
							},
							"geometry": {
								"x": 0,
								"y": 0,
								"width": 817,
								"height": 458
							},
							"window": null,
							"urgent": false,
							"floating_nodes": [
							],
							"sticky": false,
							"type": "con",
							"fullscreen_mode": 0,
							"pid": 25370,
							"app_id": "termite",
							"visible": true,
							"shell": "xdg_shell",
							"inhibit_idle": false,
							"idle_inhibitors": {
								"application": "none",
								"user": "fullscreen",
							},
							"nodes": [
							]
						}
					]
				}
			]
		}
	]
}
```

## 5. GET_MARKS

*MESSAGE*++
Retrieve the currently set marks

*REPLY*++
An array of marks current set. Since each mark can only be set for one
container, this is a set so each value is unique and the order is undefined.

*Example Reply:*
```
[
	"one",
	"test"
]
```

## 6. GET_BAR_CONFIG (WITHOUT A PAYLOAD)

*MESSAGE*++
When sending without a payload, this retrieves the list of configured bar IDs

*REPLY*++
An array of bar IDs, which are strings

*Example Reply:*
```
[
	"bar-0",
	"bar-1"
]
```

## 6. GET_BAR_CONFIG (WITH A PAYLOAD)

*MESSAGE*++
When sent with a bar ID as the payload, this retrieves the config associated
with the specified by the bar ID in the payload. This is used by swaybar, but
could also be used for third party bars

*REPLY*++
An object that represents the configuration for the bar with the bar ID sent as
the payload. The following properties exists and more information about what
their value mean can be found in *sway-bar*(5):

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- id
:  string
:[ The bar ID
|- mode
:  string
:  The mode for the bar. It can be _dock_, _hide_, or _invisible_
|- position
:  string
:  The bar's position. It can currently either be _bottom_ or _top_
|- status_command
:  string
:  The command which should be run to generate the status line
|- font
:  string
:  The font to use for the text on the bar
|- workspace_buttons
:  boolean
:  Whether to display the workspace buttons on the bar
|- workspace_min_width
:  integer
:  Minimum width in px for the workspace buttons on the bar
|- binding_mode_indicator
:  boolean
:  Whether to display the current binding mode on the bar
|- verbose
:  boolean
:  For i3 compatibility, this will be the boolean value _false_.
|- colors
:  object
:  An object containing the _#RRGGBBAA_ colors to use for the bar. See below
   for more information
|- gaps
:  object
:  An object representing the gaps for the bar consisting of _top_, _right_,
   _bottom_, and _left_.
|- bar_height
:  integer
:  The absolute height to use for the bar or _0_ to automatically size based
   on the font
|- status_padding
:  integer
:  The vertical padding to use for the status line
|- status_edge_padding
:  integer
:  The horizontal padding to use for the status line when at the end of an
   output


The colors object contains the following properties, which are all strings
containing the _#RRGGBBAA_ representation of the color:
[- *PROPERTY*
:- *DESCRIPTION*
|- background
:[ The color to use for the bar background on unfocused outputs
|- statusline
:  The color to use for the status line text on unfocused outputs
|- separator
:  The color to use for the separator text on unfocused outputs
|- focused_background
:  The color to use for the background of the bar on the focused output
|- focused_statusline
:  The color to use for the status line text on the focused output
|- focused_separator
:  The color to use for the separator text on the focused output
|- focused_workspace_text
:  The color to use for the text of the focused workspace button
|- focused_workspace_bg
:  The color to use for the background of the focused workspace button
|- focused_workspace_border
:  The color to use for the border of the focused workspace button
|- active_workspace_text
:  The color to use for the text of the workspace buttons for the visible
   workspaces on unfocused outputs
|- active_workspace_bg
:  The color to use for the background of the workspace buttons for the visible
   workspaces on unfocused outputs
|- active_workspace_border
:  The color to use for the border of the workspace buttons for the visible
   workspaces on unfocused outputs
|- inactive_workspace_text
:  The color to use for the text of the workspace buttons for workspaces that
   are not visible
|- inactive_workspace_bg
:  The color to use for the background of the workspace buttons for workspaces
   that are not visible
|- inactive_workspace_border
:  The color to use for the border of the workspace buttons for workspaces
   that are not visible
|- urgent_workspace_text
:  The color to use for the text of the workspace buttons for workspaces that
   contain an urgent view
|- urgent_workspace_bg
:  The color to use for the background of the workspace buttons for workspaces
   that contain an urgent view
|- urgent_workspace_border
:  The color to use for the border of the workspace buttons for workspaces that
   contain an urgent view
|- binding_mode_text
:  The color to use for the text of the binding mode indicator
|- binding_mode_bg
:  The color to use for the background of the binding mode indicator
|- binding_mode_border
:  The color to use for the border of the binding mode indicator


*Example Reply:*
```
{
	"id": "bar-0",
	"mode": "dock",
	"position": "top",
	"status_command": "while date +'%Y-%m-%d %l:%M:%S %p'; do sleep 1; done",
	"font": "monospace 10",
	"gaps": {
		"top": 0,
		"right": 0,
		"bottom": 0,
		"left": 0
	},
	"bar_height": 0,
	"status_padding": 1,
	"status_edge_padding": 3,
	"workspace_buttons": true,
	"workspace_min_width": 0,
	"binding_mode_indicator": true,
	"verbose": false,
	"pango_markup": false,
	"colors": {
		"background": "#323232ff",
		"statusline": "#ffffffff",
		"separator": "#666666ff",
		"focused_background": "#323232ff",
		"focused_statusline": "#ffffffff",
		"focused_separator": "#666666ff",
		"focused_workspace_border": "#4c7899ff",
		"focused_workspace_bg": "#285577ff",
		"focused_workspace_text": "#ffffffff",
		"inactive_workspace_border": "#32323200",
		"inactive_workspace_bg": "#32323200",
		"inactive_workspace_text": "#5c5c5cff",
		"active_workspace_border": "#333333ff",
		"active_workspace_bg": "#5f676aff",
		"active_workspace_text": "#ffffffff",
		"urgent_workspace_border": "#2f343aff",
		"urgent_workspace_bg": "#900000ff",
		"urgent_workspace_text": "#ffffffff",
		"binding_mode_border": "#2f343aff",
		"binding_mode_bg": "#900000ff",
		"binding_mode_text": "#ffffffff"
	},
}
```

## 7. GET_VERSION

*MESSAGE*++
Retrieve version information about the sway process

*REPLY*++
An object containing the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- major
:  integer
:[ The major version of the sway process
|- minor
:  integer
:  The minor version of the sway process
|- patch
:  integer
:  The patch version of the sway process
|- human_readable
:  string
:  A human readable version string that will likely contain more useful
   information such as the git commit short hash and git branch
|- loaded_config_file_name
:  string
:  The path to the loaded config file


*Example Reply:*
```
{
	"human_readable": "1.0-rc1-117-g2f7247e0 (Feb 24 2019, branch 'master')",
	"major": 1,
	"minor": 0,
	"patch": 0,
	"loaded_config_file_name": "/home/redsoxfan/.config/sway/config"
}
```

## 8. GET_BINDING_MODES

*MESSAGE*++
Retrieve the list of binding modes that currently configured

*REPLY*++
An array of strings, with each string being the name of a binding mode. This
will always contain at least one mode (currently _default_), which is the
default binding mode

*Example Reply:*
```
[
	"default",
	"resize",
]
```

## 9. GET_CONFIG

*MESSAGE*++
Retrieve the contents of the config that was last loaded

*REPLY*++
An object with a single string property containing the contents of the config

*Example Reply:*
```
{
	"config": "set $mod Mod4\nbindsym $mod+q exit\n"
}
```

## 10. SEND_TICK

*MESSAGE*++
Issues a _TICK_ event to all clients subscribing to the event to ensure that
all events prior to the tick were received. If a payload is given, it will be
included in the _TICK_ event

*REPLY*++
A single object contains the property _success_, which is a boolean value
indicating whether the _TICK_ event was sent.

*Example Reply:*
```
{
	"success": true
}
```

## 11. SYNC

*MESSAGE*++
For i3 compatibility, this command will just return a failure object since it
does not make sense to implement in sway due to the X11 nature of the command.
If you are curious about what this IPC command does in i3, refer to the i3
documentation.

*REPLY*++
A single object that contains the property _success_, which is set to the
boolean value _false_.

*Exact Reply:*
```
{
	"success": false
}
```

## 12. GET_BINDING_STATE

*MESSAGE*++
Returns the currently active binding mode.

*REPLY*++
A single object that contains the property _name_, which is set to the
currently active binding mode as a string.

*Exact Reply:*
```
{
	"name": "default"
}
```


## 100. GET_INPUTS

*MESSAGE*++
Retrieve a list of the input devices currently available

*REPLY*++
An array of objects corresponding to each input device. Each object has the
following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- identifier
:  string
:[ The identifier for the input device
|- name
:  string
:  The human readable name for the device
|- vendor
:  integer
:  The vendor code for the input device
|- product
:  integer
:  The product code for the input device
|- type
:  string
:  The device type. Currently this can be _keyboard_, _pointer_, _touch_,
   _tablet\_tool_, _tablet\_pad_, or _switch_
|- xkb_active_layout_name
:  string
:  (Only keyboards) The name of the active keyboard layout in use
|- xkb_layout_names
:  array
:  (Only keyboards) A list a layout names configured for the keyboard
|- xkb_active_layout_index
:  integer
:  (Only keyboards) The index of the active keyboard layout in use
|- scroll_factor
:  floating
:  (Only pointers) Multiplier applied on scroll event values.
|- sensitivity
:  floating
:  (Only pointers) Multiplier applied on relative pointer movements.
|- libinput
:  object
:  (Only libinput devices) An object describing the current device settings.
   See below for more information

The _libinput_ object describes the device configuration for libinput devices.
Only properties that are supported for the device will be added to the object.
In addition to the possible options listed, all string properties may also be
_unknown_, in the case that a new option is added to libinput. See
*sway-input*(5) for information on the meaning of the possible values. The
following properties will be included for devices that support them:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- send_events
:  string
:[ Whether events are being sent by the device. It can be _enabled_,
   _disabled_, or _disabled\_on\_external\_mouse_
|- tap
:  string
:  Whether tap to click is enabled. It can be _enabled_ or _disabled_
|- tap_button_map
:  string
:  The finger to button mapping in use. It can be _lmr_ or _lrm_
|- tap_drag
:  string
:  Whether tap-and-drag is enabled. It can be _enabled_ or _disabled_
|- tap_drag_lock
:  string
:  Whether drag-lock is enabled. It can be _enabled_ or _disabled_
|- accel_speed
:  double
:  The pointer-acceleration in use
|- accel_profile
:  string
:  The acceleration profile in use. It can be _none_, _flat_, or _adaptive_
|- natural_scroll
:  string
:  Whether natural scrolling is enabled. It can be _enabled_ or _disabled_
|- left_handed
:  string
:  Whether left-handed mode is enabled. It can be _enabled_ or _disabled_
|- click_method
:  string
:  The click method in use. It can be _none_, _button_areas_, or _clickfinger_
|- middle_emulation
:  string
:  Whether middle emulation is enabled. It can be _enabled_ or _disabled_
|- scroll_method
:  string
:  The scroll method in use. It can be _none_, _two_finger_, _edge_, or
   _on_button_down_
|- scroll_button
:  int
:  The scroll button to use when _scroll_method_ is _on_button_down_. This
   will be given as an input event code
|- dwt
:  string
:  Whether disable-while-typing is enabled. It can be _enabled_ or _disabled_
|- calibration_matrix
:  array
:  An array of 6 floats representing the calibration matrix for absolute
   devices such as touchscreens


*Example Reply:*
```
[
	{
		"identifier": "1:1:AT_Translated_Set_2_keyboard",
		"name": "AT Translated Set 2 keyboard",
		"vendor": 1,
		"product": 1,
		"type": "keyboard",
		"xkb_active_layout_name": "English (US)",
		"libinput": {
			"send_events": "enabled"
		}
	},
	{
		"identifier": "1267:5:Elan_Touchpad",
		"name": "Elan Touchpad",
		"vendor": 1267,
		"product": 5,
		"type": "pointer",
		"libinput": {
			"send_events": "enabled",
			"tap": "enabled",
			"tap_button_map": "lmr",
			"tap_drag": "enabled",
			"tap_drag_lock": "disabled",
			"accel_speed": 0.0,
			"accel_profile": "none",
			"natural_scroll", "disabled",
			"left_handed": "disabled",
			"click_method": "button_areas",
			"middle_emulation": "disabled",
			"scroll_method": "edge",
			"dwt": "enabled"
		}
	},
	{
		"identifier": "3034:22494:USB2.0_VGA_UVC_WebCam:_USB2.0_V",
		"name": "USB2.0 VGA UVC WebCam: USB2.0 V",
		"vendor": 3034,
		"product": 22494,
		"type": "keyboard",
		"xkb_active_layout_name": "English (US)",
		"libinput": {
			"send_events": "enabled"
		}
	},
	{
		"identifier": "0:3:Sleep_Button",
		"name": "Sleep Button",
		"vendor": 0,
		"product": 3,
		"type": "keyboard",
		"xkb_active_layout_name": "English (US)",
		"libinput": {
			"send_events": "enabled"
		}
	},
	{
		"identifier": "0:5:Lid_Switch",
		"name": "Lid Switch",
		"vendor": 0,
		"product": 5,
		"type": "switch",
		"libinput": {
			"send_events": "enabled"
		}
	},
	{
		"identifier": "0:6:Video_Bus",
		"name": "Video Bus",
		"vendor": 0,
		"product": 6,
		"type": "keyboard",
		"xkb_active_layout_name": "English (US)",
		"libinput": {
			"send_events": "enabled"
		}
	},
	{
		"identifier": "0:1:Power_Button",
		"name": "Power Button",
		"vendor": 0,
		"product": 1,
		"type": "keyboard",
		"xkb_active_layout_name": "English (US)",
		"libinput": {
			"send_events": "enabled"
		}
	}
]
```

## 101. GET_SEATS

*MESSAGE*++
Retrieve a list of the seats currently configured

*REPLY*++
An array of objects corresponding to each seat. There will always be at least
one seat. Each object has the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- name
:  string
:] The unique name for the seat
|- capabilities
:  integer
:  The number of capabilities that the seat has
|- focus
:  integer
:  The id of the node currently focused by the seat or _0_ when the seat is
   not currently focused by a node (i.e. a surface layer or xwayland unmanaged
   has focus)
|- devices
:  array
:  An array of input devices that are attached to the seat. Currently, this
   is an array of objects that are identical to those returned by _GET\_INPUTS_


*Example Reply:*
```
[
	{
		"name": "seat0",
		"capabilities": 3,
		"focus": 7,
		"devices": [
			{
				"identifier": "1:1:AT_Translated_Set_2_keyboard",
				"name": "AT Translated Set 2 keyboard",
				"vendor": 1,
				"product": 1,
				"type": "keyboard",
				"xkb_active_layout_name": "English (US)",
				"libinput": {
					"send_events": "enabled"
				}
			},
			{
				"identifier": "1267:5:Elan_Touchpad",
				"name": "Elan Touchpad",
				"vendor": 1267,
				"product": 5,
				"type": "pointer",
				"libinput": {
					"send_events": "enabled",
					"tap": "enabled",
					"tap_button_map": "lmr",
					"tap_drag": "enabled",
					"tap_drag_lock": "disabled",
					"accel_speed": 0.0,
					"accel_profile": "none",
					"natural_scroll", "disabled",
					"left_handed": "disabled",
					"click_method": "button_areas",
					"middle_emulation": "disabled",
					"scroll_method": "edge",
					"dwt": "enabled"
				}
			},
			{
				"identifier": "3034:22494:USB2.0_VGA_UVC_WebCam:_USB2.0_V",
				"name": "USB2.0 VGA UVC WebCam: USB2.0 V",
				"vendor": 3034,
				"product": 22494,
				"type": "keyboard",
				"xkb_active_layout_name": "English (US)",
				"libinput": {
					"send_events": "enabled"
				}
			},
			{
				"identifier": "0:3:Sleep_Button",
				"name": "Sleep Button",
				"vendor": 0,
				"product": 3,
				"type": "keyboard",
				"xkb_active_layout_name": "English (US)",
				"libinput": {
					"send_events": "enabled"
				}
			},
			{
				"identifier": "0:5:Lid_Switch",
				"name": "Lid Switch",
				"vendor": 0,
				"product": 5,
				"type": "switch",
				"libinput": {
					"send_events": "enabled"
				}
			},
			{
				"identifier": "0:6:Video_Bus",
				"name": "Video Bus",
				"vendor": 0,
				"product": 6,
				"type": "keyboard",
				"xkb_active_layout_name": "English (US)",
				"libinput": {
					"send_events": "enabled"
				}
			},
			{
				"identifier": "0:1:Power_Button",
				"name": "Power Button",
				"vendor": 0,
				"product": 1,
				"type": "keyboard",
				"xkb_active_layout_name": "English (US)",
				"libinput": {
					"send_events": "enabled"
				}
			}
		]
	}
]
```

# EVENTS

Events are a way for client to get notified of changes to sway. A client can
subscribe to any events it wants to be notified of changes for. The event is
sent in the same format as a reply. The following events are currently
available:

[- *EVENT TYPE*
:- *NAME*
:- *DESCRIPTION*
|- 0x80000000
:  workspace
:[ Sent whenever an event involving a workspace occurs such as initialization
   of a new workspace or a different workspace gains focus
|- 0x80000002
:  mode
:  Sent whenever the binding mode changes
|- 0x80000003
:  window
:  Sent whenever an event involving a view occurs such as being reparented,
   focused, or closed
|- 0x80000004
:  barconfig_update
:  Sent whenever a bar config changes
|- 0x80000005
:  binding
:  Sent when a configured binding is executed
|- 0x80000006
:  shutdown
:  Sent when the ipc shuts down because sway is exiting
|- 0x80000007
:  tick
:  Sent when an ipc client sends a _SEND\_TICK_ message
|- 0x80000014
:  bar_state_update
:  Send when the visibility of a bar should change due to a modifier
|- 0x80000015
:  input
:  Sent when something related to input devices changes


## 0x80000000. WORKSPACE

Sent whenever a change involving a workspace occurs. The event consists of a
single object with the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- change
:  string
:[ The type of change that occurred. See below for more information
|- current
:  object
:  An object representing the workspace effected or _null_ for _reload_ changes
|- old
:  object
:  For a _focus_ change, this is will be an object representing the workspace
   being switched from. Otherwise, it is _null_


The following change types are currently available:
[- *TYPE*
:- *DESCRIPTION*
|- init
:[ The workspace was created
|- empty
:  The workspace is empty and is being destroyed since it is not visible
|- focus
:  The workspace was focused. See the _old_ property for the previous focus
|- move
:  The workspace was moved to a different output
|- rename
:  The workspace was renamed
|- urgent
:  A view on the workspace has had their urgency hint set or all urgency hints
   for views on the workspace have been cleared
|- reload
:  The configuration file has been reloaded


*Example Event:*
```
{
	"change": "init",
	"old": null,
	"current": {
		"id": 10,
		"name": "2",
		"rect": {
			"x": 0,
			"y": 0,
			"width": 0,
			"height": 0
		},
		"focused": false,
		"focus": [
		],
		"border": "none",
		"current_border_width": 0,
		"layout": "splith",
		"percent": null,
		"window_rect": {
			"x": 0,
			"y": 0,
			"width": 0,
			"height": 0
		},
		"deco_rect": {
			"x": 0,
			"y": 0,
			"width": 0,
			"height": 0
		},
		"geometry": {
			"x": 0,
			"y": 0,
			"width": 0,
			"height": 0
		},
		"window": null,
		"urgent": false,
		"floating_nodes": [
		],
		"num": 2,
		"output": "eDP-1",
		"type": "workspace",
		"representation": null,
		"nodes": [
		]
	}
}
```

## 0x80000002. MODE

Sent whenever the binding mode changes. The event consists of a single object
with the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- change
:  string
:[ The binding mode that became active
|- pango_markup
:  boolean
:  Whether the mode should be parsed as pango markup


*Example Event:*
```
{
	"change": "default",
	"pango_markup": false
}
```

## 0x80000003. WINDOW

Sent whenever a change involving a view occurs. The event consists of a single
object with the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- change
:  string
:[ The type of change that occurred. See below for more information
|- container
:  object
:  An object representing the view effected


The following change types are currently available:
[- *TYPE*
:- *DESCRIPTION*
|- new
:[ The view was created
|- close
:  The view was closed
|- focus
:  The view was focused
|- title
:  The view's title has changed
|- fullscreen_mode
:  The view's fullscreen mode has changed
|- move
:  The view has been reparented in the tree
|- floating
:  The view has become floating or is no longer floating
|- urgent
:  The view's urgency hint has changed status
|- mark
:  A mark has been added or removed from the view


*Example Event:*
```
{
	"change": "new",
	"container": {
		"id": 12,
		"name": null,
		"rect": {
			"x": 0,
			"y": 0,
			"width": 0,
			"height": 0
		},
		"focused": false,
		"focus": [
		],
		"border": "none",
		"current_border_width": 0,
		"layout": "none",
		"percent": 0.0,
		"window_rect": {
			"x": 0,
			"y": 0,
			"width": 0,
			"height": 0
		},
		"deco_rect": {
			"x": 0,
			"y": 0,
			"width": 0,
			"height": 0
		},
		"geometry": {
			"x": 0,
			"y": 0,
			"width": 1124,
			"height": 422
		},
		"window": 4194313,
		"urgent": false,
		"floating_nodes": [
		],
		"type": "con",
		"pid": 19787,
		"app_id": null,
		"window_properties": {
			"class": "URxvt",
			"instance": "urxvt",
			"transient_for": null
		},
		"nodes": [
		]
	}
}
```

## 0x80000004. BARCONFIG_UPDATE

Sent whenever a config for a bar changes. The event is identical to that of
_GET_BAR_CONFIG_ when a bar ID is given as a payload. See _6. GET\_BAR\_CONFIG
(WITH A PAYLOAD)_ above for more information.

## 0x80000005. BINDING

Sent whenever a binding is executed. The event is a single object with the
following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- change
:  string
:[ The change that occurred for the binding. Currently this will only be _run_
|- command
:  string
:  The command associated with the binding
|- event_state_mask
:  array
:  An array of strings that correspond to each modifier key for the binding
|- input_code
:  integer
:  For keyboard bindcodes, this is the key code for the binding. For mouse
   bindings, this is the X11 button number, if there is an equivalent. In all
   other cases, this will be _0_.
|- symbol
:  string
:  For keyboard bindsyms, this is the bindsym for the binding. Otherwise, this
   will be _null_
|- input_type
:  string
:  The input type that triggered the binding. This is either _keyboard_ or
   _mouse_


*Example Event:*
```
{
	"change": "run",
	"binding": {
		"command": "workspace 2",
		"event_state_mask": [
			"Mod4"
		],
		"input_code": 0,
		"symbol": "2",
		"input_type": "keyboard"
	}
}
```

## 0x80000006. SHUTDOWN

Sent whenever the IPC is shutting down. The event is a single object with the
property _change_, which is a string containing the reason for the shutdown.
Currently, the only value for _change_ is _exit_, which is issued when sway is
exiting.

*Example Event:*
```
{
	"change": "exit"
}
```

## 0x80000007. TICK

Sent when first subscribing to tick events or by a _SEND\_TICK_ message. The
event is a single object with the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- first
:  boolean
:  Whether this event was triggered by subscribing to the tick events
|- payload
:  string
:  The payload given with a _SEND\_TICK_ message, if any. Otherwise, an empty
   string


*Example Event:*
```
{
	"first": true
	"payload": ""
}
```

## 0x80000014. BAR_STATE_UPDATE

Sent when the visibility of a bar changes due to a modifier being pressed. The
event is a single object with the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- id
:  string
:[ The bar ID effected
|- visible_by_modifier
:  boolean
:  Whether the bar should be made visible due to a modifier being pressed


*Example Event:*
```
{
	"id": "bar-0",
	"visible_by_modifier": true
}
```

## 0x80000015. INPUT

Sent when something related to the input devices changes. The event is a single
object with the following properties:

[- *PROPERTY*
:- *DATA TYPE*
:- *DESCRIPTION*
|- change
:  string
:[ What has changed
|- input
:  object
:  An object representing the input that is identical the ones GET_INPUTS gives

The following change types are currently available:
[- *TYPE*
:- *DESCRIPTION*
|- added
:[ The input device became available
|- removed
:  The input device is no longer available
|- xkb_keymap
:  (Keyboards only) The keymap for the keyboard has changed
|- xkb_layout
:  (Keyboards only) The effective layout in the keymap has changed
|- libinput_config
:  (libinput device only) A libinput config option for the device changed

*Example Event:*
```
{
	"change": "xkb_layout",
	"input": {
		"identifier": "1:1:AT_Translated_Set_2_keyboard",
		"name": "AT Translated Set 2 keyboard",
		"vendor": 1,
		"product": 1,
		"type": "keyboard",
		"xkb_layout_names": [
			"English (US)",
			"English (Dvorak)"
		],
		"xkb_active_layout_index": 1,
		"xkb_active_layout_name": "English (Dvorak)",
		"libinput": {
			"send_events": "enabled"
		}
	}
}
```

# SEE ALSO

*sway*(1) *sway*(5) *sway-bar*(5) *swaymsg*(1) *sway-input*(5) *sway-output*(5)
