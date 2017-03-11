#include "elf.h"
#include <stdint.h>
#include <std/std.h>
#include <std/printf.h>
#include <std/kheap.h>
#include <kernel/util/vfs/fs.h>
#include <kernel/util/paging/paging.h>
#include <kernel/util/multitasking/tasks/task.h>

static bool elf_check_magic(elf_header* hdr) {
	if (!hdr) return false;

	if (hdr->ident[EI_MAG0] != ELFMAG0) {
		printf_err("ELF parser: EI_MAG0 (%d) incorrect", hdr->ident[EI_MAG0]);
		return false;
	}
	if (hdr->ident[EI_MAG1] != ELFMAG1) {
		printf_err("ELF parser: EI_MAG1 (%d) incorrect", hdr->ident[EI_MAG1]);
		return false;
	}
	if (hdr->ident[EI_MAG2] != ELFMAG2) {
		printf_err("ELF parser: EI_MAG2 (%d) incorrect", hdr->ident[EI_MAG2]);
		return false;
	}
	if (hdr->ident[EI_MAG3] != ELFMAG3) {
		printf_err("ELF parser: EI_MAG3 (%d) incorrect", hdr->ident[EI_MAG3]);
		return false;
	}
	return true;
}

static bool elf_check_supported(elf_header* hdr) {
	if (hdr->ident[EI_CLASS] != ELFCLASS32) {
		printf_err("ELF parser: Unsupported file class");
		return false;
	}
	if (hdr->ident[EI_DATA] != ELFDATA2LSB) {
		printf_err("ELF parser: Unsupported byte order");
		return false;
	}
	if (hdr->machine != EM_386) {
		printf_err("ELF parser: Unsupported target");
		return false;
	}
	if (hdr->ident[EI_VERSION] != EV_CURRENT) {
		printf_err("ELF parser: Unsupported version");
		return false;
	}
	if (hdr->type != ET_REL && hdr->type != ET_EXEC) {
		printf_err("ELF parser: Unsupported file type");
		return false;
	}
	return true;
}

bool elf_validate(elf_header* hdr) {
	if (!elf_check_magic(hdr)) {
		printf_err("ELF parser: Invalid ELF magic");
		return false;
	}
	if (!elf_check_supported(hdr)) {
		printf_err("ELF parser: File not supported");
		return false;
	}
	return true;
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
	//printf("Loading ELF %s\n", filename);
	FILE* elf = fopen(filename, "r");
	if (!elf) {
		printf_err("Couldn't find file %s", filename);
		return;
	}

	//find file size
	fseek(elf, 0, SEEK_END);
	uint32_t size = ftell(elf);
	fseek(elf, 0, SEEK_SET);

	char* filebuf = kmalloc(size);
	for (int i = 0; i < size; i++) {
		filebuf[i] = fgetc(elf);
	}
	elf_load_file(filename, filebuf, size);
	return -1;
}

bool elf_load_segment(unsigned char* src, elf_phdr* seg) {
	//loadable?
	if (seg->type != PT_LOAD) {
		printf_err("Tried to load non-loadable segment");
		printk_err("Tried to load non-loadable segment");
		return false; 
	}

	unsigned char* src_base = src + seg->offset;
	//figure out range to map this binary to in virtual memory
	unsigned char* dest_base = (unsigned char*)seg->vaddr;

	unsigned char* dest_limit = (uintptr_t)(dest_base + seg->memsz + 0x1000) & 0xFFFFF000;

	//alloc enough mem for new task
	for (uint32_t i = dest_base; i < dest_limit; i += 0x1000) {
#include <kernel/util/paging/paging.h>
		extern page_directory_t* current_directory;
		page_t* page = get_page(i, 1, current_directory);

		if (page) {
			if (!alloc_frame(page, 1, 1)) {
				//printf_err("ELF: alloc_frame failed");
				//while (1) {}
			}
		}
	}

	// Copy data
	memcpy(dest_base, src_base, seg->memsz);

	return true;
}

uint32_t elf_load_small(unsigned char* src) {
	//draw_boot_background();

	elf_header* hdr = (elf_header*)src;
	elf_phdr* phdr_table = (elf_phdr*)((uint32_t)hdr + hdr->phoff);
	uintptr_t phdr_table_addr = (uint32_t)hdr + hdr->phoff;

	int segcount = hdr->phnum; 
	if (!segcount) return 0;

	bool found_loadable_seg = false;
	//load each segment
	for (int i = 0; i < segcount; i++) {
		elf_phdr* segment = (elf_phdr*)(phdr_table_addr + (i * hdr->phentsize));
		if (elf_load_segment(src, segment)) {
			found_loadable_seg = true;
		}
	}

	//return entry point
	if (found_loadable_seg) {
		return hdr->entry;
	}
	return 0;
}

char* elf_get_string_table(void* file, uint32_t binary_size) {
	elf_header* hdr = (elf_header*)file;
	char* string_table;
	uint32_t i = 0;
	for (uint32_t x = 0; x < hdr->shentsize * hdr->shnum; x += hdr->shentsize) {
		if (hdr->shoff + x > binary_size) {
			printf("ELF: Tried to read beyond the end of the file.\n");
			return NULL;
		}
		elf_s_header* shdr = (elf_s_header*)(file + (hdr->shoff + x));
		if (i == hdr->shstrndx) {
			string_table = (char *)(file + shdr->offset);
			return string_table;
		}
		i++;
	}
}

void* elf_load_file(char* name, void* file, uint32_t binary_size) {
	elf_header* hdr = (elf_header*)file;
	if (!elf_validate(hdr)) {
		return;
	}

	char* string_table = elf_get_string_table(hdr, binary_size);

	uint32_t prog_break = 0;
	uint32_t bss_loc = 0;
	for (uint32_t x = 0; x < hdr->shentsize * hdr->shnum; x += hdr->shentsize) {
		if (hdr->shoff + x > binary_size) {
			printf("Tried to read beyond the end of the file.\n");
			return NULL;
		}

		elf_s_header* shdr = (elf_s_header*)((uintptr_t)file + (hdr->shoff + x));
		char* section_name = (char*)((uintptr_t)string_table + shdr->name);

		//alloc memory for .bss segment
		if (!strcmp(section_name, ".bss")) {
			uintptr_t page_aligned = shdr->size + 
									 (0x1000 - 
									 (shdr->size % 0x1000));
			for (int i = 0; i <= page_aligned; i += 0x1000) {
				extern page_directory_t* current_directory;
				alloc_frame(get_page(shdr->addr + i, 1, current_directory), 1, 1);
			}

			//zero out .bss
			char* buf = (char*)shdr->addr;
			memset(buf, 0, shdr->size);

			//set program break to .bss segment
			prog_break = shdr->addr + shdr->size;
			bss_loc = shdr->addr;
		}
	}

	uint32_t entry = elf_load_small(file);
	kfree(file);

	if (entry) {
		task_t* elf = task_with_pid(getpid());
		elf->prog_break = prog_break;
		elf->bss_loc = bss_loc;
		elf->name = strdup(name);

		int(*elf_main)(void) = (int(*)(void))entry;
		become_first_responder();

		int ret = elf_main();
		ASSERT(0, "this should be unreachable!");
	}
	else {
		printf_err("ELF wasn't loadable!");
		printk_err("ELF wasn't loadable!");
		return;
	}
}

