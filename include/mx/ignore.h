/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `headerpick' and other ignore/retain related facilities; And `headerorder'.
 *
 * Copyright (c) 2012 - 2025 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_IGNORE_H
#define mx_IGNORE_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_ignore; /* Transparent */
struct mx_hdrorder;

/* Special ignore (where _TYPE is covered by POSIX `ignore' / `retain').
 * _ALL is very special in that it does not have a backing object.
 * Go over enum to avoid cascads of (different) CC warnings for used CTA()s */
enum{
	mx__IGNORE_ALL = -2,
	mx__IGNORE_TYPE = -3,
	mx__IGNORE_SAVE = -4,
	mx__IGNORE_FWD = -5,
	mx__IGNORE_TOP = -6,
	mx__IGNORE_ADJUST = 3,
	mx__IGNORE_MAX = -(mx__IGNORE_TOP) - mx__IGNORE_ADJUST
};
#define mx_IGNORE_ALL R(struct mx_ignore*,mx__IGNORE_ALL)
#define mx_IGNORE_TYPE R(struct mx_ignore*,mx__IGNORE_TYPE)
#define mx_IGNORE_SAVE R(struct mx_ignore*,mx__IGNORE_SAVE)
#define mx_IGNORE_FWD R(struct mx_ignore*,mx__IGNORE_FWD)
#define mx_IGNORE_TOP R(struct mx_ignore*,mx__IGNORE_TOP)

struct mx_hdrorder{ /* xxx v15 temporary result type */
	uz ho_cnt; /* Count of slots in .ho_fields and .ho_sorted */
	char const **ho_fields; /* The names of covered header fields in sort order */
	struct n_strlist **ho_sorted; /* NIL initialized array .. for consumers */
	struct n_strlist *ho_unsorted; /* NIL initialized .. ditto */
	struct n_strlist **ho_unsorted_tail; /* points to &.ho_unsorted at first */
	char ho__buf[su_VFIELD_SIZE(0)]; /* storage for anything */
};

/* `(un)?headerpick' */
EXPORT int c_headerpick(void *vp);
EXPORT int c_unheaderpick(void *vp);

/* `[un]retain' and `[un]ignore' are standardized and will not vanish. */
EXPORT int c_retain(void *vp);
EXPORT int c_ignore(void *vp);
EXPORT int c_unretain(void *vp);
EXPORT int c_unignore(void *vp);

/* TODO Will all vanish in v15 */
EXPORT int c_saveretain(void *v);
EXPORT int c_saveignore(void *v);
EXPORT int c_unsaveretain(void *v);
EXPORT int c_unsaveignore(void *v);
EXPORT int c_fwdretain(void *v);
EXPORT int c_fwdignore(void *v);
EXPORT int c_unfwdretain(void *v);
EXPORT int c_unfwdignore(void *v);

/* Object lifecycle for non-specials.
 * auto_cleanup: whether memory storage should be managed via go_cleanup_ctx; if so, _del() MUST not be called */
EXPORT struct mx_ignore *mx_ignore_new(char const *name, boole auto_cleanup);
EXPORT void mx_ignore_del(struct mx_ignore *self);

/* Returns NIL if no such object exists */
EXPORT struct mx_ignore *mx_ignore_by_name(char const *name);

/* Are there just _any_ user settings covered by self? */
EXPORT boole mx_ignore_is_any(struct mx_ignore const *self);

/* Set an entry to retain (or ignore).
 * Returns FAL0 if dat is not a valid header field name or an invalid regular expression, TRU1 if insertion took place,
 * and TRUM1 if already set */
EXPORT boole mx_ignore_insert(struct mx_ignore *self, boole retain, char const *dat);

/* Returns TRU1 if retained, TRUM1 if ignored, FAL0 if not covered */
EXPORT boole mx_ignore_lookup(struct mx_ignore const *self, char const *dat);
#define mx_ignore_is_ign(SELF,FDAT) (mx_ignore_lookup(SELF, FDAT) == TRUM1)

/* */
EXPORT int c_headerorder(void *vp);
EXPORT int c_unheaderorder(void *vp);

/* Create a hdrorder instance in auto memory if the user has configured a `headerorder',
 * and self will pass through some of those; otherwise returns NIL.
 * Note: .ho_fields point to internal storage -- self must not be modified!
 * xxx v15 temporary: later headerorder_sort_object_list() or something, possibly no-op.. */
EXPORT struct mx_hdrorder *mx_headerorder_new_auto_if(struct mx_ignore const *self);

#include <su/code-ou.h>
#endif /* mx_IGNORE_H */
/* s-itt-mode */
