/*-
 * Copyright (c) 1997 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/memdesc.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>

#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/md_var.h>

#define	MAX_BPAGES	1024

struct bus_dma_tag {
	bus_dma_tag_t	parent;
	bus_size_t	alignment;
	bus_size_t	boundary;
	bus_addr_t	lowaddr;
	bus_addr_t	highaddr;
	bus_dma_filter_t *filter;
	void		*filterarg;
	bus_size_t	maxsize;
	u_int		nsegments;
	bus_size_t	maxsegsz;
	int		flags;
	int		ref_count;
	int		map_count;
	bus_dma_lock_t	*lockfunc;
	void		*lockfuncarg;
	bus_dma_segment_t *segments;
};

struct bounce_page {
	vm_offset_t	vaddr;		/* kva of bounce buffer */
	bus_addr_t	busaddr;	/* Physical address */
	vm_offset_t	datavaddr;	/* kva of client data */
	bus_addr_t	dataaddr;	/* client physical address */
	bus_size_t	datacount;	/* client data count */
	STAILQ_ENTRY(bounce_page) links;
};

u_int busdma_swi_pending;

static struct mtx bounce_lock;
static STAILQ_HEAD(bp_list, bounce_page) bounce_page_list;
static int free_bpages;
static int reserved_bpages;
static int active_bpages;
static int total_bpages;
static int total_bounced;
static int total_deferred;

static SYSCTL_NODE(_hw, OID_AUTO, busdma, CTLFLAG_RD, 0, "Busdma parameters");
SYSCTL_INT(_hw_busdma, OID_AUTO, free_bpages, CTLFLAG_RD, &free_bpages, 0,
    "Free bounce pages");
SYSCTL_INT(_hw_busdma, OID_AUTO, reserved_bpages, CTLFLAG_RD, &reserved_bpages,
    0, "Reserved bounce pages");
SYSCTL_INT(_hw_busdma, OID_AUTO, active_bpages, CTLFLAG_RD, &active_bpages, 0,
    "Active bounce pages");
SYSCTL_INT(_hw_busdma, OID_AUTO, total_bpages, CTLFLAG_RD, &total_bpages, 0,
    "Total bounce pages");
SYSCTL_INT(_hw_busdma, OID_AUTO, total_bounced, CTLFLAG_RD, &total_bounced, 0,
    "Total bounce requests");
SYSCTL_INT(_hw_busdma, OID_AUTO, total_deferred, CTLFLAG_RD, &total_deferred,
    0, "Total bounce requests that were deferred");

struct bus_dmamap {
	struct bp_list	bpages;
	int		pagesneeded;
	int		pagesreserved;
	bus_dma_tag_t	dmat;
	struct memdesc	mem;
	bus_dmamap_callback_t *callback;
	void		*callback_arg;
	STAILQ_ENTRY(bus_dmamap) links;
};

static STAILQ_HEAD(, bus_dmamap) bounce_map_waitinglist;
static STAILQ_HEAD(, bus_dmamap) bounce_map_callbacklist;
static struct bus_dmamap nobounce_dmamap;

static void init_bounce_pages(void *dummy);
static int alloc_bounce_pages(bus_dma_tag_t dmat, u_int numpages);
static int reserve_bounce_pages(bus_dma_tag_t dmat, bus_dmamap_t map,
    int commit);
static bus_addr_t add_bounce_page(bus_dma_tag_t dmat, bus_dmamap_t map,
    vm_offset_t vaddr, bus_addr_t addr, bus_size_t size);
static void free_bounce_page(bus_dma_tag_t dmat, struct bounce_page *bpage);
static __inline int run_filter(bus_dma_tag_t dmat, bus_addr_t paddr,
    bus_size_t len);

/*
 * Return true if a match is made.
 *
 * To find a match walk the chain of bus_dma_tag_t's looking for 'paddr'.
 *
 * If paddr is within the bounds of the dma tag then call the filter callback
 * to check for a match, if there is no filter callback then assume a match.
 */
static __inline int
run_filter(bus_dma_tag_t dmat, bus_addr_t paddr, bus_size_t len)
{
	bus_size_t bndy;
	int retval;

	retval = 0;
	bndy = dmat->boundary;
	do {
		if (((paddr > dmat->lowaddr && paddr <= dmat->highaddr) ||
		    (paddr & (dmat->alignment - 1)) != 0 ||
		    (paddr & bndy) != ((paddr + len) & bndy)) &&
		    (dmat->filter == NULL ||
		    (*dmat->filter)(dmat->filterarg, paddr) != 0))
			retval = 1;
		dmat = dmat->parent;
	} while (retval == 0 && dmat != NULL);
	return (retval);
}

/*
 * Convenience function for manipulating driver locks from busdma (during
 * busdma_swi, for example).  Drivers that don't provide their own locks
 * should specify &Giant to dmat->lockfuncarg.  Drivers that use their own
 * non-mutex locking scheme don't have to use this at all.
 */
void
busdma_lock_mutex(void *arg, bus_dma_lock_op_t op)
{
	struct mtx *dmtx;

	dmtx = (struct mtx *)arg;
	switch (op) {
	case BUS_DMA_LOCK:
		mtx_lock(dmtx);
		break;
	case BUS_DMA_UNLOCK:
		mtx_unlock(dmtx);
		break;
	default:
		panic("Unknown operation 0x%x for busdma_lock_mutex!", op);
	}
}

/*
 * dflt_lock should never get called.  It gets put into the dma tag when
 * lockfunc == NULL, which is only valid if the maps that are associated
 * with the tag are meant to never be defered.
 * XXX Should have a way to identify which driver is responsible here.
 */
static void
dflt_lock(void *arg, bus_dma_lock_op_t op)
{
	panic("driver error: busdma dflt_lock called");
}

#define BUS_DMA_MIN_ALLOC_COMP BUS_DMA_BUS4

/*
 * Allocate a device specific dma_tag.
 */
int
bus_dma_tag_create(bus_dma_tag_t parent, bus_size_t alignment,
    bus_size_t boundary, bus_addr_t lowaddr, bus_addr_t highaddr,
    bus_dma_filter_t *filter, void *filterarg, bus_size_t maxsize,
    int nsegments, bus_size_t maxsegsz, int flags, bus_dma_lock_t *lockfunc,
    void *lockfuncarg, bus_dma_tag_t *dmat)
{
	bus_dma_tag_t newtag;
	int error = 0;

	/* Basic sanity checking */
	if (boundary != 0 && boundary < maxsegsz)
		maxsegsz = boundary;

	/* Return a NULL tag on failure */
	*dmat = NULL;

	newtag = (bus_dma_tag_t)malloc(sizeof(*newtag), M_DEVBUF, M_NOWAIT);
	if (newtag == NULL)
		return (ENOMEM);

	newtag->parent = parent;
	newtag->alignment = alignment;
	newtag->boundary = boundary;
	newtag->lowaddr = trunc_page(lowaddr) + (PAGE_SIZE - 1);
	newtag->highaddr = trunc_page(highaddr) + (PAGE_SIZE - 1);
	newtag->filter = filter;
	newtag->filterarg = filterarg;
	newtag->maxsize = maxsize;
	newtag->nsegments = nsegments;
	newtag->maxsegsz = maxsegsz;
	newtag->flags = flags;
	newtag->ref_count = 1; /* Count ourself */
	newtag->map_count = 0;
	if (lockfunc != NULL) {
		newtag->lockfunc = lockfunc;
		newtag->lockfuncarg = lockfuncarg;
	} else {
		newtag->lockfunc = dflt_lock;
		newtag->lockfuncarg = NULL;
	}
	newtag->segments = NULL;

	/* Take into account any restrictions imposed by our parent tag */
	if (parent != NULL) {
		newtag->lowaddr = MIN(parent->lowaddr, newtag->lowaddr);
		newtag->highaddr = MAX(parent->highaddr, newtag->highaddr);
		if (newtag->boundary == 0)
			newtag->boundary = parent->boundary;
		else if (parent->boundary != 0)
			newtag->boundary = MIN(parent->boundary,
			    newtag->boundary);
		if (newtag->filter == NULL) {
			/*
			 * Short circuit looking at our parent directly
			 * since we have encapsulated all of its information
			 */
			newtag->filter = parent->filter;
			newtag->filterarg = parent->filterarg;
			newtag->parent = parent->parent;
		}
		if (newtag->parent != NULL)
			atomic_add_int(&parent->ref_count, 1);
	}

	if (newtag->lowaddr < ptoa(Maxmem) && (flags & BUS_DMA_ALLOCNOW) != 0) {
		/* Must bounce */

		if (ptoa(total_bpages) < maxsize) {
			int pages;

			pages = atop(maxsize) - total_bpages;

			/* Add pages to our bounce pool */
			if (alloc_bounce_pages(newtag, pages) < pages)
				error = ENOMEM;
		}
		/* Performed initial allocation */
		newtag->flags |= BUS_DMA_MIN_ALLOC_COMP;
	}

	if (error != 0) {
		free(newtag, M_DEVBUF);
	} else {
		*dmat = newtag;
	}
	return (error);
}

int
bus_dma_tag_destroy(bus_dma_tag_t dmat)
{
	if (dmat != NULL) {

		if (dmat->map_count != 0)
			return (EBUSY);

		while (dmat != NULL) {
			bus_dma_tag_t parent;

			parent = dmat->parent;
			atomic_subtract_int(&dmat->ref_count, 1);
			if (dmat->ref_count == 0) {
				if (dmat->segments != NULL)
					free(dmat->segments, M_DEVBUF);
				free(dmat, M_DEVBUF);
				/*
				 * Last reference count, so
				 * release our reference
				 * count on our parent.
				 */
				dmat = parent;
			} else
				dmat = NULL;
		}
	}
	return (0);
}

/*
 * Allocate a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
int
bus_dmamap_create(bus_dma_tag_t dmat, int flags, bus_dmamap_t *mapp)
{
	int error;

	error = 0;

	if (dmat->segments == NULL) {
		dmat->segments = (bus_dma_segment_t *)malloc(
		    sizeof(bus_dma_segment_t) * dmat->nsegments, M_DEVBUF,
		    M_NOWAIT);
		if (dmat->segments == NULL)
			return (ENOMEM);
	}

	/*
	 * Bouncing might be required if the driver asks for an active
	 * exclusion region, a data alignment that is stricter than 1, and/or
	 * an active address boundary.
	 */
	if (dmat->lowaddr < ptoa(Maxmem)) {
		/* Must bounce */
		int maxpages;

		*mapp = (bus_dmamap_t)malloc(sizeof(**mapp), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (*mapp == NULL)
			return (ENOMEM);

		/* Initialize the new map */
		STAILQ_INIT(&((*mapp)->bpages));

		/*
		 * Attempt to add pages to our pool on a per-instance
		 * basis up to a sane limit.
		 */
		maxpages = MIN(MAX_BPAGES, Maxmem - atop(dmat->lowaddr));
		if ((dmat->flags & BUS_DMA_MIN_ALLOC_COMP) == 0
		 || (dmat->map_count > 0 && total_bpages < maxpages)) {
			int pages;

			pages = MAX(atop(dmat->maxsize), 1);
			pages = MIN(maxpages - total_bpages, pages);
			if (alloc_bounce_pages(dmat, pages) < pages)
				error = ENOMEM;

			if ((dmat->flags & BUS_DMA_MIN_ALLOC_COMP) == 0) {
				if (error == 0)
					dmat->flags |= BUS_DMA_MIN_ALLOC_COMP;
			} else {
				error = 0;
			}
		}
	} else {
		*mapp = NULL;
	}
	if (error == 0)
		dmat->map_count++;
	return (error);
}

/*
 * Destroy a handle for mapping from kva/uva/physical
 * address space into bus device space.
 */
int
bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map)
{

	if (map != NULL && map != &nobounce_dmamap) {
		if (STAILQ_FIRST(&map->bpages) != NULL)
			return (EBUSY);
		free(map, M_DEVBUF);
	}
	dmat->map_count--;
	return (0);
}


/*
 * Allocate a piece of memory that can be efficiently mapped into
 * bus device space based on the constraints lited in the dma tag.
 * A dmamap to for use with dmamap_load is also allocated.
 */
int
bus_dmamem_alloc(bus_dma_tag_t dmat, void** vaddr, int flags,
    bus_dmamap_t *mapp)
{
	int mflags;

	if (flags & BUS_DMA_NOWAIT)
		mflags = M_NOWAIT;
	else
		mflags = M_WAITOK;

	/* If we succeed, no mapping/bouncing will be required */
	*mapp = NULL;

	if (dmat->segments == NULL) {
		dmat->segments = (bus_dma_segment_t *)malloc(
		    sizeof(bus_dma_segment_t) * dmat->nsegments, M_DEVBUF,
		    mflags);
		if (dmat->segments == NULL)
			return (ENOMEM);
	}
	if (flags & BUS_DMA_ZERO)
		mflags |= M_ZERO;

	/*
	 * XXX:
	 * (dmat->alignment < dmat->maxsize) is just a quick hack; the exact
	 * alignment guarantees of malloc need to be nailed down, and the
	 * code below should be rewritten to take that into account.
	 *
	 * In the meantime, we'll warn the user if malloc gets it wrong.
	 */
	if ((dmat->maxsize <= PAGE_SIZE) &&
	   (dmat->alignment < dmat->maxsize) &&
	    dmat->lowaddr >= ptoa(Maxmem)) {
		*vaddr = malloc(dmat->maxsize, M_DEVBUF, mflags);
	} else {
		/*
		 * XXX Use Contigmalloc until it is merged into this facility
		 *     and handles multi-seg allocations.  Nobody is doing
		 *     multi-seg allocations yet though.
		 * XXX Certain AGP hardware does.
		 */
		*vaddr = contigmalloc(dmat->maxsize, M_DEVBUF, mflags,
		    0ul, dmat->lowaddr, dmat->alignment? dmat->alignment : 1ul,
		    dmat->boundary);
	}
	if (*vaddr == NULL)
		return (ENOMEM);
	else if (vtophys(*vaddr) & (dmat->alignment - 1))
		printf("bus_dmamem_alloc failed to align memory properly.\n");
	return (0);
}

/*
 * Free a piece of memory and it's allociated dmamap, that was allocated
 * via bus_dmamem_alloc.  Make the same choice for free/contigfree.
 */
void
bus_dmamem_free(bus_dma_tag_t dmat, void *vaddr, bus_dmamap_t map)
{
	/*
	 * dmamem does not need to be bounced, so the map should be
	 * NULL
	 */
	if (map != NULL)
		panic("bus_dmamem_free: Invalid map freed\n");
	if ((dmat->maxsize <= PAGE_SIZE) &&
	   (dmat->alignment < dmat->maxsize) &&
	    dmat->lowaddr >= ptoa(Maxmem))
		free(vaddr, M_DEVBUF);
	else {
		contigfree(vaddr, dmat->maxsize, M_DEVBUF);
	}
}

static void
_bus_dmamap_count_phys(bus_dma_tag_t dmat, bus_dmamap_t map, vm_paddr_t buf,
    bus_size_t buflen, int flags)
{
	bus_addr_t curaddr;
	bus_size_t sgsize;

	if ((dmat->lowaddr < paddr_max || dmat->boundary > 0 ||
	    dmat->alignment > 1) && map != &nobounce_dmamap &&
	    map->pagesneeded == 0) {
		/*
		 * Count the number of bounce pages
		 * needed in order to complete this transfer
		 */
		curaddr = buf;
		while (buflen != 0) {
			sgsize = MIN(buflen, dmat->maxsegsz);
			if (run_filter(dmat, curaddr, 0) != 0) {
				sgsize = MIN(sgsize, PAGE_SIZE);
				map->pagesneeded++;
			}
			curaddr += sgsize;
			buflen -= sgsize;
		}
	}
}

static void
_bus_dmamap_count_pages(bus_dma_tag_t dmat, bus_dmamap_t map, pmap_t pmap,  
    void *buf, bus_size_t buflen, int flags)
{
	vm_offset_t vaddr;
	vm_offset_t vendaddr;
	bus_addr_t paddr;

	if ((dmat->lowaddr < ptoa(Maxmem) || dmat->boundary > 0 ||
	    dmat->alignment > 1) && map != &nobounce_dmamap &&
	    map->pagesneeded == 0) {
		/*
		 * Count the number of bounce pages
		 * needed in order to complete this transfer
		 */
		vaddr = trunc_page((vm_offset_t)buf);
		vendaddr = (vm_offset_t)buf + buflen;

		while (vaddr < vendaddr) {
			if (pmap == kernel_pmap)
				paddr = pmap_kextract(vaddr);
			else
				paddr = pmap_extract(pmap, vaddr);
			if (run_filter(dmat, paddr, 0) != 0)
				map->pagesneeded++;
			vaddr += PAGE_SIZE;
		}
	}
}

static int
_bus_dmamap_reserve_pages(bus_dma_tag_t dmat, bus_dmamap_t map, int flags)
{

	/* Reserve Necessary Bounce Pages */
	mtx_lock(&bounce_lock);
	if (flags & BUS_DMA_NOWAIT) {
		if (reserve_bounce_pages(dmat, map, 0) != 0) {
			mtx_unlock(&bounce_lock);
			return (ENOMEM);
		}
	} else {
		if (reserve_bounce_pages(dmat, map, 1) != 0) {
			/* Queue us for resources */
			STAILQ_INSERT_TAIL(&bounce_map_waitinglist,
			    map, links);
			mtx_unlock(&bounce_lock);
			return (EINPROGRESS);
		}
	}
	mtx_unlock(&bounce_lock);

	return (0);
}

/*
 * Add a single contiguous physical range to the segment list.
 */
static int
_bus_dmamap_addseg(bus_dma_tag_t dmat, bus_dmamap_t map, bus_addr_t curaddr,
    bus_size_t sgsize, bus_dma_segment_t *segs, int *segp)
{
	bus_addr_t baddr, bmask;
	int seg;

	/*
	 * Make sure we don't cross any boundaries.
	 */
	bmask = ~(dmat->boundary - 1);
	if (dmat->boundary > 0) {
		baddr = (curaddr + dmat->boundary) & bmask;
		if (sgsize > (baddr - curaddr))
			sgsize = (baddr - curaddr);
	}

	/*
	 * Insert chunk into a segment, coalescing with
	 * previous segment if possible.
	 */
	seg = *segp;
	if (seg == -1) {
		seg = 0;
		segs[seg].ds_addr = curaddr;
		segs[seg].ds_len = sgsize;
	} else {
		if (curaddr == segs[seg].ds_addr + segs[seg].ds_len &&
		    (segs[seg].ds_len + sgsize) <= dmat->maxsegsz &&
		    (dmat->boundary == 0 ||
		    (segs[seg].ds_addr & bmask) == (curaddr & bmask)))
			segs[seg].ds_len += sgsize;
		else {
			if (++seg >= dmat->nsegments)
				return (0);
			segs[seg].ds_addr = curaddr;
			segs[seg].ds_len = sgsize;
		}
	}
	*segp = seg;
	return (sgsize);
}

/*
 * Utility function to load a physical buffer.  segp contains
 * the starting segment on entrace, and the ending segment on exit.
 */
int
_bus_dmamap_load_phys(bus_dma_tag_t dmat, bus_dmamap_t map,
    vm_paddr_t buf, bus_size_t buflen, int flags, bus_dma_segment_t *segs,
    int *segp)
{
	bus_addr_t curaddr;
	bus_size_t sgsize;
	int error;

	if (map == NULL)
		map = &nobounce_dmamap;

	if (segs == NULL)
		segs = dmat->segments;

	if (map != &nobounce_dmamap) {
		_bus_dmamap_count_phys(dmat, map, buf, buflen, flags);
		if (map->pagesneeded != 0) {
			error = _bus_dmamap_reserve_pages(dmat, map, flags);
			if (error)
				return (error);
		}
	}

	while (buflen > 0) {
		curaddr = buf;
		sgsize = MIN(buflen, dmat->maxsegsz);
		if (map->pagesneeded != 0 &&
		    run_filter(dmat, curaddr, sgsize)) {
			sgsize = MIN(sgsize, PAGE_SIZE);
			curaddr = add_bounce_page(dmat, map, 0, curaddr,
			    sgsize);
		}
		sgsize = _bus_dmamap_addseg(dmat, map, curaddr, sgsize, segs,
		    segp);
		if (sgsize == 0)
			break;
		buf += sgsize;
		buflen -= sgsize;
	}

	/*
	 * Did we fit?
	 */
	return (buflen != 0 ? EFBIG : 0); /* XXX better return value here? */
}
/*
 * Utility function to load a linear buffer.  segp contains
 * the starting segment on entrace, and the ending segment on exit.
 */
int
_bus_dmamap_load_buffer(bus_dma_tag_t dmat, bus_dmamap_t map, void *buf,
    bus_size_t buflen, pmap_t pmap, int flags,
    bus_dma_segment_t *segs, int *segp)
{
	bus_size_t sgsize;
	bus_addr_t curaddr;
	vm_offset_t vaddr;
	int error;

	if (map == NULL)
		map = &nobounce_dmamap;

	if (segs == NULL)
		segs = dmat->segments;

	if (map != &nobounce_dmamap) {
		_bus_dmamap_count_pages(dmat, map, pmap, buf, buflen, flags);
		if (map->pagesneeded != 0) {
			error = _bus_dmamap_reserve_pages(dmat, map, flags);
			if (error)
				return (error);
		}
	}

	vaddr = (vm_offset_t)buf;

	while (buflen > 0) {
		/*
		 * Get the physical address for this segment.
		 */
		if (pmap == kernel_pmap)
			curaddr = pmap_kextract(vaddr);
		else
			curaddr = pmap_extract(pmap, vaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)curaddr & PAGE_MASK);
		if (sgsize > dmat->maxsegsz)
			sgsize = dmat->maxsegsz;
		if (buflen < sgsize)
			sgsize = buflen;

		if (map->pagesneeded != 0 && run_filter(dmat, curaddr, sgsize))
			curaddr = add_bounce_page(dmat, map, vaddr, curaddr,
			    sgsize);

		sgsize = _bus_dmamap_addseg(dmat, map, curaddr, sgsize, segs,
		    segp);
		if (sgsize == 0)
			break;

		vaddr += sgsize;
		buflen -= sgsize;
	}

	/*
	 * Did we fit?
	 */
	return (buflen != 0 ? EFBIG : 0); /* XXX better return value here? */
}


void
__bus_dmamap_waitok(bus_dma_tag_t dmat, bus_dmamap_t map,
    struct memdesc *mem, bus_dmamap_callback_t *callback, void *callback_arg)
{
	if (map != NULL) {
		map->dmat = dmat;
		map->mem = *mem;
		map->callback = callback;
		map->callback_arg = callback_arg;
	}
}

bus_dma_segment_t *
_bus_dmamap_complete(bus_dma_tag_t dmat, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, int error)
{

	if (segs == NULL)
		segs = dmat->segments;
	return (segs);
}

/*
 * Release the mapping held by map.
 */
void
_bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map)
{
	struct bounce_page *bpage;

	while ((bpage = STAILQ_FIRST(&map->bpages)) != NULL) {
		STAILQ_REMOVE_HEAD(&map->bpages, links);
		free_bounce_page(dmat, bpage);
	}
}

void
_bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map, bus_dmasync_op_t op)
{
	struct bounce_page *bpage;

	if ((bpage = STAILQ_FIRST(&map->bpages)) != NULL) {
		/*
		 * Handle data bouncing.  We might also
		 * want to add support for invalidating
		 * the caches on broken hardware
		 */

		if (op & BUS_DMASYNC_PREWRITE) {
			while (bpage != NULL) {
				if (bpage->datavaddr != 0)
					bcopy((void *)bpage->datavaddr,
					    (void *)bpage->vaddr,
					    bpage->datacount);
				else
					physcopyout(bpage->dataaddr,
					    (void *)bpage->vaddr,
					    bpage->datacount);
				bpage = STAILQ_NEXT(bpage, links);
			}
			total_bounced++;
		}

		if (op & BUS_DMASYNC_POSTREAD) {
			while (bpage != NULL) {
				if (bpage->datavaddr != 0)
					bcopy((void *)bpage->vaddr,
					    (void *)bpage->datavaddr,
					    bpage->datacount);
				else
					physcopyin((void *)bpage->vaddr,
					    bpage->dataaddr,
					    bpage->datacount);
				bpage = STAILQ_NEXT(bpage, links);
			}
			total_bounced++;
		}
	}
}

static void
init_bounce_pages(void *dummy __unused)
{

	free_bpages = 0;
	reserved_bpages = 0;
	active_bpages = 0;
	total_bpages = 0;
	STAILQ_INIT(&bounce_page_list);
	STAILQ_INIT(&bounce_map_waitinglist);
	STAILQ_INIT(&bounce_map_callbacklist);
	mtx_init(&bounce_lock, "bounce pages lock", NULL, MTX_DEF);
}
SYSINIT(bpages, SI_SUB_LOCK, SI_ORDER_ANY, init_bounce_pages, NULL);

static int
alloc_bounce_pages(bus_dma_tag_t dmat, u_int numpages)
{
	int count;

	count = 0;
	while (numpages > 0) {
		struct bounce_page *bpage;

		bpage = (struct bounce_page *)malloc(sizeof(*bpage), M_DEVBUF,
		    M_NOWAIT | M_ZERO);
		if (bpage == NULL)
			break;
		bpage->vaddr = (vm_offset_t)contigmalloc(PAGE_SIZE, M_DEVBUF,
		    M_NOWAIT, 0ul, dmat->lowaddr, PAGE_SIZE, dmat->boundary);
		if (bpage->vaddr == 0) {
			free(bpage, M_DEVBUF);
			break;
		}
		bpage->busaddr = pmap_kextract(bpage->vaddr);
		mtx_lock(&bounce_lock);
		STAILQ_INSERT_TAIL(&bounce_page_list, bpage, links);
		total_bpages++;
		free_bpages++;
		mtx_unlock(&bounce_lock);
		count++;
		numpages--;
	}
	return (count);
}

static int
reserve_bounce_pages(bus_dma_tag_t dmat, bus_dmamap_t map, int commit)
{
	int pages;

	mtx_assert(&bounce_lock, MA_OWNED);
	pages = MIN(free_bpages, map->pagesneeded - map->pagesreserved);
	if (commit == 0 && map->pagesneeded > (map->pagesreserved + pages))
		return (map->pagesneeded - (map->pagesreserved + pages));
	free_bpages -= pages;
	reserved_bpages += pages;
	map->pagesreserved += pages;
	pages = map->pagesneeded - map->pagesreserved;

	return (pages);
}

static bus_addr_t
add_bounce_page(bus_dma_tag_t dmat, bus_dmamap_t map, vm_offset_t vaddr,
    bus_addr_t addr, bus_size_t size)
{
	struct bounce_page *bpage;

	KASSERT(map != NULL && map != &nobounce_dmamap,
	    ("add_bounce_page: bad map %p", map));

	if (map->pagesneeded == 0)
		panic("add_bounce_page: map doesn't need any pages");
	map->pagesneeded--;

	if (map->pagesreserved == 0)
		panic("add_bounce_page: map doesn't need any pages");
	map->pagesreserved--;

	mtx_lock(&bounce_lock);
	bpage = STAILQ_FIRST(&bounce_page_list);
	if (bpage == NULL)
		panic("add_bounce_page: free page list is empty");

	STAILQ_REMOVE_HEAD(&bounce_page_list, links);
	reserved_bpages--;
	active_bpages++;
	mtx_unlock(&bounce_lock);

	if (dmat->flags & BUS_DMA_KEEP_PG_OFFSET) {
		/* Page offset needs to be preserved. */
		bpage->vaddr |= vaddr & PAGE_MASK;
		bpage->busaddr |= vaddr & PAGE_MASK;
	}
	bpage->datavaddr = vaddr;
	bpage->dataaddr = addr;
	bpage->datacount = size;
	STAILQ_INSERT_TAIL(&(map->bpages), bpage, links);
	return (bpage->busaddr);
}

static void
free_bounce_page(bus_dma_tag_t dmat, struct bounce_page *bpage)
{
	struct bus_dmamap *map;

	bpage->datavaddr = 0;
	bpage->datacount = 0;
	if (dmat->flags & BUS_DMA_KEEP_PG_OFFSET) {
		/*
		 * Reset the bounce page to start at offset 0.  Other uses
		 * of this bounce page may need to store a full page of
		 * data and/or assume it starts on a page boundary.
		 */
		bpage->vaddr &= ~PAGE_MASK;
		bpage->busaddr &= ~PAGE_MASK;
	}

	mtx_lock(&bounce_lock);
	STAILQ_INSERT_HEAD(&bounce_page_list, bpage, links);
	free_bpages++;
	active_bpages--;
	if ((map = STAILQ_FIRST(&bounce_map_waitinglist)) != NULL) {
		if (reserve_bounce_pages(map->dmat, map, 1) == 0) {
			STAILQ_REMOVE_HEAD(&bounce_map_waitinglist, links);
			STAILQ_INSERT_TAIL(&bounce_map_callbacklist, map,
			    links);
			busdma_swi_pending = 1;
			total_deferred++;
			swi_sched(vm_ih, 0);
		}
	}
	mtx_unlock(&bounce_lock);
}

void
busdma_swi(void)
{
	bus_dma_tag_t dmat;
	struct bus_dmamap *map;

	mtx_lock(&bounce_lock);
	while ((map = STAILQ_FIRST(&bounce_map_callbacklist)) != NULL) {
		STAILQ_REMOVE_HEAD(&bounce_map_callbacklist, links);
		mtx_unlock(&bounce_lock);
		dmat = map->dmat;
		(dmat->lockfunc)(dmat->lockfuncarg, BUS_DMA_LOCK);
		bus_dmamap_load_mem(map->dmat, map, &map->mem, map->callback,
		    map->callback_arg, BUS_DMA_WAITOK);
		(dmat->lockfunc)(dmat->lockfuncarg, BUS_DMA_UNLOCK);
		mtx_lock(&bounce_lock);
	}
	mtx_unlock(&bounce_lock);
}
