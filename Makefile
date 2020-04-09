test: gc.h libbgc.a test.c
	$(CC) test.c -std=c11 -O3 libbgc.a -o test

libbgc.a: gc.h gc.c x64_reg.o
	$(CC) -c -std=c11 gc.c -O3 -o libbgc.o
	ar rcs libbgc.a libbgc.o x64_reg.o

x64_reg.o: x64_reg.asm
	nasm -f elf64 x64_reg.asm -o x64_reg.o