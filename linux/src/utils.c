#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include "utils.h"

char* expand_path(const char* path) {
    if (path[0] != '~') {
        return strdup(path);
    }

    const char* home;
    if (path[1] == '/' || path[1] == '\0') {
        // ~/path or ~
        home = getenv("HOME");
        if (!home) {
            struct passwd* pwd = getpwuid(getuid());
            if (pwd) {
                home = pwd->pw_dir;
            }
        }
    } else {
        // ~user/path
        char username[256];
        const char* slash = strchr(path, '/');
        size_t len = slash ? slash - path - 1 : strlen(path) - 1;
        strncpy(username, path + 1, len);
        username[len] = '\0';
        struct passwd* pwd = getpwnam(username);
        home = pwd ? pwd->pw_dir : NULL;
    }

    if (!home) {
        return NULL;
    }

    const char* rest = strchr(path, '/');
    if (!rest) {
        rest = path + strlen(path);
    }

    size_t result_len = strlen(home) + strlen(rest) + 1;
    char* result = malloc(result_len);
    if (!result) {
        return NULL;
    }

    snprintf(result, result_len, "%s%s", home, rest);
    return result;
} 