gc: x64_reg.o cproc-compiler/cproc
	cproc-compiler/cproc gc.c x64_reg.o -o gc

cproc-compiler/cproc:
	git submodule update --init --recursive
	cd cproc-compiler && ./configure && make

x64_reg.o: x64_reg.asm
	nasm -f elf64 x64_reg.asm -o x64_reg.o