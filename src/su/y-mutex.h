/*@ mutex.h: internals, generic version.
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
#ifndef su__MUTEX_Y
# define su__MUTEX_Y 1

su_USECASE_MX_DISABLED

#elif su__MUTEX_Y == 1
# undef su__MUTEX_Y
# define su__MUTEX_Y 2
# ifdef su_HAVE_MT
#  error Dummy implementation.

s32
su__mutex_os_create(struct su_mutex *self, u32 estate){
   s32 rv;
   UNUSED(estate);

   rv = su_STATE_NONE;
   /* init self->mtx_.os */
#ifndef su__MUTEX_SPIN
   /* init self->mtx_.os_lck */
#endif
   return rv;
}

void
su__mutex_os_gut(struct su_mutex *self){
   UNUSED(self);
   /* destroy self->mtx_.os */
#ifndef su__MUTEX_SPIN
   /* destroy self->mtx_.os_lck */
#endif
}

SINLINE void
a_mutex_os_lock(struct su_mutex *self, u8 *osmtx, struct su_thread *tsp){
   UNUSED(self);
   UNUSED(osmtx);
   UNUSED(tsp);
}

SINLINE void
a_mutex_os_unlock(struct su_mutex *self, u8 *osmtx){
   UNUSED(self);
   UNUSED(osmtx);
}

# endif /* su_HAVE_MT */
#elif su__MUTEX_Y == 2
# undef su__MUTEX_Y
# define su__MUTEX_Y 3

#else
# error .
#endif
/* s-it-mode */
