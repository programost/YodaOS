ENTRY(start)

SECTIONS
{
    . = 0x100000;          /* GRUB загружает ядро по адресу 1MB */

    .multiboot : { *(.multiboot) }
    .text : { *(.text) }
    .data : { *(.data) }
    .bss : { *(.bss) }

    /DISCARD/ : { *(.comment) *(.note) }
}