/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Conditional expressions.
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
#ifndef mx_CNDEXP_H
#define mx_CNDEXP_H

#include <su/code.h>

#define mx_HEADER
#include <su/code-in.h>

/* Evaluate a conditional expression as presented in argv.
 * Returns TRU1 if the condition is true, FAL0 if it is not.
 * TRUM1 is returned upon error, log_on_error then allows n_err() reports */
EXPORT boole mx_cndexp_parse(char const * const *argv, boole log_on_error);

#include <su/code-ou.h>
#endif /* mx_CNDEXP_H */
/* s-it-mode */
