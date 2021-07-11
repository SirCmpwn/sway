grimshot(1)

# NAME

grimshot - a helper for screenshots within sway

# SYNOPSIS

*grimshot* [--notify] (copy|save) [TARGET] [FILE]++
*grimshot* check++
*grimshot* usage

# OPTIONS

*--notify*
	Show notifications to the user that a screenshot has been taken.

*save*
	Save the screenshot into a regular file. Grimshot will write images
	files to *XDG_SCREENSHOTS_DIR* if this is set (or defined
	in *user-dirs.dir*), or otherwise fall back to *XDG_PICTURES_DIR*.
	Set FILE to '-' to pipe the output to STDOUT.

*copy*
	Copy the screenshot data (as image/png) into the clipboard.

# DESCRIPTION

Grimshot is an easy-to-use screenshot utility for sway. It provides a
convenient interface over grim, slurp and jq, and supports storing the
screenshot either directly to the clipboard using wl-copy or to a file.

# EXAMPLES

An example usage pattern is to add these bindings to your sway config:

```
# Screenshots:
# Super+P: Current window
# Super+Shift+p: Select area
# Super+Alt+p Current output
# Super+Ctrl+p Select a window

bindsym Mod4+p       exec grimshot save active
bindsym Mod4+Shift+p exec grimshot save area
bindsym Mod4+Mod1+p  exec grimshot save output
bindsym Mod4+Ctrl+p  exec grimshot save window
```

# TARGETS

grimshot can capture the following named targets:

_active_
	Captures the currently active window.

_screen_
	Captures the entire screen. This includes all visible outputs.

_area_
	Allows manually selecting a rectangular region, and captures that.

_window_
	Allows manually selecting a single window (by clicking on it), and
	captures it.

_output_
	Captures the currently active output.

_selection_
	Allows manually selection a single window (by clicking on it), an output (by
	clicking outside of all windows, e.g. on the status bar), or an area (by
	using click and drag).

# OUTPUT

Grimshot will print the filename of the captured screenshot to stdout if called
with the _save_ subcommand.

# SEE ALSO

*grim*(1)
