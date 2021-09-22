/*@ boswap.h: internals, generic version.
 *
 * Copyright (c) 2001 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_BOSWAP_H
# error Please include boswap.h instead
#elif !defined su__BOSWAP_X
# define su__BOSWAP_X 1

#elif su__BOSWAP_X == 1
# undef su__BOSWAP_X
# define su__BOSWAP_X 2

INLINE u16 su__boswap_16(u16 v){
   return (
      ((v & 0x00FFu) << 8) |
      ((v & 0xFF00u) >> 8)
   );
}

INLINE u32 su__boswap_32(u32 v){
   return (
      ((v & 0x000000FFu) << 24) |
      ((v & 0x0000FF00u) <<  8) |
      ((v & 0x00FF0000u) >>  8) |
      ((v & 0xFF000000u) >> 24)
   );
}

INLINE u64 su__boswap_64(u64 v){
   return (
      ((v & U64_C(0x00000000000000FF)) << 56) |
      ((v & U64_C(0x000000000000FF00)) << 40) |
      ((v & U64_C(0x0000000000FF0000)) << 24) |
      ((v & U64_C(0x00000000FF000000)) <<  8) |
      ((v & U64_C(0x000000FF00000000)) >>  8) |
      ((v & U64_C(0x0000FF0000000000)) >> 24) |
      ((v & U64_C(0x00FF000000000000)) >> 40) |
      ((v & U64_C(0xFF00000000000000)) >> 56)
   );
}

#elif su__BOSWAP_X == 2
# undef su__BOSWAP_X
# define su__BOSWAP_X 3

#else
# error .
#endif
/* s-it-mode */
