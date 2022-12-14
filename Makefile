CC=gcc
CFLAGS=-Wall -Wextra -Wswitch-enum -pedantic -ggdb -pg -std=c11

SRC_C=main.c
SRC_ASM=__switch_thread__.asm __get_result__.asm
SRC=$(SRC_C) $(SRC_ASM)
OBJ_C=$(SRC_C:.c=.o)
OBJ=$(OBJ_C) $(SRC_ASM:.asm=.o)

build: compile link

compile: $(SRC_C) $(SRC_ASM) 
	$(CC) -c $(CFLAGS) -o $(OBJ_C) $(SRC_C)
	fasm __switch_thread__.asm
	fasm __get_result__.asm

link: $(OBJ)
	$(CC) -o main $(OBJ)

clear: $(OBJ)
	-rm $(OBJ) 
