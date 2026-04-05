#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include "inc/types.h"
#include <inc/elf.h>
#include "inc/stdio.h"

#include <kern/pmap.h>
#include <kern/kclock.h>

bool hidden_test_cases();