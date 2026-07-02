#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// yes this is the only thing here but it's the only place i could think to put it
void mkdirp(char *dir) {
    if (dir[0] == '\0') return;
    size_t last_slash_idx = 1;
    do {
        while(dir[last_slash_idx] != '\0' && dir[last_slash_idx] != '/') {
            last_slash_idx++;
        }
        char old_char = dir[last_slash_idx];
        dir[last_slash_idx] = '\0';
        if (access(dir, F_OK) != 0) {
            if (mkdir(dir, S_IRWXU) != 0) {
                fputs("could not create directory, aborting\n", stderr);
                exit(EXIT_FAILURE);
            }
        }
        dir[last_slash_idx] = old_char;
    } while (dir[last_slash_idx++] != '\0');
}

