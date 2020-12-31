/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of time.h.
 *
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE time
#define mx_SOURCE
#define mx_SOURCE_TIME

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <stdio.h> /* TODO fmtcodec*/
#include <time.h>

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/time.h>

#include "mx/time.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct mx_time_current mx_time_current;

struct mx_time_current *
mx_time_current_update(struct mx_time_current *tc_or_nil, boole full_update){
	NYD2_IN;

	if(tc_or_nil == NIL)
		tc_or_nil = &mx_time_current;

	tc_or_nil->tc_time = mx_time_now(TRU1)->ts_sec;

	if(full_update){
		char *cp;
		struct tm *tmp;
		time_t t;

		if(sizeof(t) == 4 && tc_or_nil->tc_time >= S32_MAX)
			t = 0;
		else
			t = S(time_t,tc_or_nil->tc_time);
jredo:
		if((tmp = gmtime(&t)) == NIL){
			t = 0;
			goto jredo;
		}
		su_mem_copy(&tc_or_nil->tc_gm, tmp, sizeof tc_or_nil->tc_gm);

		if((tmp = localtime(&t)) == NIL){
			t = 0;
			goto jredo;
		}
		su_mem_copy(&tc_or_nil->tc_local, tmp, sizeof tc_or_nil->tc_local);

		cp = su_cs_pcopy(tc_or_nil->tc_ctime, mx_time_ctime(tc_or_nil->tc_time, tmp));
		*cp++ = '\n';
		*cp = '\0';
		ASSERT(P2UZ(++cp - tc_or_nil->tc_ctime) < sizeof(tc_or_nil->tc_ctime));
	}

	NYD2_OU;
	return tc_or_nil;
}

struct su_timespec const *
mx_time_now(boole force_update){ /* TODO event loop update IF cmd requests! */
	static struct su_timespec ts_now;
	NYD2_IN;

	if(UNLIKELY(su_state_has(su_STATE_REPRODUCIBLE))){
		/* Guaranteed 32-bit posnum TODO SOURCE_DATE_EPOCH should be 64-bit! */
		(void)su_idec_s64_cp(&ts_now.ts_sec, ok_vlook(SOURCE_DATE_EPOCH), 0,NIL);
		ts_now.ts_nano = 0;
	}else if(force_update || ts_now.ts_sec == 0)
		su_timespec_current(&ts_now);

	/* Just in case.. */
	if(UNLIKELY(ts_now.ts_sec < 0))
		ts_now.ts_sec = 0;

	NYD2_OU;
	return &ts_now;
}

s32
mx_time_tzdiff(s64 secsepoch, struct tm const *utcp_or_nil, struct tm const *localp_or_nil){
	struct tm tmbuf[2], *tmx;
	time_t t;
	s32 rv;
	NYD2_IN;
	UNUSED(utcp_or_nil);

	if(secsepoch < 0)
		secsepoch = 0;
	else if(utcp_or_nil == NIL || localp_or_nil == NIL){
		if(sizeof(t) == 4 && secsepoch >= S32_MAX)
			secsepoch = 0;
	}

	rv = 0;

	if(localp_or_nil == NIL){
		t = S(time_t,secsepoch);
		if((tmx = localtime(&t)) == NIL)
			goto jleave;
		tmbuf[0] = *tmx;
		localp_or_nil = &tmbuf[0];
	}

#ifdef mx_HAVE_TM_GMTOFF
	rv = localp_or_nil->tm_gmtoff;

#else
	if(utcp_or_nil == NIL){
		t = S(time_t,secsepoch);
		if((tmx = gmtime(&t)) == NIL)
			goto jleave;
		tmbuf[1] = *tmx;
		utcp_or_nil = &tmbuf[1];
	}

	rv = ((((localp_or_nil->tm_hour - utcp_or_nil->tm_hour) * 60) +
			(localp_or_nil->tm_min - utcp_or_nil->tm_min)) * 60) +
			(localp_or_nil->tm_sec - utcp_or_nil->tm_sec);

	if((t = (localp_or_nil->tm_yday - utcp_or_nil->tm_yday)) != 0)
		rv += (t == 1) ? S(s32,su_TIME_DAY_SECS) : -S(s32,su_TIME_DAY_SECS);
#endif

jleave:
	NYD2_OU;
	return rv;
}

char *
mx_time_ctime(s64 secsepoch, struct tm const *localtime_or_nil){/* TODO err*/
	/* Problem is that secsepoch may be invalid for representation of ctime(3),
	 * which indeed is asctime(localtime(t)); musl libc says for asctime(3):
	 *		ISO C requires us to use the above format string,
	 *		even if it will not fit in the buffer. Thus asctime_r
	 *		is _supposed_ to crash if the fields in tm are too large.
	 *		We follow this behavior and crash "gracefully" to warn
	 *		application developers that they may not be so lucky
	 *		on other implementations (e.g. stack smashing..).
	 * So we need to do it on our own or the libc may kill us */
	static char buf[32]; /* TODO static buffer (-> datetime_to_format()) */

	s32 y, md, th, tm, ts;
	char const *wdn, *mn;
	struct tm const *tmp;
	NYD_IN;
	LCTA(FIELD_SIZEOF(struct mx_time_current,tc_ctime) == sizeof(buf), "Buffers should have equal size");

	if((tmp = localtime_or_nil) == NIL){
		time_t t;

		if(secsepoch < 0)
			t = 0;
		else if(sizeof(t) == 4 && secsepoch >= S32_MAX)
			t = 0;
		else
			t = S(time_t,secsepoch);
jredo:
		if((tmp = localtime(&t)) == NIL){
			/* TODO error log */
			t = 0;
			goto jredo;
		}
	}

	if(UNLIKELY((y = tmp->tm_year) < 0 || y >= 9999/*S32_MAX*/ - 1900)){
		y = 1970;
		wdn = su_time_weekday_names_abbrev[su_TIME_WEEKDAY_THURSDAY];
		mn = su_time_month_names_abbrev[su_TIME_MONTH_JANUARY];
		md = 1;
		th = tm = ts = 0;
	}else{
		y += 1900;
		wdn = su_TIME_WEEKDAY_IS_VALID(tmp->tm_wday) ? su_time_weekday_names_abbrev[tmp->tm_wday] : n_qm;
		mn = su_TIME_MONTH_IS_VALID(tmp->tm_mon) ? su_time_month_names_abbrev[tmp->tm_mon] : n_qm;

		if((md = tmp->tm_mday) < 1 || md > 31)
			md = 1;

		if((th = tmp->tm_hour) < 0 || th > 23)
			th = 0;
		if((tm = tmp->tm_min) < 0 || tm > 59)
			tm = 0;
		if((ts = tmp->tm_sec) < 0 || ts > 60)
			ts = 0;
	}

	(void)snprintf(buf, sizeof buf, "%3s %3s%3d %.2d:%.2d:%.2d %d", wdn, mn, md, th, tm, ts, y);

	NYD_OU;
	return buf;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_TIME
/* s-itt-mode */
