#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-data-control-protocol.h"

void noop() {}

void device_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    (void) data;
    zwlr_data_control_device_v1_destroy(device);
}

struct zwlr_data_control_device_v1_listener device_listener = {
    .data_offer = &noop,
    .selection = &noop,
    .primary_selection = &noop,
    .finished = &device_finished,
};

struct device_info {
    struct wl_seat *seat;
    uint32_t seat_name;
    struct zwlr_data_control_manager_v1 *dcm;
    uint32_t dcm_name;
    struct zwlr_data_control_device_v1 *device;
};

void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct device_info *device_info = data;
    (void) version; // bad idea?
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        device_info->seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
    } else if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
        device_info->dcm = wl_registry_bind(registry, name, &zwlr_data_control_manager_v1_interface, version);
    }
    if (device_info->device == NULL && device_info->seat != NULL && device_info->dcm != NULL) {
        device_info->device = zwlr_data_control_manager_v1_get_data_device(device_info->dcm, device_info->seat);
        zwlr_data_control_device_v1_add_listener(device_info->device, &device_listener, NULL);
    }
}

void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {
    struct device_info *device_info = data;
    (void) registry;
    if (name == device_info->seat_name) {
        wl_seat_destroy(device_info->seat);
        device_info->seat = NULL;
        if (device_info->device != NULL) {
            zwlr_data_control_device_v1_destroy(device_info->device);
        }
    } else if (name == device_info->dcm_name) {
        zwlr_data_control_manager_v1_destroy(device_info->dcm);
        device_info->dcm = NULL;
        if (device_info->device != NULL) {
            zwlr_data_control_device_v1_destroy(device_info->device);
        }
    }
}

struct wl_registry_listener registry_listener = {
    .global = &registry_global,
    .global_remove = &registry_remove,
};

void send(void *data, struct zwlr_data_control_source_v1 *sauce, const char *mime_type, int32_t fd) {
    int *sauce_fd = data;
    (void) sauce;
    (void) mime_type; // only one offered
    // TODO get sendfile to work
    // sendfile(sauce_info->fd, fd, NULL, SIZE_MAX);
    off_t start_pos = lseek(*sauce_fd, 0, SEEK_CUR);
    while (true) {
        char buf[1024];
        ssize_t n = read(*sauce_fd, buf, sizeof buf);
        if (n <= 0) break;
        write(fd, buf, n);
    }
    close(fd);
    lseek(*sauce_fd, start_pos, SEEK_SET);
}

void cancelled(void *data, struct zwlr_data_control_source_v1 *sauce) {
    (void) data;
    (void) sauce;
    fputs("selection cancelled, exiting", stderr);
    exit(0);
}

struct zwlr_data_control_source_v1_listener sauce_listener = {
    .send = &send,
    .cancelled = &cancelled,
};

int main(int argv, char *argc[]) {
    if (argv != 2) {
        fprintf(stderr, "expected 1 argument (clipfile number), got %d\n", argv - 1);
        exit(1);
    }
    // find clip dir
    char *clip_filename = NULL;
    char *state_dir = getenv("XDG_STATE_HOME");
    if (state_dir == NULL) {
        fputs("$XDG_STATE_HOME not set, trying $HOME/.local/state\n", stderr);
        char *home = getenv("HOME");
        if (home == NULL) {
            fputs("$HOME not set, aborting\n", stderr);
            exit(2);
        }
        state_dir = malloc(strlen(home) + strlen("/.local/state") + 1);
        state_dir[0] = '\0';
        strcat(state_dir, home);
        strcat(state_dir, "/.local/state");
    }

    size_t state_dir_len = strlen(state_dir);
    char *zzz_dirname = "zzz_clip";
    clip_filename = malloc(state_dir_len + 1 + strlen(zzz_dirname) + 1 + strlen(argc[1]) + 1);
    clip_filename[0] = '\0';
    strcat(clip_filename, state_dir);
    strcat(clip_filename, "/");
    strcat(clip_filename, zzz_dirname);
    strcat(clip_filename, "/");
    strcat(clip_filename, argc[1]);

    int clipfile = open(clip_filename, O_RDONLY);
    if (errno) {
        perror(NULL);
    }

    char mime[256];
    int mime_len = 0;
    char c;
    while (read(clipfile, &c, 1) == 1 && c != '\n') mime[mime_len++] = c; // ooh dangerous!
    mime[mime_len] = '\0';

    int *heapfd = malloc(sizeof *heapfd);
    *heapfd = clipfile;

    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    struct device_info device_info = {
        .seat = NULL,
        .seat_name = 0,
        .dcm = NULL,
        .dcm_name = 0,
        .device = NULL
    };
    wl_registry_add_listener(registry, &registry_listener, &device_info);
    
    bool created = false;
    while (wl_display_dispatch(display) != -1) {
        if (device_info.device != NULL && !created) {
            created = true;
            struct zwlr_data_control_source_v1 *sauce =
                zwlr_data_control_manager_v1_create_data_source(device_info.dcm);
            zwlr_data_control_source_v1_offer(sauce, mime);
            zwlr_data_control_source_v1_add_listener(sauce, &sauce_listener, heapfd);
            zwlr_data_control_device_v1_set_selection(device_info.device, sauce);
        }
    }

    wl_display_disconnect(display);
    return 0;
}
