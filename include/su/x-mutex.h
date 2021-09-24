/*@ mutex.h: internals, generic dummy version.
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
# error Please include mutex.h instead
#elif !defined su__MUTEX_X
# define su__MUTEX_X 1

su_USECASE_MX_DISABLED

#elif su__MUTEX_X == 1
# undef su__MUTEX_X
# define su__MUTEX_X 2
# ifndef su_HAVE_MT
#  define su__MUTEX_HAVE_FUNS

INLINE void
su__mutex_lock(struct su_mutex *self, struct su_thread *tsp
      su__MUTEX_ARGS_DECL){
   self->mtx_.lck = 1;

   DVL(
      if(!su__mutex_check(self, su__MUTEX_LOCK, tsp, file, line))
         goto jleave;
      self->mtx_.line = line;
      self->mtx_.file = file;
   )

   if(self->mtx_.count++ == 0)
      self->mtx_.owner = tsp;

DVL( jleave: )
   self->mtx_.lck = 0;
}

INLINE boole
su__mutex_trylock(struct su_mutex *self, struct su_thread *tsp
      su__MUTEX_ARGS_DECL){
   boole rv;

   self->mtx_.lck = 1;

   DVL(
      if(!(rv = su__mutex_check(self, su__MUTEX_TRYLOCK, tsp, file, line)))
         goto jleave;
      self->mtx_.line = line;
      self->mtx_.file = file;
   )

   if((rv = (self->mtx_.owner == tsp || self->mtx_.owner == NIL))){
      self->mtx_.owner = tsp;
      ++self->mtx_.count;
   }

DVL( jleave: )
   self->mtx_.lck = 0;
   return rv;
}

INLINE void
su__mutex_unlock(struct su_mutex *self  su__MUTEX_ARGS_DECL){
   self->mtx_.lck = 1;

   DVL(
      if(!su__mutex_check(self, su__MUTEX_UNLOCK, NIL, file, line))
         goto jleave;
      self->mtx_.line = line;
      self->mtx_.file = file;
   )

   if(--self->mtx_.count == 0)
      self->mtx_.owner = NIL;

DVL( jleave: )
   self->mtx_.lck = 0;
}

# endif /* !su_HAVE_MT */
#elif su__MUTEX_X == 2
# undef su__MUTEX_X
# define su__MUTEX_X 3

#else
# error .
#endif
/* s-it-mode */
