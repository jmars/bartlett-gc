test: gc.h libbgc.o x64_reg.o test.c
	$(CC) test.c -O3 libbgc.o x64_reg.o -o test

libbgc.o: gc.h gc.c
	$(CC) -c gc.c -O3 -o libbgc.o

x64_reg.o: x64_reg.asm
	nasm -f elf64 x64_reg.asm -o x64_reg.o