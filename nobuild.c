#define NOBUILD_IMPLEMENTATION
#include "./nobuild.h"

#define CFLAGS "-Wall", "-Wextra", "-std=c99", "-pedantic", "-O3"
#define LDFLAGS "-lmicrohttpd", "-lz"

void build_server()
{
    Cstr tool_path = "server.c";
    INFO("Start building server.c");

#ifndef _WIN32
    CMD("cc", CFLAGS, "-o", NOEXT(tool_path), tool_path, LDFLAGS);
#else
    CMD("cl.exe", CFLAGS, "-o", NOEXT(tool_path), tool_path, LDFLAGS);
#endif
    INFO("End building server.c");
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);
    
    build_server();

    return 0;
}