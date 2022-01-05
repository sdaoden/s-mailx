/*@ Implementation of time.h, struct timespec.
 *
 * Copyright (c) 2021 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_time_spec
#define su_SOURCE
#define su_SOURCE_TIME_SPEC

#include "su/code.h"

su_USECASE_CONFIG_CHECKS(su_HAVE_CLOCK_GETTIME su_HAVE_GETTIMEOFDAY)

#include <time.h>

#include "su/time.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct su_timespec *
su_timespec_current(struct su_timespec *self){
   NYD2_IN;
   ASSERT(self);

   /* C99 */{
#ifdef su_HAVE_CLOCK_GETTIME
      struct timespec ts;

      if(sizeof(*self) == sizeof(ts) &&
            sizeof(self->ts_sec) == sizeof(ts.tv_sec))
         clock_gettime(CLOCK_REALTIME, R(struct timespec*,self));
      else{
         clock_gettime(CLOCK_REALTIME, &ts);
         self->ts_sec = S(s64,ts.tv_sec);
         self->ts_nano = S(sz,ts.tv_nsec);
      }

#elif defined su_HAVE_GETTIMEOFDAY
      struct timeval tv;

      gettimeofday(&tv, NIL);
      self->ts_sec = S(s64,tv.tv_sec);
      self->ts_nano = S(sz,tv.tv_usec) * 1000;

#else
      self->ts_sec = S(s64,time(NIL));
      self->ts_nano = 0;
#endif
   }

   NYD2_OU;
   return self;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_TIME_SPEC
/* s-it-mode */
