/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd.h.
 *@ TODO The new cmd_arg_parse() argument list parser is
 *@ TODO too stupid yet, however: it should fully support subcommands, too, so
 *@ TODO that, e.g., "vexpr regex" arguments can be fully prepared by the
 *@ TODO generic parser.  But at least a bit.
 *
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/sort.h>

#include "mx/cmd-charsetalias.h"
#include "mx/cmd-cnd.h"
#include "mx/cmd-commandalias.h"
#include "mx/cmd-csop.h"
#include "mx/cmd-edit.h"
#include "mx/cmd-filetype.h"
#include "mx/cmd-misc.h"
#include "mx/cmd-mlist.h"
#include "mx/cmd-shortcut.h"
#include "mx/cmd-spam.h"
#include "mx/cmd-vexpr.h"
#include "mx/colour.h"
#include "mx/compat.h"
#include "mx/cred-netrc.h"
#include "mx/dig-msg.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/ignore.h"
#include "mx/mailcap.h"
#include "mx/mime-type.h"
#include "mx/mta-aliases.h"
#include "mx/names.h"
#include "mx/privacy.h"
#include "mx/sigs.h"
#include "mx/termios.h"
#include "mx/tty.h"
#include "mx/url.h"

#include "mx/cmd.h"
#include "su/code-in.h"

/* Create a multiline info string about all known additional infos for lcp */
#ifdef mx_HAVE_DOCSTRINGS
static char const *a_cmd_cmdinfo(struct mx_cmd_desc const *cdp);
#endif

/* Print a list of all commands */
static int a_cmd_c_list(void *vp);

/* `help' / `?' command */
static int a_cmd_c_help(void *vp);

/* List of all commands; but first their cmd_arg_desc instances */
#include "mx/cmd-tab.h" /* $(MX_SRCDIR) */
static struct mx_cmd_desc const a_cmd_ctable[] = {
#include <mx/cmd-tab.h>
};

/* And the indexes */
#include "mx/gen-cmd-tab.h" /* $(MX_SRCDIR) */

/* And a list of things which are special to the lexer in go.c, so that we can
 * provide help and list them.
 * This cross-file relationship is a bit unfortunate.. */
#ifdef mx_HAVE_DOCSTRINGS
# define DS(S) , S
#else
# define DS(S)
#endif
static struct mx_cmd_desc const a_cmd_ctable_plus[] = {
   { n_ns, R(int(*)(void*),-1), mx_CMD_ARG_TYPE_STRING, 0, 0, NIL
      DS(N_("Comment command: ignore remaining (continuable) line")) },
   { n_hy, R(int(*)(void*),-1), mx_CMD_ARG_TYPE_WYSH, 0, 0, NIL
      DS(N_("Print out the preceding message")) }
};
#undef DS

#ifdef mx_HAVE_DOCSTRINGS
static char const *
a_cmd_cmdinfo(struct mx_cmd_desc const *cdp){
   struct n_string rvb, *rv;
   char const *cp;
   NYD2_IN;

   rv = n_string_creat_auto(&rvb);
   rv = n_string_reserve(rv, 80);

   switch(cdp->cd_caflags & mx_CMD_ARG_TYPE_MASK){
   case mx_CMD_ARG_TYPE_MSGLIST:
      cp = N_("message-list");
      break;
   case mx_CMD_ARG_TYPE_NDMLIST:
      cp = N_("message-list (without default)");
      break;
   case mx_CMD_ARG_TYPE_STRING:
   case mx_CMD_ARG_TYPE_RAWDAT:
      cp = N_("string data");
      break;
   case mx_CMD_ARG_TYPE_RAWLIST:
      cp = N_("old-style quoting");
      break;
   case mx_CMD_ARG_TYPE_WYRA:
      cp = N_("`wysh' for sh(1)ell-style quoting");
      break;
   case mx_CMD_ARG_TYPE_WYSH:
      cp = (cdp->cd_mflags_o_minargs == 0 && cdp->cd_mmask_o_maxargs == 0)
            ? N_("sh(1)ell-style quoting (takes no arguments)")
            : N_("sh(1)ell-style quoting");
      break;
   default:
   case mx_CMD_ARG_TYPE_ARG:{
      u32 flags, xflags;
      uz i, ol;
      struct mx_cmd_arg_desc const *cadp;

      rv = n_string_push_cp(rv, _("argument tokens: "));

      for(cadp = cdp->cd_cadp, ol = i = 0; i < cadp->cad_no; ++i){
         xflags = flags = cadp->cad_ent_flags[i][0];
jfakeent:
         if(flags & mx_CMD_ARG_DESC_OPTION){
            ++ol;
            rv = n_string_push_c(rv, '[');
         }
         if(i != 0){
            rv = n_string_push_c(rv, ',');
            rv = n_string_push_c(rv, ' ');
         }
         if(flags & mx_CMD_ARG_DESC_GREEDY)
            rv = n_string_push_c(rv, ':');
         switch(flags & mx__CMD_ARG_DESC_TYPE_MASK){
         default:
         case mx_CMD_ARG_DESC_SHEXP:
            rv = n_string_push_cp(rv, _("(shell-)token"));
            break;
         case mx_CMD_ARG_DESC_MSGLIST:
            rv = n_string_push_cp(rv, _("(shell-)msglist"));
            break;
         case mx_CMD_ARG_DESC_NDMSGLIST:
            rv = n_string_push_cp(rv, _("(shell-)msglist (no default)"));
            break;
         case mx_CMD_ARG_DESC_MSGLIST_AND_TARGET:
            rv = n_string_push_cp(rv, _("(shell-)msglist"));
            ++i;
            xflags = mx_CMD_ARG_DESC_SHEXP;
         }
         if(flags & mx_CMD_ARG_DESC_GREEDY)
            rv = n_string_push_c(rv, ':');

         if(xflags != flags){
            flags = xflags;
            goto jfakeent;
         }
      }
      while(ol-- > 0)
         rv = n_string_push_c(rv, ']');
      cp = NIL;
      }break;
   }
   if(cp != NIL)
      rv = n_string_push_cp(rv, V_(cp));

   /* Note: on updates, change the manual! */
   if(cdp->cd_caflags & mx_CMD_ARG_G)
      rv = n_string_push_cp(rv, _(" | `global'"));
   if(cdp->cd_caflags & mx_CMD_ARG_L)
      rv = n_string_push_cp(rv, _(" | `local'"));
   if(cdp->cd_caflags & mx_CMD_ARG_V)
      rv = n_string_push_cp(rv, _(" | `vput'"));
   if(cdp->cd_caflags & mx_CMD_ARG_EM)
      rv = n_string_push_cp(rv, _(" | *!*"));

   if(cdp->cd_caflags & mx_CMD_ARG_A)
      rv = n_string_push_cp(rv, _(" | needs-box"));

   if(cdp->cd_caflags & (mx_CMD_ARG_I | mx_CMD_ARG_M | mx_CMD_ARG_X)){
      rv = n_string_push_cp(rv, _(" | yay:"));
      if(cdp->cd_caflags & mx_CMD_ARG_I)
         rv = n_string_push_cp(rv, _(" batch/interactive"));
      if(cdp->cd_caflags & mx_CMD_ARG_M)
         rv = n_string_push_cp(rv, _(" send-mode"));
      if(cdp->cd_caflags & mx_CMD_ARG_X)
         rv = n_string_push_cp(rv, _(" subprocess"));
   }

   if(cdp->cd_caflags & (mx_CMD_ARG_R | mx_CMD_ARG_S)){
      rv = n_string_push_cp(rv, _(" | nay:"));
      if(cdp->cd_caflags & mx_CMD_ARG_R)
         rv = n_string_push_cp(rv, _(" compose mode"));
      if(cdp->cd_caflags & mx_CMD_ARG_S)
         rv = n_string_push_cp(rv, _(" startup"));
   }

   if(cdp->cd_caflags & mx_CMD_ARG_HGABBY)
      rv = n_string_push_cp(rv, _(" | history:gabby"));
   if(cdp->cd_caflags & mx_CMD_ARG_NOHIST)
      rv = n_string_push_cp(rv, _(" | history:ignored"));

   cp = n_string_cp(rv);
   NYD2_OU;
   return cp;
}
#endif /* mx_HAVE_DOCSTRINGS */

static int
a_cmd_c_list(void *vp){
   FILE *fp;
   struct mx_cmd_desc const **cdpa, *cdp, **cdpa_curr;
   uz i, l, scrwid;
   NYD_IN;
   UNUSED(vp);

   i = NELEM(a_cmd_ctable_plus) + NELEM(a_cmd_ctable) +1;
   cdpa = n_autorec_alloc(sizeof(cdp) * i);

   for(i = 0; i < NELEM(a_cmd_ctable_plus); ++i)
      cdpa[i] = &a_cmd_ctable_plus[i];
   for(l = 0; l < NELEM(a_cmd_ctable); ++i, ++l)
      cdpa[i] = &a_cmd_ctable[l];
   cdpa[i] = NIL;

   if((fp = mx_fs_tmp_open("list", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL)
      fp = n_stdout;

   scrwid = mx_TERMIOS_WIDTH_OF_LISTS();

   fprintf(fp, _("Commands are:\n"));
   l = 1;
   for(i = 0, cdpa_curr = cdpa; (cdp = *cdpa_curr++) != NIL;){
      char const *pre, *suf;

      if(cdp->cd_func == NIL)
         pre = "[", suf = "]";
      else
         pre = suf = n_empty;

#ifdef mx_HAVE_DOCSTRINGS
      if(n_poption & n_PO_D_V){
         fprintf(fp, "%s%s%s\n", pre, cdp->cd_name, suf);
         ++l;
         fprintf(fp, "  : %s%s\n",
            ((cdp->cd_caflags & mx_CMD_ARG_O) ? "OBSOLETE: " : su_empty),
            V_(cdp->cd_doc));
         ++l;
         fprintf(fp, "  : %s\n", (cdp->cd_func != NIL ? a_cmd_cmdinfo(cdp)
            : _("command is not compiled in")));
         ++l;
      }else
#endif
           {
         uz j;

         j = su_cs_len(cdp->cd_name);
         if(*pre != '\0')
            j += 2;

         if((i += j + 2) > scrwid){
            i = j;
            fprintf(fp, "\n");
            ++l;
         }
         fprintf(fp, (*cdpa_curr != NIL ? "%s%s%s, " : "%s%s%s\n"),
            pre, cdp->cd_name, suf);
      }
   }

   if(fp != n_stdout){
      page_or_print(fp, l);

      mx_fs_close(fp);
   }else
      clearerr(fp);

   NYD_OU;
   return n_EXIT_OK;
}

static int
a_cmd_c_help(void *vp){
   int rv;
   char const *arg;
   FILE *fp;
   NYD_IN;

   if((fp = mx_fs_tmp_open("help", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL)
      fp = n_stdout;

   /* Help for a single command? */
   if((arg = *S(char const**,vp)) != NIL){
      struct mx_cmd_desc const *cdp, *cdp_max;
      char const *alias_name, *alias_exp, *aepx;

      /* Aliases take precedence, unless disallowed.
       * Avoid self-recursion; since a commandalias can shadow a command of
       * equal name allow one level of expansion to return an equal result:
       * "commandalias q q;commandalias x q;x" should be "x->q->q->quit" */
      alias_name = NIL;
      if(*arg == '\\')
         ++arg;
      else while((aepx = mx_commandalias_exists(arg, &alias_exp)) != NIL &&
            (alias_name == NIL || su_cs_cmp(alias_name, aepx))){
         alias_name = aepx;
         fprintf(fp, "%s -> ", arg);
         arg = alias_exp;
      }

      cdp_max = &(cdp = a_cmd_ctable)[NELEM(a_cmd_ctable)];
      cdp = &cdp[a_CMD_CIDX(*arg)];

jredo:
      for(; cdp < cdp_max; ++cdp){
         if(cdp->cd_func == NIL || !su_cs_starts_with(cdp->cd_name, arg))
            continue;

         fputs(arg, fp);
         if(su_cs_cmp(arg, cdp->cd_name))
            fprintf(fp, " (%s)", cdp->cd_name);
#ifdef mx_HAVE_DOCSTRINGS
         fprintf(fp, ": %s%s",
            ((cdp->cd_caflags & mx_CMD_ARG_O) ? "OBSOLETE: " : su_empty),
            V_(cdp->cd_doc));
         if(n_poption & n_PO_D_V)
            fprintf(fp, "\n  : %s", a_cmd_cmdinfo(cdp));
#endif
         putc('\n', fp);
         rv = 0;
         goto jleave;
      }

      if(cdp_max == &a_cmd_ctable[NELEM(a_cmd_ctable)]){
         cdp_max = &(cdp =
               a_cmd_ctable_plus)[NELEM(a_cmd_ctable_plus)];
         goto jredo;
      }

      if(alias_name != NIL){
         fprintf(fp, "%s\n", n_shexp_quote_cp(arg, TRU1));
         rv = 0;
      }else{
         n_err(_("Unknown command: `%s'\n"), arg);
         rv = 1;
      }
   }else{
      /* Very ugly, but take care for compiler supported string lengths :( */
#ifdef mx_HAVE_UISTRINGS
      fputs(su_program, fp);
      fputs(_(
         " commands -- <msglist> denotes message specification tokens, e.g.,\n"
         "1-5, :n, @f@Ulf or . (current, the \"dot\"), separated by *ifs*:\n"),
         fp);
      fputs(_(
"\n"
"type <msglist>         type (`print') messages (honour `headerpick' etc.)\n"
"Type <msglist>         like `type' but always show all headers\n"
"next                   goto and type next message\n"
"headers                header summary ... for messages surrounding \"dot\"\n"
"search <msglist>       ... for the given expression list (alias for `from')\n"
"delete <msglist>       delete messages (can be `undelete'd)\n"),
         fp);

      fputs(_(
"\n"
"save <msglist> folder  append messages to folder and mark as saved\n"
"copy <msglist> folder  like `save', but do not mark them (`move' moves)\n"
"write <msglist> file   write message contents to file (prompts for parts)\n"
"Reply <msglist>        reply to message sender(s) only\n"
"reply <msglist>        like `Reply', but address all recipients\n"
"Lreply <msglist>       forced mailing list `reply' (see `mlist')\n"),
         fp);

      fputs(_(
"\n"
"mail <recipients>      compose a mail for the given recipients\n"
"file folder            change to another mailbox\n"
"File folder            like `file', but open readonly\n"
"quit                   quit and apply changes to the current mailbox\n"
"xit or exit            like `quit', but discard changes\n"
"!shell command         shell escape\n"
"list                   show all commands (reacts upon *verbose*)\n"),
         fp);
#endif /* mx_HAVE_UISTRINGS */

      rv = (ferror(fp) != 0);
   }

jleave:
   if(fp != n_stdout){
      page_or_print(fp, 0);

      mx_fs_close(fp);
   }else
      clearerr(fp);

   NYD_OU;
   return rv;
}

char const *
mx_cmd_isolate_name(char const *cmd){
   NYD2_IN;
   while(*cmd != '\0' &&
         su_cs_find_c("\\!~|? \t0123456789&%@$^.:/-+*'\",;(`", *cmd) == NULL)
      ++cmd;
   NYD2_OU;
   return n_UNCONST(cmd);
}

boole
mx_cmd_is_valid_name(char const *cmd){
   /* Mirrors things from go.c */
   static char const a_prefixes[][8] =
         {"global", "ignerr", "local", "wysh", "u", "vput"};
   uz i;
   NYD2_IN;

   i = 0;
   do if(!su_cs_cmp_case(cmd, a_prefixes[i])){
      cmd = NIL;
      break;
   }while(++i < NELEM(a_prefixes));

   NYD2_OU;
   return (cmd != NIL);
}

struct mx_cmd_desc const *
mx_cmd_firstfit(char const *cmd){
   struct mx_cmd_desc const *cdp;
   char c, C, x;
   NYD2_IN;

   C = su_cs_to_upper(c = *cmd);
   cdp = &a_cmd_ctable[a_CMD_CIDX(c)];
   c = su_cs_to_lower(c);

   for(; cdp < &a_cmd_ctable[NELEM(a_cmd_ctable)]; ++cdp)
      if(cdp->cd_func != NIL && su_cs_starts_with(cdp->cd_name, cmd))
         goto jleave;
      else if((x = *cdp->cd_name) != c && x != C)
         break;

   /* This is not called for anything in the _plus table */
   cdp = NIL;
jleave:
   NYD2_OU;
   return cdp;
}

struct mx_cmd_desc const *
mx_cmd_default(void){
   struct mx_cmd_desc const *cdp;
   NYD2_IN;

   cdp = &a_cmd_ctable[a_CMD_DEFAULT_IDX];
   NYD2_OU;
   return cdp;
}

boole
mx_cmd_print_synopsis(struct mx_cmd_desc const *cdp_or_nil, FILE *fp_or_nil){
   char const *name, *doc;
   boole rv;
   NYD2_IN;

   rv = TRU1;
   name = (cdp_or_nil != NIL) ? cdp_or_nil->cd_name : su_empty;
   if((doc = mx_cmd_get_brief_doc(cdp_or_nil)) != NIL)
      doc = V_(doc);

   if(*name != '\0'){
      if(fp_or_nil == NIL)
         n_err(_("Synopsis: %s: %s\n"), name, doc);
      else
         rv = (fprintf(fp_or_nil, _("Synopsis: %s: %s\n"), name, doc) >= 0);
   }

   NYD2_OU;
   return rv;
}

boole
mx_cmd_arg_parse(struct mx_cmd_arg_ctx *cacp){
   enum {a_NONE, a_STOPLOOP = 1u<<0, a_GREEDYJOIN = 1u<<1};

   struct mx_cmd_arg ncap, *lcap, *target_argp, **target_argpp, *cap;
   struct str shin_orig, shin;
   u8 f;
   void const *cookie;
   uz cad_idx, parsed_args;
   struct mx_cmd_arg_desc const *cadp;
   NYD_IN;

   ASSERT(cacp->cac_inlen == 0 || cacp->cac_indat != NULL);
   ASSERT(cacp->cac_desc->cad_no > 0);
#ifdef mx_HAVE_DEBUG
   /* C99 */{
      boole opt_seen = FAL0;

      for(cadp = cacp->cac_desc, cad_idx = 0;
            cad_idx < cadp->cad_no; ++cad_idx){
         ASSERT(cadp->cad_ent_flags[cad_idx][0] & mx__CMD_ARG_DESC_TYPE_MASK);

         /* TODO CMD_ARG_DESC_MSGLIST+ may only be used as the last entry */
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST) ||
            cad_idx + 1 == cadp->cad_no);
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] &
               mx_CMD_ARG_DESC_NDMSGLIST) || cad_idx + 1 == cadp->cad_no);
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] &
               mx_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
            cad_idx + 1 == cadp->cad_no);

         ASSERT(!opt_seen ||
            (cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));
         if(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION)
            opt_seen = TRU1;
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY) ||
            cad_idx + 1 == cadp->cad_no);

         /* TODO CMD_ARG_DESC_MSGLIST+ can only be CMD_ARG_DESC_GREEDY.
          * TODO And they may not be CMD_ARG_DESC_OPTION */
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST) ||
            (cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY));
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] &
               mx_CMD_ARG_DESC_NDMSGLIST) ||
            (cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY));
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] &
               mx_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
            (cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY));

         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST) ||
            !(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] &
               mx_CMD_ARG_DESC_NDMSGLIST) ||
            !(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));
         ASSERT(!(cadp->cad_ent_flags[cad_idx][0] &
               mx_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
            !(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));
      }
   }
#endif /* mx_HAVE_DEBUG */

   n_pstate_err_no = su_ERR_NONE;
   shin.s = n_UNCONST(cacp->cac_indat); /* "logical" only */
   shin.l = (cacp->cac_inlen == UZ_MAX ? su_cs_len(shin.s) : cacp->cac_inlen);
   shin_orig = shin;
   cacp->cac_no = 0;
   cacp->cac_cm_local = FAL0;
   cacp->cac_arg = lcap = NIL;
   cacp->cac_vput = NIL;

   cookie = NULL;
   parsed_args = 0;
   f = a_NONE;

   /* TODO We need to test >= 0 in order to deal with MSGLIST arguments, as
    * TODO those use getmsglist() and that needs to deal with that situation.
    * TODO In the future that should change; see jloop_break TODO below */
   for(cadp = cacp->cac_desc, cad_idx = 0;
         /*shin.l >= 0 &&*/ cad_idx < cadp->cad_no; ++cad_idx){
jredo:
      su_mem_set(&ncap, 0, sizeof ncap);
      ncap.ca_indat = shin.s;
      /* >ca_inline once we know */
      su_mem_copy(&ncap.ca_ent_flags[0], &cadp->cad_ent_flags[cad_idx][0],
         sizeof ncap.ca_ent_flags);
      target_argpp = NULL;
      f &= ~a_STOPLOOP;

      switch(ncap.ca_ent_flags[0] & mx__CMD_ARG_DESC_TYPE_MASK){
      default:
      case mx_CMD_ARG_DESC_SHEXP:{
         struct n_string shou, *shoup;
         BITENUM_IS(u32,n_shexp_state) shs;

         if(shin.l == 0) goto jloop_break; /* xxx (required grrr) quickshot */

         shoup = n_string_creat_auto(&shou);

         ncap.ca_arg_flags =
         shs = n_shexp_parse_token((ncap.ca_ent_flags[1] | n_SHEXP_PARSE_LOG |
               n_SHEXP_PARSE_META_SEMICOLON | n_SHEXP_PARSE_TRIM_SPACE),
               shoup, &shin,
               (ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_GREEDY
                  ? &cookie : NIL));

         if((shs & n_SHEXP_STATE_META_SEMICOLON) && shin.l > 0){
            ASSERT(shs & n_SHEXP_STATE_STOP);
            mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, shin.s, shin.l);
            shin.l = 0;
         }

         ncap.ca_inlen = P2UZ(shin.s - ncap.ca_indat);
         if((shs & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_ERR_MASK)) ==
               n_SHEXP_STATE_OUTPUT){
            ncap.ca_arg.ca_str.s = n_string_cp(shoup);
            ncap.ca_arg.ca_str.l = shou.s_len;
         }

         if(shs & n_SHEXP_STATE_ERR_MASK)
            goto jerr;
         if((shs & n_SHEXP_STATE_STOP) &&
               (ncap.ca_ent_flags[0] & (mx_CMD_ARG_DESC_OPTION |
                  mx_CMD_ARG_DESC_HONOUR_STOP))){
            if(!(shs & n_SHEXP_STATE_OUTPUT)){
               /* We would return FAL0 for bind in "bind;echo huhu" or
                * "reply # comment", whereas we do not for "bind" or "reply"
                * due to the "shin.l==0 goto jloop_break;" introductional
                * quickshot; ensure we succeed */
               if(shs & (n_SHEXP_STATE_STOP | n_SHEXP_STATE_META_SEMICOLON))
                  goto jloop_break;
               goto jleave;
            }

            /* Succeed if we had any arg */
            f |= a_STOPLOOP;
         }else if(!(shs & n_SHEXP_STATE_OUTPUT)){
            ASSERT(0);
            goto jerr;
         }
         }break;
      case mx_CMD_ARG_DESC_MSGLIST_AND_TARGET:
         target_argpp = &target_argp;
         /* FALLTHRU */
      case mx_CMD_ARG_DESC_MSGLIST:
      case mx_CMD_ARG_DESC_NDMSGLIST:
         /* TODO _MSGLIST yet at end and greedy only (fast hack).
          * TODO And consumes too much memory */
         ASSERT(shin.s[shin.l] == '\0');
         if(n_getmsglist(shin.s, (ncap.ca_arg.ca_msglist =
                  n_autorec_calloc(msgCount +1, sizeof *ncap.ca_arg.ca_msglist)
               ), cacp->cac_msgflag, target_argpp) < 0){
            goto jerr;
         }

         if(ncap.ca_arg.ca_msglist[0] == 0){
            u32 e;

            switch(ncap.ca_ent_flags[0] & mx__CMD_ARG_DESC_TYPE_MASK){
            case mx_CMD_ARG_DESC_MSGLIST_AND_TARGET:
            case mx_CMD_ARG_DESC_MSGLIST:
               if((ncap.ca_arg.ca_msglist[0] = first(cacp->cac_msgflag,
                     cacp->cac_msgmask)) == 0){
                  if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
                        (n_poption & n_PO_D_V))
                     n_err(_("No applicable messages\n"));

                  e = mx_CMD_ARG_DESC_TO_ERRNO(ncap.ca_ent_flags[0]);
                  if(e == 0)
                     e = su_ERR_NOMSG;
                  n_pstate_err_no = e;
                  goto jerr;
               }
               ncap.ca_arg.ca_msglist[1] = 0;
               ASSERT(n_msgmark1 == NIL);
               n_msgmark1 = &message[ncap.ca_arg.ca_msglist[0] - 1];

               /* TODO For the MSGLIST_AND_TARGET case an entirely empty input
                * TODO results in no _TARGET argument: ensure it is there! */
               if(target_argpp != NULL && (cap = *target_argpp) == NULL){
                  cap = n_autorec_calloc(1, sizeof *cap);
                  cap->ca_arg.ca_str.s = n_UNCONST(n_empty);
                  *target_argpp = cap;
               }
               /* FALLTHRU */
            default:
               break;
            }
         }else if((ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE
               ) && ncap.ca_arg.ca_msglist[1] != 0){
            if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
                  (n_poption & n_PO_D_V))
               n_err(_("Cannot specify multiple messages at once\n"));
            n_pstate_err_no = su_ERR_NOTSUP;
            goto jerr;
         }
         shin.l = 0;
         f |= a_STOPLOOP; /* XXX Asserted to be last above! */
         break;
      }
      ++parsed_args;

      if(f & a_GREEDYJOIN){ /* TODO speed this up! */
         char *cp;
         uz i;

         ASSERT((ncap.ca_ent_flags[0] & mx__CMD_ARG_DESC_TYPE_MASK
            ) != mx_CMD_ARG_DESC_MSGLIST);
         ASSERT(lcap != NULL);
         ASSERT(target_argpp == NULL);
         i = lcap->ca_arg.ca_str.l;
         lcap->ca_arg.ca_str.l += 1 + ncap.ca_arg.ca_str.l;
         cp = n_autorec_alloc(lcap->ca_arg.ca_str.l +1);
         su_mem_copy(cp, lcap->ca_arg.ca_str.s, i);
         lcap->ca_arg.ca_str.s = cp;
         cp[i++] = ' ';
         su_mem_copy(&cp[i], ncap.ca_arg.ca_str.s, ncap.ca_arg.ca_str.l +1);
      }else{
         cap = n_autorec_alloc(sizeof *cap);
         su_mem_copy(cap, &ncap, sizeof ncap);
         if(lcap == NULL)
            cacp->cac_arg = cap;
         else
            lcap->ca_next = cap;
         lcap = cap;
         ++cacp->cac_no;

         if(target_argpp != NULL){
            lcap->ca_next = cap = *target_argpp;
            if(cap != NULL){
               lcap = cap;
               ++cacp->cac_no;
            }
         }
      }

      if(f & a_STOPLOOP)
         goto jleave;

      if((shin.l > 0 || cookie != NULL) &&
            (ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_GREEDY)){
         if(!(f & a_GREEDYJOIN) && ((ncap.ca_ent_flags[0] &
                  mx_CMD_ARG_DESC_GREEDY_JOIN) &&
               (ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_SHEXP)))
            f |= a_GREEDYJOIN;
         goto jredo;
      }
   }

jloop_break:
   if(cad_idx < cadp->cad_no){
      if(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION))
         goto jerr;
   }else if(!(f & a_STOPLOOP) && shin.l > 0){
      n_pstate_err_no = su_ERR_2BIG;
      goto jerr;
   }

   lcap = (struct mx_cmd_arg*)-1;
jleave:
   NYD_OU;
   return (lcap != NULL);

jerr:
   if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
            (n_poption & n_PO_D_V)){
      if(n_pstate_err_no != su_ERR_NONE)
         n_err(_("%s: %s\n"), cadp->cad_name, su_err_doc(n_pstate_err_no));
      else{
         uz i;

         for(i = 0; (i < cadp->cad_no &&
               !(cadp->cad_ent_flags[i][0] & mx_CMD_ARG_DESC_OPTION)); ++i)
            ;

         n_err(_("%s: parsing stopped after %" PRIuZ " arguments "
               "(need %" PRIuZ "%s)\n"
               "     Input: %.*s\n"
               "   Stopped: %.*s\n"),
            cadp->cad_name, parsed_args,
               i, (i == cadp->cad_no ? su_empty : "+"),
            S(int,shin_orig.l), shin_orig.s,
            S(int,shin.l), shin.s);
      }

      if(!su_state_has(su_STATE_REPRODUCIBLE))
         mx_cmd_print_synopsis(mx_cmd_firstfit(cadp->cad_name), NIL);
   }

   if(n_pstate_err_no == su_ERR_NONE)
      n_pstate_err_no = su_ERR_INVAL;

   lcap = NIL;
   goto jleave;
}

void *
mx_cmd_arg_save_to_heap(struct mx_cmd_arg_ctx const *cacp){
   struct mx_cmd_arg *ncap;
   struct mx_cmd_arg_ctx *ncacp;
   char *buf;
   struct mx_cmd_arg const *cap;
   uz len, i;
   NYD2_IN;

   /* For simplicity, save it all in once chunk, so that it can be thrown away
    * with a simple n_free() from whoever is concerned */
   len = sizeof *cacp;
   for(cap = cacp->cac_arg; cap != NULL; cap = cap->ca_next){
      i = cap->ca_arg.ca_str.l +1;
      i = Z_ALIGN(i);
      len += sizeof(*cap) + i;
   }
   if(cacp->cac_vput != NULL)
      len += su_cs_len(cacp->cac_vput) +1;

   ncacp = n_alloc(len);
   *ncacp = *cacp;
   buf = (char*)&ncacp[1];

   for(ncap = NULL, cap = cacp->cac_arg; cap != NULL; cap = cap->ca_next){
      void *vp;

      vp = buf;
      su_DBG( su_mem_set(vp, 0, sizeof *ncap); )

      if(ncap == NULL)
         ncacp->cac_arg = vp;
      else
         ncap->ca_next = vp;
      ncap = vp;
      ncap->ca_next = NULL;
      ncap->ca_ent_flags[0] = cap->ca_ent_flags[0];
      ncap->ca_ent_flags[1] = cap->ca_ent_flags[1];
      ncap->ca_arg_flags = cap->ca_arg_flags;
      su_mem_copy(ncap->ca_arg.ca_str.s = (char*)&ncap[1],
         cap->ca_arg.ca_str.s,
         (i = (ncap->ca_arg.ca_str.l = cap->ca_arg.ca_str.l) +1));

      i = Z_ALIGN(i);
      buf += sizeof(*ncap) + i;
   }

   if(cacp->cac_vput != NULL){
      ncacp->cac_vput = buf;
      su_mem_copy(buf, cacp->cac_vput, su_cs_len(cacp->cac_vput) +1);
   }else
      ncacp->cac_vput = NULL;
   NYD2_OU;
   return ncacp;
}

struct mx_cmd_arg_ctx *
mx_cmd_arg_restore_from_heap(void *vp){
   struct mx_cmd_arg *cap, *ncap;
   struct mx_cmd_arg_ctx *cacp, *rv;
   NYD2_IN;

   rv = n_autorec_alloc(sizeof *rv);
   cacp = vp;
   *rv = *cacp;

   for(ncap = NULL, cap = cacp->cac_arg; cap != NULL; cap = cap->ca_next){
      vp = n_autorec_alloc(sizeof(*ncap) + cap->ca_arg.ca_str.l +1);
      su_DBG( su_mem_set(vp, 0, sizeof *ncap); )

      if(ncap == NULL)
         rv->cac_arg = vp;
      else
         ncap->ca_next = vp;
      ncap = vp;
      ncap->ca_next = NULL;
      ncap->ca_ent_flags[0] = cap->ca_ent_flags[0];
      ncap->ca_ent_flags[1] = cap->ca_ent_flags[1];
      ncap->ca_arg_flags = cap->ca_arg_flags;
      su_mem_copy(ncap->ca_arg.ca_str.s = (char*)&ncap[1],
         cap->ca_arg.ca_str.s,
         (ncap->ca_arg.ca_str.l = cap->ca_arg.ca_str.l) +1);
   }

   if(cacp->cac_vput != NULL)
      rv->cac_vput = savestr(cacp->cac_vput);
   NYD2_OU;
   return rv;
}

int
getrawlist(boole wysh, char **res_dat, uz res_size,
      char const *line, uz linesize){
   int res_no;
   NYD_IN;

   n_pstate &= ~n_PS_ARGLIST_MASK;

   if(res_size == 0){
      res_no = -1;
      goto jleave;
   }else if(UCMP(z, res_size, >, INT_MAX))
      res_size = INT_MAX;
   else
      --res_size;
   res_no = 0;

   if(!wysh){
      /* And assuming result won't grow input */
      char c2, c, quotec, *cp2, *linebuf;

      linebuf = n_lofi_alloc(linesize);

      for(;;){
         for(; su_cs_is_blank(*line); ++line)
            ;
         if(*line == '\0')
            break;

         if(UCMP(z, res_no, >=, res_size)){
            n_err(_("Too many input tokens for result storage\n"));
            res_no = -1;
            break;
         }

         cp2 = linebuf;
         quotec = '\0';

         /* TODO v15: complete switch in order mirror known behaviour */
         while((c = *line++) != '\0'){
            if(quotec != '\0'){
               if(c == quotec){
                  quotec = '\0';
                  continue;
               }else if(c == '\\'){
                  if((c2 = *line++) == quotec)
                     c = c2;
                  else
                     --line;
               }
            }else if(c == '"' || c == '\''){
               quotec = c;
               continue;
            }else if(c == '\\'){
               if((c2 = *line++) != '\0')
                  c = c2;
               else
                  --line;
            }else if(su_cs_is_blank(c))
               break;
            *cp2++ = c;
         }

         res_dat[res_no++] = savestrbuf(linebuf, P2UZ(cp2 - linebuf));
         if(c == '\0')
            break;
      }

      n_lofi_free(linebuf);
   }else{
      /* sh(1) compat mode.  Prepare shell token-wise */
      struct n_string store;
      struct str input;
      void const *cookie;

      n_string_creat_auto(&store);
      input.s = n_UNCONST(line);
      input.l = linesize;
      cookie = NULL;

      for(;;){
         if(UCMP(z, res_no, >=, res_size)){
            n_err(_("Too many input tokens for result storage\n"));
            res_no = -1;
            break;
         }

         /* C99 */{
            BITENUM_IS(u32,n_shexp_state) shs;

            shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
                  (cookie == NULL ? n_SHEXP_PARSE_TRIM_SPACE : 0) |
                  /* TODO not here in old style n_SHEXP_PARSE_IFS_VAR |*/
                  n_SHEXP_PARSE_META_SEMICOLON), &store, &input, &cookie);

            if((shs & n_SHEXP_STATE_META_SEMICOLON) && input.l > 0){
               ASSERT(shs & n_SHEXP_STATE_STOP);
               mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, input.s, input.l);
            }

            if(shs & n_SHEXP_STATE_ERR_MASK){
               /* Ignore Unicode error, just keep the normalized \[Uu] */
               if((shs & n_SHEXP_STATE_ERR_MASK) != n_SHEXP_STATE_ERR_UNICODE){
                  res_no = -1;
                  break;
               }
            }

            if(shs & n_SHEXP_STATE_OUTPUT){
               res_dat[res_no++] = n_string_cp(&store);
               n_string_drop_ownership(&store);
            }

            if(shs & n_SHEXP_STATE_STOP)
               break;
         }
      }

      n_string_gut(&store);
   }

   if(res_no >= 0)
      res_dat[(uz)res_no] = NULL;
jleave:
   NYD_OU;
   return res_no;
}

#include "su/code-ou.h"
/* s-it-mode */
