The virtual memory layout is in `inc/memlayout.h`. The kernel stacks are below `0xf0000000`, above which the 256MB kernel physical memory is remapped.

# Part 1: Physical Page Management
Need physical memory allocation to store page tables for later virtual memory implementation.

### Exercise 1
`boot_alloc()` allocates pages of contiguous physical memory. (Not sure how to figure out what to return when `n != 0` and what to do to allocate but NOT initialize. Why no malloc. (this have something to do with its usage as is shown and used in `mem_init()`). Where is that `end`. (?))\
`mem_init()` creates one initial page `kern_pgdir` as a page directory. This is stores at virtual address `UVPT`, so the index (`PDX(UVPT)`) of `kern_pgdir` should be the physical address of itself. `kern_pgdir` is the size of 4096 (`PGSIZE`), which exactly has 1024 entries for 32-bit addresses (?). `struct PageInfo` is not physical address itself, but could be identity-mapped to physical pages. The `i`th PageInfo corresponds to the `i`th page, so the physical address of that page should be `(current_pageinfo - pages) << PGSHIFT`, which is calculated in `page2pa` function in `kern/pmap.h`.\
`page_init()`. Here we are dealing with the first 256MB physical memory, which is mapped to `0xf0000000` in virtual address. The kernel should already been in the physical memory after the extended memory `EXTPHYSMEM`, and some other things are also in. So use boot_alloc(0) to find the first free virtual address.\
`page_alloc()` and `page_free()`. Just pop from and push into the `page_free_list`. Not changing the `pp_ref`, and take care of `pp_link`.
