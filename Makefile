all:
	nasm -f elf32 src/boot.asm -o boot.o
	gcc -m32 -ffreestanding -c src/kernel.c -o kernel.o
	ld -m elf_i386 -T linker.ld -o boot/kernel.bin boot.o kernel.o
	grub-mkrescue -o LunarOS.iso .

run:
	qemu-system-x86_64 -cdrom LunarOS.iso