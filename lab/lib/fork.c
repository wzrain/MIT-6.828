// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	int perm = uvpt[((uint32_t)addr) >> 12] & 0xfff;
	if (!(err & FEC_WR) || !(perm & PTE_COW)) {
		panic("faulting access wrong\n");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	addr = ROUNDDOWN(addr, PGSIZE);
	envid_t envid = sys_getenvid();
	r = sys_page_alloc(envid, (void *)PFTEMP, PTE_P | PTE_W | PTE_U);
	if (r < 0) panic("pgfault: create temp location failed\n");
	memmove((void *)PFTEMP, addr, PGSIZE);
	r = sys_page_unmap(envid, addr);
	if (r < 0) panic("pgfault: unmap addr failed\n");
	r = sys_page_map(envid, (void *)PFTEMP, envid, addr, PTE_P | PTE_U | PTE_W);
	if (r < 0) panic("pgfault: map pftemp to addr failed\n");
	r = sys_page_unmap(envid, (void *)PFTEMP);
	if (r < 0) panic("pgfault: unmap pftemp failed\n");

	// panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	// panic("duppage not implemented");
	void *addr = (void *)(pn * PGSIZE);
	int shareperm = uvpt[pn] & PTE_SYSCALL;
	if (shareperm & PTE_SHARE) {
		r = sys_page_map(thisenv->env_id, addr, envid, addr, shareperm);
		if (r < 0) panic("duppage: map to child failed\n");
		return 0;
	}
	int perm = uvpt[pn] & 0xfff, newperm = PTE_P | PTE_U;
	if ((perm & PTE_W) || (perm & PTE_COW)) newperm |= PTE_COW;
	r = sys_page_map(thisenv->env_id, addr, envid, addr, newperm);
	if (r < 0) panic("duppage: map to child failed\n");
	if ((perm & PTE_W) || (perm & PTE_COW)) {
		r = sys_page_map(thisenv->env_id, addr, thisenv->env_id, addr, newperm);
		if (r < 0) panic("duppage: remap failed\n");
	}
	
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	// panic("fork not implemented");
	int r;
	set_pgfault_handler(pgfault);
	envid_t envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	for (uint32_t addr = UTEXT; addr < USTACKTOP; addr += PGSIZE) {
		if ((uvpd[(addr >> 22) & 0x3ff] & PTE_P) && (uvpt[addr >> 12] & PTE_P)) {
			duppage(envid, (addr >> 12));
		}
	}

	r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W);
	if (r < 0) panic("Fork: allocating for exception stack failed!\n");

	extern void _pgfault_upcall();
	r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (r < 0) panic("Fork: set pgfault upcall failed!\n");

	r = sys_env_set_status(envid, ENV_RUNNABLE);
	if (r < 0) panic("Fork: set env status failed!\n");

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
