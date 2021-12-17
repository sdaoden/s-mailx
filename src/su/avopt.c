/*@ Implementation of avopt.h.
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
#define su_FILE su_avopt
#define su_SOURCE
#define su_SOURCE_AVOPT

#include "su/code.h"

#include "su/cs.h"
#include "su/mem.h"

#include "su/avopt.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_avopt_flags{
   a_AVOPT_NONE,
   a_AVOPT_DONE = 1u<<0,
   a_AVOPT_STOP = 1u<<1,
   a_AVOPT_SHORT = 1u<<2,
   a_AVOPT_LONG = 1u<<3,
   a_AVOPT_FOLLOWUP = 1u<<4,
   a_AVOPT_CURR_LONG = 1u<<5,
   a_AVOPT_ERR_OPT = 1u<<6,
   a_AVOPT_ERR_ARG = 1u<<7,
   a_AVOPT_ROUND_MASK = a_AVOPT_CURR_LONG | a_AVOPT_ERR_OPT | a_AVOPT_ERR_ARG
};

char const su_avopt_fmt_err_arg[] = N_("option requires argument: %s\n");
char const su_avopt_fmt_err_opt[] = N_("invalid option: %s\n");

struct su_avopt *
su_avopt_setup(struct su_avopt *self, u32 argc, char const * const *argv,
      char const *opts_short_or_nil, char const * const *opts_long_or_nil){
   NYD_IN;
   ASSERT(self);

   su_mem_set(self, 0, sizeof *self);

   ASSERT_NYD_EXEC(argc == 0 || argv != NIL, self->avo_flags = a_AVOPT_DONE);
   ASSERT_NYD_EXEC(opts_short_or_nil != NIL || opts_long_or_nil != NIL,
      self->avo_flags = a_AVOPT_DONE);

   self->avo_flags = (argc == 0 ? a_AVOPT_DONE : a_AVOPT_NONE) |
         (opts_short_or_nil != NIL ? a_AVOPT_SHORT : a_AVOPT_NONE) |
         (opts_long_or_nil != NIL ? a_AVOPT_LONG : a_AVOPT_NONE);
   self->avo_argc = argc;
   self->avo_argv = argv;
   self->avo_opts_short = opts_short_or_nil;
   self->avo_opts_long = opts_long_or_nil;

   /* Somewhat test the content of the option strings in debug code */
#if DVLDBGOR(1, 0)
   /* C99 */{
      uz i;
      char const *cp;

      if(self->avo_flags & a_AVOPT_SHORT){
         if((cp = self->avo_opts_short)[0] == '\0'){
            self->avo_flags &= ~a_AVOPT_SHORT;
            su_log_write(su_LOG_CRIT, "su_avopt_setup(): empty short option "
               "string is unsupported");
         }else do{
            if(!su_cs_is_print(*cp)){
               self->avo_flags &= ~a_AVOPT_SHORT;
               su_log_write(su_LOG_CRIT, "su_avopt_setup(): invalid non-"
                  "printable bytes in short option string: %s",
                  self->avo_opts_short);
            }else if(*cp == '-'){
               self->avo_flags &= ~a_AVOPT_SHORT;
               su_log_write(su_LOG_CRIT, "su_avopt_setup(): invalid "
                  "hyphen-minus in short option string: %s",
                  self->avo_opts_short);
            }
         }while(*++cp != '\0');
      }

      if(self->avo_flags & a_AVOPT_LONG){
         if(self->avo_opts_long[0] == NIL){
            self->avo_flags &= ~a_AVOPT_LONG;
            su_log_write(su_LOG_CRIT, "su_avopt_setup(): empty long option "
               "array is unsupported");
         }else for(i = 0; (cp = self->avo_opts_long[i]) != NIL; ++i){
            if(cp[0] == '\0'){
               self->avo_flags &= ~a_AVOPT_LONG;
               su_log_write(su_LOG_CRIT, "su_avopt_setup(): empty long option "
                  "strings are unsupported");
            }else do{
               if(*cp == ';'){
                  /* Empty option, or no equivalence? */
                  if(cp == self->avo_opts_long[i] || cp[1] == '\0'){
jelong_semicolon:
                     self->avo_flags &= ~a_AVOPT_LONG;
                     su_log_write(su_LOG_CRIT, "su_avopt_setup(): invalid "
                        "semicolon usage in long option string: %s",
                        self->avo_opts_long[i]);
                  }
                  /* Could only indicate documentation string, too */
                  else if(cp[1] == ';'){
                     ;
                  }
                  /* Otherwise we require an equivalence, optionally followed
                   * by documentation */
                  else if(cp[2] != '\0' && cp[2] != ';'){
                     goto jelong_semicolon;
                  }else if(cp[1] == su_AVOPT_STATE_DONE ||
                        cp[1] == su_AVOPT_STATE_LONG ||
                        cp[1] == su_AVOPT_STATE_ERR_OPT ||
                        cp[1] == su_AVOPT_STATE_ERR_ARG){
                     self->avo_flags &= ~a_AVOPT_LONG;
                     su_log_write(su_LOG_CRIT, "su_avopt_setup(): long option "
                        "equivalence byte shadows enum su_avopt_state: %s",
                        self->avo_opts_long[i]);
                  }else if(self->avo_flags & a_AVOPT_SHORT){
                     char const *osp;
                     char sopt;
                     boole wantsopt;

                     wantsopt = (cp[-1] == ':');
                     sopt = cp[1];

                     for(osp = self->avo_opts_short; *osp != '\0'; ++osp){
                        if(*osp != sopt){
                           if(osp[1] == ':')
                              ++osp;
                        }else{
                           if((osp[1] == ':') != wantsopt){
                              self->avo_flags &= ~a_AVOPT_LONG;
                              su_log_write(su_LOG_CRIT, "su_avopt_setup(): "
                                 "long option %s argument, short does%s: %s",
                                 (wantsopt ? "wants" : "does not want"),
                                 (wantsopt ? " not" : su_empty),
                                 self->avo_opts_long[i]);
                           }
                           break;
                        }
                     }
                  }
                  break;
               }else if(*cp == ':'){
                  if(cp == self->avo_opts_long[i])
                     goto jelong_colon;
                  if(cp[1] != '\0'){
                     if(cp[1] == ';')
                        continue;
jelong_colon:
                     self->avo_flags &= ~a_AVOPT_LONG;
                     su_log_write(su_LOG_CRIT, "su_avopt_setup(): invalid "
                        "colon in long option string: %s",
                        self->avo_opts_long[i]);
                  }
                  break;
               }else if(*cp == '='){
                  self->avo_flags &= ~a_AVOPT_LONG;
                  su_log_write(su_LOG_CRIT, "su_avopt_setup(): invalid "
                     "equal-sign in long option string: %s",
                     self->avo_opts_long[i]);
               }else if(!su_cs_is_print(*cp)){
                  self->avo_flags &= ~a_AVOPT_LONG;
                  su_log_write(su_LOG_CRIT, "su_avopt_setup(): invalid non-"
                     "printable bytes in long option string: %s",
                     self->avo_opts_long[i]);
               }
            }while(*++cp != '\0');
         }
      }

      if(!(self->avo_flags & (a_AVOPT_SHORT | a_AVOPT_LONG)))
         self->avo_flags |= a_AVOPT_DONE;
   }
#endif /* DVLDBGOR(1,0) */

   NYD_OU;
   return self;
}

s8
su_avopt_parse(struct su_avopt *self){
   u8 flags;
   char const *opt, *curr;
   s8 rv;
   NYD_IN;
   ASSERT(self);

   rv = su_AVOPT_STATE_DONE;
   curr = self->avo_curr;
   if((flags = self->avo_flags) & a_AVOPT_DONE)
      goto jleave;
   flags &= ~(a_AVOPT_ROUND_MASK);

   /* If follow up is set this necessarily is a short option */
   if(flags & a_AVOPT_FOLLOWUP){
      ASSERT(flags & a_AVOPT_SHORT);
      goto jshort;
   }

   /* We need an argv entry */
   if(self->avo_argc == 0){
      flags |= a_AVOPT_DONE;
      goto jleave;
   }

   /* Finished if not - or a plain -, which is to be kept in argc/argv!
    * Otherwise we consume this argc/argv entry */
   curr = *self->avo_argv;
   if(*curr != '-' || *++curr == '\0'){
      flags |= a_AVOPT_DONE;
      goto jleave;
   }
   --self->avo_argc;
   ++self->avo_argv;

   /* -- stops argument processing unless long options are enabled and
    * anything non-NUL follows */
   if(UNLIKELY(*curr == '-')){
      if(!(flags & a_AVOPT_LONG) || *++curr == '\0'){
         flags |= a_AVOPT_DONE | a_AVOPT_STOP;
         goto jleave;
      }else{
         /* This is a long option */
         char const * const *opta, *xcurr;

         flags |= a_AVOPT_CURR_LONG;
         rv = su_AVOPT_STATE_LONG;

         opta = self->avo_opts_long;
         for(;;){
jlong_outer:
            if((opt = *opta++) == NIL)
               goto jerropt;
            if(*opt != *(xcurr = curr))
               continue;

            while(*++opt != '\0')
               if(*opt == ':' || *opt == ';')
                  break;
               else if(*opt != *++xcurr)
                  goto jlong_outer;

            if(*++xcurr == '\0' || (*xcurr == '=' && *opt == ':'))
               break;
         }

         /* Have it: argument? */
         if(*opt == ':'){
            ++opt;
            if(*xcurr == '=')
               ++xcurr;
            else{
               if(self->avo_argc == 0)
                  goto jerrarg;
               --self->avo_argc;
               xcurr = *self->avo_argv++;
            }
         }else
            xcurr = NIL;
         self->avo_current_arg = xcurr;
         self->avo_current_long_idx = P2UZ(opta - self->avo_opts_long) - 1;

         /* Short option equivalence instead of su_AVOPT_STATE_LONG? */
         if(*opt == ';' && *++opt != ';')
            rv = *opt;
      }
   }else if(UNLIKELY((flags & a_AVOPT_SHORT) == 0))
      goto jerropt;
   else{
      /* A short option */
jshort:
      opt = self->avo_opts_short;
      rv = S(s8,*curr++);

      while(UCMP(8, *opt, !=, rv)){
         /* Skip argument spec. */
         if(opt[1] == ':')
            ++opt;
         /* If we do not know about this, skip entire ARGV entry! */
         if(UNLIKELY(*++opt == '\0'))
            goto jerropt;
      }

      /* Does this take an argument? */
      flags &= ~a_AVOPT_FOLLOWUP;
      if(opt[1] != ':'){
         if(*curr != '\0')
            flags |= a_AVOPT_FOLLOWUP;
         opt = NIL;
      }else{
         if(*curr == '\0'){
            if(self->avo_argc == 0)
               goto jerrarg;
            --self->avo_argc;
            curr = *self->avo_argv++;
         }
         opt = curr;
      }
      self->avo_current_arg = opt;
   }

jleave:
   self->avo_curr = curr;
   self->avo_current_opt = rv;
   self->avo_current_opt =
         (rv != su_AVOPT_STATE_DONE || !(flags & a_AVOPT_STOP)) ? rv
            : su_AVOPT_STATE_STOP;
   self->avo_flags = flags;
   NYD_OU;
   return rv;

jerropt:
   if(flags & a_AVOPT_CURR_LONG)
      self->avo_current_err_opt = curr;
   else{
      self->avo_current_err_opt = self->avo__buf;
      self->avo__buf[0] = S(char,rv);
      self->avo__buf[1] = '\0';
   }
   flags &= ~a_AVOPT_FOLLOWUP;
   flags |= a_AVOPT_ERR_OPT;
   rv = su_AVOPT_STATE_ERR_OPT;
   goto jleave;

jerrarg:
   ASSERT(!(flags & a_AVOPT_FOLLOWUP));
   if(flags & a_AVOPT_CURR_LONG)
      self->avo_current_err_opt = curr;
   else{
      self->avo_current_err_opt = self->avo__buf;
      self->avo__buf[0] = S(char,rv);
      self->avo__buf[1] = '\0';
   }
   flags |= a_AVOPT_ERR_ARG;
   rv = su_AVOPT_STATE_ERR_ARG;
   goto jleave;
}

boole
su_avopt_dump_doc(struct su_avopt const *self,
      boole (*ptf)(up cookie, boole has_arg, char const *sopt,
         char const *lopt, char const *doc), up cookie){
   char s_so[8], s_lo[128];
   char const * const *opta, *opt, *cp;
   uz l;
   boole rv;
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD_EXEC(ptf != NIL, rv = TRU1);

   rv = TRU1;

   if(self->avo_flags & a_AVOPT_LONG){
      s_so[2] = '\0';
      s_lo[0] = s_lo[1] = '-';

      for(opta = self->avo_opts_long; (opt = *opta++) != NIL;){
         cp = opt;

         while(*++opt != '\0')
            if(*opt == ':' || *opt == ';')
               break;
         l = P2UZ(opt - cp);
         if(l > sizeof(s_lo) - 2 -1)
            l = sizeof(s_lo) - 2 -1;
         su_mem_copy(&s_lo[2], cp, l);
         s_lo[l += 2] = '\0';

         /* Have it: argument? */
         if((rv = (*opt == ':')))
            ++opt;

         /* (Actual) Short option equivalence? */
         s_so[0] = '\0';
         if(*opt == ';' && *++opt != ';'){
            if(su_cs_is_print(*opt)){
               s_so[0] = '-';
               s_so[1] = *opt;
            }
            ++opt;
         }

         /* Documentation? */
         if(*opt == ';')
            ++opt;
         else
            opt = su_empty;

         rv = (*ptf)(cookie, rv, s_so, s_lo, opt);
      }
   }
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_AVOPT
/* s-it-mode */
