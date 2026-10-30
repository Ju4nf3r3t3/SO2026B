CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE -pthread
LDFLAGS = -pthread
ARCHSALIDA = eafitOS
SRCS = main.c cat_datos.c cat_memoria.c cat_monitoreo.c cat_util.c editor.c huffman.c compresor.c
OBJS = $(SRCS:.c=.o)

# 'all' solo compila; para ejecutar el shell usar 'make run'
all: $(ARCHSALIDA)

$(ARCHSALIDA): $(OBJS)
	$(CC) $(CFLAGS) -o $(ARCHSALIDA) $(OBJS) $(LDFLAGS)

%.o: %.c shell.h huffman.h compresor.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(ARCHSALIDA)
	./$(ARCHSALIDA)

clean:
	rm -f $(ARCHSALIDA) $(OBJS)

.PHONY: all run clean
