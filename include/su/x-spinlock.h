/*@ spinlock.h: internals, generic (atomic.h based) version.
 *
 * Copyright (c) 2001 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
# error Please include spinlock.h instead
#elif !defined su__SPINLOCK_X
# define su__SPINLOCK_X 1

su_USECASE_MX_DISABLED

#elif su__SPINLOCK_X == 1
# undef su__SPINLOCK_X
# define su__SPINLOCK_X 2

# ifdef su__SPINLOCK_IS
INLINE void su__spinlock_lock(struct su_spinlock *self, up v){
   while(!su_atomic_cas_p(&self->sl_lck, 0, v))
      ;
}

INLINE boole su__spinlock_trylock(struct su_spinlock *self, up v){
   return su_atomic_cas_p(&self->sl_lck, 0, v);
}

INLINE void su__spinlock_unlock(struct su_spinlock *self) {self->sl_lck = 0;}
# endif /* su__SPINLOCK_IS */

#elif su__SPINLOCK_X == 2
# undef su__SPINLOCK_X
# define su__SPINLOCK_X 3

#else
# error .
#endif
/* s-it-mode */
