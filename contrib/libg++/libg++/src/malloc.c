/* ---------- To make a malloc.h, start cutting here ------------ */

/* 
  A version of malloc/free/realloc written by Doug Lea and released to the 
  public domain.  Send questions/comments/complaints/performance data
  to dl@cs.oswego.edu

* preliminary VERSION 2.6.2q Mon Jan  8 10:28:33 1996  Doug Lea  (dl at gee)
  
   Note: There may be an updated version of this malloc obtainable at
           ftp://g.oswego.edu/pub/misc/malloc.c
         Check before installing!

* Synopsis of public routines

  (Much fuller descriptions are contained in the program documentation below.)

  malloc(size_t n);
     Return a pointer to a newly allocated chunk of at least n bytes, or null
     if no space is available.
  free(Void_t* p);
     Release the chunk of memory pointed to by p, or no effect if p is null.
  realloc(Void_t* p, size_t n);
     Return a pointer to a chunk of size n that contains the same data
     as does chunk p up to the minimum of (n, p's size) bytes, or null
     if no space is available. The returned pointer may or may not be
     the same as p. If p is null, equivalent to malloc.  Unless the
     #define REALLOC_ZERO_BYTES_FREES below is set, realloc with a
     size argument of zero (re)allocates a minimum-sized chunk.
  memalign(size_t alignment, size_t n);
     Return a pointer to a newly allocated chunk of n bytes, aligned
     in accord with the alignment argument, which must be a power of
     two.
  valloc(size_t n);
     Equivalent to memalign(pagesize, n), where pagesize is the page
     size of the system (or as near to this as can be figured out from
     all the includes/defines below.)
  calloc(size_t unit, size_t quantity);
     Returns a pointer to quantity * unit bytes, with all locations
     set to zero.
  cfree(Void_t* p);
     Equivalent to free(p).
  malloc_trim(size_t pad);
     Release all but pad bytes of freed top-most memory back 
     to the system. Return 1 if successful, else 0.
  malloc_usable_size(Void_t* p);
     Report the number usable allocated bytes associated with allocated
     chunk p. This may or may not report more bytes than were requested,
     due to alignment and minimum size constraints.
  malloc_stats();
     Prints brief summary statistics on stderr.
  mallinfo()
     Returns (by copy) a struct containing various summary statistics.
  mallopt(int parameter_number, int parameter_value)
     Changes one of the tunable parameters described below. Returns
     1 if successful in changing the parameter, else 0.

* Vital statistics:

  Alignment:                            8-byte

       8 byte alignment is currently hardwired into the design.  This
       seems to suffice for all current machines and C compilers.

  Assumed pointer representation:       4 bytes
  Assumed size_t  representation:       4 bytes
  Minimum overhead per allocated chunk: 4 bytes

       Each malloced chunk has a hidden overhead of 4 bytes holding size
       and status information.  

  Minimum allocated size:              16 bytes (12 bytes usable, 4 overhead)

       When a chunk is freed, 12 additional bytes are needed; 4 for a
       trailing size field and 8 bytes for free list pointers. Thus,
       the minimum allocatable size is 16 bytes, of which 12 bytes are
       usable. Even a request for zero bytes (i.e., malloc(0)) returns 
       a pointer to something of the minimum allocatable size.  

  Maximum allocated size:      2147483640 (2^31 - 8) bytes

       It is assumed that (possibly signed) 32 bit values suffice to
       represent chunk sizes. `Possibly signed' is due to the fact
       that `size_t' may be defined on a system as either a signed or
       an unsigned type. To be conservative, values that would appear
       as negative numbers are avoided.  The maximum size chunk is
       2^31 - 8 bytes.  Requests for negative sizes (when size_t is
       signed) or those greater than (2^31 - 8) bytes will return a
       minimum-sized chunk.

  Maximum overhead wastage per allocated chunk: normally 15 bytes

       Alignnment demands, plus the minimum allocatable size restriction
       make the normal worst-case wastage 15 bytes (i.e., up to 15
       more bytes will be allocated than were requested in malloc), with 
       two exceptions:
         1. Because requests for zero bytes allocate non-zero space,
            the worst case wastage for a request of zero bytes is 24 bytes.
         2. For requests >= mmap_threshold that are serviced via
            mmap(), the worst case wastage is 8 bytes plus the remainder
            from a system page (the minimal mmap unit); typically 4096 bytes.


* Synopsis of compile-time options:

    People have reported using previous versions of this malloc on all
    versions of Unix, sometimes by tweaking some of the defines
    below. It has been tested most extensively on Solaris and
    Linux. People have also reported adapting this malloc for use in
    stand-alone embedded systems.

    The implementation is in straight, hand-tuned ANSI C.  Among other
    consequences, it uses a lot of macros.  Because of this, to be at
    all usable, this code should be compiled using an optimizing compiler
    (for example gcc -O2) that can simplify expressions and control
    paths.

  __STD_C                  (default: derived from C compiler defines)
     Nonzero if using ANSI-standard C compiler, a C++ compiler, or
     a C compiler sufficiently close to ANSI to get away with it.
  DEBUG                    (default: NOT defined)
     Define to enable debugging. Adds fairly extensive assertion-based 
     checking to help track down memory errors, but noticeably slows down
     execution.
  REALLOC_ZERO_BYTES_FREES (default: NOT defined) 
     Define this if you think that realloc(p, 0) should be equivalent
     to free(p). Otherwise, since malloc returns a unique pointer for
     malloc(0), so does realloc(p, 0).
  HAVE_MEMCPY               (default: defined)
     Define if you are not otherwise using ANSI STD C, but still 
     have memcpy and memset in your C library and want to use them.
     Otherwise, simple internal versions are supplied.
  HAVE_MMAP                 (default: defined as 1)
     Define to non-zero to optionally make malloc() use mmap() to
     allocate very large blocks.  
  malloc_getpagesize        (default: derived from system #includes)
     Either a constant or routine call returning the system page size.
  HAVE_USR_INCLUDE_MALLOC_H (default: NOT defined) 
     Optionally define if you are on a system with a /usr/include/malloc.h
     that declares struct mallinfo. It is not at all necessary to
     define this even if you do, but will ensure consistency.
  INTERNAL_LINUX_C_LIB      (default: NOT defined)
     Defined only when compiled as part of Linux libc.
     Also note that there is some odd internal name-magling via defines
     (for example, internally, `malloc' is named `mALLOc') needed
     when compiling in this case. These look funny but don't otherwise
     affect anything.
  MORECORE                  (default: sbrk)
     The name of the routine to call to obtain more memory from the system.
  MORECORE_FAILURE          (default: -1)
     The value returned upon failure of MORECORE.
  DEFAULT_TRIM_THRESHOLD
  DEFAULT_TOP_PAD       
  DEFAULT_MMAP_THRESHOLD
  DEFAULT_MMAP_MAX      
     Default values of tunable parameters (described in detail below)
     controlling interaction with host system routines (sbrk, mmap, etc).
     These values may also be changed dynamically via mallopt(). The
     preset defaults are those that give best performance for typical
     programs/systems.


*/




/* Preliminaries */

#ifndef __STD_C
#ifdef __STDC__
#define __STD_C     1
#else
#if __cplusplus
#define __STD_C     1
#else
#define __STD_C     0
#endif /*__cplusplus*/
#endif /*__STDC__*/
#endif /*__STD_C*/

#ifndef Void_t
#if __STD_C
#define Void_t      void
#else
#define Void_t      char
#endif
#endif /*Void_t*/

#if __STD_C
#include <stddef.h>   /* for size_t */
#else
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>    /* needed for malloc_stats */


/*
  Compile-time options
*/


/*
    Debugging:

    Because freed chunks may be overwritten with link fields, this
    malloc will often die when freed memory is overwritten by user
    programs.  This can be very effective (albeit in an annoying way)
    in helping track down dangling pointers.

    If you compile with -DDEBUG, a number of assertion checks are
    enabled that will catch more memory errors. You probably won't be
    able to make much sense of the actual assertion errors, but they
    should help you locate incorrectly overwritten memory.  The
    checking is fairly extensive, and will slow down execution
    noticeably. Calling malloc_stats or mallinfo with DEBUG set will
    attempt to check every non-mmapped allocated and free chunk in the
    course of computing the summmaries. (By nature, mmapped regions
    cannot be checked very much automatically.)

    Setting DEBUG may also be helpful if you are trying to modify 
    this code. The assertions in the check routines spell out in more 
    detail the assumptions and invariants underlying the algorithms.

*/

#if DEBUG 
#include <assert.h>
#else
#define assert(x) ((void)0)
#endif



/*
  REALLOC_ZERO_BYTES_FREES should be set if a call to
  realloc with zero bytes should be the same as a call to free.
  Some people think it should. Otherwise, since this malloc
  returns a unique pointer for malloc(0), so does realloc(p, 0). 
*/


/*   #define REALLOC_ZERO_BYTES_FREES */


/*
  HAVE_MEMCPY should be defined if you are not otherwise using
  ANSI STD C, but still have memcpy and memset in your C library
  and want to use them in calloc and realloc. Otherwise simple
  macro versions are defined here.
*/

#define HAVE_MEMCPY 

#if __STD_C || defined(HAVE_MEMCPY)

void* memset(void*, int, size_t);
void* memcpy(void*, const void*, size_t);

#define MALLOC_ZERO(charp, nbytes)  memset(charp, 0, nbytes)
#define MALLOC_COPY(dest,src,nbytes) memcpy((dest), (src), (nbytes))

#else

/* We only invoke with multiples of size_t units, with size_t alignment */

#define MALLOC_ZERO(charp, nbytes)                                            \
{                                                                             \
  size_t* mzp = (size_t*)(charp);                                             \
  size_t mzn = (nbytes) / sizeof(size_t);                                     \
  while (mzn-- > 0) *mzp++ = 0;                                               \
} 

#define MALLOC_COPY(dest,src,nbytes)                                          \
{                                                                             \
  size_t* mcsrc = (size_t*) src;                                              \
  size_t* mcdst = (size_t*) dest;                                             \
  long mcn = (nbytes) / sizeof(size_t);                                       \
  while (mcn-- > 0) *mcdst++ = *mcsrc++;                                      \
}

#endif

/*
  Define HAVE_MMAP to optionally make malloc() use mmap() to
  allocate very large blocks.  These will be returned to the
  operating system immediately after a free().
*/

#ifndef HAVE_MMAP
#define HAVE_MMAP 1
#endif

#if HAVE_MMAP

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

#endif /* HAVE_MMAP */

/*
  Access to system page size. To the extent possible, this malloc
  manages memory from the system in page-size units.
  
  The following mechanics for getpagesize were adapted from 
  bsd/gnu getpagesize.h 
*/

#ifndef malloc_getpagesize
#if defined(BSD) || defined(DGUX) || defined(HAVE_GETPAGESIZE)
   extern size_t getpagesize();
#  define malloc_getpagesize getpagesize()
#else
#  include <sys/param.h>
#  ifdef EXEC_PAGESIZE
#    define malloc_getpagesize EXEC_PAGESIZE
#  else
#    ifdef NBPG
#      ifndef CLSIZE
#        define malloc_getpagesize NBPG
#      else
#        define malloc_getpagesize (NBPG * CLSIZE)
#      endif
#    else 
#      ifdef NBPC
#        define malloc_getpagesize NBPC
#      else
#        ifdef PAGESIZE
#          define malloc_getpagesize PAGESIZE
#        else
#          define malloc_getpagesize (4096) /* just guess */
#        endif
#      endif
#    endif 
#  endif
#endif 
#endif

/*

  This version of malloc supports the standard SVID/XPG mallinfo
  routine that returns a struct containing the same kind of
  information you can get from malloc_stats. It should work on
  any SVID/XPG compliant system that has a /usr/include/malloc.h
  defining struct mallinfo. (If you'd like to install such a thing
  yourself, cut out the preliminary declarations as described above
  and below and save them in a malloc.h file. But there's no
  compelling reason to bother to do this.)

  The main declaration needed is the mallinfo struct that is returned
  (by-copy) by mallinfo().  The SVID/XPG malloinfo struct contains a
  bunch of fields, most of which are not even meaningful in this
  version of malloc. Some of these fields are are instead filled by
  mallinfo() with other numbers that might possibly be of interest.

  HAVE_USR_INCLUDE_MALLOC_H should be set if you have a
  /usr/include/malloc.h file that includes a declaration of struct
  mallinfo.  If so, it is included; else an SVID2/XPG2 compliant
  version is declared below.  These must be precisely the same for
  mallinfo() to work.

*/

/* #define HAVE_USR_INCLUDE_MALLOC_H */

#if HAVE_USR_INCLUDE_MALLOC_H
#include "/usr/include/malloc.h"
#else

/* SVID2/XPG mallinfo structure */

struct mallinfo {
  int arena;    /* total space allocated from system */
  int ordblks;  /* number of non-inuse chunks */
  int smblks;   /* unused -- always zero */
  int hblks;    /* number of mmapped regions */
  int hblkhd;   /* total space in mmapped regions */
  int usmblks;  /* unused -- always zero */
  int fsmblks;  /* unused -- always zero */
  int uordblks; /* total allocated space */
  int fordblks; /* total non-inuse space */
  int keepcost; /* top-most, releasable (via malloc_trim) space */
};	

/* SVID2/XPG mallopt options */

#define M_MXFAST  1    /* UNUSED in this malloc */
#define M_NLBLKS  2    /* UNUSED in this malloc */
#define M_GRAIN   3    /* UNUSED in this malloc */
#define M_KEEP    4    /* UNUSED in this malloc */

#endif

/* mallopt options that actually do something */

#define M_TRIM_THRESHOLD    -1
#define M_TOP_PAD           -2
#define M_MMAP_THRESHOLD    -3
#define M_MMAP_MAX          -4



#ifndef DEFAULT_TRIM_THRESHOLD
#define DEFAULT_TRIM_THRESHOLD (1024 * 1024)
#endif

/*
    M_TRIM_THRESHOLD is the maximum amount of unused top-most memory 
      to keep before releasing via malloc_trim in free().

      Automatic trimming is mainly useful in long-lived programs.
      Because trimming via sbrk can be slow on some systems, and can
      sometimes be wasteful (in cases where programs immediately
      afterward allocate more large chunks) the value should be high
      enough so that your overall system performance would improve by
      releasing.  

      The trim threshold and the mmap control parameters (see below)
      can be traded off with one another. Trimming and mmapping are
      two different ways of releasing unused memory back to the
      system. Between these two, it is often possible to keep
      system-level demands of a long-lived program down to a bare
      minimum. For example, in one test suite of sessions measuring
      the XF86 X server on Linux, using a trim threshold of 128K and a
      mmap threshold of 192K led to near-minimal long term resource
      consumption.  

      If you are using this malloc in a long-lived program, it should
      pay to experiment with these values.  As a rough guide, you
      might set to a value close to the average size of a process
      (program) running on your system.  Releasing this much memory
      would allow such a process to run in memory.  Generally, it's
      worth it to tune for trimming rather tham memory mapping when a
      program undergoes phases where several large chunks are
      allocated and released in ways that can reuse each other's
      storage, perhaps mixed with phases where there are no such
      chunks at all.  And in well-behaved long-lived programs,
      controlling release of large blocks via trimming versus mapping
      is usually faster.

      However, in most programs, these parameters serve mainly as
      protection against the system-level effects of carrying around
      massive amounts of unneeded memory. Since frequent calls to
      sbrk, mmap, and munmap otherwise degrade performance, the default
      parameters are set to relatively high values that serve only as
      safeguards.

      The default trim value is high enough to cause trimming only in
      fairly extreme (by current memory consumption standards) cases.
      It must be greater than page size to have any useful effect.  To
      disable trimming completely, you can set to (unsigned long)(-1);


*/


#ifndef DEFAULT_TOP_PAD
#define DEFAULT_TOP_PAD        (0)
#endif

/*
    M_TOP_PAD is the amount of extra `padding' space to allocate or 
      retain whenever sbrk is called. It is used in two ways internally:

      * When sbrk is called to extend the top of the arena to satisfy
        a new malloc request, this much padding is added to the sbrk
        request.

      * When malloc_trim is called automatically from free(),
        it is used as the `pad' argument.

      In both cases, the actual amount of padding is rounded 
      so that the end of the arena is always a system page boundary.

      The main reason for using padding is to avoid calling sbrk so
      often. Having even a small pad greatly reduces the likelihood
      that nearly every malloc request during program start-up (or
      after trimming) will invoke sbrk, which needlessly wastes
      time. 

      Automatic rounding-up to page-size units is normally sufficient
      to avoid measurable overhead, so the default is 0.  However, in
      systems where sbrk is relatively slow, it can pay to increase
      this value, at the expense of carrying around more memory than 
      the program needs.

*/


#ifndef DEFAULT_MMAP_THRESHOLD
#define DEFAULT_MMAP_THRESHOLD (128 * 1024)
#endif

/*

    M_MMAP_THRESHOLD is the request size threshold for using mmap() 
      to service a request. Requests of at least this size that cannot 
      be allocated using already-existing space will be serviced via mmap.  
      (If enough normal freed space already exists it is used instead.)

      Using mmap segregates relatively large chunks of memory so that
      they can be individually obtained and released from the host
      system. A request serviced through mmap is never reused by any
      other request (at least not directly; the system may just so
      happen to remap successive requests to the same locations).

      Segregating space in this way has the benefit that mmapped space
      can ALWAYS be individually released back to the system, which
      helps keep the system level memory demands of a long-lived
      program low. Mapped memory can never become `locked' between
      other chunks, as can happen with normally allocated chunks, which
      menas that even trimming via malloc_trim would not release them.

      However, it has the disadvantages that:

         1. The space cannot be reclaimed, consolidated, and then
            used to service later requests, as happens with normal chunks. 
         2. It can lead to more wastage because of mmap page alignment
            requirements
         3. It causes malloc performance to be more dependent on host
            system memory management support routines which may vary in
            implementation quality and may impose arbitrary
            limitations. Generally, servicing a request via normal
            malloc steps is faster than going through a system's mmap.

      All together, these considerations should lead you to use mmap
      only for relatively large requests.  


*/



#ifndef DEFAULT_MMAP_MAX
#if HAVE_MMAP
#define DEFAULT_MMAP_MAX       (8)
#else
#define DEFAULT_MMAP_MAX       (0)
#endif
#endif

/*
    M_MMAP_MAX is the maximum number of requests to simultaneously 
      service using mmap. This parameter exists because:

         1. Some systems have a limited number of internal tables for
            use by mmap.
         2. In most systems, overreliance on mmap can degrade overall
            performance.
         3. If a program allocates many large regions, it is probably
            better off using normal sbrk-based allocation routines that
            can reclaim and reallocate normal heap memory. Using a
            small value allows transition into this mode after the
            first few allocations.

      Setting to 0 disables all use of mmap.  If HAVE_MMAP is not set,
      the default value is 0, and attempts to set it to non-zero values
      in mallopt will fail.
*/



/* 

  Special defines for linux libc

  Except when compiled using these special defines for Linux libc
  using weak aliases, this malloc is NOT designed to work in
  multithreaded applications.  No semaphores or other concurrency
  control are provided to ensure that multiple malloc or free calls
  don't run at the same time, which could be disasterous. A single
  semaphore could be used across malloc, realloc, and free (which is
  essentially the effect of the linux weak alias approach). It would
  be hard to obtain finer granularity.

*/


#ifdef INTERNAL_LINUX_C_LIB

#if __STD_C

Void_t * __default_morecore_init (ptrdiff_t);
Void_t *(*__morecore)(ptrdiff_t) = __default_morecore_init;

#else

Void_t * __default_morecore_init ();
Void_t *(*__morecore)() = __default_morecore_init;

#endif

#define MORECORE (*__morecore)
#define MORECORE_FAILURE 0

#else /* INTERNAL_LINUX_C_LIB */

#if __STD_C
extern Void_t*     sbrk(ptrdiff_t);
#else
extern Void_t*     sbrk();
#endif

#ifndef MORECORE
#define MORECORE sbrk
#endif

#ifndef MORECORE_FAILURE
#define MORECORE_FAILURE -1
#endif

#endif /* INTERNAL_LINUX_C_LIB */

#if defined(INTERNAL_LINUX_C_LIB) && defined(__ELF__)

#define cALLOc		__libc_calloc
#define fREe		__libc_free
#define mALLOc		__libc_malloc
#define mEMALIGn	__libc_memalign
#define rEALLOc		__libc_realloc
#define vALLOc		__libc_valloc
#define mALLINFo	__libc_mallinfo
#define mALLOPt		__libc_mallopt

#pragma weak calloc = __libc_calloc
#pragma weak free = __libc_free
#pragma weak cfree = __libc_free
#pragma weak malloc = __libc_malloc
#pragma weak memalign = __libc_memalign
#pragma weak realloc = __libc_realloc
#pragma weak valloc = __libc_valloc
#pragma weak mallinfo = __libc_mallinfo
#pragma weak mallopt = __libc_mallopt

#else

#define cALLOc		calloc
#define fREe		free
#define mALLOc		malloc
#define mEMALIGn	memalign
#define rEALLOc		realloc
#define vALLOc		valloc
#define mALLINFo	mallinfo
#define mALLOPt		mallopt

#endif

/* Public routines */

#if __STD_C

Void_t* mALLOc(size_t);
void    fREe(Void_t*);
Void_t* rEALLOc(Void_t*, size_t);
Void_t* mEMALIGn(size_t, size_t);
Void_t* vALLOc(size_t);
Void_t* cALLOc(size_t, size_t);
void    cfree(Void_t*);
int     malloc_trim(size_t);
size_t  malloc_usable_size(Void_t*);
void    malloc_stats();
int     mALLOPt(int, int);
struct mallinfo mALLINFo(void);
#else
Void_t* mALLOc();
void    fREe();
Void_t* rEALLOc();
Void_t* mEMALIGn();
Void_t* vALLOc();
Void_t* cALLOc();
void    cfree();
int     malloc_trim();
size_t  malloc_usable_size();
void    malloc_stats();
int     mALLOPt();
struct mallinfo mALLINFo();
#endif


#ifdef __cplusplus
};  /* end of extern "C" */
#endif

/* ---------- To make a malloc.h, end cutting here ------------ */




/*
  Type declarations
*/


struct malloc_chunk
{
  size_t size;               /* Size in bytes, including overhead. */
  struct malloc_chunk* fd;   /* double links -- used only if free. */
  struct malloc_chunk* bk;
  size_t unused;             /* to pad decl to min chunk size */
};

typedef struct malloc_chunk* mchunkptr;

/*

   malloc_chunk details:

    (The following includes lightly edited explanations by Colin Plumb.)

    Chunks of memory are maintained using a `boundary tag' method as
    described in e.g., Knuth or Standish.  (See the paper by Paul
    Wilson ftp://ftp.cs.utexas.edu/pub/garbage/allocsrv.ps for a
    survey of such techniques.)  Sizes of free chunks are stored both
    in the front of each chunk and at the end.  This makes
    consolidating fragmented chunks into bigger chunks very fast.  The
    size fields also hold bits representing whether chunks are free or
    in use.

    An allocated chunk looks like this:  


    chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of chunk, in bytes                         |P|
      mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             User data starts here...                          .
            .                                                               .
            .             (malloc_usable_space() bytes)                     .
            .                                                               |
nextchunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of next chunk                              |1|
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


    Where "chunk" is the front of the chunk for the purpose of most of
    the malloc code, but "mem" is the pointer that is returned to the
    user.  "Nextchunk" is the beginning of the next contiguous chunk.

    Chunks always begin on odd-word boundries, so the mem portion
    (which is returned to the user) is on an even word boundary, and
    thus double-word aligned.

    Free chunks are stored in circular doubly-linked lists, and look like this:

    chunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    `head:' |             Size of chunk, in bytes                         |P|
      mem-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Forward pointer to next chunk in list             |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Back pointer to previous chunk in list            |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Unused space (may be 0 bytes long)                .
            .                                                               .
            .                                                               |
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    `foot:' |             Size of chunk, in bytes                           |
nextchunk-> +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            |             Size of next chunk                              |0|
            +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    The P (PREV_INUSE) bit, stored in the unused low-order bit of the
    chunk size (which is always a multiple of two words), is an in-use
    bit for the *previous* chunk.  If that bit is *clear*, then the
    word before the current chunk size contains the previous chunk
    size, and can be used to find the front of the previous chunk.
    (The very first chunk allocated always has this bit set,
    preventing access to non-existent (or non-owned) memory.)

    The two exceptions to all this are 

     1. The special chunk `top', which doesn't bother using the 
        trailing size field since there is no
        next contiguous chunk that would have to index off it. (After
        initialization, `top' is forced to always exist.  If it would
        become less than MINSIZE bytes long, it is replenished via
        malloc_extend_top.)

     2. Chunks allocated via mmap, which have the second-lowest-order
        bit (IS_MMAPPED) set in their size fields.  Because they are
        never merged or traversed from any other chunk, they have no
        foot size or inuse information.

    Available chunks are kept in any of several places (all declared below):

    * `av': An array of chunks serving as bin headers for consolidated
       chunks. Each bin is doubly linked.  The bins are approximately
       proportionally (log) spaced.  There are a lot of these bins
       (128). This may look excessive, but works very well in
       practice.  All procedures maintain the invariant that no
       consolidated chunk physically borders another one. Chunks in
       bins are kept in size order, with ties going to the
       approximately least recently used chunk.

       The chunks in each bin are maintained in decreasing sorted order by
       size.  This is irrelevant for the small bins, which all contain
       the same-sized chunks, but facilitates best-fit allocation for
       larger chunks. (These lists are just sequential. Keeping them in
       order almost never requires enough traversal to warrant using
       fancier ordered data structures.)  Chunks of the same size are
       linked with the most recently freed at the front, and allocations
       are taken from the back.  This results in LRU or FIFO allocation
       order, which tends to give each chunk an equal opportunity to be
       consolidated with adjacent freed chunks, resulting in larger free
       chunks and less fragmentation. 

    * `top': The top-most available chunk (i.e., the one bordering the
       end of available memory) is treated specially. It is never
       included in any bin, is used only if no other chunk is
       available, and is released back to the system if it is very
       large (see M_TRIM_THRESHOLD).

    * `last_remainder': A bin holding only the remainder of the
       most recently split (non-top) chunk. This bin is checked
       before other non-fitting chunks, so as to provide better
       locality for runs of sequentially allocated chunks. 

    *  Implicitly, through the host system's memory mapping tables.
       If supported, requests greater than a threshold are usually 
       serviced via calls to mmap, and then later released via munmap.

*/






/*  sizes, alignments */

#define SIZE_SZ                (sizeof(size_t))
#define MALLOC_ALIGNMENT       (SIZE_SZ + SIZE_SZ)
#define MALLOC_ALIGN_MASK      (MALLOC_ALIGNMENT - 1)
#define MINSIZE                (sizeof(struct malloc_chunk))

/* conversion from malloc headers to user pointers, and back */

#define chunk2mem(p)   ((Void_t*)((char*)(p) + SIZE_SZ))
#define mem2chunk(mem) ((mchunkptr)((char*)(mem) - SIZE_SZ))

/* pad request bytes into a usable size */

#define request2size(req) \
  (((long)(req) < (long)(MINSIZE - SIZE_SZ)) ?  MINSIZE : \
   (((req) + SIZE_SZ + MALLOC_ALIGN_MASK) & ~(MALLOC_ALIGN_MASK)))


/* Check if m has acceptable alignment */

#define aligned_OK(m)    (((size_t)((m)) & (MALLOC_ALIGN_MASK)) == 0)




/* 
  Physical chunk operations  
*/


/* size field is or'ed with PREV_INUSE when previous adjacent chunk in use */

#define PREV_INUSE 0x1 

/* size field is or'ed with IS_MMAPPED if the chunk was obtained with mmap() */

#define IS_MMAPPED 0x2

/* Bits to mask off when extracting size */

#define SIZE_BITS (PREV_INUSE|IS_MMAPPED)


/* Ptr to next physical malloc_chunk. */

#define next_chunk(p) ((mchunkptr)( ((char*)(p)) + ((p)->size & ~PREV_INUSE) ))

/* Ptr to previous physical malloc_chunk */

#define prev_chunk(p)\
   ((mchunkptr)( ((char*)(p)) - *((size_t*)((char*)(p) - SIZE_SZ))))


/* Treat space at ptr + offset as a chunk */

#define chunk_at_offset(p, s)  ((mchunkptr)(((char*)(p)) + (s)))




/* 
  Dealing with use bits 
*/

/* extract p's inuse bit */

#define inuse(p)\
((((mchunkptr)(((char*)(p))+((p)->size & ~PREV_INUSE)))->size) & PREV_INUSE)

/* extract inuse bit of previous chunk */

#define prev_inuse(p)  ((p)->size & PREV_INUSE)

/* check for mmap()'ed chunk */

#define chunk_is_mmapped(p) ((p)->size & IS_MMAPPED)

/* set/clear chunk as in use without otherwise disturbing */

#define set_inuse(p)\
((mchunkptr)(((char*)(p)) + ((p)->size & ~PREV_INUSE)))->size |= PREV_INUSE

#define clear_inuse(p)\
((mchunkptr)(((char*)(p)) + ((p)->size & ~PREV_INUSE)))->size &= ~(PREV_INUSE)

/* check/set/clear inuse bits in known places */

#define inuse_bit_at_offset(p, s)\
 (((mchunkptr)(((char*)(p)) + (s)))->size & PREV_INUSE)

#define set_inuse_bit_at_offset(p, s)\
 (((mchunkptr)(((char*)(p)) + (s)))->size |= PREV_INUSE)

#define clear_inuse_bit_at_offset(p, s)\
 (((mchunkptr)(((char*)(p)) + (s)))->size &= ~(PREV_INUSE))




/* 
  Dealing with size fields 
*/

/* Get size, ignoring use bits */

#define chunksize(p)          ((p)->size & ~(SIZE_BITS))

/* Set size at head, without disturbing its use bit */

#define set_head_size(p, s)   ((p)->size = (((p)->size & PREV_INUSE) | (s)))

/* Set size/use ignoring previous bits in header */

#define set_head(p, s)        ((p)->size = (s))

/* Set size at footer (only when chunk is not in use) */

#define set_foot(p, s)   (*((size_t*)((char*)(p) + (s) - SIZE_SZ)) = (s))

/* Get size of previous (but not inuse) chunk */

#define prev_size(p)          (*((size_t*)((char*)(p) - SIZE_SZ)))





/*
   Bins

    The bins, `av_' are an array of pairs of pointers serving as the
    heads of (initially empty) doubly-linked lists of chunks, laid out
    in a way so that each pair can be treated as if it were in a
    malloc_chunk. (This way, the fd/bk offsets for linking bin heads
    and chunks are the same).

    Bins for sizes < 512 bytes contain chunks of all the same size, spaced
    8 bytes apart. Larger bins are approximately logarithmically
    spaced. (See the table below.) The `av_' array is never mentioned
    directly in the code, but instead via bin access macros.

    Bin layout:

    64 bins of size       8
    32 bins of size      64
    16 bins of size     512
     8 bins of size    4096
     4 bins of size   32768
     2 bins of size  262144
     1 bin  of size what's left

    There is actually a little bit of slop in the numbers in bin_index
    for the sake of speed. This makes no difference elsewhere.

    The special chunks `top' and `last_remainder' get their own bins,
    (this is implemented via yet more trickery with the av_ array),
    although `top' is never properly linked to its bin since it is
    always handled specially.

*/

#define NAV             128   /* number of bins */

typedef struct malloc_chunk* mbinptr;

/* access macros */

#define bin_at(i)      ((mbinptr)(&(av_[2 * (i)])))
#define next_bin(b)    ((mbinptr)((char*)(b) + 2 * sizeof(mbinptr)))

/*
   The first 2 bins are never indexed. The corresponding av_ cells are instead
   used for bookkeeping. This is not to save space, but to simplify
   indexing, maintain locality, and avoid some initialization tests.
*/

#define top            (bin_at(0)->fd)   /* The topmost chunk */
#define last_remainder (bin_at(1))       /* remainder from last split */


/*
   Because top initially points to its own bin with initial
   zero size, thus forcing extension on the first malloc request, 
   we avoid having any special code in malloc to check whether 
   it even exists yet. But we still need to in malloc_extend_top.
*/

#define initial_top    ((mchunkptr)(av_)) 

/* Helper macro to initialize bins */

#define IAV(i)  (mbinptr)(av_ + 2 * i), (mbinptr)(av_ + 2 * i)

static mbinptr av_[NAV * 2 + 2] = {
 0, 
 IAV(0),   IAV(1),   IAV(2),   IAV(3),   IAV(4),   IAV(5),   IAV(6),   IAV(7),
 IAV(8),   IAV(9),   IAV(10),  IAV(11),  IAV(12),  IAV(13),  IAV(14),  IAV(15),
 IAV(16),  IAV(17),  IAV(18),  IAV(19),  IAV(20),  IAV(21),  IAV(22),  IAV(23),
 IAV(24),  IAV(25),  IAV(26),  IAV(27),  IAV(28),  IAV(29),  IAV(30),  IAV(31),
 IAV(32),  IAV(33),  IAV(34),  IAV(35),  IAV(36),  IAV(37),  IAV(38),  IAV(39),
 IAV(40),  IAV(41),  IAV(42),  IAV(43),  IAV(44),  IAV(45),  IAV(46),  IAV(47),
 IAV(48),  IAV(49),  IAV(50),  IAV(51),  IAV(52),  IAV(53),  IAV(54),  IAV(55),
 IAV(56),  IAV(57),  IAV(58),  IAV(59),  IAV(60),  IAV(61),  IAV(62),  IAV(63),
 IAV(64),  IAV(65),  IAV(66),  IAV(67),  IAV(68),  IAV(69),  IAV(70),  IAV(71),
 IAV(72),  IAV(73),  IAV(74),  IAV(75),  IAV(76),  IAV(77),  IAV(78),  IAV(79),
 IAV(80),  IAV(81),  IAV(82),  IAV(83),  IAV(84),  IAV(85),  IAV(86),  IAV(87),
 IAV(88),  IAV(89),  IAV(90),  IAV(91),  IAV(92),  IAV(93),  IAV(94),  IAV(95),
 IAV(96),  IAV(97),  IAV(98),  IAV(99),  IAV(100), IAV(101), IAV(102), IAV(103),
 IAV(104), IAV(105), IAV(106), IAV(107), IAV(108), IAV(109), IAV(110), IAV(111),
 IAV(112), IAV(113), IAV(114), IAV(115), IAV(116), IAV(117), IAV(118), IAV(119),
 IAV(120), IAV(121), IAV(122), IAV(123), IAV(124), IAV(125), IAV(126), IAV(127),
 0
};



/* field-extraction macros */

#define first(b) ((b)->fd)
#define last(b)  ((b)->bk)

/* 
  Indexing into bins
*/

#define bin_index(sz)                                                          \
(((((unsigned long)(sz)) >> 9) ==    0) ?       (((unsigned long)(sz)) >>  3): \
 ((((unsigned long)(sz)) >> 9) <=    4) ?  56 + (((unsigned long)(sz)) >>  6): \
 ((((unsigned long)(sz)) >> 9) <=   20) ?  91 + (((unsigned long)(sz)) >>  9): \
 ((((unsigned long)(sz)) >> 9) <=   84) ? 110 + (((unsigned long)(sz)) >> 12): \
 ((((unsigned long)(sz)) >> 9) <=  340) ? 119 + (((unsigned long)(sz)) >> 15): \
 ((((unsigned long)(sz)) >> 9) <= 1364) ? 124 + (((unsigned long)(sz)) >> 18): \
                                          126)                     
/* 
  bins for chunks < 512 are all spaced 8 bytes apart, and hold
  identically sized chunks. This is exploited in malloc.
*/

#define MAX_SMALLBIN         63
#define MAX_SMALLBIN_SIZE   512
#define SMALLBIN_WIDTH        8

#define smallbin_index(sz)  (((unsigned long)(sz)) >> 3)

/* 
   Requests are `small' if both the corresponding and the next bin are small
*/

#define is_small_request(nb) (nb < MAX_SMALLBIN_SIZE - SMALLBIN_WIDTH)



/*
    To help compensate for the large number of bins, a one-level index
    structure is used for bin-by-bin searching.  `binblocks' is a
    one-word bitvector recording whether groups of BINBLOCKWIDTH bins
    have any (possibly) non-empty bins, so they can be skipped over
    all at once during during traversals. The bits are NOT always
    cleared as soon as all bins in a block are empty, but instead only
    when all are noticed to be empty during traversal in malloc.
*/

#define BINBLOCKWIDTH     4   /* bins per block */

#define binblocks      (bin_at(0)->size) /* bitvector of nonempty blocks */

/* bin<->block macros */

#define idx2binblock(ix)    (1 << (ix / BINBLOCKWIDTH))
#define mark_binblock(ii)   (binblocks |= idx2binblock(ii))
#define clear_binblock(ii)  (binblocks &= ~(idx2binblock(ii)))




/*  Other static bookkeeping data */

/* variables holding tunable values */

static unsigned long trim_threshold   = DEFAULT_TRIM_THRESHOLD;
static unsigned long top_pad          = DEFAULT_TOP_PAD;
static unsigned int  n_mmaps_max      = DEFAULT_MMAP_MAX;
static unsigned long mmap_threshold   = DEFAULT_MMAP_THRESHOLD;

/* The first value returned from sbrk */
static char* sbrk_base = (char*)(-1);

/* The maximum memory obtained from system via sbrk */
static unsigned long max_sbrked_mem = 0; 

/* The maximum via either sbrk or mmap */
static unsigned long max_total_mem = 0; 

/* internal working copy of mallinfo */
static struct mallinfo current_mallinfo = {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* The total memory obtained from system via sbrk */
#define sbrked_mem  (current_mallinfo.arena)

/* Tracking mmaps */

static unsigned int n_mmaps = 0;
static unsigned int max_n_mmaps = 0;
static unsigned long mmapped_mem = 0;
static unsigned long max_mmapped_mem = 0;



/* 
  Debugging support 
*/

#if DEBUG


/*
  These routines make a number of assertions about the states
  of data structures that should be true at all times. If any
  are not true, it's very likely that a user program has somehow
  trashed memory. (It's also possible that there is a coding error
  in malloc. In which case, please report it!)
*/

#if __STD_C
static void do_check_chunk(mchunkptr p) 
#else
static void do_check_chunk(p) mchunkptr p;
#endif
{ 
  size_t sz = p->size & ~PREV_INUSE;

  /* No checkable chunk is mmapped */
  assert(!chunk_is_mmapped(p));

  /* Check for legal address ... */
  assert((char*)p >= sbrk_base);
  if (p != top) 
    assert((char*)p + sz <= (char*)top);
  else
    assert((char*)p + sz <= sbrk_base + sbrked_mem);

}


#if __STD_C
static void do_check_free_chunk(mchunkptr p) 
#else
static void do_check_free_chunk(p) mchunkptr p;
#endif
{ 
  size_t sz = p->size & ~PREV_INUSE;
  mchunkptr next = chunk_at_offset(p, sz);

  do_check_chunk(p);

  /* Check whether it claims to be free ... */
  assert(!inuse(p));

  /* Unless a special marker, must have OK fields */
  if ((long)sz >= (long)MINSIZE)
  {
    assert((sz & MALLOC_ALIGN_MASK) == 0);
    assert((((size_t)((char*)(p) + SIZE_SZ)) & MALLOC_ALIGN_MASK) == 0);
    /* ... matching footer field */
    assert(*((size_t*)((char*)(p) + sz - SIZE_SZ)) == sz);
    /* ... and is fully consolidated */
    assert(prev_inuse(p));
    assert (next == top || inuse(next));
    
    /* ... and has minimally sane links */
    assert(p->fd->bk == p);
    assert(p->bk->fd == p);
  }
  else /* markers are always of size SIZE_SZ */
    assert(sz == SIZE_SZ); 
}

#if __STD_C
static void do_check_inuse_chunk(mchunkptr p) 
#else
static void do_check_inuse_chunk(p) mchunkptr p;
#endif
{ 
  mchunkptr next = next_chunk(p);
  do_check_chunk(p);

  /* Check whether it claims to be in use ... */
  assert(inuse(p));

  /* ... and is surrounded by OK chunks.
    Since more things can be checked with free chunks than inuse ones,
    if an inuse chunk borders them and debug is on, it's worth doing them.
  */
  if (!prev_inuse(p)) 
  {
    mchunkptr prv = prev_chunk(p);
    assert(next_chunk(prv) == p);
    do_check_free_chunk(prv);
  }
  if (next == top)
    assert(prev_inuse(next));
  else if (!inuse(next))
    do_check_free_chunk(next);

}

#if __STD_C
static void do_check_malloced_chunk(mchunkptr p, size_t s) 
#else
static void do_check_malloced_chunk(p, s) mchunkptr p; size_t s;
#endif
{
  size_t sz = p->size & ~PREV_INUSE;
  long room = sz - s;

  do_check_inuse_chunk(p);

  /* Legal size ... */
  assert((long)sz >= (long)MINSIZE);
  assert((sz & MALLOC_ALIGN_MASK) == 0);
  assert(room >= 0);
  assert(room < (long)MINSIZE);

  /* ... and alignment */
  assert((((size_t)((char*)(p) + SIZE_SZ)) & MALLOC_ALIGN_MASK) == 0);


  /* ... and was allocated at front of an available chunk */
  assert(prev_inuse(p));

}


#define check_free_chunk(P)  do_check_free_chunk(P)
#define check_inuse_chunk(P) do_check_inuse_chunk(P)
#define check_chunk(P) do_check_chunk(P)
#define check_malloced_chunk(P,N) do_check_malloced_chunk(P,N)
#else
#define check_free_chunk(P) 
#define check_inuse_chunk(P)
#define check_chunk(P)
#define check_malloced_chunk(P,N)
#endif



/* 
  Macro-based internal utilities
*/


/*  
  Linking chunks in bin lists.
  Call these only with variables, not arbitrary expressions, as arguments.
*/

/* 
  Place chunk p of size s in its bin, in size order,
  putting it ahead of others of same size.
*/


#define frontlink(P, S, IDX, BK, FD)                                          \
{                                                                             \
  if (S < MAX_SMALLBIN_SIZE)                                                  \
  {                                                                           \
    IDX = smallbin_index(S);                                                  \
    mark_binblock(IDX);                                                       \
    BK = bin_at(IDX);                                                         \
    FD = BK->fd;                                                              \
    P->bk = BK;                                                               \
    P->fd = FD;                                                               \
    FD->bk = BK->fd = P;                                                      \
  }                                                                           \
  else                                                                        \
  {                                                                           \
    IDX = bin_index(S);                                                       \
    BK = bin_at(IDX);                                                         \
    FD = BK->fd;                                                              \
    if (FD == BK) mark_binblock(IDX);                                         \
    else                                                                      \
    {                                                                         \
      while (FD != BK && S < chunksize(FD)) FD = FD->fd;                      \
      BK = FD->bk;                                                            \
    }                                                                         \
    P->bk = BK;                                                               \
    P->fd = FD;                                                               \
    FD->bk = BK->fd = P;                                                      \
  }                                                                           \
}


/* take a chunk off a list */

#define unlink(P, BK, FD)                                                     \
{                                                                             \
  BK = P->bk;                                                                 \
  FD = P->fd;                                                                 \
  FD->bk = BK;                                                                \
  BK->fd = FD;                                                                \
}                                                                             \

/* Place p as the last remainder */

#define link_last_remainder(P)                                                \
{                                                                             \
  last_remainder->fd = last_remainder->bk =  P;                               \
  P->fd = P->bk = last_remainder;                                             \
}

/* Clear the last_remainder bin */

#define clear_last_remainder \
  (last_remainder->fd = last_remainder->bk = last_remainder)






/* Routines dealing with mmap(). */

#if HAVE_MMAP

#if __STD_C
static mchunkptr mmap_chunk(size_t size)
#else
static mchunkptr mmap_chunk(size) size_t size;
#endif
{
  size_t offset = (MALLOC_ALIGNMENT) - SIZE_SZ;
  size_t page_mask = malloc_getpagesize - 1;
  char *cp;
  mchunkptr p;

#ifndef MAP_ANONYMOUS
  static int fd = -1;
#endif

  if(n_mmaps >= n_mmaps_max) return 0; /* too many regions */

  /* The offset to the start of the mmapped region is stored
   * in a size_t field immediately before the chunk.
   */
  size = (size + offset + page_mask) & ~page_mask;

#ifdef MAP_ANONYMOUS
  cp = (char *)mmap(0, size, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
#else /* !MAP_ANONYMOUS */
  if (fd < 0) 
  {
    fd = open("/dev/zero", O_RDWR);
    if(fd < 0) return 0;
  }
  cp = (char *)mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
#endif

  if(cp == (char *)-1) return 0;

  n_mmaps++;
  if (n_mmaps > max_n_mmaps) max_n_mmaps = n_mmaps;
  
  p = (mchunkptr)(cp + offset);

  /* We demand that eight bytes into a page must be 8-byte aligned. */
  assert(aligned_OK(chunk2mem(p)));

  *((size_t *)p - 1) = offset;
  set_head(p, (size - offset)|IS_MMAPPED);
  
  mmapped_mem += size;
  if ((unsigned long)mmapped_mem > (unsigned long)max_mmapped_mem) 
    max_mmapped_mem = mmapped_mem;
  if ((unsigned long)(mmapped_mem + sbrked_mem) > (unsigned long)max_total_mem) 
    max_total_mem = mmapped_mem + sbrked_mem;
  return p;
}

#if __STD_C
static void munmap_chunk(mchunkptr p)
#else
static void munmap_chunk(p) mchunkptr p;
#endif
{
  size_t offset = *((size_t *)p - 1);
  size_t size = chunksize(p);
  int ret;

  assert (chunk_is_mmapped(p));
  assert(! ((char*)p >= sbrk_base && (char*)p < sbrk_base + sbrked_mem));
  assert((n_mmaps > 0));
  assert(((size + offset) & (malloc_getpagesize-1)) == 0);

  ret = munmap((char *)p - offset, size + offset);

  /* munmap returns non-zero on failure */
  assert(ret == 0);

  n_mmaps--;
  mmapped_mem -= (size + offset);
}

#endif /* HAVE_MMAP */




/* 
  Extend the top-most chunk by obtaining memory from system.
  Main interface to sbrk (but see also malloc_trim).
*/

#if __STD_C
static void malloc_extend_top(size_t nb)
#else
static void malloc_extend_top(nb) size_t nb;
#endif
{
  char*     brk;                 /* return value from sbrk */
  size_t    front_misalign;      /* unusable bytes at front of sbrked space */
  size_t    correction;          /* bytes for 2nd sbrk call */
  char*     new_brk;             /* return of 2nd sbrk call */
  size_t    top_size;            /* new size of top chunk */

  mchunkptr old_top      = top;  /* Record state of old top */
  size_t    old_top_size = chunksize(old_top);
  char*     old_end      = (char*)(chunk_at_offset(old_top, old_top_size));

  /* Pad request with top_pad plus minimal overhead */
  
  size_t    sbrk_size     = nb + top_pad + MINSIZE;
  unsigned long pagesz    = malloc_getpagesize;

  /* If not the first time through, round to preserve page boundary */
  /* Otherwise, we need to correct to a page size below anyway. */
  /* (We also correct below if an intervening foreign sbrk call.) */

  if (sbrk_base != (char*)(-1))
    sbrk_size = (sbrk_size + (pagesz - 1)) & ~(pagesz - 1);

  brk = (char*)(MORECORE (sbrk_size));

  /* Fail if sbrk failed or if a foreign sbrk call killed our space */
  if (brk == (char*)(MORECORE_FAILURE) || 
      (brk < old_end && old_top != initial_top))
    return;     

  sbrked_mem += sbrk_size;

  if (brk == old_end) /* can just add bytes to current top */
  {
    top_size = sbrk_size + old_top_size;
    set_head(top, top_size | PREV_INUSE);
  }
  else
  {
    if (sbrk_base == (char*)(-1))  /* First time through. Record base */
      sbrk_base = brk;
    else  /* Someone else called sbrk().  Count those bytes as sbrked_mem. */
      sbrked_mem += brk - (char*)old_end;

    /* Guarantee alignment of first new chunk made from this space */
    front_misalign = (size_t)chunk2mem(brk) & MALLOC_ALIGN_MASK;
    if (front_misalign > 0) 
    {
      correction = (MALLOC_ALIGNMENT) - front_misalign;
      brk += correction;
    }
    else
      correction = 0;

    /* Guarantee the next brk will be at a page boundary */
    correction += pagesz - ((size_t)(brk + sbrk_size) & (pagesz - 1));

    /* Allocate correction */
    new_brk = (char*)(MORECORE (correction));
    if (new_brk == (char*)(MORECORE_FAILURE)) return; 

    sbrked_mem += correction;

    top = (mchunkptr)brk;
    top_size = new_brk - brk + correction;
    set_head(top, top_size | PREV_INUSE);

    if (old_top != initial_top)
    {
      /* There must have been an intervening foreign sbrk call. */
      /* A double fencepost is necessary to prevent consolidation */
      chunk_at_offset(old_top, old_top_size - 2*SIZE_SZ)->size = 
        SIZE_SZ|PREV_INUSE;
      chunk_at_offset(old_top, old_top_size -   SIZE_SZ)->size = 
        SIZE_SZ|PREV_INUSE;

      /* Also keep size a multiple of MINSIZE */
      old_top_size = (old_top_size - 2*SIZE_SZ) & MALLOC_ALIGN_MASK;
      chunk_at_offset(old_top, old_top_size          )->size =
        SIZE_SZ|PREV_INUSE;
      chunk_at_offset(old_top, old_top_size + SIZE_SZ)->size =
        SIZE_SZ|PREV_INUSE;
      set_head_size(old_top, old_top_size);
      /* If possible, release the rest. */
      if (old_top_size >= MINSIZE) 
        fREe(chunk2mem(old_top));
    }
  }

  if ((unsigned long)sbrked_mem > (unsigned long)max_sbrked_mem) 
    max_sbrked_mem = sbrked_mem;
  if ((unsigned long)(mmapped_mem + sbrked_mem) > (unsigned long)max_total_mem) 
    max_total_mem = mmapped_mem + sbrked_mem;

  /* We always land on a page boundary */
  assert(((size_t)((char*)top + top_size) & (pagesz - 1)) == 0);
}




/* Main public routines */


/*
  Malloc Algorthim:

    The requested size is first converted into a usable form, `nb'.
    This currently means to add 4 bytes overhead plus possibly more to
    obtain 8-byte alignment and/or to obtain a size of at least
    MINSIZE (currently 16 bytes), the smallest allocatable size.
    (All fits are considered `exact' if they are within MINSIZE bytes.)

    From there, the first successful of the following steps is taken:

      1. The bin corresponding to the request size is scanned, and if
         a chunk of exactly the right size is found, it is taken.

      2. The most recently remaindered chunk is used if it is big
         enough.  This is a form of (roving) first fit, used only in
         the absence of exact fits. Runs of consecutive requests use
         the remainder of the chunk used for the previous such request
         whenever possible. This limited use of a first-fit style
         allocation strategy tends to give contiguous chunks
         coextensive lifetimes, which improves locality and can reduce
         fragmentation in the long run.

      3. Other bins are scanned in increasing size order, using a
         chunk big enough to fulfill the request, and splitting off
         any remainder.  This search is strictly by best-fit; i.e.,
         the smallest (with ties going to approximately the least
         recently used) chunk that fits is selected.

      4. If large enough, the chunk bordering the end of memory
         (`top') is split off. (This use of `top' is in accord with
         the best-fit search rule.  In effect, `top' is treated as
         larger (and thus less well fitting) than any other available
         chunk since it can be extended to be as large as necessary
         (up to system limitations).

      5. If the request size meets the mmap threshold and the
         system supports mmap, and there are few enough currently
         allocated mmapped regions, and a call to mmap succeeds,
         the request is allocated via direct memory mapping.

      6. Otherwise, the top of memory is extended by
         obtaining more space from the system (normally using sbrk,
         but definable to anything else via the MORECORE macro).
         Memory is gathered from the system (in system page-sized
         units) in a way that allows chunks obtained across different
         sbrk calls to be consolidated, but does not require
         contiguous memory. Thus, it should be safe to intersperse
         mallocs with other sbrk calls.


      All allocations are made from the the `lowest' part of any found
      chunk. (The implementation invariant is that prev_inuse is
      always true of any allocated chunk; i.e., that each allocated
      chunk borders either a previously allocated and still in-use chunk,
      or the base of its memory arena.)

*/

#if __STD_C
Void_t* mALLOc(size_t bytes)
#else
Void_t* mALLOc(bytes) size_t bytes;
#endif
{
  mchunkptr victim;                  /* inspected/selected chunk */
  size_t    victim_size;             /* its size */
  int       idx;                     /* index for bin traversal */
  mbinptr   bin;                     /* associated bin */
  mchunkptr remainder;               /* remainder from a split */
  long      remainder_size;          /* its size */
  int       remainder_index;         /* its bin index */
  unsigned long block;               /* block traverser bit */
  int       startidx;                /* first bin of a traversed block */
  mchunkptr fwd;                     /* misc temp for linking */
  mchunkptr bck;                     /* misc temp for linking */

  size_t nb  = request2size(bytes);  /* padded request size; */

  /* Check for exact match in a bin */

  if (is_small_request(nb))  /* Faster version for small requests */
  {
    idx = smallbin_index(nb); 

    /* No traversal or size check necessary for small bins.  */
    /* Also scan the next one, since it would have a remainder < MINSIZE */

    if ( ((victim = last(bin_at(idx)))   != bin_at(idx)) || 
         ((victim = last(bin_at(idx+1))) != bin_at(idx+1)))
    {
      victim_size = chunksize(victim);
      unlink(victim, bck, fwd);
      set_inuse_bit_at_offset(victim, victim_size);
      check_malloced_chunk(victim, nb);
      return chunk2mem(victim);
    }

    idx += 2; /* Set for bin scan below. We've already scanned 2 bins. */

  }
  else
  {
    idx = bin_index(nb);
    bin = bin_at(idx);

    for (victim = last(bin); victim != bin; victim = victim->bk)
    {
      victim_size = chunksize(victim);
      remainder_size = victim_size - nb;
      
      if (remainder_size >= (long)MINSIZE) /* too big */
      {
        --idx; /* adjust to rescan below after checking last remainder */
        break;   
      }

      else if (remainder_size >= 0) /* exact fit */
      {
        unlink(victim, bck, fwd);
        set_inuse_bit_at_offset(victim, victim_size);
        check_malloced_chunk(victim, nb);
        return chunk2mem(victim);
      }
    }

    ++idx; 

  }

  /* Try to use the last split-off remainder */

  if ( (victim = last_remainder->fd) != last_remainder)
  {
    victim_size = chunksize(victim);
    remainder_size = victim_size - nb;

    if (remainder_size >= (long)MINSIZE) /* re-split */
    {
      remainder = chunk_at_offset(victim, nb);
      set_head(victim, nb | PREV_INUSE);
      link_last_remainder(remainder);
      set_head(remainder, remainder_size | PREV_INUSE);
      set_foot(remainder, remainder_size);
      check_malloced_chunk(victim, nb);
      return chunk2mem(victim);
    }

    clear_last_remainder;

    if (remainder_size >= 0)  /* exhaust */
    {
      set_inuse_bit_at_offset(victim, victim_size);
      check_malloced_chunk(victim, nb);
      return chunk2mem(victim);
    }

    /* Else place in bin */

    frontlink(victim, victim_size, remainder_index, bck, fwd);
  }

  /* 
     If there are any possibly nonempty big-enough blocks, 
     search for best fitting chunk by scanning bins in blockwidth units.
  */

  if ( (block = idx2binblock(idx)) <= binblocks)  
  {

    /* Get to the first marked block */

    if ( (block & binblocks) == 0) 
    {
      /* force to an even block boundary */
      idx = (idx & ~(BINBLOCKWIDTH - 1)) + BINBLOCKWIDTH;
      block <<= 1;
      while ((block & binblocks) == 0)
      {
        idx += BINBLOCKWIDTH;
        block <<= 1;
      }
    }
      
    /* For each possibly nonempty block ... */
    for (;;)  
    {
      startidx = idx;          /* (track incomplete blocks) */

      /* For each bin in this block ... */
      do           
      { 
        bin = bin_at(idx);

        /* Find and use first big enough chunk ... */

        for (victim = last(bin); victim != bin; victim = victim->bk)
        {
          victim_size = chunksize(victim);
          remainder_size = victim_size - nb;

          if (remainder_size >= (long)MINSIZE) /* split */
          {
            remainder = chunk_at_offset(victim, nb);
            set_head(victim, nb | PREV_INUSE);
            unlink(victim, bck, fwd);
            link_last_remainder(remainder);
            set_head(remainder, remainder_size | PREV_INUSE);
            set_foot(remainder, remainder_size);
            check_malloced_chunk(victim, nb);
            return chunk2mem(victim);
          }

          else if (remainder_size >= 0)  /* take */
          {
            set_inuse_bit_at_offset(victim, victim_size);
            unlink(victim, bck, fwd);
            check_malloced_chunk(victim, nb);
            return chunk2mem(victim);
          }

        }

      } while ((++idx & (BINBLOCKWIDTH - 1)) != 0);

      /* Clear out the block bit. */

      do   /* Possibly backtrack to try to clear a partial block */
      {
        if ((startidx & (BINBLOCKWIDTH - 1)) == 0)
        {
          binblocks &= ~block;
          break;
        }
        --startidx;
      } while (first(bin_at(startidx)) == bin_at(startidx));

      /* Get to the next possibly nonempty block */

      if ( (block <<= 1) <= binblocks && (block != 0) ) 
      {
        while ((block & binblocks) == 0)
        {
          idx += BINBLOCKWIDTH;
          block <<= 1;
        }
      }
      else
        break;
    }
  }


  /* Try to use top chunk */

  /* Require that there be a remainder, ensuring top always exists  */
  if ( (remainder_size = chunksize(top) - nb) < (long)MINSIZE)
  {

#if HAVE_MMAP
    /* If big and would otherwise need to extend, try to use mmap instead */
    if ((unsigned long)nb >= (unsigned long)mmap_threshold &&
        (victim = mmap_chunk(nb)) != 0)
      return chunk2mem(victim);
#endif

    /* Try to extend */
    malloc_extend_top(nb);
    if ( (remainder_size = chunksize(top) - nb) < (long)MINSIZE)
      return 0; /* propagate failure */
  }

  victim = top;
  set_head(victim, nb | PREV_INUSE);
  top = chunk_at_offset(victim, nb);
  set_head(top, remainder_size | PREV_INUSE);
  check_malloced_chunk(victim, nb);
  return chunk2mem(victim);

}




/*

  free() algorithm :

    cases:

       1. free(0) has no effect.  

       2. If the chunk was allocated via mmap, it is release via munmap().

       3. If a returned chunk borders the current high end of memory,
          it is consolidated into the top, and if the total unused
          topmost memory exceeds the trim threshold, malloc_trim is
          called.

       4. Other chunks are consolidated as they arrive, and
          placed in corresponding bins. (This includes the case of
          consolidating with the current `last_remainder').

*/


#if __STD_C
void fREe(Void_t* mem)
#else
void fREe(mem) Void_t* mem;
#endif
{
  mchunkptr p;         /* chunk corresponding to mem */
  size_t    hd;        /* its head field */
  size_t    sz;        /* its size */
  int       idx;       /* its bin index */
  mchunkptr next;      /* next contiguous chunk */
  size_t    nextsz;    /* its size */
  size_t    prevsz;    /* size of previous contiguous chunk */
  mchunkptr bck;       /* misc temp for linking */
  mchunkptr fwd;       /* misc temp for linking */
  int       islr;      /* track whther merging with last_remainder */

  if (mem == 0)                              /* free(0) has no effect */
    return;

  p = mem2chunk(mem);
  hd = p->size;

#if HAVE_MMAP
  if (hd & IS_MMAPPED)                       /* release mmapped memory. */
  {
    munmap_chunk(p);
    return;
  }
#endif
  
  check_inuse_chunk(p);
  
  sz = hd & ~PREV_INUSE;
  next = chunk_at_offset(p, sz);
  nextsz = chunksize(next);
  
  if (next == top)                            /* merge with top */
  {
    sz += nextsz;

    if (!(hd & PREV_INUSE))                    /* consolidate backward */
    {
      prevsz = prev_size(p);
      p = chunk_at_offset(p, -prevsz);
      sz += prevsz;
      unlink(p, bck, fwd);
    }

    set_head(p, sz | PREV_INUSE);
    top = p;
    if ((unsigned long)(sz) >= (unsigned long)trim_threshold) 
      malloc_trim(top_pad); 
    return;
  }

  set_head(next, nextsz);                    /* clear inuse bit */

  islr = 0;

  if (!(hd & PREV_INUSE))                    /* consolidate backward */
  {
    prevsz = prev_size(p);
    p = chunk_at_offset(p, -prevsz);
    sz += prevsz;
    
    if (p->fd == last_remainder)             /* keep as last_remainder */
      islr = 1;
    else
      unlink(p, bck, fwd);
  }
  
  if (!(inuse_bit_at_offset(next, nextsz)))   /* consolidate forward */
  {
    sz += nextsz;
    
    if (!islr && next->fd == last_remainder)  /* re-insert last_remainder */
    {
      islr = 1;
      link_last_remainder(p);   
    }
    else
      unlink(next, bck, fwd);
  }


  set_head(p, sz | PREV_INUSE);
  set_foot(p, sz);
  if (!islr)
    frontlink(p, sz, idx, bck, fwd);  
}





/*

  Realloc algorithm:

    If the reallocation is for additional space, and the chunk can be
    extended, it is, else a malloc-copy-free sequence is taken.  There
    are several different ways that a chunk could be extended. All are
    tried:

       * Extending forward into following adjacent free chunk.
       * Shifting backwards, joining preceding adjacent space
       * Both shifting backwards and extending forward.
       * Extending into newly sbrked space


    If the reallocation is for less space, the trailing space is
    lopped off and freed.  Unless the #define REALLOC_ZERO_BYTES_FREES
    is set, realloc with a size argument of zero (re)allocates a
    minimum-sized chunk.

    Chunks that were obtained via mmap cannot be extended or shrunk.
    If their reallocation is for additional space, they are copied.
    If for less, they are just left alone.

    The old unix realloc convention of allowing the last-free'd chunk
    to be used as an argument to realloc is no longer supported.
    I don't know of any programs still relying on this feature,
    and allowing it would also allow too many other incorrect 
    usages of realloc to be sensible.


*/


#if __STD_C
Void_t* rEALLOc(Void_t* oldmem, size_t bytes)
#else
Void_t* rEALLOc(oldmem, bytes) Void_t* oldmem; size_t bytes;
#endif
{
  size_t    nb;               /* padded request size */

  mchunkptr oldp;             /* chunk corresponding to oldmem */
  size_t    oldsize;          /* its size */

  mchunkptr newp;             /* chunk to return */
  size_t    newsize;          /* its size */
  Void_t*   newmem;           /* corresponding user mem */

  mchunkptr next;             /* next contiguous chunk after oldp */
  size_t    nextsize;         /* its size */

  mchunkptr prev;             /* previous contiguous chunk before oldp */
  size_t    prevsize;         /* its size */

  mchunkptr remainder;        /* holds split off extra space from newp */
  size_t    remainder_size;   /* its size */

  mchunkptr bck;              /* misc temp for linking */
  mchunkptr fwd;              /* misc temp for linking */

#ifdef REALLOC_ZERO_BYTES_FREES
  if (bytes == 0) { fREe(oldmem); return 0; }
#endif


  /* realloc of null is supposed to be same as malloc */
  if (oldmem == 0) return mALLOc(bytes);

  newp    = oldp    = mem2chunk(oldmem);
  newsize = oldsize = chunksize(oldp);


  nb = request2size(bytes);

#if HAVE_MMAP
  if (chunk_is_mmapped(oldp)) 
  {
    if(oldsize >= nb) return oldmem; /* do nothing */
    /* Must alloc, copy, free. */
    newmem = mALLOc(bytes);
    if (newmem == 0) return 0; /* propagate failure */
    MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
    munmap_chunk(oldp);
    return newmem;
  }
#endif

  check_inuse_chunk(oldp);

  if ((long)(oldsize) < (long)(nb))  
  {

    /* Try expanding forward */

    next = chunk_at_offset(oldp, oldsize);
    if (next == top || !inuse(next)) 
    {
      nextsize = chunksize(next);

      /* Forward into top only if a remainder */
      if (next == top)
      {
        if ((long)(nextsize + newsize) >= (long)(nb + MINSIZE))
        {
          newsize += nextsize;
          top = chunk_at_offset(oldp, nb);
          set_head(top, (newsize - nb) | PREV_INUSE);
          set_head_size(oldp, nb);
          return chunk2mem(oldp);
        }
      }

      /* Forward into next chunk */
      else if (((long)(nextsize + newsize) >= (long)(nb)))
      { 
        unlink(next, bck, fwd);
        newsize  += nextsize;
        goto split;
      }
    }
    else
    {
      next = 0;
      nextsize = 0;
    }

    /* Try shifting backwards. */

    if (!prev_inuse(oldp))
    {
      prev = prev_chunk(oldp);
      prevsize = chunksize(prev);

      /* try forward + backward first to save a later consolidation */

      if (next != 0)
      {
        /* into top */
        if (next == top)
        {
          if ((long)(nextsize + prevsize + newsize) >= (long)(nb + MINSIZE))
          {
            unlink(prev, bck, fwd);
            newp = prev;
            newsize += prevsize + nextsize;
            newmem = chunk2mem(newp);
            MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
            top = chunk_at_offset(newp, nb);
            set_head(top, (newsize - nb) | PREV_INUSE);
            set_head_size(newp, nb);
            return chunk2mem(newp);
          }
        }

        /* into next chunk */
        else if (((long)(nextsize + prevsize + newsize) >= (long)(nb)))
        {
          unlink(next, bck, fwd);
          unlink(prev, bck, fwd);
          newp = prev;
          newsize += nextsize + prevsize;
          newmem = chunk2mem(newp);
          MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
          goto split;
        }
      }
      
      /* backward only */
      if (prev != 0 && (long)(prevsize + newsize) >= (long)nb)  
      {
        unlink(prev, bck, fwd);
        newp = prev;
        newsize += prevsize;
        newmem = chunk2mem(newp);
        MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
        goto split;
      }
    }

    /* Must allocate */

    newmem = mALLOc (bytes);

    if (newmem == 0)  /* propagate failure */
      return 0; 

    /* Avoid copy if newp is next chunk after oldp. */
    /* (This can only happen when new chunk is sbrk'ed.) */

    if ( (newp = mem2chunk(newmem)) == next_chunk(oldp)) 
    {
      newsize += chunksize(newp);
      newp = oldp;
      goto split;
    }

    /* Otherwise copy, free, and exit */
    MALLOC_COPY(newmem, oldmem, oldsize - SIZE_SZ);
    fREe(oldmem);
    return newmem;
  }

 split:  /* split off extra room in old or expanded chunk */

  if (newsize - nb >= MINSIZE) /* split off remainder */
  {
    remainder = chunk_at_offset(newp, nb);
    remainder_size = newsize - nb;
    set_head_size(newp, nb);
    set_head(remainder, remainder_size | PREV_INUSE);
    set_inuse_bit_at_offset(remainder, remainder_size);
    fREe(chunk2mem(remainder)); /* let free() deal with it */
  }
  else
  {
    set_head_size(newp, newsize);
    set_inuse_bit_at_offset(newp, newsize);
  }

  check_inuse_chunk(newp);
  return chunk2mem(newp);
}




/*

  memalign algorithm:

    memalign requests more than enough space from malloc, finds a spot
    within that chunk that meets the alignment request, and then
    possibly frees the leading and trailing space. 

    The alignment argument must be a power of two. This property is not
    checked by memalign, so misuse may result in random runtime errors.

    8-byte alignment is guaranteed by normal malloc calls, so don't
    bother calling memalign with an argument of 8 or less.

    Overreliance on memalign is a sure way to fragment space.

*/


#if __STD_C
Void_t* mEMALIGn(size_t alignment, size_t bytes)
#else
Void_t* mEMALIGn(alignment, bytes) size_t alignment; size_t bytes;
#endif
{
  size_t    nb;               /* padded  request size */
  char*     m;                /* memory returned by malloc call */
  mchunkptr p;                /* corresponding chunk */
  char*     brk;              /* alignment point within p */
  mchunkptr newp;             /* chunk to return */
  size_t    newsize;          /* its size */
  size_t    leadsize;         /* leading space befor alignment point */
  mchunkptr remainder;        /* spare room at end to split off */
  long      remainder_size;   /* its size */

  /* If need less alignment than we give anyway, just relay to malloc */

  if (alignment <= MALLOC_ALIGNMENT) return mALLOc(bytes);

  /* Otherwise, ensure that it is at least a minimum chunk size */
  
  if (alignment <  MINSIZE) alignment = MINSIZE;

  /* Call malloc with worst case padding to hit alignment. */

  nb = request2size(bytes);
  m  = (char*)(mALLOc(nb + alignment + MINSIZE));

  if (m == 0) return 0; /* propagate failure */

  p = mem2chunk(m);

  if ((((size_t)(m)) % alignment) == 0) /* aligned */
  {
#if HAVE_MMAP
    if(chunk_is_mmapped(p))
      return chunk2mem(p); /* nothing more to do */
#endif
  }
  else /* misaligned */
  {
    /* 
      Find an aligned spot inside chunk.
      Since we need to give back leading space in a chunk of at 
      least MINSIZE, if the first calculation places us at
      a spot with less than MINSIZE leader, we can move to the
      next aligned spot -- we've allocated enough total room so that
      this is always possible.
    */

    brk = (char*) ( (((size_t)(m + alignment - 1)) & -alignment) - SIZE_SZ );
    if ((long)(brk - (char*)(p)) < MINSIZE) brk = brk + alignment;

    newp = (mchunkptr)brk;
    leadsize = brk - (char*)(p);
    newsize = chunksize(p) - leadsize;

#if HAVE_MMAP
    if(chunk_is_mmapped(p)) 
    {
      *((size_t *)newp - 1) = *((size_t *)p - 1) + leadsize;
      set_head(newp, newsize|IS_MMAPPED);
      return chunk2mem(newp);
    }
#endif

    /* give back leader, use the rest */

    set_head(newp, newsize | PREV_INUSE);
    set_inuse_bit_at_offset(newp, newsize);
    set_head_size(p, leadsize);
    fREe(chunk2mem(p));
    p = newp;
  }

  /* Also give back spare room at the end */

  remainder_size = chunksize(p) - nb;

  if (remainder_size >= (long)MINSIZE)
  {
    remainder = chunk_at_offset(p, nb);
    set_head(remainder, remainder_size | PREV_INUSE);
    set_head_size(p, nb);
    fREe(chunk2mem(remainder));
  }

  check_inuse_chunk(p);
  return chunk2mem(p);

}




/*
    valloc just invokes memalign with alignment argument equal
    to the page size of the system (or as near to this as can
    be figured out from all the includes/defines above.)
*/

#if __STD_C
Void_t* vALLOc(size_t bytes)
#else
Void_t* vALLOc(bytes) size_t bytes;
#endif
{
  return mEMALIGn (malloc_getpagesize, bytes);
}


/*

  calloc calls malloc, then zeroes out the allocated chunk.

*/

#if __STD_C
Void_t* cALLOc(size_t n, size_t elem_size)
#else
Void_t* cALLOc(n, elem_size) size_t n; size_t elem_size;
#endif
{
  mchunkptr p;
  size_t csz;

  size_t sz = n * elem_size;
  Void_t* mem = mALLOc (sz);

  if (mem == 0) 
    return 0;
  else
  {
    p = mem2chunk(mem);

    if(chunk_is_mmapped(p)) /* no clearing is necessary */
      return mem;

    csz = chunksize(p);
    MALLOC_ZERO(mem, csz - SIZE_SZ);
    return mem;
  }
}

/*
 
  cfree just calls free. It is needed/defined on some systems
  that pair it with calloc, presumably for odd historical reasons.

*/

#if !defined(INTERNAL_LINUX_C_LIB) || !defined(__ELF__)
#if __STD_C
void cfree(Void_t *mem)
#else
void cfree(mem) Void_t *mem;
#endif
{
  free(mem);
}
#endif



/*

    Malloc_trim gives memory back to the system (via negative
    arguments to sbrk) if there is unused memory at the `high' end of
    the malloc pool. You can call this after freeing large blocks of
    memory to potentially reduce the system-level memory requirements
    of a program. However, it cannot guarantee to reduce memory. Under
    some allocation patterns, some large free blocks of memory will be
    locked between two used chunks, so they cannot be given back to
    the system.

    The `pad' argument to malloc_trim represents the amount of free
    trailing space to leave untrimmed. If this argument is zero,
    only the minimum amount of memory to maintain internal data
    structures will be left (one page or less). Non-zero arguments
    can be supplied to maintain enough trailing space to service
    future expected allocations without having to re-obtain memory
    from the system.

    Malloc_trim returns 1 if it actually released any memory, else 0.

*/

#if __STD_C
int malloc_trim(size_t pad)
#else
int malloc_trim(pad) size_t pad;
#endif
{
  long  top_size;        /* Amount of top-most memory */
  long  extra;           /* Amount to release */
  char* current_brk;     /* address returned by pre-check sbrk call */
  char* new_brk;         /* address returned by negative sbrk call */

  unsigned long pagesz = malloc_getpagesize;

  top_size = chunksize(top);
  extra = ((top_size - pad - MINSIZE + (pagesz-1)) / pagesz - 1) * pagesz;

  if (extra < (long)pagesz)  /* Not enough memory to release */
    return 0;

  else
  {
    /* Test to make sure no one else called sbrk */
    current_brk = (char*)(MORECORE (0));
    if (current_brk != (char*)(top) + top_size)
      return 0;     /* Apparently we don't own memory; must fail */

    else
    {
      new_brk = (char*)(MORECORE (-extra));
      
      if (new_brk == (char*)(MORECORE_FAILURE)) /* sbrk failed? */
      {
        /* Try to figure out what we have */
        current_brk = (char*)(MORECORE (0));
        top_size = current_brk - (char*)top;
        if (top_size >= (long)MINSIZE) /* if not, we are very very dead! */
        {
          sbrked_mem = current_brk - sbrk_base;
          set_head(top, top_size | PREV_INUSE);
        }
        check_chunk(top);
        return 0; 
      }

      else
      {
        /* Success. Adjust top accordingly. */
        set_head(top, (top_size - extra) | PREV_INUSE);
        sbrked_mem -= extra;
        check_chunk(top);
        return 1;
      }
    }
  }
}



/*
  malloc_usable_size:

    This routine tells you how many bytes you can actually use in an
    allocated chunk, which may be more than you requested (although
    often not). You can use this many bytes without worrying about
    overwriting other allocated objects. Not a particularly great
    programming practice, but still sometimes useful.

*/

#if __STD_C
size_t malloc_usable_size(Void_t* mem)
#else
size_t malloc_usable_size(mem) Void_t* mem;
#endif
{
  mchunkptr p;
  if (mem == 0)
    return 0;
  else
  {
    p = mem2chunk(mem);
    if(!chunk_is_mmapped(p))
    {
      if (!inuse(p)) return 0;
      check_inuse_chunk(p);
    }
    return chunksize(p) - SIZE_SZ;
  }
}




/* Utility to update current_mallinfo for malloc_stats and mallinfo() */

static void malloc_update_mallinfo() 
{
  int i;
  mbinptr b;
  mchunkptr p;
#if DEBUG
  mchunkptr q;
#endif

  size_t avail = chunksize(top);
  int   navail = ((long)(avail) >= (long)MINSIZE)? 1 : 0;

  for (i = 1; i < NAV; ++i)
  {
    b = bin_at(i);
    for (p = last(b); p != b; p = p->bk) 
    {
#if DEBUG
      check_free_chunk(p);
      for (q = next_chunk(p); 
           q < top && inuse(q) && (long)(chunksize(q)) >= (long)MINSIZE; 
           q = next_chunk(q))
        check_inuse_chunk(q);
#endif
      avail += chunksize(p);
      navail++;
    }
  }

  current_mallinfo.ordblks = navail;
  current_mallinfo.uordblks = sbrked_mem - avail;
  current_mallinfo.fordblks = avail;
  current_mallinfo.hblks = n_mmaps;
  current_mallinfo.hblkhd = mmapped_mem;
  current_mallinfo.keepcost = chunksize(top);

}



/*

  malloc_stats:

    Prints on stderr the amount of space obtain from the system (both
    via sbrk and mmap), the maximum amount (which may be more than
    current if malloc_trim and/or munmap got called), the maximum
    number of simultaneous mmap regions used, and the current number
    of bytes allocated via malloc (or realloc, etc) but not yet
    freed. (Note that this is the number of bytes allocated, not the
    number requested. It will be larger than the number requested
    because of alignment and bookkeeping overhead.)

*/

void malloc_stats()
{
  malloc_update_mallinfo();
  fprintf(stderr, "max system bytes = %10u\n", 
          (unsigned int)(max_total_mem));
  fprintf(stderr, "system bytes     = %10u\n", 
          (unsigned int)(sbrked_mem + mmapped_mem));
  fprintf(stderr, "in use bytes     = %10u\n", 
          (unsigned int)(current_mallinfo.uordblks + mmapped_mem));
#if HAVE_MMAP
  fprintf(stderr, "max mmap regions = %10u\n", 
          (unsigned int)max_n_mmaps);
#endif
}

/*
  mallinfo returns a copy of updated current mallinfo.
*/

struct mallinfo mALLINFo()
{
  malloc_update_mallinfo();
  return current_mallinfo;
}




/*
  mallopt:

    mallopt is the general SVID/XPG interface to tunable parameters.
    The format is to provide a (parameter-number, parameter-value) pair.
    mallopt then sets the corresponding parameter to the argument
    value if it can (i.e., so long as the value is meaningful),
    and returns 1 if successful else 0.

    See descriptions of tunable parameters above.

*/

#if __STD_C
int mALLOPt(int param_number, int value)
#else
int mALLOPt(param_number, value) int param_number; int value;
#endif
{
  switch(param_number) 
  {
    case M_TRIM_THRESHOLD:
      trim_threshold = value; return 1; 
    case M_TOP_PAD:
      top_pad = value; return 1; 
    case M_MMAP_THRESHOLD:
      mmap_threshold = value; return 1;
    case M_MMAP_MAX:
#if HAVE_MMAP
      n_mmaps_max = value; return 1;
#else
      if (value != 0) return 0; else  n_mmaps_max = value; return 1;
#endif

    default:
      return 0;
  }
}

/*

History:

    V2.6.2 Tue Dec  5 06:52:55 1995  Doug Lea  (dl at gee)
      * Integrated most documentation with the code.
      * Add support for mmap, with help from 
        Wolfram Gloger (Gloger@lrz.uni-muenchen.de).
      * Use last_remainder in more cases.
      * Pack bins using idea from  colin@nyx10.cs.du.edu
      * Use ordered bins instead of best-fit threshhold
      * Eliminate block-local decls to simplify tracing and debugging.
      * Support another case of realloc via move into top
      * Fix error occuring when initial sbrk_base not word-aligned.  
      * Rely on page size for units instead of SBRK_UNIT to
        avoid surprises about sbrk alignment conventions.
      * Add mallinfo, mallopt. Thanks to Raymond Nijssen
        (raymond@es.ele.tue.nl) for the suggestion. 
      * Add `pad' argument to malloc_trim and top_pad mallopt parameter.
      * More precautions for cases where other routines call sbrk,
        courtesy of Wolfram Gloger (Gloger@lrz.uni-muenchen.de).
      * Added macros etc., allowing use in linux libc from
        H.J. Lu (hjl@gnu.ai.mit.edu)
      * Inverted this history list

    V2.6.1 Sat Dec  2 14:10:57 1995  Doug Lea  (dl at gee)
      * Re-tuned and fixed to behave more nicely with V2.6.0 changes.
      * Removed all preallocation code since under current scheme
        the work required to undo bad preallocations exceeds
        the work saved in good cases for most test programs.
      * No longer use return list or unconsolidated bins since
        no scheme using them consistently outperforms those that don't
        given above changes.
      * Use best fit for very large chunks to prevent some worst-cases.
      * Added some support for debugging

    V2.6.0 Sat Nov  4 07:05:23 1995  Doug Lea  (dl at gee)
      * Removed footers when chunks are in use. Thanks to
        Paul Wilson (wilson@cs.texas.edu) for the suggestion.

    V2.5.4 Wed Nov  1 07:54:51 1995  Doug Lea  (dl at gee)
      * Added malloc_trim, with help from Wolfram Gloger 
        (wmglo@Dent.MED.Uni-Muenchen.DE).

    V2.5.3 Tue Apr 26 10:16:01 1994  Doug Lea  (dl at g)

    V2.5.2 Tue Apr  5 16:20:40 1994  Doug Lea  (dl at g)
      * realloc: try to expand in both directions
      * malloc: swap order of clean-bin strategy;
      * realloc: only conditionally expand backwards
      * Try not to scavenge used bins
      * Use bin counts as a guide to preallocation
      * Occasionally bin return list chunks in first scan
      * Add a few optimizations from colin@nyx10.cs.du.edu

    V2.5.1 Sat Aug 14 15:40:43 1993  Doug Lea  (dl at g)
      * faster bin computation & slightly different binning
      * merged all consolidations to one part of malloc proper
         (eliminating old malloc_find_space & malloc_clean_bin)
      * Scan 2 returns chunks (not just 1)
      * Propagate failure in realloc if malloc returns 0
      * Add stuff to allow compilation on non-ANSI compilers 
          from kpv@research.att.com
     
    V2.5 Sat Aug  7 07:41:59 1993  Doug Lea  (dl at g.oswego.edu)
      * removed potential for odd address access in prev_chunk
      * removed dependency on getpagesize.h
      * misc cosmetics and a bit more internal documentation
      * anticosmetics: mangled names in macros to evade debugger strangeness
      * tested on sparc, hp-700, dec-mips, rs6000 
          with gcc & native cc (hp, dec only) allowing
          Detlefs & Zorn comparison study (in SIGPLAN Notices.)

    Trial version Fri Aug 28 13:14:29 1992  Doug Lea  (dl at g.oswego.edu)
      * Based loosely on libg++-1.2X malloc. (It retains some of the overall 
         structure of old version,  but most details differ.)

*/
