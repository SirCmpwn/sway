lib_sway_common = static_library(
	'sway-common',
	files(
		'cairo.c',
		'ipc-client.c',
		'log.c',
		'list.c',
		'pango.c',
		'stringop.c',
		'util.c'
	),
	dependencies: [
		cairo,
		pango,
		pangocairo,
		wayland_client.partial_dependency(compile_args: true)
	],
	include_directories: sway_inc
)
