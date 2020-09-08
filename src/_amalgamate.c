// test to verify that compilation unit works
#define STDARC_C
#include "arc.h"
    #include "zip.c"
    #include "tar.c"
    #include "pak.c"
    #include "vfs.c"
    #include "dir.c"

// actual amalgamate app
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

int include( const char *file ) {
#ifdef _WIN32
    char cmd[256] = {0}, *cat = "type", *echoln = "echo.";
#else
    char cmd[256] = {0}, *cat = "cat", *echoln = "echo";
#endif
    struct stat st = {0};
    if( stat(file, &st) < 0 ) {
        snprintf( cmd, 256, "echo #include \"%s\" /* amalgamate: warning: cannot find file */ && %s", file, echoln );
        return system( cmd ), fprintf(stderr, "Warning: cannot find '%s'\n", file), -1;
    } else {
        snprintf( cmd, 256, "echo #line 1 \"src/%s\" && %s %s && %s", file, cat, file, echoln );
        return system( cmd );
    }
}

int main() {
    include("arc.h");
        include("zip.c");
        include("tar.c");
        include("pak.c");
        include("vfs.c");
        include("dir.c");
}
