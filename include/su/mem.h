/*@ Memory: tools like copy, move etc., and a heap allocation interface.
 *
 * Copyright (c) 2001 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef su_MEM_H
#define su_MEM_H
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
/* A memset that is not optimized away */
EXPORT_DATA void * (* volatile su_mem_set_volatile)(void*, int, uz);
EXPORT sz su_mem_cmp(void const *vpa, void const *vpb, uz len);
EXPORT void *su_mem_copy(void *vp, void const *src, uz len);
EXPORT void *su_mem_find(void const *vp, s32 what, uz len);
EXPORT void *su_mem_rfind(void const *vp, s32 what, uz len);
EXPORT void *su_mem_move(void *vp, void const *src, uz len);
EXPORT void *su_mem_set(void *vp, s32 what, uz len);
#if (defined su_HAVE_DEBUG && !defined su_HAVE_MEM_CANARIES_DISABLE) ||\
      defined DOXYGEN
# define su_MEM_ALLOC_DEBUG
#endif
enum su_mem_alloc_flags{
   su_MEM_ALLOC_NONE,
   su_MEM_ALLOC_CLEAR = 1u<<1,
   su_MEM_ALLOC_32BIT_OVERFLOW = 1u<<2,
   su_MEM_ALLOC_31BIT_OVERFLOW = 1u<<3,
   su_MEM_ALLOC_OVERFLOW_OK = su_STATE_ERR_OVERFLOW,
   su_MEM_ALLOC_NOMEM_OK = su_STATE_ERR_NOMEM,
   su_MEM_ALLOC_MAYFAIL = su_STATE_ERR_PASS,
   su_MEM_ALLOC_MUSTFAIL = su_STATE_ERR_NOPASS,
   su__MEM_ALLOC_MARK_SHIFT = 16u,
   su_MEM_ALLOC_MARK_0 = 0u<<su__MEM_ALLOC_MARK_SHIFT,
   su_MEM_ALLOC_MARK_1 = 1u<<su__MEM_ALLOC_MARK_SHIFT,
   su_MEM_ALLOC_MARK_2 = 2u<<su__MEM_ALLOC_MARK_SHIFT,
   su_MEM_ALLOC_MARK_3 = 3u<<su__MEM_ALLOC_MARK_SHIFT,
   su_MEM_ALLOC_MARK_AUTO = su_MEM_ALLOC_MARK_1,
   su_MEM_ALLOC_MARK_AUTO_HUGE = su_MEM_ALLOC_MARK_2,
   su_MEM_ALLOC_MARK_LOFI = su_MEM_ALLOC_MARK_3,
   su__MEM_ALLOC_MARK_MAX = 3u,
   su__MEM_ALLOC_MARK_MASK = 3u,
   su__MEM_ALLOC_USER_MASK = 0xFF | su_STATE_ERR_MASK |
         (su__MEM_ALLOC_MARK_MASK << su__MEM_ALLOC_MARK_SHIFT)
};
#ifdef su_SOURCE_MEM_ALLOC
CTA((su_STATE_ERR_MASK & ~0xFF00u) == 0,
   "Reuse of low order bits impossible, or mask excesses storage");
#endif
enum{
   su_MEM_ALLOC_MIN = Z_ALIGN(1)
};
enum su_mem_conf_option{
   su_MEM_CONF_NONE,
   /* su_MEM_ALLOC_DEBUG only: booleans */
   su_MEM_CONF_DEBUG = 1u<<0,
   su_MEM_CONF_ON_ERROR_EMERG = 1u<<1,
   su_MEM_CONF_LINGER_FREE = 1u<<2,
   su_MEM_CONF_LINGER_FREE_RELEASE = 1u<<3,
/*madvise,free area count*/
/*say_if_empty_on_exit,statistics*/
   su__MEM_CONF_MAX = su_MEM_CONF_LINGER_FREE_RELEASE
};
#ifdef su_MEM_ALLOC_DEBUG
EXPORT boole su__mem_get_can_book(uz size, uz no);
EXPORT boole su__mem_check(su_DBG_LOC_ARGS_DECL_SOLE);
EXPORT boole su__mem_trace(su_DBG_LOC_ARGS_DECL_SOLE);
#endif
EXPORT void *su_mem_allocate(uz size, uz no, u32 maf  su_DBG_LOC_ARGS_DECL);
EXPORT void *su_mem_reallocate(void *ovp, uz size, uz no, u32 maf
      su_DBG_LOC_ARGS_DECL);
EXPORT void su_mem_free(void *ovp  su_DBG_LOC_ARGS_DECL);
#define su_MEM_ALLOCATE(SZ,NO,F) \
      su_mem_allocate(SZ, NO, F  su_DBG_LOC_ARGS_INJ)
#define su_MEM_REALLOCATE(OVP,SZ,NO,F) \
      su_mem_reallocate(OVP, SZ, NO, F  su_DBG_LOC_ARGS_INJ)
#ifdef su_HAVE_DBG_LOC_ARGS
# define su_MEM_ALLOCATE_LOC(SZ,NO,F,FNAME,LNNO) \
      su_mem_allocate(SZ, NO, F, FNAME, LNNO)
# define su_MEM_REALLOCATE_LOC(OVP,SZ,NO,F,FNAME,LNNO) \
      su_mem_reallocate(OVP, SZ, NO, F, FNAME, LNNO)
#else
# define su_MEM_ALLOCATE_LOC(SZ,NO,F,FNAME,LNNO) \
      su_mem_allocate(SZ, NO, F)
# define su_MEM_REALLOCATE_LOC(OVP,SZ,NO,F,FNAME,LNNO) \
      su_mem_reallocate(OVP, SZ, NO, F)
#endif
/* The "normal" interface; there X, X_LOC, and X_LOCOR */
#define su_MEM_ALLOC(SZ) su_MEM_ALLOCATE(SZ, 1, su_MEM_ALLOC_NONE)
#define su_MEM_ALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, 1, su_MEM_ALLOC_NONE, FNAME, LNNO)
#define su_MEM_ALLOC_N(SZ,NO) su_MEM_ALLOCATE(SZ, NO, su_MEM_ALLOC_NONE)
#define su_MEM_ALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, NO, su_MEM_ALLOC_NONE, FNAME, LNNO)
#define su_MEM_CALLOC(SZ) su_MEM_ALLOCATE(SZ, 1, su_MEM_ALLOC_CLEAR)
#define su_MEM_CALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, 1, su_MEM_ALLOC_CLEAR, FNAME, LNNO)
#define su_MEM_CALLOC_N(SZ,NO) su_MEM_ALLOCATE(SZ, NO, su_MEM_ALLOC_CLEAR)
#define su_MEM_CALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, NO, su_MEM_ALLOC_CLEAR, FNAME, LNNO)
#define su_MEM_REALLOC(OVP,SZ) su_MEM_REALLOCATE(OVP, SZ, 1, su_MEM_ALLOC_NONE)
#define su_MEM_REALLOC_LOC(OVP,SZ,FNAME,LNNO) \
      su_MEM_REALLOCATE_LOC(OVP, SZ, 1, su_MEM_ALLOC_NONE, FNAME, LNNO)
#define su_MEM_REALLOC_N(OVP,SZ,NO) \
      su_MEM_REALLOCATE(OVP, SZ, NO, su_MEM_ALLOC_NONE)
#define su_MEM_REALLOC_N_LOC(OVP,SZ,NO,FNAME,LNNO) \
      su_MEM_REALLOCATE_LOC(OVP, SZ, NO, su_MEM_ALLOC_NONE, FNAME, LNNO)
#define su_MEM_TALLOC(T,NO) su_S(T *,su_MEM_ALLOC_N(sizeof(T), NO))
#define su_MEM_TALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_ALLOC_N_LOC(sizeof(T), NO, FNAME, LNNO))
#define su_MEM_TCALLOC(T,NO) su_S(T *,su_MEM_CALLOC_N(sizeof(T), NO))
#define su_MEM_TCALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_CALLOC_N_LOC(sizeof(T), NO, FNAME, LNNO))
#define su_MEM_TREALLOC(T,OVP,NO) \
      su_S(T *,su_MEM_REALLOC_N(OVP, sizeof(T), NO))
#define su_MEM_TREALLOC_LOC(T,OVP,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_REALLOC_N_LOC(OVP, sizeof(T), NO, FNAME, LNNO))
#define su_MEM_TALLOCF(T,NO,F) su_S(T *,su_MEM_ALLOCATE(sizeof(T), NO, F))
#define su_MEM_TALLOCF_LOC(T,NO,F,FNAME,LNNO) \
      su_S(T *,su_MEM_ALLOCATE_LOC(sizeof(T), NO, F, FNAME, LNNO))
#define su_MEM_TCALLOCF(T,NO,F) \
   su_S(T *,su_MEM_ALLOCATE(sizeof(T), NO, su_MEM_ALLOC_CLEAR | (F)))
#define su_MEM_TCALLOCF_LOC(T,NO,F,FNAME,LNNO) \
   su_S(T *,su_MEM_ALLOCATE_LOC(sizeof(T), NO, su_MEM_ALLOC_CLEAR | (F)),\
      FNAME, LNNO)
#define su_MEM_TREALLOCF(T,OVP,NO,F) \
      su_S(T *,su_MEM_REALLOCATE(OVP, sizeof(T), NO, F))
#define su_MEM_TREALLOCF_LOC(T,OVP,NO,F,FNAME,LNNO) \
      su_S(T *,su_MEM_REALLOCATE_LOC(OVP, sizeof(T), NO, F, FNAME, LNNO))
#define su_MEM_FREE(OVP) su_mem_free(OVP  su_DBG_LOC_ARGS_INJ)
#ifdef su_HAVE_DBG_LOC_ARGS
# define su_MEM_FREE_LOC(OVP,FNAME,LNNO) su_mem_free(OVP, FNAME, LNNO)
#else
# define su_MEM_FREE_LOC(OVP,FNAME,LNNO) su_mem_free(OVP)
#endif
/* (The painful _LOCOR series) */
#ifdef su_HAVE_DBG_LOC_ARGS
# define su_MEM_ALLOC_LOCOR(SZ,ORARGS) su_MEM_ALLOC_LOC(SZ, ORARGS)
# define su_MEM_ALLOC_N_LOCOR(SZ,NO,ORARGS) su_MEM_ALLOC_N_LOC(SZ, NO, ORARGS)
# define su_MEM_CALLOC_LOCOR(SZ,ORARGS) su_MEM_CALLOC_LOC(SZ, ORGARGS)
# define su_MEM_CALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_CALLOC_N_LOC(SZ, NO, ORARGS)
# define su_MEM_REALLOC_LOCOR(OVP,SZ,ORARGS) \
      su_MEM_REALLOC_LOC(OVP, SZ, ORARGS)
# define su_MEM_REALLOC_N_LOCOR(OVP,SZ,NO,ORARGS) \
      su_MEM_REALLOC_N_LOC(OVP, SZ, NO, ORARGS)
# define su_MEM_TALLOC_LOCOR(T,NO,ORARGS) su_MEM_TALLOC_LOC(T, NO, ORARGS)
# define su_MEM_TCALLOC_LOCOR(T,NO,ORARGS) su_MEM_TCALLOC_LOC(T, NO, ORARGS)
# define su_MEM_TREALLOC_LOCOR(T,OVP,NO,ORARGS) \
      su_MEM_TREALLOC_LOC(T, OVP, NO, ORARGS)
# define su_MEM_TALLOCF_LOCOR(T,NO,F,ORARGS) \
   su_MEM_TALLOCF_LOC(T, NO, F, ORARGS)
# define su_MEM_TCALLOCF_LOCOR(T,NO,F,ORARGS) \
   su_MEM_TCALLOCF_LOC(T, NO, F, ORARGS)
# define su_MEM_TREALLOCF_LOCOR(T,OVP,NO,F,ORARGS) \
      su_MEM_TREALLOCF_LOC(T, OVP, NO, F, ORARGS)
# define su_MEM_FREE_LOCOR(OVP,ORARGS) su_MEM_FREE_LOC(OVP, ORARGS)
#else
# define su_MEM_ALLOC_LOCOR(SZ,ORARGS) su_MEM_ALLOC(SZ)
# define su_MEM_ALLOC_N_LOCOR(SZ,NO,ORARGS) su_MEM_ALLOC_N(SZ, NO)
# define su_MEM_CALLOC_LOCOR(SZ,ORARGS) su_MEM_CALLOC(SZ)
# define su_MEM_CALLOC_N_LOCOR(SZ,NO,ORARGS) su_MEM_CALLOC_N(SZ, NO)
# define su_MEM_REALLOC_LOCOR(OVP,SZ,ORARGS) su_MEM_REALLOC(OVP, SZ)
# define su_MEM_REALLOC_N_LOCOR(OVP,SZ,NO,ORARGS) su_MEM_REALLOC_N(OVP, SZ, NO)
# define su_MEM_TALLOC_LOCOR(T,NO,ORARGS) su_MEM_TALLOC(T, NO)
# define su_MEM_TCALLOC_LOCOR(T,NO,ORARGS) su_MEM_TCALLOC(T, NO)
# define su_MEM_TREALLOC_LOCOR(T,OVP,NO) su_MEM_TREALLOC(T, OVP, NO)
# define su_MEM_TALLOCF_LOCOR(T,NO,F,ORARGS) su_MEM_TALLOCF(T, NO, F)
# define su_MEM_TCALLOCF_LOCOR(T,NO,F,ORARGS) su_MEM_TCALLOCF(T, NO, F)
# define su_MEM_TREALLOCF_LOCOR(T,OVP,F,NO) su_MEM_TREALLOCF(T, OVP, NO, F)
# define su_MEM_FREE_LOCOR(OVP,ORARGS) su_MEM_FREE_LOC(OVP, ORARGS)
#endif /* !su_HAVE_DBG_LOC_ARGS */
INLINE boole su_mem_get_can_book(uz size, uz no, uz notoadd){
   if(UZ_MAX - no <= notoadd)
      return FAL0;
   no += notoadd;
   if(no == 0)
      return TRU1;
   no = UZ_MAX / no;
   if(no < size || (size != 1 && no == size))
      return FAL0;
#ifdef su_MEM_ALLOC_DEBUG
   if(!su__mem_get_can_book(size, no))
      return FAL0;
#endif
   return TRU1;
}
#define su_mem_get_usable_size(SZ) su_Z_ALIGN(SZ) /* XXX fake */
#define su_mem_get_usable_size_32(SZ) su_S(su_u32,su_Z_ALIGN(SZ)) /*XXX*/
/* XXX get_usable_size_ptr(), get_memory_usage()  */
EXPORT void su_mem_set_conf(u32 mco, uz val);
INLINE boole su_mem_check(void){
#ifdef su_MEM_ALLOC_DEBUG
   return su__mem_check(su_DBG_LOC_ARGS_INJ_SOLE);
#else
   return FAL0;
#endif
}
/* XXX mem_check_ptr()  */
/* XXX mem_[usage_]info(); opt: output channel, thread ptr */
INLINE boole su_mem_trace(void){ /* XXX ochannel, thrptr*/
#ifdef su_MEM_ALLOC_DEBUG
   return su__mem_trace(su_DBG_LOC_ARGS_INJ_SOLE);
#else
   return FAL0;
#endif
}
C_DECL_END
#include <su/code-ou.h>
#endif /* !su_MEM_H */
/* s-it-mode */
