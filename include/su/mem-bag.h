/*@ Mem bag objects to throw in and possibly forget about allocations.
 *@ Depends on su_HAVE_MEM_BAG_{AUTO,LOFI}. TODO FLUX
 *@ The allocation interface is macro-based for the sake of debugging.
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_MEM_BAG_H
#define su_MEM_BAG_H
#include <su/code.h>
#if defined su_HAVE_MEM_BAG_AUTO || defined su_HAVE_MEM_BAG_LOFI
# define su_HAVE_MEM_BAG
#endif
#ifdef su_HAVE_MEM_BAG
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
struct su_mem_bag;
#ifdef su_USECASE_MX
# define su_MEM_BAG_SELF (n_go_data->gdc_membag)
#endif
/* Equality CTAsserted */
enum su_mem_bag_alloc_flags{
   su_MEM_BAG_ALLOC_NONE,
   su_MEM_BAG_ALLOC_CLEAR = 1u<<1,
   su_MEM_BAG_ALLOC_OVERFLOW_OK = su_STATE_ERR_OVERFLOW,
   su_MEM_BAG_ALLOC_NOMEM_OK = su_STATE_ERR_NOMEM,
   su_MEM_BAG_ALLOC_MAYFAIL = su_STATE_ERR_PASS,
   su_MEM_BAG_ALLOC_MUSTFAIL = su_STATE_ERR_NOPASS,
   su__MEM_BAG_ALLOC_USER_MASK = 0xFF | su_STATE_ERR_MASK
};
struct su_mem_bag{
   struct su_mem_bag *mb_top; /* Stacktop (outermost object only) */
   struct su_mem_bag *mb_outer; /* Outer object in stack */
   struct su_mem_bag *mb_outer_save;
   u32 mb_bsz; /* Pool size available to users.. */
   u32 mb_bsz_wo_gap; /* ..less some GAP */
#ifdef su_HAVE_MEM_BAG_AUTO
   sz mb_auto_relax_recur;
   struct su__mem_bag_auto_buf *mb_auto_top;
   struct su__mem_bag_auto_buf *mb_auto_full;
   struct su__mem_bag_auto_huge *mb_auto_huge;
#endif
#ifdef su_HAVE_MEM_BAG_LOFI
   struct su__mem_bag_lofi_pool *mb_lofi_pool;
   struct su__mem_bag_lofi_chunk *mb_lofi_top;
#endif
};
EXPORT struct su_mem_bag *su_mem_bag_create(struct su_mem_bag *self, uz bsz);
EXPORT void su_mem_bag_gut(struct su_mem_bag *self);
EXPORT struct su_mem_bag *su_mem_bag_fixate(struct su_mem_bag *self);
EXPORT struct su_mem_bag *su_mem_bag_reset(struct su_mem_bag *self);
EXPORT struct su_mem_bag *su_mem_bag_push(struct su_mem_bag *self,
      struct su_mem_bag *that_one);
EXPORT struct su_mem_bag *su_mem_bag_pop(struct su_mem_bag *self,
      struct su_mem_bag *that_one);
INLINE struct su_mem_bag *
su_mem_bag_top(struct su_mem_bag *self){
   ASSERT_RET(self != NIL, NIL);
   ASSERT_RET(self->mb_outer == NIL, su_mem_bag_top(self->mb_outer));
   return (self->mb_top != NIL) ? self->mb_top : self;
}
/*
 * Allocation interface: auto
 */
#ifdef su_HAVE_MEM_BAG_AUTO
EXPORT struct su_mem_bag *su_mem_bag_auto_relax_create(
      struct su_mem_bag *self);
EXPORT struct su_mem_bag *su_mem_bag_auto_relax_gut(struct su_mem_bag *self);
EXPORT struct su_mem_bag *su_mem_bag_auto_relax_unroll(
      struct su_mem_bag *self);
EXPORT void *su_mem_bag_auto_allocate(struct su_mem_bag *self, uz size, uz no,
      u32 mbaf  su_DBG_LOC_ARGS_DECL);
# define su_MEM_BAG_AUTO_ALLOCATE(BAGP,SZ,NO,F) \
      su_mem_bag_auto_allocate(BAGP, SZ, NO, F  su_DBG_LOC_ARGS_INJ)
# ifdef su_HAVE_DBG_LOC_ARGS
#  define su_MEM_BAG_AUTO_ALLOCATE_LOC(BAGP,SZ,NO,F,FNAME,LNNO) \
      su_mem_bag_auto_allocate(BAGP, SZ, NO, F, FNAME, LNNO)
# else
#  define su_MEM_BAG_AUTO_ALLOCATE_LOC(BAGP,SZ,NO,F,FNAME,LNNO) \
      su_mem_bag_auto_allocate(BAGP, SZ, NO, F)
# endif
/* The "normal" interface, slim, but su_USECASE_ specific: use _ALLOCATE_ for
 * other use cases.  These set MUSTFAIL and always return a valid pointer. */
# ifdef su_MEM_BAG_SELF
#  define su_MEM_BAG_SELF_AUTO_ALLOC(SZ) \
      su_MEM_BAG_AUTO_ALLOCATE(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_AUTO_ALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_BAG_AUTO_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_AUTO_ALLOC_N(SZ,NO) \
      su_MEM_BAG_AUTO_ALLOCATE(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_AUTO_ALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_BAG_AUTO_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_AUTO_CALLOC(SZ) \
      su_MEM_BAG_AUTO_ALLOCATE(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_AUTO_CALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_BAG_AUTO_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_AUTO_CALLOC_N(SZ,NO) \
      su_MEM_BAG_AUTO_ALLOCATE(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_AUTO_CALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_BAG_AUTO_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_AUTO_TALLOC(T,NO) \
      su_S(T *,su_MEM_BAG_SELF_AUTO_ALLOC_N(sizeof(T), su_S(su_uz,NO)))
#  define su_MEM_BAG_SELF_AUTO_TALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_BAG_SELF_AUTO_ALLOC_N_LOC(sizeof(T), su_S(su_uz,NO),\
         FNAME, LNNO))
#  define su_MEM_BAG_SELF_AUTO_TCALLOC(T,NO) \
      su_S(T *,su_MEM_BAG_SELF_AUTO_CALLOC_N(sizeof(T), su_S(su_uz,NO))
#  define su_MEM_BAG_SELF_AUTO_TCALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_BAG_SELF_AUTO_CALLOC_N_LOC(sizeof(T), su_S(su_uz,NO),\
         FNAME, LNNO))
   /* (The painful _LOCOR series) */
#  ifdef su_HAVE_DBG_LOC_ARGS
#   define su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_AUTO_ALLOC_LOC(SZ, ORARGS)
#   define su_MEM_BAG_SELF_AUTO_ALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_ALLOC_N_LOC(SZ, NO, ORARGS)
#   define su_MEM_BAG_SELF_AUTO_CALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_AUTO_CALLOC_LOC(SZ, ORGARGS)
#   define su_MEM_BAG_SELF_AUTO_CALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_CALLOC_N_LOC(SZ, NO, ORARGS)
#   define su_MEM_BAG_SELF_AUTO_TALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_TALLOC_LOC(T, NO, ORARGS)
#   define su_MEM_BAG_SELF_AUTO_TCALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_TCALLOC_LOC(T, NO, ORARGS)
#  else
#   define su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_AUTO_ALLOC(SZ)
#   define su_MEM_BAG_SELF_AUTO_ALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_ALLOC_N(SZ, NO)
#   define su_MEM_BAG_SELF_AUTO_CALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_AUTO_CALLOC(SZ)
#   define su_MEM_BAG_SELF_AUTO_CALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_CALLOC_N(SZ, NO)
#   define su_MEM_BAG_SELF_AUTO_TALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_TALLOC(T, NO)
#   define su_MEM_BAG_SELF_AUTO_TCALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_AUTO_TCALLOC(T, NO)
#  endif /* !su_HAVE_DBG_LOC_ARGS */
# endif /* su_MEM_BAG_SELF */
#endif /* su_HAVE_MEM_BAG_AUTO */
/*
 * Allocation interface: lofi
 */
#ifdef su_HAVE_MEM_BAG_LOFI
EXPORT void *su_mem_bag_lofi_snap_create(struct su_mem_bag *self);
EXPORT struct su_mem_bag *su_mem_bag_lofi_snap_unroll(struct su_mem_bag *self,
      void *cookie);
EXPORT void *su_mem_bag_lofi_allocate(struct su_mem_bag *self, uz size, uz no,
      u32 mbaf  su_DBG_LOC_ARGS_DECL);
EXPORT struct su_mem_bag *su_mem_bag_lofi_free(struct su_mem_bag *self,
      void *ovp su_DBG_LOC_ARGS_DECL);
# define su_MEM_BAG_LOFI_ALLOCATE(BAGP,SZ,NO,F) \
      su_mem_bag_lofi_allocate(BAGP, SZ, NO, F  su_DBG_LOC_ARGS_INJ)
# ifdef su_HAVE_DBG_LOC_ARGS
#  define su_MEM_BAG_LOFI_ALLOCATE_LOC(BAGP,SZ,NO,F,FNAME,LNNO) \
      su_mem_bag_lofi_allocate(BAGP, SZ, NO, F, FNAME, LNNO)
# else
#  define su_MEM_BAG_LOFI_ALLOCATE_LOC(BAGP,SZ,NO,F,FNAME,LNNO) \
      su_mem_bag_lofi_allocate(BAGP, SZ, NO, F)
# endif
# define su_MEM_BAG_LOFI_FREE(BAGP,OVP) \
   su_mem_bag_lofi_free(BAGP, OVP  su_DBG_LOC_ARGS_INJ)
# ifdef su_HAVE_DBG_LOC_ARGS
#  define su_MEM_BAG_LOFI_FREE_LOC(BAGP,OVP,FNAME,LNNO) \
      su_mem_bag_lofi_free(BAGP, OVP, FNAME, LNNO)
# else
#  define su_MEM_BAG_LOFI_FREE_LOC(BAGP,OVP,FNAME,LNNO) \
      su_mem_bag_lofi_free(BAGP, OVP)
# endif
/* The "normal" interface, slim, but su_USECASE_ specific: use _ALLOCATE_ for
 * other use cases.  These set MUSTFAIL and always return a valid pointer. */
# ifdef su_MEM_BAG_SELF
#  define su_MEM_BAG_SELF_LOFI_ALLOC(SZ) \
      su_MEM_BAG_LOFI_ALLOCATE(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_LOFI_ALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_BAG_LOFI_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_LOFI_ALLOC_N(SZ,NO) \
      su_MEM_BAG_LOFI_ALLOCATE(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_LOFI_ALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_BAG_LOFI_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_LOFI_CALLOC(SZ) \
      su_MEM_BAG_LOFI_ALLOCATE(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_LOFI_CALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_BAG_LOFI_SELF_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_LOFI_CALLOC_N(SZ,NO) \
      su_MEM_BAG_LOFI_ALLOCATE(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_LOFI_CALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_BAG_LOFI_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_CLEAR | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)
#  define su_MEM_BAG_SELF_LOFI_TALLOC(T,NO) \
      su_S(T *,su_MEM_BAG_SELF_LOFI_ALLOC_N(sizeof(T), su_S(su_uz,NO)))
#  define su_MEM_BAG_SELF_LOFI_TALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_BAG_SELF_LOFI_ALLOC_N_LOC(sizeof(T), su_S(su_uz,NO),\
         FNAME, LNNO))
#  define su_MEM_BAG_SELF_LOFI_TCALLOC(T,NO) \
      su_S(T *,su_MEM_BAG_SELF_LOFI_CALLOC_N(sizeof(T), su_S(su_uz,NO))
#  define su_MEM_BAG_SELF_LOFI_TCALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_BAG_SELF_LOFI_CALLOC_N_LOC(sizeof(T), su_S(su_uz,NO),\
         FNAME, LNNO))
#  define su_MEM_BAG_SELF_LOFI_FREE(OVP) \
      su_MEM_BAG_LOFI_FREE(su_MEM_BAG_SELF, OVP)
#  define su_MEM_BAG_SELF_LOFI_FREE_LOC(OVP,FNAME,LNNO) \
      su_MEM_BAG_LOFI_FREE_LOC(su_MEM_BAG_SELF, OVP, FNAME, LNNO)
   /* (The painful _LOCOR series) */
#  ifdef su_HAVE_DBG_LOC_ARGS
#   define su_MEM_BAG_SELF_LOFI_ALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_LOFI_ALLOC_LOC(SZ, ORARGS)
#   define su_MEM_BAG_SELF_LOFI_ALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_ALLOC_N_LOC(SZ, NO, ORARGS)
#   define su_MEM_BAG_SELF_LOFI_CALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_LOFI_CALLOC_LOC(SZ, ORGARGS)
#   define su_MEM_BAG_SELF_LOFI_CALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_CALLOC_N_LOC(SZ, NO, ORARGS)
#   define su_MEM_BAG_SELF_LOFI_TALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_TALLOC_LOC(T, NO, ORARGS)
#   define su_MEM_BAG_SELF_LOFI_TCALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_TCALLOC_LOC(T, NO, ORARGS)
#   define su_MEM_BAG_SELF_LOFI_FREE_LOCOR(OVP,ORARGS) \
      su_MEM_BAG_SELF_LOFI_FREE_LOC(OVP, ORARGS)
#  else
#   define su_MEM_BAG_SELF_LOFI_ALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_LOFI_ALLOC(SZ)
#   define su_MEM_BAG_SELF_LOFI_ALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_ALLOC_N(SZ, NO)
#   define su_MEM_BAG_SELF_LOFI_CALLOC_LOCOR(SZ,ORARGS) \
      su_MEM_BAG_SELF_LOFI_CALLOC(SZ)
#   define su_MEM_BAG_SELF_LOFI_CALLOC_N_LOCOR(SZ,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_CALLOC_N(SZ, NO)
#   define su_MEM_BAG_SELF_LOFI_TALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_TALLOC(T, NO)
#   define su_MEM_BAG_SELF_LOFI_TCALLOC_LOCOR(T,NO,ORARGS) \
      su_MEM_BAG_SELF_LOFI_TCALLOC(T, NO)
#   define su_MEM_BAG_SELF_LOFI_FREE_LOCOR(OVP,ORARGS) \
      su_MEM_BAG_SELF_LOFI_FREE_LOC(OVP, ORARGS)
#  endif /* !su_HAVE_DBG_LOC_ARGS */
# endif /* su_MEM_BAG_SELF */
#endif /* su_HAVE_MEM_BAG_LOFI */
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)
class mem_bag;
class EXPORT mem_bag : private su_mem_bag{
   su_CLASS_NO_COPY(mem_bag)
public:
   enum alloc_flags{
      alloc_none = su_MEM_BAG_ALLOC_NONE,
      alloc_clear = su_MEM_BAG_ALLOC_CLEAR,
      alloc_overflow_ok = su_MEM_BAG_ALLOC_OVERFLOW_OK,
      alloc_nomem_ok = su_MEM_BAG_ALLOC_NOMEM_OK,
      alloc_mayfail = su_MEM_BAG_ALLOC_MAYFAIL,
      alloc_mustfail = su_MEM_BAG_ALLOC_MUSTFAIL
   };
   mem_bag(uz bsz=0) {su_mem_bag_create(this, bsz);}
   ~mem_bag(void) {su_mem_bag_gut(this);}
   mem_bag &fixate(void) {SELFTHIS_RET(su_mem_bag_fixate(this));}
   mem_bag &reset(void) {SELFTHIS_RET(su_mem_bag_reset(this));}
   mem_bag &push(mem_bag &that_one){
      SELFTHIS_RET(su_mem_bag_push(this, &that_one));
   }
   mem_bag &pop(mem_bag &that_one){
      SELFTHIS_RET(su_mem_bag_pop(this, &that_one));
   }
   mem_bag &top(void){
      return (mb_top != NIL) ? *S(mem_bag*,mb_top) : *this;
   }
#ifdef su_HAVE_MEM_BAG_AUTO
   mem_bag &auto_relax_create(void){
      SELFTHIS_RET(su_mem_bag_auto_relax_create(this));
   }
   mem_bag &auto_relax_gut(void){
      SELFTHIS_RET(su_mem_bag_auto_relax_gut(this));
   }
   mem_bag &auto_relax_unroll(void){
      SELFTHIS_RET(su_mem_bag_auto_relax_unroll(this));
   }
   void *auto_allocate(uz size, uz no=1, u32 af=alloc_none){
      return su_mem_bag_auto_allocate(this, size, no, af  su_DBG_LOC_ARGS_INJ);
   }
#endif /* su_HAVE_MEM_BAG_AUTO */
#ifdef su_HAVE_MEM_BAG_LOFI
   void *lofi_snap_create(void) {return su_mem_bag_lofi_snap_create(this);}
   mem_bag &lofi_snap_unroll(void *cookie){
      SELFTHIS_RET(su_mem_bag_lofi_snap_unroll(this, cookie));
   }
   void *lofi_allocate(uz size, uz no=1, u32 af=alloc_none){
      return su_mem_bag_lofi_allocate(this, size, no, af  su_DBG_LOC_ARGS_INJ);
   }
   mem_bag &lofi_free(void *ovp){
      SELFTHIS_RET(su_mem_bag_lofi_free(this, ovp  su_DBG_LOC_ARGS_INJ));
   }
#endif /* su_HAVE_MEM_BAG_LOFI */
};
NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_HAVE_MEM_BAG */
#endif /* !su_MEM_BAG_H */
/* s-it-mode */
