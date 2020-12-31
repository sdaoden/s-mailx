/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ OAUTHBEARER (RFC 7628, SASL mechanisms for OAuth, borrowing from OAuth 2.0
 *@ bearer token, RFC 6750); and XOAUTH2 (an early-shoot alternate mechanism).
 *@ xxx Primitive.  Better: restartable object, or a linked-in-stream filter.
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
#ifndef mx_CRED_OAUTHBEARER_H
#define mx_CRED_OAUTHBEARER_H

#include <mx/nail.h>
#ifdef mx_HAVE_NET

#include <mx/cred-auth.h>
#include <mx/url.h>

#define mx_HEADER
#include <su/code-in.h>

/* This LOFI allocates a result buffer that contains pre_len bytes of
 * uninitialized storage at the front plus the OAUTHBEARER (or XOAUTH2)
 * Initial Client Response.
 * pre_len must not excess "T18446744073709551615 AUTHENTICATE OAUTHBEARER ".
 * Upon error (it was logged) res is zeroed. */
EXPORT boole mx_oauthbearer_create_icr(struct str *res, uz pre_len,
      struct mx_url const *urlp, struct mx_cred_ctx const *ccp,
      boole is_xoauth2);

#include <su/code-ou.h>
#endif /* mx_HAVE_NET */
#endif /* mx_CRED_OAUTHBEARER_H */
/* s-it-mode */
