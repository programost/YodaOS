#!/bin/bash
echo "Building YodaOS v2.0 Graphics Edition..."

# Компиляция загрузчика
nasm -f bin boot.asm -o boot.bin

# Компиляция ядра
nasm -f bin kernel.asm -o kernel.bin

# Создание образа (1.44MB floppy)
dd if=/dev/zero of=yodaos.img bs=512 count=2880
dd if=boot.bin of=yodaos.img conv=notrunc
dd if=kernel.bin of=yodaos.img bs=512 seek=1 conv=notrunc

echo "Build complete!"
echo "Image: yodaos.img"
echo ""
echo "To run with QEMU:"
echo "  qemu-system-i386 -fda yodaos.img"
echo ""
echo "New features:"
echo "  - VGA 320x200 graphics mode (vga 1)"
echo "  - Date/time commands"
echo "  - Sound beep command"
echo "  - Switch between graphics/text modes"