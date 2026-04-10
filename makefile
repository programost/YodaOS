CC = gcc
ASM = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -nostdlib -nostdinc -fno-pie -fno-stack-protector -Wall -Wextra -I. -Os
ASMFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T linker.ld -no-pie

OBJS = boot.o asm_funcs.o kernel.o drivers.o fs.o string.o

all: yodaos.iso

%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

yodaos.iso: kernel.elf
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'menuentry "YodaOS" {' >> iso/boot/grub/grub.cfg
	echo '    multiboot /boot/kernel.elf' >> iso/boot/grub/grub.cfg
	echo '    boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o yodaos.iso iso

clean:
	rm -f *.o kernel.elf yodaos.iso
	rm -rf iso

run: yodaos.iso
	qemu-system-i386 -cdrom yodaos.iso -m 64 -drive file=disk.img,format=raw,if=ide -boot d

disk.img:
	dd if=/dev/zero of=disk.img bs=512 count=131072
