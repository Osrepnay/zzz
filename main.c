#include <dirent.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <sys/stat.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "wlr-data-control-protocol.h"

struct zzz_list {
    void *value;
    struct zzz_list *next;
};

struct zzz_list *zzz_list_singleton(void *value) {
    struct zzz_list *list = malloc(sizeof *list);
    *list = (struct zzz_list) {
        .value = value,
        .next = NULL,
    };
    return list;
}

void zzz_list_free(struct zzz_list *list) {
    while (list != NULL) {
        free(list->value);
        struct zzz_list *before = list;
        list = list->next;
        free(before);
    }
}

void zzz_list_prepend(struct zzz_list **list, void *value) {
    struct zzz_list *new_list = zzz_list_singleton(value);
    struct zzz_list *list_copy = malloc(sizeof *list_copy);
    list_copy = *list;
    new_list->next = list_copy;
    *list = new_list;
}

void zzz_list_reverse(struct zzz_list **list) {
    struct zzz_list *prev = NULL;
    while (*list != NULL) {
        struct zzz_list *real_next = (*list)->next;
        (*list)->next = prev;
        prev = *list;
        *list = real_next;
    }
    *list = prev;
}

void noop() {}

int clip_fd() {
    static long zzz_counter = -1;
    static char *zzz_dir = NULL;
    static size_t zzz_dir_len = 0;

    // if first run
    if (zzz_dir == NULL) {
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
        zzz_dir_len = state_dir_len + 1 + strlen(zzz_dirname) + 1;
        zzz_dir = malloc(zzz_dir_len + 1);
        zzz_dir[0] = '\0';
        strcat(zzz_dir, state_dir);
        zzz_dir[state_dir_len] = '/';
        zzz_dir[state_dir_len + 1] = '\0';
        strcat(zzz_dir, zzz_dirname);
        zzz_dir[zzz_dir_len - 1] = '/'; // trailng slash
        zzz_dir[zzz_dir_len] = '\0';

        // TODO no .local fails. why do you not have .local?
        mkdir(state_dir, S_IRUSR | S_IWUSR | S_IXUSR);
        mkdir(zzz_dir, S_IRUSR | S_IWUSR | S_IXUSR);

        DIR *zzz_dir_dir = opendir(zzz_dir);
        if (zzz_dir_dir == NULL) {
            fprintf(stderr, "directory %s could not be read or created, aborting", zzz_dir);
        }
        // find maximum clip number
        struct dirent *file;
        while ((file = readdir(zzz_dir_dir)) != NULL) {
            long num = strtol(file->d_name, NULL, 10);
            if (num > zzz_counter) {
                zzz_counter = num;
            }
        }
        ++zzz_counter;
    }

    char *zzz_file = malloc(zzz_dir_len + snprintf(NULL, 0, "%ld", zzz_counter) + 1);
    zzz_file[0] = '\0';
    strcpy(zzz_file, zzz_dir);
    sprintf(zzz_file + zzz_dir_len, "%ld", zzz_counter);
    ++zzz_counter;

    int zzz_fd = open(zzz_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    free(zzz_file);
    return zzz_fd;
}

struct zzz_list *mime_regexes() {
    char *config_dir = getenv("XDG_CONFIG_HOME");
    if (config_dir == NULL) {
        fputs("$XDG_CONFIG_HOME not set, trying $HOME/.config\n", stderr);
        char *home = getenv("HOME");
        if (home == NULL) {
            fputs("$HOME not set, aborting\n", stderr);
            exit(2);
        }
        config_dir = malloc(strlen(home) + strlen("/.config") + 1);
        config_dir[0] = '\0';
        strcat(config_dir, home);
        strcat(config_dir, "/.config");
    }
    char *config_filename = malloc(strlen(config_dir) + 1 + strlen("zzz_mimes") + 1);
    config_filename[0] = '\0';
    strcat(config_filename, config_dir);
    strcat(config_filename, "/zzz_mimes");

    FILE *config_file = fopen(config_filename, "r");
    if (config_file == NULL) {
        fprintf(stderr, "config file at %s does not exist, aborting\n", config_filename);
        exit(3);
    }
    struct zzz_list *pcre2_codes = NULL;
    int line = 0;
    int c = EOF + 1;
    while (c != EOF) {
        unsigned char *regex_str = malloc(32);
        size_t regex_str_len = 0;
        size_t regex_str_cap = 32;
        while ((c = fgetc(config_file)) != '\n' && c != EOF) {
            regex_str[regex_str_len++] = c;
            if (regex_str_len >= regex_str_cap) {
                regex_str = realloc(regex_str, regex_str_cap *= 2);
            }
        }
        // trailing newline(s)
        if (regex_str_len == 0) break;
        if (strcmp((char *) regex_str, "UNKNOWN") == 0) { // is casting safe?
            zzz_list_prepend(&pcre2_codes, NULL); // free(NULL) is safe til
        } else {
            int pcre2_err;
            size_t pcre2_offset;
            pcre2_code *regex_compiled = pcre2_compile(
                regex_str,
                regex_str_len,
                PCRE2_ANCHORED | PCRE2_CASELESS,
                &pcre2_err,
                &pcre2_offset,
                NULL
            );
            if (regex_compiled == NULL) {
                unsigned char pcre2_err_str[256]; // 256 is big enough right?
                fprintf(stderr, "PCRE compile error at line %d offset %zu: %s\n", line, pcre2_offset, pcre2_err_str);
                exit(3);
            }
            zzz_list_prepend(&pcre2_codes, regex_compiled);
        }
        ++line;
    }
    zzz_list_reverse(&pcre2_codes);
    fclose(config_file);
    return pcre2_codes;
}

struct wl_display *display;
struct zzz_list *config;

void offer_new_offer(void *data, struct zwlr_data_control_offer_v1 *offer, const char *mime) {
    struct zzz_list **current_mimes = data;
    (void) offer;
    char *mime_cpy = malloc(strlen(mime) + 1);
    strcpy(mime_cpy, mime);
    zzz_list_prepend(current_mimes, mime_cpy);
}

struct zwlr_data_control_offer_v1_listener offer_listener = {
    .offer = &offer_new_offer,
};

void device_offer(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    struct zzz_list **current_mimes = data;
    (void) device;
    zzz_list_free(*current_mimes);
    *current_mimes = NULL;
    zwlr_data_control_offer_v1_add_listener(offer, &offer_listener, data);
}

int mime_score(const char *mime) {
    struct zzz_list *config_node = config;
    int i = 0;
    int mime_i = -1;
    int unknown_i = INT_MAX; // deprioritize unknown if not found
    while (config_node != NULL) {
        pcre2_code *regex = config_node->value;
        if (regex == NULL) {
            unknown_i = i;
            ++i;
            config_node = config_node->next;
            continue;
        }
        pcre2_match_data *data = pcre2_match_data_create_from_pattern(regex, NULL); // stupid stupid stupid
        if (mime_i < 0) {
            int result = pcre2_match(
                regex,
                (const unsigned char *) mime,
                PCRE2_ZERO_TERMINATED,
                0,
                PCRE2_PARTIAL_SOFT,
                data, NULL
            );
            // success
            if (result > 0) {
                mime_i = i;
                break;
            }
        }
        ++i;
        config_node = config_node->next;
    }
    return mime_i < 0 ? unknown_i : mime_i;
}

void device_selection(void *data, struct zwlr_data_control_device_v1 *device, struct zwlr_data_control_offer_v1 *offer) {
    struct zzz_list **current_mimes = data;
    (void) device;
    if (offer != NULL) {
        zzz_list_reverse(current_mimes);
        struct zzz_list *current_mimes_node = *current_mimes;
        int chosen_mime_score = INT_MAX;
        char *chosen_mime = NULL;
        while (current_mimes_node != NULL) {
            int score = mime_score(current_mimes_node->value);
            if (score < chosen_mime_score) {
                chosen_mime_score = score;
                chosen_mime = current_mimes_node->value;
            }
            current_mimes_node = current_mimes_node->next;
        }
        if (chosen_mime == NULL) {
            fputs("no mimetypes were given for selection, ignoring\n", stderr);
        } else {
            int fd = clip_fd();
            write(fd, chosen_mime, strlen(chosen_mime));
            char newline = '\n';
            write(fd, &newline, 1);
            zwlr_data_control_offer_v1_receive(offer, chosen_mime, fd);
            close(fd);
            wl_display_roundtrip(display);
        }
    }
}

void device_finished(void *data, struct zwlr_data_control_device_v1 *device) {
    // TODO also clear devicein device_info; make global?
    struct zzz_list **current_mimes = data;
    zwlr_data_control_device_v1_destroy(device);
    zzz_list_free(*current_mimes);
    *current_mimes = NULL;
}

struct zwlr_data_control_device_v1_listener device_listener = {
    .data_offer = &device_offer,
    .selection = &device_selection,
    .primary_selection = &noop,
    .finished = &device_finished,
};

// data necessary to get a device
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
        zwlr_data_control_device_v1_add_listener(
            device_info->device,
            &device_listener,
            calloc(sizeof(struct zzz_list *), 1)
        );
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

int main(void) {
    config = mime_regexes();
    display = wl_display_connect(NULL);
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
    
    while (wl_display_dispatch(display) != -1);

    wl_display_disconnect(display);
    return 0;
}
