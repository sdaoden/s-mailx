/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Spam related facilities.
 *
 * Copyright (c) 2013 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_SPAM_H
#define mx_CMD_SPAM_H

#include <mx/nail.h>
#ifdef mx_HAVE_SPAM

#define mx_HEADER
#include <su/code-in.h>

EXPORT int c_spam_clear(void *vp);
EXPORT int c_spam_set(void *vp);
EXPORT int c_spam_forget(void *vp);
EXPORT int c_spam_ham(void *vp);
EXPORT int c_spam_rate(void *vp);
EXPORT int c_spam_spam(void *vp);

#include <su/code-ou.h>
#endif /* mx_HAVE_SPAM */
#endif /* mx_CMD_SPAM_H */
/* s-it-mode */