/*@ Implementation of time.h, utilities (non-portable, sleep).
 *
 * Copyright (c) 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_time_sleep
#define su_SOURCE
#define su_SOURCE_TIME_SLEEP

#include "su/code.h"

su_USECASE_CONFIG_CHECKS(
   su_HAVE_CLOCK_NANOSLEEP su_HAVE_NANOSLEEP
   su_HAVE_SLEEP
   )

#include <time.h>

#ifdef su_HAVE_SLEEP
# include <unistd.h>
#endif

#include "su/time.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

uz
su_time_msleep(uz millis, boole ignint){
   uz rv;
   NYD2_IN;

#if defined su_HAVE_CLOCK_NANOSLEEP || defined su_HAVE_NANOSLEEP
   /* C99 */{
      struct su_timespec ts, trem;

      ts.ts_sec = millis / su_TIMESPEC_SEC_MILLIS;
      ts.ts_nano = (millis %= su_TIMESPEC_SEC_MILLIS) *
            (su_TIMESPEC_SEC_NANOS / su_TIMESPEC_SEC_MILLIS);

      for(;;){
         struct su_timespec *tsp;
         int i;

         if((i = su_time_nsleep(&ts, tsp = &trem)) == su_ERR_NONE){
            rv = 0;
            break;
         }else if(i != su_ERR_INTR)
            tsp = &ts;
         else if(ignint){
            ts = trem;
            continue;
         }

         rv = ((tsp->ts_sec * su_TIMESPEC_SEC_MILLIS) +
                  (tsp->ts_nano /
                   (su_TIMESPEC_SEC_NANOS / su_TIMESPEC_SEC_MILLIS)));
         break;
      }
   }

#elif defined su_HAVE_SLEEP
   if((millis /= su_TIMESPEC_SEC_MILLIS) == 0)
      millis = 1;

   while((rv = sleep(S(ui,millis))) != 0 && ignint)
      millis = rv;

#else
# error Configuration should have detected a function for sleeping.
#endif

   NYD2_OU;
   return rv;
}

s32
su_time_nsleep(struct su_timespec const *dur, struct su_timespec *rem_or_nil){
   s32 rv;
   NYD2_IN;

   ASSERT_NYD_EXEC(dur != NIL, rv = su_ERR_INVAL);

   if(dur->ts_sec < 0 ||
         dur->ts_nano < 0 || dur->ts_nano >= su_TIMESPEC_SEC_NANOS){
      rv = su_ERR_INVAL;
      goto jleave;
   }

   /* C99 */{
#if defined su_HAVE_CLOCK_NANOSLEEP || defined su_HAVE_NANOSLEEP
      struct timespec ts, tsr;

      ts.tv_sec = dur->ts_sec;
      ts.tv_nsec = dur->ts_nano;

      rv =
# ifdef su_HAVE_CLOCK_NANOSLEEP
            clock_nanosleep
# else
            nanosleep
# endif
            (

# ifdef su_HAVE_CLOCK_NANOSLEEP
#  if defined _POSIX_MONOTONIC_CLOCK && _POSIX_MONOTONIC_CLOCK +0 > 0
            CLOCK_MONOTONIC
#  else
            CLOCK_REALTIME
#  endif
            , 0,
# endif
            &ts, &tsr);

      if(rv != 0){
         switch((rv = su_err_no_by_errno())){
         case su_ERR_INTR:
            if(rem_or_nil != NIL){
               rem_or_nil->ts_sec = tsr.tv_sec;
               rem_or_nil->ts_nano = tsr.tv_nsec;
            }
            break;
         case su_ERR_INVAL:
            break;
         default:
            rv = su_ERR_INVAL;
            break;
         }
      }

#else /* su_HAVE_CLOCK_NANOSLEEP || su_HAVE_NANOSLEEP */
      uz millis;

      if(dur->ts_sec >= UZ_MAX / su_TIMESPEC_SEC_MILLIS){
         rv = su_ERR_INVAL;
         goto jleave;
      }

      millis = S(uz,dur->ts_sec) * su_TIMESPEC_SEC_MILLIS;
      millis += S(uz,dur->ts_nano) /
            (su_TIMESPEC_SEC_NANOS / su_TIMESPEC_SEC_MILLIS);

      millis = su_time_msleep(millis, FAL0);

      if(millis == 0)
         rv = su_ERR_NONE;
      else{
         if(rem_or_nil != NIL){
            rem_or_nil->ts_sec = millis / su_TIMESPEC_SEC_MILLIS;
            rem_or_nil->ts_nano = (millis %= su_TIMESPEC_SEC_MILLIS) *
                  (su_TIMESPEC_SEC_NANOS / su_TIMESPEC_SEC_MILLIS);
         }
         rv = su_ERR_INTR;
      }
#endif /* !(su_HAVE_CLOCK_NANOSLEEP || su_HAVE_NANOSLEEP) */
   }

jleave:
   NYD2_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_TIME_SLEEP
/* s-it-mode */
