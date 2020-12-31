/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `mlist', `mlsubscribe'.
 *
 * Copyright (c) 2014 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_MLIST_H
#define mx_CMD_MLIST_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

enum mx_mlist_type{
   mx_MLIST_OTHER, /* Normal address */
   mx_MLIST_POSSIBLY, /* Has a List-Post: header, but not know otherwise */
   mx_MLIST_KNOWN, /* A known `mlist' */
   mx_MLIST_SUBSCRIBED /* A `mlsubscribe'd list */
};

/* `(un)?ml(ist|subscribe)' */
EXPORT int c_mlist(void *vp);
EXPORT int c_unmlist(void *vp);
EXPORT int c_mlsubscribe(void *vp);
EXPORT int c_unmlsubscribe(void *vp);

/* Whether a name is a known (or subscribed_only) list */
EXPORT enum mx_mlist_type mx_mlist_query(char const *name,
      boole subscribed_only);

/* Give MLIST_OTHER to search for any kind of list, in which case all receivers
 * are searched until EOL or _SUBSCRIBED is seen.
 * _POSSIBLY may not be searched for; it return _POSSIBLY for _OTHER, though.
 * XXX possibly belongs to message or header specific code */
EXPORT enum mx_mlist_type mx_mlist_query_mp(struct message *mp,
      enum mx_mlist_type what);

#include <su/code-ou.h>
#endif /* mx_CMD_MLIST_H */
/* s-it-mode */
