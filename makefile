# YodaOS — исходники: boot/, arch/x86_64/, kernel/, drivers/, fs/, ramfs/, lib/, shell/, include/
# Объектные файлы в build/ — корень репозитория остаётся для ISO, ELF, disk.img

BUILD := build

CC = gcc
ASM = nasm
LD = ld

CFLAGS = -m64 -mno-red-zone -ffreestanding -nostdlib -nostdinc -fno-pie -fno-stack-protector -Wall -Wextra -Iinclude -Os
ASMFLAGS = -f elf64
LDFLAGS = -m elf_x86_64 -T linker.ld -no-pie -nostdlib

OBJS = $(BUILD)/boot.o $(BUILD)/asm_funcs.o $(BUILD)/intr.o $(BUILD)/usr_entry.o \
	$(BUILD)/ring3_demo.o $(BUILD)/idt.o $(BUILD)/syscall.o \
	$(BUILD)/kernel.o $(BUILD)/drivers.o $(BUILD)/fs.o $(BUILD)/ramfs.o \
	$(BUILD)/string.o $(BUILD)/bb_applets.o $(BUILD)/shell.o

.PHONY: all clean run

all: yodaos.iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: boot/boot.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD)/asm_funcs.o: arch/x86_64/asm_funcs.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD)/intr.o: arch/x86_64/intr.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD)/usr_entry.o: arch/x86_64/usr_entry.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD)/ring3_demo.o: arch/x86_64/ring3_demo.asm | $(BUILD)
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD)/idt.o: arch/x86_64/idt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/syscall.o: arch/x86_64/syscall.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.o: kernel/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/drivers.o: drivers/drivers.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs.o: fs/fs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ramfs.o: ramfs/ramfs.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/string.o: lib/string.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bb_applets.o: ramfs/bb_applets.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/shell.o: shell/shell.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

yodaos.iso: kernel.elf
	mkdir -p iso/boot/grub
	cp kernel.elf iso/boot/kernel.elf
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo 'menuentry "YodaOS" {' >> iso/boot/grub/grub.cfg
	echo '    multiboot2 /boot/kernel.elf' >> iso/boot/grub/grub.cfg
	echo '    boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	grub-mkrescue -o yodaos.iso iso

clean:
	rm -rf $(BUILD) *.o kernel.elf yodaos.iso
	rm -rf iso

# disk.img: locking=off — не требовать эксклюзивный fcntl, если другой QEMU держит образ
run: yodaos.iso disk.img
	qemu-system-x86_64 -cdrom yodaos.iso -m 64 -boot d \
		-blockdev driver=file,node-name=yodahd,filename=disk.img,locking=off \
		-blockdev driver=raw,node-name=yodaraw,file=yodahd \
		-device ide-hd,drive=yodaraw,bus=ide.0,unit=0

disk.img:
	dd if=/dev/zero of=disk.img bs=512 count=131072
