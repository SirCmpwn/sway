lib_sway_client = static_library(
	'sway-client',
	files(
		'background-image.c',
		'loop.c',
		'pool-buffer.c',
	),
	dependencies: [
		cairo,
		pango,
		pangocairo,
		wayland_client,
		gdk_pixbuf,
	],
	link_with: [lib_sway_common],
	include_directories: sway_inc
)
