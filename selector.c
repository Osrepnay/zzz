#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lister.h"
#include "storer.h"
#include "zzz_list.h"

static bool parse_long(const char *str, long *res) {
    char *str_end = NULL;
    long result = strtol(str, &str_end, 10);
    if (*str_end == '\0' || str_end == str) {
        return false;
    }
    *res = result;
    return true;
}

bool select_labels_with_command(char *const *argv, int argc, struct zzz_list *labels) {
    int stdin_fds[2];
    int stdout_fds[2];
    if (pipe(stdin_fds) == -1 || pipe(stdout_fds) == -1) {
        perror("pipe");
        return false;
    }
    pid_t pid = fork();
    bool failed = false;
    switch (pid) {
    case -1:
        perror("fork");
        failed = true;
        close(stdin_fds[0]);
        close(stdin_fds[1]);
        close(stdout_fds[0]);
        close(stdout_fds[1]);
        break;
    case 0:
        close(stdin_fds[1]);
        close(stdout_fds[0]);
        if (dup2(stdin_fds[0], STDIN_FILENO) == -1
                || dup2(stdout_fds[1], STDOUT_FILENO) == -1) {
            perror("dup");
            failed = true;
        }
        close(stdin_fds[0]);
        close(stdout_fds[1]);
        if (failed) break;

        char **null_term_argv = malloc(sizeof(*null_term_argv) * (argc + 1));
        memcpy(null_term_argv, argv, sizeof(*null_term_argv) * argc);
        null_term_argv[argc] = NULL;
        execvp(null_term_argv[0], null_term_argv);
        perror("exec");
        exit(EXIT_FAILURE);
        break;
    default:;
        close(stdin_fds[0]);
        close(stdout_fds[1]);
        FILE *file = fdopen(stdin_fds[1], "w");
        signal(SIGPIPE, SIG_IGN);
        if (!fprint_listing(file, false)) {
            failed = true;
        }
        signal(SIGPIPE, SIG_DFL);
        fclose(file);
        if (failed) {
            fputs("failed to send data to selector\n", stderr);
            close(stdout_fds[0]);
            break;
        }
        char numbuf[64];
        size_t numbuf_len = 0;
        ssize_t read_ct;
        while ((read_ct = read(stdout_fds[0], numbuf, 64)) > 0) {
            numbuf_len += read_ct;
        }
        close(stdout_fds[0]);
        if (read_ct < 0) {
            perror("read");
            failed = true;
            break;
        }
        numbuf[numbuf_len] = '\0';

        if (numbuf_len == 0) {
            fputs("nothing selected\n", stderr);
            failed = true;
            break;
        }
        long idx;
        if (!parse_long(numbuf, &idx)) {
            fputs("failed when reading from selector program, is it configured to print the index?\n", stderr);
            failed = true;
            break;
        }
        *labels = *(struct zzz_list *)zzz_list_by_idx(&storer_index, idx);
        break;
    }
    return !failed;
}
