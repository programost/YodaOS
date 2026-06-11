#include "string.h"
#include "types.h"

extern void isr_gpf(void);
extern void isr_page_fault(void);
extern void isr_of(void);

struct idt_entry {
    uint16_t off_lo;
    uint16_t sel;
    uint8_t ist;
    uint8_t flags;
    uint16_t off_mid;
    uint32_t off_hi;
    uint32_t zero;
} __attribute__((packed));

extern void isr80_asm(void);

static struct idt_entry idt[256];

struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtp;

static void idt_set_gate(unsigned vec, void (*isr)(void), uint8_t flags) {
    uint64_t p = (uint64_t)(uintptr_t)isr;
    idt[vec].off_lo = (uint16_t)(p & 0xFFFF);
    idt[vec].sel = 0x08;
    idt[vec].ist = 0;
    idt[vec].flags = flags;
    idt[vec].off_mid = (uint16_t)((p >> 16) & 0xFFFF);
    idt[vec].off_hi = (uint32_t)(p >> 32);
    idt[vec].zero = 0;
}

void idt_install(void) {
    memset(idt, 0, sizeof(idt));
    idt_set_gate(0x80, isr80_asm, 0xEE);
    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base = (uint64_t)(uintptr_t)idt;
    __asm__ volatile("lidt %0" : : "m"(idtp));

    idt_set_gate(13, isr_gpf, 0x8E);   // #GP
    idt_set_gate(14, isr_page_fault, 0x8E); // #PF
    idt_set_gate(4, isr_of, 0x8E);     // #OF
}

