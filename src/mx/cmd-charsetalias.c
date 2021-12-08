/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-charsetalias.h.
 *@ TODO Support vput, i.e.: vput charsetalias x what-this-expands-to
 *@ TODO _CSAL -> _CCSAL
 *
 * Copyright (c) 2017 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_charsetalias
#define mx_SOURCE
#define mx_SOURCE_CMD_CHARSETALIAS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>

#include "mx/cmd.h"
#include "mx/iconv.h"

#include "mx/cmd-charsetalias.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* ..of a_csal_dp */
#define a_CSAL_FLAGS (su_CS_DICT_OWNS | su_CS_DICT_HEAD_RESORT |\
      su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_CSAL_TRESHOLD_SHIFT 4

static struct su_cs_dict *a_csal_dp, a_csal__d;

DVL( static void a_csal__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags); )

#if DVLOR(1, 0)
static void
a_csal__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags){
   NYD2_IN;

   if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM)
      su_cs_dict_gut(&a_csal__d);

   a_csal_dp = NIL;

   NYD2_OU;
}
#endif

int
c_charsetalias(void *vp){
   struct su_cs_dict_view dv;
   int rv;
   char const **argv, *key, *dat;
   NYD_IN;

   if((key = *(argv = vp)) == NIL){
      struct n_strlist *slp;

      slp = NIL;
      rv = !(mx_xy_dump_dict("charsetalias", a_csal_dp, &slp, NIL,
               &mx_xy_dump_dict_gen_ptf) &&
            mx_page_or_print_strlist("charsetalias", slp, FAL0));
   }else if(argv[1] == NIL ||
         (argv[2] == NIL && argv[0][0] == '-' && argv[0][1] == '\0')){
      if(argv[1] != NIL)
         key = argv[1];
      dat = key;

      if((key = n_iconv_normalize_name(key)) != NIL && a_csal_dp != NIL &&
            su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_csal_dp), key)){
         struct n_strlist *slp;

         if(argv[1] == NIL)
            dat = S(char const*,su_cs_dict_view_data(&dv));
         else
            dat = mx_charsetalias_expand(key, TRU1);

         slp = mx_xy_dump_dict_gen_ptf("charsetalias", key, dat);
         rv = (fputs(slp->sl_dat, n_stdout) == EOF);
         rv |= (putc('\n', n_stdout) == EOF);
      }else{
         n_err(_("No such charsetalias: %s\n"), n_shexp_quote_cp(dat, FAL0));
         rv = 1;
      }
   }else{
      if(a_csal_dp == NIL){
         a_csal_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_csal__d, a_CSAL_FLAGS, &su_cs_toolbox),
               a_CSAL_TRESHOLD_SHIFT);
         DVL( su_state_on_gut_install(&a_csal__on_gut, FAL0,
            su_STATE_ERR_NOPASS); )
      }

      for(rv = 0; key != NIL; argv += 2, key = *argv){
         if((key = n_iconv_normalize_name(key)) == NIL){
            n_err(_("charsetalias: invalid source charset %s\n"),
               n_shexp_quote_cp(*argv, FAL0));
            rv = 1;
            continue;
         }else if((dat = argv[1]) == NIL){
            mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("charsetalias"),
               NIL);
            rv = 1;
            break;
         }else if((dat = n_iconv_normalize_name(dat)) == NIL){
            n_err(_("charsetalias: %s: invalid target charset %s\n"),
               n_shexp_quote_cp(argv[0], FAL0),
               n_shexp_quote_cp(argv[1], FAL0));
            rv = 1;
            continue;
         }

         if(su_cs_dict_replace(a_csal_dp, key, C(char*,dat)) > 0){
            n_err(_("Failed to create `charsetalias' storage: %s\n"),
               n_shexp_quote_cp(key, FAL0));
            rv = 1;
         }
      }
   }

   NYD_OU;
   return rv;
}

int
c_uncharsetalias(void *vp){
   char const **argv, *cp, *key;
   int rv;
   NYD_IN;

   rv = 0;
   cp = (argv = vp)[0];

   do{
      if(cp[1] == '\0' && cp[0] == '*'){
         if(a_csal_dp != NIL)
            su_cs_dict_clear(a_csal_dp);
      }else if((key = n_iconv_normalize_name(cp)) == NIL ||
            a_csal_dp == NIL || !su_cs_dict_remove(a_csal_dp, key)){
         n_err(_("No such `charsetalias': %s\n"), n_shexp_quote_cp(cp, FAL0));
         rv = 1;
      }
   }while((cp = *++argv) != NIL);

   NYD_OU;
   return rv;
}

char const *
mx_charsetalias_expand(char const *cp, boole is_normalized){
   uz i;
   char const *cp_orig, *dat;
   NYD_IN;

   cp_orig = cp;

   if(!is_normalized)
      cp = n_iconv_normalize_name(cp);

   if(a_csal_dp != NIL)
      for(i = 0;; ++i){
         if((dat = S(char*,su_cs_dict_lookup(a_csal_dp, cp))) == NIL)
            break;
         cp = dat;
         if(i == 8) /* XXX Magic (same as for `ghost' expansion) */
            break;
      }

   if(cp != cp_orig)
      cp = savestr(cp);
   NYD_OU;
   return cp;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_CHARSETALIAS
/* s-it-mode */
