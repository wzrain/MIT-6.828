# Part A: Multiprocessor Support and Cooperative Multitasking
Extend JOS to run on a multiprocessor system. Add system calls for user environments to create new environments. Cooperative round-robin scheduling allows the kernel to switch from one environment to another when the current environment voluntarily relinquishes the CPU. Preemptive scheduling allows the kernel to retake control of the CPU from an environment.

## Multiprocessor Support
We support symmetric multiprocessing (SMP), in which all CPUs have equivalent access to system resources such as memory and I/O buses. During the boot process there is a bootstrap processor (BSP) for initializing the system and booting the os, and application processors (APs) are activated by the boostrap processor after the os is running. The hardware and BIOS determine which processor is the bootstrap processor. \
In an SMP system each CPU has a local APIC (LAPIC) unit, which is for delivering interrupts throughout the system. The LAPIC will provide its CPU with an id. \
A processor accesses its LAPIC by memory-mapped I/O where some physical memory is hardwired to registers of I/O devices, so load/store instructions can be used to access device registers. The LAPIC lives in physical address `0xfe000000`, which is too high to be mapped at `KERNBASE`. Instead we map it at `MMIOBASE` (`0xef800000` in virtual address space).

### Exercise 1
We need to return the starting point of this block of memory, which is the old `base`. `size` should be rounded up and the ending point of the memory should not exceed `MMIOLIM`.

### Application Processor Bootstrap
Before booting APs, the BSP should collect information about the multiprocessor system, such as the number of CPUs, APIC ids, MMIO address of the LAPIC unit. This is done by retrieving the MP config table in BIOS's region of memory in `mp_init()` of `kern/mpconfig.c`. \
The AP bootstrap process is done by `boot_aps()` in `kern/init.c`. AP's entry code (`kern/mpentry.S`) is copied to a memory location that is addressable in real mode. Unlike the bootloader (?),  we can control where the AP will start executing. We copy the entry to `0x7000` (`MPENTRY_PADDR`), but any unused and page-aligned 640KB below would work. \
Then `boot_aps()` sends `STARTUP` interprocessor interrupt (IPI) to the LAPIC of a corresponding AP to activate it, along with an initial `CS:IP` address where the AP start executing (`MPENTRY_PADDR` here). Then after some setup the AP is put into protected mode with paging and then call `mp_main()` for the C setup. AP will signal `CPU_STARTED` flag in `cpu_status` of `struct CpuInfo` and `boot_aps()` will go to activate another one.
### Exercise 2
Just set the corresponding page of `MPENTRY_ADDR` as in use by setting `pp_ref = 1`. 
1. Compare `kern/mpentry.S` side by side with `boot/boot.S`. Bearing in mind that `kern/mpentry.S` is compiled and linked to run above KERNBASE just like everything else in the kernel, what is the purpose of macro `MPBOOTPHYS`? Why is it necessary in `kern/mpentry.S` but not in `boot/boot.S`? In other words, what could go wrong if it were omitted in `kern/mpentry.S`? \
`kern/mpentry.S` has already entered protected mode to be executed above `KERNBASE`. So we need `MPBOOTPHYS` to turn the virtual address like `start32` back to physical address. (?)

### Per-CPU State and Initialization
It's important to dinstinguish between private CPU states and global states shared by the entire system. Private states are defined in `struct CpuInfo` of `kern/cpu.h`. `cpunum()` returns the id of the CPU that calls it, which could be an index of arrays like `cpus`. 
* **Per-CPU kernel stack.** Multiple CPUs can trap into the kernel at the same time, so separate kernel stacks for each processor are needed (represented by `percpu_kstacks`). CPU 0's stack still grows from `KSTACKTOP`, and CPU 1's stack grows from `KSTKGAP` bytes below the bottom of CPU 0's stack.
* **Per-CPU TSS and TSS descriptor.** We need separate TSSs to specify different kernel stacks' location (represented by `cpus[i].cpu_ts`). \
The fields of a TSS belong to two classes. A dynamic set that the processor updates in every task switching, such as registers and the selector of the TSS of the previous task. A static set that processors only read, such as the selector of the task's LDT. It's a continuous table (104 entries of 32 bits). \
TSS may reside anywhere in the linear space. When TSS spans a page boundary and the high addressed page is not present, an exception is raised. \
TSS is defined by a descriptor of two continuous 32 bits like other segments. The BASE, LIMIT, DPL fields are similar to other descriptors (except for gate descriptor where the selector it stores points to another entry in the table). The LIMIT should be no smaller than 103. The B-bit indicates whether the task is busy. 
* **Per-CPU current environment pointer.** `cpus[cpunum()].cpu_env` points to the currenty executing environment on the current CPU.
* **Per-CPU system registers.** All registers are private to a CPU.

### Exercise 3
Use `boot_map_region()` Note that `percpu_kstack[i]` is already the virtual address of the stack top of CPU i's kernel stack.
### Exercise 4
Initialize `thiscpu->cpu_ts` like how the global `ts` is initialized previously. The stack top and the gdt index are related to the CPU id. `sd_s` is for system segments. `ts_iomb` specifies the base address of the I/O permission bit map (so `sizeof(Taskstate)` means this map is directly after TSS). Note that the selector is not `GD_TSS0`. CPUs other than CPU 0 should have different indices (the high 13 bits of the selector) right after the index of `CPU 0`. So just add `cid << 3` to `GD_TSS0`.

### Locking
We need to address race conditions when CPUs run kernel code simultaneously. When an environment enters kernel mode it acquires the *big kernel lock* and releases when it returns to user mode. So only one environment can run in kernel mode at one time.
### Exercise 5
In `kern/init.c` `i386_init()` acquire the lock before `boot_aps()` to activate all APs. All APs will be waiting at `lock_kernel()` to get scheduled until `i386_init()` creates environments and makes one environment run by `sched_yield()`, which will call `env_run()` and release the lock. When we dealing with traps from user mode we also need to acquire the lock.

2. It seems that using the big kernel lock guarantees that only one CPU can run the kernel code at a time. Why do we still need separate kernel stacks for each CPU? Describe a scenario in which using a shared kernel stack will go wrong, even with the protection of the big kernel lock. \
(?) Before the environment entering `trap()` to acquire the lock, the old `SS, esp` and so on have already been pushed into the kernel stack (this happens before the `ds, es` are pushed in `kern/trapentry.S`, which is before `trap()`).

## Round-Robin Scheduling
`sched_yield()` in `kern/sched.c` selects a new environment to run. It searches through `envs[]` circularly starting after the previously running environment, pick the first `ENV_RUNNABLE` and call `env_run()`. It must not run the same environment on two CPUs at the same time (can tell by `ENV_RUNNING`). `sys_yield()` is a system call to invoke `sched_yield()` for users to voluntarily give up the CPU.
### Exercise 6
Note that `curenv == NULL` means there are no previous running environments  or the whole process is over and we should quit. So we should find if there are environments with `ENV_RUNNABLE` in that situation. Add a new syscall `SYS_yield` in `kern/syscall.c` and invoke `sys_yield()`. Use `ENV_CREATE` to create `user_yield` environments in `kern/init.c`. \
Note that we need to use indices of `envs` array to traverse it rather than just use `env_link` which only leads to free environments. The index could be calculated with `ENVX` macro.

3. In your implementation of `env_run()` you should have called `lcr3()`. Before and after the call to `lcr3()`, your code makes references (at least it should) to the variable `e`, the argument to `env_run`. Upon loading the `%cr3` register, the addressing context used by the MMU is instantly changed. But a virtual address (namely `e`) has meaning relative to a given address context--the address context specifies the physical address to which the virtual address maps. Why can the pointer `e` be dereferenced both before and after the addressing switch? \
`e` should be in kernel space since `envs` is from `boot_alloc` and also be mapped at `UENVS` in `kern_pgdir`. The `env_pgdir` just copied these mapping from `kern_pgdir` in `env_setup_vm()`, so this should work in either context.
4. Whenever the kernel switches from one environment to another, it must ensure the old environment's registers are saved so they can be restored properly later. Why? Where does this happen? \
There is something to do with `Trapframe` (how the `Trapframe` can be popped as the stack top ?). The state is restored at `env_pop_tf()`. \
The process might be that in `env_pop_tf()` the assembly code takes an input of `tf`, and take this `tf` as the "stack" by set `esp` to 0. The `pop` instructions set corresponding registers, and `iret` makes the entry point, which is in `tf_eip`, as the starting point. When a trap happens, the `trapentry.S` will make the stack top looks the same as a `Trapframe`, and pass this `Trapframe` to the `trap()` function. In `trap()` if this trap is from a user environment this `Trapframe` will be copied to `curenv->env_tf`, which is the `Trapframe` of the current environment (since `curenv->env_tf` is not a pointer). Then let `tf` point to `curenv->env_tf` and just ignore things on the stack. So basically `Trapframe` is for saving the state for environments to continue executing from later. (The kernel stack should not be bothered since there is only one environment can enter the kernel mode at one time, and we don't need to empty it since every time we can get the kernel stack's original `esp` from TSS. (?))

## System Calls for Environment Creation
We need to allow not only *kernel* but also *user* environments to create and start other user environments. `fork()` system call copies the entire address space of the parent process to create a new child process. The only difference is the process id and the parent process id. In the parent `fork()` returns the child's id and in the child it returns 0. Each process gets its own address space and modifications are invisible to each other. \
There are more primitive syscalls for implementing `fork()`. `sys_exofork()` creates a new environment with an almost blank slate. Nothing is mapped in user address space and it's not runnable. It shares the register state as the parent when this is called. The parent returns the childs `envid_t`, and the child returns 0 (after it's made runnable by te parent). `sys_env_set_status` set the status of a specified environment to `ENV_RUNNABLE` or `ENV_NOT_RUNNABLE`. This is for marking a new environment ready to run. `sys_page_alloc` allocates a page of physical memory and maps it at a given virtual address. `sys_page_map` copies a page mapping from one environment's address space to another, which makes a memory sharing. `sys_page_unmap` unmaps a page at a given virtual address. \
The id 0 means the current environment, which is supported by `envid2env()`.

### Exercise 7
`sys_exofork()`. Use `env_alloc()` to create a new environment and set the status as `ENV_NOT_RUNNABLE`. Note that we need to copy all the `Trapframe` to the new environment to make the state completely the same. To make the return value different, the `eax` register need to be modified to 0 to indicate that this function returns 0 in the child process. The user interface of this function is not in `lib/syscall.c` but inlined in `inc/lib.h`, where it shows that the return value is stored in the `eax` register (`"=a" (ret)`). \
`sys_env_set_status()`. Use `envid2env()` to get the actual environment corresponding to the `envid`. The to-set `status` should be either `ENV_RUNNABLE` or `ENV_NOT_RUNNABLE`. \
`sys_page_alloc()`. Allocate a page with `page_alloc(1)` and try `page_insert()`. Check constraints for `va` and `perm`.\
`sys_page_map()`. To map an existing page to another address, we need `page_lookup()` first. This also helps find out the permission of this page and the new `perm` need to obey the read-write permission. Check constraints for addresses and `perm` as well.
`sys_page_unmap()` basically removes one mapping at `va`. The `page_remove()` function just decreases the reference counter of the mapped page. `page_free()` is called inside `page_remove()` if the reference counter reaches 0.