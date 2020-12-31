/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of tty.h -- creating prompt, and asking questions.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE tty_prompts
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/mem.h>

#include "mx/cmd.h"
#include "mx/cmd-cnd.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/sigs.h"
#include "mx/termios.h"
#include "mx/ui-str.h"

#ifdef mx_HAVE_COLOUR
# include "mx/colour.h"
#endif

#include "mx/tty.h"
#include "su/code-in.h"

boole
mx_tty_yesorno(char const * volatile prompt, boole noninteract_default){
   boole rv;
   NYD_IN;

   if(!(n_psonce & n_PSO_INTERACTIVE) || (n_pstate & n_PS_ROBOT))
      rv = noninteract_default;
   else{
      uz lsize;
      char *ldat;
      char const *quest;

      rv = FAL0;

      quest = noninteract_default ? _("[yes]/no? ") : _("[no]/yes? ");
      if(prompt == NIL)
         prompt = _("Continue");
      prompt = savecatsep(prompt, ' ', quest);

      mx_fs_linepool_aquire(&ldat, &lsize);
      while(mx_go_input(mx_GO_INPUT_CTX_DEFAULT | mx_GO_INPUT_NL_ESC, prompt,
               &ldat, &lsize, NIL,NIL) >= 0){
         boole x;

         x = n_boolify(ldat, UZ_MAX, noninteract_default);
         if(x >= FAL0){
            rv = x;
            break;
         }
      }
      mx_fs_linepool_release(ldat, lsize);
   }

   NYD_OU;
   return rv;
}

#ifdef mx_HAVE_NET
char *
mx_tty_getuser(char const * volatile query) /* TODO v15-compat obsolete */
{
   uz lsize;
   char *ldat, *user;
   NYD_IN;

   if (query == NULL)
      query = _("User: ");

   mx_fs_linepool_aquire(&ldat, &lsize);
   if(mx_go_input(mx_GO_INPUT_CTX_DEFAULT | mx_GO_INPUT_NL_ESC, query,
         &ldat, &lsize, NIL, NIL) >= 0)
      user = savestr(ldat);
   else
      user = NIL;
   mx_fs_linepool_release(ldat, lsize);

   NYD_OU;
   return user;
}

char *
mx_tty_getpass(char const *query){
   uz lsize;
   char *ldat, *pass;
   NYD_IN;

   pass = NIL;

   if(n_psonce & n_PSO_TTYANY){
      if(query == NIL)
         query = _("Password: ");

      mx_termios_cmdx(mx_TERMIOS_CMD_PUSH | mx_TERMIOS_CMD_PASSWORD);

      fputs(query, mx_tty_fp);
      fflush(mx_tty_fp);

      mx_fs_linepool_aquire(&ldat, &lsize);
      if(readline_restart(mx_tty_fp, &ldat, &lsize, 0) >= 0)
         pass = savestr(ldat);
      mx_fs_linepool_release(ldat, lsize);

      mx_termios_cmdx(mx_TERMIOS_CMD_POP | mx_TERMIOS_CMD_PASSWORD);

      putc('\n', mx_tty_fp);
   }

   NYD_OU;
   return pass;
}
#endif /* mx_HAVE_NET */

boole
mx_tty_getfilename(struct n_string *store,
      BITENUM_IS(u32,mx_go_input_flags) gif,
      char const *prompt_or_nil, char const *init_content_or_nil){
   char const *cp;
   boole rv;
   NYD_IN;

   if((n_psonce & (n_PSO_INTERACTIVE | n_PSO_GETFILENAME_QUOTE_NOTED)
         ) == n_PSO_INTERACTIVE){
      n_psonce |= n_PSO_GETFILENAME_QUOTE_NOTED;
      fprintf(n_stdout,
         _("# All file names need to be sh(1)ell-style quoted, everywhere\n"));
   }

   store = n_string_trunc(store, 0);
   if((cp = mx_go_input_cp(gif, prompt_or_nil, init_content_or_nil)) != NIL)
      rv = n_shexp_unquote_one(store, cp);
   else
      rv = TRU2;

   NYD_OU;
   return rv;
}

u32
mx_tty_create_prompt(struct n_string *store, char const *xprompt,
      BITENUM_IS(u32,mx_go_input_flags) gif){
   struct mx_visual_info_ctx vic;
   struct str in, out;
   u32 pwidth, poff;
   char const *cp;
   NYD2_IN;
   ASSERT(n_psonce & n_PSO_INTERACTIVE);

#ifdef mx_HAVE_ERRORS
   if(!(n_psonce & n_PSO_ERRORS_NOTED) && n_pstate_err_cnt > 0){
      n_psonce |= n_PSO_ERRORS_NOTED;
      n_err(_("There are messages in the error ring, "
         "manageable via `errors' command\n"));
   }
#endif

jredo:
   n_string_trunc(store, 0);

   if(gif & mx_GO_INPUT_PROMPT_NONE){
      pwidth = poff = 0;
      goto jleave;
   }

   if(!(gif & mx_GO_INPUT_NL_FOLLOW)){
      boole x;

      if((x = mx_cnd_if_exists())){
         if(store->s_len != 0)
            store = n_string_push_c(store, '#');
         if(x == TRUM1)
            store = n_string_push_cp(store, _("WHITEOUT#"));
         store = n_string_push_cp(store, _("NEED `endif'"));
      }
   }

   if((poff = store->s_len) != 0){
      ++poff;
      store = n_string_push_c(store, '#');
      store = n_string_push_c(store, ' ');
   }

   cp = (gif & mx_GO_INPUT_PROMPT_EVAL)
         ? (gif & mx_GO_INPUT_NL_FOLLOW ? ok_vlook(prompt2) : ok_vlook(prompt))
         : xprompt;
   if(cp != NIL && *cp != '\0'){
      BITENUM_IS(u32,n_shexp_state) shs;

      store = n_string_push_cp(store, cp);
      in.s = n_string_cp(store);
      in.l = store->s_len;
      out = in;
      store = n_string_drop_ownership(store);

      shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
            n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
            n_SHEXP_PARSE_QUOTE_AUTO_DSQ), store, &in, NIL);
      if((shs & n_SHEXP_STATE_ERR_MASK) || !(shs & n_SHEXP_STATE_STOP)){
         store = n_string_clear(store);
         store = n_string_take_ownership(store, out.s, out.l +1, out.l);
jeeval:
         n_err(_("*prompt2?* evaluation failed, actively unsetting it\n"));
         if(gif & mx_GO_INPUT_NL_FOLLOW)
            ok_vclear(prompt2);
         else
            ok_vclear(prompt);
         goto jredo;
      }

      if(!store->s_auto)
         su_FREE(out.s);
   }

   /* Make all printable TODO not know, we want to pass through ESC/CSI! */
#if 0
   in.s = n_string_cp(store);
   in.l = store->s_len;
   mx_makeprint(&in, &out);
   store = n_string_assign_buf(store, out.s, out.l);
   su_FREE(out.s);
#endif

   /* We need the visual width.. */
   su_mem_set(&vic, 0, sizeof vic);
   vic.vic_indat = n_string_cp(store);
   vic.vic_inlen = store->s_len;
   for(pwidth = 0; vic.vic_inlen > 0;){
      /* but \[ .. \] is not taken into account */
      if(vic.vic_indat[0] == '\\' && vic.vic_inlen > 1 &&
            vic.vic_indat[1] == '['){
         uz i;

         i = P2UZ(vic.vic_indat - store->s_dat);
         store = n_string_cut(store, i, 2);
         cp = &n_string_cp(store)[i];
         i = store->s_len - i;
         for(;; ++cp, --i){
            if(i < 2){
               n_err(_("Open \\[ sequence not closed in *prompt2?*\n"));
               goto jeeval;
            }
            if(cp[0] == '\\' && cp[1] == ']')
               break;
         }
         i = P2UZ(cp - store->s_dat);
         store = n_string_cut(store, i, 2);
         vic.vic_indat = &n_string_cp(store)[i];
         vic.vic_inlen = store->s_len - i;
      }else if(!mx_visual_info(&vic, mx_VISUAL_INFO_WIDTH_QUERY |
            mx_VISUAL_INFO_ONE_CHAR)){
         n_err(_("Character set error in evaluation of *prompt2?*\n"));
         goto jeeval;
      }else{
         pwidth += S(u32,vic.vic_vi_width);
         vic.vic_indat = vic.vic_oudat;
         vic.vic_inlen = vic.vic_oulen;
      }
   }

   /* And there may be colour support, too */
#ifdef mx_HAVE_COLOUR
   if(mx_COLOUR_IS_ACTIVE()){
      struct mx_colour_pen *ccp;
      struct str const *rsp, *psp, *esp;

      psp = NIL;
      if((rsp = mx_colour_reset_to_str()) != NIL &&
         (ccp = mx_colour_pen_create(mx_COLOUR_ID_MLE_PROMPT, NIL)) != NIL &&
            (psp = mx_colour_pen_to_str(ccp)) != NIL){
         store = n_string_insert_buf(store, poff, psp->s, psp->l);
         store = n_string_push_buf(store, rsp->s, rsp->l);
      }

      if(poff > 0 && rsp != NIL &&
            (((ccp = mx_colour_pen_create(mx_COLOUR_ID_MLE_ERROR, NIL)
               ) != NIL &&
             (esp = mx_colour_pen_to_str(ccp)) != NIL) || (esp = psp) != NIL)){
         store = n_string_insert_buf(store, poff, rsp->s, rsp->l);
         store = n_string_unshift_buf(store, esp->s, esp->l);
      }
   }
#endif /* mx_HAVE_COLOUR */

jleave:
   NYD2_OU;
   return pwidth;
}

#include "su/code-ou.h"
/* s-it-mode */
