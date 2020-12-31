/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ .netrc file handling.
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
#ifndef mx_CRED_NETRC_H
#define mx_CRED_NETRC_H

#include <mx/nail.h>
#ifdef mx_HAVE_NETRC

#include <mx/url.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_netrc_entry{
   char const *nrce_machine; /* Matching machine entry */
   char const *nrce_login; /* Or NIL */
   char const *nrce_password; /* Or NIL */
};

/* `netrc' */
EXPORT int c_netrc(void *vp);

/* Lookup an entry for urlp in the .netrc database, fill in result on success:
 * any data points into internal storage.
 * Returns FAL0 if no entry has been found, or if no user was provided and
 * multiple entries match for the given host.
 * Otherwise returns TRU1 if an entry was found, and
 * TRUM1 if an exact match has been found; an exact match is
 * - if urlp has a user and we have a machine (hostname) / login (user) match
 * - if only one match exists for machine (and it does not contradict
 *   a possible URL provided user) */
EXPORT boole mx_netrc_lookup(struct mx_netrc_entry *result,
      struct mx_url const *urlp);

#include <su/code-ou.h>
#endif /* mx_HAVE_NETRC */
#endif /* mx_CRED_NETRC_H */
/* s-it-mode */
