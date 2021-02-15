#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/security.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "list.h"
#include "log.h"

struct cmd_results *cmd_security_label(int argc, char **argv) {
	int i;
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "security_label", EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	char* op = argv[0];
	char* who = argv[1];
	uint32_t privs = 0;
	enum { SET, PERMIT, DENY } mode;
	if (strcmp(op, "set") == 0) {
		mode = SET;
	} else if (strcmp(op, "permit") == 0) {
		mode = PERMIT;
	} else if (strcmp(op, "deny") == 0) {
		mode = DENY;
	} else {
		return cmd_results_new(CMD_INVALID, "Invalid operation: %s", op);
	}

	for(i = 2; i < argc; i++) {
		char* arg = argv[i];
		enum security_perm priv = security_get_perm_by_name(arg);
		if (priv != PRIV_LAST)
			privs |= 1 << priv;
		else if (strcmp(arg, "*") == 0)
			privs |= ~0;
		else
			return cmd_results_new(CMD_INVALID, "Unknown permission: %s", arg);
	}

	if (!config->security_configs)
		config->security_configs = create_list();

	for (i = 0; i < config->security_configs->length; i++) {
		struct security_config *cfg = config->security_configs->items[i];
		if (strcmp(cfg->name, who) == 0)
			break;
	}
	if (i == config->security_configs->length) {
		struct security_config *cfg = calloc(1, sizeof(*cfg));
		cfg->name = strdup(who);
		list_add(config->security_configs, cfg);
	}

	struct security_config *cfg = config->security_configs->items[i];
	switch (mode) {
	case SET:
		cfg->permitted = privs;
		break;
	case PERMIT:
		cfg->permitted |= privs;
		break;
	case DENY:
		cfg->permitted &= ~privs;
		break;
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
