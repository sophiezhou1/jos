// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/hidden.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


int mon_show(int argc, char **argv, struct Trapframe *tf);
int mon_showmappings(int argc, char **argv, struct Trapframe *tf);
int mon_continue(int argc, char **argv, struct Trapframe *tf);
int mon_si(int argc, char **argv, struct Trapframe *tf);


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};


// LAB 1: add your command to here...
static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display a stack backtrace", mon_backtrace },
	// { "hidden", "Run hidden test cases", exec_hidden_cases},
	{ "show", "Display colorful ASCII art", mon_show },
	{ "showmappings", "Display physical page mappings for a VA range", mon_showmappings },
	{ "continue", "Continue execution", mon_continue },
	{ "si", "Single-step one instruction", mon_si },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t ebp, *ptr_ebp;
	struct Eipdebuginfo info;

	ebp = read_ebp();
	cprintf("Stack backtrace:\n");

	while (ebp != 0) {
		ptr_ebp = (uint32_t *)ebp;
		uint32_t eip = ptr_ebp[1];

		// Print the frame info
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n",
		        ebp, eip, ptr_ebp[2], ptr_ebp[3], ptr_ebp[4], ptr_ebp[5], ptr_ebp[6]);

		// Print the metadata with EXACTLY 9 spaces indentation
		if (debuginfo_eip(eip, &info) == 0) {
			cprintf("         %s:%d: %.*s+%d\n",
			        info.eip_file,
			        info.eip_line,
			        info.eip_fn_namelen, info.eip_fn_name,
			        eip - info.eip_fn_addr);
		}

		ebp = ptr_ebp[0];
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

int

mon_show(int argc, char **argv, struct Trapframe *tf)

{

    cprintf("\x1b[31m  ##     ##   #######   ######\n");
    cprintf("\x1b[32m ##     ##  ##        ##    ##\n");
    cprintf("\x1b[33m#########  ########   ####### \n");
    cprintf("\x1b[34m      ##   ##     ##      ##  \n");
    cprintf("\x1b[35m     ##     #######      ## \n");
    cprintf("\x1b[0m");

    return 0;

}

int mon_showmappings(int argc, char **argv, struct Trapframe *tf) {
    if (argc != 3) {
        cprintf("Usage: showmappings [begin_va] [end_va]\n");
        return 0;
    }

    uintptr_t begin = (uintptr_t)strtol(argv[1], NULL, 16);
    uintptr_t end = (uintptr_t)strtol(argv[2], NULL, 16);

    for (; begin <= end; begin += PGSIZE) {
        pte_t *pte = pgdir_walk(kern_pgdir, (void *)begin, 0);
        
        cprintf("VA: 0x%08x -> ", begin);
        if (!pte || !(*pte & PTE_P)) {
            cprintf("Not Mapped\n");
        } else {
            // PTE_ADDR strips the permission bits to give the Physical Address
            cprintf("PA: 0x%08x | Perms: %s%s%s\n", 
                PTE_ADDR(*pte),
                (*pte & PTE_W) ? "W" : "R",
                (*pte & PTE_U) ? "U" : "S",
                (*pte & PTE_P) ? "P" : "-");
        }
    }
    return 0;
}

int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	if (tf == NULL) {
		cprintf("No current trap frame.\n");
		return 0;
	}

	tf->tf_eflags &= ~FL_TF;
	return -1;
}

int
mon_si(int argc, char **argv, struct Trapframe *tf)
{
	if (tf == NULL) {
		cprintf("No current trap frame.\n");
		return 0;
	}

	tf->tf_eflags |= FL_TF;
	return -1;
}
