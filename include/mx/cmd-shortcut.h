/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `shortcut'.
 *
 * Copyright (c) 2017 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_SHORTCUT_H
#define mx_CMD_SHORTCUT_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

/* `(un)?shortcut' */
EXPORT int c_shortcut(void *vp);
EXPORT int c_unshortcut(void *vp);

/* Check if str is a shortcut, return expansion or NIL */
EXPORT char const *mx_shortcut_expand(char const *str);

#include <su/code-ou.h>
#endif /* mx_CMD_SHORTCUT_H */
/* s-it-mode */
