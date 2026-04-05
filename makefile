CC = gcc -m32 -ffreestanding -nostdlib -nostartfiles -fno-stack-protector -c
AS = nasm -f elf32
LD = ld -m elf_i386 -T linker.ld

OBJS = boot.o kernel.o drivers.o fs.o asm_funcs.o string.o

all: yodaos.bin

yodaos.bin: $(OBJS)
	$(LD) $(OBJS) -o yodaos.bin

boot.o: boot.asm
	$(AS) boot.asm -o boot.o

asm_funcs.o: asm_funcs.asm
	$(AS) asm_funcs.asm -o asm_funcs.o

kernel.o: kernel.c kernel.h drivers.h fs.h string.h
	$(CC) kernel.c -o kernel.o

drivers.o: drivers.c drivers.h kernel.h
	$(CC) drivers.c -o drivers.o

fs.o: fs.c fs.h drivers.h kernel.h string.h
	$(CC) fs.c -o fs.o

string.o: string.c string.h
	$(CC) string.c -o string.o

clean:
	rm -f *.o yodaos.bin disk.img

run: yodaos.bin
	qemu-system-i386 -kernel yodaos.bin -hda disk.img

create_disk:
	dd if=/dev/zero of=disk.img bs=512 count=20480