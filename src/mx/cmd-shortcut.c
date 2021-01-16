/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-shortcut.h.
 *@ TODO Support vput, i.e.: vput shortcut x what-this-expands-to
 *@ TODO _SCUT -> _CSCUT
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
#define su_FILE cmd_shortcut
#define mx_SOURCE
#define mx_SOURCE_CMD_SHORTCUT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>

#include "mx/cmd.h"

#include "mx/cmd-shortcut.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* ..of a_scut_dp */
#define a_SCUT_FLAGS (su_CS_DICT_OWNS | su_CS_DICT_HEAD_RESORT |\
      su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_SCUT_TRESHOLD_SHIFT 2

static struct su_cs_dict *a_scut_dp, a_scut__d; /* XXX atexit _gut() (DVL()) */

int
c_shortcut(void *vp){
   struct su_cs_dict_view dv;
   int rv;
   char const **argv, *key, *dat;
   NYD_IN;

   if((key = *(argv = vp)) == NIL){
      struct n_strlist *slp;

      slp = NIL;
      rv = !(mx_xy_dump_dict("shortcut", a_scut_dp, &slp, NIL,
               &mx_xy_dump_dict_gen_ptf) &&
            mx_page_or_print_strlist("shortcut", slp, FAL0));
   }else if(argv[1] == NIL){
      if(a_scut_dp != NIL &&
            su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_scut_dp), key)){
         struct n_strlist *slp;

         slp = mx_xy_dump_dict_gen_ptf("shortcut", key,
               su_cs_dict_view_data(&dv));
         rv = (fputs(slp->sl_dat, n_stdout) == EOF);
         rv |= (putc('\n', n_stdout) == EOF);
      }else{
         n_err(_("No such shortcut: %s\n"), n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }
   }else{
      if(a_scut_dp == NIL)
         a_scut_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_scut__d, a_SCUT_FLAGS, &su_cs_toolbox),
               a_SCUT_TRESHOLD_SHIFT);

      for(rv = 0; key != NIL; argv += 2, key = *argv){
         if((dat = argv[1]) == NIL){
            mx_cmd_print_synopsis(mx_cmd_firstfit("shortcut"), NIL);
            rv = 1;
            break;
         }

         if(su_cs_dict_replace(a_scut_dp, key, C(char*,dat)) > 0){
            n_err(_("Failed to create `shortcut' storage: %s\n"),
               n_shexp_quote_cp(key, FAL0));
            rv = 1;
         }
      }
   }

   NYD_OU;
   return rv;
}

int
c_unshortcut(void *vp){
   int rv;
   NYD_IN;

   rv = !mx_unxy_dict("shortcut", a_scut_dp, vp);
   NYD_OU;
   return rv;
}

char const *
mx_shortcut_expand(char const *cp){
   NYD_IN;

   if(a_scut_dp != NIL)
      cp = S(char*,su_cs_dict_lookup(a_scut_dp, cp));
   else
      cp = NIL;
   NYD_OU;
   return cp;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_SHORTCUT
/* s-it-mode */
