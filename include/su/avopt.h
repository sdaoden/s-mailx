/*@ Command line option parser.
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
#ifndef su_AVOPT_H
#define su_AVOPT_H

/*!
 * \file
 * \ingroup AVOPT
 * \brief \r{AVOPT}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_avopt;

/*!
 * \defgroup AVOPT Command line argument parser
 * \ingroup MISC
 * \brief Command line argument option parser (\r{su/avopt.h})
 *
 * Differences to the POSIX \c{getopt()} standard:
 *
 * \list{\li{
 * Long options are supported.
 * They can be mapped to a short option by adding a \c{;CHAR} suffix to
 * the long option (after the possible \c{:} which indicates an argument).
 * If so, calling \r{su_avopt_parse()} will return the short option equivalence
 * instead of only setting \r{su_avopt::avo_current_long_idx} and returning
 * \r{su_AVOPT_STATE_LONG}.
 * }\li{
 * If long options are used, documentation strings can be included, and dumped
 * via \r{su_avopt_dump_doc()}.
 * They can be appended after a \c{;} suffix that follows the possibly empty
 * short option mapping.
 * See below for examples.
 * }\li{
 * This implementation always differentiates in between
 * \r{su_AVOPT_STATE_ERR_OPT} and \r{su_AVOPT_STATE_ERR_ARG} errors.
 * A leading colon \c{:} in the short option string is thus treated as a normal
 * argument.
 * }\li{
 * Any printable ASCII character except hyphen-minus \c{-} may be used as an
 * option.
 *
 * Long options can neither contain colon \c{:} (indicating the option has an
 * argument), semicolon \c{;} (indicating a short option letter equivalent
 * follows), nor the equal-sign \c{=} (used to separate an option and argument
 * within the same argument vector slot), however.
 *
 * It is best to place the short option colon \c{:} as the first entry in the
 * short option string, otherwise the colon will be mistreated as an argument
 * requirement for the preceeding option.
 * }\li{
 * Error messages are not logged.
 * Instead, the format strings \r{su_avopt_fmt_err_arg} and
 * \r{su_avopt_fmt_err_opt} can be used with an argument of
 * \r{su_avopt::avo_current_err_opt}.
 * }}
 *
 * \cb{
 *   #define N_(X) X
 *   static char const a_sopts[] = "::A:h#";
 *   static char const * const a_lopts[] = {
 *      "resource-files:;:;" N_("control loading of resource files"),
 *      "account:;A;" N_("execute an `account' command"),
 *      "batch-mode;#;" N_("more confined non-interactive setup"),
 *      "long-help;\201;" N_("this listing"),
 *      NIL
 *   };
 *
 *   struct su_avopt avo;
 *   char const *emsg;
 *   s8 i;
 *
 *   su_avopt_setup(&avo, --argc, su_C(char const*const*,++argv),
 *      a_sopts, a_lopts);
 *   while((i = su_avopt_parse(&avo)) != su_AVOPT_STATE_DONE){
 *      switch(i){
 *      case 'A':
 *         "account_name" = avo.avo_current_arg;
 *         break;
 *      case 'h':
 *      case su_S(char,su_S(su_u8,'\201')):
 *         a_main_usage(n_stdout);
 *         if(i != 'h'){
 *            fprintf(n_stdout, "\nLong options:\n");
 *            su_avopt_dump_doc(&avo, &a_main_dump_doc, su_S(su_up,n_stdout));
 *         }
 *         exit(0);
 *      case '#':
 *         n_var_setup_batch_mode();
 *         break;
 *      case su_AVOPT_STATE_ERR_ARG:
 *         emsg = su_avopt_fmt_err_arg;
 *         if(0){
 *            // FALLTHRU
 *      case su_AVOPT_STATE_ERR_OPT:
 *            emsg = su_avopt_fmt_err_opt;
 *         }
 *         fprintf(stderr, emsg, avo.avo_current_err_opt);
 *         exit(1);
 *      }
 *   }
 *
 *   argc = avo.avo_argc;
 *   argv = su_C(char**,avo.avo_argv);
 * }
 *
 * \remarks{Since the return value of \r{su_avopt_parse()} and
 * \r{su_avopt::avo_current_opt} are both \r{su_s8}, the entire range of bytes
 * with the high bit set is available to provide short option equivalences for
 * long options.}
 * @{
 */

/*! \remarks{The values of these constants are ASCII control characters.} */
enum su_avopt_state{
   su_AVOPT_STATE_DONE = '\0', /*!< \_ */
   su_AVOPT_STATE_STOP = '\001', /*!< \_ */
   su_AVOPT_STATE_LONG = '\002', /*!< \_ */
   su_AVOPT_STATE_ERR_ARG = '\003', /*!< \_ */
   su_AVOPT_STATE_ERR_OPT = '\004' /*!< \_ */
};

/*! \remarks{Most fields make sense only after \r{su_avopt_parse()} has been
 * called (at least once).} */
struct su_avopt{
   char const *avo_current_arg; /*!< Current argument or \NIL. */
   s8 avo_current_opt; /*!< Or a \r{su_avopt_state} constant. */
   u8 avo_flags;
   /*! Only useful if \r{su_AVOPT_STATE_LONG} has been returned (and can be
    * found in \r{su_avopt::avo_current_opt}). */
   u16 avo_current_long_idx;
   u32 avo_argc; /*!< Remaining count. */
   char const * const *avo_argv; /*!< Remaining entries. */
   char const *avo_curr;
   char const *avo_opts_short; /*!< Short options as given. */
   char const * const *avo_opts_long; /*!< Long options as given. */
   /*! The current option that lead to an error as a NUL terminated string.
    * In case of long options only this may include the argument, too, i.e.,
    * the entire argument vector entry which caused failure.
    * Only useful if any of \r{su_AVOPT_STATE_ERR_ARG} and
    * \r{su_AVOPT_STATE_ERR_OPT} has occurred. */
   char const *avo_current_err_opt;
   char avo__buf[Z_ALIGN_PZ(2)];
};

/*! \_ */
EXPORT_DATA char const su_avopt_fmt_err_arg[];

/*! \_ */
EXPORT_DATA char const su_avopt_fmt_err_opt[];

/*! One of \a{opts_short_or_nil} and \a{opts_long_or_nil} must be given.
 * The latter must be a \NIL terminated array. */
EXPORT struct su_avopt *su_avopt_setup(struct su_avopt *self,
      u32 argc, char const * const *argv,
      char const *opts_short_or_nil, char const * const *opts_long_or_nil);

/*! Returns either a member of \r{su_avopt_state} to indicate errors and
 * other states, or a short option character.
 * In case of \r{su_AVOPT_STATE_LONG}, \r{su_avopt::avo_current_long_idx}
 * points to the entry in \r{su_avopt::avo_opts_long} that has been
 * detected. */
EXPORT s8 su_avopt_parse(struct su_avopt *self);

/*! If there are long options, query them all in order and dump them via the
 * given pointer to function \a{ptf}.
 * If there is a short option equivalence, \a{sopt} is not the empty string.
 * Options will be preceeded with \c{-} and \c{--}, respectively.
 * Stops when \a{ptf} returns \FAL0, otherwise returns \TRU1.
 *
 * \remarks{The long option string is copied over to a stack buffer of 128
 * bytes (\a{lopt}), any excess is cut off.}
 *
 * \remarks{The documentation string \a{doc} always points to a byte in the
 * corresponding long option string passed in by the user.} */
EXPORT boole su_avopt_dump_doc(struct su_avopt const *self,
      boole (*ptf)(up cookie, boole has_arg, char const *sopt,
         char const *lopt, char const *doc), up cookie);

/*! @} */
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class avopt;

/*!
 * \ingroup AVOPT
 * C++ variant of \r{AVOPT} (\r{su/avopt.h})
 */
class EXPORT avopt : private su_avopt{
   su_CLASS_NO_COPY(avopt);
public:
   /*! \copydoc{su_avopt_state} */
   enum state{
      /*! \copydoc{su_AVOPT_STATE_DONE} */
      state_done = su_AVOPT_STATE_DONE,
      /*! \copydoc{su_AVOPT_STATE_LONG} */
      state_long = su_AVOPT_STATE_LONG,
      /*! \copydoc{su_AVOPT_STATE_ERR_ARG} */
      state_err_arg = su_AVOPT_STATE_ERR_ARG,
      /*! \copydoc{su_AVOPT_STATE_ERR_OPT} */
      state_err_opt = su_AVOPT_STATE_ERR_OPT
   };

   /*! \copydoc{su_avopt_setup()} */
   avopt(u32 argc, char const * const *argv, char const *opts_short,
         char const * const *opts_long=NIL){
      su_avopt_setup(this, argc, argv, opts_short, opts_long);
   }
   /*! \_ */
   ~avopt(void){}

   /*! \copydoc{su_avopt_parse()} */
   s8 parse(void) {return su_avopt_parse(this);}

   /*! \copydoc{su_avopt::avo_current_arg} */
   char const *current_arg(void) const {return avo_current_arg;}

   /*! \copydoc{su_avopt::avo_current_opt} */
   s8 current_opt(void) const {return avo_current_opt;}

   /*! \copydoc{su_avopt::avo_current_long_idx} */
   u16 current_long_idx(void) const {return avo_current_long_idx;}

   /*! \copydoc{su_avopt::avo_current_err_opt} */
   char const *current_err_opt(void) const {return avo_current_err_opt;}

   /*! \copydoc{su_avopt::avo_argc} */
   s32 argc(void) const {return avo_argc;}

   /*! \copydoc{su_avopt::avo_argv} */
   char const * const *argv(void) const {return avo_argv;}

   /*! \copydoc{su_avopt::avo_opts_short} */
   char const *opts_short(void) const {return avo_opts_short;}

   /*! \copydoc{su_avopt::avo_opts_long} */
   char const * const *opts_long(void) const {return avo_opts_long;}

   /*! \copydoc{su_avopt_dump_doc()} */
   boole dump_doc(boole (*ptf)(up cookie, boole has_arg, char const *sopt,
         char const *lopt, char const *doc), up cookie=NIL) const{
      return su_avopt_dump_doc(this, ptf, cookie);
   }
};

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_AVOPT_H */
/* s-it-mode */
