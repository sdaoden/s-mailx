/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME parameter handling.
 *
 * Copyright (c) 2016 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
struct mx_mime_probe_charset_ctx;

/* Get a (decoded) mime style parameter from a header body */
EXPORT char *mx_mime_param_get(char const *param, char const *headerbody);

/* RFC 2045/RFC 2231 format parameter name to have value, AUTO_ALLOC() it or NIL in result.
 * 0 on error, 1 or -1 on success: the latter if result contains \n newlines, which it will if the created param
 * requires more than MIME_LINELEN bytes; there never is a trailing newline character.
 * clean_is_ascii is a TODO HACK in that we assume a clean value is indeed US-ASCII not *charset-7bit*: this should be
 * used for parameters like charset= where the charset is "known to be" US-ASCII */
/* TODO mime_param_create() should return a StrList<> or something.
 * TODO in fact it should take a HeaderField* and append HeaderFieldParam*! */
EXPORT s8 mx_mime_param_create(boole clean_is_ascii, struct str *result, char const *name, char const *value,
		struct mx_mime_probe_charset_ctx const *mpccp);

/* Get the boundary out of a Content-Type: multipart/xyz header field, return AUTO_ALLOC()ated copy of it;
 * store su_cs_len() in *len if set */
EXPORT char *mx_mime_param_boundary_get(char const *headerbody, uz *len);

/* Create an encoded AUTO_ALLOC()ed MIME boundary */
/* TODO hook into sendout with optional struct mx_send_ctx argument, then, if given, we could create
 * TODO the minimum possible boundary if we know all parts are MIME-aware */
EXPORT char *mx_mime_param_boundary_create(void);

#include <su/code-ou.h>
#endif /* mx_MIME_PARAM_H */
/* s-itt-mode */
