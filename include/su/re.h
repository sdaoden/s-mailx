/*@ (POSIX) Regular expressions.
 *
 * Copyright (c) 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_RE_H
#define su_RE_H

/*!
 * \file
 * \ingroup RE
 * \brief \r{RE}
 */

#include <su/code.h>
#ifdef su_HAVE_RE

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_re;
struct su_re_match;

/*!
 * \defgroup RE Regular expressions
 * \ingroup TEXT
 * \brief Regular expressions (\r{su/re.h})
 *
 * If \r{su_HAVE_RE} is defined POSIX regular expressions are available.
 * @{ */

/*! \_ */
enum su_re_setup_flags{
   su_RE_SETUP_NONE = 0, /*!< This is 0. */
   su_RE_SETUP_EXT = 1u<<0, /*!< Use POSIX extended syntax. */
   su_RE_SETUP_EXTENDED = su_RE_SETUP_EXT, /*!< Equals \r{su_RE_SETUP_EXT}. */
   su_RE_SETUP_ICASE = 1u<<1, /*!< Match case-insensitively. */
   su_RE_SETUP_NONL = 1u<<2, /*!< \em ANY matches do not match newlines. */
   su_RE_SETUP_TEST_ONLY = 1u<<3 /*!< Do not create match position reports. */
};

/*! \_ */
enum su_re_eval_flags{
   su_RE_EVAL_NONE = 0, /*!< This is 0. */
   su_RE_EVAL_NOTBOL = 1u<<0, /*!< Begin-of-line does not match. */
   su_RE_EVAL_NOTEOL = 1u<<1 /*!< End-of-line does not match. */
};

/*! \_ */
enum su_re_errors{
   su_RE_ERROR_NONE = 0, /*!< No error: this is 0. */
   su_RE_ERROR_BADBR, /*!< Invalid use of back reference. */
   su_RE_ERROR_BADPAT, /*!< Invalid use of pattern such as group or list. */
   su_RE_ERROR_BADRPT, /*!< Invalid use of repetition, like using "*" first. */
   su_RE_ERROR_BRACE, /*!< Brace imbalanced. */
   su_RE_ERROR_BRACK, /*!< Bracket list imbalanced. */
   su_RE_ERROR_COLLATE, /*!< Invalid collation element. */
   su_RE_ERROR_CTYPE, /*!< Unknown character class name. */
   su_RE_ERROR_ESCAPE, /*!< Trailing backslash. */
   su_RE_ERROR_PAREN, /*!< Parenthesis group imbalanced. */
   su_RE_ERROR_RANGE, /*!< Invalid use of range (invalid endpoint). */
   su_RE_ERROR_SPACE, /*!< Regular expression routines ran out of memory. */
   su_RE_ERROR_SUBREG, /*!< Invalid reference to subexpression group. */
   su_RE_ERROR_MISC /*!< Non-specific error, like pattern space too small. */
};

/*! \_ */
struct su_re{
    /*! \r{su_re_setup_flags} as given to \r{su_re_setup_cp()}. */
   BITENUM_IS(u8,su_re_setup_flags) re_setup_flags;
   /*! After \r{su_re_setup_cp()}: one of the \r{su_re_errors}. */
   BITENUM_IS(u8,su_re_errors) re_error; /* (to make it fit in 8-bit) */
   boole re_eval_ok; /*!< Whether last \r{su_re_eval_cp()} matched. */
    /*! \r{su_re_eval_flags} as given to last \r{su_re_eval_cp()}. */
   BITENUM_IS(u8,su_re_eval_flags) re_eval_flags;
   /*! Number of parenthesized subexpression groups found by
    * \r{su_re_setup_cp()}, and entries in \r{#re_match} (starting at index 1).
    * If \r{su_RE_SETUP_TEST_ONLY} was given this is always 0. */
   u32 re_group_count;
   /*! Match position array unless disabled via \r{su_RE_SETUP_TEST_ONLY}.
    * It contains \r{#re_group_count} entries, plus the entry at index 0,
    * which always exists, then, and denotes the overall matching string.
    * The content is accessible after a \r{su_re_eval_cp()} if \r{#re_eval_ok},
    * subexpression groups which did not match have their fields set to -1. */
   struct su_re_match *re_match;
   char const *re_input; /*!< Buffer pointer given to \r{su_re_eval_cp()}. */
   void *re_super;
};

/*! Entries which did not match have their fields set to -1. */
struct su_re_match{
   s64 rem_start; /*!< The starting byte offset in \r{su_re::re_input}. */
   s64 rem_end; /*!< The ending byte offset. */
};

EXPORT struct su_re *su__re_reset(struct su_re *self);
EXPORT char const *su__re_error_doc(u8 error);

/*! \_ */
INLINE struct su_re *su_re_create(struct su_re *self){
   ASSERT(self);
   self->re_error = su_RE_ERROR_NONE;
   self->re_super = NIL;
   return self;
}

/*! \_ */
INLINE void su_re_gut(struct su_re *self){
   ASSERT(self);
   if(self->re_super != NIL)
      su__re_reset(self);
}

/*! Reset any allocation and the \r{su_re_is_setup()} state. */
INLINE struct su_re *su_re_reset(struct su_re *self){
   ASSERT(self);
   if(self->re_super != NIL)
      self = su__re_reset(self);
   return self;
}

/*! Unless \r{su_RE_SETUP_TEST_ONLY} was set in \a{flags} parenthesized
 * subexpression group matches will be created, accessible via
 * \r{su_re::re_match}, \r{su_re::re_group_count} will denote the number of
 * found and accessible match groups, and the overall matching string will be
 * tracked in the first index of \r{su_re::re_match} (not counted against
 * matches).
 * A \r{su_re_errors} is (stored in \r{su_re::re_error} and) returned,
 * and \r{su_re_error_doc()} might be used; memory is allocated with
 * \r{su_MEM_ALLOC_MAYFAIL} and errors are mapped to \r{su_RE_ERROR_SPACE}. */
EXPORT BITENUM_IS(u8,su_re_errors) su_re_setup_cp(struct su_re *self,
      char const *expr, BITENUM_IS(u8,su_re_setup_flags) flags);

/*! This condition is asserted by most functions below. */
INLINE boole su_re_is_setup(struct su_re const *self){
   ASSERT(self);
   return (self->re_super != NIL);
}

/*! Return setup failure string; does not assert \r{su_re_is_setup()}. */
INLINE char const *su_re_error_doc(struct su_re const *self){
   ASSERT(self);
   return su__re_error_doc(self->re_error);
}

/*! Evaluate whether \a{input} is matched by a \r{su_re_is_setup()} \SELF.
 * \r{su_re::re_eval_ok} and \r{su_re::re_input} will always be set, but
 * \r{su_re::re_match} is only upon success (if so configured). */
EXPORT boole su_re_eval_cp(struct su_re *self, char const *input,
      BITENUM_IS(u8,su_re_eval_flags) flags);

/*! \_ */
INLINE char const *su_re_get_error_doc(enum su_re_errors error){
   return su__re_error_doc(error);
}

/*! @} */
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class re;
//class re::match;

/*!
 * \ingroup RE
 * C++ variant of \r{RE} (\r{su/re.h})
 */
class EXPORT re : private su_re{
   su_CLASS_NO_COPY(re);
public:
   class match;

   /*! \copydoc{su_re_match} */
   class match : private su_re_match{
      friend class re;
   protected:
      match(void) {}
   public:
      ~match(void) {}

      /*! \_ */
      boole is_valid(void) const {return (rem_start != -1);}
      /*! \_ */
      s64 start(void) const {return rem_start;}
      /*! \_ */
      s64 end(void) const {return rem_end;}
   };

private:
#ifndef DOXYGEN
   CXXCAST(match, struct su_re_match);
#endif
public:

   /*! \copydoc{su_re_setup_flags} */
   enum setup_flags{
      setup_none = su_RE_SETUP_NONE, /*!< \copydoc{su_RE_SETUP_NONE} */
      setup_ext = su_RE_SETUP_EXT, /*!< \copydoc{su_RE_SETUP_EXT} */
      setup_extended = setup_ext, /*!< Equals \r{#setup_ext}. */
      setup_icase = su_RE_SETUP_ICASE, /*!< \copydoc{su_RE_SETUP_ICASE} */
      setup_nonl = su_RE_SETUP_NONL, /*!< \copydoc{su_RE_SETUP_NONL} */
       /*! \copydoc{su_RE_SETUP_TEST_ONLY} */
      setup_test_only = su_RE_SETUP_TEST_ONLY
   };

   /*! \copydoc{su_re_eval_flags} */
   enum eval_flags{
      eval_none = su_RE_EVAL_NONE, /*!< \copydoc{su_RE_EVAL_NONE} */
      eval_notbol = su_RE_EVAL_NOTBOL, /*!< \copydoc{su_RE_EVAL_NOTBOL} */
      eval_noteol = su_RE_EVAL_NOTEOL /*!< \copydoc{su_RE_EVAL_NOTEOL} */
   };

   /*! \copydoc{su_re_errors} */
   enum errors{
      error_none = su_RE_ERROR_NONE, /*!< \copydoc{su_RE_ERROR_NONE} */
      error_badbr = su_RE_ERROR_BADBR, /*!< \copydoc{su_RE_ERROR_BADBR} */
      error_badpat = su_RE_ERROR_BADPAT, /*!< \copydoc{su_RE_ERROR_BADPAT} */
      error_badrpt = su_RE_ERROR_BADRPT, /*!< \copydoc{su_RE_ERROR_BADRPT} */
      error_brace = su_RE_ERROR_BRACE, /*!< \copydoc{su_RE_ERROR_BRACE} */
      error_brack = su_RE_ERROR_BRACK, /*!< \copydoc{su_RE_ERROR_BRACK} */
      error_collate = su_RE_ERROR_COLLATE, /*!< \copydoc{su_RE_ERROR_COLLATE}*/
      error_ctype = su_RE_ERROR_CTYPE, /*!< \copydoc{su_RE_ERROR_CTYPE} */
      error_escape = su_RE_ERROR_ESCAPE, /*!< \copydoc{su_RE_ERROR_ESCAPE} */
      error_paren = su_RE_ERROR_PAREN, /*!< \copydoc{su_RE_ERROR_PAREN} */
      error_range = su_RE_ERROR_RANGE, /*!< \copydoc{su_RE_ERROR_RANGE} */
      error_space = su_RE_ERROR_SPACE, /*!< \copydoc{su_RE_ERROR_SPACE} */
      error_subreg = su_RE_ERROR_SUBREG, /*!< \copydoc{su_RE_ERROR_SUBREG} */
      error_misc = su_RE_ERROR_MISC /*!< \copydoc{su_RE_ERROR_MISC} */
   };

   /*! \copydoc{su_re_create()} */
   re(void) {su_re_create(this);}

   /*! \copydoc{su_re_gut()} */
   ~re(void) {su_re_gut(this);}

   /*! \copydoc{su_re_reset()} */
   re &reset(void) {SELFTHIS_RET(su_re_reset(this));}

   /*! \copydoc{su_re_setup_cp()}.
    * \remarks{Sets \r{#setup_ext} by default.} */
   BITENUM_IS(u8,errors) setup(char const *expr,
         BITENUM_IS(u8,setup_flags) flags=setup_ext){
      return S(errors,su_re_setup_cp(this, expr, S(u8,flags)));
   }

   /*! \copydoc{su_re_is_setup()} */
   boole is_setup(void) const {return su_re_is_setup(this);}

   /*! \copydoc{su_re::re_error} */
   errors error(void) const {return S(errors,re_error);}

   /*! \copydoc{su_re_error_doc()} */
   char const *error_doc(void) const {return su_re_error_doc(this);}

   /*! Whether \r{#setup_test_only} was not used. */
   boole is_test_only(void) const{
      return ((re_setup_flags & setup_test_only) != 0);
   }

   /*! \copydoc{su_re::re_group_count} */
   uz group_count(void) const {return re_group_count;}

   /*! \copydoc{su_re_eval_cp()} */
   boole eval(char const *input, BITENUM_IS(u8,eval_flags) flags=eval_none){
      return su_re_eval_cp(this, input, S(u8,flags));
   }

   /*! \copydoc{su_re::re_eval_ok} */
   boole eval_ok(void) const {return re_eval_ok;}

   /*! Get \r{match} for group \a{no}, which is asserted to be valid.
    * \NIL can thus be returned. */
   match const *match_at(uz no) const{
      ASSERT_RET(!is_test_only(), NIL);
      ASSERT_RET(no <= group_count(), NIL);
      return R(match const*,&re_match[no]);
   }

   /*! \copydoc{su_re::re_input} */
   char const *input(void) const{
      ASSERT_RET(is_setup(), NIL);
      return re_input;
   }

   /*! \copydoc{su_re_get_error_doc()} */
   static char const *get_error_doc(errors error){
      return su_re_get_error_doc(S(su_re_errors,error));
   }
};

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_HAVE_RE */
#endif /* !su_RE_H */
/* s-it-mode */
