/*@ Implementation of mutex.h.
 *@ NOTE: mutex_create(): used within su_state_create_core()!
 *@ TODO No wait lists and thus deadlock checking yet
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
#define su_FILE su_mutex
#define su_SOURCE
#define su_SOURCE_MUTEX

#include "su/code.h"

su_USECASE_MX_DISABLED

#include "su/atomic.h"
#include "su/thread.h"

#include "su/mutex.h"
#include "su/y-mutex.h" /* $(SU_SRCDIR) */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

MT( static struct su_mutex *a_mutex_init(struct su_mutex *self); )

#include <su/y-mutex.h> /* 2. */

#ifdef su_HAVE_MT
static struct su_mutex *
a_mutex_init(struct su_mutex *self){
   su__glck_gi9r();

   if(!(self->mtx_.flags & su_MUTEX_INIT)){
      su__mutex_os_create(self, su_STATE_ERR_NOPASS);
      self->mtx_.flags |= su_MUTEX_INIT;
   }

   su__gnlck_gi9r();
   return self;
}
#endif

#if DVLOR(1, 0)
boole
su__mutex_check(struct su_mutex *self, enum su__mutex_xfn mf,
      struct su_thread *tsp, char const *file, u32 line){
   boole rv;

   if(tsp == NIL)
      tsp = su_thread_self();

   rv = FAL0;

   switch(mf){
   case su__MUTEX_GUT:
      if(!(self->mtx_.flags & su_MUTEX_INIT))
         break;
      if(self->mtx_.owner != NIL){
         su_log_write(su_LOG_ALERT,
            "su_mutex_gut(%p=%s): still locked by %s at %s:%u\n"
            "   Last seen at %s:%u\n",
            self, self->mtx_.name,
              su_thread_name(self->mtx_.owner), file, line,
            self->mtx_.file, self->mtx_.line);
         goto jleave;
      }
      break;
   case su__MUTEX_LOCK:
   case su__MUTEX_TRYLOCK:
      if(self->mtx_.owner == tsp && (self->mtx_.flags & su_MUTEX_FLAT)){
         su_log_write(su_LOG_ALERT,
            "su_mutex_(try)?lock(%p=%s): flat yet locked by %s at %s:%u\n"
            "   Last seen at %s:%u\n",
            self, self->mtx_.name, su_thread_name(tsp), file, line,
            self->mtx_.file, self->mtx_.line);
         goto jleave;
      }
      break;
   case su__MUTEX_UNLOCK_NOLOCK:
      su_log_write(su_LOG_ALERT,
         "su_mutex_unlock(%p=%s): never used until seen at %s:%u\n",
         self, self->mtx_.name, file, line);
      goto jleave;
   case su__MUTEX_UNLOCK:
      if(self->mtx_.owner == NIL){
         su_log_write(su_LOG_ALERT,
            "su_mutex_unlock(%p=%s): not locked at %s:%u\n"
            "   Last seen at %s:%u\n",
            self, self->mtx_.name, file, line,
            self->mtx_.file, self->mtx_.line);
         goto jleave;
      }
      break;
   }

   rv = TRU1;
jleave:
   return rv;
}
#endif /* DVLOR(1, 0) */

#ifdef su_HAVE_MT
# ifdef su__MUTEX_SPIN
#  define a_LOCK() su_atomic_busy_xchg_8(self->mtx_.lck, 1)
#  define a_UNLOCK() self->mtx_.lck = 0
# else
#  define a_LOCK() a_mutex_os_lock(self, self->mtx_.os_lck, tsp)
#  define a_UNLOCK() a_mutex_os_unlock(self, self->mtx_.os_lck, tsp)
# endif

void
su__mutex_lock(struct su_mutex *self, struct su_thread *tsp
      su__MUTEX_ARGS_DECL){
   DVLOR( UNUSED(file); UNUSED(line); )

   if(!(self->mtx_.flags & su_MUTEX_INIT))
      self = a_mutex_init(self);

   a_LOCK();

#if DVLOR(1, 0)
   if(!su__mutex_check(self, su__MUTEX_LOCK, tsp, file, line))
      goto jleave;
#endif

   if(self->mtx_.owner != tsp){
      boole xlock;

      if((xlock = (self->mtx_.owner != NIL))){
         DVL( self->mtx_.line = line; self->mtx_.file = file; )
         a_UNLOCK();
      }
      a_mutex_os_lock(self, self->mtx_.os, tsp);
      if(xlock)
         a_LOCK();

      self->owner = tsp;
   }

   ++self->mtx_.count;

DVLOR( jleave: )
   DVL( self->mtx_.line = line; self->mtx_.file = file; )
   a_UNLOCK();
}

boole
su__mutex_trylock(struct su_mutex *self, struct su_thread *tsp
      su__MUTEX_ARGS_DECL){
   boole rv;

   DVLOR( UNUSED(file); UNUSED(line); )

   if(!(self->mtx_.flags & su_MUTEX_INIT))
      self = a_mutex_init(self);

   a_LOCK();

#if DVLOR(1, 0)
   if(!(rv = su__mutex_check(self, su__MUTEX_TRYLOCK, tsp, file, line)))
      goto jleave;
#endif

   if((rv = (self->mtx_.owner == tsp)))
      ;
   else if((rv = (self->mtx_.owner == NIL))){
      a_mutex_os_lock(self, self->mtx_.os, tsp);
      self->owner = tsp;
   }else
      goto jleave;

   ++self->mtx_.count;

jleave:
   DVL( self->mtx_.line = line; self->mtx_.file = file; )
   a_UNLOCK();

   return rv;
}

void
su__mutex_unlock(struct su_mutex *self  su__MUTEX_ARGS_DECL){
   DVLOR( UNUSED(file); UNUSED(line); )

#if DVLOR(1, 0)
   if(!(self->mtx_.flags & su_MUTEX_INIT)){
      su__mutex_check(self, su__MUTEX_UNLOCK_NOLOCK, tsp, file, line);
      goto jleave;
   }
#endif

   a_LOCK();

#if DVLOR(1, 0)
   if(!su__mutex_check(self, su__MUTEX_UNLOCK, tsp, file, line))
      goto jleave;
#endif

   if(--self->mtx_.count == 0){
      self->mtx_.owner = NIL;
      a_mutex_os_unlock(self, self->mtx_.os, tsp);
   }

DVLOR( jleave: )
   DVL( self->mtx_.line = line; self->mtx_.file = file; )
   a_UNLOCK();
}

# undef a_LOCK
# undef a_UNLOCK
#endif /* su_HAVE_MT */

#include <su/y-mutex.h> /* 3. */

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_MUTEX
/* s-it-mode */
