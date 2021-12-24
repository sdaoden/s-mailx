/*@ Anything (locale agnostic: ASCII only) around char and char*.
 *@ TODO optimization option like atomic.h
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
#ifndef su_CS_H
#define su_CS_H

/*!
 * \file
 * \ingroup CS
 * \brief \r{CS} tools and heap
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* Forwards */
#ifdef su_HAVE_MD
struct su_siphash;
#endif

/* cs {{{ */
/*!
 * \defgroup CS Byte character data
 * \ingroup TEXT
 * \brief Byte character data, locale agnostic: ASCII only (\r{su/cs.h})
 *
 * \remarks{Functions that include \c{cbuf} in their name are capable to work
 * on buffers with include \c{NUL}s unless the length parameter was given as
 * \r{su_UZ_MAX}, since that enforces search for the terminating \c{NUL}.}
 * @{
 */

/*! \_ */
enum su_cs_ctype{
   su_CS_CTYPE_NONE, /*!< \_ */
   su_CS_CTYPE_ALNUM = 1u<<0, /*!< \_ */
   su_CS_CTYPE_ALPHA = 1u<<1, /*!< \_ */
   su_CS_CTYPE_BLANK = 1u<<2, /*!< \_ */
   su_CS_CTYPE_CNTRL = 1u<<3, /*!< \_ */
   su_CS_CTYPE_DIGIT = 1u<<4, /*!< \_ */
   su_CS_CTYPE_GRAPH = 1u<<5, /*!< \_ */
   su_CS_CTYPE_LOWER = 1u<<6, /*!< \_ */
   su_CS_CTYPE_PRINT = 1u<<7, /*!< \_ */
   su_CS_CTYPE_PUNCT = 1u<<8, /*!< \_ */
   su_CS_CTYPE_SPACE = 1u<<9, /*!< \_ */
   su_CS_CTYPE_UPPER = 1u<<10, /*!< \_ */
   su_CS_CTYPE_WHITE = 1u<<11, /*!< SPACE, HT or LF. */
   su_CS_CTYPE_XDIGIT = 1u<<12, /*!< \_ */

   su__CS_CTYPE_MAXSHIFT = 13u,
   su__CS_CTYPE_MASK = (1u<<su__CS_CTYPE_MAXSHIFT) - 1
};

EXPORT_DATA u16 const su__cs_ctype[1u + S8_MAX];
EXPORT_DATA u8 const su__cs_tolower[1u + S8_MAX];
EXPORT_DATA u8 const su__cs_toupper[1u + S8_MAX];

/*! \_ */
EXPORT_DATA struct su_toolbox const su_cs_toolbox;

/*! \_ */
EXPORT_DATA struct su_toolbox const su_cs_toolbox_case;

/*! This actually tests for 7-bit cleanliness. */
INLINE boole su_cs_is_ascii(s32 x) {return (S(u32,x) <= S8_MAX);}

#undef a_X
#define a_X(X,F) \
   return (su_cs_is_ascii(X) &&\
      (su__cs_ctype[S(u32,X)] & su_CONCAT(su_CS_CTYPE_,F)) != 0)

/*! \r{su_CS_CTYPE_ALNUM}. */
INLINE boole su_cs_is_alnum(s32 x) {a_X(x, ALNUM);}

/*! \r{su_CS_CTYPE_ALPHA}. */
INLINE boole su_cs_is_alpha(s32 x) {a_X(x, ALPHA);}

/*! \r{su_CS_CTYPE_BLANK}. */
INLINE boole su_cs_is_blank(s32 x) {a_X(x, BLANK);}

/*! \r{su_CS_CTYPE_CNTRL}. */
INLINE boole su_cs_is_cntrl(s32 x) {a_X(x, CNTRL);}

/*! \r{su_CS_CTYPE_DIGIT}. */
INLINE boole su_cs_is_digit(s32 x) {a_X(x, DIGIT);}

/*! \r{su_CS_CTYPE_GRAPH}. */
INLINE boole su_cs_is_graph(s32 x) {a_X(x, GRAPH);}

/*! \r{su_CS_CTYPE_LOWER}. */
INLINE boole su_cs_is_lower(s32 x) {a_X(x, LOWER);}

/*! \r{su_CS_CTYPE_PRINT}. */
INLINE boole su_cs_is_print(s32 x) {a_X(x, PRINT);}

/*! \r{su_CS_CTYPE_PUNCT}. */
INLINE boole su_cs_is_punct(s32 x) {a_X(x, PUNCT);}

/*! \r{su_CS_CTYPE_SPACE}. */
INLINE boole su_cs_is_space(s32 x) {a_X(x, SPACE);}

/*! \r{su_CS_CTYPE_UPPER}. */
INLINE boole su_cs_is_upper(s32 x) {a_X(x, UPPER);}

/*! \r{su_CS_CTYPE_WHITE}. */
INLINE boole su_cs_is_white(s32 x) {a_X(x, WHITE);}

/*! \r{su_CS_CTYPE_XDIGIT}. */
INLINE boole su_cs_is_xdigit(s32 x) {a_X(x, XDIGIT);}

#undef a_X

/*! Test \a{x} for any of the \r{su_cs_ctype} bits given in \a{csct}. */
INLINE boole su_cs_is_ctype(s32 x, u32 csct){
   return (su_cs_is_ascii(x) && (su__cs_ctype[x] & csct) != 0);
}

/*! String comparison, byte-based, case-sensitive. */
EXPORT sz su_cs_cmp(char const *cp1, char const *cp2);

/*! \r{su_cs_cmp()}, size-cramped.
 * \remarks{A \a{n} of 0 compares equal.} */
EXPORT sz su_cs_cmp_n(char const *cp1, char const *cp2, uz n);

/*! String comparison, byte-based, case-insensitive. */
EXPORT sz su_cs_cmp_case(char const *cp1, char const *cp2);

/*! \r{su_cs_cmp_case()}, size-cramped.
 * \remarks{A \a{n} of 0 compares equal.} */
EXPORT sz su_cs_cmp_case_n(char const *cp1, char const *cp2, uz n);

/*! Copy at most \a{n} bytes of \a{src} to \a{dst}, and return \a{dst} again.
 * Returns \NIL if \a{dst} is not large enough; \a{dst} will always be
 * terminated unless \a{n} was 0 on entry.
 * Also see \r{su_cs_pcopy_n()}. */
EXPORT char *su_cs_copy_n(char *dst, char const *src, uz n);

/*! Duplicate a buffer into a \r{su_MEM_TALLOC()}ated duplicate.
 * Unless \a{len} was \r{su_UZ_MAX} and thus detected by searching NUL,
 * embedded NUL bytes will be included in the result.
 * \ESTATE. */
EXPORT char *su_cs_dup_cbuf(char const *buf, uz len, u32 estate);

/*! \r{su_cs_dup_cbuf()}. */
EXPORT char *su_cs_dup(char const *cp, u32 estate);

/*! Is \a{x} the ending (sub)string of \a{cp}? */
EXPORT boole su_cs_ends_with_case(char const *cp, char const *x);

/*! Search \a{xp} within \a{cp}, return pointer to location or \NIL.
 * Returns \a{cp} if \a{xp} is the empty buffer. */
EXPORT char *su_cs_find(char const *cp, char const *xp);

/*! Search \a{xc} within \a{cp}, return pointer to location or \NIL. */
EXPORT char *su_cs_find_c(char const *cp, char xc);

/*! Like \r{su_cs_find()}, but case-insensitive. */
EXPORT char *su_cs_find_case(char const *cp, char const *xp);

/*! Returns offset to first character of \a{xp} in \a{cp}, or \r{su_UZ_MAX}.
 * \remarks{Will not find NUL.} */
EXPORT uz su_cs_first_of_cbuf_cbuf(char const *cp, uz cplen,
      char const *xp, uz xlen);

/*! \_ */
INLINE uz su_cs_first_of(char const *cp, char const *xp){
   ASSERT_RET(cp != NIL, UZ_MAX);
   ASSERT_RET(xp != NIL, UZ_MAX);
   return su_cs_first_of_cbuf_cbuf(cp, UZ_MAX, xp, UZ_MAX);
}

/*! Hash a string (buffer).
 * This should be considered an attackable hash, for now Chris Torek's hash
 * algorithm is used, the resulting hash is stirred as shown by Bret Mulvey.
 * For more attack-proof hashing see \r{su_cs_hash_strong_cbuf()} or \r{MD}.
 * Also see \r{su_cs_hash_case_cbuf()}. */
EXPORT uz su_cs_hash_cbuf(char const *buf, uz len);

/*! \r{su_cs_hash_cbuf()}. */
INLINE uz su_cs_hash(char const *cp){
   ASSERT_RET(cp != NIL, 0);
   return su_cs_hash_cbuf(cp, UZ_MAX);
}

/*! Hash a string (buffer), case-insensitively, otherwise identical to
 * \r{su_cs_hash_cbuf()}.
 * As usual, if \a{len} is 0 \a{buf} may be \NIL. */
EXPORT uz su_cs_hash_case_cbuf(char const *buf, uz len);

/*! \r{su_cs_hash_case_cbuf()}. */
INLINE uz su_cs_hash_case(char const *cp){
   ASSERT_RET(cp != NIL, 0);
   return su_cs_hash_case_cbuf(cp, UZ_MAX);
}

#if defined su_HAVE_MD || defined DOXYGEN
/*! Strong hash creators (like \r{su_cs_hash_strong_cbuf()}) that are
 * (more) proof against algorithmic complexity attacks on hashes use
 * a random-seeded built-in \r{MD} template that is created once needed first.
 * By giving a non-\NIL \a{tp} one can instead set the template explicitly:
 * \a{tp} must be fully setup in order to be usable for copy-assignment,
 * and the object backing \a{tp} must remain accessible.
 *
 * \remarks{The built-in template is setup only once, but which may fail since
 * the used \r{su_random_builtin_generate()} can: implicit lazy initialization
 * via the hash creators themselve pass \r{su_STATE_ERR_NOPASS} as \a{estate}!
 * \ESTATE_RV; setup is incomplete unless this returns \r{su_STATE_NONE}.}
 *
 * \remarks{Only available with \r{su_HAVE_MD}.
 * As of today this uses \r{MD_SIPHASH} with endianess-adjusted 64-bit output;
 * if \r{su_UZ_BITS} is 32 the 64-bit output is mixed down to 32-bit.
 * The type backing \a{tp} is only forward-declared, no header is included.} */
EXPORT s32 su_cs_hash_strong_setup(struct su_siphash const *tp, u32 estate);

/*! Hash a string (buffer) with a strong algorithm,
 * see \r{su_cs_hash_strong_setup()} for the complete picture,
 * and \r{su_cs_hash_strong_case_cbuf()} for case-insensitivity.
 * \remarks{Only available with \r{su_HAVE_MD}.}
 * \remarks{May abort the program unless initialization was successfully
 * asserted via \r{su_cs_hash_strong_setup()}.} */
EXPORT uz su_cs_hash_strong_cbuf(char const *buf, uz len);

/*! \r{su_cs_hash_strong_cbuf()}. */
INLINE uz su_cs_hash_strong(char const *cp){
   ASSERT_RET(cp != NIL, 0);
   return su_cs_hash_strong_cbuf(cp, UZ_MAX);
}

/*! Hash a string (buffer) with a strong algorithm, case-insensitively,
 * otherwise identical to \r{su_cs_hash_strong_cbuf()}, see there for more.
 * As usual, if \a{len} is 0 \a{buf} may be \NIL. */
EXPORT uz su_cs_hash_strong_case_cbuf(char const *buf, uz len);

/*! \r{su_cs_hash_strong_case_cbuf()}. */
INLINE uz su_cs_hash_strong_case(char const *cp){
   ASSERT_RET(cp != NIL, 0);
   return su_cs_hash_strong_case_cbuf(cp, UZ_MAX);
}
#endif /* su_HAVE_MD || DOXYGEN */

/*! \_ */
EXPORT uz su_cs_len(char const *cp);

/*! Copy \a{src} to \a{dst}, return pointer to NUL in \a{dst}. */
EXPORT char *su_cs_pcopy(char *dst, char const *src);

/*! Copy \a{src} to \a{dst}, return pointer to NUL in \a{dst}.
 * Returns \NIL if \a{dst} is not large enough; \a{dst} will always be
 * terminated unless \a{n} was 0 on entry. */
EXPORT char *su_cs_pcopy_n(char *dst, char const *src, uz n);

/*! Search \a{x} within \a{cp}, starting at end, return pointer to location
 * or \NIL. */
EXPORT char *su_cs_rfind_c(char const *cp, char x);

/*! Find the next \a{sep}arator in *\a{iolist}, terminate the resulting
 * substring and return it.
 * \r{su_cs_is_space()} surrounding the result will be trimmed away.
 * If \a{ignore_empty} is set, empty results will be skipped over.
 * \a{iolist} will be updated for the next round, \NIL will be placed if the
 * input string is exhausted.
 * If called with an exhausted string, \NIL is returned.
 * (\r{su_cs_sep_escable_c()} supports separator escaping.) */
EXPORT char *su_cs_sep_c(char **iolist, char sep, boole ignore_empty);

/*! Like \r{su_cs_sep_c()}, but supports escaping of \a{sep}arators via reverse
 * solidus characters.
 * \remarks{Whereas reverse solidus characters are supposed to escape the next
 * character, including reverse solidus itself, only those which escape \a{sep}
 * characters will be stripped from the result string.} */
EXPORT char *su_cs_sep_escable_c(char **iolist, char sep, boole ignore_empty);

/*! Is \a{x} the starting (sub)string of \a{cp}? */
EXPORT boole su_cs_starts_with(char const *cp, char const *x);

/*! Is \a{x} the starting (sub)string of \a{cp}? */
EXPORT boole su_cs_starts_with_n(char const *cp, char const *x, uz n);

/*! Is \a{x} the starting (sub)string of \a{cp}, case-insensitively? */
EXPORT boole su_cs_starts_with_case(char const *cp, char const *x);

/*! Is \a{x} the starting (sub)string of \a{cp}, case-insensitively? */
EXPORT boole su_cs_starts_with_case_n(char const *cp, char const *x, uz n);

/*! Map to lowercase equivalent, or return unchanged.
 * For convenience values beyond \c{char} are supported (e.g., \c{EOF}), they
 * are returned unchanged. */
INLINE s32 su_cs_to_lower(s32 x){
   return (S(u32,x) <= S8_MAX ? su__cs_tolower[x] : x);
}

/*! Uppercasing variant of \r{su_cs_to_lower()}. */
INLINE s32 su_cs_to_upper(s32 x){
   return (S(u32,x) <= S8_MAX ? su__cs_toupper[x] : x);
}
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_A_T_T_DECL_ONLY
# include <su/a-t-t.h>
# include <su/md-siphash.h>

# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class cs;

/* CS {{{ */
/*!
 * \ingroup CS
 * C++ variant of \r{CS} (\r{su/cs.h})
 *
 * \remarks{Debug assertions are performed in the C base only.}
 *
 * \remarks{In difference, for C++, \c{su/md-siphash.h} is included.}
 */
class EXPORT cs{
   su_CLASS_NO_COPY(cs);
public:
   /*! \copydoc{su_cs_ctype} */
   enum ctype{
      /*! \copydoc{su_CS_CTYPE_NONE} */
      ctype_none = su_CS_CTYPE_NONE,
      /*! \copydoc{su_CS_CTYPE_ALNUM} */
      ctype_alnum = su_CS_CTYPE_ALNUM,
      /*! \copydoc{su_CS_CTYPE_ALPHA} */
      ctype_alpha = su_CS_CTYPE_ALPHA,
      /*! \copydoc{su_CS_CTYPE_BLANK} */
      ctype_blank = su_CS_CTYPE_BLANK,
      /*! \copydoc{su_CS_CTYPE_CNTRL} */
      ctype_cntrl = su_CS_CTYPE_CNTRL,
      /*! \copydoc{su_CS_CTYPE_DIGIT} */
      ctype_digit = su_CS_CTYPE_DIGIT,
      /*! \copydoc{su_CS_CTYPE_GRAPH} */
      ctype_graph = su_CS_CTYPE_GRAPH,
      /*! \copydoc{su_CS_CTYPE_LOWER} */
      ctype_lower = su_CS_CTYPE_LOWER,
      /*! \copydoc{su_CS_CTYPE_PRINT} */
      ctype_print = su_CS_CTYPE_PRINT,
      /*! \copydoc{su_CS_CTYPE_PUNCT} */
      ctype_punct = su_CS_CTYPE_PUNCT,
      /*! \copydoc{su_CS_CTYPE_SPACE} */
      ctype_space = su_CS_CTYPE_SPACE,
      /*! \copydoc{su_CS_CTYPE_UPPER} */
      ctype_upper = su_CS_CTYPE_UPPER,
      /*! \copydoc{su_CS_CTYPE_WHITE} */
      ctype_white = su_CS_CTYPE_WHITE,
      /*! \copydoc{su_CS_CTYPE_XDIGIT} */
      ctype_xdigit = su_CS_CTYPE_XDIGIT
   };

   /*! \copydoc{su_cs_toolbox} */
   static NSPC(su)type_toolbox<char*> const * const type_toolbox;
   /*! \copydoc{su_cs_toolbox} */
   static NSPC(su)type_toolbox<char const*> const * const const_type_toolbox;

   /*! \copydoc{su_cs_toolbox_case} */
   static NSPC(su)type_toolbox<char*> const * const type_toolbox_case;
   /*! \copydoc{su_cs_toolbox_case} */
   static NSPC(su)type_toolbox<char const*> const * const
         const_type_toolbox_case;

   /*! \copydoc{su_cs_is_ascii()} */
   static boole is_ascii(s32 x) {return su_cs_is_ascii(x);}

   /*! \copydoc{su_cs_is_alnum()} */
   static boole is_alnum(s32 x) {return su_cs_is_alnum(x);}

   /*! \copydoc{su_cs_is_alpha()} */
   static boole is_alpha(s32 x) {return su_cs_is_alpha(x);}

   /*! \copydoc{su_cs_is_blank()} */
   static boole is_blank(s32 x) {return su_cs_is_blank(x);}

   /*! \copydoc{su_cs_is_cntrl()} */
   static boole is_cntrl(s32 x) {return su_cs_is_cntrl(x);}

   /*! \copydoc{su_cs_is_digit()} */
   static boole is_digit(s32 x) {return su_cs_is_digit(x);}

   /*! \copydoc{su_cs_is_graph()} */
   static boole is_graph(s32 x) {return su_cs_is_graph(x);}

   /*! \copydoc{su_cs_is_lower()} */
   static boole is_lower(s32 x) {return su_cs_is_lower(x);}

   /*! \copydoc{su_cs_is_print()} */
   static boole is_print(s32 x) {return su_cs_is_print(x);}

   /*! \copydoc{su_cs_is_punct()} */
   static boole is_punct(s32 x) {return su_cs_is_punct(x);}

   /*! \copydoc{su_cs_is_space()} */
   static boole is_space(s32 x) {return su_cs_is_space(x);}

   /*! \copydoc{su_cs_is_upper()} */
   static boole is_upper(s32 x) {return su_cs_is_upper(x);}

   /*! \copydoc{su_cs_is_white()} */
   static boole is_white(s32 x) {return su_cs_is_white(x);}

   /*! \copydoc{su_cs_is_xdigit()} */
   static boole is_xdigit(s32 x) {return su_cs_is_xdigit(x);}

   /*! \copydoc{su_cs_is_ctype()} */
   static boole is_ctype(s32 x, u32 ct) {return su_cs_is_ctype(x, ct);}

   /*! \copydoc{su_cs_cmp()} */
   static sz cmp(char const *cp1, char const *cp2){
      return su_cs_cmp(cp1, cp2);
   }

   /*! \copydoc{su_cs_cmp_n()} */
   static sz cmp(char const *cp1, char const *cp2, uz n){
      return su_cs_cmp_n(cp1, cp2, n);
   }

   /*! \copydoc{su_cs_cmp_case()} */
   static sz cmp_case(char const *cp1, char const *cp2){
      return su_cs_cmp_case(cp1, cp2);
   }

   /*! \copydoc{su_cs_cmp_case_n()} */
   static sz cmp_case(char const *cp1, char const *cp2, uz n){
      return su_cs_cmp_case_n(cp1, cp2, n);
   }

   /*! \copydoc{su_cs_copy_n()} */
   static char *copy(char *dst, char const *src, uz n){
      return su_cs_copy_n(dst, src, n);
   }

   /*! \copydoc{su_cs_dup_cbuf()} */
   static char *dup(char const *buf, uz len, u32 estate=state::none){
      return su_cs_dup_cbuf(buf, len, estate);
   }

   /*! \copydoc{su_cs_dup()} */
   static char *dup(char const *cp, u32 estate=state::none){
      return su_cs_dup(cp, estate);
   }

   /*! \copydoc{su_cs_find()} */
   static char *find(char const *cp, char const *x) {return su_cs_find(cp, x);}

   /*! \copydoc{su_cs_find_c()} */
   static char *find(char const *cp, char x) {return su_cs_find_c(cp, x);}

   /*! \copydoc{su_cs_hash_cbuf()} */
   static uz hash(char const *buf, uz len) {return su_cs_hash_cbuf(buf, len);}

   /*! \copydoc{su_cs_hash()} */
   static uz hash(char const *cp) {return su_cs_hash(cp);}

   /*! \copydoc{su_cs_hash_case_cbuf()} */
   static uz hash_case(char const *buf, uz len){
      return su_cs_hash_case_cbuf(buf, len);
   }

   /*! \copydoc{su_cs_hash_case()} */
   static uz hash_case(char const *cp) {return su_cs_hash_case(cp);}

#ifdef su_HAVE_MD
   /*! \copydoc{su_cs_hash_strong_setup()} */
   static s32 hash_strong_setup(siphash const *tp, u32 estate=state::none){
      return su_cs_hash_strong_setup(S(struct su_siphash const*,tp), estate);
   }

   /*! \copydoc{su_cs_hash_strong_cbuf()} */
   static uz hash_strong(char const *buf, uz len){
      return su_cs_hash_strong_cbuf(buf, len);
   }

   /*! \copydoc{su_cs_hash_strong()} */
   static uz hash_strong(char const *cp) {return su_cs_hash_strong(cp);}

   /*! \copydoc{su_cs_hash_strong_case_cbuf()} */
   static uz hash_strong_case(char const *buf, uz len){
      return su_cs_hash_strong_case_cbuf(buf, len);
   }

   /*! \copydoc{su_cs_hash_strong_case()} */
   static uz hash_strong_case(char const *cp){
      return su_cs_hash_strong_case(cp);
   }
#endif /* su_HAVE_MD */

   /*! \copydoc{su_cs_len()} */
   static uz len(char const *cp) {return su_cs_len(cp);}

   /*! \copydoc{su_cs_pcopy()} */
   static char *pcopy(char *dst, char const *src){
      return su_cs_pcopy(dst, src);
   }

   /*! \copydoc{su_cs_pcopy_n()} */
   static char *pcopy(char *dst, char const *src, uz n){
      return su_cs_pcopy_n(dst, src, n);
   }

   /*! \copydoc{su_cs_rfind_c()} */
   static char *rfind(char const *cp, char x) {return su_cs_rfind_c(cp, x);}

   /*! \copydoc{su_cs_sep_c()} */
   static char *sep(char **iolist, char sep, boole ignore_empty){
      return su_cs_sep_c(iolist, sep, ignore_empty);
   }

   /*! \copydoc{su_cs_sep_escable_c()} */
   static char *sep_escable(char **iolist, char sep, boole ignore_empty){
      return su_cs_sep_escable_c(iolist, sep, ignore_empty);
   }

   /*! \copydoc{su_cs_starts_with()} */
   static boole starts_with(char const *cp, char const *x){
      return su_cs_starts_with(cp, x);
   }

   /*! \copydoc{su_cs_to_lower()} */
   static s32 to_lower(s32 c) {return su_cs_to_lower(c);}

   /*! \copydoc{su_cs_to_upper()} */
   static s32 to_upper(s32 c) {return su_cs_to_upper(c);}
};
/* }}} */

/*!
 * \ingroup CS
 * \r{auto_type_toolbox} specialization (also \r{cs::toolbox}; \r{su/cs.h})
 */
template<>
class auto_type_toolbox<char*>{
public:
   /*! \_ */
   static type_toolbox<char*> const *get_instance(void){
      return cs::type_toolbox;
   }
};

/*!
 * \ingroup CS
 * \r{auto_type_toolbox} specialization (also \r{cs::toolbox}; \r{su/cs.h})
 */
template<>
class auto_type_toolbox<char const*>{
public:
   /*! \_ */
   static type_toolbox<char const*> const *get_instance(void){
      return cs::const_type_toolbox;
   }
};

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_CS_H */
/* s-it-mode */
