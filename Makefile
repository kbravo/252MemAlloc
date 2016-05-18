
CC = gcc -g

all: MyMalloc.so

MyMalloc.so: MyMalloc.c
	$(CC) -fPIC -c -g MyMalloc.c
	gcc -shared -o MyMalloc.so MyMalloc.o

clean:
	rm -f *.o MyMalloc.so core a.out *.out *.txt
