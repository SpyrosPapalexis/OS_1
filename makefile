all: main
main: main.c
	gcc -o main main.c -Wall -lrt -pthread 
clean:
	rm -f main;
