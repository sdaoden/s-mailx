/*@ Mutual exclusion device.
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
#ifndef su_MUTEX_H
#define su_MUTEX_H

/*!
 * \file
 * \ingroup MUTEX
 * \brief \r{MUTEX}
 */

#include <su/code.h>

su_USECASE_MX_DISABLED

#include <su/atomic.h>
#include <su/thread.h>

#include <su/x-mutex.h> /* 1. */
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_mutex;

/* mutex {{{ */
/*!
 * \defgroup MUTEX Mutual exclusion device
 * \ingroup SMP
 * \brief Mutual exclusion device (\r{su/mutex.h})
 *
 * Without \r{su_HAVE_MT} this is a "checking" no-op wrapper.
 * Dependent upon \r{su_DVLOR()} the debug-enabled functions simply ignore
 * their debug-only arguments.
 * Please see \r{SMP} for peculiarities of \SU lock types.
 * @{
 */

#if !defined su_HAVE_MT || su_ATOMIC_IS_REAL
# define su__MUTEX_SPIN
#endif

enum su_mutex_flags{
   su_MUTEX_FLAT = 1u<<0,
   su_MUTEX_INIT = 1u<<1
};

#if DVLOR(1, 0)
enum su__mutex_xfn{
   su__MUTEX_GUT,
   su__MUTEX_LOCK,
   su__MUTEX_TRYLOCK,
   su__MUTEX_UNLOCK_NOLOCK,
   su__MUTEX_UNLOCK
};
#endif

/*! \_ */
struct su_mutex{
   struct{
      MT( u8 os[su__MUTEX_IMPL_SIZE]; )
#ifndef su__MUTEX_SPIN
      MT( u8 os_lck[su__MUTEX_IMPL_SIZE]; )
#endif
      u8 ATOMIC lck; /* Iff su__MUTEX_SPIN */
      u8 flags;
      u16 line; /* dbg */
      u32 count;
      struct su_thread *owner;
      struct su_thread *waiters;
      char const *file; /* dbg */
      char const *name; /* Last field! */
   }
#ifdef su__MUTEX_IMPL_ALIGNMENT
      su_CC_ALIGNED(su__MUTEX_IMPL_ALIGNMENT)
# undef su__MUTEX_IMPL_ALIGNMENT
#endif
      mtx_;
};

#define su__MUTEX_ARGS_DECL DVLOR(su_COMMA char const *file su_COMMA u32 line,)
#define su__MUTEX_ARGS DVLOR(su_COMMA file su_COMMA line,)
#define su__MUTEX_ARGS_INJ su_DVLOR(su_COMMA __FILE__ su_COMMA __LINE__,)

/* */
#ifdef DOXYGEN
   /*! Please see \r{SMP} for consequences of initializing objects like so. */
# define su_MUTEX_I9R(NAME_OR_NIL)

  /*! Non-recursive initializer.
   * A flat mutex can (should) be locked once only, a condition that is
   * verified by debug-enabled code.
   * Please see \r{SMP} for consequences of initializing objects like so. */
# define su_MUTEX_FLAT_I9R(NAME_OR_NIL)
#else
# ifdef su_HAVE_MT
#  ifdef su__MUTEX_SPIN
#   define su__MUTEX_IMPL_I9R() {0,},
#  else
#   define su__MUTEX_IMPL_I9R() {0,}, {0,},
#  endif
# else
#  define su__MUTEX_IMPL_I9R()
# endif
# define su__MUTEX_I9R(FLAGS,NAME_OR_NIL) \
   {{su__MUTEX_IMPL_I9R() \
      su_FIELD_INITN(lck) 0,\
      su_FIELD_INITN(flags) FLAGS,\
      su_FIELD_INITN(line) 0,\
      su_FIELD_INITN(count) 0,\
      su_FIELD_INITN(owner) su_NIL,\
      su_FIELD_INITN(waiters) su_NIL,\
      su_FIELD_INITN(file) su_NIL,\
      su_FIELD_INITN(name) NAME_OR_NIL}}

# define su_MUTEX_I9R(NAME_OR_NIL) su__MUTEX_I9R(0, NAME_OR_NIL)
# define su_MUTEX_FLAT_I9R(NAME_OR_NIL) \
   su__MUTEX_I9R(su_MUTEX_FLAT, NAME_OR_NIL)
#endif /* !DOXYGEN */

/*! Uses \r{su_DVLOR()} to select the according mutex lock interface. */
#define su_MUTEX_LOCK(SELF) \
   su_DVLOR(su_mutex_lock_dbg,su_mutex_lock)(SELF su__MUTEX_ARGS_INJ)

/*! Uses \r{su_DVLOR()} to select the according mutex lock interface. */
#define su_MUTEX_LOCK_TS(SELF,TSP) \
   su_DVLOR(su_mutex_lock_ts_dbg,su_mutex_lock_ts)\
      (SELF, TSP su__MUTEX_ARGS_INJ)

/*! Uses \r{su_DVLOR()} to select the according mutex trylock interface. */
#define su_MUTEX_TRYLOCK(SELF) \
   su_DVLOR(su_mutex_trylock_dbg,su_mutex_trylock)(SELF su__MUTEX_ARGS_INJ)

/*! Uses \r{su_DVLOR()} to select the according mutex trylock interface. */
#define su_MUTEX_TRYLOCK_TS(SELF,TSP) \
   su_DVLOR(su_mutex_trylock_ts_dbg,su_mutex_trylock_ts)\
      (SELF, TSP su__MUTEX_ARGS_INJ)

/*! Uses \r{su_DVLOR()} to select the according mutex unlock interface. */
#define su_MUTEX_UNLOCK(SELF) \
   su_DVLOR(su_mutex_unlock_dbg,su_mutex_unlock)(SELF su__MUTEX_ARGS_INJ)

/* */
#if DVLOR(1, 0)
EXPORT boole su__mutex_check(struct su_mutex *self,
      enum su__mutex_xfn mf, struct su_thread *tsp, char const *file,u32 line);
#endif

#include <su/x-mutex.h> /* 2. */

#ifndef su__MUTEX_HAVE_FUNS
# ifndef su_HAVE_MT
#  error Combination will not work
# endif

EXPORT s32 su__mutex_os_create(struct su_mutex *self, u32 estate);
EXPORT void su__mutex_os_gut(struct su_mutex *self);
EXPORT void su__mutex_lock(struct su_mutex *self, struct su_thread *tsp
      su__MUTEX_ARGS_DECL);
EXPORT boole su__mutex_trylock(struct su_mutex *self, struct su_thread *tsp
      su__MUTEX_ARGS_DECL);
EXPORT void su__mutex_unlock(struct su_mutex *self  su__MUTEX_ARGS_DECL);
#endif

/*! Creates a recursive mutex.
 * \ESTATE_RV. */
INLINE s32 su_mutex_create(struct su_mutex *self, char const *name_or_nil,
      u32 estate){
   s32 rv;
   ASSERT(self);
   FIELD_RANGE_ZERO(struct su_mutex, self, mtx_.lck, mtx_.file);
   self->mtx_.name = name_or_nil;
   rv = su_STATE_NONE;
   MT(
      if((rv = su__mutex_os_create(self, estate)) == su_STATE_NONE)
         self->mtx_.flags |= su_MUTEX_INIT;
   )
   return rv;
}

/*! \_ */
INLINE void su_mutex_gut(struct su_mutex *self){
   ASSERT(self);
   DVL( if(su__mutex_check(self, su__MUTEX_GUT, NIL su__MUTEX_ARGS_INJ)) )
      MT( if(self->mtx_.flags & su_MUTEX_INIT) su__mutex_os_gut(self) )
         ;
}

/*! Turn \SELF into a flat mutex.
 * It is asserted that \SELF is not locked, and normally this should be called
 * directly after the constructor; \r{su_MUTEX_FLAT_I9R()} says:
 * \copydoc{su_MUTEX_FLAT_I9R()} */
INLINE void su_mutex_disable_recursion(struct su_mutex *self){
   ASSERT(self);
   ASSERT(self->mtx_.owner == NIL);
   self->mtx_.flags |= su_MUTEX_FLAT;
}

/*! The name as given to the constructor. */
INLINE char const *su_mutex_name(struct su_mutex const *self){
   ASSERT(self);
   return self->mtx_.name;
}

/*! Via \r{su_DVLOR()}: file name of last operation, or \NIL. */
INLINE char const *su_mutex_file(struct su_mutex const *self){
   ASSERT(self);
   return DVLOR(self->mtx_.file, NIL);
}

/*! Via \r{su_DVLOR()}: line number of last operation, or 0. */
INLINE u32 su_mutex_line(struct su_mutex const *self){
   ASSERT(self);
   return DVLOR(self->mtx_.line, 0);
}

/*! Is \SELF "currently locked"? */
INLINE boole su_mutex_is_locked(struct su_mutex const *self){
   ASSERT(self);
   return (self->mtx_.owner != NIL);
}

/*! Debug variant of \r{su_mutex_lock_ts()}. */
INLINE void su_mutex_lock_ts_dbg(struct su_mutex *self, struct su_thread *tsp,
      char const *file, u32 line){
   ASSERT(self);
   ASSERT_EXEC(tsp == su_thread_self(), tsp = su_thread_self());
   UNUSED(file);
   UNUSED(line);
   su__mutex_lock(self, tsp  su__MUTEX_ARGS);
}

/*! Lock mutex.
 * \remarks{\a{tsp} must be \r{su_thread_self()}.} */
INLINE void su_mutex_lock_ts(struct su_mutex *self, struct su_thread *tsp){
   ASSERT(self);
   ASSERT_EXEC(tsp == su_thread_self(), tsp = su_thread_self());
   su_mutex_lock_ts_dbg(self, tsp, __FILE__, __LINE__);
}

/*! Debug variant of \r{su_mutex_lock()}. */
INLINE void su_mutex_lock_dbg(struct su_mutex *self,
      char const *file, u32 line){
   ASSERT(self);
   su_mutex_lock_ts_dbg(self, su_thread_self(), file, line);
}

/*! \_ */
INLINE void su_mutex_lock(struct su_mutex *self){
   su_mutex_lock_dbg(self, __FILE__, __LINE__);
}

/*! Debug variant of \r{su_mutex_trylock_ts()}. */
INLINE boole su_mutex_trylock_ts_dbg(struct su_mutex *self,
      struct su_thread *tsp, char const *file, u32 line){
   ASSERT(self);
   ASSERT_EXEC(tsp == su_thread_self(), tsp = su_thread_self());
   UNUSED(file);
   UNUSED(line);
   return su__mutex_trylock(self, tsp su__MUTEX_ARGS);
}

/*! Try to lock mutex, but do not wait if it cannot be taken immediately,
 * return whether it could be taken.
 * \remarks{\a{tsp} must be \r{su_thread_self()}.} */
INLINE boole su_mutex_trylock_ts(struct su_mutex *self, struct su_thread *tsp){
   ASSERT(self);
   ASSERT_EXEC(tsp == su_thread_self(), tsp = su_thread_self());
   return su_mutex_trylock_ts_dbg(self, tsp, __FILE__, __LINE__);
}

/*! \_ */
INLINE boole su_mutex_trylock(struct su_mutex *self){
   ASSERT(self);
   return su_mutex_trylock_ts_dbg(self, su_thread_self(), __FILE__, __LINE__);
}

/*! Debug variant of \r{su_mutex_trylock()}. */
INLINE boole su_mutex_trylock_dbg(struct su_mutex *self,
      char const *file, u32 line){
   ASSERT(self);
   return su_mutex_trylock_ts_dbg(self, su_thread_self(), file, line);
}

/*! Debug variant of \r{su_mutex_unlock()}. */
INLINE void su_mutex_unlock_dbg(struct su_mutex *self,
      char const *file, u32 line){
   ASSERT(self);
   UNUSED(file);
   UNUSED(line);
   su__mutex_unlock(self su__MUTEX_ARGS);
}

/*! \_ */
INLINE void su_mutex_unlock(struct su_mutex *self){
   ASSERT(self);
   su_mutex_unlock_dbg(self, __FILE__, __LINE__);
}
/*! @} *//* }}} */

#include <su/x-mutex.h> /* 3. */

#ifndef su_SOURCE_MUTEX
# undef su__MUTEX_SPIN
# undef su__MUTEX_ARGS_DECL
# undef su__MUTEX_ARGS
# undef su__MUTEX_HAVE_FUNS
#endif

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class mutex;
//class mutex::scope;

/* mutex {{{ */
/*!
 * \ingroup MUTEX
 * C++ variant of \r{MUTEX} (\r{su/mutex.h})
 */
class mutex : private su_mutex{
   su_CLASS_NO_COPY(mutex);
public:
   class scope;

   /*! A scope object that temporarily locks the given mutex as long as it is
    * in scope; the destructor causes unlocking. */
   class scope{
      su_CLASS_NO_COPY(scope);

      mutex * const m_mtx;
   public:
      /*! \_ */
      scope(mutex *mtxp) : m_mtx(mtxp){
          ASSERT(mtxp != NIL);
          mtxp->lock();
      }

      /*! \_ */
      scope(mutex *mtxp, thread *tsp) : m_mtx(mtxp){
          ASSERT(mtxp != NIL);
          mtxp->lock(tsp);
      }

      /*! \_ */
      ~scope(void){
         ASSERT_RET(m_mtx != NIL);
         m_mtx->unlock();
      }
   };

public:
   /*! \remarks{As documented in \r{SMP} \r{create()} is real constructor!} */
   mutex(void) {STRUCT_ZERO(su_mutex, this);}

   /*! \copydoc{su_mutex_gut()} */
   ~mutex(void) {su_mutex_gut(this);}

   /*! \copydoc{su_mutex_create()} */
   s32 create(char const *name=NIL, u32 estate=state::none){
      return su_mutex_create(this, name, estate);
   }

   /*! \copydoc{su_mutex_disable_recursion()} */
   void disable_recursion(void) {
      ASSERT_RET(!is_locked());
      su_mutex_disable_recursion(this);
   }

   /*! \copydoc{su_mutex_name()} */
   char const *name(void) const {return su_mutex_name(this);}

   /*! \copydoc{su_mutex_file()} */
   char const *file(void) const {return su_mutex_file(this);}

   /*! \copydoc{su_mutex_line()} */
   u32 line(void) const {return su_mutex_line(this);}

   /*! \copydoc{su_mutex_is_locked()} */
   boole is_locked(void) const {return su_mutex_is_locked(this);}

   /*! \copydoc{su_mutex_lock()} */
   void lock(void) {su_mutex_lock(this);}

   /*! \copydoc{su_mutex_lock_dbg()} */
   void lock(char const *file, u32 line) {su_mutex_lock_dbg(this, file, line);}

   /*! \copydoc{su_mutex_lock_ts()} */
   void lock(thread *tsp){
      ASSERT_EXEC(tsp == thread::self(), tsp = thread::self());
      su_mutex_lock_ts(this, tsp);
   }

   /*! \copydoc{su_mutex_lock_ts_dbg()} */
   void lock(thread *tsp, char const *file, u32 line){
      ASSERT_EXEC(tsp == thread::self(), tsp = thread::self());
      su_mutex_lock_ts(this, tsp, file, line);
   }

   /*! \copydoc{su_mutex_trylock()} */
   boole trylock(void) {return su_mutex_trylock(this);}

   /*! \copydoc{su_mutex_trylock_dbg()} */
   boole trylock(char const *file, u32 line){
      return su_mutex_trylock_dbg(this, file, line);
   }

   /*! \copydoc{su_mutex_trylock_ts()} */
   boole trylock(thread *tsp){
      ASSERT_EXEC(tsp == thread::self(), tsp = thread::self());
      return su_mutex_trylock_ts(this, tsp);
   }

   /*! \copydoc{su_mutex_trylock_ts_dbg()} */
   boole trylock(thread *tsp, char const *file, u32 line){
      ASSERT_EXEC(tsp == thread::self(), tsp = thread::self());
      return su_mutex_trylock_ts_dbg(this, tsp, file, line);
   }

   /*! \copydoc{su_mutex_unlock()} */
   void unlock(void) {su_mutex_unlock(this);}

   /*! \copydoc{su_mutex_unlock_dbg()} */
   void unlock(char const *file, u32 line){
      su_mutex_unlock_dbg(this, file, line);
   }
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_MUTEX_H */
/* s-it-mode */
