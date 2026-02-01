#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "read_config.h"

char *config_path(void) {
    char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    char *filename = "/zzzclip";
    if (xdg_config_home == NULL) {
        char *home = getenv("HOME");
        if (home == NULL) {
            fputs("no $HOME, aborting\n", stderr);
            exit(EXIT_FAILURE);
        }
        char *config_dirname = "/.config";
        char *final = malloc(strlen(home) + strlen(config_dirname) + strlen(filename) + 1);
        strcat(final, home);
        strcat(final, config_dirname);
        strcat(final, filename);
        return final;
    } else {
        char *final = malloc(strlen(xdg_config_home) + strlen(filename) + 1);
        final[0] = '\0';
        strcat(final, xdg_config_home);
        strcat(final, filename);
        return final;
    }
}

struct mime_pref get_config(void) {
    char *path = config_path();
    int config_fd = open(path, O_RDWR);

    if (config_fd >= 0) {
        size_t chunk_size = 1024;
        size_t config_text_cap = chunk_size + 1;
        size_t config_text_len = 0;
        char *config_text = malloc(config_text_cap + 1);
        while (true) {
            if (config_text_len + chunk_size + 1 > config_text_cap) {
                config_text = realloc(config_text, config_text_cap *= 2);
            }
            ssize_t bytes_read = read(config_fd, config_text, chunk_size);
            config_text_len += bytes_read;
            if (bytes_read != (ssize_t)chunk_size) {
                break;
            }
        }
        close(config_fd);
        config_text[config_text_len] = '\0';

        struct mime_pref pref;
        if (parse_mime_prefs(config_text, &pref)) {
            return pref;
        } else {
            fputs("corrupt config file\n", stderr);
            exit(EXIT_FAILURE);
        }
    } else {
        // couldn't access read config file, use default
        char *default_text =
            "[(image/png image/jpeg image/*)"
            "(UTF8_STRING text/plain;charset=utf8 TEXT text/plain)]";
        struct mime_pref pref;
        assert(parse_mime_prefs(default_text, &pref));
        return pref;
    }
}
