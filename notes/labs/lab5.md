# File system preliminaries
The file system here provides features like creating, reading, writing and deleting files organized in a hierarchical directory structure. \
There are no ownership or permissions as this being a single-user system. There are no hard links, symbolic links, time stamps or device files.

## On-Disk File System Structure
Most UNIX file systems divide disk space into *inode* regions and *data* regions. An inode is assigned to each file in the file system., which holds metadata about the file such as its `stat` attributes and pointers to its data blocks. The data regions are divided into larger (8KB or more) data blocks, where file data and directory metadata are stored. Directories entries contain file names and pointers to inodes. A file is hard-linked if multiple directory entries refer to that file inode. \
Here things are simplified since no hard link is supported. The file's (or sub-directory's (?)) metadata are stored in the only directory entry describing that file. \
Both files and directories consist of data blocks, which may be scattered throughout the disk like the physical pages. The file system environment hides the details of block layout, providing interfaces for reading and writing at arbitrary offsets within files. Modifications to directories are handled as a part of actions like creation and deletion. Direct read to directory metadata is allowed and could be done by `read`, which means user environments can scan directory by themselves (e.g. to implement `ls` program) without other special calls. This makes application programs dependent on the format of directory metadata, which means changing the file system's internal layout requires changing in applications.

### Sectors and Blocks
Disk read and write is performed in units of sectors, each of which has 512 bytes here. File systems allocate and use disk in units of blocks. Sectors are related to hardware, where blocks are indicating how the operating system work with the disk. The block size should be a multiple of the sector size. \
Since storage price has gone cheaper and larger granularities are efficient, the block size tends to be large. Here it's 4096 bytes. 

### Superblocks
File systems reserve certain disk blocks at "easy to find" locations (the very beginning or the end) for the file system's metadata such as the block size, disk size, metadata for root directory, timestamps and so on. These are called superblocks.\
Here we have one superblock at block 1 on the disk. The layout is in `struct Super` in `inc/fs.h`. Block 0 is for boot loaders and partition tables. Many real file systems use several superblocks mainly for fault tolerance. 

### File Meta-data
The metadata layout for a file is described by `struct File` in `inc/fs.h`. It includes the file name, size, type (file or directory), pointers to the blocks comprising the file. Since there's no inode, this metadata is stored in a directory entry on disk. For simplicity we use `File` for it appearing both on disk and in memory (?). \
The `f_direct` array in `File` contains space for block numbers of the first `NDIRECT` blocks of the file, which are referred as direct blocks. For larger files there is an additional block called the indirect block for 4096/4 = 1024 block numbers. So the file size limit could be 1034 blocks, which is over 4MB. For even larger files double- and triple-indirect blocks could be used.

### Directories versus Regular Files
`File` can represent either a regular file or a directory, which is distinguished by `type`. Regular files' data blocks are not interpreted, while contents of directory files are interpreted as a series of `File` structures describing files and subdirectories in the directory. \
The superblock contains a `File` structure (`s_root` field in `struct Super`) that holds the metadata for the root directory. The contents of this directory file may contain a sequence of `File` structures describing the files an directories located in the root directory. Subdirectories may in turn have more `File` structures of sub-subdirectories, and so on.

# The File System

## Disk Acess
The file system environment needs to access the disk. We implement the IDE disk driver as part of the user-level file system environment, instead of adding the driver to the kernel with some syscalls. \
We could rely on polling, "programmed I/O"-based disk access and not use disk interrupts. \
The x86 processor uses the IOPL bits in the eflags register to determine whether protected-mode code is allowed to perform special device I/O instructions such as the IN and OUT instructions. Since all the IDE disk registers are located in the I/O space rather than being memory-mapped, giving I/O privilege to the file system is all we need. Here we only want the file system environment to access I/O space but not other environments.
### Exercise 1
Mask the `tf_eflags` with `FL_IOPL_MASK` defined in `inc/mmu.h` if the environment type is `ENV_TYPE_FS`.

1. Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why? \
The `tf_eflags` will be saved in `env_tf` and restored later. So no (?).

## The Block Cache
Our file system will be limited to handling disks of 3GB or less, since we reserve a fixed 3GB region for the file system environment's address space, from 0x10000000 (`DISKMAP`) to 0xd0000000 (`DISKMAP + DISKMAX`) as a memory-mapped version of the disk. `diskaddr()` in `fs/bc.c` translates disk block numbers to virtual addresses. Since the file system environment is only responsible for file access, there's no need for stacks (?) or something, which makes this reservation reasonable. \
We will not read the entire disk into memory but use *demand paging*, where we only allocate pages in the disk map region and read the corresponding block from the disk in response to a page fault. 
### Exercise 2
`bc_pgfault()` actually takes an `UTrapframe` as the parameter and serves as the page fault handler when we perform demand paging. This is similar to the mechanism in `lib/fork.c`. It checks the fault happens within the block cache region and check whether the block number is valid. \
Then we need to round down the `addr` and alloc a new page for it in the current file system environment. We need `ide_read()` in `fs/ide.c` to read the disk block into memory. It operates on sectors and requires the starting sector's number as well as the number of sector to be read, and the destination address (`addr` here). Here every block has 8 (`BLKSECTS`, defined in `fs/fs.h`) sectors, so the starting sector is `blockno * BLKSECTS`. We need to read `BLKSECTS` sectors to fill a page. \
After reading we need to clear the dirty bit in the page table entry since this page is just read. Also we need to check whether this block is allocated (why after reading ?). \
`flush_block()` sends the content of the page containing the input `addr` back to the disk block. We could use `va_is_mapped()` (which effectively checks the `PTE_P` bit of `uvpt` and `uvpd`) and `va_is_dirty` (which checks the `PTE_D` bit of `uvpt`) to check whether we don't need to do anything. Then we just round down `addr` since we are operating on the entire page, and write it to the block with `ide_write()`. Then we use `sys_page_map()` to reset the permission bits. We could just use `PTE_SYSCALL` to mask the original permission bits. It's useful to use 0 for the current environment in user mode.

The `fs_init()` function in `fs/fs.c` initializes the block cache with `bc_init()`, which set the page fault handler to `bc_pgfault()` and try to store pointers to the disk map region in `super` global variable. Then `super` can be read as if they were in memory and the page fault handler will get to work if necessary.

## The Block Bitmap
After `fs_init()` set the `bitmap` pointer, we can treat it as an array of bits for each block on the disk to mark whether the block is free. In `block_is_free()` we can see that it's treated as an array of 32-bit number, so for certain `blockno` its corresponding bit is located in `blockno / 32`th entry and it's the `blockno % 32`th one in that entry.
### Exercise 3
Note that in `bitmap` 1 indicates free. So we need to find the first bit which is 0. We search through every entry, of which there are `super->s_nblocks / 32`. When we find a 0 bit set it to one and return the corresponding block number. Note that we need to flush this bitmap block rather than the corresponding block back to disk. The bitmap block starts in block 2. In every block there are 1024 entries, so the current block should be `2 + i / 1024` where `i` is the entry index.

## File Operations
We need to interpret and manage `File`, scan and manage entries and directory files and walk the file system from root to resolve an absolute path. Some functions are provided in `fs/fs.c`. \
`dir_lookup()` looks for a `dir/name` file in `dir`. Note that `dir` could have `dir->f_size / BLKSIZE` "virtual" blocks. For each "virtual" block, we could find out its virtual address via `file_get_block()`. This virtual address could also store `File` structures to represent an array of files. \
`dir_alloc_file()` finds a free `File` structure in `dir`. The free `File` is indicated by an empty file name. The searching process is similar to what in `dir_lookup()`. If there is no current free file, add another block (by setting an entry of `f_direct` or the indirect block to nonzero) and return the first `File` structure of that block. \
`walk_path()` evaluates a path name and return the found file and its directory. If we only find the directory, `lastelem` is set as the final path element. 
### Exercise 4
`file_block_walk()` find the entry in the `File` structure of the corresponding `filebno` "virtual" block number. The entry stores the "physical" block number. So if `filebno < NDIRECT` just return the entry in `f->f_direct`. Otherwise we need to search inside the indirect block which contains 1024 more "physical" block number. If there is no indirect block and `alloc` is set, we need to allocate a new block. Here we can use `flush_block()` to clear the whole indirect block (?). Then we just find the virtual address of the indirect block via `diskaddr()` and return the corresponding entry in that address. \
`file_get_block()` finds the actual "physical" block and return its corresponding virtual address. We could use `file_block_walk()` to find the entry and the "physical" block number is stored there. If the value is 0 we need to allocate a new block and modify the entry. Then we store the virtual address in `blk`.

## The file system interface
We need to make other environments able to use the file system. Other environments cannot directly call functions in the file system environment, and we need to expose access to the file system via a remote procedure call (RPC). \
Regular environments could call interfaces in `lib/fd.c`, for example `read()`. This works on any file descriptor (?) and dispatches to the device function, here `devfile_read()`, which is specifically for on-disk files. `devfile_*` functions in `lib/file.c` are for the client side of the file system operations, building up arguments in a request struction, calling `fsipc()` to send IPC request and unpacking and returing results. `fsipc()` handles the common details of sending a request to the server and receiving the reply. \
The file system server is in `fs/serv.c`. It repeatedly receives a request from IPC in `serve()`, dispatching the request to the appropriate handler, and sending the result back. For `read()`, `serve()` will dispatch to `serve_read()`, which will take care of read requests by unpacking the structure and call `file_read()` in `fs/fs.c` to read the file. \
We use the 32-bit value in IPC for the request type (RPCs are numbered like syscalls). The arguments to the request in `union Fsipc` are stored in the page shared via IPC. The client side shares the pag at `fsipcbuf`. The server maps the incoming request page at `fsreq` (`0xffff000`). \
When sending the response back, the server uses the 32-bit value as the return code. `FSREQ_READ` and `FSREQ_STAT` also return data, which is written to the page on which the client sent its request. The page is not needed to be sent again since the client shared it with the file system already. `FSREQ_OPEN` shares with the client a new "Fd page" (?).
### Exercise 5
`read()` in `lib/fd.c` finds the corresponding file descriptor and the device (file, console I/O or pipes), and pass them to the corresponding device's read function (`devfile_read()` here). `devfile_read()` set the `read` structure (`req_fileid` and `req_n`) inside a `union Fsipc` global value `fsipcbuf`, and call `fsipc()` of type `FSREQ_READ` for IPC. `fsipc()` send the type as the 32-bit value and the `fsipcbuf` global value as the page to the file system environment. \
The file system server use `serve()` to repeatedly receive the information from uer environments. It stores the received `union Fsipc` in a global pointer `fsreq` and calls the `fshandler` of `FSREQ_READ`, which is `serve_read()`. \
In `serve_read()` we need an `OpenFile` to find the already-open file according to `fsipc->read.req_fileid`. The `OpenFile` structure stores the file's file id, its `File` pointer, the open mode (`O_RDONLY`, `O_WRONLY` or `O_RDWR`) and the file descriptor `struct Fd`. Then `file_read()` is called to read the specified `File` pointer in the `OpenFile` structure. The required reading bytes (`req_n`) is specified in `fsipc` and the current offset is in `OpenFile`'s `Fd` structure. The result is stored in `fsipc`'s `readRet` structure, which is the physical page the user environment and the file system share. After reading the offset in `OpenFile` should be updated by adding the return value of `file_read()`, which is the number of bytes read. This is also the `serve_read()`'s return value if read succeeds. \
After the handler returns the file system sends the return code of the handler back to the user environment. If it's an open operation it will also send a page of the new file descriptor of the open file (`open()` in `lib/file.c` calls `fsipc()` with a nonnull `dstva`).
### Exercise 6
This is similar to `serve_read()` and `devfile_read()`. `serve_write()` finds the corresponding `OpenFile` structure and write to the file with `req_n` as the number bytes to write and `req_buf` as the content to write. Update the offset as well. `devfile_write()` sets the global `fsipcbuf`. The `buf` pointer should be `memmove`d into the `req_buf`.

# Spawning Processes
`spawn` creates a new environment, loads a program image from the file system and then starts the child environment to run this program. The parent continues running independently of the child. This is like a `fork()` immediately followed by `exec()` in the child. The `spawn` is easier to implement in user space than `exec` (?).
### Exercise 7
Use `user_mem_assert()` to check whether the input `tf` is valid. Get the corresponding environment with `envid2env()`. The `FL_IF` bit in `tf_eflags` should be set. `FL_IOPL` should be set to 0. The CPL is represented by RPL in the segment selector, which means we need to set `tf_cs`'s RPL to 3 (later we check whether the environment is in user mode by checking the last two bits in `tf_cs`, for example in `kern/trap.c`).

We need this system call to modify the `Trapframe` of the child environment. The `fork()` does not need this because the child from `fork()` should have basically the same `Trapframe` as the parent except for the return value, which is set in `sys_exofork()`. Here we need to set the new `eip` for the child since it will execute some other programs and the new `esp` since it will not *continue* to execute something but *start* a new execution. Note that we get the `Trapframe` from the `envs` array, which is mapped at `UENVS` readonly. So we cannot directly change the value, but have to use such a syscall.\
The `eip` is set as the `elf->e_entry` read from the program. `init_stack()` is used for initializing the child's user stack, which takes the argument strings for the new execution from `spawn()`. It first get the address (`string_store`) for the actual argument strings into a temporary "stack" at `UTEMP`, and then get the address (`argv_store`) for pointers (32-bit `uintptr_t`) for each of the string, and then check whether there are two more spaces for `argc` and `argv` (the pointer to pointers to strings) themselves in a page. After allocating a new page at `UTEMP`, we set the value of pointers to strings (`argv_store[i]`) to the actual address in `USTACK` after we finish, and copy the strings (from `argv[i]`) to the address of strings `string_store[i] + 1` (for `'\0'`). Then also set the `argv` (the one below `argv_store`, which is `argv_store[-1]`) to the actual address in `USTACK` and set the `argc`. The `esp` points to the actual address of the `argc`. Then just map the `UTEMP` to the child address space's `USTACKTOP - PGSIZE` and unmap in the current environment. \
We also need to set the segments of the program via `map_segment` (which basically is a counterpart of `memcpy` in `load_icode()` of `kern/env.c` back in lab 3, but should read from the program and store it at `UTEMP` and then map it to the child), and copy shared pages via `copy_shared_pages()`. Then finally we use the syscall to set the `Trapframe` we modified earlier and also set the status for the child to make it runnable. \
`spawnl()` could be used for variable arguments, which set up the `argv` with input arguments and call `spawn()`.

## Sharing library state across fork and spawn
The file descriptor can also emcompass pipes and console I/O. Here we use `struct Dev` with pointers to functions to represent the device type. `lib/fd.c` provides general interfaces on top of this. Each `struct Fd` indicates the device type and the general functions dispatch operations to device-specific functions. \
`lib/fd.c` also has a file descriptor table region in each application environment's address (?), starting at `FDTABLE` (`0xd0000000`). This area reserves a page for each file descriptor (up to 32 (`MAXFD`)) the application can open at once (The `opentab` in `fs/serv.c` stores up to 1024 (`MAXOPEN`) `OpenFile` structures for open files in the file system). \
If we want to share file descriptor state across `fork()` and `spawn()`, but the file descriptor is in user memory. So `fork()` will copy the state when it's written rather than share. This means environments won't be able to seek in files they didn't open themselves and pipes won't work across a fork (?). The `spawn` will leave behind the memory. The spawned environment starts with no open file descriptors. \
We define `PTE_SHARE` bit (`0x400`), which is one of the `PTE_SYSCALL` bits. If this bit is set, then the page table entry will be directly copied in both `fork()` and `spawn()`, which means we share the same page. 
### Exercise 8
In `lib/fork.c` we add `PTE_SHARE` check and just simply copy the whole page with the `PTE_SYSCALL` bits. In `lib/spawn.c` we go through the entire user space the copy every page table entry that has `PTE_SHARE` set. 

# The keyboard interface
We need a way to type for the shell to work. So far we've only taken input while in the kernel monitor. `kern/console.c` contains the keyboard and serial drivers, and we need to attach these to the rest of the system (?).
### Exercise 9
Just dispatch the `IRQ_KBD` by calling `kbd_intr()` and `IRQ_SERIAL` by `serial_intr()` in `trap_dispatch()`.

`kbd_intr()` and `serial_intr()` fill a buffer with the recently read input while the consol file type consumes the buffer (?). \
In `ser/testkbd.c`, `readline()` in `lib/readline.c` is called for every input line. It repeatedly calls `getchar()` in `kern/console.c`, which calls `cons_getc()` to wait for an input character. \
`serial_intr()` and `kbd_intr()` are called inside `cons_getc()` to make sure that `cons_getc()` works even if the interrupt is disabled (e.g. from the kernel monitor). `kbd_intr()` encapsulates `cons_intr` which takes a function as an argument, here it's `kbd_proc_data()`. `kbd_proc_data()` basically uses `inb` to read a character from the I/O port and return it. `cons_intr` puts the input character into the circular console input buffer. \
If there are new input characters, `cons_getc()` will return the character, which will cause `getchar()` to return. The new character will be printed out, and if it's not `EOF` or for changing to the next line, `getchar()` will be called again.
(why sometimes trapno 36 sometimes two trapno 33, one before the output char and one right after it ?)

# The Shell
`user/icode.c` will setup the console as file descriptors 0 and 1 (stdin and stdout). It will spawn `user/sh.c`, which is the shell. \
The library routine `cprintf` prints straight to the console without using the descriptor code. To print output to a particular file descriptor, use `fprintf(1, "...", ...)` for stdout (file descriptor 1). `printf("...", ...)` is a shortcut. 
### Exercise 10
It would be nice to redirect I/O in the shell. For input redirection, we first open the corresponding file `t` and try to open it onto the file descriptor 0 for stdin. If the file descriptor is not 0, just duplicate the file descriptor to 0 and close the previous one.

read pipes consoles \
how the file path things work  serve_open etc.\
shell impl 

kbd_intr serial_intr