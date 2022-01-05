/*@ Time and date utilities.
 *@ TODO C timespec calculations, more constants
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
#ifndef su_TIME_H
#define su_TIME_H

/*!
 * \file
 * \ingroup TIME
 * \brief \r{TIME}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_timespec;

/* time {{{ */
/*!
 * \defgroup TIME Time and date utilities
 * \ingroup MISC
 * \brief Time and date utilities (\r{su/time.h})
 * @{
 */

/* timespec {{{ */
/*!
 * \defgroup TIMESPEC Time in seconds and nanoseconds
 * \ingroup TIME
 * \brief Time in seconds and nanoseconds (\r{su/time.h})
 * @{
 */

#define su_TIMESPEC_SEC_MILLIS 1000l /*!< \_ */
#define su_TIMESPEC_SEC_MICROS 1000000l /*!< \_ */
#define su_TIMESPEC_SEC_NANOS 1000000000l /*!< \_ */

/*! \_ */
struct su_timespec{
   s64 ts_sec; /*!< \_ */
   sz ts_nano; /*!< \_ */
};

/*! Update \SELF to the current realtime clock, and return it again. */
EXPORT struct su_timespec *su_timespec_current(struct su_timespec *self);

/*! \_ */
INLINE boole su_timespec_is_valid(struct su_timespec const *self){
   ASSERT_RET(self != NIL, FAL0);
   return (self->ts_sec >= 0 &&
         self->ts_nano >= 0 && self->ts_nano < su_TIMESPEC_SEC_NANOS);
}

/*! \copydoc{su_cmp_fun()} */
INLINE sz su_timespec_cmp(struct su_timespec const *self,
      struct su_timespec const *tp){
   s64 x;
   ASSERT_RET(self != NIL, -(tp != NIL));
   ASSERT_RET(tp != NIL, 1);
   x = self->ts_sec - tp->ts_sec;
   if(x == 0)
      x = self->ts_nano - tp->ts_nano;
   return (x == 0 ? 0 : x < 0 ? -1 : 1);
}

/*! Is equal? */
INLINE boole su_timespec_is_EQ(struct su_timespec const *self,
      struct su_timespec const *tp){
   ASSERT_RET(self != NIL, (tp == NIL));
   ASSERT_RET(tp != NIL, FAL0);
   return (self->ts_sec == tp->ts_sec && self->ts_nano == tp->ts_nano);
}

/*! Is not equal? */
INLINE boole su_timespec_is_NE(struct su_timespec const *self,
      struct su_timespec const *tp){
   ASSERT_RET(self != NIL, (tp != NIL));
   ASSERT_RET(tp != NIL, FAL0);
   return (self->ts_sec != tp->ts_sec || self->ts_nano != tp->ts_nano);
}

/*! Is less-than? */
INLINE boole su_timespec_is_LT(struct su_timespec const *self,
      struct su_timespec const *tp){
   s64 x;
   ASSERT_RET(self != NIL, (tp != NIL));
   ASSERT_RET(tp != NIL, FAL0);
   x = self->ts_sec - tp->ts_sec;
   return (x < 0 || (x == 0 && self->ts_nano < tp->ts_nano));
}

/*! It less-than-or-equal?  */
INLINE boole su_timespec_is_LE(struct su_timespec const *self,
      struct su_timespec const *tp){
   s64 x;
   ASSERT_RET(self != NIL, (tp != NIL));
   ASSERT_RET(tp != NIL, FAL0);
   x = self->ts_sec - tp->ts_sec;
   return (x < 0 || (x == 0 && self->ts_nano <= tp->ts_nano));
}

/*! Is greater-than-or-equal? */
INLINE boole su_timespec_is_GE(struct su_timespec const *self,
      struct su_timespec const *tp){
   s64 x;
   ASSERT_RET(self != NIL, (tp == NIL));
   ASSERT_RET(tp != NIL, TRU1);
   x = self->ts_sec - tp->ts_sec;
   return (x > 0 || (x == 0 && self->ts_nano >= tp->ts_nano));
}

/*! Is greater-than? */
INLINE boole su_timespec_is_GT(struct su_timespec const *self,
      struct su_timespec const *tp){
   s64 x;
   ASSERT_RET(self != NIL, FAL0);
   ASSERT_RET(tp != NIL, TRU1);
   x = self->ts_sec - tp->ts_sec;
   return (x > 0 || (x == 0 && self->ts_nano > tp->ts_nano));
}

/*! Add \a{tp} to \SELF.
 * This does not prevent integer wraparounds. */
INLINE struct su_timespec *su_timespec_add(struct su_timespec *self,
      struct su_timespec const *tp){
   s64 sec;
   sz nano;
   ASSERT_RET(self != NIL, self);
   ASSERT_RET(tp != NIL, self);

   sec = self->ts_sec;
   nano = self->ts_nano;
   sec += tp->ts_sec;
   nano += tp->ts_nano;
   while(nano >= su_TIMESPEC_SEC_NANOS){
      ++sec;
      nano -= su_TIMESPEC_SEC_NANOS;
   }
   self->ts_sec = sec;
   self->ts_nano = nano;
   return self;
}

/*! Subtract \a{tp} to \SELF.
 * This does not prevent integer wraparounds. */
INLINE struct su_timespec *su_timespec_sub(struct su_timespec *self,
      struct su_timespec const *tp){
   s64 sec;
   sz nano;
   ASSERT_RET(self != NIL, self);
   ASSERT_RET(tp != NIL, self);

   sec = self->ts_sec;
   nano = self->ts_nano;
   sec -= tp->ts_sec;
   nano -= tp->ts_nano;
   while(nano < 0){
      --sec;
      nano += su_TIMESPEC_SEC_NANOS;
   }
   self->ts_sec = sec;
   self->ts_nano = nano;
   return self;
}
/*! @} *//* }}} */

/* utils {{{ */
/*! Maximum seconds (inclusive) since the UNIX epoch (1970-01-01T00:00:00)
 * representable by our algorithm(s), which should work until
 * 65535-12-31T23:59:59.
 * \remarks{Signed 64-bit constant.} */
#define su_TIME_EPOCH_MAX su_S64_C(0x1D30BE2E1FF)

#define su_TIME_MIN_SECS 60u /*!< \_ */

#define su_TIME_HOUR_MINS 60u /*!< \_ */
#define su_TIME_HOUR_SECS (su_TIME_MIN_SECS * su_TIME_HOUR_MINS) /*!< \_ */

#define su_TIME_DAY_HOURS 24u /*!< \_ */
#define su_TIME_DAY_MINS (su_TIME_HOUR_MINS * su_TIME_DAY_HOURS) /*!< \_ */
#define su_TIME_DAY_SECS (su_TIME_HOUR_SECS * su_TIME_DAY_HOURS) /*!< \_ */

#define su_TIME_YEAR_GREGORIAN 1752u /*!< \_ */
#define su_TIME_YEAR_DAYS 365u /*!< \_ */

#define su_TIME_JDN_EPOCH 2440588ul /*!< Julian-Day-Number of UNIX epoch. */

/*! \_ */
enum su_time_weekdays{
   su_TIME_WEEKDAY_SUNDAY, /*!< \_ */
   su_TIME_WEEKDAY_MONDAY, /*!< \_ */
   su_TIME_WEEKDAY_TUESDAY, /*!< \_ */
   su_TIME_WEEKDAY_WEDNESDAY, /*!< \_ */
   su_TIME_WEEKDAY_THURSDAY, /*!< \_ */
   su_TIME_WEEKDAY_FRIDAY, /*!< \_ */
   su_TIME_WEEKDAY_SATURDAY /*!< \_ */
};
/*! \_ */
#define su_TIME_WEEKDAY_IS_VALID(X) (su_S(uz,X) <= su_TIME_WEEKDAY_SATURDAY)

/*! \_ */
enum su_time_months{
   su_TIME_MONTH_JANUARY, /*!< \_ */
   su_TIME_MONTH_FEBRUARY, /*!< \_ */
   su_TIME_MONTH_MARCH, /*!< \_ */
   su_TIME_MONTH_APRIL, /*!< \_ */
   su_TIME_MONTH_MAY, /*!< \_ */
   su_TIME_MONTH_JUNE, /*!< \_ */
   su_TIME_MONTH_JULY, /*!< \_ */
   su_TIME_MONTH_AUGUST, /*!< \_ */
   su_TIME_MONTH_SEPTEMBER, /*!< \_ */
   su_TIME_MONTH_OCTOBER, /*!< \_ */
   su_TIME_MONTH_NOVEMBER, /*!< \_ */
   su_TIME_MONTH_DECEMBER /*!< \_ */
};
/*! \_ */
#define su_TIME_MONTH_IS_VALID(X) (su_S(uz,X) <= su_TIME_MONTH_DECEMBER)

#define su_TIME_WEEKDAY_NAMES_ABBREV_LEN 3u /*!< \_ */
EXPORT_DATA char const su_time_weekday_names_abbrev[su_TIME_WEEKDAY_SATURDAY +1
   ][su_TIME_WEEKDAY_NAMES_ABBREV_LEN +1]; /*!< \_ */

#define su_TIME_MONTH_NAMES_ABBREV_LEN 3u /*!< \_ */
EXPORT_DATA char const su_time_month_names_abbrev[su_TIME_MONTH_DECEMBER +1
   ][su_TIME_MONTH_NAMES_ABBREV_LEN +1]; /*!< \_ */

/*! Returns \FAL0 for all times before the UNIX epoch, or after
 * \r{su_TIME_EPOCH_MAX}, in which case (set) arguments will be assigned 0.
 * Assumes UTC, and does not know about leap seconds etc.
 * \ASSERT{\FAL0, any of the required pointers is \NIL} */
EXPORT boole su_time_epoch_to_gregor(s64 epsec, u32 *yp, u32 *mp, u32 *dp,
      u32 *hourp_or_nil, u32 *minp_or_nil, u32 *secp_or_nil);

/*! Returns -1 for all times before the UNIX epoch (1970-01-01T00:00:00),
 * as well as for invalid arguments (XXX these checks, however, are only
 * superficial, they do not correlate days to months, nor months to years,
 * and (thus) do know nothing about leap years or months).
 * A 0 \a{m} or \a{d} are normalized to 1.
 * \remarks{One might also want to check the return value against
 * \c{if(sizeof(time) == 4 && rval <= S32_MAX) ..}.} */
EXPORT s64 su_time_gregor_to_epoch(u32 y, u32 m, u32 d,
      u32 hour, u32 min, u32 sec);

/*! Convert Gregorian date \a{y}ear, \a{m}onth and \a{d}ay to the according
 * Julian-Day-Number.
 * The algorithm is supposed to work for all dates in between 1582-10-15
 * (0001-01-01 but that not Gregorian) and 65535-12-31.
 * It turns any 0 argument into a 1, and is bizarrely forgiving otherwise. */
EXPORT uz su_time_gregor_to_jdn(u32 y, u32 m, u32 d);

/*! Convert the Julian-Day-Number \a{jdn} to a Gregorian date and store the
 * result as \a{*yp}ear, \a{*mp}onth and \a{*dp}ay.
 * \ASSERT{void, any of the pointers is \NIL}
 * \remarks{The algorithm is supposed to work for all dates in between
 * 1582-10-15 (0001-01-01 but that not Gregorian) and 65535-12-31.} */
EXPORT void su_time_jdn_to_gregor(uz jdn, u32 *yp, u32 *mp, u32 *dp);

/*! Is \a{y} a leap year?
 * \remarks{Assumes Julian calendar before \r{su_TIME_YEAR_GREGORIAN}.} */
INLINE boole su_time_year_is_leap(u32 y){
   boole rv = ((y & 3) == 0);
   if(y >= su_TIME_YEAR_GREGORIAN)
      rv = ((rv && y % 100 != 0) || y % 400 == 0);
   return rv;
}

/*! Returns 0 if fully slept, or the number of \a{millis} left to sleep if
 * \a{ignint} is true and we were interrupted.
 * The actually available real resolution is operating system dependent, and
 * could even be a second or less.
 * \remarks{In the worst case the implementation will be \c{SIGALRM} based.} */
EXPORT uz su_time_msleep(uz millis, boole ignint);

/*! High-resolution sleep.
 * Sleep for the time given in \a{dur} and return \ERR{NONE} if fully slept.
 * \ERR{INVAL} is returned if \a{dur} is invalid (negative members, more than
 * \r{su_TIMESPEC_SEC_NANOS} nanoseconds).
 * \ERR{INTR} is returned and a given \a{rem_or_nil} is filled in with the
 * remaining time to sleep if an interruption occurred.
 * \remarks{In the unlikely event of missing operating system support this
 * effectively equals \r{su_time_msleep()}.
 * On the other hand improved interfaces like \c{clock_nanosleep(2)} are used
 * if available.} */
EXPORT s32 su_time_nsleep(struct su_timespec const *dur,
      struct su_timespec *rem_or_nil);
/* }}} */
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class time;
//class time::spec;

/* time {{{ */
/*!
 * \ingroup TIME
 * C++ variant of \r{TIME} (\r{su/time.h})
 */
class time{
   // friend of time::spec
   su_CLASS_NO_COPY(time);
public:
   class spec;

   /* spec {{{ */
   /*! \copydoc{TIMESPEC} */
   class spec : private su_timespec{
      friend class path;
      friend class time;
   public:
      /*! \copydoc{su_TIMESPEC_SEC_MILLIS} */
      static sz const sec_millis = su_TIMESPEC_SEC_MILLIS;

      /*! \copydoc{su_TIMESPEC_SEC_MICROS} */
      static sz const sec_micros = su_TIMESPEC_SEC_MICROS;

      /*! \copydoc{su_TIMESPEC_SEC_NANOS} */
      static sz const sec_nanos = su_TIMESPEC_SEC_NANOS;

      /*! \_ */
      spec(void) {}

      /*! \_ */
      spec(spec const &t) {ts_sec = t.ts_sec; ts_nano = t.ts_nano;}

      /*! \_ */
      ~spec(void) {}

      /*! \_ */
      spec &assign(spec const &t) {
         ts_sec = t.ts_sec;
         ts_nano = t.ts_nano;
         return *this;
      }

      /*! \_ */
      spec &operator=(spec const &t) {return assign(t);}

      /*! \copydoc{su_timespec::ts_sec} */
      s64 sec(void) const {return ts_sec;}

      /*! \copydoc{su_timespec::ts_sec} */
      spec &sec(s64 sec) {ts_sec = sec; return *this;}

      /*! \copydoc{su_timespec::ts_nano} */
      sz nano(void) const {return ts_nano;}

      /*! \copydoc{su_timespec::ts_sec} */
      spec &nano(sz nano) {ts_nano = nano; return *this;}

      /*! \copydoc{su_timespec_current()} */
      spec &current(void) {SELFTHIS_RET(su_timespec_current(this));}

      /*! \copydoc{su_timespec_is_valid()} */
      boole is_valid(void) const {return su_timespec_is_valid(this);}

      /*! \copydoc{su_timespec_cmp()} */
      sz cmp(spec const &t) const {return su_timespec_cmp(this, &t);}

      /*! \copydoc{su_timespec_is_EQ()} */
      boole operator==(spec const &t) const{
         return su_timespec_is_EQ(this, &t);
      }

      /*! \copydoc{su_timespec_is_NE()} */
      boole operator!=(spec const &t) const{
         return su_timespec_is_NE(this, &t);
      }

      /*! \copydoc{su_timespec_is_LT()} */
      boole operator<(spec const &t) const{
         return su_timespec_is_LT(this, &t);
      }

      /*! \copydoc{su_timespec_is_LE()} */
      boole operator<=(spec const &t) const{
         return su_timespec_is_LE(this, &t);
      }

      /*! \copydoc{su_timespec_is_GE()} */
      boole operator>=(spec const &t) const{
         return su_timespec_is_GE(this, &t);
      }

      /*! \copydoc{su_timespec_is_GT()} */
      boole operator>(spec const &t) const{
         return su_timespec_is_GT(this, &t);
      }

      /*! \copydoc{su_timespec_add()} */
      spec &add(spec const &t) {SELFTHIS_RET(su_timespec_add(this, &t));}

      /*! \copydoc{su_timespec_add()} */
      spec &operator+=(spec const &t) {return add(t);}

      /*! \copydoc{su_timespec_sub()} */
      spec &sub(spec const &t) {SELFTHIS_RET(su_timespec_sub(this, &t));}

      /*! \copydoc{su_timespec_add()} */
      spec &operator-=(spec const &t) {return sub(t);}
   };
   /* }}} */

   /* utils {{{ */
   /*! \copydoc{su_TIME_EPOCH_MAX} */
   static s64 const epoch_max = su_TIME_EPOCH_MAX;

   /*! \copydoc{su_TIME_MIN_SECS} */
   static uz const min_secs = su_TIME_MIN_SECS;

   /*! \copydoc{su_TIME_HOUR_MINS} */
   static uz const hour_mins = su_TIME_HOUR_MINS;
   /*! \copydoc{su_TIME_HOUR_SECS} */
   static uz const hour_secs = su_TIME_HOUR_SECS;

   /*! \copydoc{su_TIME_DAY_HOURS} */
   static uz const day_hours = su_TIME_DAY_HOURS;
   /*! \copydoc{su_TIME_DAY_SECS} */
   static uz const day_secs = su_TIME_DAY_SECS;

   /*! \copydoc{su_TIME_YEAR_GREGORIAN} */
   static uz const year_gregorian = su_TIME_YEAR_GREGORIAN;

   /*! \copydoc{su_TIME_YEAR_DAYS} */
   static uz const year_days = su_TIME_YEAR_DAYS;

   /*! \copydoc{su_TIME_JDN_EPOCH} */
   static uz const jdn_epoch = su_TIME_JDN_EPOCH;

   /*! \copydoc{su_time_weekdays} */
   enum weekdays{
      weekday_sunday = su_TIME_WEEKDAY_SUNDAY, /*!< \_ */
      weekday_monday = su_TIME_WEEKDAY_MONDAY, /*!< \_ */
      weekday_tuesday = su_TIME_WEEKDAY_TUESDAY, /*!< \_ */
      weekday_wednesday = su_TIME_WEEKDAY_WEDNESDAY, /*!< \_ */
      weekday_thursday = su_TIME_WEEKDAY_THURSDAY, /*!< \_ */
      weekday_friday = su_TIME_WEEKDAY_FRIDAY, /*!< \_ */
      weekday_saturday = su_TIME_WEEKDAY_SATURDAY /*!< \_ */
   };

   /*! \copydoc{su_time_months} */
   enum months{
      month_january = su_TIME_MONTH_JANUARY, /*!< \_ */
      month_february = su_TIME_MONTH_FEBRUARY, /*!< \_ */
      month_march = su_TIME_MONTH_MARCH, /*!< \_ */
      month_april = su_TIME_MONTH_APRIL, /*!< \_ */
      month_may = su_TIME_MONTH_MAY, /*!< \_ */
      month_june = su_TIME_MONTH_JUNE, /*!< \_ */
      month_july = su_TIME_MONTH_JULY, /*!< \_ */
      month_august = su_TIME_MONTH_AUGUST, /*!< \_ */
      month_september = su_TIME_MONTH_SEPTEMBER, /*!< \_ */
      month_october = su_TIME_MONTH_OCTOBER, /*!< \_ */
      month_november = su_TIME_MONTH_NOVEMBER, /*!< \_ */
      month_december = su_TIME_MONTH_DECEMBER /*!< \_ */
   };

   /*! \copydoc{su_TIME_WEEKDAY_IS_VALID()} */
   static boole weekday_is_valid(uz m) {return su_TIME_WEEKDAY_IS_VALID(m);}

   /*! \copydoc{su_time_weekday_names_abbrev}
    * \ASSERT{\NIL, \a m is not valid} */
   static char const *weekday_name_abbrev(uz m){
      ASSERT_RET(weekday_is_valid(m), NIL);
      return su_time_weekday_names_abbrev[m];
   }

   /*! \copydoc{su_TIME_MONTH_IS_VALID()} */
   static boole month_is_valid(uz m) {return su_TIME_MONTH_IS_VALID(m);}

   /*! \copydoc{su_time_month_names_abbrev}
    * \ASSERT{\NIL, \a m is not valid} */
   static char const *month_name_abbrev(uz m){
      ASSERT_RET(month_is_valid(m), NIL);
      return su_time_month_names_abbrev[m];
   }

   /*! \copydoc{su_time_epoch_to_gregor()} */
   static boole epoch_to_gregor(s64 epsec, u32 *yp, u32 *mp, u32 *dp,
         u32 *hourp_or_nil=NIL, u32 *minp_or_nil=NIL, u32 *secp_or_nil=NIL){
      ASSERT_RET(yp != NIL, FAL0);
      ASSERT_RET(mp != NIL, FAL0);
      ASSERT_RET(dp != NIL, FAL0);
      return su_time_epoch_to_gregor(epsec, yp, mp, dp,
         hourp_or_nil, minp_or_nil, secp_or_nil);
   }

   /*! \copydoc{su_time_gregor_to_epoch()} */
   static s64 gregor_to_epoch(u32 y, u32 m, u32 d,
         u32 hour=0, u32 min=0, u32 sec=0){
      return su_time_gregor_to_epoch(y, m, d, hour, min, sec);
   }

   /*! \copydoc{su_time_gregor_to_jdn()} */
   static uz gregor_to_jdn(u32 y, u32 m, u32 d){
      return su_time_gregor_to_jdn(y, m, d);
   }

   /*! \copydoc{su_time_jdn_to_gregor()} */
   static void jdn_to_gregor(uz jdn, u32 *yp, u32 *mp, u32 *dp){
      ASSERT_RET_VOID(yp != NIL);
      ASSERT_RET_VOID(mp != NIL);
      ASSERT_RET_VOID(dp != NIL);
      su_time_jdn_to_gregor(jdn, yp, mp, dp);
   }

   /*! \copydoc{su_time_year_is_leap()} */
   static boole year_is_leap(u32 y) {return su_time_year_is_leap(y);}

   /*! \copydoc{su_time_msleep()} */
   static uz msleep(uz millis, boole ignint=TRU1){
      return su_time_msleep(millis, ignint);
   }

   /*! \copydoc{su_time_nsleep()} */
   static s32 nsleep(spec const *dur, spec *rem_or_nil=NIL){
      return su_time_nsleep(dur, rem_or_nil);
   }
   /* }}} */
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_TIME_H */
/* s-it-mode */
