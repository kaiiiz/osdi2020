#ifndef __MM_H__
#define __MM_H__

#define KERNEL_VIRT_BASE        0xFFFF000000000000
#define PAGE_SIZE               4096
#define PAGE_FRAMES_NUM         (0x40000000 / PAGE_SIZE)
#define PAGE_MASK               ~0xFFF
#define MAX_BUDDY_ORDER         9 // 2^0 ~ 2^8 => 4k to 1MB
#define MAX_OBJ_ALLOCTOR_NUM    32
#define MAX_POOL_PAGES          16

#ifndef __ASSEMBLY__

#include "typedef.h"
#include "schedule.h"
#include "list.h"

enum booking_status {
    AVAL,
    USED,
};

struct buddy_t {
    uint32_t nr_free;
    struct list_head head;
};

struct page_t {
    enum booking_status used;
    int order;
    int idx; // debug used
    struct list_head list;
};

struct free_list {
    struct free_list *next;
};

struct pool_t {
    enum booking_status used;
    int obj_size;
    int obj_per_page;
    int obj_used;
    int page_used;
    uint64_t page_addr[MAX_POOL_PAGES];
    struct free_list* free;
};

/* Variables init in mm.c */
extern struct pool_t obj_allocator[MAX_OBJ_ALLOCTOR_NUM];
extern struct buddy_t free_area[MAX_BUDDY_ORDER];
extern struct page_t *page;
extern uint64_t remain_page;

/* Function in mm.c */
void mm_init();
void kfree(void* addr);
void* kmalloc(uint64_t size);
void fork_pgd(struct task_t* target, struct task_t* dest);
void* map_page(struct task_t* task, uint64_t user_addr);
void page_reclaim(uint64_t pgd_phy);

// object allocator
int obj_alloc_register(uint64_t size);
void* obj_alloc_kernel(int token);
void obj_free(int token, void* virt_addr);

// buddy system
void buddy_info();
void buddy_init();
void buddy_push(struct buddy_t* bd, struct list_head* elmt);
void* buddy_alloc(int order);
void buddy_free(void* virt_addr);

// utility function
uint64_t user_addr_to_page_addr(uint64_t user_addr, uint64_t pgd);
void memzero(uint8_t* addr, int size);
void memcpy(void *dest, void *src, uint64_t size);
uint64_t virtual_to_physical(uint64_t virt_addr);
uint64_t phy_to_pfn(uint64_t phy_addr);
struct page_t* find_buddy(int pfn, int order);

#endif /* __ASSEMBLY__ */

#endif /* __MM_H__ */