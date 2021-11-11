/*@ Anything (locale agnostic: ASCII only) around char and char*.
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
#ifndef su_CS_H
#define su_CS_H
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
enum su_cs_ctype{
   su_CS_CTYPE_NONE,
   su_CS_CTYPE_ALNUM = 1u<<0,
   su_CS_CTYPE_ALPHA = 1u<<1,
   su_CS_CTYPE_BLANK = 1u<<2,
   su_CS_CTYPE_CNTRL = 1u<<3,
   su_CS_CTYPE_DIGIT = 1u<<4,
   su_CS_CTYPE_GRAPH = 1u<<5,
   su_CS_CTYPE_LOWER = 1u<<6,
   su_CS_CTYPE_PRINT = 1u<<7,
   su_CS_CTYPE_PUNCT = 1u<<8,
   su_CS_CTYPE_SPACE = 1u<<9,
   su_CS_CTYPE_UPPER = 1u<<10,
   su_CS_CTYPE_WHITE = 1u<<11,
   su_CS_CTYPE_XDIGIT = 1u<<12,
   su__CS_CTYPE_MAXSHIFT = 13u,
   su__CS_CTYPE_MASK = (1u<<su__CS_CTYPE_MAXSHIFT) - 1
};
EXPORT_DATA u16 const su__cs_ctype[S8_MAX + 1];
EXPORT_DATA u8 const su__cs_tolower[S8_MAX + 1];
EXPORT_DATA u8 const su__cs_toupper[S8_MAX + 1];
EXPORT_DATA struct su_toolbox const su_cs_toolbox;
EXPORT_DATA struct su_toolbox const su_cs_toolbox_case;
INLINE boole su_cs_is_ascii(s32 x) {return (S(u32,x) <= S8_MAX);}
#undef a_X
#define a_X(X,F) \
   return (su_cs_is_ascii(X) &&\
      (su__cs_ctype[S(u32,X)] & su_CONCAT(su_CS_CTYPE_,F)) != 0)
INLINE boole su_cs_is_alnum(s32 x) {a_X(x, ALNUM);}
INLINE boole su_cs_is_alpha(s32 x) {a_X(x, ALPHA);}
INLINE boole su_cs_is_blank(s32 x) {a_X(x, BLANK);}
INLINE boole su_cs_is_cntrl(s32 x) {a_X(x, CNTRL);}
INLINE boole su_cs_is_digit(s32 x) {a_X(x, DIGIT);}
INLINE boole su_cs_is_graph(s32 x) {a_X(x, GRAPH);}
INLINE boole su_cs_is_lower(s32 x) {a_X(x, LOWER);}
INLINE boole su_cs_is_print(s32 x) {a_X(x, PRINT);}
INLINE boole su_cs_is_punct(s32 x) {a_X(x, PUNCT);}
INLINE boole su_cs_is_space(s32 x) {a_X(x, SPACE);}
INLINE boole su_cs_is_upper(s32 x) {a_X(x, UPPER);}
INLINE boole su_cs_is_white(s32 x) {a_X(x, WHITE);}
INLINE boole su_cs_is_xdigit(s32 x) {a_X(x, XDIGIT);}
#undef a_X
INLINE boole su_cs_is_ctype(s32 x, u32 csct){
   return (su_cs_is_ascii(x) && (su__cs_ctype[x] & csct) != 0);
}
EXPORT sz su_cs_cmp(char const *cp1, char const *cp2);
EXPORT sz su_cs_cmp_n(char const *cp1, char const *cp2, uz n);
EXPORT sz su_cs_cmp_case(char const *cp1, char const *cp2);
EXPORT sz su_cs_cmp_case_n(char const *cp1, char const *cp2, uz n);
EXPORT char *su_cs_copy_n(char *dst, char const *src, uz n);
EXPORT char *su_cs_dup_cbuf(char const *buf, uz len, u32 estate);
EXPORT char *su_cs_dup(char const *cp, u32 estate);
#if 0
EXPORT boole su_cs_ends_with_case(char const *cp, char const *x);
#endif
EXPORT char *su_cs_find(char const *cp, char const *xp);
EXPORT char *su_cs_find_c(char const *cp, char xc);
EXPORT char *su_cs_find_case(char const *cp, char const *xp);
EXPORT uz su_cs_first_of_cbuf_cbuf(char const *cp, uz cplen,
      char const *xp, uz xlen);
INLINE uz su_cs_first_of(char const *cp, char const *xp){
   ASSERT_RET(cp != NIL, UZ_MAX);
   ASSERT_RET(xp != NIL, UZ_MAX);
   return su_cs_first_of_cbuf_cbuf(cp, UZ_MAX, xp, UZ_MAX);
}
EXPORT uz su_cs_hash_cbuf(char const *buf, uz len);
INLINE uz su_cs_hash(char const *cp){
   ASSERT_RET(cp != NIL, 0);
   return su_cs_hash_cbuf(cp, UZ_MAX);
}
EXPORT uz su_cs_hash_case_cbuf(char const *buf, uz len);
INLINE uz su_cs_hash_case(char const *cp){
   ASSERT_RET(cp != NIL, 0);
   return su_cs_hash_case_cbuf(cp, UZ_MAX);
}
EXPORT uz su_cs_len(char const *cp);
EXPORT char *su_cs_pcopy(char *dst, char const *src);
EXPORT char *su_cs_pcopy_n(char *dst, char const *src, uz n);
EXPORT char *su_cs_rfind_c(char const *cp, char x);
EXPORT char *su_cs_sep_c(char **iolist, char sep, boole ignore_empty);
EXPORT char *su_cs_sep_escable_c(char **iolist, char sep, boole ignore_empty);
EXPORT boole su_cs_starts_with(char const *cp, char const *x);
EXPORT boole su_cs_starts_with_n(char const *cp, char const *x, uz n);
EXPORT boole su_cs_starts_with_case(char const *cp, char const *x);
EXPORT boole su_cs_starts_with_case_n(char const *cp, char const *x, uz n);
INLINE s32 su_cs_to_lower(s32 x){
   return (S(u32,x) <= S8_MAX ? su__cs_tolower[x] : x);
}
INLINE s32 su_cs_to_upper(s32 x){
   return (S(u32,x) <= S8_MAX ? su__cs_toupper[x] : x);
}
C_DECL_END
#include <su/code-ou.h>
#endif /* su_CS_H */
/* s-it-mode */
