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

	if (!((err & FEC_WR) && (uvpt[PGNUM(addr)] & PTE_COW)))
		panic("pgfault: not a write to a COW page. va=%08x err=%08x pte=%08x",
		      addr, err, uvpt[PGNUM(addr)]);

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	addr = ROUNDDOWN(addr, PGSIZE);

	if ((r = sys_page_alloc(0, PFTEMP, PTE_P | PTE_U | PTE_W)) < 0)
		panic("pgfault: sys_page_alloc failed: %e", r);

	memmove(PFTEMP, addr, PGSIZE);

	if ((r = sys_page_map(0, PFTEMP, 0, addr, PTE_P | PTE_U | PTE_W)) < 0)
		panic("pgfault: sys_page_map failed: %e", r);

	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("pgfault: sys_page_unmap failed: %e", r);
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
	void *addr = (void *)(pn * PGSIZE);
	pte_t pte = uvpt[pn];

	if ((pte & PTE_W) || (pte & PTE_COW)) {
		// Map into child as COW
		if ((r = sys_page_map(0, addr, envid, addr, PTE_P | PTE_U | PTE_COW)) < 0)
			return r;
		// Re-map in parent as COW (must come second — see exercise note)
		if ((r = sys_page_map(0, addr, 0, addr, PTE_P | PTE_U | PTE_COW)) < 0)
			return r;
	} else {
		// Read-only page: share it as-is
		if ((r = sys_page_map(0, addr, envid, addr, PTE_P | PTE_U)) < 0)
			return r;
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
	envid_t envid;
	uintptr_t addr;
	int r;

	// 1. Install the C-level page fault handler in the parent.
	set_pgfault_handler(pgfault);

	// 2. Create the child environment.
	envid = sys_exofork();
	if (envid < 0)
		panic("fork: sys_exofork failed: %e", envid);

	if (envid == 0) {
		// We are the child.
		// Fix up thisenv to point to our own Env struct.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We are the parent.

	// 3. Copy address space mappings for all pages below UTOP,
	//    except the exception stack (UXSTACKTOP-PGSIZE .. UXSTACKTOP).
	for (addr = 0; addr < UTOP - PGSIZE; addr += PGSIZE) {
		// Check that the page directory entry and page table entry exist.
		if (!(uvpd[PDX(addr)] & PTE_P))
			continue;
		if (!(uvpt[PGNUM(addr)] & PTE_P))
			continue;

		if ((r = duppage(envid, PGNUM(addr))) < 0)
			panic("fork: duppage failed for va=%08x: %e", addr, r);
	}

	// 4. Allocate a fresh page for the child's exception stack
	//    (cannot be COW because the fault handler itself runs there).
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE),
	                        PTE_P | PTE_U | PTE_W)) < 0)
		panic("fork: sys_page_alloc for child exception stack: %e", r);

	// 5. Set the child's page-fault upcall entrypoint to match the parent's.
	extern void _pgfault_upcall(void);
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		panic("fork: sys_env_set_pgfault_upcall: %e", r);

	// 6. Mark the child runnable.
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("fork: sys_env_set_status: %e", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
