# Part 1: PC Bootstrap

Maybe use x86 assembly later. qemu and gdb

Address layout. Old PCs were capable of 1MB physical memory(0x0 - 0xfffff). 640KB low memory. 0xf0000 - 0xfffff for BIOS ROM. For backward compability these things are preserved. 

The IBM PC starts executing at 0xffff0. CS = 0xf000, IP = 0xfff0. (real mode)

## Exercise 2
Find out what BIOS is doing first (?)

# Part 2: The Boot Loader
A sector is a minimum transfer granularity. 512 bytes here. Boot sector is where the boot loader code resides. BIOS load the boot sector into memory 0x7c00. 

The boot loader switch from real mode to protected mode to access memory above 1MB. In protected mode there are virtual memory. Segments are moved between memory and disk (?). Every segment is assigned an entry in the descriptor table describing whether this segment is in memory, the base address, the access permission. 32-bit protected mode could use pages. \
The boot loader then reads the kernel from the hard disk by directly accessing the IDE disk device registers via the x86's special I/O instructions.\
`obj/boot/boot.asm` and `obj/kern/kernel.asm` for tracking in gdb.

## Exercise 3
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

## Exercise 5
The program will be stuck in the `ljmp` instruction to switch into protected mode. The gdt was loaded wrong, and the `ljmp` will go to a wrong instruction.

## Exercise 6
In the first breakpoint when the boot loader started, 0x100000, which is the start address of extended memory, stores nothing. After the second breakpoint when we entered kernel, there are something in the memory. Probably the loaded kernel loaded by boot loader.

