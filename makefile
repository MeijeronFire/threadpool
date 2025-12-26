CFLAGS ?= -Wall
file=tpool

all:
	gcc -Iinclude $(CFLAGS) -c src/$(file).c -o $(file).o
	ar rcs $(file).a $(file).o
	rm $(file).o