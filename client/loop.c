#define _POSIX_C_SOURCE 200112L
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include "list.h"
#include "log.h"
#include "loop.h"

struct loop_fd_event {
	void (*callback)(int fd, short mask, void *data);
	void *data;
};

struct loop_timer {
	void (*callback)(void *data);
	void *data;
	struct timespec expiry;
};

struct loop {
	struct pollfd *fds;
	int fd_length;
	int fd_capacity;

	list_t *fd_events; // struct loop_fd_event
	list_t *timers; // struct loop_timer
};

struct loop *loop_create(void) {
	struct loop *loop = calloc(1, sizeof(struct loop));
	if (!loop) {
		sway_log(SWAY_ERROR, "Unable to allocate memory for loop");
		return NULL;
	}
	loop->fd_capacity = 10;
	loop->fds = malloc(sizeof(struct pollfd) * loop->fd_capacity);
	loop->fd_events = create_list();
	loop->timers = create_list();
	return loop;
}

void loop_destroy(struct loop *loop) {
	list_free_items_and_destroy(loop->fd_events);
	list_free_items_and_destroy(loop->timers);
	free(loop->fds);
	free(loop);
}

void loop_poll(struct loop *loop) {
	// Calculate next timer in ms
	int ms = INT_MAX;
	if (loop->timers->length) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		for (int i = 0; i < loop->timers->length; ++i) {
			struct loop_timer *timer = loop->timers->items[i];
			int timer_ms = (timer->expiry.tv_sec - now.tv_sec) * 1000;
			timer_ms += (timer->expiry.tv_nsec - now.tv_nsec) / 1000000;
			if (timer_ms < ms) {
				ms = timer_ms;
			}
		}
	}
	if (ms < 0) {
		ms = 0;
	}

	poll(loop->fds, loop->fd_length, ms);

	// Dispatch fds
	for (int i = 0; i < loop->fd_length; ++i) {
		struct pollfd pfd = loop->fds[i];
		struct loop_fd_event *event = loop->fd_events->items[i];

		// Always send these events
		unsigned events = pfd.events | POLLHUP | POLLERR;

		if (pfd.revents & events) {
			event->callback(pfd.fd, pfd.revents, event->data);
		}
	}

	// Dispatch timers
	if (loop->timers->length) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		for (int i = 0; i < loop->timers->length; ++i) {
			struct loop_timer *timer = loop->timers->items[i];
			bool expired = timer->expiry.tv_sec < now.tv_sec ||
				(timer->expiry.tv_sec == now.tv_sec &&
				 timer->expiry.tv_nsec < now.tv_nsec);
			if (expired) {
				timer->callback(timer->data);
				loop_remove_timer(loop, timer);
				--i;
			}
		}
	}
}

void loop_add_fd(struct loop *loop, int fd, short mask,
		void (*callback)(int fd, short mask, void *data), void *data) {
	struct loop_fd_event *event = calloc(1, sizeof(struct loop_fd_event));
	if (!event) {
		sway_log(SWAY_ERROR, "Unable to allocate memory for event");
		return;
	}
	event->callback = callback;
	event->data = data;
	list_add(loop->fd_events, event);

	struct pollfd pfd = {fd, mask, 0};

	if (loop->fd_length == loop->fd_capacity) {
		int capacity = loop->fd_capacity + 10;
		struct pollfd *tmp = realloc(loop->fds,
				sizeof(struct pollfd) * capacity);
		if (!tmp) {
			sway_log(SWAY_ERROR, "Unable to allocate memory for pollfd");
			return;
		}
		loop->fds = tmp;
		loop->fd_capacity = capacity;
	}

	loop->fds[loop->fd_length++] = pfd;
}

struct loop_timer *loop_add_timer(struct loop *loop, int ms,
		void (*callback)(void *data), void *data) {
	struct loop_timer *timer = calloc(1, sizeof(struct loop_timer));
	if (!timer) {
		sway_log(SWAY_ERROR, "Unable to allocate memory for timer");
		return NULL;
	}
	timer->callback = callback;
	timer->data = data;

	clock_gettime(CLOCK_MONOTONIC, &timer->expiry);
	timer->expiry.tv_sec += ms / 1000;

	long int nsec = (ms % 1000) * 1000000;
	if (timer->expiry.tv_nsec + nsec >= 1000000000) {
		timer->expiry.tv_sec++;
		nsec -= 1000000000;
	}
	timer->expiry.tv_nsec += nsec;

	list_add(loop->timers, timer);

	return timer;
}

bool loop_remove_fd(struct loop *loop, int fd) {
	for (int i = 0; i < loop->fd_length; ++i) {
		if (loop->fds[i].fd == fd) {
			free(loop->fd_events->items[i]);
			list_del(loop->fd_events, i);

			loop->fd_length--;
			memmove(&loop->fds[i], &loop->fds[i + 1],
					sizeof(struct pollfd) * (loop->fd_length - i));

			return true;
		}
	}
	return false;
}

bool loop_remove_timer(struct loop *loop, struct loop_timer *timer) {
	for (int i = 0; i < loop->timers->length; ++i) {
		if (loop->timers->items[i] == timer) {
			list_del(loop->timers, i);
			free(timer);
			return true;
		}
	}
	return false;
}
