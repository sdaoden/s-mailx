/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Commands: conditional constructs.
 *
 * Copyright (c) 2014 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_CND_H
#define mx_CMD_CND_H

#include <su/code.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_go_data_ctx;

/* if.elif.else.endif conditional execution */
EXPORT int c_if(void *vp);
EXPORT int c_elif(void *vp);
EXPORT int c_else(void *vp);
EXPORT int c_endif(void *vp);

/* Whether an `if' block exists (TRU1) / is in a whiteout condition (TRUM1) */
EXPORT boole mx_cnd_if_exists(void);

/* An execution context is teared down, and it finds to have an if stack */
EXPORT void mx_cnd_if_stack_del(struct mx_go_data_ctx *gdcp);

#include <su/code-ou.h>
#endif /* mx_CMD_CND_H */
/* s-it-mode */
