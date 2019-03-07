/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `mlist', `mlsubscribe'.
 *
 * Copyright (c) 2014 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MLIST_H
#define mx_MLIST_H

#include <mx/nail.h>

#include <su/code-in.h>

enum mx_mlist_type{
   mx_MLIST_OTHER = 0, /* Normal address */
   mx_MLIST_KNOWN = 1, /* A known `mlist' */
   mx_MLIST_SUBSCRIBED = -1 /* A `mlsubscribe'd list */
};

/* `(un)?ml(ist|subscribe)' */
FL int c_mlist(void *vp);
FL int c_unmlist(void *vp);
FL int c_mlsubscribe(void *vp);
FL int c_unmlsubscribe(void *vp);

/* Whether a name is a (wanted) list;
 * give MLIST_OTHER to the latter to search for any, in which case all
 * receivers are searched until EOL or _SUBSCRIBED is seen.
 * XXX the latter possibly belongs to message or header */
FL enum mx_mlist_type mx_mlist_query(char const *name, boole subscribed_only);
FL enum mx_mlist_type mx_mlist_query_mp(struct message *mp,
      enum mx_mlist_type what);

#include <su/code-ou.h>
#endif /* mx_MLIST_H */
/* s-it-mode */
