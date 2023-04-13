/*@ thread.c: generic version.
 *
 * Copyright (c) 2001 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su__THREAD_Y
# define su__THREAD_Y 1

# ifdef su__HAVE_SCHED_YIELD
#  include <sched.h>
# elif defined su__HAVE_PTHREAD_YIELD
#  include <pthread.h>
# else
#  include "su/time.h"
# endif

#elif su__THREAD_Y == 1
# undef su__THREAD_Y
# define su__THREAD_Y 2

#elif su__THREAD_Y == 2
# undef su__THREAD_Y
# define su__THREAD_Y 3

void
su_thread_yield(void){
# ifdef su__HAVE_SCHED_YIELD
	sched_yield(); /* We _are_ single threaded */
# elif defined su__HAVE_PTHREAD_YIELD
	pthread_yield();
# else
	struct su_timespec ts;

	STRUCT_ZERO(struct su_timespec, &ts);
	su_time_nsleep(&ts, NIL);
# endif
}

#else
# error .
#endif
/* s-itt-mode */
