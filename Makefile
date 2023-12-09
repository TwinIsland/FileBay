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

# nobuild executable
nobuild:
	$(CC) nobuild.c -o nobuild

# Clean target
clean:
	rm -f $(OBJ) $(EXEC)
	rm -f nobuild

.PHONY: all clean
