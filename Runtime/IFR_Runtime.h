/* mpxrt.h                  -*-C++-*-
 *
 *************************************************************************
 *
 *  @copyright
 *  Copyright (C) 2015, Intel Corporation
 *  All rights reserved.
 *
 *  @copyright
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  @copyright
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 *  WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 **************************************************************************/
#define _GNU_SOURCE
#include <ucontext.h>
#ifndef REG_RIP
#error "no REG_RIP?"
#endif

#ifdef __i386__

/* i386 directory size is 4MB.  */
#define NUM_L1_BITS 20
#define NUM_L2_BITS 10
#define NUM_IGN_BITS 2
#define MPX_L1_ADDR_MASK  0xfffff000UL
#define MPX_L2_ADDR_MASK  0xfffffffcUL
#define MPX_L2_VALID_MASK 0x00000001UL

#define REG_IP_IDX      REG_EIP
#define REX_PREFIX

#define XSAVE_OFFSET_IN_FPMEM    sizeof (struct _libc_fpstate)

#else /* __i386__ */

/* x86_64 directory size is 2GB.  */
#define NUM_L1_BITS 28
#define NUM_L2_BITS 17
#define NUM_IGN_BITS 3
#define MPX_L1_ADDR_MASK  0xfffffffffffff000ULL
#define MPX_L2_ADDR_MASK  0xfffffffffffffff8ULL
#define MPX_L2_VALID_MASK 0x0000000000000001ULL

#define REG_IP_IDX    REG_RIP
#define REX_PREFIX    "0x48, "

#define XSAVE_OFFSET_IN_FPMEM 0

#endif /* !__i386__ */

#define MPX_L1_SIZE ((1UL << NUM_L1_BITS) * sizeof (void *))

#include <stdarg.h>
#include <pthread.h>
#include <sys/types.h>


#define MAX_THDS 256


/* IFRs begin after acquire calls, after unknown function calls, at
   the beginning of basic blocks, and after variable declarations. */
extern "C" void IFRit_begin_ifrs(unsigned long, unsigned long, unsigned long, ... );
extern "C" void IFRit_begin_one_read_ifr(unsigned long, unsigned long);
extern "C" void IFRit_begin_one_write_ifr(unsigned long, unsigned long);

/* These calls have release semantics. All IFRs should be killed,
   except variables that are known to be accessed after the call. */
extern "C" int IFRit_pthread_mutex_unlock(pthread_mutex_t *, unsigned long,
					  unsigned long, ...);
extern "C" int IFRit_pthread_create(pthread_t *, const pthread_attr_t *,
				    void *(*thread) (void *), void *,
				    unsigned long numMay,
				    unsigned long numMust, ...);
extern "C" void IFRit_free(void *ptr, unsigned long, unsigned long, ...);
extern "C" int IFRit_pthread_rwlock_unlock(pthread_rwlock_t *rwlock,
					   unsigned long, unsigned long, ...);

/* These calls perform what amounts to a release followed by an
   acquire. That means other threads could safely modify any variable
   during the call, so all IFRs must be killed at this call. */
extern "C" int IFRit_pthread_cond_wait(pthread_cond_t *cond,
				       pthread_mutex_t *mutex);
extern "C" int IFRit_pthread_cond_timedwait(pthread_cond_t *cond,
					    pthread_mutex_t *mutex,
					    const struct timespec *abstime);
extern "C" int IFRit_pthread_barrier_wait(pthread_barrier_t *);
extern "C" void *IFRit_realloc(void *ptr, size_t size);

/* Generic end IFR call */
extern "C" void IFRit_end_ifrs();
