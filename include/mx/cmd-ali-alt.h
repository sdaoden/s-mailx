/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `alias' and `alternates' and related support.
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
#ifndef mx_CMD_ALI_ALT_H
#define mx_CMD_ALI_ALT_H

#include <mx/nail.h>

#include <mx/names.h>

#define mx_HEADER
#include <su/code-in.h>

/* `(un)?alias' */
EXPORT int c_alias(void *vp);
EXPORT int c_unalias(void *vp);

/* Is name a valid alias name (as opposed to: "is an alias") */
EXPORT boole mx_alias_is_valid_name(char const *name);

/* */
EXPORT boole mx_alias_exists(char const *name);

/* Recursively expand np (*metoo*) */
EXPORT struct mx_name *mx_alias_expand(struct mx_name *np, boole force_metoo);

/* Map all of the aliased users in the invoker's mailrc file and insert them into the list (*allnet*) */
EXPORT struct mx_name *mx_alias_expand_list(struct mx_name *names, boole allnet);

/* `(un)?alternates' deal with the list of alternate names */
EXPORT int c_alternates(void *vp);
EXPORT int c_unalternates(void *vp);

/* */
EXPORT boole mx_alternates_exists(char const *name, boole force_metoo);

/* If keep_single is set one alternates member will be allowed in np */
EXPORT struct mx_name *mx_alternates_remove(struct mx_name *np, boole keep_single);

#include <su/code-ou.h>
#endif /* mx_CMD_ALI_ALT_H */
/* s-itt-mode */
