The virtual memory layout is in `inc/memlayout.h`. The kernel stacks are below `0xf0000000`, above which the 256MB kernel physical memory is remapped.

# Part 1: Physical Page Management
Need physical memory allocation to store page tables for later virtual memory implementation.

### Exercise 1
`boot_alloc()` allocates pages of contiguous physical memory. (Not sure how to figure out what to return when `n != 0` and what to do to allocate but NOT initialize. Why no malloc. (this have something to do with its usage as is shown and used in `mem_init()`). Where is that `end`. (The kernel was loaded at `0xf0000000`. Its global variables and memory allocated by `boot_alloc()` afterwards should be in this region ?))\
`mem_init()` creates one initial page `kern_pgdir` as a page directory. This is stores at virtual address `UVPT`, so the index (`PDX(UVPT)`) of `kern_pgdir` should be the physical address of itself. `kern_pgdir` is the size of 4096 (`PGSIZE`), which exactly has 1024 entries for 32-bit addresses (?). `struct PageInfo` is not physical address itself, but could be identity-mapped to physical pages. The `i`th PageInfo corresponds to the `i`th page, so the physical address of that page should be `(current_pageinfo - pages) << PGSHIFT`, which is calculated in `page2pa` function in `kern/pmap.h`.\
`page_init()`. Here we are dealing with the first 256MB physical memory, which is mapped to `0xf0000000` in virtual address. The kernel should already been in the physical memory after the extended memory `EXTPHYSMEM`, and some other things are also in. So use boot_alloc(0) to find the first free virtual address.\
`page_alloc()` and `page_free()`. Just pop from and push into the `page_free_list`. Not changing the `pp_ref`, and take care of `pp_link`.

# Part 2: Virtual Memory
The segment descriptors have fields BASE, which defines the location of the segment within the 4G linear address space. The processor concatenate three fragments to form a single 32-bit value. \
The field LIMIT defines the size of the segment, which could be concatenated from two fragments and form a 20-bit value. If this is for one-byte unit, the limit is 1MB. If the unit is 4KB, the limit is 4GB. This granularity is specified by the granularity bit in the descriptor, when set the unit size is 4KB. \
The field TYPE distinguishes between various kinds of descriptors. \
The field DPL is used for protection. \
The Segment-Present bit indicates whether the descriptor is valid for address tranformation. The process will signal an exception if this bit is zero. Most of the descriptor then could be marked AVAILABLE and free to use. When the linear address isnot mapped by paging mechanism or the segment is not present in memory, this could be the case.\
The Accessed bit is set when a selector for the descriptor is loaded into a segment register or used by a selector test instruction.

There are two kinds of descriptor tables: the global descriptor table (GDT) and local descriptor tables (LDT). Such a table is an array of 64-bit entries containing descriptors. The processor locates the GDT and the current LDT by the GDTR and LDTR registers, which store base addresses of the tables in linear address space and segment limits.

The selector of a logical address identifies a descriptor by specifying a descriptor table and indexing a descriptor in it. The value of selectors are usually assigned by linkers or loaders. The 16-bit selector use its high 13 bits for indexing (which means there are at most 8192 entries in one descriptor table). Bit 2 indicates whether this selector refers to GDT or the current LDT. Bit 1-0 (RPL) is used for protection mechanism. The first entry is not used by the processor and the corresponding selector can be used as a null selector and later useful for initializing unused segment registers so as to trap accidental references.

Segment registers stores information from descriptors. The visible part are manipulated as a 16-bit register by programs. The invisible part is manipulated by processors. Using load instructions (MOV, POP, LDS, CALL, JMP, etc.) a program loads the visible part with a 16-bit selector and the processor fetches the corresponding descriptor information into the invisible part.

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
`boot_map_region()`. Use `pgdir_walk` to get the corresponding page table entry. The mapping granularity should be "page". This might be done with only one page directory entry and several contiguous page table entries if `size` is not large. (Why it doesn't change the reference counter (?)\
`page_insert()`. We only need to check whether the presence bit in the page table is 1, no matter this page table had already existed or has just been allocated by `pgdir_walk` because the result from `pgdir_walk` is also a page table with the presence bit cleared. The tricky part about the corner case is that if we remove the page first, that page may end up in the free page list, and after we insert it back and increase `pp_ref`, one page in the free list will have non-zero reference counter. So it's better to increase `pp_ref` before `page_remove` to make sure `pp` not go back to the free list.\
`page_lookup()`. During insertion we directly use `create = 1` for `pgdir_walk` because eventually we need to make an entry exist, so it's "create if not there", so no need to check existence by setting `create = 0`. Here we don't need ensured existence, so only use `create = 0` to lookup (1 might also do). Note that every time to use the page entry as physical address, `PTE_ADDR` is needed for removing low bit flags.\
`page_remove()`. If a parameter is a double pointer, we can just pass a reference of a pointer to it. Passing a reference also means that double pointer is not `NULL`.

# Part 3: Kernel Address Space
User environments cannot see above `ULIM`, which contains memory-mapped I/O, CPU kernel stacks and remapped physical memory.

## Permissions and Fault Isolation
Permission bits are used to allow user code access only to the user part of address. `PTE_W` applies to both user and kernel code. `[UTOP, ULIM)` space endows same permission for user and kernel (expose read-only kernel data to user).

## Initializing the Kernel Address Space
### Exercise 5
Map `pages` to `UPAGES` for user reading. The `pages`'s original mapping (its physical address + `KERNBASE`) might be done in next tasks. Here there are two ways, one is to explicitly insert new pages with `page_insert` and do mapping page to page from the `pages`'s physical address to `UPAGES`. Or it's also acceptable to use `boot_map_region` to map contiguous addresses. (Are these two methods the same or which one is correct for later use? Looks like `page_insert` increases the reference counter and `boot_map_region` doesn't. (?)) The kernel stack mapping is similar. Two ways. \
The physical memory are all mapped at `KERNBASE`. This might be done by `boot_map_region`. Some of the pages has already got a reference counter of 1 in `page_init`, so maybe not change their reference counter here but just add them into the page table, "static" mapping as the comments of `boot_map_region` said. (?)

2. Every page directory entry stores an address of a page table, which might be allocated by `pgdir_walk`. And every page table entry stores an address of an actual page. The page table itself is also a page. So a page directory entry corresponds to 4MB memory (`PTSIZE`). \
The entry 956 (`UPAGES >> 22`) has the base virtual address `UPAGES`(`0xef000000`), and points to a page table that leads to pages that store `pages`. \
The entry 957 (`UVPT >> 22`) points to the page directory itself. (So we can get physical addresses of page tables (?))\
Starting from the entry 960 (`KERNBASE >> 22`) there are physical memory mapping. Every entry points to a page table where continuous page table entries points to continuous physical memory. So every entry corresponds to 4MB continuous physical memory.
3. Permission bits (?)
4. The physical memory capacity should be related to the capacity of `pages`, which is 4MB. So there could be at most 512K `PageInfo`s, and that's 2G memory. The size of the page directory reflects the virtual memory capacity.
5. ~~512K pages means 512 page tables~~ Page table number is not related to physical page number, but decided by virtual address range. So there still are 1024 page tables due to 1024 entries in the page directory. So 1025 pages used as memory management. The `pages` could take 4MB (one `PTSIZE`). So totally roughly 8MB. (?)
6. `%eip` changed after `jmp *%eax`. So two more instructions get executed before `%eip` goes above `KERNBASE`. Virtual addresses `[0, 4MB)` are also mapped to physical addresses `[0, 4MB)` in `kern/entrypgdir.c` to make sure that two instructions can succeed. The `entry_pgdir` in `kern/entrypgdir.c` is set to `%cr3` in `kern/entry.S`, and is used until the setup of `kern_pgdir`. This is why we get virtual addresses above `KERNBASE` before set up pages. Since `kern_pgdir` does not map first 4MB virtual addresses to first 4MB physical address, we need that transition to make `%eip` above `KERNBASE`.