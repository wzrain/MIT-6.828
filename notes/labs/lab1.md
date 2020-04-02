# Part 1: PC Bootstrap

Maybe use x86 assembly later. qemu and gdb

Address layout. Old PCs were capable of 1MB physical memory(0x0 - 0xfffff). 640KB low memory. 0xf0000 - 0xfffff for BIOS ROM. For backward compability these things are preserved. 

The IBM PC starts executing at 0xffff0. CS = 0xf000, IP = 0xfff0. (real mode)

### Exercise 2
Find out what BIOS is doing first (?)

# Part 2: The Boot Loader
A sector is a minimum transfer granularity. 512 bytes here. Boot sector is where the boot loader code resides. BIOS load the boot sector into memory 0x7c00 from the first sector of the hard disk. 

The boot loader switch from real mode to protected mode to access memory above 1MB. In protected mode there are virtual memory. Segments are moved between memory and disk (?). Every segment is assigned an entry in the descriptor table describing whether this segment is in memory, the base address, the access permission. 32-bit protected mode could use pages. \
The boot loader then reads the kernel from the hard disk by directly accessing the IDE disk device registers via the x86's special I/O instructions.\
`obj/boot/boot.asm` and `obj/kern/kernel.asm` for tracking in gdb.

### Exercise 3
Trace through `boot/boot.S` and `obj/boot/boot.asm`.\
Trace through `bootmain()` and into `readsect()` in `boot/main.c`. (?) `boot/main.c` read each section from disk to memory at its load address and then jump to the kernel's entry point.

* At what point does the processor start executing 32-bit code? What exactly causes the switch from 16- to 32-bit mode?\
`ljmp $PROT_MODE_CSEG, $protcseg`, cr0's bit 0 is 1, which means it's in prot mode.
* What is the last instruction of the boot loader executed, and what is the first instruction of the kernel it just loaded?\
In `bootmain()`, `((void (*)(void)) (ELFHDR->e_entry))();`, then execute the first instruction in `kernel.asm` or `entry.S`.
* Where is the first instruction of the kernel?\
`entry.S`
* How does the boot loader decide how many sectors it must read in order to fetch the entire kernel from disk? Where does it find this information?\
From the ELF header. (?)

The compiler turns `.c` file into object(`.o`) file containing assembly language instructions. The linker combine all object files into one binary image, here in ELF format. An ELF executable is a header (ELF header and program header) with loading information followed by several program sections containing code or data to be loaded into memory. The boot loader loads them and starts execution.\
Different program sections: `.text` for instructions, `.rodata` for read-only data (eg. strings), `.data` for initialized data. Uninitialized global variables are in `.bss` and set to zero. \
VMA (link address, where the section expects to execute), LMA (load address, where the section is loaded). Typically same thing. Different in kernel.\
Several program headers specify which parts of ELF object to load and destination address ("LOAD"). In `boot/main.c` it's `ph->p_pa`.

### Exercise 5
The program will be stuck in the `ljmp` instruction to switch into protected mode. The gdt was loaded wrong (the program is loaded right according to BIOS, but the gdt is loaded (`lgdtw`) according to where the program "thinks" it locates, which is the wrong address), and the `ljmp` will go to a wrong instruction.

### Exercise 6
`x /Nx ADDR` prints `N` words of memory at `ADDR`. In the first breakpoint when the boot loader started, 0x100000, which is the start address of extended memory, stores nothing. After the second breakpoint when we entered kernel, there are something in the memory. Probably the loaded kernel loaded by boot loader.

# Part 3: The Kernel
## Using virtual memory to work around position dependence
The link(0xf0100000) and load(0x100000) address of kernel is different, unlike the boot loader. The high link address leave lower parts of virtual address space for user programs (See lab2).\
Map virtual addr 0xf0100000 to phys addr 0x100000(load addr). (So the link address is more like virtual address ?)\
In `kern/entry.S` the `CR0_PG` flag is set and paging is enabled. Virtual addresses 0xf0000000 - 0xf0400000 as well as 0x0 - 0x400000 are all translated into phys addr
0x0 - 0x400000.

### Exercise 7
The breakpoint should be set at 0x100025 rather than 0xf0100025 (?). \
Memory at 0xf0100000 before `movl %eax, %cr0` is empty. After paging is enabled 0xf0100000 and 0x100000 have same data, which means 0x100000 is mapped to 0xf0100000.\
Without paging the program crashes at `movl $0x0, %esp`. No virtual address, cannot access to 0xf010002c.

## Formatted Printing to the Console
`kern/printf.c` called `vprintfmt` in `lib/printfmt.c` and `cputchar` in `kern/console.c`. `lib/printfmt.c` called `putch` 

### Exercise 8
Modify in `lib/printfmt.c`. The pattern is similar to other cases.

1. `cputchar`
2. `CRT_SIZE` means the capacity of the output buffer, and `crt_pos` means the last position of the buffer. `void memmove(void* dest, void* src, int num)` copies `src - src+num` to `dest` (dealing with overlapping spaces). So here it just copies all except the first line to the buffer head, and clear the last line (turn it into `' '`) and update `crt_pos`. \
Here `0x0700` means set that `int` into default attribute. 32-bit `int` use high 16 bits for attributes and low 16-bits for the character itself.
3. `fmt` is the string pattern. `va_list` is used for dealing variable parameters. `va_start` initializes `va_list` by specifying the last invariable parameter, here `fmt`. So here `ap` contains `x, y, z`. `va_end` is for invalidating `ap`.\
`va_arg` get the parameter `ap` currently points at in the specified type, and move `ap` to the next parameter. `va_arg` is called by `getint` and `getuint` when the pattern is `%d` or `%u`. `lflag` is determined by `l`s between `%` and `d`. For other non `%`pattern characters just output them.
4. `57616` in hex(`%x`) is `e110`. `%s` cares about addresses, so if we pass `i` instead of `&i` we'll try to print something located at `0x00646c72`. `%s` takes `0x00646c72` as a `char*` and print every byte. Here it's little-endian (the end is at low address), so the print starts at `0x72`, and then `0x6c, 0x64, 0x00`, which gives `rld\0`. In big-endian we need to change `i` to `0x726c6400`.
5. The `y` will be a random value. (?)
6. The parameter initially was pushed into stack from right to left. (?)
* *Challenge* Maybe change the attribute of the char (high 16 bits)(?)

## The Stack
### Exercise 9
`kern/entry.S` clears the frame pointer and set the stack pointer. In `obj/kern/kernel.asm` we can see the stack top is `0xf0110000` (where does this addr come from?). The stack size `KSTKSIZE` is stored in `.data` section, which is 32KB, so the reserved space is `0xf0110000-0xf0108000`. The stack top is at the high address, since the stack grows towards low address.\
.

`esp` register (stack pointer) points to the stack top (lowest location in use). In 32-bit mode the stack can only hold 32-bit values, and `esp` is divisible by 4.\
`ebp` register (base pointer/frame pointer) points to the previous `ebp` that is saved on the stack after entry to the new function. So previously parameters of the new function as well as the return address (`eip`) are saved. So parameters are located at positive offsets from `ebp` and local variables are at negative offsets.

### Exercise 10
For `test_backtrace(5)`, after `esp` is ste up for local variables, `$esp = 0xf010ffc8, $ebp = 0xf010ffd8`. For `test_backtrace(4)`, `$esp = 0xf010ffa8, $ebp = 0xf010ffb8`, and so on. Use `x /52x $esp` to examine the stack status after 5 recursive calls. Basically got what is mentioned above. (?)

### Exercise 11
Traceback by following `ebp` until `ebp` is 0. `eip` is `ebp + 4`, and `args` are starting from `ebp + 8`.

### Exercise 12
All additional information can be found in the struct `Eipdebuginfo` in `kern/kdebug.h`. Use `debuginfo_eip` method to get the information of the current `eip`.\
The `debuginfo_eip` method uses stabs format in `inc/stab.h` to record symbols. We need `N_SLINE` stab type to find line numbers. The binary search manipulates the index of the symbol table (`lline, rline`). `n_desc` stores the line number, and `n_value` stores the address.\
`printf("%.*s", length, string)` prints at most `length` characters of `string`, so it can be used to deal with non-null-terminated strings in stabs tables.\
stab (?) __STAB_* (?)

