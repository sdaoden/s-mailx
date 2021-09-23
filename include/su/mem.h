/*@ Memory: tools like copy, move etc., and a heap allocation interface.
 *
 * Copyright (c) 2001 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

/*!
 * \file
 * \ingroup MEM
 * \brief \r{MEM} tools and heap
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* mem_tools {{{ */
/*!
 * \defgroup MEM_TOOLS Memory tools
 * \ingroup MEM
 * \brief \r{MEM} tools like copy (\r{su/mem.h})
 *
 * In general argument pointers may be given as \NIL if a length argument
 * which covers them is given as 0.
 * @{
 */

/*! \_ */
EXPORT sz su_mem_cmp(void const *vpa, void const *vpb, uz len);

/*! \_ */
EXPORT void *su_mem_copy(void *vp, void const *src, uz len);

/*! \_ */
EXPORT void *su_mem_find(void const *vp, s32 what, uz len);

/*! \_ */
EXPORT void *su_mem_rfind(void const *vp, s32 what, uz len);

/*! \_ */
EXPORT void *su_mem_move(void *vp, void const *src, uz len);

/*! \_ */
EXPORT void *su_mem_set(void *vp, s32 what, uz len);

/*! Ensure \a{len} bytes of \a{vp} are zeroed.
 * Care is taken compiler or linker do not optimize this away. */
INLINE void su_mem_zero(void *vp, uz len){
   void *(*const volatile set_v)(void *, s32, uz) = &su_mem_set;
   (*set_v)(vp, 0, len);
}
/*! @} *//* }}} */

/* heap memory {{{ */
/*!
 * \defgroup MEM_CACHE_ALLOC Heap memory
 * \ingroup MEM
 * \brief Allocating heap memory (\r{su/mem.h})
 *
 * It interacts with \r{su_STATE_ERR_NOMEM} and \r{su_STATE_ERR_OVERFLOW},
 * but also allows per-call failure ignorance.
 *
 * The \r{su_MEM_ALLOC_DEBUG}-enabled cache surrounds served chunks with
 * canaries which will detect write bound violations.
 * Also, the enum \r{su_mem_alloc_flags} allows flagging allocation requests
 * with three (or four when including "0") different flag marks;
 * these will show up in error messages, like allocation check and cache dump
 * output, which are also available, then.
 * Finally served memory chunks will be initialized with a (non-0) pattern
 * when debugging is enabled.
 * @{
 */

#if (defined su_HAVE_DEBUG && !defined su_HAVE_MEM_CANARIES_DISABLE) ||\
      defined DOXYGEN
   /*! Whether the cache is debug-enabled.
    * It is if \r{su_HAVE_DEBUG} is, and \r{su_HAVE_MEM_CANARIES_DISABLE}
    * is not defined. */
# define su_MEM_ALLOC_DEBUG
#endif

/*! \_ */
enum su_mem_alloc_flags{
   su_MEM_ALLOC_NONE, /*!< \_ */
   /*! Zero memory.
    * \remarks{TODO: until the C++ memory cache is ported this flag will not be
    * honoured by reallocation requests.} */
   su_MEM_ALLOC_ZERO = 1u<<1,
   /*! Try to mark memory so it is not included in core dumps, is not
    * inherited by child processes upon \c{fork(2)} time, and is zeroed when
    * the pointer is \r{su_mem_free()}d.
    * But for the last all mentioned attributes require operating system
    * support and therefore may or may not be backed by functionality.
    * \remarks{TODO: until the C++ memory cache is ported this flag will not
    * do anything.} */
   su_MEM_ALLOC_CONCEAL = 1u<<2,

   /*! Perform overflow checks against 32-bit, not \r{su_UZ_MAX}. */
   su_MEM_ALLOC_32BIT_OVERFLOW = 1u<<3,
   /*! Perform overflow checks against 31-bit, not \r{su_UZ_MAX}. */
   su_MEM_ALLOC_31BIT_OVERFLOW = 1u<<4,

   /*! An alias (i.e., same value) for \r{su_STATE_ERR_OVERFLOW}. */
   su_MEM_ALLOC_OVERFLOW_OK = su_STATE_ERR_OVERFLOW,
   /*! An alias (i.e., same value) for \r{su_STATE_ERR_NOMEM}. */
   su_MEM_ALLOC_NOMEM_OK = su_STATE_ERR_NOMEM,
   /*! An alias (i.e., same value) for \r{su_STATE_ERR_PASS}. */
   su_MEM_ALLOC_MAYFAIL = su_STATE_ERR_PASS,
   /*! An alias (i.e., same value) for \r{su_STATE_ERR_NOPASS}. */
   su_MEM_ALLOC_MUSTFAIL = su_STATE_ERR_NOPASS,

   su__MEM_ALLOC_MARK_SHIFT = 16u,
   /*! Debug (log etc.) flag mark "no mark", */
   su_MEM_ALLOC_MARK_0 = 0u<<su__MEM_ALLOC_MARK_SHIFT,
   /*! ..mark 1, */
   su_MEM_ALLOC_MARK_1 = 1u<<su__MEM_ALLOC_MARK_SHIFT,
   /*! ..mark 2, and */
   su_MEM_ALLOC_MARK_2 = 2u<<su__MEM_ALLOC_MARK_SHIFT,
   /*! ..mark 3. */
   su_MEM_ALLOC_MARK_3 = 3u<<su__MEM_ALLOC_MARK_SHIFT,

   /*! The \r{MEM_BAG} uses marks: auto-reclaimed storage (mark 1). */
   su_MEM_ALLOC_MARK_AUTO = su_MEM_ALLOC_MARK_1,
   /*! The \r{MEM_BAG} uses marks: huge auto-reclaimed storage (mark 2). */
   su_MEM_ALLOC_MARK_AUTO_HUGE = su_MEM_ALLOC_MARK_2,
   /*! The \r{MEM_BAG} uses marks: LOFI storage (mark 3). */
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
   /*! Minimum size served for an allocation (\r{su_mem_get_usable_size()}).
    * If one of \r{su_MEM_ALLOC_DEBUG} and \r{su_HAVE_MEM_CANARIES_DISABLE} is
    * defined then this value is not inspected (in order to exactly detect user
    * bound violations, for example via an address sanitizer).
    *
    * \remarks{It is always impossible to allocate 0 bytes, however.
    * The minimum will be 1, then.} */
   su_MEM_ALLOC_MIN = Z_ALIGN(1)
};

/*! Most \r{su_mem_set_conf()} flags are \r{su_MEM_ALLOC_DEBUG} specific.
 * They are bits not values unless otherwise noted. */
enum su_mem_conf_option{
   su_MEM_CONF_NONE,
   /* su_MEM_ALLOC_DEBUG only: booleans */
   su_MEM_CONF_DEBUG = 1u<<0, /*!< More tests, be verbose. */
   su_MEM_CONF_ON_ERROR_EMERG = 1u<<1, /*!< Error out if tests fail. */
   su_MEM_CONF_LINGER_FREE = 1u<<2, /*!< Keep \c{free()}s until.. */
   su_MEM_CONF_LINGER_FREE_RELEASE = 1u<<3, /*!< ..they are released */

/*madvise,free area count*/
/*say_if_empty_on_exit,statistics*/
   su__MEM_CONF_MAX = su_MEM_CONF_LINGER_FREE_RELEASE
};

#ifdef su_MEM_ALLOC_DEBUG
EXPORT boole su__mem_get_can_book(uz size, uz no);
EXPORT boole su__mem_check(su_DBG_LOC_ARGS_DECL_SOLE);
EXPORT boole su__mem_trace(boole dumpmem  su_DBG_LOC_ARGS_DECL);
#endif

/*! Rather internal, but due to the \r{su_mem_alloc_flags} \a{maf} maybe
 * handy sometimes.
 * Normally to be used through the macros below */
EXPORT void *su_mem_allocate(uz size, uz no,
      BITENUM_IS(u32,su_mem_alloc_flags) maf  su_DBG_LOC_ARGS_DECL);

/*! If \NIL is returned, then the original memory has not been freed.
 * (From our point of view. TODO We are yet backed by OS malloc.) */
EXPORT void *su_mem_reallocate(void *ovp, uz size, uz no,
      BITENUM_IS(u32,su_mem_alloc_flags) maf  su_DBG_LOC_ARGS_DECL);

/*! \_ */
EXPORT void su_mem_free(void *ovp  su_DBG_LOC_ARGS_DECL);

/*! \_ */
#define su_MEM_ALLOCATE(SZ,NO,F) \
      su_mem_allocate(SZ, NO, F  su_DBG_LOC_ARGS_INJ)

/*! \_ */
#define su_MEM_REALLOCATE(OVP,SZ,NO,F) \
      su_mem_reallocate(OVP, SZ, NO, F  su_DBG_LOC_ARGS_INJ)

#ifdef su_HAVE_DBG_LOC_ARGS
# define su_MEM_ALLOCATE_LOC(SZ,NO,F,FNAME,LNNO) \
      su_mem_allocate(SZ, NO, F, FNAME, LNNO)

# define su_MEM_REALLOCATE_LOC(OVP,SZ,NO,F,FNAME,LNNO) \
      su_mem_reallocate(OVP, SZ, NO, F, FNAME, LNNO)
#else
   /*! \_ */
# define su_MEM_ALLOCATE_LOC(SZ,NO,F,FNAME,LNNO) \
      su_mem_allocate(SZ, NO, F)

   /*! \_ */
# define su_MEM_REALLOCATE_LOC(OVP,SZ,NO,F,FNAME,LNNO) \
      su_mem_reallocate(OVP, SZ, NO, F)
#endif

/* The "normal" interface; there X, X_LOC, and X_LOCOR */

/*! \_ */
#define su_MEM_ALLOC(SZ) su_MEM_ALLOCATE(SZ, 1, su_MEM_ALLOC_NONE)
/*! \_ */
#define su_MEM_ALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, 1, su_MEM_ALLOC_NONE, FNAME, LNNO)

/*! \_ */
#define su_MEM_ALLOC_N(SZ,NO) su_MEM_ALLOCATE(SZ, NO, su_MEM_ALLOC_NONE)
/*! \_ */
#define su_MEM_ALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, NO, su_MEM_ALLOC_NONE, FNAME, LNNO)

/*! \_ */
#define su_MEM_CALLOC(SZ) su_MEM_ALLOCATE(SZ, 1, su_MEM_ALLOC_ZERO)
/*! \_ */
#define su_MEM_CALLOC_LOC(SZ,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, 1, su_MEM_ALLOC_ZERO, FNAME, LNNO)

/*! \_ */
#define su_MEM_CALLOC_N(SZ,NO) su_MEM_ALLOCATE(SZ, NO, su_MEM_ALLOC_ZERO)
/*! \_ */
#define su_MEM_CALLOC_N_LOC(SZ,NO,FNAME,LNNO) \
      su_MEM_ALLOCATE_LOC(SZ, NO, su_MEM_ALLOC_ZERO, FNAME, LNNO)

/*! \_ */
#define su_MEM_REALLOC(OVP,SZ) su_MEM_REALLOCATE(OVP, SZ, 1, su_MEM_ALLOC_NONE)
/*! \_ */
#define su_MEM_REALLOC_LOC(OVP,SZ,FNAME,LNNO) \
      su_MEM_REALLOCATE_LOC(OVP, SZ, 1, su_MEM_ALLOC_NONE, FNAME, LNNO)

/*! \_ */
#define su_MEM_REALLOC_N(OVP,SZ,NO) \
      su_MEM_REALLOCATE(OVP, SZ, NO, su_MEM_ALLOC_NONE)
/*! \_ */
#define su_MEM_REALLOC_N_LOC(OVP,SZ,NO,FNAME,LNNO) \
      su_MEM_REALLOCATE_LOC(OVP, SZ, NO, su_MEM_ALLOC_NONE, FNAME, LNNO)

/*! \_ */
#define su_MEM_TALLOC(T,NO) su_S(T *,su_MEM_ALLOC_N(sizeof(T), NO))
/*! \_ */
#define su_MEM_TALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_ALLOC_N_LOC(sizeof(T), NO, FNAME, LNNO))

/*! \_ */
#define su_MEM_TCALLOC(T,NO) su_S(T *,su_MEM_CALLOC_N(sizeof(T), NO))
/*! \_ */
#define su_MEM_TCALLOC_LOC(T,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_CALLOC_N_LOC(sizeof(T), NO, FNAME, LNNO))

/*! \_ */
#define su_MEM_TREALLOC(T,OVP,NO) \
      su_S(T *,su_MEM_REALLOC_N(OVP, sizeof(T), NO))
/*! \_ */
#define su_MEM_TREALLOC_LOC(T,OVP,NO,FNAME,LNNO) \
      su_S(T *,su_MEM_REALLOC_N_LOC(OVP, sizeof(T), NO, FNAME, LNNO))

/*! \_ */
#define su_MEM_TALLOCF(T,NO,F) su_S(T *,su_MEM_ALLOCATE(sizeof(T), NO, F))
/*! \_ */
#define su_MEM_TALLOCF_LOC(T,NO,F,FNAME,LNNO) \
      su_S(T *,su_MEM_ALLOCATE_LOC(sizeof(T), NO, F, FNAME, LNNO))

/*! \_ */
#define su_MEM_TCALLOCF(T,NO,F) \
   su_S(T *,su_MEM_ALLOCATE(sizeof(T), NO, su_MEM_ALLOC_ZERO | (F)))
/*! \_ */
#define su_MEM_TCALLOCF_LOC(T,NO,F,FNAME,LNNO) \
   su_S(T *,su_MEM_ALLOCATE_LOC(sizeof(T), NO, su_MEM_ALLOC_ZERO | (F)),\
      FNAME, LNNO)

/*! \_ */
#define su_MEM_TREALLOCF(T,OVP,NO,F) \
      su_S(T *,su_MEM_REALLOCATE(OVP, sizeof(T), NO, F))
/*! \_ */
#define su_MEM_TREALLOCF_LOC(T,OVP,NO,F,FNAME,LNNO) \
      su_S(T *,su_MEM_REALLOCATE_LOC(OVP, sizeof(T), NO, F, FNAME, LNNO))

/*! \_ */
#define su_MEM_FREE(OVP) su_mem_free(OVP  su_DBG_LOC_ARGS_INJ)
#ifdef su_HAVE_DBG_LOC_ARGS
# define su_MEM_FREE_LOC(OVP,FNAME,LNNO) su_mem_free(OVP, FNAME, LNNO)
#else
   /*! \_ */
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
   /*! \_ */
# define su_MEM_ALLOC_LOCOR(SZ,ORARGS) su_MEM_ALLOC(SZ)
   /*! \_ */
# define su_MEM_ALLOC_N_LOCOR(SZ,NO,ORARGS) su_MEM_ALLOC_N(SZ, NO)
   /*! \_ */
# define su_MEM_CALLOC_LOCOR(SZ,ORARGS) su_MEM_CALLOC(SZ)
   /*! \_ */
# define su_MEM_CALLOC_N_LOCOR(SZ,NO,ORARGS) su_MEM_CALLOC_N(SZ, NO)
   /*! \_ */
# define su_MEM_REALLOC_LOCOR(OVP,SZ,ORARGS) su_MEM_REALLOC(OVP, SZ)
   /*! \_ */
# define su_MEM_REALLOC_N_LOCOR(OVP,SZ,NO,ORARGS) su_MEM_REALLOC_N(OVP, SZ, NO)
   /*! \_ */
# define su_MEM_TALLOC_LOCOR(T,NO,ORARGS) su_MEM_TALLOC(T, NO)
   /*! \_ */
# define su_MEM_TCALLOC_LOCOR(T,NO,ORARGS) su_MEM_TCALLOC(T, NO)
   /*! \_ */
# define su_MEM_TREALLOC_LOCOR(T,OVP,NO) su_MEM_TREALLOC(T, OVP, NO)
   /*! \_ */
# define su_MEM_TALLOCF_LOCOR(T,NO,F,ORARGS) su_MEM_TALLOCF(T, NO, F)
   /*! \_ */
# define su_MEM_TCALLOCF_LOCOR(T,NO,F,ORARGS) su_MEM_TCALLOCF(T, NO, F)
   /*! \_ */
# define su_MEM_TREALLOCF_LOCOR(T,OVP,F,NO) su_MEM_TREALLOCF(T, OVP, NO, F)
   /*! \_ */
# define su_MEM_FREE_LOCOR(OVP,ORARGS) su_MEM_FREE_LOC(OVP, ORARGS)
#endif /* !su_HAVE_DBG_LOC_ARGS */
/*! @} *//* }}} */

/* heap support {{{ */
/*!
 * \defgroup MEM_CACHE_SUP Heap "support"
 * \ingroup MEM
 * \brief \r{MEM_CACHE_ALLOC} support (\r{su/mem.h})
 * @{
 */

/*! In a chunk used to store objects of \a{size}, which currently contains
 * \a{no} objects, can \a{notoadd} objects be added?
 * \remarks{Always compares against \r{su_UZ_MAX}.}
 * \remarks{Results depend on \r{su_MEM_ALLOC_DEBUG}.} */
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

/*! Of heap allocations */
#define su_mem_get_usable_size(SZ) su_Z_ALIGN(SZ) /* XXX fake */

/*! Of heap allocations */
#define su_mem_get_usable_size_32(SZ) su_S(su_u32,su_Z_ALIGN(SZ)) /*XXX*/
/* XXX get_usable_size_ptr(), get_memory_usage()  */

/*! Most options are actually boolean flags: multiple thereof can be set or
 * cleared with one operation by ORing together the according
 * \r{su_mem_conf_option}s in \a{mco} , the (then) \r{su_boole} \a{val} will be
 * used for all of them.
 * \list{\li{
 * \c{LINGER_FREE}: unsetting causes \c{LINGER_FREE_RELEASE} to take place.
 * }\li{
 * \c{LINGER_FREE_RELEASE} completely ignores \a{val}.
 * Using this flag will perform a \r{su_mem_check()} first.
 * }} */
EXPORT void su_mem_set_conf(BITENUM_IS(u32,su_mem_conf_option) mco, uz val);

/*! Check all existing allocations for bound violations etc.
 * Return \TRU1 on errors, \TRUM1 on fatal errors.
 * Always succeeds if \r{su_MEM_ALLOC_DEBUG} is not defined. */
INLINE boole su_mem_check(void){
#ifdef su_MEM_ALLOC_DEBUG
   return su__mem_check(su_DBG_LOC_ARGS_INJ_SOLE);
#else
   return FAL0;
#endif
}

/* XXX mem_check_ptr()  */
/* XXX mem_[usage_]info(); opt: output channel, thread ptr */

/*! Dump statistics and memory content (with \a{dumpmem})
 * via \r{su_log_write()} and \r{su_LOG_INFO} level.
 * Checks all existing allocations for bound violations etc. while doing so.
 * A positive \a{dumpmem} appends memory content to the allocation info line,
 * a negative places the dump on a follow line.
 * Return \TRU1 on errors, \TRUM1 on fatal errors.
 * Always succeeds if \r{su_MEM_ALLOC_DEBUG} is not defined. */
INLINE boole su_mem_trace(boole dumpmem){ /* XXX ochannel, thrptr */
   UNUSED(dumpmem);
#ifdef su_MEM_ALLOC_DEBUG
   return su__mem_trace(dumpmem su_DBG_LOC_ARGS_INJ);
#else
   return FAL0;
#endif
}
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class mem;

/* {{{ */
/*! \_ */
class mem{
   su_CLASS_NO_COPY(mem);
public:
   /* c++ memory tools {{{ */
   /*!
    * \defgroup CXX_MEM_TOOLS C++ memory tools
    * \ingroup MEM_TOOLS
    * \brief C++ variant of \r{MEM_TOOLS} (\r{su/mem.h})
    *
    * In general argument pointers may be given as \NIL if a length argument
    * which covers them is given as 0.
    * @{
    */

   /*! \copydoc{su_mem_cmp()} */
   static sz cmp(void const *vpa, void const *vpb, uz len){
      return su_mem_cmp(vpa, vpb, len);
   }

   /*! \copydoc{su_mem_copy()} */
   static void *copy(void *vp, void const *src, uz len){
      return su_mem_copy(vp, src, len);
   }

   /*! \copydoc{su_mem_find()} */
   static void *find(void const *vp, s32 what, uz len){
      return su_mem_find(vp, what, len);
   }

   /*! \copydoc{su_mem_rfind()} */
   static void *rfind(void const *vp, s32 what, uz len){
      return su_mem_rfind(vp, what, len);
   }

   /*! \copydoc{su_mem_move()} */
   static void *move(void *vp, void const *src, uz len){
      return su_mem_move(vp, src, len);
   }

   /*! \copydoc{su_mem_set()} */
   static void *set(void *vp, s32 what, uz len){
      return su_mem_set(vp, what, len);
   }

   /*! \copydoc{su_mem_zero()} */
   static void zero(void *vp, uz len) {su_mem_zero(vp, len);}
   /*! @} *//* }}} */

public:
   /* c++ heap memory {{{ */
   /*!
    * \defgroup CXX_MEM_CACHE_ALLOC C++ heap memory
    * \ingroup MEM_CACHE_ALLOC
    * \brief C++ variant of \r{MEM_CACHE_ALLOC} (\r{su/mem.h})
    *
    * It interacts with \r{state::err_nomem} and \r{state::err_overflow},
    * but also allows per-call failure ignorance.
    * \remarks{No mirrors of the C interface macros are available.}
    * @{
    */

   struct johnny;
   struct mary;

   /*! \copydoc{su_mem_alloc_flags} */
   enum alloc_flags{
      alloc_none = su_MEM_ALLOC_NONE, /*!< \copydoc{su_MEM_ALLOC_NONE} */
      alloc_zero = su_MEM_ALLOC_ZERO, /*!< \copydoc{su_MEM_ALLOC_ZERO} */
      /*! \copydoc{su_MEM_ALLOC_CONCEAL} */
      alloc_conceal = su_MEM_ALLOC_CONCEAL,

      /*! \copydoc{su_MEM_ALLOC_32BIT_OVERFLOW} */
      alloc_32bit_overflow = su_MEM_ALLOC_32BIT_OVERFLOW,
      /*! \copydoc{su_MEM_ALLOC_31BIT_OVERFLOW} */
      alloc_31bit_overflow = su_MEM_ALLOC_31BIT_OVERFLOW,

      /*! \copydoc{su_MEM_ALLOC_OVERFLOW_OK} */
      alloc_overflow_ok = su_MEM_ALLOC_OVERFLOW_OK,
      /*! \copydoc{su_MEM_ALLOC_NOMEM_OK} */
      alloc_nomem_ok = su_MEM_ALLOC_NOMEM_OK,
      /*! \copydoc{su_MEM_ALLOC_MAYFAIL} */
      alloc_mayfail = su_MEM_ALLOC_MAYFAIL,
      /*! \copydoc{su_MEM_ALLOC_MUSTFAIL} */
      alloc_mustfail = su_MEM_ALLOC_MUSTFAIL,

      alloc_mark_0 = su_MEM_ALLOC_MARK_0, /*!< \copydoc{su_MEM_ALLOC_MARK_0} */
      alloc_mark_1 = su_MEM_ALLOC_MARK_1, /*!< \copydoc{su_MEM_ALLOC_MARK_1} */
      alloc_mark_2 = su_MEM_ALLOC_MARK_2, /*!< \copydoc{su_MEM_ALLOC_MARK_2} */
      alloc_mark_3 = su_MEM_ALLOC_MARK_3 /*!< \copydoc{su_MEM_ALLOC_MARK_3} */
   };

   enum{
      alloc_min = su_MEM_ALLOC_MIN /*!< \copydoc{su_MEM_ALLOC_MIN} */
   };

   /*! \copydoc{su_mem_conf_option} */
   enum conf_option{
      conf_debug = su_MEM_CONF_DEBUG, /*!< \copydoc{su_MEM_CONF_DEBUG} */
      /*! \copydoc{su_MEM_CONF_ON_ERROR_EMERG} */
      conf_on_error_emerg = su_MEM_CONF_ON_ERROR_EMERG,
      /*! \copydoc{su_MEM_CONF_LINGER_FREE} */
      conf_linger_free = su_MEM_CONF_LINGER_FREE,
      /*! \copydoc{su_MEM_CONF_LINGER_FREE_RELEASE} */
      conf_linger_free_release = su_MEM_CONF_LINGER_FREE_RELEASE
   };

/*! The base of \r{su_MEM_NEW()} etc.
 * Be aware it does not even set \r{su_MEM_ALLOC_MUSTFAIL} automatically. */
#define su_MEM_ALLOC_NEW(T,F) \
      new(su_MEM_ALLOCATE(sizeof(T), 1, F),\
         su_S(su_NSPC(su)mem::johnny const*,su_NIL)) T
/*! \r{su_MEM_ALLOC_NEW()} */
#define su_MEM_ALLOC_NEW_LOC(T,F,FNAME,LNNO) \
      new(su_MEM_ALLOCATE_LOC(sizeof(T), 1, F, FNAME, LNNO),\
         su_S(su_NSPC(su)mem::johnny const*,su_NIL)) T

/*! \_ */
#define su_MEM_NEW(T) su_MEM_ALLOC_NEW(T, su_MEM_ALLOC_MUSTFAIL)
/*! \_ */
#define su_MEM_NEW_LOC(T,FNAME,LNNO) \
   su_MEM_ALLOC_NEW_LOC(T, su_MEM_ALLOC_MUSTFAIL, FNAME, LNNO)

/*! \_ */
#define su_MEM_CNEW(T) \
   su_MEM_ALLOC_NEW(T, su_MEM_ALLOC_ZERO | su_MEM_ALLOC_MUSTFAIL)
/*! \_ */
#define su_MEM_CNEW_LOC(T,FNAME,LNNO) \
   su_MEM_ALLOC_NEW_LOC(T, su_MEM_ALLOC_ZERO | su_MEM_ALLOC_MUSTFAIL,\
      FNAME, LNNO)

/*! \_ */
#define su_MEM_NEW_HEAP(T,VP) new(VP, su_S(su_NSPC(su)mem::johnny*,su_NIL)) T

/*! \_ */
#define su_MEM_DEL(TP) (su_NSPC(su)mem::del__heap(TP), su_MEM_FREE(TP))
/*! \_ */
#define su_MEM_DEL_LOC(TP,FNAME,LNNO) \
      (su_NSPC(su)mem::del__heap(TP), su_MEM_FREE_LOC(TP, FNAME, LNNO))

/*! \_ */
#define su_MEM_DEL_HEAP(TP) su_NSPC(su)mem::del__heap(TP)
/*! \_ */
#define su_MEM_DEL_HEAP_LOC(TP,FNAME,LNNO) su_NSPC(su)mem::del__heap(TP)

/*! \_ */
#define su_MEM_DEL_PRIVATE(T,TP) \
      (su_ASSERT((TP) != su_NIL), (TP)->~T(), su_MEM_FREE(TP))
/*! \_ */
#define su_MEM_DEL_PRIVATE_LOC(T,TP,FNAME,LNNO) \
      (su_ASSERT_LOC((TP) != su_NIL, FNAME, LNNO),\
       (TP)->~T(), su_MEM_FREE_LOC(TP, FNAME, LNNO))

/*! \_ */
#define su_MEM_DEL_HEAP_PRIVATE(T,TP) (su_ASSERT((TP) != su_NIL), (TP)->~T())
/*! \_ */
#define su_MEM_DEL_HEAP_PRIVATE_LOC(T,TP,FNAME,LNNO) \
   (su_ASSERT((TP) != su_NIL), (TP)->~T())

/* (The painful _LOCOR series) */
#ifdef su_HAVE_DBG_LOC_ARGS
# define su_MEM_NEW_LOCOR(T,ORARGS) su_MEM_NEW_LOC(T, ORARGS)
# define su_MEM_CNEW_LOCOR(T,ORARGS) su_MEM_CNEW_LOC(T, ORARGS)
# define su_MEM_NEW_HEAP_LOCOR(T,VP,ORARGS) su_MEM_NEW_HEAP_LOC(T, VP, ORARGS)
# define su_MEM_DEL_LOCOR(TP,ORARGS) su_MEM_DEL_LOC(TP, ORARGS)
# define su_MEM_DEL_HEAP_LOCOR(TP,ORARGS) su_MEM_DEL_HEAP_LOC(TP, ORARGS)
# define su_MEM_DEL_PRIVATE_LOCOR(T,TP,ORARGS) \
      su_MEM_DEL_PRIVATE_LOC(T, TP, ORARGS)
# define su_MEM_DEL_HEAP_PRIVATE_LOCOR(T,TP,ORARGS) \
      su_MEM_DEL_HEAP_PRIVATE_LOC(T, TP, ORARGS)
#else
   /*! \_ */
# define su_MEM_NEW_LOCOR(T,ORARGS) su_MEM_NEW(T)
   /*! \_ */
# define su_MEM_CNEW_LOCOR(T,ORARGS) su_MEM_CNEW(T)
   /*! \_ */
# define su_MEM_NEW_HEAP_LOCOR(T,VP,ORARGS) su_MEM_NEW_HEAP(T, VP)
   /*! \_ */
# define su_MEM_DEL_LOCOR(TP,ORARGS) su_MEM_DEL(TP)
   /*! \_ */
# define su_MEM_DEL_HEAP_LOCOR(TP,ORARGS) su_MEM_DEL_HEAP(TP)
   /*! \_ */
# define su_MEM_DEL_PRIVATE_LOCOR(T,TP,ORARGS) su_MEM_DEL_PRIVATE(T, TP)
   /*! \_ */
# define su_MEM_DEL_HEAP_PRIVATE_LOCOR(T,TP,ORARGS) \
      su_MEM_DEL_HEAP_PRIVATE(T, TP)
#endif /* !su_HAVE_DBG_LOC_ARGS */
   /*! @} *//* }}} */

public:
   /* c++ heap support {{{ */
   /*!
    * \defgroup CXX_MEM_CACHE_SUP C++ heap "support"
    * \ingroup MEM_CACHE_SUP
    * \brief \r{CXX_MEM_CACHE_ALLOC} support (\r{su/mem.h})
    * @{
    */

   /*! \copydoc{su_mem_get_usable_size()} */
   static uz get_usable_size(uz size) {return su_mem_get_usable_size(size);}

   /*! \copydoc{su_mem_get_usable_size_32()} */
   static u32 get_usable_size_32(uz size){
      return su_mem_get_usable_size_32(size);
   }

   /*! \copydoc{su_mem_conf_option()} */
   static void set_conf(BITENUM_IS(u32,conf_option) co, uz val){
      su_mem_set_conf(co, val);
   }

   /*! \r{su_mem_check()} */
   static void check(void) {su_mem_check();}

   /*! \r{su_mem_trace()} */
   static void trace(boole dumpmem=FAL0) {su_mem_trace(dumpmem);}

   template<class T>
   static void del__heap(T *tptr){
      ASSERT_RET_VOID(tptr != NIL);
      tptr->~T();
   }
   /*! @} *//* }}} */
};
/* }}} */

NSPC_END(su)

/*!
 * \ingroup CXX_MEM_CACHE_ALLOC
 * In order to be able, a global overwrite of \c{new()} is necessary.
 */
inline void *operator new(size_t sz, void *vp, NSPC(su)mem::johnny const *j){
   UNUSED(sz);
   UNUSED(j);
   return vp;
}

# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* !su_MEM_H */
/* s-it-mode */
