#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <pango/pangocairo.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <wlr/version.h>
#include "sway/commands.h"
#include "sway/config.h"
#include "sway/server.h"
#include "sway/swaynag.h"
#include "sway/desktop/transaction.h"
#include "sway/tree/root.h"
#include "sway/ipc-server.h"
#include "ipc-client.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

static bool terminate_request = false;
static int exit_value = 0;
struct sway_server server = {0};
struct sway_debug debug = {0};

void sway_terminate(int exit_code) {
	if (!server.wl_display) {
		// Running as IPC client
		exit(exit_code);
	} else {
		// Running as server
		terminate_request = true;
		exit_value = exit_code;
		ipc_event_shutdown("exit");
		wl_display_terminate(server.wl_display);
	}
}

void sig_handler(int signal) {
	sway_terminate(EXIT_SUCCESS);
}

void detect_raspi(void) {
	bool raspi = false;
	FILE *f = fopen("/sys/firmware/devicetree/base/model", "r");
	if (!f) {
		return;
	}
	char *line = NULL;
	size_t line_size = 0;
	while (getline(&line, &line_size, f) != -1) {
		if (strstr(line, "Raspberry Pi")) {
			raspi = true;
			break;
		}
	}
	fclose(f);
	FILE *g = fopen("/proc/modules", "r");
	if (!g) {
		free(line);
		return;
	}
	bool vc4 = false;
	while (getline(&line, &line_size, g) != -1) {
		if (strstr(line, "vc4")) {
			vc4 = true;
			break;
		}
	}
	free(line);
	fclose(g);
	if (!vc4 && raspi) {
		fprintf(stderr, "\x1B[1;31mWarning: You have a "
				"Raspberry Pi, but the vc4 Module is "
				"not loaded! Set 'dtoverlay=vc4-kms-v3d'"
				"in /boot/config.txt and reboot.\x1B[0m\n");
	}
}

void detect_proprietary(int allow_unsupported_gpu) {
	FILE *f = fopen("/proc/modules", "r");
	if (!f) {
		return;
	}
	char *line = NULL;
	size_t line_size = 0;
	while (getline(&line, &line_size, f) != -1) {
		if (strncmp(line, "nvidia ", 7) == 0) {
			if (allow_unsupported_gpu) {
				sway_log(SWAY_ERROR,
						"!!! Proprietary Nvidia drivers are in use !!!");
			} else {
				sway_log(SWAY_ERROR,
					"Proprietary Nvidia drivers are NOT supported. "
					"Use Nouveau. To launch sway anyway, launch with "
					"--my-next-gpu-wont-be-nvidia and DO NOT report issues.");
				exit(EXIT_FAILURE);
			}
			break;
		}
		if (strstr(line, "fglrx")) {
			if (allow_unsupported_gpu) {
				sway_log(SWAY_ERROR,
						"!!! Proprietary AMD drivers are in use !!!");
			} else {
				sway_log(SWAY_ERROR, "Proprietary AMD drivers do NOT support "
					"Wayland. Use radeon. To try anyway, launch sway with "
					"--unsupported-gpu and DO NOT report issues.");
				exit(EXIT_FAILURE);
			}
			break;
		}
	}
	free(line);
	fclose(f);
}

void run_as_ipc_client(char *command, char *socket_path) {
	int socketfd = ipc_open_socket(socket_path);
	uint32_t len = strlen(command);
	char *resp = ipc_single_command(socketfd, IPC_COMMAND, command, &len);
	printf("%s\n", resp);
	free(resp);
	close(socketfd);
}

static void log_env(void) {
	const char *log_vars[] = {
		"LD_LIBRARY_PATH",
		"LD_PRELOAD",
		"PATH",
		"SWAYSOCK",
	};
	for (size_t i = 0; i < sizeof(log_vars) / sizeof(char *); ++i) {
		char *value = getenv(log_vars[i]);
		sway_log(SWAY_INFO, "%s=%s", log_vars[i], value != NULL ? value : "");
	}
}

static void log_file(FILE *f) {
	char *line = NULL;
	size_t line_size = 0;
	ssize_t nread;
	while ((nread = getline(&line, &line_size, f)) != -1) {
		if (line[nread - 1] == '\n') {
			line[nread - 1] = '\0';
		}
		sway_log(SWAY_INFO, "%s", line);
	}
	free(line);
}

static void log_distro(void) {
	const char *paths[] = {
		"/etc/lsb-release",
		"/etc/os-release",
		"/etc/debian_version",
		"/etc/redhat-release",
		"/etc/gentoo-release",
	};
	for (size_t i = 0; i < sizeof(paths) / sizeof(char *); ++i) {
		FILE *f = fopen(paths[i], "r");
		if (f) {
			sway_log(SWAY_INFO, "Contents of %s:", paths[i]);
			log_file(f);
			fclose(f);
		}
	}
}

static void log_kernel(void) {
	FILE *f = popen("uname -a", "r");
	if (!f) {
		sway_log(SWAY_INFO, "Unable to determine kernel version");
		return;
	}
	log_file(f);
	pclose(f);
}


static bool drop_permissions(void) {
	if (getuid() != geteuid() || getgid() != getegid()) {
		// Set the gid and uid in the correct order.
		if (setgid(getgid()) != 0) {
			sway_log(SWAY_ERROR, "Unable to drop root group, refusing to start");
			return false;
		}
		if (setuid(getuid()) != 0) {
			sway_log(SWAY_ERROR, "Unable to drop root user, refusing to start");
			return false;
		}
	}
	if (setgid(0) != -1 || setuid(0) != -1) {
		sway_log(SWAY_ERROR, "Unable to drop root (we shouldn't be able to "
			"restore it after setuid), refusing to start");
		return false;
	}
	return true;
}

void enable_debug_flag(const char *flag) {
	if (strcmp(flag, "damage=highlight") == 0) {
		debug.damage = DAMAGE_HIGHLIGHT;
	} else if (strcmp(flag, "damage=rerender") == 0) {
		debug.damage = DAMAGE_RERENDER;
	} else if (strcmp(flag, "noatomic") == 0) {
		debug.noatomic = true;
	} else if (strcmp(flag, "txn-wait") == 0) {
		debug.txn_wait = true;
	} else if (strcmp(flag, "txn-timings") == 0) {
		debug.txn_timings = true;
	} else if (strncmp(flag, "txn-timeout=", 12) == 0) {
		server.txn_timeout_ms = atoi(&flag[12]);
	} else {
		sway_log(SWAY_ERROR, "Unknown debug flag: %s", flag);
	}
}

static sway_log_importance_t convert_wlr_log_importance(
		enum wlr_log_importance importance) {
	switch (importance) {
	case WLR_ERROR:
		return SWAY_ERROR;
	case WLR_INFO:
		return SWAY_INFO;
	default:
		return SWAY_DEBUG;
	}
}

static void handle_wlr_log(enum wlr_log_importance importance,
		const char *fmt, va_list args) {
	static char sway_fmt[1024];
	snprintf(sway_fmt, sizeof(sway_fmt), "[wlr] %s", fmt);
	_sway_vlog(convert_wlr_log_importance(importance), sway_fmt, args);
}

int main(int argc, char **argv) {
	static int verbose = 0, debug = 0, validate = 0, allow_unsupported_gpu = 0;

	static const struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"config", required_argument, NULL, 'c'},
		{"validate", no_argument, NULL, 'C'},
		{"debug", no_argument, NULL, 'd'},
		{"version", no_argument, NULL, 'v'},
		{"verbose", no_argument, NULL, 'V'},
		{"get-socketpath", no_argument, NULL, 'p'},
		{"unsupported-gpu", no_argument, NULL, 'u'},
		{"my-next-gpu-wont-be-nvidia", no_argument, NULL, 'u'},
		{0, 0, 0, 0}
	};

	char *config_path = NULL;

	const char* usage =
		"Usage: sway [options] [command]\n"
		"\n"
		"  -h, --help             Show help message and quit.\n"
		"  -c, --config <config>  Specify a config file.\n"
		"  -C, --validate         Check the validity of the config file, then exit.\n"
		"  -d, --debug            Enables full logging, including debug information.\n"
		"  -v, --version          Show the version number and quit.\n"
		"  -V, --verbose          Enables more verbose logging.\n"
		"      --get-socketpath   Gets the IPC socket path and prints it, then exits.\n"
		"\n";

	int c;
	while (1) {
		int option_index = 0;
		c = getopt_long(argc, argv, "hCdD:vVc:", long_options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'h': // help
			printf("%s", usage);
			exit(EXIT_SUCCESS);
			break;
		case 'c': // config
			free(config_path);
			config_path = strdup(optarg);
			break;
		case 'C': // validate
			validate = 1;
			break;
		case 'd': // debug
			debug = 1;
			break;
		case 'D': // extended debug options
			enable_debug_flag(optarg);
			break;
		case 'u':
			allow_unsupported_gpu = 1;
			break;
		case 'v': // version
			printf("sway version " SWAY_VERSION "\n");
			exit(EXIT_SUCCESS);
			break;
		case 'V': // verbose
			verbose = 1;
			break;
		case 'p': ; // --get-socketpath
			if (getenv("SWAYSOCK")) {
				printf("%s\n", getenv("SWAYSOCK"));
				exit(EXIT_SUCCESS);
			} else {
				fprintf(stderr, "sway socket not detected.\n");
				exit(EXIT_FAILURE);
			}
			break;
		default:
			fprintf(stderr, "%s", usage);
			exit(EXIT_FAILURE);
		}
	}

	// Since wayland requires XDG_RUNTIME_DIR to be set, abort with just the
	// clear error message (when not running as an IPC client).
	if (!getenv("XDG_RUNTIME_DIR") && optind == argc) {
		fprintf(stderr,
				"XDG_RUNTIME_DIR is not set in the environment. Aborting.\n");
		exit(EXIT_FAILURE);
	}

	// As the 'callback' function for wlr_log is equivalent to that for
	// sway, we do not need to override it.
	if (debug) {
		sway_log_init(SWAY_DEBUG, sway_terminate);
		wlr_log_init(WLR_DEBUG, handle_wlr_log);
	} else if (verbose) {
		sway_log_init(SWAY_INFO, sway_terminate);
		wlr_log_init(WLR_INFO, handle_wlr_log);
	} else {
		sway_log_init(SWAY_ERROR, sway_terminate);
		wlr_log_init(WLR_ERROR, handle_wlr_log);
	}

	sway_log(SWAY_INFO, "Sway version " SWAY_VERSION);
	sway_log(SWAY_INFO, "wlroots version " WLR_VERSION_STR);
	log_kernel();
	log_distro();
	log_env();
	detect_proprietary(allow_unsupported_gpu);
	detect_raspi();

	if (optind < argc) { // Behave as IPC client
		if (optind != 1) {
			sway_log(SWAY_ERROR,
					"Detected both options and positional arguments. If you "
					"are trying to use the IPC client, options are not "
					"supported. Otherwise, check the provided arguments for "
					"issues. See `man 1 sway` or `sway -h` for usage. If you "
					"are trying to generate a debug log, use "
					"`sway -d 2>sway.log`.");
			exit(EXIT_FAILURE);
		}
		if (!drop_permissions()) {
			exit(EXIT_FAILURE);
		}
		char *socket_path = getenv("SWAYSOCK");
		if (!socket_path) {
			sway_log(SWAY_ERROR, "Unable to retrieve socket path");
			exit(EXIT_FAILURE);
		}
		char *command = join_args(argv + optind, argc - optind);
		run_as_ipc_client(command, socket_path);
		free(command);
		return 0;
	}

	if (!server_privileged_prepare(&server)) {
		return 1;
	}

	if (!drop_permissions()) {
		server_fini(&server);
		exit(EXIT_FAILURE);
	}

	// handle SIGTERM signals
	signal(SIGTERM, sig_handler);
	signal(SIGINT, sig_handler);

	// prevent ipc from crashing sway
	signal(SIGPIPE, SIG_IGN);

	sway_log(SWAY_INFO, "Starting sway version " SWAY_VERSION);

	root = root_create();

	if (!server_init(&server)) {
		return 1;
	}

	if (validate) {
		bool valid = load_main_config(config_path, false, true);
		free(config_path);
		return valid ? 0 : 1;
	}

	ipc_init(&server);

	setenv("WAYLAND_DISPLAY", server.socket, true);
	if (!load_main_config(config_path, false, false)) {
		sway_terminate(EXIT_FAILURE);
		goto shutdown;
	}

	if (!server_start(&server)) {
		sway_terminate(EXIT_FAILURE);
		goto shutdown;
	}

	config->active = true;
	load_swaybars();
	run_deferred_commands();
	run_deferred_bindings();
	transaction_commit_dirty();

	if (config->swaynag_config_errors.client != NULL) {
		swaynag_show(&config->swaynag_config_errors);
	}

	server_run(&server);

shutdown:
	sway_log(SWAY_INFO, "Shutting down sway");

	server_fini(&server);
	root_destroy(root);
	root = NULL;

	free(config_path);
	free_config(config);

	pango_cairo_font_map_set_default(NULL);

	return exit_value;
}
