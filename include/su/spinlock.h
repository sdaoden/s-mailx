/*@ Spinning mutual exclusion device.
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
#ifndef su_SPINLOCK_H
#define su_SPINLOCK_H

/*!
 * \file
 * \ingroup SPINLOCK
 * \brief \r{SPINLOCK}
 */

#include <su/code.h>

su_USECASE_MX_DISABLED

#include <su/atomic.h>
#include <su/thread.h>

#if !defined su_HAVE_MT || su_ATOMIC_IS_REAL
# define su__SPINLOCK_IS
#else
# include <su/mutex.h>
#endif

#include <su/x-spinlock.h> /* 1. */
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_spinlock;

/* spinlock {{{ */
/*!
 * \defgroup SPINLOCK Spinning mutual exclusion device
 * \ingroup SMP
 * \brief Spinning mutual exclusion device (\r{su/spinlock.h})
 *
 * Spinlocks are fast non-recursive mutual exclusion devices which do not
 * suspend the calling thread shall the lock already be taken, but instead busy
 * spin until they are capable to gain it.
 * They are implemented by means of \r{ATOMIC}, and dependent upon hardware
 * support busy spinning may be replaced with something better.
 * If unavailable spinlocks base themselves upon \r{su_mutex} objects, despite
 * the behaviour change that this implies.
 *
 * Spinlocks can be used to protect smallest regions of code,
 * like access to a single variable et cetera.
 * They should not be used to protect code that can give up time slices by
 * definition.
 * They offer no debug support.
 *
 * Without \r{su_HAVE_MT} this is a checking no-op wrapper.
 * Spinlock does not offer \r{su_nyd_chirp()} information.
 * Please see \r{SMP} for peculiarities of \SU lock types.
 *
 * \remarks{With \r{su_HAVE_DEVEL} and/or \r{su_HAVE_DEBUG} some sanity checks
 * are performed.
 * Note that these debug enabled spinlocks are not binary compatible with
 * non-debug spinlocks, to be more exact their type size differs.}
 * @{
 */

#if defined su__SPINLOCK_IS && DVLOR(1, 0)
# define su__SPINLOCK_DBG
#endif

#ifdef su__SPINLOCK_DBG
enum su__spinlock_xfn{
   su__SPIN_DTOR,
   su__SPIN_LOCK,
   su__SPIN_TRYLOCK,
   su__SPIN_UNLOCK
};
#endif

/*! \_ */
struct su_spinlock{
#ifdef su__SPINLOCK_IS
   up ATOMIC sl_lck; /* With __SPINLOCK_DBG a thread*, otherwise a bool */
   DVL( char const *sl_name; )
#else
   struct su_mutex sl_mtx;
#endif
};

#if defined su__SPINLOCK_IS || defined DOXYGEN
   /*! Please see \r{SMP} for consequences of initializing objects like so. */
# define su_SPINLOCK_I9R(DBG_NAME_OR_NIL) {0 su_DVL(su_COMMA DBG_NAME_OR_NIL)}
#else
# define su_SPINLOCK_I9R(DBG_NAME_OR_NIL) su_MUTEX_FLAT_I9R(DBG_NAME_OR_NIL)
#endif

#ifdef su__SPINLOCK_DBG
EXPORT boole su__spinlock_check(struct su_spinlock *self,
      enum su__spinlock_xfn slf, up v);
#endif

#include <su/x-spinlock.h> /* 2. */

/*! \_ */
INLINE s32 su_spinlock_create(struct su_spinlock *self,
      char const *dbg_name_or_nil, u32 estate){
   s32 rv;
   ASSERT(self);
   UNUSED(dbg_name_or_nil);
   UNUSED(estate);
#ifdef su__SPINLOCK_IS
   self->sl_lck = 0;
   DVL( self->sl_name = dbg_name_or_nil; )
   rv = su_STATE_NONE;
#else
   rv = su_mutex_create(&self->sl_mtx, dbg_name_or_nil, estate);
#endif
   return rv;
}

/*! \_ */
INLINE void su_spinlock_gut(struct su_spinlock *self){
   ASSERT(self);
#ifdef su__SPINLOCK_IS
   UNUSED(self);
   DVL( su__spinlock_check(self, su__SPIN_DTOR, 0); )
#else
   su_mutex_gut(&self->sl_mtx);
#endif
}

/*! \_ */
INLINE void su_spinlock_lock(struct su_spinlock *self){
   ASSERT(self);
#ifdef su__SPINLOCK_IS
   up v = DVLOR(R(up,su_thread_self()), 1);
   DVL( if(!su__spinlock_check(self, su__SPIN_LOCK, v)) return; )
   su__spinlock_lock(self, v);
#else
   su_mutex_lock(&self->sl_mtx);
#endif
}

/*! Try to lock the spinlock once, but do not spin but instead return if it
 * cannot be taken immediately.
 * Return whether the calling thread owns the lock. */
INLINE boole su_spinlock_trylock(struct su_spinlock *self){
   ASSERT(self);
#ifdef su__SPINLOCK_IS
   up v = DVLOR(R(up,su_thread_self()), 1);
   DVL( if(!su__spinlock_check(self, su__SPIN_TRYLOCK, v)) return FAL0; )
   return su__spinlock_trylock(self, v);
#else
   return su_mutex_trylock(&self->sl_mtx);
#endif
}

/*! \_ */
INLINE void su_spinlock_unlock(struct su_spinlock *self){
   ASSERT(self);
#ifdef su__SPINLOCK_IS
   DVL( if(!su__spinlock_check(self, su__SPIN_UNLOCK, 0)) return; )
   su__spinlock_unlock(self);
#else
   su_mutex_unlock(&self->sl_mtx);
#endif
}
/*! @} *//* }}} */

#include <su/x-spinlock.h> /* 3. */

#ifndef su_SOURCE_SPINLOCK
# undef su__SPINLOCK_IS
# undef su__SPINLOCK_DBG
#endif

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class spinlock;

/* spinlock {{{ */
/*!
 * \ingroup SPINLOCK
 * C++ variant of \r{SPINLOCK} (\r{su/spinlock.h})
 */
class spinlock : private su_spinlock{
   su_CLASS_NO_COPY(spinlock);
public:
   /*! \remarks{As documented in \r{SMP} \r{create()} is real constructor!} */
   spinlock(void) {STRUCT_ZERO(su_spinlock, this);}

   /*! \copydoc{su_spinlock_gut()} */
   ~spinlock(void) {su_spinlock_gut(this);}

   /*! \copydoc{su_spinlock_create()} */
   s32 create(char const *dbg_name_or_nil=NIL, u32 estate=state::none){
      return su_spinlock_create(this, dbg_name_or_nil, estate);
   }

   /*! \copydoc{su_spinlock_lock()} */
   void lock(void) {su_spinlock_lock(this);}

   /*! \copydoc{su_spinlock_trylock()} */
   boole trylock(void) {return su_spinlock_trylock(this);}

   /*! \copydoc{su_spinlock_unlock()} */
   void unlock(void) {su_spinlock_unlock(this);}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_SPINLOCK_H */
/* s-it-mode */
