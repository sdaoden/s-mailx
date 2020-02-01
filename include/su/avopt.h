/*@ Command line option parser.
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
#ifndef su_AVOPT_H
#define su_AVOPT_H
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
struct su_avopt;
enum su_avopt_state{
   su_AVOPT_STATE_DONE = '\0',
   su_AVOPT_STATE_LONG = '\001',
   su_AVOPT_STATE_ERR_ARG = '\002',
   su_AVOPT_STATE_ERR_OPT = '\003'
};
struct su_avopt{
   char const *avo_current_arg;
   s8 avo_current_opt;
   u8 avo_flags;
   u16 avo_current_long_idx;
   u32 avo_argc;
   char const * const *avo_argv;
   char const *avo_curr;
   char const *avo_opts_short;
   char const * const *avo_opts_long;
   char const *avo_current_err_opt;
   char avo__buf[Z_ALIGN_PZ(2)];
};
EXPORT_DATA char const su_avopt_fmt_err_arg[];
EXPORT_DATA char const su_avopt_fmt_err_opt[];
EXPORT struct su_avopt *su_avopt_setup(struct su_avopt *self,
      u32 argc, char const * const *argv,
      char const *opts_short_or_nil, char const * const *opts_long_or_nil);
EXPORT s8 su_avopt_parse(struct su_avopt *self);
EXPORT boole su_avopt_dump_doc(struct su_avopt const *self,
      boole (*ptf)(up cookie, boole has_arg, char const *sopt,
         char const *lopt, char const *doc), up cookie);
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)
class avopt;
class EXPORT avopt : private su_avopt{
public:
   enum state{
      state_done = su_AVOPT_STATE_DONE,
      state_long = su_AVOPT_STATE_LONG,
      state_err_arg = su_AVOPT_STATE_ERR_ARG,
      state_err_opt = su_AVOPT_STATE_ERR_OPT
   };
   avopt(u32 argc, char const * const *argv, char const *opts_short,
         char const * const *opts_long=NIL){
      su_avopt_setup(this, argc, argv, opts_short, opts_long);
   }
   ~avopt(void){}
   s8 parse(void) {return su_avopt_parse(this);}
   char const *current_arg(void) const {return avo_current_arg;}
   s8 current_opt(void) const {return avo_current_opt;}
   u16 current_long_idx(void) const {return avo_current_long_idx;}
   char const *current_err_opt(void) const {return avo_current_err_opt;}
   s32 argc(void) const {return avo_argc;}
   char const * const *argv(void) const {return avo_argv;}
   char const *opts_short(void) const {return avo_opts_short;}
   char const * const *opts_long(void) const {return avo_opts_long;}
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
