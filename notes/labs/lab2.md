The virtual memory layout is in `inc/memlayout.h`. The kernel stacks are below `0xf0000000`, above which the 256MB kernel physical memory is remapped.

# Part 1: Physical Page Management
Need physical memory allocation to store page tables for later virtual memory implementation.

### Exercise 1
`boot_alloc()` allocates pages of contiguous physical memory. (Not sure how to figure out what to return when `n != 0` and what to do to allocate but NOT initialize. Why no malloc. (this have something to do with its usage as is shown and used in `mem_init()`). Where is that `end`. (The kernel was loaded at `0xf0000000`. Its global variables and memory allocated by `boot_alloc()` afterwards should be in this region ?))\
`mem_init()` creates one initial page `kern_pgdir` as a page directory. This is stores at virtual address `UVPT`, so the index (`PDX(UVPT)`) of `kern_pgdir` should be the physical address of itself. `kern_pgdir` is the size of 4096 (`PGSIZE`), which exactly has 1024 entries for 32-bit addresses (?). `struct PageInfo` is not physical address itself, but could be identity-mapped to physical pages. The `i`th PageInfo corresponds to the `i`th page, so the physical address of that page should be `(current_pageinfo - pages) << PGSHIFT`, which is calculated in `page2pa` function in `kern/pmap.h`.\
`page_init()`. Here we are dealing with the first 256MB physical memory, which is mapped to `0xf0000000` in virtual address. The kernel should already been in the physical memory after the extended memory `EXTPHYSMEM`, and some other things are also in. So use boot_alloc(0) to find the first free virtual address.\
`page_alloc()` and `page_free()`. Just pop from and push into the `page_free_list`. Not changing the `pp_ref`, and take care of `pp_link`.

# Part 2: Virtual Memory
## Virtual, Linear, and Physical Addresses
The virtual address is translated into linear address by segmentation mechanism. The linear address is translated into physical address by paging mechanism. Here we only focus on paging.

We use `uintptr_t` and `phyaddr_t` to distinguish virtual and physical address. All pointers are virtual addresses. To cast a `phyaddr_t` and dereference might result in unintended results.
1. Should be `uintptr_t` since there's derefence.

We could use `KADDR` and `PADDR` to convert between virtual and physical address. 

## Reference counting
One physical page might be mapped to multiple virtual addresses. `pp_ref` keeps counting the number of references. It going to zero means it's free. The mappings above `UTOP` are mostly set up at boot time and should never be freed. So the counter basically works for those physical pages appearing below `UTOP`. Page directory pages (?)

## Page Table Management
### Exercise 4
`pgdir_walk()`. Note that we only care about the page table entry, but not caring about the actual physical address the input `va` tries to map. Also, every page table is exactly an actual page (which is what `page table page` means in the comment). And be aware that the page directory also stores physical addresses (consistent with page tables (?)). Here only the high 20 bits matters (the start address of one page) where the low 12 bits are some flags. `page_alloc`'s flag should be 1 to initialize the physical memory.\
`boot_map_region()`. Use `pgdir_walk` to get the corresponding page table entry. The mapping granularity should be "page". This might be done with only one page directory entry and several contiguous page table entries if `size` is not large. \
`page_insert()`. We only need to check whether the presence bit in the page table is 1, no matter this page table had already existed or has just been allocated by `pgdir_walk` because the result from `pgdir_walk` is also a page table with the presence bit cleared. The tricky part about the corner case is that if we remove the page first, that page may end up in the free page list, and after we insert it back and increase `pp_ref`, one page in the free list will have non-zero reference counter. So it's better to increase `pp_ref` before `page_remove` to make sure `pp` not go back to the free list.\
`page_lookup()`. During insertion we directly use `create = 1` for `pgdir_walk` because eventually we need to make an entry exist, so it's "create if not there", so no need to check existence by setting `create = 0`. Here we don't need ensured existence, so only use `create = 0` to lookup (1 might also do). Note that every time to use the page entry as physical address, `PTE_ADDR` is needed for removing low bit flags.\
`page_remove()`. If a parameter is a double pointer, we can just pass a reference of a pointer to it. Passing a reference also means that double pointer is not `NULL`.