/*@ Mem bag objects to throw in and possibly forget about allocations.
 *@ Depends on su_HAVE_MEM_BAG_{AUTO,LOFI}. TODO FLUX
 *@ The allocation interface is macro-based for the sake of debugging.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

/*!
 * \file
 * \ingroup MEM
 * \brief \r{MEM_BAG}: objects to pool allocations
 */

#include <su/code.h>

#if defined su_HAVE_MEM_BAG_AUTO || defined su_HAVE_MEM_BAG_LOFI
   /*! \ingroup MEM_BAG
    * Defined if memory bags are available.
    * They are if the \r{CONFIG} enables one of the memory bag allocation types
    * \r{su_HAVE_MEM_BAG_AUTO} and/or \r{su_HAVE_MEM_BAG_LOFI}. */
# define su_HAVE_MEM_BAG
#endif
#ifdef su_HAVE_MEM_BAG

/* su_USECASE related foreign hooks XXX should not be here! config.h? */
#ifdef su_USECASE_MX
# include <su/code-in.h>
C_DECL_BEGIN

struct mx_go_data_ctx;
struct su_mem_bag;
struct su__mem_bag_mx {struct su_mem_bag *mbm_bag;};
EXPORT_DATA struct mx_go_data_ctx *mx_go_data;
# define su_MEM_BAG_SELF (su_R(struct su__mem_bag_mx*,mx_go_data)->mbm_bag)

C_DECL_END
# include <su/code-ou.h>
#endif /* su_USECASE_MX */

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_mem_bag;

/* {{{ */
/*!
 * \defgroup MEM_BAG Memory bags
 * \ingroup MEM
 * \brief \r{MEM} objects to pool allocations (\r{su/mem-bag.h})
 *
 * Memory bags introduce possibilities to bundle heap memory with a job,
 * and to throw away the bag(s) as such after the job is done.
 * They are a more generalized successor of the string allocator that
 * Kurt Shoens developed for BSD Mail in the 70s.
 * As such, neither constructors nor destructors are supported.
 *
 * Dependent upon compile-time options bags offer the auto-reclaimed string
 * memory allocator (with \r{su_HAVE_MEM_BAG_AUTO}),
 * and/or a replacement for \c{alloca(3)} (\r{su_HAVE_MEM_BAG_LOFI}: a stack of
 * last-out, first-in memory storage).
 * In general the memory served is \r{su_Z_ALIGN()} aligned.
 * Allocation requests larger than about \r{su_S32_MAX} result in
 * \r{su_STATE_ERR_OVERFLOW} errors.
 *
 * The bag(s) form(s) a stack which can be \r{su_mem_bag_push()}ed and
 * \r{su_mem_bag_pop()}ped.
 * One only works with the outermost object, it will internally choose the
 * \r{su_mem_bag_top()} automatically.
 *
 * By defining \c{su_MEM_BAG_SELF} a more convenient preprocessor based
 * interface is available, just as for the \r{MEM_CACHE_ALLOC} interface.
 * This is not furtherly documented, though.
 *
 * If any of \r{su_HAVE_DEBUG} or \r{su_HAVE_MEM_CANARIES_DISABLE} is defined
 * then these objects only manage the chunks, the user chunk memory itself
 * will be served by the normal \r{MEM_CACHE_ALLOC} allocator instead, to
 * allow neatless integration within address sanitizers etc.
 * @{
 */

/*! Mirrors a subset of the \r{su_mem_alloc_flags}. *//* Equality CTAsserted */
enum su_mem_bag_alloc_flags{
   su_MEM_BAG_ALLOC_NONE, /*!< \_ */
   su_MEM_BAG_ALLOC_ZERO = 1u<<1, /*!< Zero memory. */

   /*! An alias (i.e., same value) for \r{su_STATE_ERR_OVERFLOW}. */
   su_MEM_BAG_ALLOC_OVERFLOW_OK = su_STATE_ERR_OVERFLOW,
   /*! An alias (i.e., same value) for \r{su_STATE_ERR_NOMEM}. */
   su_MEM_BAG_ALLOC_NOMEM_OK = su_STATE_ERR_NOMEM,
   /*! An alias (i.e., same value) for \r{su_STATE_ERR_PASS}. */
   su_MEM_BAG_ALLOC_MAYFAIL = su_STATE_ERR_PASS,
   /*! An alias (i.e., same value) for \r{su_STATE_ERR_NOPASS}. */
   su_MEM_BAG_ALLOC_MUSTFAIL = su_STATE_ERR_NOPASS,
   su__MEM_BAG_ALLOC_USER_MASK = 0xFF | su_STATE_ERR_MASK
};

/*! \_ */
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

/*! \a{bsz} is a buffer size hint used to space memory chunk pool buffers,
 * which thus also defines the maximum size of chunks which are served (less
 * some internal management overhead).
 * If \a{bsz} is 0 then two pages (\r{su_PAGE_SIZE}) are used (to accommodate
 * the use case of page allocations), otherwise it is cramped to some internal
 * limits (currently 1 KB / 10 MB).
 *
 * Memory bags can serve chunks larger than what pools can handle, but these
 * need special treatment, and thus counteract the idea of a pool: their
 * occurrence is logged with \r{su_HAVE_DEBUG}. */
EXPORT struct su_mem_bag *su_mem_bag_create(struct su_mem_bag *self, uz bsz);

/*! If \SELF owns a stack of bags as created via \r{su_mem_bag_push()}, all
 * entries of the stack will be forcefully popped and destructed;
 * presence of a stack is a \r{su_HAVE_DEBUG} log condition. */
EXPORT void su_mem_bag_gut(struct su_mem_bag *self);

/*! Fixate the current snapshot of auto-reclaimed and flux storage of \SELF:
 * earlier allocations will be persistent, only later ones will be covered by
 * \r{su_mem_bag_reset()}.
 * \remarks{Only to be called once per object.}
 * \remarks{Only applies to \SELF or its current \r{su_mem_bag_top()}, if there
 * is one, does not propagate through the stack.} */
EXPORT struct su_mem_bag *su_mem_bag_fixate(struct su_mem_bag *self);

/*! To be called from the (main)loops upon tick and break-off time to perform
 * debug checking and memory cleanup.
 * If \SELF owns a stack of \r{su_mem_bag_push()}ed objects, these will be
 * forcefully destructed.
 * The cleanup will release all LOFI memory, drop all the relaxation created by
 * \r{su_mem_bag_auto_relax_create()} and all auto-reclaimed and flux storage
 * that is not covered by r{su_mem_bag_fixate()}.
 * \remarks{Possible \r{su_HAVE_DEBUG} logs via \r{su_LOG_DEBUG}.} */
EXPORT struct su_mem_bag *su_mem_bag_reset(struct su_mem_bag *self);

/*! Push the initialized bag \a{that_one} onto the bag stack layer of \SELF.
 * \a{that_one} will be used to serve memory until \r{su_mem_bag_pop()} is
 * called, which implicitly happens during \r{su_mem_bag_reset()}.
 * It is possible to push and thus pop a bag twice: this is sometimes
 * handy to store memory persistantly in some outer stack level. */
EXPORT struct su_mem_bag *su_mem_bag_push(struct su_mem_bag *self,
      struct su_mem_bag *that_one);

/*! Pop the \r{su_mem_bag_push()}ed bag \a{that_one} off the stack.
 * If \a{that_one} is not top of the stack, all bags from top down to
 * \a{that_one} will be popped in one go.
 * Popping a stack does not reset its allocations. */
EXPORT struct su_mem_bag *su_mem_bag_pop(struct su_mem_bag *self,
      struct su_mem_bag *that_one);

/*! Get the bag that currently serves on the stack top.
 * Returns \SELF if there is no stack. */
INLINE struct su_mem_bag *su_mem_bag_top(struct su_mem_bag *self){
   ASSERT_RET(self != NIL, NIL);
#if ASSERT_INJOR(1, 0)
   if(self->mb_outer != NIL){
      ASSERT_EXEC(self->mb_outer == NIL, (void)0);
      do
         self = self->mb_outer;
      while(self->mb_outer != NIL);
   }
#endif
   return (self->mb_top != NIL) ? self->mb_top : self;
}

/*
 * Allocation interface: auto {{{
 */

#ifdef su_HAVE_MEM_BAG_AUTO
/*! Lower memory pressure on auto-reclaimed storage for code which has
 * a sinus-curve looking style of memory usage, i.e., peek followed by
 * release, like, e.g., doing a task on all messages of a mailbox in order.
 * Such code should call \c{relax_create()}, successively call
 * \c{relax_unroll()} after a single job has been handled, concluded with
 * a final \c{relax_gut()}.
 * \remarks{Only applies to \SELF or its current \r{su_mem_bag_top()}, if there
 * is one, does not propagate through the stack.}
 * \remarks{Can be called multiple times: \r{su_mem_bag_auto_relax_unroll()} as
 * well as \r{su_mem_bag_auto_relax_gut()} only perform real actions when
 * called on the level which called this the first time.} */
EXPORT struct su_mem_bag *su_mem_bag_auto_relax_create(
      struct su_mem_bag *self);

/*! See \r{su_mem_bag_auto_relax_create()}. */
EXPORT struct su_mem_bag *su_mem_bag_auto_relax_gut(struct su_mem_bag *self);

/*! See \r{su_mem_bag_auto_relax_create()}. */
EXPORT struct su_mem_bag *su_mem_bag_auto_relax_unroll(
      struct su_mem_bag *self);

/*! This is rather internal, but due to the \r{su_mem_bag_alloc_flags}
 * \a{mbaf} maybe handy sometimes.
 * Normally to be used through the macro interface.
 * Attempts to allocate \r{su_S32_MAX} or more bytes result in overflow errors,
 * see \r{su_MEM_BAG_ALLOC_OVERFLOW_OK} and \r{su_MEM_BAG_ALLOC_NOMEM_OK}. */
EXPORT void *su_mem_bag_auto_allocate(struct su_mem_bag *self, uz size, uz no,
      BITENUM_IS(u32,su_mem_bag_alloc_flags) mbaf  su_DBG_LOC_ARGS_DECL);

/*! \_ */
# define su_MEM_BAG_AUTO_ALLOCATE(BAGP,SZ,NO,F) \
      su_mem_bag_auto_allocate(BAGP, SZ, NO, F  su_DBG_LOC_ARGS_INJ)
# ifdef su_HAVE_DBG_LOC_ARGS
#  define su_MEM_BAG_AUTO_ALLOCATE_LOC(BAGP,SZ,NO,F,FNAME,LNNO) \
      su_mem_bag_auto_allocate(BAGP, SZ, NO, F, FNAME, LNNO)
# else
   /*! \_ */
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
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_AUTO_CALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_BAG_AUTO_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)

#  define su_MEM_BAG_SELF_AUTO_CALLOC_N(SZ,NO) \
      su_MEM_BAG_AUTO_ALLOCATE(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_AUTO_CALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_BAG_AUTO_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)

#  define su_MEM_BAG_SELF_AUTO_TALLOC(T,NO) \
      su_S(T *,su_MEM_BAG_SELF_AUTO_ALLOC_N(sizeof(T), su_S(su_uz,NO)))
#  define su_MEM_BAG_SELF_AUTO_TALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_BAG_SELF_AUTO_ALLOC_N_LOC(sizeof(T), su_S(su_uz,NO),\
         FNAME, LNNO))

#  define su_MEM_BAG_SELF_AUTO_TCALLOC(T,NO) \
      su_S(T *,su_MEM_BAG_SELF_AUTO_CALLOC_N(sizeof(T), su_S(su_uz,NO)))
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
/* }}} */

/*
 * Allocation interface: lofi {{{
 */

#ifdef su_HAVE_MEM_BAG_LOFI
/*! The snapshot can be used in a local context: if taken, many allocations
 * can be freed in one go by calling \c{lofi_snap_unroll()}.
 * \remarks{Only applies to \SELF or its current \r{su_mem_bag_top()}, if there
 * is one, does not propagate through the stack.} */
EXPORT void *su_mem_bag_lofi_snap_create(struct su_mem_bag *self);

/*! Unroll a taken LOFI snapshot by freeing all its allocations.
 * The \a{cookie} is no longer valid after this operation.
 * This can only be called on the stack level where the snap has been taken. */
EXPORT struct su_mem_bag *su_mem_bag_lofi_snap_unroll(struct su_mem_bag *self,
      void *cookie);

/*! This is rather internal, but due to the \r{su_mem_bag_alloc_flags}
 * \a{mbaf} maybe handy sometimes.
 * Normally to be used through the macro interface.
 * Attempts to allocate \r{su_S32_MAX} or more bytes result in overflow errors,
 * see \r{su_MEM_BAG_ALLOC_OVERFLOW_OK} and \r{su_MEM_BAG_ALLOC_NOMEM_OK}. */
EXPORT void *su_mem_bag_lofi_allocate(struct su_mem_bag *self, uz size, uz no,
      BITENUM_IS(u32,su_mem_bag_alloc_flags) mbaf  su_DBG_LOC_ARGS_DECL);

/*! Free \a{ovp}; \r{su_HAVE_DEBUG} will log if it is not stack top. */
EXPORT struct su_mem_bag *su_mem_bag_lofi_free(struct su_mem_bag *self,
      void *ovp su_DBG_LOC_ARGS_DECL);

/*! \_ */
# define su_MEM_BAG_LOFI_ALLOCATE(BAGP,SZ,NO,F) \
      su_mem_bag_lofi_allocate(BAGP, SZ, NO, F  su_DBG_LOC_ARGS_INJ)
# ifdef su_HAVE_DBG_LOC_ARGS
#  define su_MEM_BAG_LOFI_ALLOCATE_LOC(BAGP,SZ,NO,F,FNAME,LNNO) \
      su_mem_bag_lofi_allocate(BAGP, SZ, NO, F, FNAME, LNNO)
# else
   /*! \_ */
#  define su_MEM_BAG_LOFI_ALLOCATE_LOC(BAGP,SZ,NO,F,FNAME,LNNO) \
      su_mem_bag_lofi_allocate(BAGP, SZ, NO, F)
# endif

/*! \_ */
# define su_MEM_BAG_LOFI_FREE(BAGP,OVP) \
   su_mem_bag_lofi_free(BAGP, OVP  su_DBG_LOC_ARGS_INJ)
# ifdef su_HAVE_DBG_LOC_ARGS
#  define su_MEM_BAG_LOFI_FREE_LOC(BAGP,OVP,FNAME,LNNO) \
      su_mem_bag_lofi_free(BAGP, OVP, FNAME, LNNO)
# else
   /*! \_ */
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
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_LOFI_CALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_BAG_LOFI_SELF_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, 1,\
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)

#  define su_MEM_BAG_SELF_LOFI_CALLOC_N(SZ,NO) \
      su_MEM_BAG_LOFI_ALLOCATE(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL)
#  define su_MEM_BAG_SELF_LOFI_CALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_BAG_LOFI_ALLOCATE_LOC(su_MEM_BAG_SELF, SZ, NO,\
         su_MEM_BAG_ALLOC_ZERO | su_MEM_BAG_ALLOC_MUSTFAIL, FNAME, LNNO)

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
/* }}}*/

/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class mem_bag;

/* mem_bag {{{ */
/*!
 * \ingroup MEM_BAG
 * C++ variant of \r{MEM_BAG} (\r{su/mem-bag.h})
 */
class EXPORT mem_bag : private su_mem_bag{
   su_CLASS_NO_COPY(mem_bag);
public:
   /*! \copydoc{su_mem_bag_alloc_flags} */
   enum alloc_flags{
      /*! \copydoc{su_MEM_BAG_ALLOC_NONE} */
      alloc_none = su_MEM_BAG_ALLOC_NONE,
      /*! \copydoc{su_MEM_BAG_ALLOC_ZERO} */
      alloc_zero = su_MEM_BAG_ALLOC_ZERO,

      /*! \copydoc{su_MEM_BAG_ALLOC_OVERFLOW_OK} */
      alloc_overflow_ok = su_MEM_BAG_ALLOC_OVERFLOW_OK,
      /*! \copydoc{su_MEM_BAG_ALLOC_NOMEM_OK} */
      alloc_nomem_ok = su_MEM_BAG_ALLOC_NOMEM_OK,
      /*! \copydoc{su_MEM_BAG_ALLOC_MAYFAIL} */
      alloc_mayfail = su_MEM_BAG_ALLOC_MAYFAIL,
      /*! \copydoc{su_MEM_BAG_ALLOC_MUSTFAIL} */
      alloc_mustfail = su_MEM_BAG_ALLOC_MUSTFAIL
   };

   /*! \copydoc{su_mem_bag_create()} */
   mem_bag(uz bsz=0) {su_mem_bag_create(this, bsz);}

   /*! \copydoc{su_mem_bag_gut()} */
   ~mem_bag(void) {su_mem_bag_gut(this);}

   /*! \copydoc{su_mem_bag_fixate()} */
   mem_bag &fixate(void) {SELFTHIS_RET(su_mem_bag_fixate(this));}

   /*! \copydoc{su_mem_bag_reset()} */
   mem_bag &reset(void) {SELFTHIS_RET(su_mem_bag_reset(this));}

   /*! \copydoc{su_mem_bag_push()} */
   mem_bag &push(mem_bag &that_one){
      SELFTHIS_RET(su_mem_bag_push(this, &that_one));
   }

   /*! \copydoc{su_mem_bag_pop()} */
   mem_bag &pop(mem_bag &that_one){
      SELFTHIS_RET(su_mem_bag_pop(this, &that_one));
   }

   /*! \copydoc{su_mem_bag_top()} */
   mem_bag &top(void){
      return (mb_top != NIL) ? *S(mem_bag*,mb_top) : *this;
   }

#ifdef su_HAVE_MEM_BAG_AUTO
   /*! \copydoc{su_mem_bag_auto_relax_create()} */
   mem_bag &auto_relax_create(void){
      SELFTHIS_RET(su_mem_bag_auto_relax_create(this));
   }

   /*! \copydoc{su_mem_bag_auto_relax_gut()} */
   mem_bag &auto_relax_gut(void){
      SELFTHIS_RET(su_mem_bag_auto_relax_gut(this));
   }

   /*! \copydoc{su_mem_bag_auto_relax_unroll()} */
   mem_bag &auto_relax_unroll(void){
      SELFTHIS_RET(su_mem_bag_auto_relax_unroll(this));
   }

   /*! \copydoc{su_mem_bag_auto_allocate()} */
   void *auto_allocate(uz size, uz no=1,
         BITENUM_IS(u32,alloc_flags) af=alloc_none){
      return su_mem_bag_auto_allocate(this, size, no, af  su_DBG_LOC_ARGS_INJ);
   }
#endif /* su_HAVE_MEM_BAG_AUTO */

#ifdef su_HAVE_MEM_BAG_LOFI
   /*! \copydoc{su_mem_bag_lofi_snap_create()} */
   void *lofi_snap_create(void) {return su_mem_bag_lofi_snap_create(this);}

   /*! \copydoc{su_mem_bag_lofi_snap_unroll()} */
   mem_bag &lofi_snap_unroll(void *cookie){
      SELFTHIS_RET(su_mem_bag_lofi_snap_unroll(this, cookie));
   }

   /*! \copydoc{su_mem_bag_lofi_allocate()} */
   void *lofi_allocate(uz size, uz no=1,
         BITENUM_IS(u32,alloc_flags) af=alloc_none){
      return su_mem_bag_lofi_allocate(this, size, no, af  su_DBG_LOC_ARGS_INJ);
   }

   /*! \copydoc{su_mem_bag_lofi_free()} */
   mem_bag &lofi_free(void *ovp){
      SELFTHIS_RET(su_mem_bag_lofi_free(this, ovp  su_DBG_LOC_ARGS_INJ));
   }
#endif /* su_HAVE_MEM_BAG_LOFI */
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_HAVE_MEM_BAG */
#endif /* !su_MEM_BAG_H */
/* s-it-mode */
