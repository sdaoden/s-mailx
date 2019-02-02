/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ n_cmd_firstfit(): the table of commands + `help' and `list'.
 *@ And n_cmd_arg_parse(), the (new) argument list parser. TODO this is
 *@ TODO too stupid yet, however: it should fully support subcommands, too, so
 *@ TODO that, e.g., "vexpr regex" arguments can be fully prepared by the
 *@ TODO generic parser.  But at least a bit.
 *@ TODO See cmd-tab.h for sort and speedup TODOs.
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
 */
/* Command table and getrawlist() also:
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 *
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef su_FILE
#define su_FILE cmd_tab
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>

/* Create a multiline info string about all known additional infos for lcp */
#ifdef mx_HAVE_DOCSTRINGS
static char const *a_ctab_cmdinfo(struct n_cmd_desc const *cdp);
#endif

/* Print a list of all commands */
static int a_ctab_c_list(void *vp);

static int a_ctab__pcmd_cmp(void const *s1, void const *s2);

/* `help' / `?' command */
static int a_ctab_c_help(void *vp);

#if defined mx_HAVE_DEVEL && defined su_MEM_ALLOC_DEBUG
static int a_ctab_c_memtrace(void *vp);
#endif

/* List of all commands; but first their n_cmd_arg_desc instances */
#include "mx/cmd-tab.h" /* $(MX_SRCDIR) */

static struct n_cmd_desc const a_ctab_ctable[] = {
#include <mx/cmd-tab.h>
};

/* And a list of things which are special to the lexer in go.c, so that we can
 * provide help and list them.
 * This cross-file relationship is a bit unfortunate.. */
#ifdef mx_HAVE_DOCSTRINGS
# define DS(S) , S
#else
# define DS(S)
#endif
static struct n_cmd_desc const a_ctab_ctable_plus[] = {
   { n_ns, (int(*)(void*))-1, n_CMD_ARG_TYPE_STRING, 0, 0, NULL
      DS(N_("Comment command: ignore remaining (continuable) line")) },
   { n_hy, (int(*)(void*))-1, n_CMD_ARG_TYPE_WYSH, 0, 0, NULL
      DS(N_("Print out the preceding message")) }
};
#undef DS

#ifdef mx_HAVE_DOCSTRINGS
static char const *
a_ctab_cmdinfo(struct n_cmd_desc const *cdp){
   struct n_string rvb, *rv;
   char const *cp;
   n_NYD2_IN;

   rv = n_string_creat_auto(&rvb);
   rv = n_string_reserve(rv, 80);

   switch(cdp->cd_caflags & n_CMD_ARG_TYPE_MASK){
   case n_CMD_ARG_TYPE_MSGLIST:
      cp = N_("message-list");
      break;
   case n_CMD_ARG_TYPE_NDMLIST:
      cp = N_("message-list (without default)");
      break;
   case n_CMD_ARG_TYPE_STRING:
   case n_CMD_ARG_TYPE_RAWDAT:
      cp = N_("string data");
      break;
   case n_CMD_ARG_TYPE_RAWLIST:
      cp = N_("old-style quoting");
      break;
   case n_CMD_ARG_TYPE_WYRA:
      cp = N_("`wysh' for sh(1)ell-style quoting");
      break;
   case n_CMD_ARG_TYPE_WYSH:
      cp = (cdp->cd_minargs == 0 && cdp->cd_maxargs == 0)
            ? N_("sh(1)ell-style quoting (takes no arguments)")
            : N_("sh(1)ell-style quoting");
      break;
   default:
   case n_CMD_ARG_TYPE_ARG:{
      ui32_t flags, xflags;
      size_t i;
      struct n_cmd_arg_desc const *cadp;

      rv = n_string_push_cp(rv, _("argument tokens: "));

      for(cadp = cdp->cd_cadp, i = 0; i < cadp->cad_no; ++i){
         xflags = flags = cadp->cad_ent_flags[i][0];
jfakeent:
         if(i != 0)
            rv = n_string_push_c(rv, ',');

         if(flags & n_CMD_ARG_DESC_OPTION)
            rv = n_string_push_c(rv, '[');
         if(flags & n_CMD_ARG_DESC_GREEDY)
            rv = n_string_push_c(rv, ':');
         switch(flags & n__CMD_ARG_DESC_TYPE_MASK){
         default:
         case n_CMD_ARG_DESC_SHEXP:
            rv = n_string_push_cp(rv, _("(shell-)token"));
            break;
         case n_CMD_ARG_DESC_MSGLIST:
            rv = n_string_push_cp(rv, _("(shell-)msglist"));
            break;
         case n_CMD_ARG_DESC_NDMSGLIST:
            rv = n_string_push_cp(rv, _("(shell-)msglist (no default)"));
            break;
         case n_CMD_ARG_DESC_MSGLIST_AND_TARGET:
            rv = n_string_push_cp(rv, _("(shell-)msglist"));
            ++i;
            xflags = n_CMD_ARG_DESC_SHEXP;
         }
         if(flags & n_CMD_ARG_DESC_GREEDY)
            rv = n_string_push_c(rv, ':');
         if(flags & n_CMD_ARG_DESC_OPTION)
            rv = n_string_push_c(rv, ']');

         if(xflags != flags){
            flags = xflags;
            goto jfakeent;
         }
      }
      cp = NULL;
      }break;
   }
   if(cp != NULL)
      rv = n_string_push_cp(rv, V_(cp));

   /* Note: on updates, change the manual! */
   if(cdp->cd_caflags & n_CMD_ARG_L)
      rv = n_string_push_cp(rv, _(" | `local'"));
   if(cdp->cd_caflags & n_CMD_ARG_V)
      rv = n_string_push_cp(rv, _(" | `vput'"));
   if(cdp->cd_caflags & n_CMD_ARG_EM)
      rv = n_string_push_cp(rv, _(" | *!*"));

   if(cdp->cd_caflags & n_CMD_ARG_A)
      rv = n_string_push_cp(rv, _(" | needs-box"));

   if(cdp->cd_caflags & (n_CMD_ARG_I | n_CMD_ARG_M | n_CMD_ARG_X)){
      rv = n_string_push_cp(rv, _(" | ok:"));
      if(cdp->cd_caflags & n_CMD_ARG_I)
         rv = n_string_push_cp(rv, _(" batch/interactive"));
      if(cdp->cd_caflags & n_CMD_ARG_M)
         rv = n_string_push_cp(rv, _(" send-mode"));
      if(cdp->cd_caflags & n_CMD_ARG_X)
         rv = n_string_push_cp(rv, _(" subprocess"));
   }

   if(cdp->cd_caflags & (n_CMD_ARG_R | n_CMD_ARG_S)){
      rv = n_string_push_cp(rv, _(" | not ok:"));
      if(cdp->cd_caflags & n_CMD_ARG_R)
         rv = n_string_push_cp(rv, _(" compose-mode"));
      if(cdp->cd_caflags & n_CMD_ARG_S)
         rv = n_string_push_cp(rv, _(" startup"));
   }

   if(cdp->cd_caflags & n_CMD_ARG_G)
      rv = n_string_push_cp(rv, _(" | gabby"));

   cp = n_string_cp(rv);
   n_NYD2_OU;
   return cp;
}
#endif /* mx_HAVE_DOCSTRINGS */

static int
a_ctab_c_list(void *vp){
   FILE *fp;
   struct n_cmd_desc const **cdpa, *cdp, **cdpa_curr;
   size_t i, l, scrwid;
   n_NYD_IN;

   i = n_NELEM(a_ctab_ctable) + n_NELEM(a_ctab_ctable_plus) +1;
   cdpa = n_autorec_alloc(sizeof(cdp) * i);

   for(i = 0; i < n_NELEM(a_ctab_ctable); ++i)
      cdpa[i] = &a_ctab_ctable[i];
   for(l = 0; l < n_NELEM(a_ctab_ctable_plus); ++i, ++l)
      cdpa[i] = &a_ctab_ctable_plus[l];
   cdpa[i] = NULL;

   if(*(void**)vp == NULL)
      qsort(cdpa, i, sizeof(*cdpa), &a_ctab__pcmd_cmp);

   if((fp = Ftmp(NULL, "list", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
      fp = n_stdout;

   scrwid = n_SCRNWIDTH_FOR_LISTS;

   fprintf(fp, _("Commands are:\n"));
   l = 1;
   for(i = 0, cdpa_curr = cdpa; (cdp = *cdpa_curr++) != NULL;){
      char const *pre, *suf;

      if(cdp->cd_func == NULL)
         pre = "[", suf = "]";
      else
         pre = suf = n_empty;

#ifdef mx_HAVE_DOCSTRINGS
      if(n_poption & n_PO_D_V){
         fprintf(fp, "%s%s%s\n", pre, cdp->cd_name, suf);
         ++l;
         fprintf(fp, "  : %s\n", V_(cdp->cd_doc));
         ++l;
         fprintf(fp, "  : %s\n", a_ctab_cmdinfo(cdp));
         ++l;
      }else
#endif
           {
         size_t j;

         j = su_cs_len(cdp->cd_name);
         if(*pre != '\0')
            j += 2;

         if((i += j + 2) > scrwid){
            i = j;
            fprintf(fp, "\n");
            ++l;
         }
         fprintf(fp, (*cdpa_curr != NULL ? "%s%s%s, " : "%s%s%s\n"),
            pre, cdp->cd_name, suf);
      }
   }

   if(fp != n_stdout){
      page_or_print(fp, l);
      Fclose(fp);
   }
   n_NYD_OU;
   return 0;
}

static int
a_ctab__pcmd_cmp(void const *s1, void const *s2){
   struct n_cmd_desc const * const *cdpa1, * const *cdpa2;
   int rv;
   n_NYD2_IN;

   cdpa1 = s1;
   cdpa2 = s2;
   rv = su_cs_cmp((*cdpa1)->cd_name, (*cdpa2)->cd_name);
   n_NYD2_OU;
   return rv;
}

static int
a_ctab_c_help(void *vp){
   int rv;
   char const *arg;
   n_NYD_IN;

   /* Help for a single command? */
   if((arg = *(char const**)vp) != NULL){
      struct n_cmd_desc const *cdp, *cdp_max;
      struct str const *alias_exp;
      char const *alias_name, *aepx;

      /* Aliases take precedence.
       * Avoid self-recursion; since a commandalias can shadow a command of
       * equal name allow one level of expansion to return an equal result:
       * "commandalias q q;commandalias x q;x" should be "x->q->q->quit" */
      alias_name = NULL;
      while((aepx = n_commandalias_exists(arg, &alias_exp)) != NULL &&
            (alias_name == NULL || su_cs_cmp(alias_name, aepx))){
         alias_name = aepx;
         fprintf(n_stdout, "%s -> ", arg);
         arg = alias_exp->s;
      }

      cdp_max = &(cdp = a_ctab_ctable)[n_NELEM(a_ctab_ctable)];
jredo:
      for(; cdp < cdp_max; ++cdp){
         if(su_cs_starts_with(cdp->cd_name, arg)){
            fputs(arg, n_stdout);
            if(su_cs_cmp(arg, cdp->cd_name))
               fprintf(n_stdout, " (%s)", cdp->cd_name);
         }else
            continue;

#ifdef mx_HAVE_DOCSTRINGS
         fprintf(n_stdout, ": %s", V_(cdp->cd_doc));
         if(n_poption & n_PO_D_V)
            fprintf(n_stdout, "\n  : %s", a_ctab_cmdinfo(cdp));
#endif
         putc('\n', n_stdout);
         rv = 0;
         goto jleave;
      }

      if(cdp_max == &a_ctab_ctable[n_NELEM(a_ctab_ctable)]){
         cdp_max = &(cdp =
               a_ctab_ctable_plus)[n_NELEM(a_ctab_ctable_plus)];
         goto jredo;
      }

      if(alias_name != NULL){
         fprintf(n_stdout, "%s\n", n_shexp_quote_cp(arg, TRU1));
         rv = 0;
      }else{
         n_err(_("Unknown command: `%s'\n"), arg);
         rv = 1;
      }
   }else{
      /* Very ugly, but take care for compiler supported string lengths :( */
#ifdef mx_HAVE_UISTRINGS
      fputs(su_program, n_stdout);
      fputs(_(
         " commands -- <msglist> denotes message specification tokens,\n"
         "e.g., 1-5, :n or . (current, the \"dot\"), separated by *ifs*:\n"),
         n_stdout);
      fputs(_(
"\n"
"type <msglist>         type (`print') messages (honour `headerpick' etc.)\n"
"Type <msglist>         like `type' but always show all headers\n"
"next                   goto and type next message\n"
"headers                header summary ... for messages surrounding \"dot\"\n"
"search <msglist>       ... for the given list (alias for `from')\n"
"delete <msglist>       delete messages (can be `undelete'd)\n"),
         n_stdout);

      fputs(_(
"\n"
"save <msglist> folder  append messages to folder and mark as saved\n"
"copy <msglist> folder  like `save', but do not mark them (`move' moves)\n"
"write <msglist> file   write message contents to file (prompts for parts)\n"
"Reply <msglist>        reply to message sender(s) only\n"
"reply <msglist>        like `Reply', but address all recipients\n"
"Lreply <msglist>       forced mailing list `reply' (see `mlist')\n"),
         n_stdout);

      fputs(_(
"\n"
"mail <recipients>      compose a mail for the given recipients\n"
"file folder            change to another mailbox\n"
"File folder            like `file', but open readonly\n"
"quit                   quit and apply changes to the current mailbox\n"
"xit or exit            like `quit', but discard changes\n"
"!shell command         shell escape\n"
"list [<anything>]      all available commands [in search order]\n"),
         n_stdout);
#endif /* mx_HAVE_UISTRINGS */

      rv = (ferror(n_stdout) != 0);
   }
jleave:
   n_NYD_OU;
   return rv;
}

#if defined mx_HAVE_DEVEL && defined su_MEM_ALLOC_DEBUG
static int
a_ctab_c_memtrace(void *vp){
   int rv;
   ui32_t oopt;
   n_NYD2_IN;
   n_UNUSED(vp);

   /* Only for development.. */
   oopt = n_poption;
   if(!(oopt & n_PO_VERB))
      ok_bset(verbose);
   rv = (su_mem_trace() != FAL0);
   if(!(oopt & n_PO_VERB))
      ok_bclear(verbose);
   n_NYD2_OU;
   return rv;
}
#endif

FL char const *
n_cmd_isolate(char const *cmd){
   n_NYD2_IN;
   while(*cmd != '\0' &&
         su_cs_find_c("\\!~|? \t0123456789&%@$^.:/-+*'\",;(`", *cmd) == NULL)
      ++cmd;
   n_NYD2_OU;
   return n_UNCONST(cmd);
}

FL struct n_cmd_desc const *
n_cmd_firstfit(char const *cmd){ /* TODO *hashtable*! linear list search!!! */
   struct n_cmd_desc const *cdp;
   n_NYD2_IN;

   for(cdp = a_ctab_ctable; cdp < &a_ctab_ctable[n_NELEM(a_ctab_ctable)]; ++cdp)
      if(*cmd == *cdp->cd_name && cdp->cd_func != NULL &&
            su_cs_starts_with(cdp->cd_name, cmd))
         goto jleave;
   cdp = NULL;
jleave:
   n_NYD2_OU;
   return cdp;
}

FL struct n_cmd_desc const *
n_cmd_default(void){
   struct n_cmd_desc const *cdp;
   n_NYD2_IN;

   cdp = &a_ctab_ctable[0];
   n_NYD2_OU;
   return cdp;
}

FL bool_t
n_cmd_arg_parse(struct n_cmd_arg_ctx *cacp){
   struct n_cmd_arg ncap, *lcap, *target_argp, **target_argpp, *cap;
   struct str shin_orig, shin;
   bool_t stoploop, greedyjoin;
   void const *cookie;
   size_t cad_idx, parsed_args;
   struct n_cmd_arg_desc const *cadp;
   n_NYD_IN;

   assert(cacp->cac_inlen == 0 || cacp->cac_indat != NULL);
   assert(cacp->cac_desc->cad_no > 0);
#ifdef mx_HAVE_DEBUG
   /* C99 */{
      bool_t opt_seen = FAL0;

      for(cadp = cacp->cac_desc, cad_idx = 0;
            cad_idx < cadp->cad_no; ++cad_idx){
         assert(cadp->cad_ent_flags[cad_idx][0] & n__CMD_ARG_DESC_TYPE_MASK);

         /* TODO n_CMD_ARG_DESC_MSGLIST+ may only be used as the last entry */
         assert(!(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_MSGLIST) ||
            cad_idx + 1 == cadp->cad_no);
         assert(!(cadp->cad_ent_flags[cad_idx][0] &
               n_CMD_ARG_DESC_NDMSGLIST) || cad_idx + 1 == cadp->cad_no);
         assert(!(cadp->cad_ent_flags[cad_idx][0] &
               n_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
            cad_idx + 1 == cadp->cad_no);

         assert(!opt_seen ||
            (cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_OPTION));
         if(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_OPTION)
            opt_seen = TRU1;
         assert(!(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_GREEDY) ||
            cad_idx + 1 == cadp->cad_no);

         /* TODO n_CMD_ARG_DESC_MSGLIST+ can only be n_CMD_ARG_DESC_GREEDY.
          * TODO And they may not be n_CMD_ARG_DESC_OPTION */
         assert(!(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_MSGLIST) ||
            (cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_GREEDY));
         assert(!(cadp->cad_ent_flags[cad_idx][0] &
               n_CMD_ARG_DESC_NDMSGLIST) ||
            (cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_GREEDY));
         assert(!(cadp->cad_ent_flags[cad_idx][0] &
               n_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
            (cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_GREEDY));

         assert(!(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_MSGLIST) ||
            !(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_OPTION));
         assert(!(cadp->cad_ent_flags[cad_idx][0] &
               n_CMD_ARG_DESC_NDMSGLIST) ||
            !(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_OPTION));
         assert(!(cadp->cad_ent_flags[cad_idx][0] &
               n_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
            !(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_OPTION));
      }
   }
#endif /* mx_HAVE_DEBUG */

   n_pstate_err_no = su_ERR_NONE;
   shin.s = n_UNCONST(cacp->cac_indat); /* "logical" only */
   shin.l = (cacp->cac_inlen == UIZ_MAX ? su_cs_len(shin.s) : cacp->cac_inlen);
   shin_orig = shin;
   cacp->cac_no = 0;
   cacp->cac_arg = lcap = NULL;

   cookie = NULL;
   parsed_args = 0;
   greedyjoin = FAL0;

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
      stoploop = FAL0;

      switch(ncap.ca_ent_flags[0] & n__CMD_ARG_DESC_TYPE_MASK){
      default:
      case n_CMD_ARG_DESC_SHEXP:{
         struct n_string shou, *shoup;
         enum n_shexp_state shs;
         ui32_t addflags;

         if(shin.l == 0) goto jloop_break; /* TODO */

         if(cad_idx == cadp->cad_no - 1 ||
               (cadp->cad_ent_flags[cad_idx + 1][0] & n_CMD_ARG_DESC_OPTION))
            addflags = n_SHEXP_PARSE_META_SEMICOLON;
         else
            addflags = n_SHEXP_PARSE_NONE;

         shoup = n_string_creat_auto(&shou);
         ncap.ca_arg_flags =
         shs = n_shexp_parse_token((ncap.ca_ent_flags[1] | addflags |
                  n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_LOG),
               shoup, &shin,
               (ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_GREEDY ? &cookie : NULL));
         ncap.ca_inlen = PTR2SIZE(shin.s - ncap.ca_indat);
         if((shs & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_ERR_MASK)) ==
               n_SHEXP_STATE_OUTPUT){
            if((shs & n_SHEXP_STATE_META_SEMICOLON) && shou.s_len == 0)
               goto jloop_break;
            ncap.ca_arg.ca_str.s = n_string_cp(shoup);
            ncap.ca_arg.ca_str.l = shou.s_len;
            shoup = n_string_drop_ownership(shoup);
         }
         n_string_gut(shoup);

         if(shs & n_SHEXP_STATE_ERR_MASK)
            goto jerr;
         if((shs & n_SHEXP_STATE_STOP) &&
               (ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_HONOUR_STOP)){
            if(!(shs & n_SHEXP_STATE_OUTPUT))
               goto jleave;
            stoploop = TRU1;
         }else if(!(shs & n_SHEXP_STATE_OUTPUT)) /* XXX Is this right? */
            goto jerr;
         }break;
      case n_CMD_ARG_DESC_MSGLIST_AND_TARGET:
         target_argpp = &target_argp;
         /* FALLTHRU */
      case n_CMD_ARG_DESC_MSGLIST:
      case n_CMD_ARG_DESC_NDMSGLIST:
         /* TODO _MSGLIST yet at end and greedy only (fast hack).
          * TODO And consumes too much memory */
         assert(shin.s[shin.l] == '\0');
         if(n_getmsglist(shin.s, (ncap.ca_arg.ca_msglist =
                  n_autorec_calloc(msgCount +1, sizeof *ncap.ca_arg.ca_msglist)
               ), cacp->cac_msgflag, target_argpp) < 0){
            n_pstate_err_no = su_ERR_INVAL;/* XXX should come from getmsglist*/
            goto jerr;
         }

         if(ncap.ca_arg.ca_msglist[0] == 0){
            ui32_t e;

            switch(ncap.ca_ent_flags[0] & n__CMD_ARG_DESC_TYPE_MASK){
            case n_CMD_ARG_DESC_MSGLIST_AND_TARGET:
            case n_CMD_ARG_DESC_MSGLIST:
               if((ncap.ca_arg.ca_msglist[0] = first(cacp->cac_msgflag,
                     cacp->cac_msgmask)) == 0){
                  if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
                        (n_poption & n_PO_D_V))
                     n_err(_("No applicable messages\n"));

                  e = n_CMD_ARG_DESC_TO_ERRNO(ncap.ca_ent_flags[0]);
                  if(e == 0)
                     e = su_ERR_NODATA;
                  n_pstate_err_no = e;
                  goto jerr;
               }
               ncap.ca_arg.ca_msglist[1] = 0;

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
         }else if((ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE
               ) && ncap.ca_arg.ca_msglist[1] != 0){
            if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
                  (n_poption & n_PO_D_V))
               n_err(_("Cannot specify multiple messages at once\n"));
            n_pstate_err_no = su_ERR_NOTSUP;
            goto jerr;
         }
         shin.l = 0;
         stoploop = TRU1; /* XXX Asserted to be last above! */
         break;
      }
      ++parsed_args;

      if(greedyjoin == TRU1){ /* TODO speed this up! */
         char *cp;
         size_t i;

         assert((ncap.ca_ent_flags[0] & n__CMD_ARG_DESC_TYPE_MASK
            ) != n_CMD_ARG_DESC_MSGLIST);
         assert(lcap != NULL);
         assert(target_argpp == NULL);
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

      if(stoploop)
         goto jleave;

      if((shin.l > 0 || cookie != NULL) &&
            (ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_GREEDY)){
         if(!greedyjoin)
            greedyjoin = ((ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_GREEDY_JOIN) &&
                     (ncap.ca_ent_flags[0] & n_CMD_ARG_DESC_SHEXP))
                  ? TRU1 : TRUM1;
         goto jredo;
      }
   }

jloop_break:
   if(cad_idx < cadp->cad_no &&
         !(cadp->cad_ent_flags[cad_idx][0] & n_CMD_ARG_DESC_OPTION))
      goto jerr;

   lcap = (struct n_cmd_arg*)-1;
jleave:
   n_NYD_OU;
   return (lcap != NULL);

jerr:
   if(n_pstate_err_no == su_ERR_NONE){
      n_pstate_err_no = su_ERR_INVAL;

      if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
            (n_poption & n_PO_D_V)){
         size_t i;

         for(i = 0; (i < cadp->cad_no &&
               !(cadp->cad_ent_flags[i][0] & n_CMD_ARG_DESC_OPTION)); ++i)
            ;

         n_err(_("`%s': parsing stopped after %" PRIuZ " arguments "
               "(need %" PRIuZ "%s)\n"
               "     Input: %.*s\n"
               "   Stopped: %.*s\n"),
            cadp->cad_name, parsed_args,
               i, (i == cadp->cad_no ? n_empty : "+"),
            (int)shin_orig.l, shin_orig.s,
            (int)shin.l, shin.s);
      }
   }
   lcap = NULL;
   goto jleave;
}

FL void *
n_cmd_arg_save_to_heap(struct n_cmd_arg_ctx const *cacp){
   struct n_cmd_arg *ncap;
   struct n_cmd_arg_ctx *ncacp;
   char *buf;
   struct n_cmd_arg const *cap;
   size_t len, i;
   n_NYD2_IN;

   /* For simplicity, save it all in once chunk, so that it can be thrown away
    * with a simple n_free() from whoever is concerned */
   len = sizeof *cacp;
   for(cap = cacp->cac_arg; cap != NULL; cap = cap->ca_next){
      i = cap->ca_arg.ca_str.l +1;
      i = n_ALIGN(i);
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
         (ncap->ca_arg.ca_str.l = i = cap->ca_arg.ca_str.l) +1);

      i = n_ALIGN(i);
      buf += sizeof(*ncap) + i;
   }

   if(cacp->cac_vput != NULL){
      ncacp->cac_vput = buf;
      su_mem_copy(buf, cacp->cac_vput, su_cs_len(cacp->cac_vput) +1);
   }else
      ncacp->cac_vput = NULL;
   n_NYD2_OU;
   return ncacp;
}

FL struct n_cmd_arg_ctx *
n_cmd_arg_restore_from_heap(void *vp){
   struct n_cmd_arg *cap, *ncap;
   struct n_cmd_arg_ctx *cacp, *rv;
   n_NYD2_IN;

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
   n_NYD2_OU;
   return rv;
}

FL int
getrawlist(bool_t wysh, char **res_dat, size_t res_size,
      char const *line, size_t linesize){
   int res_no;
   n_NYD_IN;

   n_pstate &= ~n_PS_ARGLIST_MASK;

   if(res_size == 0){
      res_no = -1;
      goto jleave;
   }else if(UICMP(z, res_size, >, INT_MAX))
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

         if(UICMP(z, res_no, >=, res_size)){
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

         res_dat[res_no++] = savestrbuf(linebuf, PTR2SIZE(cp2 - linebuf));
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
         if(UICMP(z, res_no, >=, res_size)){
            n_err(_("Too many input tokens for result storage\n"));
            res_no = -1;
            break;
         }

         /* C99 */{
            enum n_shexp_state shs;

            if((shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
                        (cookie == NULL ? n_SHEXP_PARSE_TRIM_SPACE : 0) |
                        /* TODO not here in old style n_SHEXP_PARSE_IFS_VAR |*/
                        n_SHEXP_PARSE_META_SEMICOLON),
                     &store, &input, &cookie)
                  ) & n_SHEXP_STATE_ERR_MASK){
               /* Simply ignore Unicode error, just keep the normalized \[Uu] */
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
      res_dat[(size_t)res_no] = NULL;
jleave:
   n_NYD_OU;
   return res_no;
}

/* s-it-mode */
