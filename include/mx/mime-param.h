/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME parameter handling.
 *
 * Copyright (c) 2016 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MIME_PARAM_H
#define mx_MIME_PARAM_H

#include <su/code.h>

#define mx_HEADER
#include <su/code-in.h>

struct str;

/* Get a mime style parameter from a header body */
EXPORT char *mx_mime_param_get(char const *param, char const *headerbody);

/* Format parameter name to have value, AUTO_ALLOC() it or NIL in result.
 * 0 on error, 1 or -1 on success: the latter if result contains \n newlines,
 * which it will if the created param requires more than MIME_LINELEN bytes;
 * there is never a trailing newline character */
/* TODO mime_param_create() should return a StrList<> or something.
 * TODO in fact it should take a HeaderField* and append HeaderFieldParam*! */
EXPORT s8 mx_mime_param_create(struct str *result, char const *name,
      char const *value);

/* Get the boundary out of a Content-Type: multipart/xyz header field, return
 * AUTO_ALLOC()ated copy of it; store su_cs_len() in *len if set */
EXPORT char *mx_mime_param_boundary_get(char const *headerbody, uz *len);

/* Create a AUTO_ALLOC()ed MIME boundary */
/* TODO "minimal" argument, for a_mt_classify_round() */
EXPORT char *mx_mime_param_boundary_create(void);

#include <su/code-ou.h>
#endif /* mx_MIME_PARAM_H */
/* s-it-mode */
