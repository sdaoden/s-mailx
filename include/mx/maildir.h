/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Maildir folder support.
 *
 * Copyright (c) 2012 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MAILDIR_H
#define mx_MAILDIR_H

#include <mx/nail.h>
#ifdef mx_HAVE_MAILDIR

#define mx_HEADER
#include <su/code-in.h>

EXPORT int maildir_setfile(char const *who, char const *name, enum fedit_mode fm);
EXPORT boole maildir_quit(boole hold_sigs_on);

EXPORT boole maildir_append(char const *name, FILE *fp, s64 offset, boole realstat);

EXPORT boole maildir_remove(char const *name);

EXPORT boole mx_maildir_lazy_load_header(struct mailbox *mbp, u32 lo, u32 hi);
EXPORT boole mx_maildir_msg_lazy_load(struct mailbox *mbp, struct message *mp, enum needspec ns);

#include <su/code-ou.h>
#endif /* mx_HAVE_MAILDIR */
#endif /* mx_MAILDIR_H */
/* s-itt-mode */
