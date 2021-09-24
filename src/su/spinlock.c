/*@ Implementation of spinlock.h.
 *@ NOTE: spinlock_create(): used within su_state_create()!
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
#define su_FILE su_spinlock
#define su_SOURCE
#define su_SOURCE_SPINLOCK

#include "su/code.h"

su_USECASE_MX_DISABLED
su_EMPTY_FILE()

#include "su/spinlock.h"
#ifdef su__SPINLOCK_DBG

#include "su/thread.h"

/*#include "su/spinlock.h"*/
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

boole
su__spinlock_check(struct su_spinlock *self, enum su__spinlock_xfn slf, up v){
   boole rv;

   if(v == 0)
      v = R(up,su_thread_self());

   rv = FAL0;

   switch(slf){
   case su__SPIN_DTOR:
      if(self->sl_lck != 0){
         su_log_write(su_LOG_ALERT,
            "su_spinlock_gut(%p=%s): still locked by %s\n",
            self, self->sl_name,
            su_thread_name(R(struct su_thread*,self->sl_lck)));
         goto jleave;
      }
      break;
   case su__SPIN_LOCK:
   case su__SPIN_TRYLOCK:
      if(self->sl_lck == v){
         su_log_write(su_LOG_ALERT,
            "su_spinlock_(try)?lock(%p=%s): already locked by %s\n",
            self, self->sl_name, su_thread_name(R(struct su_thread*,v)));
         goto jleave;
      }
      break;
   case su__SPIN_UNLOCK:
      if(self->sl_lck == 0){
         su_log_write(su_LOG_ALERT,
            "su_spinlock_unlock(%p=%s): not locked\n", self, self->sl_name);
         goto jleave;
      }
      break;
   }

   rv = TRU1;
jleave:
   return rv;
}

#include "su/code-ou.h"
#endif /* su__SPINLOCK_DBG */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_SPINLOCK
/* s-it-mode */
