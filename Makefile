# Compiler settings
CC = gcc
CFLAGS = -O3 -march=native -flto -Wall -Wextra -std=c99
LDFLAGS = -lmicrohttpd -lz

# Source files
SRC = server.c
OBJ = $(SRC:.c=.o)
EXEC = server

# Default target
all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

#debug
debug:
	$(CC) $(CFLAGS) -g server.c -o server_debug $(LDFLAGS)

test:
	$(CC) $(CFLAGS) test.c -o test $(LDFLAGS)

# nobuild executable
nobuild:
	$(CC) nobuild.c -o nobuild

# install dependency
install:
	chmod +x INSTALL
	./INSTALL

# Clean target
clean:
	rm -f $(OBJ) $(EXEC)
	rm -f nobuild
	rm -f test

.PHONY: all clean
