/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of shortcut.h.
 *@ TODO Support vput, i.e.: vput shorcut x what-this-expands-to
 *
 * Copyright (c) 2017 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE shortcut
#define mx_SOURCE
#define mx_SOURCE_SHORTCUT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>

#include "mx/shortcut.h"
#include "su/code-in.h"

/* ..of a_scut_dp */
#define a_SCUT_FLAGS (su_CS_DICT_OWNS | su_CS_DICT_HEAD_RESORT |\
      su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_SCUT_TRESHOLD_SHIFT 2

struct su_cs_dict *a_scut_dp, a_scut__d; /* XXX atexit _gut() (DVL()) */

static boole a_scut_print(FILE *fp, char const *key, char const *dat);

static boole
a_scut_print(FILE *fp, char const *key, char const *dat){
   boole rv;
   NYD2_IN;

   fprintf(fp, "shortcut %s %s\n",
      n_shexp_quote_cp(key, TRU1), n_shexp_quote_cp(dat, TRU1));
   rv = (ferror(fp) == 0);
   NYD2_OU;
   return rv;
}

FL int
c_shortcut(void *vp){
   struct su_cs_dict_view dv;
   int rv;
   char const **argv, *key, *dat;
   NYD_IN;

   if((key = *(argv = vp)) == NIL)
      rv = !mx_show_sorted_dict("shortcut", a_scut_dp,
            R(boole(*)(FILE*,char const*,void const*),&a_scut_print), NIL);
   else if(argv[1] == NIL){
      if(a_scut_dp != NIL &&
            su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_scut_dp), key)){
         dat = S(char const*,su_cs_dict_view_data(&dv));
         rv = !a_scut_print(n_stdout, key, dat);
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
            n_err(_("Synopsis: shortcut: <shortcut> <expansion>\n"));
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

FL int
c_unshortcut(void *vp){
   int rv;
   NYD_IN;

   rv = !mx_unxy_dict("shortcut", a_scut_dp, vp);
   NYD_OU;
   return rv;
}

FL char const *
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
/* s-it-mode */
