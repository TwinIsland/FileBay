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

void install_dependency()
{
    Cstr tool_path = "./INSTALL";
    INFO("try installing dependency");

#ifndef _WIN32
    CMD("chmod", "+x", tool_path);
    CMD(tool_path);
#else
    ERRO("not support auto install script for Windows, please install dependecy manually");
#endif
}

int main(int argc, char **argv)
{
    GO_REBUILD_URSELF(argc, argv);

    switch (argc)
    {
    case 1:
        goto print_usgae;

    case 2:
        if (ENDS_WITH(argv[1], "clean"))
        {
            RM("server");
            RM("server.o");
        }
        else if (ENDS_WITH(argv[1], "install"))
        {
            install_dependency();
        }
        else if (ENDS_WITH(argv[1], "build"))
        {
            build_server();
        }
        else
            goto print_usgae;

        break;
    default:
    print_usgae:
        printf("Usage: %s <command>\nCommands:\n \
        build         - Build server executable\n \
        install       - Install dependencies\n \
        clean         - Clean build files\n",
               argv[0]);
    }

    return 0;
}