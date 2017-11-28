/*
 * mpx hash
 */

#ifndef _MASH_
#define _MASH_

#define MPX_TABLE_CAPACITY 0x100000
#define MPX_TABLE_MASK (0x0FFFFF)

void _mash_store(unsigned long key, unsigned long tag, unsigned char* val);
void _mash_get(unsigned long key, unsigned long tag, unsigned char* buf);

#define mash_store(key, tag, val) \
{ \
    __asm__ __volatile__ \
    ("bndmov (%0), %%bnd0\n" \
     "bndstx %%bnd0, (%1,%2)\n" \
     : \
     :"r"((void*)val), "r"(key), "r"(tag) \
     :); \
}

#define mash_get(key, tag, buf)\
{ \
    __asm__ __volatile__ \
    ("bndldx (%0,%2), %%bnd1\n" \
     "bndmov %%bnd1,(%1)\n" \
     : \
     :"r"(key), "r"(buf), "r"(tag) \
     :); \
}

void mash_dummy();

extern unsigned long long mreq;
extern unsigned long long mhit;
extern unsigned long long mmiss_reason_not_exists;
extern unsigned long long mmiss_reason_key_mismatch;

#endif//_MASH_

