/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground.
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

#if OPT_A3

static uint32_t getLowWord(paddr_t paddr, bool is_read_only) {
	uint32_t elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	if (is_read_only) {
		elo &= ~TLBLO_DIRTY;
	}
	return elo;
}

static bool isBetween(vaddr_t src, vaddr_t lower, vaddr_t upper) {
	return (lower <= src) && (src <= upper);
}

static void initCoreMap() {
	for (unsigned int i=0; i < core_map.npages; i++) {
		core_map.cores[i].size = 0;
	}
}

static paddr_t mapAddr(unsigned long i) {
	return core_map.offset + (paddr_t)(PAGE_SIZE * i);
}

// works?
// offset, npages, cores, created
struct CoreMap core_map = {0, 0, NULL, false};

#endif

void
vm_bootstrap(void)
{
#if OPT_A3

	paddr_t lo, hi;
	// get Phys RAM after kernel
	ram_getsize(&lo, &hi);
	// init Core Map
	// start of core maps
	core_map.cores = (struct Core*)PADDR_TO_KVADDR(lo);
	// # frames (incl. core map)
	core_map.npages = (hi - lo) / PAGE_SIZE;
	// update lo (top of core map)
	lo += sizeof(struct Core) * core_map.npages;
	// # frames (excl. core map)
	core_map.npages = (hi - lo) / PAGE_SIZE;
	// align to PAGE_SIZE if necessary
	core_map.offset = (lo % PAGE_SIZE == 0)? lo : lo + PAGE_SIZE - lo % PAGE_SIZE;
	// set every frame FREE
	initCoreMap();
	// flag down
	core_map.created = true;

#endif
/* Do nothing. */
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

#if OPT_A3

	// time to alloc !!
	if (core_map.created) {
		unsigned int i,j;
		bool found = false;

		for (i = 0; i < core_map.npages; i++) {
			// found an empty spot! (but not sure if it fits)
			if (core_map.cores[i].size == 0) {

				// iterate # pages requested
				for (j = 0; j < npages; j++) {
					// does NOT fit!
					if (core_map.cores[i+j].size != 0) {
						// skip
						i += j;
						break;
					} else {
						continue;
					}
				}

				// found fitting space
				if (j == npages) {
					found = true;
					// set each frame
					for (j = 0; j < npages; j++) {
						core_map.cores[i+j].size = npages;
					}
					// return addr
					addr = mapAddr(i);
					// done!
					break;
				} else {
					continue;
				}

			}
		}
		// either success / fail!

		// out of memory a.k.a. FAIL!
		if (!found) {
			panic("out of memory!");
			spinlock_release(&stealmem_lock);
			return ENOMEM;
		}
	// still in core map block
	} else {
		addr = ram_stealmem(npages);
	}

#else
	addr = ram_stealmem(npages);
#endif

	spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
#if OPT_A3
	// core_map should be created?...
	KASSERT(core_map.created);
	unsigned int i, j;

	spinlock_acquire(&stealmem_lock);

	// search for the addr you want to free
	for (i = 0; i < core_map.npages; i++) {
		paddr_t core_p = mapAddr(i);
		vaddr_t core_v = PADDR_TO_KVADDR(core_p);

		// found address to free!
		// match either phys or virt
		if (core_v == addr || core_p == addr) {
			// contiguous size to be removed
			unsigned long size = core_map.cores[i].size;
			// FREEEEEEEE
			for (j = 0; j < size; j++) {
				core_map.cores[i+j].size = 0;
			}
			// DONE!
			break;
		} else {
			continue;
		}

	}

	// NOT found (reach the end of frames)
	// if (i == core_map.npages) {
	// 	panic("cannot free invalid address!");
	// 	spinlock_release(&stealmem_lock);
	// }

	spinlock_release(&stealmem_lock);

#else
	/* nothing - leak the memory. */

	(void)addr;
#endif
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		#if OPT_A3
				// kill curproc
				return 1;
		#else
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
		#endif
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = getLowWord(paddr, isBetween(faultaddress, vbase1, vtop1) && as->is_loaded);
		DEBUG(DB_VM, "dumbvm: On %d.. 0x%x -> 0x%x\n", i, faultaddress, paddr);
		tlb_write(ehi, elo, i);
	#ifdef DEBUG
		tlb_read(&ehi, &elo, i);
		DEBUG(DB_VM, "read %d.. 0x%x -> 0x%x\n", i, ehi, elo);
	#endif
		splx(spl);
		return 0;
	}
#if OPT_A3
	ehi = faultaddress;
	elo = getLowWord(paddr, isBetween(faultaddress, vbase1, vtop1) && as->is_loaded);
	DEBUG(DB_VM, "dumbvm(full): On %d.. 0x%x -> 0x%x\n", i, faultaddress, paddr);
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
#else
	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
#endif
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;

#if OPT_A3
	as->is_loaded = false;
#endif

	return as;
}

void
as_destroy(struct addrspace *as)
{
#if OPT_A3

	// flush all frame struct
	// is there better way?
	// unsigned int i;
	// for (i = 0; i < core_map.npages; i++) {
	// 	core_map.cores[i].size = 0;
	// }
	free_kpages(as->as_pbase1);
	free_kpages(as->as_pbase2);
	free_kpages(as->as_stackpbase);

#endif
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
as_prepare_load(struct addrspace *as)
{
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
#if OPT_A3
	// adtivate read-only & flush TLB
	as->is_loaded = true;
	as_activate();
#else
	(void)as;
#endif
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);

	*ret = new;
	return 0;
}
