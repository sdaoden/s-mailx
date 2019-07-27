/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ .netrc file handling.
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
#ifndef mx_CRED_NETRC_H
#define mx_CRED_NETRC_H

#include <mx/nail.h>
#ifdef mx_HAVE_NETRC

#include <mx/url.h>

#define mx_HEADER
#include <su/code-in.h>

/* `netrc' */
EXPORT int c_netrc(void *vp);

/* We shall lookup a machine in .netrc says ok_blook(netrc_lookup).
 * only_pass is true then the lookup is for the password only, otherwise we
 * look for a user (and add password only if we have an exact machine match) */
EXPORT boole mx_netrc_lookup(struct mx_url *urlp, boole only_pass);

#include <su/code-ou.h>
#endif /* mx_HAVE_NETRC */
#endif /* mx_CRED_NETRC_H */
/* s-it-mode */
