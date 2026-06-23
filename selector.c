#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lister.h"
#include "xmalloc.h"
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

bool select_labels_with_command(struct zzz_list *labels, const struct zzz_list *store_index, char *const *argv, int argc) {
    int stdin_fds[2];
    int stdout_fds[2];
    // detecting error in the exec
    int error_fds[2];
    bool failed = false;
    char *err_str;

    if (pipe(stdin_fds) == -1
            || pipe(stdout_fds) == -1
            || pipe(error_fds) == -1
            || fcntl(error_fds[1], F_SETFD, fcntl(error_fds[1], F_GETFD) | FD_CLOEXEC)) {
        failed = true;
        err_str = strerror(errno);
        goto end;
    }

    pid_t pid = fork();
    switch (pid) {
    case -1:
        err_str = strerror(errno);
        failed = true;
        close(stdin_fds[0]);
        close(stdin_fds[1]);
        close(stdout_fds[0]);
        close(stdout_fds[1]);
        close(error_fds[0]);
        close(error_fds[1]);
        break;
    case 0:
        close(stdin_fds[1]);
        close(stdout_fds[0]);
        close(error_fds[0]);
        if (dup2(stdin_fds[0], STDIN_FILENO) == -1
                || dup2(stdout_fds[1], STDOUT_FILENO) == -1) {
            err_str = strerror(errno);
            failed = true;
        }
        close(stdin_fds[0]);
        close(stdout_fds[1]);
        if (failed) {
            close(error_fds[1]);
            break;
        }

        char **null_term_argv = xmalloc(sizeof(*null_term_argv) * (argc + 1));
        memcpy(null_term_argv, argv, sizeof(*null_term_argv) * argc);
        null_term_argv[argc] = NULL;
        execvp(null_term_argv[0], null_term_argv);
        write(error_fds[1], &errno, sizeof(errno));
        close(error_fds[1]);
        exit(EXIT_FAILURE);
        break;
    default:;
        close(stdin_fds[0]);
        close(stdout_fds[1]);
        close(error_fds[1]);
        // check if the exec succeeded
        int other_errno = 0;
        size_t bytes_read = 0;
        while (bytes_read < sizeof(other_errno)) {
            ssize_t status = read(error_fds[0], ((char *)&other_errno) + bytes_read, sizeof(other_errno) - bytes_read);
            if (status <= 0) break;
            bytes_read += status;
        }
        close(error_fds[0]);
        if (bytes_read == sizeof(other_errno)) {
            failed = true;
            err_str = strerror(other_errno);
            close(stdin_fds[1]);
            close(stdout_fds[0]);
            break;
        }

        FILE *stdin_file = fdopen(stdin_fds[1], "w");
        signal(SIGPIPE, SIG_IGN);
        if (!fprint_listing(stdin_file, store_index, false)) {
            failed = true;
            err_str = "failed to send data to selector";
        }
        signal(SIGPIPE, SIG_DFL);
        fclose(stdin_file);
        if (failed) {
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
            failed = true;
            err_str = strerror(errno);
            break;
        }
        numbuf[numbuf_len] = '\0';

        if (numbuf_len == 0) {
            failed = true;
            err_str = "nothing selected";
            break;
        }
        long idx;
        if (!parse_long(numbuf, &idx)) {
            failed = true;
            err_str = "failed when reading from selector program, is it configured to print the index?";
            break;
        }
        *labels = *(struct zzz_list *)zzz_list_by_idx(store_index, idx);
        break;
    }
end:
    if (failed) {
        fprintf(stderr, "selector failed: %s\n", err_str);
    }
    return !failed;
}
