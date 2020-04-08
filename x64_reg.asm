SECTION .TEXT
	GLOBAL zzReadRegister

zzReadRegister:
	cmp rdi, 0
	je zzReg0
	cmp rdi, 1
	je zzReg1
	cmp rdi, 2
	je zzReg2
	cmp rdi, 3
	je zzReg3
	cmp rdi, 4
	je zzReg4
	cmp rdi, 5
	je zzReg5
	cmp rdi, 6
	je zzReg6
	cmp rdi, 7
	je zzReg7
	cmp rdi, 8
	je zzReg8
	cmp rdi, 9
	je zzReg9
	cmp rdi, 10
	je zzReg10
	cmp rdi, 11
	je zzReg11
	cmp rdi, 12
	je zzReg12
	cmp rdi, 13
	je zzReg13
	cmp rdi, 14
	je zzReg14
	cmp rdi, 15
	je zzReg15
	mov rax,0
	ret

zzReg0:
	mov rax,rax
	ret

zzReg1:
	mov rax,rcx
	ret

zzReg2:
	mov rax,rdx
	ret

zzReg3:
	mov rax,rbx
	ret

zzReg4:
	mov rax,rsi
	ret

zzReg5:
	mov rax,rdi
	ret

zzReg6:
	mov rax,rsp
	ret

zzReg7:
	mov rax,rbp
	ret

zzReg8:
	mov rax,r8
	ret

zzReg9:
	mov rax,r9
	ret

zzReg10:
	mov rax,r10
	ret

zzReg11:
	mov rax,r11
	ret

zzReg12:
	mov rax,r12
	ret

zzReg13:
	mov rax,r13
	ret

zzReg14:
	mov rax,r14
	ret

zzReg15:
	mov rax,r15
	ret