/*@ Implementation of time.h, utilities (portable, non-sleep).
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
#define su_FILE su_time_utils
#define su_SOURCE
#define su_SOURCE_TIME_UTILS

#include "su/code.h"

#include "su/time.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

char const su_time_weekday_names_abbrev[su_TIME_WEEKDAY_SATURDAY +1
      ][su_TIME_WEEKDAY_NAMES_ABBREV_LEN +1] = {
   "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

char const su_time_month_names_abbrev[su_TIME_MONTH_DECEMBER +1
      ][su_TIME_MONTH_NAMES_ABBREV_LEN +1] = {
   "Jan", "Feb", "Mar", "Apr", "May", "Jun",
   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

boole
su_time_epoch_to_gregor(s64 epsec, u32 *yp, u32 *mp, u32 *dp,
      u32 *hourp_or_nil, u32 *minp_or_nil, u32 *secp_or_nil){
   u32 y, m, d, hour, min, sec;
   boole rv;
   NYD2_IN;

   ASSERT_NYD_EXEC(yp != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(mp != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(dp != NIL, rv = FAL0);

   if(epsec >= 0 && epsec <= su_TIME_EPOCH_MAX){
      sec = epsec % su_TIME_MIN_SECS;
            epsec /= su_TIME_MIN_SECS;
      min = epsec % su_TIME_HOUR_MINS;
            epsec /= su_TIME_HOUR_MINS;
      hour = epsec % su_TIME_DAY_HOURS;
            epsec /= su_TIME_DAY_HOURS;

      epsec += su_TIME_JDN_EPOCH;
      su_time_jdn_to_gregor(epsec, &y, &m, &d);
      rv = TRU1;
   }else{
      rv = FAL0;
      y = m = d = hour = min = sec = 0;
   }

   *yp = y;
   *mp = m;
   *dp = d;
   if(hourp_or_nil != NIL)
      *hourp_or_nil = hour;
   if(minp_or_nil != NIL)
      *minp_or_nil = min;
   if(secp_or_nil != NIL)
      *secp_or_nil = sec;

   NYD2_OU;
   return rv;
}

s64
su_time_gregor_to_epoch(u32 year, u32 month, u32 day,
      u32 hour, u32 min, u32 sec){
   uz jdn;
   s64 rv;
   NYD2_IN;

   /* XXX These tests are pretty superficial, which is documented! */
   if(UCMP(32, sec, >/*XXX leap=*/, su_TIME_MIN_SECS) ||
         UCMP(32, min, >=, su_TIME_HOUR_MINS) ||
         UCMP(32, hour, >=, su_TIME_DAY_HOURS) ||
         /*day < 1 ||*/ day > 31 ||
         /*month < 1 ||*/ month > 12 ||
         year < 1970)
      rv = -1;
   else{
      jdn = su_time_gregor_to_jdn(year, month, day);
      jdn -= su_TIME_JDN_EPOCH;

      rv = sec;
      rv += min * su_TIME_MIN_SECS;
      rv += hour * su_TIME_HOUR_SECS;

      rv += S(u64,jdn) * su_TIME_DAY_SECS;
   }

   NYD2_OU;
   return rv;
}

uz
su_time_gregor_to_jdn(u32 y, u32 m, u32 d){
   /* Algorithm is taken from Communications of the ACM, Vol 6, No 8.
    * (via third hand, plus adjustments).
    * This algorithm is supposed to work for all dates in between 1582-10-15
    * (0001-01-01 but that not Gregorian) and 65535-12-31 */
   uz jdn;
   NYD2_IN;

   if(UNLIKELY(y == 0))
      y = 1;
   if(UNLIKELY(m == 0))
      m = 1;
   if(UNLIKELY(d == 0))
      d = 1;

   if(m > 2)
      m -= 3;
   else{
      m += 9;
      --y;
   }
   jdn = y;
   jdn /= 100;
   y -= 100 * jdn;
   y *= 1461;
   y >>= 2;
   jdn *= 146097;
   jdn >>= 2;
   jdn += y;
   jdn += d;
   jdn += 1721119;
   m *= 153;
   m += 2;
   m /= 5;
   jdn += m;

   NYD2_OU;
   return jdn;
}

void
su_time_jdn_to_gregor(uz jdn, u32 *yp, u32 *mp, u32 *dp){
   /* Algorithm is taken from Communications of the ACM, Vol 6, No 8.
    * (via third hand, plus adjustments) */
   uz y, x;
   NYD2_IN;

   ASSERT_NYD(yp != NIL);
   ASSERT_NYD(mp != NIL);
   ASSERT_NYD(dp != NIL);

   jdn -= 1721119;
   jdn <<= 2;
   --jdn;
   y =   jdn / 146097;
         jdn %= 146097;
   jdn |= 3;
   y *= 100;
   y +=  jdn / 1461;
         jdn %= 1461;
   jdn += 4;
   jdn >>= 2;
   x = jdn;
   jdn <<= 2;
   jdn += x;
   jdn -= 3;
   x =   jdn / 153; /* x -> month */
         jdn %= 153;
   jdn += 5;
   jdn /= 5; /* jdn -> day */
   if(x < 10)
      x += 3;
   else{
      x -= 9;
      ++y;
   }

   *yp = S(u32,y & 0xFFFF);
   *mp = S(u32,x & 0xFF);
   *dp = S(u32,jdn & 0xFF);

   NYD2_OU;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_TIME_UTILS
/* s-it-mode */
