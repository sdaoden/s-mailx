/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ All sorts of `reply', `resend', `forward', and similar user commands.
 *
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_RESEND_H
#define mx_CMD_RESEND_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

/* All thinkable sorts of `reply' / `respond' and `followup'.. */
EXPORT int c_reply(void *vp);
EXPORT int c_replyall(void *vp); /* v15-compat */
EXPORT int c_replysender(void *vp); /* v15-compat */
EXPORT int c_Reply(void *vp);
EXPORT int c_followup(void *vp);
EXPORT int c_followupall(void *vp); /* v15-compat */
EXPORT int c_followupsender(void *vp); /* v15-compat */
EXPORT int c_Followup(void *vp);

/* ..and a mailing-list reply and followup */
EXPORT int c_Lreply(void *vp);
EXPORT int c_Lfollowup(void *vp);

/* 'forward' / `Forward' */
EXPORT int c_forward(void *vp);
EXPORT int c_Forward(void *vp);

/* Resend a message list to a third person.
 * The latter does not add the Resent-* header series */
EXPORT int c_resend(void *vp);
EXPORT int c_Resend(void *vp);

#include <su/code-ou.h>
#endif /* mx_CMD_RESEND_H */
/* s-it-mode */
