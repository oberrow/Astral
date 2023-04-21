#include <arch/acpi.h>
#include <logging.h>
#include <kernel/vmm.h>
#include <arch/mmu.h>
#include <kernel/alloc.h>

#define IOAPIC_REG_ID 0
#define IOAPIC_REG_ENTRYCOUNT 1
#define IOAPIC_REG_PRIORITY 2
#define IOAPIC_REG_ENTRY 0x10

typedef struct {
	uint8_t type;
	uint8_t length;
} __attribute__((packed)) listheader_t;

typedef struct {
	listheader_t header;
	uint16_t reserved;
	uint64_t addr;
} __attribute__((packed)) lapicoverride_t;

typedef struct {
	listheader_t header;
	uint8_t id;
	uint8_t reserved;
	uint32_t addr;
	uint32_t intbase;
} __attribute__((packed)) ioentry_t;

typedef struct {
	void *addr;
	int base;
	int top;
} ioapicdesc_t;

static ioapicdesc_t *ioapics;

static madt_t *madt;
static listheader_t *liststart;
static listheader_t *listend;
static void *lapicaddr;

static size_t overridecount;
static size_t iocount;

static inline listheader_t *getnext(listheader_t *header) {
	uintptr_t ptr = (uintptr_t)header;
	return (listheader_t *)(ptr + header->length);
}

static inline void *getentry(int type, int n) {
	listheader_t *entry = liststart;

	while (entry < listend) {
		if (entry->type == type) {
			if (n-- == 0)
				return entry;
		}
		entry = getnext(entry);
	}

	return NULL;
}

static int getcount(int type) {
	listheader_t *entry = liststart;
	int count = 0;

	while (entry < listend) {
		if (entry->type == type)
			++count;
		entry = getnext(entry);
	}

	return count;
}

static uint32_t readioapic(void *ioapic, int reg) {
	volatile uint32_t *apic = ioapic;
	*apic = reg & 0xff;
	return *(apic + 4);
}

static void writeioapic(void *ioapic, int reg, uint32_t v) {
	volatile uint32_t *apic = ioapic;
	*apic = reg & 0xff;
	*(apic + 4) = v;
}

static void writeiored(void *ioapic, uint8_t entry, uint8_t vector, uint8_t delivery, uint8_t destmode, uint8_t polarity, uint8_t mode, uint8_t mask, uint8_t dest){
	uint32_t val = vector;
	val |= (delivery & 0b111) << 8;
	val |= (destmode & 1) << 11;
	val |= (polarity & 1) << 13;
	val |= (mode & 1) << 15;
	val |= (mask & 1) << 16;

	writeioapic(ioapic, 0x10 + entry * 2, val);
	writeioapic(ioapic, 0x11 + entry * 2, (uint32_t)dest << 24);
}

void apic_init() {
	madt = acpi_findtable("APIC", 0);
	__assert(madt);

	liststart = (void *)((uintptr_t)madt + sizeof(madt_t));
	listend   = (void *)((uintptr_t)madt + madt->header.length);

	overridecount = getcount(ACPI_MADT_TYPE_OVERRIDE);
	iocount = getcount(ACPI_MADT_TYPE_IOAPIC);

	// map LAPIC to virtual memory

	lapicoverride_t *lapic64 = getentry(ACPI_MADT_TYPE_64BITADDR, 0);

	void *paddr = lapic64 ? (void *)lapic64->addr : (void *)(uint64_t)madt->addr;

	if (lapic64)
		printf("\e[94mUsing 64 bit override for the local APIC address\n\e[0m");

	lapicaddr = vmm_map(NULL, PAGE_SIZE, VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, paddr);
	__assert(lapicaddr);

	// map I/O apics to memory

	ioapics = alloc(sizeof(ioapicdesc_t) * iocount);
	__assert(ioapics);

	for (int i = 0; i < iocount; ++i) {
		ioentry_t *entry = getentry(ACPI_MADT_TYPE_IOAPIC, i);

		__assert((entry->addr % PAGE_SIZE) == 0);
		ioapics[i].addr = vmm_map(NULL, PAGE_SIZE, VMM_FLAGS_PHYSICAL, ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC, (void *)(uint64_t)entry->addr);
		__assert(ioapics[i].addr);
		ioapics[i].base = entry->intbase;
		ioapics[i].top = ioapics[i].base + ((readioapic(ioapics[i].addr, IOAPIC_REG_ENTRYCOUNT) >> 16) & 0xff) + 1;
		printf("ioapic: addr %p base %lu top %lu\n", entry->addr, entry->intbase, ioapics[i].top);
		for (int j = ioapics[i].base; j < ioapics[i].top; ++j)
			writeiored(ioapics[i].addr, j - ioapics[i].base, 0xfe, 0, 0, 0, 0, 1, 0);
			
	}
}
