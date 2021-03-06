#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <std/common.h>

#include <kernel/address_space.h>
#include <kernel/address_space_bitmap.h>

#include <kernel/interrupts/interrupts.h>

#define PAGE_PRESENT_FLAG 0x1
#define PAGE_WRITE_FLAG 0x2
#define PAGE_USER_FLAG 0x4

typedef struct page {
	uint32_t present	:  1; //page present in memory
	uint32_t rw			:  1; //read-only if clear, readwrite if set
	uint32_t user 		:  1; //kernel level only if clear
	uint32_t accessed	:  1; //has page been accessed since last refresh?
	uint32_t dirty		:  1; //has page been written to since last refresh?
	uint32_t unused		:  7; //unused/reserved bits
	uint32_t frame		: 20; //frame address, shifted right 12 bits
} vmm_pte_t;
typedef vmm_pte_t page_t;

typedef struct page_table {
	vmm_pte_t pages[1024];
} vmm_pde_t;
typedef vmm_pde_t page_table_t;

typedef struct vmm_memory_region {
    uint32_t region_start_addr;
    uint32_t region_size;
} vmm_memory_region_t;

typedef struct vmm_address_space {
    vmm_memory_region_t* region_list;
} vmm_address_space_t;

typedef struct page_directory {
	//array of pointers to pagetables
	page_table_t* tables[1024];

	//array of pointers to pagetables above, but give their *physical*
	//location, for loading into CR3 reg
	uint32_t tablesPhysical[1024];

	//physical addr of tablesPhysical.
	//needed once kernel heap is allocated and
	//directory may be in a different location in virtual memory
	uint32_t physicalAddr;
} page_directory_t;

typedef struct vmm_pdir {
    vmm_pde_t* tables[1024];
    uint32_t tablesPhysical[1024];
    uint32_t physicalAddr;
} vmm_pdir_t;

/*
typedef struct vmm_page {
    uint32_t present    :  1; //page present in memory
    uint32_t writable   :  1; //read-only if clear, readwrite if set
    uint32_t user_mode  :  1; //kernel level only if clear
    uint32_t accessed   :  1; //has page been accessed since last refresh?
    uint32_t dirty      :  1; //has page been written to since last refresh?
    uint32_t unused     :  7; //unused/reserved bits
    uint32_t frame_idx  : 20; //frame index, shifted right 12 bits. The actual frame address is this value * PAGING_FRAME_SIZE
} vmm_page_t;

typedef struct vmm_page_table {
    vmm_page_t pages[1024];
} vmm_page_table_t;

typedef struct vmm_page_directory {
    vmm_page_table_t* table_pointers[1024];
} vmm_page_directory_t;

//VMM memory space should have bitset of frames mapped in
typedef struct vmm_state {
    address_space_page_bitmap_t allocated_pages;
} vmm_state_t;
*/
//sets up environment, page directories, etc
//and, enables paging
void paging_install();

//causes passed page directory to be loaded into
//CR3 register
void switch_page_directory(page_directory_t* new_dir);

//retrieves pointer to page required
//if make == 1, if the page-table in which this page should
//reside isn't created, create it
page_t* get_page(uint32_t address, int make, page_directory_t* dir);

//retrieves the physical address currently loaded into cr3
uint32_t get_cr3();

//maps physical range to virtual memory
void vmem_map(uint32_t virt, uint32_t physical);

bool alloc_frame(page_t* page, int is_kernel, int is_writeable);
void free_frame(page_t* page);

//create a new page directory with all the info of src
//kernel pages are linked instead of copied
page_directory_t* clone_directory(page_directory_t* src);
//free all memory associated with a page directory dir
void free_directory(page_directory_t* dir);

void *mmap(void *addr, uint32_t length, int flags, int fd, uint32_t offset);
int munmap(void* addr, uint32_t length);

int brk(void* addr);
void* sbrk(int increment);

page_directory_t* page_dir_kern();
page_directory_t* page_dir_current();

//debug function to print regions
//of in-use pages in a page directory
void page_regions_print(page_directory_t* dir);

page_t* vmm_get_page_for_virtual_address(vmm_pdir_t* dir, uint32_t virt_addr);
page_t* vmm_page_alloc_for_phys_addr(page_directory_t* dir, uint32_t phys_addr);
page_t* vmm_page_alloc_for_virt_addr(page_directory_t* dir, uint32_t virt_addr);

void vmm_map_page_to_frame(page_t* page, uint32_t frame_addr);

void vmm_map_region(vmm_pdir_t* dir, uint32_t start, uint32_t size, uint16_t flags);
void vmm_identity_map_region(vmm_pdir_t* dir, uint32_t start, uint32_t size, uint16_t flags);

void vmm_dump(page_directory_t* dir);

void vmm_init(void);
bool vmm_is_active();

void vmm_load_pdir(vmm_pdir_t* dir);
vmm_pdir_t* vmm_active_pdir();

uint32_t vmm_get_phys_for_virt(uint32_t virtualaddr);
void vmm_map_virt_to_phys(vmm_pdir_t* dir, uint32_t page_addr, uint32_t frame_addr, uint16_t flags);
void vmm_map_virt(vmm_pdir_t* dir, uint32_t page_addr, uint16_t flags);

#endif
