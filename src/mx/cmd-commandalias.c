/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-commandalias.h.
 *@ TODO - When creating, split arguments and reorder modifiers and command;
 *@ TODO   add structure lookup with modifier bits and actual command: like
 *@ TODO   that "? CA" would print help for real command even with modifiers!
 *@ TODO   Also it would be nicer if modifier order would be normalized!
 *@ TODO - Support vput, i.e.: vput commandalias x what-this-expands-to
 *@ TODO   _CMDAL -> _CCMDAL
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
#define su_FILE cmd_commandalias
#define mx_SOURCE
#define mx_SOURCE_CMD_COMMANDALIAS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>

#include "mx/cmd.h"

#include "mx/cmd-commandalias.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* ..of a_cmdal_dp */
#define a_CMDAL_FLAGS (su_CS_DICT_POW2_SPACED | su_CS_DICT_OWNS |\
      su_CS_DICT_HEAD_RESORT | su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_CMDAL_TRESHOLD_SHIFT 2

static struct su_cs_dict *a_cmdal_dp, a_cmdal__d; /* XXX atexit _gut() (DVL) */

char const *
mx_commandalias_exists(char const *name, char const **expansion_or_nil){
   char const *dat;
   NYD_IN;

   if(a_cmdal_dp == NIL ||
         (dat = S(char*,su_cs_dict_lookup(a_cmdal_dp, name))) == NIL)
      name = NIL;
   else if(expansion_or_nil != NIL)
      *expansion_or_nil = dat;

   NYD_OU;
   return name;
}

int
c_commandalias(void *vp){
   struct su_cs_dict_view dv;
   struct n_string s_b, *s;
   int rv;
   char const **argv, *key;
   NYD_IN;

   if((key = *(argv = vp)) == NIL){
      struct n_strlist *slp;

      slp = NIL;
      rv = !(mx_xy_dump_dict("commandalias", a_cmdal_dp, &slp, NIL,
               &mx_xy_dump_dict_gen_ptf) &&
            mx_page_or_print_strlist("commandalias", slp, FAL0));
      goto jleave;
   }

   /* Verify the name is a valid one, and not a command modifier */
   if(*key == '\0' || *mx_cmd_isolate_name(key) != '\0' ||
         !mx_cmd_is_valid_name(key)){
      n_err(_("commandalias: not a valid command name: %s\n"),
         n_shexp_quote_cp(key, FAL0));
      rv = 1;
      goto jleave;
   }

   if(argv[1] == NIL){
      if(a_cmdal_dp != NIL &&
            su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_cmdal_dp), key)){
         struct n_strlist *slp;

         slp = mx_xy_dump_dict_gen_ptf("commandalias", key,
               su_cs_dict_view_data(&dv));
         rv = (fputs(slp->sl_dat, n_stdout) == EOF);
         rv |= (putc('\n', n_stdout) == EOF);
      }else{
         n_err(_("No such commandalias: %s\n"), n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }
   }else{
      if(a_cmdal_dp == NIL)
         a_cmdal_dp = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_cmdal__d, a_CMDAL_FLAGS, &su_cs_toolbox),
               a_CMDAL_TRESHOLD_SHIFT);

      s = n_string_creat_auto(&s_b);
      s = n_string_book(s, 500); /* xxx magic */
      while(*++argv != NIL){
         if(s->s_len > 0)
            s = n_string_push_c(s, ' ');
         s = n_string_push_cp(s, *argv); /* XXX with SU string, EOVERFLOW++ !*/
      }

      if(su_cs_dict_replace(a_cmdal_dp, key, n_string_cp(s)) <= 0)
         rv = 0;
      else{
         n_err(_("Failed to create `commandalias' storage: %s\n"),
            n_shexp_quote_cp(key, FAL0));
         rv = 1;
      }

      /*n_string_gut(s);*/
   }

jleave:
   NYD_OU;
   return rv;
}

int
c_uncommandalias(void *vp){
   int rv;
   NYD_IN;

   rv = !mx_unxy_dict("commandalias", a_cmdal_dp, vp);

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_COMMANDALIAS
/* s-it-mode */
