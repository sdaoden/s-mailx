/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Time related facilities.
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_TIME_H
#define mx_TIME_H

#include <mx/nail.h>

#include <time.h> /* TODO SU */

#include <su/time.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_time_current{ /* TODO s64, etc. */
   s64 tc_time;
   struct tm tc_gm;
   struct tm tc_local;
   char tc_ctime[32];
};

EXPORT_DATA struct mx_time_current mx_time_current; /* time(3); send: mail1() XXXcarrier */

/* Update *tc_or_nil* (global one if NIL) to now; only .tc_time updated unless *full_update* is true. */
EXPORT struct mx_time_current *mx_time_current_update(struct mx_time_current *tc_or_nil, boole full_update);

/* Get seconds since epoch, return pointer to static struct.
 * Unless force_update is true we may use the event-loop tick time */
EXPORT struct su_timespec const *mx_time_now(boole force_update);

/* TZ difference in seconds.  secsepoch is only used if any of the tm's is NIL. */
EXPORT s32 mx_time_tzdiff(s64 secsepoch, struct tm const *utcp_or_nil, struct tm const *localp_or_nil);

/* ctime(3), but do ensure 26 byte limit, do not crash XXX static buffer.  NOTE: no trailing newline */
EXPORT char *mx_time_ctime(s64 secsepoch, struct tm const *localtime_or_nil);

#include <su/code-ou.h>
#endif /* mx_TIME_H */
/* s-itt-mode */
