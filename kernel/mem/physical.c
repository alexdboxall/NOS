
#include <arch.h>
#include <physical.h>
#include <diskcache.h>
#include <common.h>
#include <spinlock.h>
#include <assert.h>
#include <string.h>
#include <irql.h>
#include <log.h>
#include <virtual.h>
#include <panic.h>

static struct spinlock phys_lock;

/*
 * One bit per page. Lower bits refer to lower pages. A clear bit indicates
 * the page is unavailable (allocated / non-RAM), and a set bit indicates the
 * page is free.
 */
#define MAX_MEMORY_PAGES (ARCH_MAX_RAM_KBS / ARCH_PAGE_SIZE * 1024) 
#define BITS_PER_ENTRY (sizeof(size_t) * 8)
#define BITMAP_ENTRIES (MAX_MEMORY_PAGES / BITS_PER_ENTRY)
static size_t allocation_bitmap[BITMAP_ENTRIES];

/*
 * Stores pages that are available for us to allocate. If set to NULL, then we
 * have yet to reinitialise the physical memory manager. The stack grows upward, 
 * and the pointer is incremented after writing the value on a push. The stack 
 * stores physical page numbers (indexes) instead of addresses.
 */
static size_t* allocation_stack = NULL;
static size_t allocation_stack_pointer = 0;

/*
 * Once we get below this number, we will start evicting pages.
 */
#define NUM_EMERGENCY_PAGES 32

/*
 * The number of physical pages available (free) remaining, and total, in the 
 * system.
 */
static size_t pages_left = 0;
static size_t total_pages = 0;

/*
 * The highest physical page number that exists on this system. Gets set during 
 * InitPhys() when scanning the system's memory map.
 */
static size_t highest_valid_page_index = 0;

static inline bool IsBitmapEntryFree(size_t index) {
    size_t base = index / BITS_PER_ENTRY;
    size_t offset = index % BITS_PER_ENTRY;
    return allocation_bitmap[base] & (1 << offset);
}

static inline void AllocateBitmapEntry(size_t index) {
    assert(IsBitmapEntryFree(index));

    size_t base = index / BITS_PER_ENTRY;
    size_t offset = index % BITS_PER_ENTRY;
    allocation_bitmap[base] &= ~(1 << offset);
}

static inline void DeallocateBitmapEntry(size_t index) {
    assert(!IsBitmapEntryFree(index));

    size_t base = index / BITS_PER_ENTRY;
    size_t offset = index % BITS_PER_ENTRY;
    allocation_bitmap[base] |= 1 << offset;
}

static inline void PushIndex(size_t index) {
    assert(index <= highest_valid_page_index);
    allocation_stack[allocation_stack_pointer++] = index;
}

static inline size_t PopIndex(void) {
    assert(allocation_stack_pointer != 0);
    return allocation_stack[--allocation_stack_pointer];
}

/*
 * Removes an entry from the stack by value. Only to be used when absolutely 
 * required, as it has O(n) runtime and is therefore very slow. 
 */
static void RemoveStackEntry(size_t index) {
    for (size_t i = 0; i < allocation_stack_pointer; ++i) {
        if (allocation_stack[i] == index) {
            memmove(allocation_stack + i, allocation_stack + i + 1, (--allocation_stack_pointer - i) * sizeof(size_t));
            return;
        }
    }
}

/**
 * Deallocates a page of physical memory that was allocated with AllocPhys(). 
 * Does not affect virtual mappings - that should be taken care of before
 * deallocating. Address must be page aligned.
 */
void DeallocPhys(size_t addr) {
    MAX_IRQL(IRQL_SCHEDULER);
    assert(addr % ARCH_PAGE_SIZE == 0);

    size_t page = addr / ARCH_PAGE_SIZE;

    AcquireSpinlockIrql(&phys_lock);

    ++pages_left;
    DeallocateBitmapEntry(page);
    if (allocation_stack != NULL) {
        PushIndex(page);
    }
    ReleaseSpinlockIrql(&phys_lock);

    if (pages_left > NUM_EMERGENCY_PAGES * 2) {
        SetDiskCaches(DISKCACHE_NORMAL);
    }
}

/**
 * Deallocates a section of physical memory that was allocated with AllocPhysContinuous(). The entire block
 * of memory must be deallocated at once, i.e. the start address of the memory should be passed in. Does not
 * affect virtual mappings - that should be taken care of before deallocating.
 * 
 * @param addr The address of the section of memory to deallocate. Must be page-aligned.
 * @param size The size of the allocation. This should be the same value that was passed into AllocPhysContinuous().
 */
void DeallocPhysContiguous(size_t addr, size_t bytes) {
    for (size_t i = 0; i < BytesToPages(bytes); ++i) {
        DeallocPhys(addr);
        addr += ARCH_PAGE_SIZE;
    }
}

static void EvictPagesIfNeeded(void*) {
    EXACT_IRQL(IRQL_STANDARD);

    extern int handling_page_fault;
    if (handling_page_fault > 0) {
        return;
    }

    if (pages_left < NUM_EMERGENCY_PAGES) {
        SetDiskCaches(DISKCACHE_TOSS);

    } else if (pages_left < NUM_EMERGENCY_PAGES * 3 / 2) {
        SetDiskCaches(DISKCACHE_REDUCE);
    }

    int timeout = 0;
    while (pages_left < NUM_EMERGENCY_PAGES && timeout < 5) {
        handling_page_fault++;
        EvictVirt();
        handling_page_fault--;
        ++timeout;
    }
}

size_t AllocPhys(void) {
    MAX_IRQL(IRQL_SCHEDULER);

    AcquireSpinlockIrql(&phys_lock);

    if (pages_left == 0) {
        Panic(PANIC_OUT_OF_PHYS);
    }
    if (pages_left <= NUM_EMERGENCY_PAGES) {
        DeferUntilIrql(IRQL_STANDARD, EvictPagesIfNeeded, NULL);
    }

    size_t index = 0;
    if (allocation_stack == NULL) {
        /*
         * No stack yet, so must use the bitmap. No point optimising this as
         * only used during boot.
         */
        while (!IsBitmapEntryFree(index)) {
            index = (index + 1) % MAX_MEMORY_PAGES;
        }
    } else {
        index = PopIndex();
    }

    AllocateBitmapEntry(index);
    --pages_left;
    ReleaseSpinlockIrql(&phys_lock);

    return index * ARCH_PAGE_SIZE;
}

/**
 * Allocates a section of contigous physical memory, that may or may not have 
 * requirements as to where the memory can be located. Must only be called after
 * a call to ReinitPhys() is made. Deallocation should be done by 
 * DeallocPhysContiguous(), passing in the same size value as passed into 
 * AllocPhysContiguous() on allocation. Will not cause pages to be evicted from 
 * RAM, so sufficient memory must exist on the system for this to succeed.
 *
 * @param bytes The size of the allocation, in bytes.
 * @param min_addr Allocated memory will not contain any addresses lower than 
 *                 this value.
 * @param max_addr Allocated memory will not contain any addresses greater or 
 *                 equal to this value. For no maximum, set to 0.
 * @param boundary Allocated memory will not contain any addresses that are an 
 *                 integer multiple of this value (although it may start at an 
 *                 integer multiple of this address). If there are no boundary 
 *                 requirements, set this to 0.
 * @return The start address of the returned physical memory area. If the 
 *         request could not be satisfied, 0 is returned.
 */
size_t AllocPhysContiguous(size_t bytes, size_t min_addr, size_t max_addr, size_t boundary) {
    /*
     * This function should not be called before the stack allocator is setup.
     * (There is no need for InitVirt() to use this function, and so checking 
     * here removes a check that would have to be done in a loop later).
     */
    if (allocation_stack == NULL) {
        return 0;
    }

    size_t pages = BytesToPages(bytes);
    size_t min_index = (min_addr + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    size_t max_index = max_addr == 0 ? highest_valid_page_index + 1 : max_addr / ARCH_PAGE_SIZE;
    size_t count = 0;

    AcquireSpinlockIrql(&phys_lock);

    /*
     * We need to check we won't try to over-allocate memory, or allocate so
     * much memory that it puts us in a critical position.
     */
    if (pages + NUM_EMERGENCY_PAGES >= pages_left) {
        ReleaseSpinlockIrql(&phys_lock);
        return 0;
    }

    for (size_t index = min_index; index < max_index; ++index) {
        /*
         * Reset the counter if we are no longer contiguous, or if we have cross
         * a boundary that we can't cross.
         */
        if (!IsBitmapEntryFree(index) || (boundary != 0 && (index % (boundary / ARCH_PAGE_SIZE) == 0))) {
            count = 0;
            continue;
        }

        ++count;
        if (count == pages) {
            /*
             * Go back to the start of the section and mark it all as allocated.
             */
            size_t start_index = index - count + 1;
            while (start_index <= index) {
                AllocateBitmapEntry(start_index);
                RemoveStackEntry(start_index);
                ++start_index;
            }

            ReleaseSpinlockIrql(&phys_lock);
            return start_index * ARCH_PAGE_SIZE;
        }
    }

    ReleaseSpinlockIrql(&phys_lock);
    return 0;
}

/**
 * Initialises the physical memory manager for the first time. Must be called 
 * before any other memory management function is called. It determines what 
 * memory is available on the system  and prepares the O(n) bitmap allocator. 
 * This will be slow, but is only needed until ReinitHeap() gets called.
 */
void InitPhys(void) {
    InitSpinlock(&phys_lock, "phys", IRQL_SCHEDULER);

	/*
	* Scan the memory tables and fill in the memory that is there.
	*/
	while (true) {
		struct arch_memory_range* range = ArchGetMemory();

		if (range == NULL) {
			/* No more memory exists */
			break;

		} else {
			/* 
			* Must round the start address up so we don't include memory outside
            * the region.
            */
			size_t first_page = (range->start + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
			size_t last_page = (range->start + range->length) / ARCH_PAGE_SIZE;

			while (first_page < last_page && first_page < MAX_MEMORY_PAGES) {
                DeallocateBitmapEntry(first_page);
                ++pages_left;
                ++total_pages;

                if (first_page > highest_valid_page_index) {
                    highest_valid_page_index = first_page;
                }

                ++first_page;
			}
		}
	}
}

static void ReclaimBitmapSpace(void) {
    /*
     * Save extra physical memory on by deallocating the memory in the bitmap 
     * that can't be reached (due to the system not having memory that goes up 
     * that high).
     */
    size_t unreachable_pages = MAX_MEMORY_PAGES - (highest_valid_page_index + 1);
    size_t unreachable_entries = unreachable_pages / BITS_PER_ENTRY;
    size_t unreachable_bitmap_pages = unreachable_entries / ARCH_PAGE_SIZE;

    size_t end_bitmap = ((size_t) allocation_bitmap) + sizeof(allocation_bitmap);
    
    /*
     * Round down, otherwise other kernel data in the same page as the end of 
     * the bitmap  will also be counted as 'free', causing memory corruption.
     */
    size_t unreachable_region = ((end_bitmap - ARCH_PAGE_SIZE * unreachable_bitmap_pages)) & ~(ARCH_PAGE_SIZE - 1);

    while (num_unreachable_bitmap_pages--) {
        DeallocPhys(ArchVirtualToPhysical(unreachable_region));
        unreachable_region += ARCH_PAGE_SIZE;
        ++total_pages;
    }
}

/**
 * Reinitialises the physical memory manager with a constant-time page allocation system.
 * Must be called after virtual memory has been initialised. Must only be called once. Must
 * be called before calling AllocPageContigous() is called. Should be called as soon as
 * possible after virtual memory is available.
 */
void ReinitPhys(void) {
    assert(allocation_stack == NULL);

    allocation_stack = (size_t*) MapVirt(0, 0, (highest_valid_page_index + 1) * sizeof(size_t), VM_READ | VM_WRITE | VM_LOCK, NULL, 0);
    
    for (size_t i = 0; i < MAX_MEMORY_PAGES; ++i) {
        if (IsBitmapEntryFree(i)) {
            PushIndex(i);
        }
    }

    ReclaimBitmapSpace();
}

size_t GetTotalPhysKilobytes(void) {
    return total_pages * (ARCH_PAGE_SIZE / 1024);
}

size_t GetFreePhysKilobytes(void) {
    return pages_left * (ARCH_PAGE_SIZE / 1024);
}