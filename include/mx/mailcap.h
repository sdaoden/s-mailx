/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Mailcap file a.k.a. RFC 1524 handling.
 *
 * Copyright (c) 2019 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MAILCAP_H
#define mx_MAILCAP_H

#include <mx/nail.h>
#ifdef mx_HAVE_MAILCAP

#define mx_HEADER
#include <su/code-in.h>

/* Forward: only used for the provider of that type ... */
struct mx_mime_type_handler;

/* `mailcap' */
EXPORT int c_mailcap(void *vp);

/* Try to find an action Mailcap handler for the MIME content-type ct, fill in
 * mthp accordingly upon success.
 * If this returns TRUM1 then it is a only-use-as-last-resort handler. */
EXPORT boole mx_mailcap_handler(struct mx_mime_type_handler *mthp,
      char const *ct, enum sendaction action, struct mimepart const *mpp);

#include <su/code-ou.h>
#endif /* mx_HAVE_MAILCAP */
#endif /* mx_MAILCAP_H */
/* s-it-mode */
