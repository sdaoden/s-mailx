/*@ Threads of execution.
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
#ifndef su_THREAD_H
#define su_THREAD_H

/*!
 * \file
 * \ingroup THREAD
 * \brief \r{THREAD}
 */

#include <su/code.h>

#include <su/x-thread.h> /* 1. */
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_thread;

/* thread {{{ */
/*!
 * \defgroup THREAD Threads of execution
 * \ingroup SMP
 * \brief Threads of execution (\r{su/thread.h})
 *
 * \remarks{In order to use multiple threads \r{su_state_create()} must have
 * been called with the \r{su_STATE_MT} flag set.}
 *
 * Mostly a no-op unless \r{su_HAVE_MT} is available.
 * @{
 */

enum su_thread_flags{
   su_THREAD_MAIN = 1u<<0
   /*su_THREAD_HAVE_CXX = 1u<<16 xxx for now just cast the C++ object! */
};

/*! \_ */
struct su_thread{
   struct{
      MT( u8 os[su__THREAD_IMPL_SIZE]; )
      u8 ATOMIC lck;
      u8 flags;
      u8 pad__[2];
      s32 err_no;
      char const *name;
      struct su_thread *wait_last; /* Lock-wait-suspension list */
      DVLOR( struct su_nyd_control, void ) *nydctl;
   }
#ifdef su__THREAD_IMPL_ALIGNMENT
      su_CC_ALIGNED(su__THREAD_IMPL_ALIGNMENT)
# undef su__THREAD_IMPL_ALIGNMENT
#endif
      t_;
};

/*! Sometimes a block needs to do work but also ensure that the current
 * thread's error number is reestablished before the block is left.
 * This macro opens a scope and saves the number, it will be reestablished once
 * the scope is left via \r{su_THREAD_ERR_NO_SCOPE_OU()}. */
#define su_THREAD_ERR_NO_SCOPE_IN() \
do{\
   struct su_thread *__su__thread_self__ = su_thread_self();\
   s32 __su__thread_self__err_no__ = __su__thread_self__->t_.err_no /**/

/*! Counterpart to \r{su_THREAD_ERR_NO_SCOPE_OU()}. */
#define su_THREAD_ERR_NO_SCOPE_OU() \
   __su__thread_self__->t_.err_no = __su__thread_self__err_no__;\
}while(0)

EXPORT_DATA struct su_thread su__thread_main;

#include <su/x-thread.h> /* 2. */

/*! \_ */
INLINE char const *su_thread_name(struct su_thread const *self){
   ASSERT(self);
   return self->t_.name;
}

/*! \_ */
INLINE s32 su_thread_err_no(struct su_thread const *self){
   ASSERT(self);
   return self->t_.err_no;
}

/*! \_ */
INLINE void su_thread_set_err_no(struct su_thread *self, s32 e){
   ASSERT(self);
   self->t_.err_no = e;
}

/*! Obtain handle to calling thread. */
INLINE struct su_thread *su_thread_self(void){
   return su__thread_self();
}

/*! \r{su_thread_err_no()} of the calling thread */
INLINE s32 su_thread_get_err_no(void){
   return su_thread_err_no(su_thread_self());
}

/*! Yield the processor of the calling thread */
EXPORT void su_thread_yield(void);

#ifdef su_HAVE_MT
# error .
#endif /* su_HAVE_MT */
/*! @} *//* }}} */

#include <su/x-thread.h> /* 3. */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class thread;

/* thread {{{ */
/*!
 * \ingroup THREAD
 * C++ variant of \r{THREAD} (\r{su/thread.h})
 */
class thread : private su_thread{
   friend class mutex;
   friend class spinlock;

   su_CLASS_NO_COPY(thread);
public:
#ifdef su_HAVE_MT
   thread(char const *name=NIL) {su_thread_create(this, name);}

   ~thread(void) {su_thread_gut(this);}

   s32 create( .. u32 estate);
#endif

   /*! \copydoc{su_thread_name()} */
   char const *name(void) const {return su_thread_name(this);}

   /*! \copydoc{su_thread_err_no()} */
   s32 err_no(void) const {return su_thread_err_no(this);}

   /*! \copydoc{su_thread_set_err_no()} */
   void set_err_no(s32 e) {su_thread_set_err_no(this, e);}

#ifdef su_HAVE_MT
# error
#endif /* su_HAVE_MT */

   /*! \copydoc{su_thread_self()} */
   static thread *self(void) {return R(thread*,su_thread_self());}

   /*! \copydoc{su_thread_get_err_no()} */
   static s32 get_err_no(void) {return su_thread_get_err_no();}

   /*! \copydoc{su_thread_yield()} */
   static void yield(void) {su_thread_yield();}
};

#ifndef DOXYGEN
CXXCAST(thread, struct su_thread);
#endif
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_THREAD_H */
/* s-it-mode */
