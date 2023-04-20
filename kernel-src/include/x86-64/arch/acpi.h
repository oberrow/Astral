#ifndef _ACPI_H
#define _ACPI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
	char sig[8];
	uint8_t checksum;
	char oem[6];
	uint8_t revision;
	uint32_t rsdt;
	uint32_t length;
	uint64_t xsdt;
	uint8_t xchecksum;
	uint8_t reserved[3];
} __attribute__((packed)) rsdp_t;

typedef struct {
	char sig[4];
	uint32_t length;
	uint8_t revision;
	uint8_t cechksum;
	char oemid[6];
	char oemtableid[8];
	uint32_t oemrevision;
	uint32_t creatorrevision;
} __attribute__((packed)) sdtheader_t;

typedef struct {
	sdtheader_t header;
	uint64_t tables[];
} __attribute__((packed)) xsdt_t;

typedef struct {
	sdtheader_t header;
	uint32_t tables[];
} __attribute__((packed)) rsdt_t;

void acpi_init();

#endif
