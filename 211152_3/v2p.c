#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>

#define KB 1024
#define MB 1024*KB

/* 
 * You may define macros and other helper functions here
 * You must not declare and use any static/global variables 
 * */

int present_fault(int error_code) {
    return error_code & 1;
}

int write_fault(int error_code) {
    return (error_code >> 1) & 1;
}

int user_access(int error_code) {
    return (error_code >> 2) & 1;
}

int is_present(u64 pte) {
    return pte & 1;
}

int write_allowed(u64 pte) {
    return (pte >> 3) & 1;
}

int user_allowed(u64 pte) {
    return (pte >> 4) & 1;
}

static int debug_mmap = 0;

static int debug_alloc_failed = 0;
int alloc_failed(void *addr) {
    if((u64 *)addr == NULL || (u64 *)addr == (void *)-1) {
        if(debug_mmap || debug_alloc_failed) printk("[ALLOC_FAILED] Alloc Failed\n");
        return 1;
    }
    return 0;
}

static int debug_allocate_dummy_node = 0;
int allocate_dummy_node(struct exec_context *ctx) {  // Assign dummy node of size 4KB
    struct vm_area *dummy = (struct vm_area *)os_alloc(sizeof(struct vm_area));
    if(alloc_failed(dummy)) {
        return -EINVAL;
    }

    if(debug_mmap || debug_allocate_dummy_node) printk("[ALLOCATE_DUMMY_NODE] Dummy node allocated\n");
    
    
    dummy->vm_start = MMAP_AREA_START;
    dummy->vm_end = MMAP_AREA_START + 4*KB;
    dummy->access_flags = 0x0;
    dummy->vm_next = NULL;
    ctx->vm_area = dummy;
    stats->num_vm_area++;
    
    if(debug_mmap || debug_allocate_dummy_node) printk("[ALLOCATE_DUMMY_NODE] Dummy node assigned\n");
    
    return 0;
}

static int debug_insert_and_merge = 0;
u64 *insert_and_merge(struct exec_context *current, struct vm_area *new_node) {  // Assumes that node can fit in the given location
    u64 ret_addr = new_node->vm_start;


    if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] Inserting and merging node %x %x\n", new_node->vm_start, new_node->vm_end);
    
    
    struct vm_area *temp = current->vm_area;
    while(temp->vm_next != NULL) { // Find the right place
        if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] checking temp %x %x\n", temp->vm_start, temp->vm_end);
        
        if(temp->vm_next->vm_start >= new_node->vm_start) {
            if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] Found right place at VM Addr %x %x\n", temp->vm_start, temp->vm_end);
            break;
        }
        
        temp = temp->vm_next;
    }
    if(temp == NULL) {
        if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] temp is null\n");
        
        return (void *)-EINVAL;
    }


    if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] Found right place at VM Addr %x %x\n", temp->vm_start, temp->vm_end);
    
    
    if(temp->vm_next->vm_start == new_node->vm_start) {
        if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] Special condition called VM Addr %x %x %x %x\n", temp->vm_start, temp->vm_end, new_node->vm_start, new_node->vm_end);
        struct vm_area *temp2 = temp->vm_next;
        temp->vm_next = new_node->vm_next;
        new_node->vm_next = NULL;
    }


    // Merge temp and new_node if end of temp is start of new_node
    if(temp->vm_end == new_node->vm_start && temp->access_flags == new_node->access_flags) {
        if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] ! Merging temp %x %x %x %x and new_node %x %x %x\n", temp->vm_start, temp->vm_end, temp->vm_next, temp->vm_next->vm_start, new_node->vm_start, new_node->vm_end, new_node->vm_next);
        
        temp->vm_end = new_node->vm_end;
        os_free(new_node, sizeof(struct vm_area));
        stats->num_vm_area--;
        
    }
    else {
        if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] ! Inserting new_node %x %x %x %x %x %x\n", new_node->vm_start, new_node->vm_end, new_node->vm_next, temp->vm_start, temp->vm_end, temp->vm_next);
        
        
        new_node->vm_next = temp->vm_next;
        temp->vm_next = new_node;
        if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] ! Inserting new_node 2222 %x %x %x %x %x %x\n", new_node->vm_start, new_node->vm_end, new_node->vm_next, temp->vm_start, temp->vm_end, temp->vm_next);

        temp = new_node;


    }


    // Merge temp->vm_next if it is not null and end of temp is start of temp->vm_next
    if(temp->vm_next != NULL && temp->vm_end == temp->vm_next->vm_start && temp->access_flags == temp->vm_next->access_flags) {
        if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] Merging temp, start->%x end->%x and temp->vm_next %x %x\n", temp->vm_start, temp->vm_end, temp->vm_next->vm_start, temp->vm_next->vm_end);
        
        temp->vm_end = temp->vm_next->vm_end;
        struct vm_area *temp2 = temp->vm_next;
        temp->vm_next = temp->vm_next->vm_next;
        stats->num_vm_area--;

        os_free(temp2, sizeof(struct vm_area));
    }
    
    if(debug_mmap || debug_insert_and_merge) printk("[INSERT_AND_MERGE] # Inserted and merged node start->%x end->%x next->%x, returning %x\n", temp->vm_start, temp->vm_end, temp->vm_next, ret_addr);
    if(debug_mmap || debug_insert_and_merge && (temp->vm_next != NULL)) printk("NEXT VM START %x\n", temp->vm_next->vm_start);
    return (void *)ret_addr;
}

int free_range_pfn(struct exec_context *current, u64 addr, u64 end_addr) {
    for(u64 page_addr = addr; page_addr < end_addr; page_addr += 4*KB) {
        u64 *page_table_page = osmap(current->pgd);

        int ctr_off = 39;
        int offset1 = (page_addr >> 39) & (0x1ff);
        int offset2 = (page_addr >> 30) & (0x1ff);
        int offset3 = (page_addr >> 21) & (0x1ff);
        int offset4 = (page_addr >> 12) & (0x1ff);
        int offset[4] = {offset4, offset3, offset2, offset1};

        u64 offset_mask = ((0xff1) << ctr_off);
        u64 pfn_mask = 0xfffffffffffff000;

        for(int i = 3; i >= 0; i--) {
            u64 page_offset = ((offset_mask & page_addr) >> ctr_off);
            u64 *level = page_table_page + offset[i];
            u64 pte = *(level);

            if(is_present(pte) && i == 0) {
                u64 pfn = ((pte & pfn_mask) >> 12);

                if(get_pfn_refcount(pfn) == 1) {
                    put_pfn(pfn);
                    os_pfn_free(USER_REG, pfn);
                }
                else {
                    put_pfn(pfn);
                }
                
                *(level) = 0;

                break;
            }
            else if(!is_present(pte)) {
                break;
            }

            page_table_page = osmap((pte & pfn_mask) >> 12);
            offset_mask >>= 9;
            ctr_off -= 9;
        }

        asm volatile("invlpg (%0)" ::"r" (page_addr) : "memory");
    }
    return 0;
}

static int debug_vm_area_mprotect = 0;
int modify_range_pfn(struct exec_context *current, u64 addr, u64 end_addr, int enable_write) {
    if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Modifying addr %x end_addr %x enable_write %x\n", addr, end_addr, enable_write);
    
    for(u64 page_addr = addr; page_addr < end_addr; page_addr += 4*KB) {
        if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Modifying page_addr %x\n", page_addr);

        u64 *page_table_page = osmap(current->pgd);
        
        if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Page table pgd VA is %x and cr3 is %x PFN is %x\n", page_table_page, current->regs.entry_cs, current->pgd);

        int ctr_off = 39;
        int offset1 = (page_addr >> 39) & (0x1ff);
        int offset2 = (page_addr >> 30) & (0x1ff);
        int offset3 = (page_addr >> 21) & (0x1ff);
        int offset4 = (page_addr >> 12) & (0x1ff);
        int offset[4] = {offset4, offset3, offset2, offset1};

        u64 offset_mask = ((0xff1) << ctr_off);
        u64 pfn_mask = 0xfffffffffffff000;

        for(int i = 3; i >= 0; i--) {
            if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Current VA is %x and level is %d\n", page_table_page, i);
            
            u64 page_offset = (offset_mask & page_addr) >> ctr_off;
            u64 *level = page_table_page + offset[i];
            u64 pte = *(level);
            
            if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Page offset is %x PTE is %x\n", page_offset, pte);
            
            if(enable_write) {
                if(i == 0) {
                    if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Checking refcount of the page to be modified\n");
                    
                    u64 page_pfn = (pte & pfn_mask) >> 12;
                    
                    if(0) printk("[MODIFY_RANGE_PFN] Page PFN is %x refcount is %d\n", page_pfn, get_pfn_refcount(page_pfn));
                    
                    if(get_pfn_refcount(page_pfn) > 1) {
                        continue;
                    }
                    else {
                        if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Refcount is 1, so setting write bit\n");
                        
                        *(level) = pte | (1 << 3);
                    }
                }
                *(level) = pte | (1 << 3);
            }
            else if(i == 0) {   // ########### Set to i == 0 afterwards
                if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Checking refcount of the page to be modified\n");
                
                u64 page_pfn = (pte & pfn_mask) >> 12;
                
                if(0) printk("[MODIFY_RANGE_PFN] Page PFN is %x refcount is %d\n", page_pfn, get_pfn_refcount(page_pfn));

                if(get_pfn_refcount(page_pfn) > 1) {
                    continue;
                }
                else {
                    if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] Refcount is 1, so setting write bit\n");
                    
                    *(level) = pte | (1 << 3);
                }
                *(level) = pte & (~(1 << 3));
            }
            
            if(debug_vm_area_mprotect) printk("[MODIFY_RANGE_PFN] After modification page offset is %x PTE is %x\n", page_offset, *(level));

            if(!is_present(pte)) {
                break;
            }
            
            page_table_page = osmap((pte & pfn_mask) >> 12);
            offset_mask >>= 9;
            ctr_off -= 9;
        }
        asm volatile("invlpg (%0)" ::"r" (page_addr) : "memory");
    }
    return 1;
}
/**
 * mprotect System call Implementation.
 */

long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if(debug_vm_area_mprotect) ("[VM_AREA_MPROTECT] Entered\n");


    if(current == NULL) {
        return -EINVAL;
    }
    if((void *)addr == NULL) {
        return -EINVAL;
    }
    // if((void *)addr >= MMAP_AREA_START && (void *)addr < MMAP_AREA_START + 4*KB) {
    //     return -EINVAL;
    // }
    if(length <= 0) {
        return -EINVAL;
    }
    if(length > 2*MB) {
        return -EINVAL;
    }
    if(prot != PROT_READ && prot != (PROT_WRITE | PROT_READ)) {
        return -EINVAL;
    }


    length = (length + 4*KB - 1) & ~(4*KB - 1);  // Round up to nearest 4KB
    

    if(debug_mmap || debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] addr %x length %x prot %x\n", addr, length, prot);
    int vm_prot;
    // Initialize vm_protection according to the bit assignemnt
    if(prot == PROT_READ) {
        vm_prot = 1;
    }
    else if(prot == (PROT_WRITE | PROT_READ)) {
        vm_prot = 3;
    }
    else {
        return -EINVAL;
    }


    if(debug_mmap || debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] vm_prot %x\n", vm_prot);
    
    
    struct vm_area *temp = current->vm_area;
    struct vm_area *prev = NULL;
    while(temp != NULL) {
        // if(temp->access_flags == vm_prot) continue;
        if(debug_mmap || debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] temp %x %x %x\n", temp->vm_start, temp->vm_end, temp->access_flags);
        
        
        if(temp->access_flags != vm_prot && temp->access_flags != 0) {
            if(temp->vm_start < addr) {
                if(temp->vm_end <= addr) {
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Case 1 - 1\n");
                }
                else if(temp->vm_end >= addr && temp->vm_end <= addr + length) {
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Case 1 - 2\n");

                    // Set end of temp to addr and create a new node with start as addr and end as temp->vm_end and set its protection to vm_prot and insert and merge it
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Changing protection of temp %x %x %x\n", temp->vm_start, temp->vm_end, temp->access_flags);
                    
                    
                    modify_range_pfn(current, addr, temp->vm_end, (prot == (PROT_WRITE | PROT_READ)));
                    
                    u64 old_end = temp->vm_end;
                    temp->vm_end = addr;
                    
                    struct vm_area *new_node = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    stats->num_vm_area++;
                    if(alloc_failed(new_node)) {
                        return -EINVAL;
                    }

                    new_node->vm_start = addr;
                    new_node->vm_end = old_end;
                    new_node->access_flags = vm_prot;
                    new_node->vm_next = NULL;

                    if(insert_and_merge(current, new_node) == (void *)-EINVAL) {
                        return -EINVAL;
                    }
                }
                else {
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Case 1 - 3\n");

                    // Set end of temp to addr and create a new node with start as addr and end as addr + length and set its protection to vm_prot and insert and merge it
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Changing protection of temp %x %x %x to %x\n", temp->vm_start, temp->vm_end, temp->access_flags, prot);
                    modify_range_pfn(current, addr, addr + length, (prot == (PROT_WRITE | PROT_READ)));
                    u64 oldend = temp->vm_end;
                    temp->vm_end = addr;

                    struct vm_area *new_node = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    stats->num_vm_area++;
                    if(alloc_failed(new_node)) {
                        return -EINVAL;
                    }

                    new_node->vm_start = addr;
                    new_node->vm_end = addr + length;
                    new_node->access_flags = vm_prot;
                    new_node->vm_next = NULL;

                    if(insert_and_merge(current, new_node) == (void *)-EINVAL) {
                        return -EINVAL;
                    }


                    struct vm_area *new_node2 = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    stats->num_vm_area++;
                    if(alloc_failed(new_node2)) {
                        return -EINVAL;
                    }

                    new_node2->vm_start = addr + length;
                    new_node2->vm_end = oldend;
                    new_node2->access_flags = temp->access_flags;
                    new_node2->vm_next = NULL;

                    if(insert_and_merge(current, new_node2) == (void *)-EINVAL) {
                        return -EINVAL;
                    }
                    // if (vm_prot == temp->access_flags)
                    //     ;
                    // else
                    // {
                    //     struct vm_area *neww = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    //     if(neww==NULL || neww==(void*)-1) return -1;
                    //     stats->num_vm_area++;
                    //     neww->vm_start = addr;
                    //     neww->vm_end = addr+length;
                    //     neww->access_flags = vm_prot;
                    //     struct vm_area *new2 = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    //     if(neww==NULL || neww==(void*)-1) return -1;
                    //     stats->num_vm_area++;
                    //     new2->vm_start = addr+length;
                    //     new2->vm_end = temp->vm_end;
                    //     new2->access_flags = temp->access_flags;
                    //     temp->vm_end = addr;
                    //     new2->vm_next = temp->vm_next;
                    //     neww->vm_next = new2;
                    //     temp->vm_next = neww;
                    // }
                }
            }
            else if(temp->vm_start >= addr && temp->vm_start < addr + length) {
                if(temp->vm_end <= addr + length) {
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Case 2 - 2\n");

                    // Set start of temp to addr and set its protection to vm_prot
                    // temp->vm_start = addr;
                    modify_range_pfn(current, temp->vm_start, temp->vm_end, (prot == (PROT_WRITE | PROT_READ)));
                    
                    temp->access_flags = vm_prot;
                    struct vm_area* temp2= current->vm_area;
                    while(temp2->vm_next!=temp)temp2=temp2->vm_next;

                    if(temp2->access_flags == temp->access_flags && temp2->vm_end == temp->vm_start) {
                        temp2->vm_end = temp->vm_end;
                        temp2->vm_next = temp->vm_next;
                        
                        os_free(temp, sizeof(struct vm_area));
                        temp=temp2;
                        stats->num_vm_area--;
                    }
                     
                    if(temp->vm_next!=NULL && temp->access_flags==temp->vm_next->access_flags && temp->vm_end==temp->vm_next->vm_start){
                    temp->vm_end = temp->vm_next->vm_end;
                    struct vm_area* temp2 = temp->vm_next;
                    temp->vm_next = temp->vm_next->vm_next;
                    os_free(temp2, sizeof(struct vm_area));
                    stats->num_vm_area--;
                    }

                    // if(insert_and_merge(current, temp) == (void *)-EINVAL) {
                    //     return -EINVAL;
                    // }
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Changed protection of temp %x %x %x %x\n", temp->vm_start, temp->vm_end, temp->vm_next, temp->access_flags);
                }
                else {
                    if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Case 2 - 3\n");

                    // Set start of temp to addr and set its protection to vm_prot and create a new node with start as addr + length and end as temp->vm_end and set its protection to temp->access_flags and insert and merge it
                    // temp->vm_start = addr;
                    // prev->vm_next = temp->vm_next;
                    modify_range_pfn(current, temp->vm_start, addr + length, (prot == (PROT_WRITE | PROT_READ)));

                    int old_prot = temp->access_flags;
                    u64 old_end = temp->vm_end;
                    temp->access_flags = vm_prot;
                    temp->vm_end = addr + length;

                    struct vm_area* temp2= current->vm_area;
                    while(temp2->vm_next!=temp)temp2=temp2->vm_next;

                    if(temp2->access_flags == temp->access_flags && temp2->vm_end == temp->vm_start) {
                        temp2->vm_end = temp->vm_end;
                        temp2->vm_next = temp->vm_next;
                        
                        os_free(temp, sizeof(struct vm_area));
                        temp=temp2;
                        stats->num_vm_area--;
                    }
                     
                    if(temp->vm_next!=NULL && temp->access_flags==temp->vm_next->access_flags && temp->vm_end==temp->vm_next->vm_start){
                    temp->vm_end = temp->vm_next->vm_end;
                    struct vm_area* temp2 = temp->vm_next;
                    temp->vm_next = temp->vm_next->vm_next;
                    os_free(temp2, sizeof(struct vm_area));
                    stats->num_vm_area--;
                    }

                    struct vm_area *new_node = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    if(alloc_failed(new_node)) {
                        return -EINVAL;
                    }
                    stats->num_vm_area++;
                    
                    new_node->vm_start = addr + length;
                    new_node->vm_end = old_end;
                    new_node->access_flags = old_prot;
                    new_node->vm_next = NULL;
                    
                    if(insert_and_merge(current, new_node) == (void *)-EINVAL) {
                        return -EINVAL;
                    }
                }
            }
            else {
                if(debug_vm_area_mprotect) printk("[VM_AREA_MPROTECT] Case 3 -3\n");

                // Ignore
            }
        }
        prev = temp;
        temp = temp->vm_next;
    }
    return 0;
}

/**
 * mmap system call implementation.
 */
static int debug_vm_area_map = 0;
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
    if(current == NULL) {
        return -EINVAL;
    }
    // if(addr == NULL) {
    //     return -EINVAL;
    // }
    if(flags != MAP_FIXED && flags != 0) {
        return -EINVAL;
    }
    if(length <= 0) {
        return -EINVAL;
    }
    if(length > 2*MB) {
        return -EINVAL;
    }
    if(prot != PROT_READ && prot != (PROT_WRITE | PROT_READ)) {
        return -EINVAL;
    }
    if(current->vm_area == NULL) {
        // printk("NUM VM AREA %d %x\n", stats->num_vm_area, current->vm_area);
        if(allocate_dummy_node(current)) {  // Dummy node alloc failed
            return -EINVAL;
        }
        // printk("NUM VM AREA %d %x\n", stats->num_vm_area, current->vm_area);
    }
    if((void *)addr != NULL && (addr < MMAP_AREA_START || addr >= MMAP_AREA_END)) {
        return -EINVAL;
    }

    if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] addr %x length %x prot %x flags %x\n", addr, length, prot, flags);
    

    length = (length + 4*KB - 1) & ~(4*KB - 1);  // Round up to nearest 4KB


    int vm_prot = 0;
    // Initialize vm_protection according to the bit assignemnt
    if(prot == (PROT_READ)) {
        vm_prot = 0x1;
    }
    else if(prot == (PROT_WRITE | PROT_READ)) {
        vm_prot = 0x3;
    }
    else {
        return -EINVAL;
    }

    if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] vm_prot %x\n", vm_prot);
    
    
    if(flags == MAP_FIXED) {  // Map fixed - only assign if possible at addr
        if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] Map fixed\n");
        
        if((void *)addr == NULL) {  // NULL address not allwoed here
            return -EINVAL;
        }
        if(addr < MMAP_AREA_START || addr + length > MMAP_AREA_END) {  // Must be in range
            return -EINVAL;
        }
        
        
        struct vm_area *temp = current->vm_area;
        while(temp != NULL) {  // Look for overlapping vm_area
            if((temp->vm_start >= addr && temp->vm_start < addr + length) || (temp->vm_end > addr && temp->vm_end <= addr + length) || (temp->vm_start <= addr && temp->vm_end >= addr + length)) {
                if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] Overlapping vm_area %x %x\n", temp->vm_start, temp->vm_end);
                return -EINVAL;
            }
            temp = temp->vm_next;
        }

        // addr = temp->vm_end;
        // Mapping can be assigned at the given location
        if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] Mapping can be assigned at the given location\n");
        struct vm_area *new_node = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] BRUH THE POINTER IS %x\n", new_node);

        if(alloc_failed(new_node)) {
            return -EINVAL;
        }
        stats->num_vm_area++;

        new_node->vm_start = addr;
        new_node->vm_end = addr + length;
        new_node->access_flags = vm_prot;
        new_node->vm_next = NULL;

        return (u64) insert_and_merge(current, new_node);
    }
    else {
        if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] Mapping not fixed\n");

        if((void *)addr != NULL) {
            if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] addr not null %x\n", addr);
            
            u64 ret = vm_area_map(current, addr, length, prot, MAP_FIXED);
            // printk("%d")
            if(ret != -EINVAL) {
                return ret;
            }
        }

        // Locate the first free region of size length
        if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] addr null\n");
        struct vm_area *temp = current->vm_area;
        while(temp != NULL) {
            if(temp->vm_next == NULL) {
                if(temp->vm_end + length <= MMAP_AREA_END) {
                    break;
                }
            }
            else {
                if(temp->vm_next->vm_start - temp->vm_end >= length) {
                    break;
                }
            }
            temp = temp->vm_next;
        }

        if(temp == NULL) {
            return -EINVAL;
        }

        if(debug_mmap || debug_vm_area_map) printk("[VM_AREA_MAP] Found free region at %x\n", temp->vm_start);
        
        addr = temp->vm_end;

        struct vm_area *new_node = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        stats->num_vm_area++;
        // printk("!!!!!!!!! %d\n", stats->num_vm_area);
        if(alloc_failed(new_node)) {
            return -EINVAL;
        }

        new_node->vm_start = addr;
        new_node->vm_end = addr + length;
        new_node->access_flags = vm_prot;
        new_node->vm_next = NULL;

        return (u64)insert_and_merge(current, new_node);
    }
    return -EINVAL;
}

/**
 * munmap system call implemenations
 */
static int debug_vm_area_unmap = 0;
long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    if(current == NULL) {
        return -EINVAL;
    }
    if((void *)addr == NULL) {
        return -EINVAL;
    }
    // if((void *)addr >= MMAP_AREA_START && (void *)addr < MMAP_AREA_START + 4*KB) {
    //     return -EINVAL;
    // }
    if(length <= 0) {
        return -EINVAL;
    }
    if(length > 2*MB) {
        return -EINVAL;
    }

    if(debug_mmap || debug_vm_area_unmap) printk("[VM_AREA_UNMAP] addr %x length %x\n", addr, length);
    
    
    length = (length + 4*KB - 1) & ~(4*KB - 1);  // Round up to nearest 4KB

    struct vm_area *temp = current->vm_area;
    struct vm_area *prev = NULL;
    while(temp != NULL) {
        if(temp->access_flags == 0x0) {
            prev = temp;
            temp = temp->vm_next;
            continue;
        }
        if(temp->vm_start < addr) {
            if(temp->vm_end <= addr) {
                // Ignore
                // temp = temp->vm_next;
            }
            else if(temp->vm_end > addr && temp->vm_end <= addr + length) {
                u64 end_addr = temp->vm_end;

                // Free pages from addr to end_addr
                free_range_pfn(current, addr, end_addr);

                temp->vm_end = addr;
                // temp = temp->vm_next;
            }
            else {
                u64 end_addr = temp->vm_end;

                // Free pages from addr to addr + length
                free_range_pfn(current, addr, addr + length);

                temp->vm_end = addr;

                struct vm_area *new_node = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                if(alloc_failed(new_node)) {
                    return -EINVAL;
                }
                stats->num_vm_area++;
                
                new_node->vm_start = addr + length;
                new_node->vm_end = end_addr;
                new_node->access_flags = temp->access_flags;
                new_node->vm_next = NULL;

                if(insert_and_merge(current, new_node) == (void *)-EINVAL) {
                    return -EINVAL;
                }
            }
        }
        else if(temp->vm_start >= addr && temp->vm_start < addr + length) {
            if(temp->vm_end <= addr + length) {
                // Free pages from temp->vm_start to temp->vm_end
                // printk("hi %x %x %x %x\n", current->vm_area, prev, temp->vm_start, prev->vm_start);
                free_range_pfn(current, temp->vm_start, temp->vm_end);

                if(prev == NULL) {
                    current->vm_area = temp->vm_next;
                }
                else {
                    prev->vm_next = temp->vm_next;
                }

                // printk("%x %x\n", prev->vm_next->vm_start, temp->vm_start);
                struct vm_area *temp2 = temp->vm_next;
                os_free(temp, sizeof(struct vm_area));
                temp = temp2;
                stats->num_vm_area--;
                // printk("!!!!!!!!!!!%d!!!!!!!!!", stats->num_vm_area);
                continue;
                // temp = temp->vm_next;
            }
            else {
                // Free pages from temp->vm_start to addr + length
                free_range_pfn(current, temp->vm_start, addr + length);

                temp->vm_start = addr + length;

            }
        }
        else {
            // temp = temp->vm_next;
        }
        prev = temp;
        temp = temp->vm_next;
    }
    return 0;
}



/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */


static int debug_pagefault = 0;
static int debug2 = 0;
static int count = 0;
long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{   
    if(debug_pagefault) ("VM Area called %d\n", count++);

    if(current == NULL) {
        return -1;
    }
    if((void *)addr == NULL) {
        return -1;
    }

    if(debug2)printk("entered page walk\n");
    if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] addr %x error_code %x nm vm %d\n", addr, error_code, stats->num_vm_area);

    // Check if addr page fault corresponds to a valid VMA
    struct vm_area *vma = current->vm_area;
    while(vma != NULL) {
        if(addr >= vma->vm_start && addr < vma->vm_end) {  // VMA found 
            break;
        }
        vma = vma->vm_next;
    }

    if(vma == NULL) {
        if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] No VMA for address %x\n", addr);
        return -1;
    }
    
    if(debug2)printk("vma\n");

    if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] VMA for addr %x is from %x to %x and access %x\n", addr, vma->vm_start, vma->vm_end, vma->access_flags);
    
    u32 access_fl = vma->access_flags & (0x3);
    if((!(vma->access_flags & 0x2)) && write_fault(error_code)) { // Write not allowed but trying to write
        if(debug_pagefault) ("[VM_AREA_PAGEFAULT] Invalid access flags of VMA - Found %x but requested write\n", vma->access_flags);
        
        return -1;
    }

    // Handle unallocated pages
    if(debug2)printk("valid vma access checks pas\n");
 
  
    // if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Page table pgd VA is %x and cr3 is %x PFN is %x\n", page_table_page, current->regs.entry_cs, current->pgd);
    if(!present_fault(error_code)) {
        if(debug2)printk("not present page do page walk and allocate\n");
        
        u64 *page_table_page = osmap(current->pgd);
        
        if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Allocating new page\n");
        
        int ctr_off = 39;
        int offset1= (addr>>39)&(0x1ff);
        int offset2= (addr>>30)&(0x1ff);
        int offset3= (addr>>21)&(0x1ff);
        int offset4= (addr>>12)&(0x1ff);
        int offset[4]={offset4, offset3, offset2, offset1};
        
        u64 offset_mask = ((0xff1) << ctr_off);
        u64 pfn_mask = 0xfffffffffffff000;
        
        for(int i = 3; i >= 0; i--) {
            if(debug2)printk("at level i %d\n", i);
            if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Current VA is %x and level is %d\n", page_table_page, i);

            u64 page_offset = ((offset_mask & addr) >> ctr_off);
            u64 *level= page_table_page+ offset[i];
            u64 pte= *(level);

            if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Page offset is %x\n", page_offset);
            
            if(!is_present(pte)) {  // Allocate PFN and update entry
                if(debug2)printk("pte not present so allocate\n");

                int present_bit = 1;
                int write_bit = ((error_code >> 1) & 1) | ((pte >> 3) & 1);
                int access_bit = 1;
                u64 pfn = NULL;

                if(i != 0) {
                    pfn = os_pfn_alloc(OS_PT_REG);
                    if(!pfn) {
                        return -1;
                    }
                    if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Allocating under OS_PT_REG PFN %x\n", pfn);

                }
                else {
                    pfn = os_pfn_alloc(USER_REG); 
                    if(!pfn) {
                        return -1;
                    } 
                    if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Allocating under USER_REG PFN %x\n", pfn); 
                }
                if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] New PFN is %x\n", pfn);
                

                *(level) = (pfn << 12) | (present_bit << 0) | (write_bit << 3) | (access_bit << 4);
                
                
                if(debug_pagefault) printk("value stored at pte %x actual %x\n", *(level), (pfn << 12) | (present_bit << 0) | (write_bit << 3) | (access_bit << 4));
                if(debug2)printk("value stored at pte %x actual %x\n", *(page_table_page + page_offset), (pfn << 12) | (present_bit << 0) | (write_bit << 3) | (access_bit << 4));
            }
                               
            if(debug2)printk("pte rakh di ab\n");

            u64 new_pfn =  ( (*(level)) & pfn_mask ) >> 12;
            u64 *page_vaddr = osmap(new_pfn);

            if(debug2) printk("new VA %x\n", *page_vaddr);
            if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Next VA is %x\n", page_vaddr);
            if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] PFN at this PTE is %x\n", new_pfn);
            if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] PTE is %x\n", ((*(u64 *)(page_table_page + page_offset))));
            
            page_table_page = page_vaddr;
            offset_mask >>= 9;
            ctr_off -= 9;
        }

        if(debug2)printk("RETURNING\n");
        if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Allocated and created PTEs\n");
        
        
        return 1;
    }

    // Handle cow fault
    if(write_fault(error_code) && ((vma->access_flags & 0x3) == 0x3)) {  // COW fault
        if(debug_pagefault) printk("[VM_AREA_PAGEFAULT] Copy-on-write fault occured\n");
        return handle_cow_fault(current, addr, vma->access_flags);
    }
    return -1;
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the 
 * end of this function (e.g., setup_child_context etc.)
 */

static int debug_cfork = 0;

int copy_page_table(u64 *new_page, u64 *current_page, int level, u64 vm_addr) {
    if(debug_cfork) printk("[COPY_PAGE_TABLE] Current %x new %x level %d vm_addr %x\n", current_page, new_page, level, vm_addr);
    
    if(level == 4) {
        if(debug_cfork) printk("[COPY_PAGE_TABLE] Level 4 reached\n");
        return 0;
    } 
    
    u64 bit_offset = 39 - (9 * level);
    u64 offset_mask = (0x1ffll) << bit_offset;
    u64 offset = ((vm_addr & offset_mask) >> bit_offset);
    u64 permission_mask = 0xfff;
    u64 *old_pte = (current_page + offset);
    if(!is_present(*old_pte)) {
        if(debug_cfork) printk("[COPY_PAGE_TABLE] PTE not present level %d vm_addr %x\n", level, vm_addr);
        
        return 0;
    }
    
    if(debug_cfork) printk("[COPY_PAGE_TABLE] Offset mask is %x\n", offset_mask);
    if(debug_cfork) printk("[COPY_PAGE_TABLE] Loop started. Offset is %x\n", offset);
    if(debug_cfork) printk("[COPY_PAGE_TABLE] Permission mask alloced. Offset is %x\n", current_page+offset);
    if(debug_cfork) printk("[COPY_PAGE_TABLE] Old PTE is %x\n", *old_pte);
    
    u64 *new_pte = (new_page + offset);
    
    if(is_present(*old_pte) && level != 3 && !is_present(*new_pte)) {
        if(debug_cfork) printk("[COPY_PAGE_TABLE] Allocating new page table\n");
        
        u64 new_pfn = os_pfn_alloc(OS_PT_REG);
        if(!new_pfn) {
            return -1;
        }
        
        *new_pte = ((*new_pte) | ((new_pfn) << 12));
        *new_pte = ((*new_pte) | (((*old_pte) & permission_mask)));
    }
    else if(is_present(*old_pte) && level == 3) {
        (*old_pte) &= (~(1ll << 3));
        (*new_pte) &= (~(1ll << 3));

        (*new_pte) = ((*new_pte) | ((*old_pte) & (0xfffffffffffff000)));
        *new_pte = ((*new_pte) | (((*old_pte) & permission_mask)));
        
        if(debug_cfork) printk("[COPY_PAGE_TABLE] New PTE is %x old PTE is %x level is %d refcount is %d\n", *new_pte, *old_pte, level, get_pfn_refcount(((*old_pte) & (0xfffffffffffff000)) >> 12));
        
        get_pfn(((*old_pte) & (0xfffffffffffff000)) >> 12);
    }
    
    *new_pte = ((*new_pte) | (((*old_pte) & permission_mask)));
    
    if(debug_cfork) ("[COPY_PAGE_TABLE] New PTE is %x old PTE is %x level %d\n", *new_pte, *old_pte, level);
    
    asm volatile("invlpg (%0)" ::"r" (vm_addr) : "memory");
    // asm volatile("invlpg (%0)" ::"r" (page_addr) : "memory");

    if(is_present(*new_pte)) {
        u64 *new_pfn = ((*new_pte) >> 12);
        u64 *old_pfn = ((*old_pte) >> 12);

        if(copy_page_table(osmap(new_pfn), osmap(old_pfn), level + 1, vm_addr)) {
            return -1;
        }
        
        if(debug_cfork) printk("[COPY_PAGE_TABLE] Copied page table\n");
    }
    
    if(debug_cfork) printk("[COPY_PAGE_TABLE] Loop ended %d\n", level);
    
    
    return 0;
}


long do_cfork(){
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
     /* Do not modify above lines
     * 
     * */   
     /*--------------------- Your code [start]---------------*/

    pid = new_ctx->pid;
    int tmp = new_ctx->pid;

    memcpy(new_ctx, ctx, sizeof(struct exec_context));

    new_ctx->ppid = ctx->pid;
    new_ctx->pid = tmp;
    new_ctx->pgd = os_pfn_alloc(OS_PT_REG);

    if(!new_ctx->pgd) {
        return -1;
    }
    for(int i = 0; i < MAX_MM_SEGS; i++) {
        new_ctx->mms[i] = ctx->mms[i];
        if(i != MM_SEG_STACK) {
            if(debug_cfork) printk("[CFORK] Parent mms[%d] start %x end %x child mms[%d] start %x end %x\n", i, ctx->mms[i].start, ctx->mms[i].next_free, i, new_ctx->mms[i].start, new_ctx->mms[i].next_free);

            for(u64 addr = ctx->mms[i].start; addr < ctx->mms[i].next_free; addr += 4*KB) {
                // if(1) printk("[CFORK] mm address is %x\n", addr);
                long ret = copy_page_table(osmap(new_ctx->pgd), osmap(ctx->pgd), 0, addr);
                if(ret<0) return -1;
                // copy_page_table2(ctx, new_ctx->pgd, addr);
                // if(1) printk("[CFORK] mm address is %x %x\n", addr, *(u64 *)addr);
            }
        }
        else{
            // if(1) printk("################[CFORK] Parent mms[%d] start %x end %x child mms[%d] start %x end %x\n", i, ctx->mms[i].start, ctx->mms[i].end, i, new_ctx->mms[i].start, new_ctx->mms[i].end);

            for(u64 addr = ctx->mms[i].start; addr < ctx->mms[i].end; addr += 4*KB) {
                //if(1) printk("$$$$$$$[CFORK] mm address is %x\n", addr);
                long ret = copy_page_table(osmap(new_ctx->pgd), osmap(ctx->pgd), 0, addr);
                if(ret<0) return -1;
                // copy_page_table2(ctx, new_ctx->pgd, addr);
            }
        }
    }

    if(debug_cfork) ("[CFORK] After MM copy\n");

    struct vm_area *temp = ctx->vm_area;
    struct vm_area *newctx = NULL;
    new_ctx->vm_area = NULL;
    while(temp != NULL) {
        struct vm_area *temp2 = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        temp2->vm_start = temp->vm_start;
        temp2->vm_end = temp->vm_end;
        temp2->access_flags = temp->access_flags;
        temp2->vm_next = NULL;

        if(debug_cfork) printk("[CFORK] Parent vm_area %x %x %x\n", temp->vm_start, temp->vm_end, temp->access_flags);
        if(debug_cfork) printk("[CFORK] Parent vm_area %x %x %x\n", temp2->vm_start, temp2->vm_end, temp2->access_flags);
        
        if(new_ctx->vm_area == NULL) {
            new_ctx->vm_area = temp2;
            newctx = temp2;
        }
        else{
            newctx->vm_next = temp2;
            newctx = temp2;
        }

        for(u64 addr = temp2->vm_start; addr < temp2->vm_end; addr += 4*KB) {
            long ret = copy_page_table(osmap(new_ctx->pgd), osmap(ctx->pgd), 0, addr);
            if(ret<0) return -1;
            if(debug_cfork) printk("[CFORK] vm address allocated %xn", addr);
        }
        temp = temp->vm_next;
    }
    if(debug_cfork) printk("[CFORK] After VM copy\n");

    //     struct vm_area *temp_parent = ctx->vm_area;
    // new_ctx->vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
    // new_ctx->vm_area->vm_start = MMAP_AREA_START;
    // new_ctx->vm_area->access_flags = 0;
    // new_ctx->vm_area->vm_end = MMAP_AREA_START + 4 * 1024;
    // struct vm_area *temp_child = new_ctx->vm_area;
    // temp_parent = temp_parent->vm_next;
   
    
    // while (temp_parent != NULL)
    // {
    //     temp_child->vm_next = (struct vm_area *)os_alloc(sizeof(struct vm_area));
    //     temp_child = temp_child->vm_next;
    //     temp_child->vm_start = temp_parent->vm_start;
    //     temp_child->vm_end = temp_parent->vm_end;
    //     temp_child->access_flags = temp_parent->access_flags;
    //     temp_parent = temp_parent->vm_next;
    //     u64 addr = temp_child->vm_start;
    //     while (addr < temp_child->vm_end)
    //     {
    //         copy_page_table(osmap(new_ctx->pgd), osmap(ctx->pgd), 0, addr);
    //         addr += 4096;
    //     }
    // }
   
    //    if(0)printk("[ DO CFORK]  vm area copied \n");


    // memcpy(new_ctx->name, ctx->name, sizeof(new_ctx->name));
    // memcpy(new_ctx->ctx_threads, ctx->ctx_threads, sizeof(ctx->ctx_threads));

    // Inse 
    //new_ctx->os_rsp = ctx->os_rsp;
    //new_ctx->os_stack_pfn = os_pfn_alloc(OS_PT_REG);  /////////// CHECK LATER!!!!!!!
    //if(debug_cfork) printk("[CFORK] Parent os_rsp %x child os_rsp %x\n", ctx->os_rsp, new_ctx->os_rsp);
     /*--------------------- Your code [end] ----------------*/
    
     /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    
    
    // struct file *filep = new_ctx->files[1];
    // if(filep && filep->fops && filep->fops->write)
    //     filep->fops->write(filep, "console op\n", 11);
    // else
    //     printk("Something is null filep %x\n", filep);

    // filep = ctx->files[1];
    // if(filep && filep->fops && filep->fops->write)
    //     filep->fops->write(filep, "console op\n", 11);
    // else
    //     printk("Something is null\n");
    //if(1) printk("[CFORK] pid = %d\n", pid);

    // Print state of each register in context
    // if(debug_cfork) printk("[CFORK] R15 %x R14 %x R13 %x R12 %x R11 %x R10 %x R9 %x R8 %x RDI %x RSI %x RBP %x RBX %x RDX %x RCX %x RAX %x RIP %x CS %x EFLAGS %x RSP %x SS %x\n", new_ctx->regs.r15, new_ctx->regs.r14, new_ctx->regs.r13, new_ctx->regs.r12, new_ctx->regs.r11, new_ctx->regs.r10, new_ctx->regs.r9, new_ctx->regs.r8, new_ctx->regs.rdi, new_ctx->regs.rsi, new_ctx->regs.rbp, new_ctx->regs.rbx, new_ctx->regs.rdx, new_ctx->regs.rcx, new_ctx->regs.rax, new_ctx->regs.entry_rip, new_ctx->regs.entry_cs, -1, new_ctx->regs.entry_rsp, new_ctx->regs.entry_ss);
    // Print the above for ctx
    // if(debug_cfork) printk("[CFORK] R15 %x R14 %x R13 %x R12 %x R11 %x R10 %x R9 %x R8 %x RDI %x RSI %x RBP %x RBX %x RDX %x RCX %x RAX %x RIP %x CS %x EFLAGS %x RSP %x SS %x\n", ctx->regs.r15, ctx->regs.r14, ctx->regs.r13, ctx->regs.r12, ctx->regs.r11, ctx->regs.r10, ctx->regs.r9, ctx->regs.r8, ctx->regs.rdi, ctx->regs.rsi, ctx->regs.rbp, ctx->regs.rbx, ctx->regs.rdx, ctx->regs.rcx, ctx->regs.rax, ctx->regs.entry_rip, ctx->regs.entry_cs, -1, ctx->regs.entry_rsp, ctx->regs.entry_ss);
    return pid;

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}



/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data) 
 * it is called when there is a CoW violation in these areas. 
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

static int debug_cow = 0;
int fix_cow_fault(struct exec_context *current, u64 addr, struct mm_segment *mems, struct vm_area *vma) {
    if(debug_cow) printk("[FIX_COW_FAULT] Fixing cow - %d %d\n", stats->num_vm_area, current->pid);
    
    u64 offset = 39;
    u64 pfn_mask = 0xfffffffffffff000;
    u64 offset_mask = 0x1ff;
    u64 prot_mask = 0xfff;
    u64 pt_pfn = current->pgd;
    u64 access_flags = 0;
    
    if(mems == NULL) {
        access_flags = (vma->access_flags & 0x3);
    }
    else {
        access_flags = (mems->access_flags & 0x3);
    }
    for(int i = 0; i < 4; i++) {
        u64 *page_table_vaddr = osmap(pt_pfn);
        u64 page_table_offset = ((addr >> offset) & offset_mask);
        u64 *pte = page_table_vaddr + page_table_offset;

        if(!is_present(*pte)) {
            if(debug_cow) printk("[FIX_COW_FAULT] Page table not present or write not allowed at level %d\n", i);
            
            return -1;
        }

        if(i == 3) {
            u64 user_pfn = ((*pte) & pfn_mask) >> 12;
            u64 refcount = get_pfn_refcount(user_pfn);
            
            if(debug_cow) printk("[FIX_COW_FAULT] User PFN is %x PTE is %x refcount is %d num vm_count %d level is %d\n", user_pfn, *pte, refcount, stats->num_vm_area, i);
            
            if(refcount > 1) {
                u64 new_pfn = os_pfn_alloc(USER_REG);
                if(!new_pfn) {
                    return -1;
                }
                memcpy(osmap(new_pfn), osmap(user_pfn), 4*KB);

                put_pfn(user_pfn);
                user_pfn = new_pfn;
                
                if(debug_cow) printk("[FIX_COW_FAULT] New PFN is %x\n", user_pfn);
            }

            if(access_flags & 0x2) {
                *pte |= (1ll << 3);
            }

            *(pte) = ((*pte) & prot_mask) | (user_pfn << 12);

            if(debug_cow) printk("[FIX_COW_FAULT] After fixing PTE is %x\n", *pte);
        }
        else {
            pt_pfn = ((*pte) & pfn_mask) >> 12;
        }

        offset -= 9;
    }

    if(debug_cow) printk("[FIX_COW_FAULT] Fixed cow num vm_count %d\n", stats->num_vm_area);
    asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
    if(debug_cow) printk("[FIX_COW_FAULT] Fixed cow %d\n", stats->num_vm_area);

    return 1;
}


long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
    if(debug_cow) printk("[HANDLE_COW_FAULT] Entered vm_count is %d\n", stats->num_vm_area);
    
    if(current == NULL) {
        return -1;
    }
    if(vaddr == NULL) {
        return -1;
    }
    
    if(debug_cow) printk("[HANDLE_COW_FAULT] vaddr %x access_flags %x\n", vaddr, access_flags);

    // Check if memory segment code
    for(int i = 0; i < MAX_MM_SEGS; i++) {
        if(i != MM_SEG_STACK && current->mms[i].start <= vaddr && current->mms[i].next_free > vaddr) {
            if(debug_cow) printk("[HANDLE_COW_FAULT] Memory segment %d\n", i);
            
            return fix_cow_fault(current, vaddr, &(current->mms[i]), NULL);
            break;
        }
        if(i == MM_SEG_STACK && current->mms[i].start <= vaddr && current->mms[i].end > vaddr) {
            if(debug_cow) printk("[HANDLE_COW_FAULT] Memory segment %d\n", i);
            
            return fix_cow_fault(current, vaddr, &(current->mms[i]), NULL);
            break;
        }
    }
    
    // Check if addr page fault corresponds to a valid VMA
    struct vm_area *vma = current->vm_area;
    while(vma != NULL) {
        if(vaddr >= vma->vm_start && vaddr < vma->vm_end) {  // VMA found 
            if(debug_cow) printk("[HANDLE_COW_FAULT] VMA found %d\n", stats->num_vm_area);
            
            return fix_cow_fault(current, vaddr, NULL, vma);
            break;
        }
        vma = vma->vm_next;
    }
    
    if(debug_cow) printk("[HANDLE_COW_FAULT] No VMA or MM found\n");
    
    
    return -1;
}

