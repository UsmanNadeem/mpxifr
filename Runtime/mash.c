/*
 * mpx hash
 */

#include "mpxrt.h"
#include "mash.h"

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/prctl.h>

void mash_dummy()
{}

void _mash_store(unsigned long key, unsigned long tag, unsigned char* val)
{
    __asm__ __volatile__
    ("bndmov (%0), %%bnd0\n"
     "bndstx %%bnd0, (%1,%2)\n"
     :
     :"r"((void*)val), "r"(key), "r"(tag)
     :);
}

void _mash_get(unsigned long key, unsigned long tag, unsigned char* buf)
{
    __asm__ __volatile__
    ("bndldx (%0,%2), %%bnd1\n"
     "bndmov %%bnd1,(%1)\n"
     :
     :"r"(key), "r"(buf), "r"(tag)
     :);
}

/////////////////////////////////////////////////////////////////////////////
//runtime support
#define MPX_ENABLE_BIT_NO 0
#define BNDPRESERVE_BIT_NO 1

struct xsave_hdr_struct
{
  uint64_t xstate_bv;
  uint64_t reserved1[2];
  uint64_t reserved2[5];
} __attribute__ ((packed));

struct bndregs_struct
{
  uint64_t bndregs[8];
} __attribute__ ((packed));

struct bndcsr_struct {
	uint64_t cfg_reg_u;
	uint64_t status_reg;
} __attribute__((packed));

struct xsave_struct
{
  uint8_t fpu_sse[512];
  struct xsave_hdr_struct xsave_hdr;
  uint8_t ymm[256];
  uint8_t lwp[128];
  struct bndregs_struct bndregs;
  struct bndcsr_struct bndcsr;
} __attribute__ ((packed));

/* Following vars are initialized at process startup only
   and thus are considered to be thread safe.  */
static void *l1base = NULL;
static int bndpreserve = 1;
static int enable = 1;

static inline void
xrstor_state (struct xsave_struct *fx, uint64_t mask)
{
  uint32_t lmask = mask;
  uint32_t hmask = mask >> 32;

  asm volatile (".byte " REX_PREFIX "0x0f,0xae,0x2f\n\t"
		: : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		:   "memory");
}

static void
enable_mpx (void)
{
    fprintf(stderr, "[MPX] MPX ENABLE\n");
    uint8_t __attribute__ ((__aligned__ (64))) buffer[4096];
    struct xsave_struct *xsave_buf = (struct xsave_struct *)buffer;

    memset (buffer, 0, sizeof (buffer));
    xrstor_state (xsave_buf, 0x18);

    /* Enable MPX.  */
    xsave_buf->xsave_hdr.xstate_bv = 0x10;
    xsave_buf->bndcsr.cfg_reg_u = (unsigned long)l1base;
    xsave_buf->bndcsr.cfg_reg_u |= enable << MPX_ENABLE_BIT_NO;
    xsave_buf->bndcsr.cfg_reg_u |= bndpreserve << BNDPRESERVE_BIT_NO;
    xsave_buf->bndcsr.status_reg = 0;

    xrstor_state (xsave_buf, 0x10);
    prctl(43,0,0,0,0);
}

static void
disable_mpx (void)
{
    uint8_t __attribute__ ((__aligned__ (64))) buffer[4096];
    struct xsave_struct *xsave_buf = (struct xsave_struct *)buffer;

    memset(buffer, 0, sizeof(buffer));
    xrstor_state(xsave_buf, 0x18);

    /* Disable MPX.  */
    xsave_buf->xsave_hdr.xstate_bv = 0x10;
    xsave_buf->bndcsr.cfg_reg_u = 0;
    xsave_buf->bndcsr.status_reg = 0;

    xrstor_state(xsave_buf, 0x10);
    prctl(44,0,0,0,0);
}

static void mpxrt_populate_mpxtable()
{
    fprintf(stderr, "[MPX] MPX POPULATE\n");
    void* buf[] = {0,0};
    for (unsigned long i=0;i<MPX_TABLE_CAPACITY;i++)
    {
        int a = 0;
        __asm__ __volatile__ 
        ("bndmov (%0), %%bnd0\n"
         "bndstx %%bnd0, (%1,%1)\n"
         :
         :"r"((void*)buf), "r"(i)
         :);
    }
}

void __attribute__ ((constructor (1005))) mpxrt_prepare (void)
{
    fprintf(stderr, "[MPX] MPX PREPARE\n");

    //setup buffer
    l1base = mmap (NULL, MPX_L1_SIZE, PROT_READ | PROT_WRITE,
        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    //and enable mpx
    enable_mpx ();
    //pre-populate mpx table
    mpxrt_populate_mpxtable();
}

unsigned long long mreq;
unsigned long long mhit;
unsigned long long mmiss_reason_not_exists;
unsigned long long mmiss_reason_key_mismatch;

void __attribute__ ((destructor(1005))) mpxrt_cleanup (void)
{
    fprintf(stderr, "[MPX] MPX CLEANUP\n");
    //disable mpx
    disable_mpx();
    //and free buffer
    munmap (l1base, MPX_L1_SIZE);
    // #if 1
    //print statistics
    // printf("cache query %lld, hit %lld , %.2f%% Hit Ratio\n",
    //     mreq, mhit,
    //     100.0*mhit/mreq);
    // printf("Miss reason: \n"
    //         "   not exists: %lld\n"
    //         "   key mismatch: %lld\n",
    //         mmiss_reason_not_exists,
    //         mmiss_reason_key_mismatch);
    // #endif
}

