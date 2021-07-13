#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if !defined(__linux__)
#error This program uses Linux-specific sysfs features
#endif

/* Simple sway status_command for laptops. Prints the date and the current
 * battery status. Warns using swaynag when the battery drops to an unreasonable
 * level. For every percentage point demise below that level, renags.
 *
 * Should compile without arguments with any reasonable Linux compiler.
 * It should be easy to modify the battery status handling code for other
 * systems
 *
 * example installation and configuration:
 *      cc laptop_status.c -o ~/.local/bin/laptop-status
 *      sed -i 's/status_command.+/status_command ~/.local/bin/laptop-status' \
 *          ~/.config/sway/config'
 */

static const char *cap_file = "/sys/class/power_supply/BAT0/capacity";
static const char *stat_file = "/sys/class/power_supply/BAT0/status";
static const int bat_warn_below = 10;

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    int status = open(stat_file, O_RDONLY);
    int cap = open(cap_file, O_RDONLY);
    if (cap < 0 || status < 0) {
        fprintf(stderr, "no battery found\n");
        exit(EXIT_FAILURE);
    }

    char timestamp[128] = {0}, bat_status[32] = {0}, bat_capacity[32] = {0};

    int nagged = 0;
    pid_t nag = 0;
    for (;;) {
        pread(cap, bat_capacity, sizeof bat_capacity - 1, 0);
        pread(status, bat_status, sizeof bat_status - 1, 0);
        bool discharging = !!strstr(bat_status, "Discharging");
        int percent = (int)strtol(bat_capacity, NULL, 10);
        if (discharging && percent < bat_warn_below) {
            if (!nagged || percent < nagged) {
                char msg[64];
                snprintf(msg, sizeof msg, "battery is low (%d%%)", percent);
                char *cmd[] = {"swaynag", "-m", msg, NULL};
                if (!(nag = fork())) {
                    execvp(cmd[0], cmd);
                }
            }
            nagged = percent;
        } else if (nag) {
            kill(nag, SIGINT);
            nag = 0;
        }
        time_t now = time(NULL);
        strftime(timestamp, sizeof timestamp - 1, "%c", localtime(&now));
        printf(
            "%s [% 3d%%]%s\n",
            timestamp,
            percent,
            strstr(bat_status, "Discharging") ? " " : "+"
        );
        sleep(1);
    }
}
