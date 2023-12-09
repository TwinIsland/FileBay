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

    switch (argc)
    {
    case 1:
        build_server();
        break;

    case 2:
        if (ENDS_WITH(argv[1], "clean"))
        {
            RM("server");
            RM("server.o");
        } else {
            goto print_usgae;
        }
        break;
    default:
print_usgae:
        printf("usage: %s <clean>\n", argv[0]);
    }

    return 0;
}