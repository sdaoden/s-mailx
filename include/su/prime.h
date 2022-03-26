/*@ Prime numbers.
 *
 * Copyright (c) 2001 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_PRIME_H
#define su_PRIME_H
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
#define su_PRIME_LOOKUP_MIN 0x2u
#define su_PRIME_LOOKUP_MAX 0x18000005u
EXPORT boole su_prime_is_prime(u64 no, boole allowpseudo);
EXPORT u64 su_prime_get_former(u64 no, boole allowpseudo);
EXPORT u64 su_prime_get_next(u64 no, boole allowpseudo);
EXPORT u32 su_prime_lookup_former(u32 no);
EXPORT u32 su_prime_lookup_next(u32 no);
C_DECL_END
#include <su/code-ou.h>
#endif /* su_PRIME_H */
/* s-it-mode */
