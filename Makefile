CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -g -std=c11 -D_POSIX_C_SOURCE=200809L

TARGET  = mysh
SRCS    = mysh.c parse.c execute.c builtins.c wildcard.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c mysh.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
