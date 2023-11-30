# Compiler settings
CC = gcc
CFLAGS = -O3 -march=native -flto
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

# Clean target
clean:
	rm -f $(OBJ) $(EXEC)

.PHONY: all clean
