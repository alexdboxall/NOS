
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>

char* getcwd(char* buf, size_t size) {
    (void) buf;
    (void) size;
    return NULL;
}