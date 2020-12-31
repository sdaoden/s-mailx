/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MTA alias processing.
 *
 * Copyright (c) 2019 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MTA_ALIASES_H
#define mx_MTA_ALIASES_H

#include <mx/nail.h>
#ifdef mx_HAVE_MTA_ALIASES

#include <mx/names.h>

#define mx_HEADER
#include <su/code-in.h>

/* `mtaaliases' */
EXPORT int c_mtaaliases(void *vp);

/* Expand all names from *npp which are still of type mx_NAME_ADDRSPEC_ISNAME,
 * iff *mta-aliases* is set.
 * Return ERR_NONE when processing completed normally, ERR_NOENT if the file
 * given in *mta-aliases* does not exist, or whatever other error occurred.
 * ERR_DESTADDRREQ is returned instead of ERR_NONE if after expansion ISNAME
 * entries still remain in *npp.
 * The result may contain duplicates */
EXPORT s32 mx_mta_aliases_expand(struct mx_name **npp);

#include <su/code-ou.h>
#endif /* mx_HAVE_MTA_ALIASES */
#endif /* mx_MTA_ALIASES_H */
/* s-it-mode */
