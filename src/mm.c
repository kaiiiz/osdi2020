#include "mm.h"
#include "mmu.h"
#include "mbox.h"

#include "peripherals/mmio.h"
#include "schedule.h"
#include "uart0.h"

struct pool_t obj_allocator[MAX_OBJ_ALLOCTOR_NUM];
struct buddy_t free_area[MAX_BUDDY_ORDER];
struct page_t *page;

/*
 *  Memory management initialization
 */
void page_struct_init(int first_avail_page, int memory_end_page) {
    int bytes = sizeof(struct page_t) * memory_end_page;
    int page_need = (bytes % PAGE_SIZE) ? bytes / PAGE_SIZE + 1 : bytes / PAGE_SIZE;
    uint64_t page_addr = (first_avail_page * PAGE_SIZE) | KERNEL_VIRT_BASE;
    page = (struct page_t*) page_addr;
    first_avail_page += page_need;

    // page booking for kernel
    int i = 0;
    while (i < first_avail_page) {
        page[i].used = USED;
        i++;
    }

    // alignment
    int remainder = i % (1 << MAX_BUDDY_ORDER);
    i += (1 << MAX_BUDDY_ORDER) - remainder;

    // page booking for normal region
    int order = MAX_BUDDY_ORDER - 1;
    int counter = 0;
    while (i < memory_end_page) {
        // init page counter times
        if (counter) {
            page[i].idx = i;
            page[i].used = AVAL;
            page[i].order = -1;
            list_head_init(&(page[i].list));
            counter--;
            i++;
        }
        // page i can fill current order
        else if (i + (1 << order) - 1 < memory_end_page) {
            page[i].idx = i;
            page[i].used = AVAL;
            page[i].order = order;
            buddy_push(&(free_area[order]), &(page[i].list));
            counter = (1 << order) - 1;
            i++;
        }
        // reduce order, try again
        else {
            order--;
        }
    }
}

void mm_init() {
    extern uint8_t __kernel_end;  // defined in linker
    uint8_t* kernel_end = &__kernel_end;
    int first_avail_page = ((uint64_t)kernel_end / PAGE_SIZE) + 1;
    int arm_memory_end_page = arm_memory_end / PAGE_SIZE;
    buddy_init();
    page_struct_init(first_avail_page, arm_memory_end_page);
}

/*
 *  Map page for user address
 */

// create pgd, return pgd address
void* create_pgd(struct task_t* task) {
    if (!task->mm.pgd) {
        void* page = buddy_alloc(0);
        if (page == NULL) return NULL;
        task->mm.pgd = virtual_to_physical((uint64_t)page);
    }
    return (void*)(task->mm.pgd + KERNEL_VIRT_BASE);
}

// create page table, return next level table
void* create_page_table(uint64_t* table, uint64_t idx) {
    if (table == NULL) return NULL;
    if (!table[idx]) {
        void* page = buddy_alloc(0);
        if (page == NULL) return NULL;
        table[idx] = virtual_to_physical((uint64_t)page) | PD_TABLE;
    }
    return (void*)((table[idx] & PAGE_MASK) + KERNEL_VIRT_BASE);
}

// create page, return page address
void* create_page(uint64_t* pte, uint64_t idx) {
    if (pte == NULL) return NULL;
    if (!pte[idx]) {
        void* page = buddy_alloc(0);
        if (page == NULL) return NULL;
        pte[idx] = virtual_to_physical((uint64_t)page) | PTE_NORAL_ATTR | PD_ACCESS_PERM_RW;
    }
    return (void*)((pte[idx] & PAGE_MASK) + KERNEL_VIRT_BASE);
}

// allocate new page in user's address table, return page's virtual address
void* map_page(struct task_t* task, uint64_t user_addr) {
    uint64_t pgd_idx = (user_addr & (PD_MASK << PGD_SHIFT)) >> PGD_SHIFT;
    uint64_t pud_idx = (user_addr & (PD_MASK << PUD_SHIFT)) >> PUD_SHIFT;
    uint64_t pmd_idx = (user_addr & (PD_MASK << PMD_SHIFT)) >> PMD_SHIFT;
    uint64_t pte_idx = (user_addr & (PD_MASK << PTE_SHIFT)) >> PTE_SHIFT;

    uint64_t* pgd = create_pgd(task);
    uint64_t* pud = create_page_table(pgd, pgd_idx);
    uint64_t* pmd = create_page_table(pud, pud_idx);
    uint64_t* pte = create_page_table(pmd, pmd_idx);
    return create_page(pte, pte_idx);
}

/*
 *  Fork page descriptor iteratively
 */
void fork_pte(uint64_t* target_pte, uint64_t* dest_pte) {
    for (int i = 0; i < 512; i++) {
        if (target_pte[i]) {
            uint64_t* target_page = (uint64_t*)((target_pte[i] & PAGE_MASK) | KERNEL_VIRT_BASE);
            uint64_t* dest_page = create_page(dest_pte, i);
            memcpy((void*)dest_page, (void*)target_page, PAGE_SIZE);
        }
    }
}

void fork_pmd(uint64_t* target_pmd, uint64_t* dest_pmd) {
    for (int i = 0; i < 512; i++) {
        if (target_pmd[i]) {
            uint64_t* target_pte = (uint64_t*)((target_pmd[i] & PAGE_MASK) | KERNEL_VIRT_BASE);
            uint64_t* dest_pte = create_page_table(dest_pmd, i);
            fork_pte(target_pte, dest_pte);
        }
    }
}

void fork_pud(uint64_t* target_pud, uint64_t* dest_pud) {
    for (int i = 0; i < 512; i++) {
        if (target_pud[i]) {
            uint64_t* target_pmd = (uint64_t*)((target_pud[i] & PAGE_MASK) | KERNEL_VIRT_BASE);
            uint64_t* dest_pmd = create_page_table(dest_pud, i);
            fork_pmd(target_pmd, dest_pmd);
        }
    }
}

void fork_pgd(struct task_t* target, struct task_t* dest) {
    uint64_t* target_pgd = (uint64_t*)((target->mm.pgd & PAGE_MASK) | KERNEL_VIRT_BASE);
    uint64_t* dest_pgd = create_pgd(dest);
    for (int i = 0; i < 512; i++) {
        if (target_pgd[i]) {
            uint64_t* target_pud = (uint64_t*)((target_pgd[i] & PAGE_MASK) | KERNEL_VIRT_BASE);
            uint64_t* dest_pud = create_page_table(dest_pgd, i);
            fork_pud(target_pud, dest_pud);
        }
    }
}

/*
 *  Page reclaim related function 
 *  Need to be called before exit
 */
void page_reclaim_pte(uint64_t pte_phy) {
    uint64_t* pte = (uint64_t*)(pte_phy | KERNEL_VIRT_BASE);
    for (int i = 0; i < 512; i++) {
        if (pte[i]) {
            void* page = (void*)((pte[i] & PAGE_MASK) | KERNEL_VIRT_BASE);
            buddy_free(page);
        }
    }
    buddy_free(pte);
}

void page_reclaim_pmd(uint64_t pmd_phy) {
    uint64_t* pmd = (uint64_t*)(pmd_phy | KERNEL_VIRT_BASE);
    for (int i = 0; i < 512; i++) {
        if (pmd[i]) {
            page_reclaim_pte(pmd[i] & PAGE_MASK);
        }
    }
    buddy_free(pmd);
}

void page_reclaim_pud(uint64_t pud_phy) {
    uint64_t* pud = (uint64_t*)(pud_phy | KERNEL_VIRT_BASE);
    for (int i = 0; i < 512; i++) {
        if (pud[i]) {
            page_reclaim_pmd(pud[i] & PAGE_MASK);
        }
    }
    buddy_free(pud);
}

void page_reclaim(uint64_t pgd_phy) {
    uint64_t* pgd = (uint64_t*)(pgd_phy | KERNEL_VIRT_BASE);
    for (int i = 0; i < 512; i++) {
        if (pgd[i]) {
            page_reclaim_pud(pgd[i] & PAGE_MASK);
        }
    }
    buddy_free(pgd);
}

/*
 *  Interface for buddy system and object allocator
 */
void* kmalloc(uint64_t size) {
    // size too large
    if (size >= PAGE_SIZE) {
        int order;
        for (int i = 0; i < MAX_BUDDY_ORDER; i++) {
            if (size <= (uint64_t)((1 << i) * PAGE_SIZE)) {
                order = i;
                break;
            }
        }
        return buddy_alloc(order);
    }
    else {
        // check exist object allocator
        for (int i = 0; i < MAX_OBJ_ALLOCTOR_NUM; i++) {
            if ((uint64_t)obj_allocator[i].obj_size == size) {
                return (void*) obj_alloc_kernel(i);
            }
        }
        // register new obj allocator
        int token = obj_alloc_register(size);
        return obj_alloc_kernel(token);
    }
}

void kfree(void* addr) {
    for (int i = 0; i < MAX_OBJ_ALLOCTOR_NUM; i++) {
        struct pool_t pool = obj_allocator[i];
        for (int j = 0; j < pool.page_used; j++) {
            int addr_pfn = phy_to_pfn(virtual_to_physical((uint64_t)addr));
            int page_pfn = phy_to_pfn(virtual_to_physical(pool.page_addr[j]));
            if (addr_pfn == page_pfn) {
                obj_free(i, addr);
                return;
            }
        }
    }
    buddy_free(addr);
}

/*
 *  Obj allocator
 */
void pool_init(struct pool_t* pool, uint64_t size) {
    pool->obj_size = size;
    pool->obj_per_page = PAGE_SIZE / size;
    pool->page_used = 0;
    pool->obj_used = 0;
    pool->free = NULL;
}

int obj_alloc_register(uint64_t size) {
    if (size >= PAGE_SIZE) {
        uart_printf("Object too large\n");
        return -1;
    }

    // normalize size
    uint64_t size_per_allocator = PAGE_SIZE / MAX_OBJ_ALLOCTOR_NUM;
    uint64_t normalize_block = size / size_per_allocator;
    if (size % size_per_allocator) {
        normalize_block++;
    }
    int normalize_size = normalize_block * size_per_allocator;

    for (int i = 0; i < MAX_OBJ_ALLOCTOR_NUM; i++) {
        if (obj_allocator[i].used == USED && obj_allocator[i].obj_size == normalize_size) {
            return i;
        }
        else if (obj_allocator[i].used == AVAL) {
            obj_allocator[i].used = USED;
            pool_init(&obj_allocator[i], normalize_size);
            return i;
        }
    }

    uart_printf("No avaliable pool for current request\n");
    return -1;
}

void* obj_alloc_kernel(int token) {
    int pool_num = token;
    struct pool_t* pool = &obj_allocator[pool_num];

    // reused free obj
    if (pool->free != NULL) {
        struct free_list *obj = pool->free;
        pool->free = pool->free->next;
        return obj;
    }

    // need new page
    if (pool->obj_used >= pool->page_used * pool->obj_per_page) {
        pool->page_addr[pool->page_used] = (uint64_t) buddy_alloc(0);
        pool->page_used++;
    }

    // allocate new obj
    uint64_t addr = pool->page_addr[pool->page_used - 1] + pool->obj_used * pool->obj_size;
    pool->obj_used++;
    return (void*) addr;
}

void obj_free(int token, void* virt_addr) {
    int pool_num = token;
    struct pool_t* pool = &obj_allocator[pool_num];
    struct free_list* free_head = pool->free;
    pool->free = (struct free_list*) virt_addr;
    pool->free->next = free_head;
    pool->obj_used--;
}

/*
 *  Buddy System
 */

void buddy_push(struct buddy_t* bd, struct list_head* elmt) {
    bd->nr_free++;
    list_add_tail(elmt, &bd->head);
}

void buddy_remove(struct buddy_t* bd, struct list_head* elmt) {
    bd->nr_free--;
    list_del(elmt);
}

void buddy_info() {
    for (int i = 0; i < MAX_BUDDY_ORDER; i++) {
        uart_printf("order: %d %d\n", i, free_area[i].nr_free);
    }
}

struct page_t* buddy_release_redundant(struct page_t* p, int target_order) {
    int cur_order = p->order;
    // recursive release if not reach to target_order
    if (cur_order > target_order) {
        int prev_order = cur_order - 1;
        struct page_t* bottom = p;
        struct page_t* top = p + (1 << prev_order);
        bottom->order = prev_order;
        top->order = prev_order;
        buddy_push(&(free_area[prev_order]), &(bottom->list));
        return buddy_release_redundant(top, target_order);
    }
    // book keeping
    p->used = USED;
    return p;
}

struct page_t* buddy_pop(struct buddy_t* bd, int target_order) {
    if (list_empty(&(bd->head))) return NULL;
    bd->nr_free--;
    struct list_head* target = bd->head.next;
    list_del(target);
    struct page_t* target_page = list_entry(target, struct page_t, list);
    return buddy_release_redundant(target_page, target_order);
}

void buddy_merge(struct page_t *bottom, struct page_t *top, int order) {
    int new_order = order + 1;
    struct page_t *buddy = find_buddy(bottom->idx, new_order);
    // check buddy exist in new order, if exist => recursively merge
    if (buddy->used == AVAL && buddy->order == new_order) {
        buddy_remove(&free_area[new_order], &buddy->list);
        if (buddy > bottom)
            buddy_merge(bottom, buddy, new_order);
        else
            buddy_merge(buddy, bottom, new_order);
    }
    // merge bottom and top to same block
    else {
        bottom->used = AVAL;
        bottom->order = new_order;
        top->order = -1;
        buddy_push(&(free_area[new_order]), &(bottom->list));
    }
}

void* buddy_alloc(int order) {
    for (int i = order; i < MAX_BUDDY_ORDER; i++) {
        if (free_area[i].nr_free > 0) {
            struct page_t* p = buddy_pop(&free_area[i], order);
            uint64_t page_virt_addr = (p->idx * PAGE_SIZE) | KERNEL_VIRT_BASE;
            memzero((void*)page_virt_addr, (1 << order) * PAGE_SIZE);
            return (void*)page_virt_addr;
        }
    }
    return NULL;
}

void buddy_free(void* virt_addr) {
    int pfn = phy_to_pfn(virtual_to_physical((uint64_t)virt_addr));
    struct page_t *p = &page[pfn];
    int order = p->order;
    struct page_t *buddy = find_buddy(pfn, order);
    // can not merge, just free current order's pages
    if (buddy->used == USED || buddy->order != order) {
        p->used = AVAL;
        buddy_push(&(free_area[order]), &(p->list));
    }
    // merge buddy iteratively
    else {
        buddy_remove(&free_area[order], &buddy->list);
        if (buddy > p)
            buddy_merge(p, buddy, order);
        else
            buddy_merge(buddy, p, order);
    }
}

void buddy_init() {
    for (int i = 0; i < MAX_BUDDY_ORDER; i++) {
        free_area[i].head.next = &free_area[i].head;
        free_area[i].head.prev = &free_area[i].head;
        free_area[i].nr_free = 0;
    }
}

/*
 * Utility function
 */
uint64_t user_addr_to_page_addr(uint64_t user_addr, uint64_t pgd_phy) {
    uint64_t pgd_idx = (user_addr & (PD_MASK << PGD_SHIFT)) >> PGD_SHIFT;
    uint64_t pud_idx = (user_addr & (PD_MASK << PUD_SHIFT)) >> PUD_SHIFT;
    uint64_t pmd_idx = (user_addr & (PD_MASK << PMD_SHIFT)) >> PMD_SHIFT;
    uint64_t pte_idx = (user_addr & (PD_MASK << PTE_SHIFT)) >> PTE_SHIFT;

    uint64_t* pgd = (uint64_t*)(pgd_phy | KERNEL_VIRT_BASE);
    uint64_t* pud = (uint64_t*)((pgd[pgd_idx] & PAGE_MASK) | KERNEL_VIRT_BASE);
    uint64_t* pmd = (uint64_t*)((pud[pud_idx] & PAGE_MASK) | KERNEL_VIRT_BASE);
    uint64_t* pte = (uint64_t*)((pmd[pmd_idx] & PAGE_MASK) | KERNEL_VIRT_BASE);
    return (pte[pte_idx] & PAGE_MASK) | KERNEL_VIRT_BASE;
}

uint64_t virtual_to_physical(uint64_t virt_addr) {
    return (virt_addr << 16) >> 16;
}

uint64_t phy_to_pfn(uint64_t phy_addr) {
    return phy_addr >> 12;
}

void memzero(uint8_t* addr, int size) {
    for (int i = 0; i < size; i++) {
        *(addr + i) = 0;
    }
}

void memcpy(void* dest, void* src, uint64_t size) {
    uint8_t* csrc = (uint8_t*)src;
    uint8_t* cdest = (uint8_t*)dest;

    for (uint64_t i = 0; i < size; i++)
        cdest[i] = csrc[i];
}
struct page_t* find_buddy(int pfn, int order) {
    int buddy_pfn = pfn ^ (1 << order);
    return &page[buddy_pfn];
}
