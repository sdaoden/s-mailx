/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Dig message objects. TODO Very very restricted (especially non-compose)
 *@ Protocol change: adjust mx-config.h:mx_DIG_MSG_PLUMBING_VERSION + `~^' man.
 *@ TODO - a_dmsg_cmd() should generate string lists, not perform real I/O.
 *@ TODO   I.e., drop FILE* arg, generate stringlist; up to callers...
 *@ TODO - With our own I/O there should then be a StringListDevice as the
 *@ TODO   owner and I/O overlay provider: NO temporary file (sic)!
 *@ XXX - Multiple objects per message could be possible (a_dmsg_find()),
 *@ XXX   except in compose mode
 *
 * Copyright (c) 2016 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE dig_msg
#define mx_SOURCE
#define mx_SOURCE_DIG_MSG

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/attachments.h"
#include "mx/cmd.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/mime.h"
#include "mx/mime-type.h"
#include "mx/names.h"

#include "mx/dig-msg.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#define a_DMSG_QUOTE(S) n_shexp_quote_cp(S, FAL0)

/**/
CTAV(HF_CMD_TO_OFF(HF_CMD_forward) == 0);
CTAV(HF_CMD_TO_OFF(HF_CMD_mail) == 1);
CTAV(HF_CMD_TO_OFF(HF_CMD_Lreply) == 2);
CTAV(HF_CMD_TO_OFF(HF_CMD_Reply) == 3);
CTAV(HF_CMD_TO_OFF(HF_CMD_reply) == 4);
CTAV(HF_CMD_TO_OFF(HF_CMD_resend) == 5);
CTAV(HF_CMD_TO_OFF(HF_CMD_MASK) == 6);

static char const a_dmsg_hf_cmd[7][8] = {
   "forward\0", "mail", "Lreply", "Reply", "reply", "resend", ""
};

struct mx_dig_msg_ctx *mx_dig_msg_read_overlay; /* XXX HACK */
struct mx_dig_msg_ctx *mx_dig_msg_compose_ctx; /* Or NIL XXX HACK*/

/* Try to convert cp into an unsigned number that corresponds to an existing
 * message number (or ERR_INVAL), search for an existing object (ERR_EXIST if
 * oexcl and exists; ERR_NOENT if not oexcl and does not exist).
 * On oexcl success *dmcp will be ALLOC()ated with .dmc_msgno and .dmc_mp
 * etc. set; but not linked into mb.mb_digmsg and .dmc_fp not created etc. */
static s32 a_dmsg_find(char const *cp, struct mx_dig_msg_ctx **dmcpp,
      boole oexcl);

/* Subcommand drivers */
static boole a_dmsg_cmd(FILE *fp, struct mx_dig_msg_ctx *dmcp,
      struct mx_cmd_arg *cmd, struct mx_cmd_arg *args);

static boole a_dmsg__header(FILE *fp, struct mx_dig_msg_ctx *dmcp,
      struct mx_cmd_arg *args);
static boole a_dmsg__attach(FILE *fp, struct mx_dig_msg_ctx *dmcp,
      struct mx_cmd_arg *args);

static s32
a_dmsg_find(char const *cp, struct mx_dig_msg_ctx **dmcpp, boole oexcl){
   struct mx_dig_msg_ctx *dmcp;
   s32 rv;
   u32 msgno;
   NYD2_IN;

   if(cp[0] == '-' && cp[1] == '\0'){
      if((dmcp = mx_dig_msg_compose_ctx) != NIL){
         *dmcpp = dmcp;
         if(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE_DIGGED)
            rv = oexcl ? su_ERR_EXIST : su_ERR_NONE;
         else
            rv = oexcl ? su_ERR_NONE : su_ERR_NOENT;
      }else
         rv = su_ERR_INVAL;
      goto jleave;
   }

   if((su_idec_u32_cp(&msgno, cp, 0, NIL
            ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED ||
         msgno == 0 || UCMP(z, msgno, >, msgCount)){
      rv = su_ERR_INVAL;
      goto jleave;
   }

   for(dmcp = mb.mb_digmsg; dmcp != NIL; dmcp = dmcp->dmc_next)
      if(dmcp->dmc_msgno == msgno){
         *dmcpp = dmcp;
         rv = oexcl ? su_ERR_EXIST : su_ERR_NONE;
         goto jleave;
      }
   if(!oexcl){
      rv = su_ERR_NOENT;
      goto jleave;
   }

   *dmcpp = dmcp = su_CALLOC(Z_ALIGN(sizeof *dmcp) + sizeof(struct header));
   dmcp->dmc_mp = &message[msgno - 1];
   dmcp->dmc_flags = mx_DIG_MSG_OWN_MEMBAG |
         ((TRU1/*TODO*/ || !(mb.mb_perm & MB_DELE))
            ? mx_DIG_MSG_RDONLY : mx_DIG_MSG_NONE);
   dmcp->dmc_msgno = msgno;
   dmcp->dmc_hp = R(struct header*,P2UZ(&dmcp[1]));
   dmcp->dmc_membag = su_mem_bag_create(&dmcp->dmc__membag_buf[0], 0);
   /* Rest done by caller */
   rv = su_ERR_NONE;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_dmsg_cmd(FILE *fp, struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *cmd,
      struct mx_cmd_arg *args){
   union {struct mx_cmd_arg *ca; char *c; struct str const *s; boole rv;} p;
   NYD2_IN;

   if(cmd == NIL)
      goto jecmd;

   p.s = &cmd->ca_arg.ca_str;
   if(su_cs_starts_with_case_n("header", p.s->s, p.s->l))
      p.rv = a_dmsg__header(fp, dmcp, args);
   else if(su_cs_starts_with_case_n("attachment", p.s->s, p.s->l)){
      if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)) /* TODO attachment support */
         p.rv = (fprintf(fp,
               "505 `digmsg attachment' only in compose mode (yet)\n") > 0);
      else
         p.rv = a_dmsg__attach(fp, dmcp, args);
   }else if(su_cs_starts_with_case_n("version", p.s->s, p.s->l)){
      if(args != NIL)
         goto jecmd;
      p.rv = (fputs("210 " mx_DIG_MSG_PLUMBING_VERSION "\n", fp) != EOF);
   }else if((p.s->l == 1 && p.s->s[0] == '?') ||
         su_cs_starts_with_case_n("help", p.s->s, p.s->l)){
      if(args != NIL)
         goto jecmd;
      p.rv = (fputs(_("211 (Arguments undergo shell-style evaluation)\n"),
               fp) != EOF &&
#ifdef mx_HAVE_UISTRINGS
            fputs(_(
               "attachment:\n"
               "   attribute name (212; 501)\n"
               "   attribute-at position\n"
               "   attribute-set name key value (210; 505/501)\n"
               "   attribute-set-at position key value\n"
               "   insert file[=input-charset[#output-charset]] "
                  "(210; 501/505/506)\n"
               "   insert #message-number\n"
               "   list (212; 501)\n"
               "   remove name (210; 501/506)\n"
               "   remove-at position (210; 501/505)\n"), fp) != EOF &&
            fputs(_(
               "header\n"
               "   insert field content (210; 501/505/506)\n"
               "   list [field] (210; [501]);\n"
               "   remove field (210; 501/505)\n"
               "   remove-at field position (210; 501/505)\n"
               "   show field (211/212; 501)\n"
               "help (211)\n"
               "version (210)\n"), fp) != EOF &&
#endif
            putc('\n', fp) != EOF);
   }else{
jecmd:
      fputs("500\n", fp);
      p.rv = FAL0;
   }
   fflush(fp);

   NYD2_OU;
   return p.rv;
}

static boole
a_dmsg__header(FILE *fp, struct mx_dig_msg_ctx *dmcp,
      struct mx_cmd_arg *args){
   struct str sin, sou;
   struct n_header_field *hfp;
   struct mx_name *np, **npp;
   uz i;
   struct mx_cmd_arg *a3p;
   char const *cp;
   struct header *hp;
   NYD2_IN;

   hp = dmcp->dmc_hp;
   UNINIT(a3p, NIL);

   if(args == NIL){
      cp = su_empty; /* xxx not NIL anyway */
      goto jdefault;
   }

   cp = args->ca_arg.ca_str.s;
   args = args->ca_next;

   /* Strip the optional colon from header names */
   if((a3p = args) != NIL){
      char *xp;

      a3p = a3p->ca_next;

      for(xp = args->ca_arg.ca_str.s;; ++xp)
         if(*xp == '\0')
            break;
         else if(*xp == ':'){
            *xp = '\0';
            break;
         }
   }

   /* TODO ERR_2BIG should happen on the cmd_arg parser side */
   if(a3p != NIL && a3p->ca_next != NIL)
      goto jecmd;

   if(su_cs_starts_with_case("insert", cp)){ /* TODO LOGIC BELONGS head.c
       * TODO That is: Header::factory(string) -> object (blahblah).
       * TODO I.e., as long as we don't have regular RFC compliant parsers
       * TODO which differentiate in between structured and unstructured
       * TODO header fields etc., a little workaround */
      struct mx_name *xnp;
      s8 aerr;
      char const *mod_suff;
      enum expand_addr_check_mode eacm;
      enum gfield ntype;
      boole mult_ok;

      if(args == NIL || a3p == NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      /* Strip [\r\n] which would render a body invalid XXX all controls? */
      /* C99 */{
         char c;

         for(cp = a3p->ca_arg.ca_str.s; (c = *cp) != '\0'; ++cp)
            if(c == '\n' || c == '\r')
               *UNCONST(char*,cp) = ' ';
      }

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Subject")){
         if(a3p->ca_arg.ca_str.l == 0)
            goto j501cp;

         if(hp->h_subject != NIL)
            hp->h_subject = savecatsep(hp->h_subject, ' ',
                  a3p->ca_arg.ca_str.s);
         else
            hp->h_subject = a3p->ca_arg.ca_str.s;
         if(fprintf(fp, "210 %s 1\n", cp) < 0)
            cp = NIL;
         goto jleave;
      }

      mult_ok = TRU1;
      ntype = GEXTRA | GFULL | GFULLEXTRA;
      eacm = EACM_STRICT;
      mod_suff = NIL;

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
         npp = &hp->h_from;
jins:
         aerr = 0;
         /* todo As said above, this should be table driven etc., but.. */
         if(ntype & GBCC_IS_FCC){
            np = nalloc_fcc(a3p->ca_arg.ca_str.s);
            if(is_addr_invalid(np, eacm))
               goto jins_505;
         }else{
            if((np = (mult_ok > FAL0 ? lextract : n_extract_single
                  )(a3p->ca_arg.ca_str.s, ntype | GNULL_OK)) == NIL)
               goto j501cp;

            if((np = checkaddrs(np, eacm, &aerr), aerr != 0)){
jins_505:
               if(fprintf(fp, "505 %s\n", cp) < 0)
                  cp = NIL;
               goto jleave;
            }
         }

         /* Go to the end of the list, track whether it contains any
          * non-deleted entries */
         i = 0;
         if((xnp = *npp) != NIL)
            for(;; xnp = xnp->n_flink){
               if(!(xnp->n_type & GDEL))
                  ++i;
               if(xnp->n_flink == NIL)
                  break;
            }

         if(!mult_ok && (i != 0 || np->n_flink != NIL)){
            if(fprintf(fp, "506 %s\n", cp) < 0)
               cp = NIL;
         }else{
            if(xnp == NIL)
               *npp = np;
            else
               xnp->n_flink = np;
            np->n_blink = xnp;
            if(fprintf(fp, "210 %s %" PRIuZ "\n", cp, ++i) < 0)
               cp = NIL;
         }
         goto jleave;
      }

#undef a_X
#define a_X(F,H,INS) \
   if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = F)) \
      {npp = &hp->H; INS; goto jins;}

      if((cp = su_cs_find_c(args->ca_arg.ca_str.s, '?')) != NIL){
         mod_suff = cp;
         args->ca_arg.ca_str.s[P2UZ(cp - args->ca_arg.ca_str.s)] = '\0';
         if(*++cp != '\0' && !su_cs_starts_with_case("single", cp)){
            cp = mod_suff;
            goto j501cp;
         }
         mult_ok = TRUM1;
      }

      /* Just like with ~t,~c,~b, immediately test *expandaddr* compliance */
      a_X("To", h_to, ntype = GTO|GFULL su_COMMA eacm = EACM_NORMAL);
      a_X("Cc", h_cc, ntype = GCC|GFULL su_COMMA eacm = EACM_NORMAL);
      a_X("Bcc", h_bcc, ntype = GBCC|GFULL su_COMMA eacm = EACM_NORMAL);

      if((cp = mod_suff) != NIL)
         goto j501cp;

      /* Not | EAF_FILE, depend on *expandaddr*! */
      a_X("Fcc", h_fcc, ntype = GBCC|GBCC_IS_FCC su_COMMA eacm = EACM_NORMAL);
      a_X("Sender", h_sender, mult_ok = FAL0);
      a_X("Reply-To", h_reply_to, eacm = EACM_NONAME);
      a_X("Mail-Followup-To", h_mft, eacm = EACM_NONAME);
      a_X("Message-ID", h_message_id,
         mult_ok = FAL0 su_COMMA ntype = GREF su_COMMA eacm = EACM_NONAME);
      a_X("References", h_ref, ntype = GREF su_COMMA eacm = EACM_NONAME);
      a_X("In-Reply-To", h_in_reply_to, ntype = GREF su_COMMA
         eacm = EACM_NONAME);

#undef a_X

      if((cp = n_header_is_known(args->ca_arg.ca_str.s, UZ_MAX)) != NIL)
         goto j505r;

      /* Free-form header fields */
      /* C99 */{
         uz nl, bl;
         struct n_header_field **hfpp;

         for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = args->ca_arg.ca_str.s;
               goto j501cp;
            }

         for(i = 0, hfpp = &hp->h_user_headers; *hfpp != NIL; ++i)
            hfpp = &(*hfpp)->hf_next;

         nl = su_cs_len(cp = args->ca_arg.ca_str.s) +1;
         bl = su_cs_len(a3p->ca_arg.ca_str.s) +1;
         *hfpp = hfp = su_AUTO_ALLOC(VSTRUCT_SIZEOF(struct n_header_field,
               hf_dat) + nl + bl);
         hfp->hf_next = NIL;
         hfp->hf_nl = nl - 1;
         hfp->hf_bl = bl - 1;
         su_mem_copy(&hfp->hf_dat[0], cp, nl);
         su_mem_copy(&hfp->hf_dat[nl], a3p->ca_arg.ca_str.s, bl);
         if(fprintf(fp, "210 %s %" PRIuZ "\n", &hfp->hf_dat[0], ++i) < 0)
            cp = NIL;
      }
   }else if(su_cs_starts_with_case("list", cp)){
jdefault:
      if(args == NIL){
         if(fputs("210", fp) == EOF){
            cp = NIL;
            goto jleave;
         }

#undef a_X
#define a_X(F,S) \
   if(su_CONCAT(hp->h_, F) != NIL && fputs(" " su_STRING(S), fp) == EOF){\
      cp = NIL;\
      goto jleave;\
   }

         a_X(subject, Subject);
         a_X(from, From);
         a_X(sender, Sender);
         a_X(to, To);
         a_X(cc, Cc);
         a_X(bcc, Bcc);
         a_X(fcc, Fcc);
         a_X(reply_to, Reply-To);
         a_X(mft, Mail-Followup-To);
         a_X(message_id, Message-ID);
         a_X(ref, References);
         a_X(in_reply_to, In-Reply-To);

         a_X(mailx_raw_to, Mailx-Raw-To);
         a_X(mailx_raw_cc, Mailx-Raw-Cc);
         a_X(mailx_raw_bcc, Mailx-Raw-Bcc);
         a_X(mailx_orig_sender, Mailx-Orig-Sender);
         a_X(mailx_orig_from, Mailx-Orig-From);
         a_X(mailx_orig_to, Mailx-Orig-To);
         a_X(mailx_orig_cc, Mailx-Orig-Cc);
         a_X(mailx_orig_bcc, Mailx-Orig-Bcc);

         if((hp->h_flags & HF_CMD_MASK) != HF_NONE &&
               fputs(" " su_STRING(Mailx-Command), fp) == EOF){
            cp = NIL;
            goto jleave;
         }

#undef a_X

         /* Print only one instance of each free-form header */
         for(hfp = hp->h_user_headers; hfp != NIL; hfp = hfp->hf_next){
            struct n_header_field *hfpx;

            for(hfpx = hp->h_user_headers;; hfpx = hfpx->hf_next)
               if(hfpx == hfp){
                  putc(' ', fp);
                  fputs(&hfp->hf_dat[0], fp);
                  break;
               }else if(!su_cs_cmp_case(&hfpx->hf_dat[0], &hfp->hf_dat[0]))
                  break;
         }
         if(putc('\n', fp) == EOF)
            cp = NIL;
         goto jleave;
      }

      if(a3p != NIL)
         goto jecmd;

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Subject")){
         np = (hp->h_subject != NIL) ? R(struct mx_name*,-1) : NIL;
         goto jlist;
      }
      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
         np = hp->h_from;
jlist:
         fprintf(fp, "%s %s\n", (np == NIL ? "501" : "210"), cp);
         goto jleave;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
      np = hp->su_CONCAT(h_,H);\
      goto jlist;\
   }

      a_X(Sender, sender);
      a_X(To, to);
      a_X(Cc, cc);
      a_X(Bcc, bcc);
      a_X(Fcc, fcc);
      a_X(Reply-To, reply_to);
      a_X(Mail-Followup-To, mft);
      a_X(Message-ID, message_id);
      a_X(References, ref);
      a_X(In-Reply-To, in_reply_to);

      a_X(Mailx-Raw-To, mailx_raw_to);
      a_X(Mailx-Raw-Cc, mailx_raw_cc);
      a_X(Mailx-Raw-Bcc, mailx_raw_bcc);
      a_X(Mailx-Orig-Sender, mailx_orig_sender);
      a_X(Mailx-Orig-From, mailx_orig_from);
      a_X(Mailx-Orig-To, mailx_orig_to);
      a_X(Mailx-Orig-Cc, mailx_orig_cc);
      a_X(Mailx-Orig-Bcc, mailx_orig_bcc);

#undef a_X

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Mailx-Command")){
         np = ((hp->h_flags & HF_CMD_MASK) != HF_NONE)
               ? R(struct mx_name*,-1) : NIL;
         goto jlist;
      }

      /* Free-form header fields */
      for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
         if(!fieldnamechar(*cp)){
            cp = args->ca_arg.ca_str.s;
            goto j501cp;
         }

      cp = args->ca_arg.ca_str.s;
      for(hfp = hp->h_user_headers;; hfp = hfp->hf_next){
         if(hfp == NIL)
            goto j501cp;
         else if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
            if(fprintf(fp, "210 %s\n", &hfp->hf_dat[0]) < 0)
               cp = NIL;
            break;
         }
      }
   }else if(su_cs_starts_with_case("remove", cp)){
      if(args == NIL || a3p != NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Subject")){
         if(hp->h_subject == NIL)
            goto j501cp;

         hp->h_subject = NIL;
         if(fprintf(fp, "210 %s\n", cp) < 0)
            cp = NIL;
         goto jleave;
      }

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
         npp = &hp->h_from;
jrem:
         if(*npp != NIL){
            *npp = NIL;
            if(fprintf(fp, "210 %s\n", cp) < 0)
               cp = NIL;
            goto jleave;
         }else
            goto j501cp;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
      npp = &hp->su_CONCAT(h_,H);\
      goto jrem;\
   }

      a_X(Sender, sender);
      a_X(To, to);
      a_X(Cc, cc);
      a_X(Bcc, bcc);
      a_X(Fcc, fcc);
      a_X(Reply-To, reply_to);
      a_X(Mail-Followup-To, mft);
      a_X(Message-ID, message_id);
      a_X(References, ref);
      a_X(In-Reply-To, in_reply_to);

#undef a_X

      if((cp = n_header_is_known(args->ca_arg.ca_str.s, UZ_MAX)) != NIL)
         goto j505r;

      /* Free-form header fields (note j501cp may print non-normalized name) */
      /* C99 */{
         struct n_header_field **hfpp;
         boole any;

         for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = args->ca_arg.ca_str.s;
               goto j501cp;
            }
         cp = args->ca_arg.ca_str.s;

         for(any = FAL0, hfpp = &hp->h_user_headers; (hfp = *hfpp) != NIL;){
            if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
               *hfpp = hfp->hf_next;
               if(!any){
                  if(fprintf(fp, "210 %s\n", &hfp->hf_dat[0]) < 0){
                     cp = NIL;
                     goto jleave;
                  }
               }
               any = TRU1;
            }else
               hfpp = &hfp->hf_next;
         }
         if(!any)
            goto j501cp;
      }
   }else if(su_cs_starts_with_case("remove-at", cp)){
      if(args == NIL || a3p == NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((su_idec_uz_cp(&i, a3p->ca_arg.ca_str.s, 0, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0){
         if(fprintf(fp, "505 invalid position: %s\n",
               a3p->ca_arg.ca_str.s) < 0)
            cp = NIL;
         goto jleave;
      }

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Subject")){
         if(hp->h_subject != NIL && i == 1){
            hp->h_subject = NIL;
            if(fprintf(fp, "210 %s 1\n", cp) < 0)
               cp = NIL;
            goto jleave;
         }else
            goto j501cp;
      }

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
         npp = &hp->h_from;
jremat:
         if((np = *npp) == NIL)
            goto j501cp;
         while(--i != 0 && np != NIL)
            np = np->n_flink;
         if(np == NIL)
            goto j501cp;

         if(np->n_blink != NIL)
            np->n_blink->n_flink = np->n_flink;
         else
            *npp = np->n_flink;
         if(np->n_flink != NIL)
            np->n_flink->n_blink = np->n_blink;

         if(fprintf(fp, "210 %s\n", cp) < 0)
            cp = NIL;
         goto jleave;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
      npp = &hp->su_CONCAT(h_,H);\
      goto jremat;\
   }

      a_X(Sender, sender);
      a_X(To, to);
      a_X(Cc, cc);
      a_X(Bcc, bcc);
      a_X(Fcc, fcc);
      a_X(Reply-To, reply_to);
      a_X(Mail-Followup-To, mft);
      a_X(Message-ID, message_id);
      a_X(References, ref);
      a_X(In-Reply-To, in_reply_to);

#undef a_X

      if((cp = n_header_is_known(args->ca_arg.ca_str.s, UZ_MAX)) != NIL)
         goto j505r;

      /* Free-form header fields */
      /* C99 */{
         struct n_header_field **hfpp;

         for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = args->ca_arg.ca_str.s;
               goto j501cp;
            }
         cp = args->ca_arg.ca_str.s;

         for(hfpp = &hp->h_user_headers; (hfp = *hfpp) != NIL;){
            if(--i == 0){
               *hfpp = hfp->hf_next;
               if(fprintf(fp, "210 %s %" PRIuZ "\n", &hfp->hf_dat[0], i) < 0){
                  cp = NIL;
                  goto jleave;
               }
               break;
            }else
               hfpp = &hfp->hf_next;
         }
         if(hfp == NIL)
            goto j501cp;
      }
   }else if(su_cs_starts_with_case("show", cp)){
      if(args == NIL || a3p != NIL)
         goto jecmd;

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Subject")){
         if((sin.s = hp->h_subject) == NIL)
            goto j501cp;
         sin.l = su_cs_len(sin.s);

         mx_mime_display_from_header(&sin, &sou,
            mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT);

         if(fprintf(fp, "212 %s\n%s\n\n", cp, a_DMSG_QUOTE(sou.s)) < 0)
            cp = NIL;

         su_FREE(sou.s);
         goto jleave;
      }

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
         np = hp->h_from;
jshow:
         if(np == NIL)
            goto j501cp;

         fprintf(fp, "211 %s\n", cp);
         do if(!(np->n_type & GDEL)){
            switch(np->n_flags & mx_NAME_ADDRSPEC_ISMASK){
            case mx_NAME_ADDRSPEC_ISFILE: cp = n_hy; break;
            case mx_NAME_ADDRSPEC_ISPIPE: cp = "|"; break;
            case mx_NAME_ADDRSPEC_ISNAME: cp = n_ns; break;
            default: cp = np->n_name; break;
            }
            fprintf(fp, "%s %s\n", cp,
               a_DMSG_QUOTE(mx_mime_fromaddr(np->n_fullname)));
         }while((np = np->n_flink) != NIL);
         if(putc('\n', fp) == EOF)
            cp = NIL;
         goto jleave;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
      np = hp->su_CONCAT(h_,H);\
      goto jshow;\
   }

      a_X(Sender, sender);
      a_X(To, to);
      a_X(Cc, cc);
      a_X(Bcc, bcc);
      a_X(Fcc, fcc);
      a_X(Reply-To, reply_to);
      a_X(Mail-Followup-To, mft);
      a_X(Message-ID, message_id);
      a_X(References, ref);
      a_X(In-Reply-To, in_reply_to);

      a_X(Mailx-Raw-To, mailx_raw_to);
      a_X(Mailx-Raw-Cc, mailx_raw_cc);
      a_X(Mailx-Raw-Bcc, mailx_raw_bcc);
      a_X(Mailx-Orig-Sender, mailx_orig_sender);
      a_X(Mailx-Orig-From, mailx_orig_from);
      a_X(Mailx-Orig-To, mailx_orig_to);
      a_X(Mailx-Orig-Cc, mailx_orig_cc);
      a_X(Mailx-Orig-Bcc, mailx_orig_bcc);

#undef a_X

      if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Mailx-Command")){
         if((i = hp->h_flags & HF_CMD_MASK) == HF_NONE)
            goto j501cp;
         if(fprintf(fp, "212 %s\n%s\n\n", cp, a_dmsg_hf_cmd[HF_CMD_TO_OFF(i)]
               ) < 0)
            cp = NIL;
         goto jleave;
      }

      /* Free-form header fields */
      /* C99 */{
         boole any;

         for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = args->ca_arg.ca_str.s;
               goto j501cp;
            }
         cp = args->ca_arg.ca_str.s;

         for(any = FAL0, hfp = hp->h_user_headers; hfp != NIL;
               hfp = hfp->hf_next){
            if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
               if(!any){
                  any = TRU1;
                  fprintf(fp, "212 %s\n", &hfp->hf_dat[0]);
               }

               sin.l = su_cs_len(sin.s = &hfp->hf_dat[hfp->hf_nl +1]);
               mx_mime_display_from_header(&sin, &sou,
                  mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT);

               fprintf(fp, "%s\n", a_DMSG_QUOTE(sou.s));

               su_FREE(sou.s);
            }
         }
         if(!any)
            goto j501cp;
         if(putc('\n', fp) == EOF)
            cp = NIL;
      }
   }else
      goto jecmd;

jleave:
   NYD2_OU;
   return (cp != NIL);

jecmd:
   if(fputs("500\n", fp) == EOF)
      cp = NIL;
   cp = NIL;
   goto jleave;
j505r:
   if(fprintf(fp, "505 read-only: %s\n", cp) < 0)
      cp = NIL;
   goto jleave;
j501cp:
   if(fprintf(fp, "501 %s\n", cp) < 0)
      cp = NIL;
   goto jleave;
}

static boole
a_dmsg__attach(FILE *fp, struct mx_dig_msg_ctx *dmcp,
      struct mx_cmd_arg *args){
   boole status;
   struct mx_attachment *ap;
   char const *cp;
   struct header *hp;
   NYD2_IN;

   hp = dmcp->dmc_hp;

   if(args == NIL){
      cp = su_empty; /* xxx not NIL anyway */
      goto jdefault;
   }

   cp = args->ca_arg.ca_str.s;
   args = args->ca_next;

   if(su_cs_starts_with_case("attribute", cp)){
      if(args == NIL || args->ca_next != NIL)
         goto jecmd;

      cp = args->ca_arg.ca_str.s;
      if((ap = mx_attachments_find(hp->h_attach, cp, NIL)) == NIL)
         goto j501;

jatt_att:
      fprintf(fp, "212 %s\n", a_DMSG_QUOTE(cp));
      if(ap->a_msgno > 0){
         if(fprintf(fp, "message-number %d\n\n", ap->a_msgno) < 0)
            cp = NIL;
      }else{
         fprintf(fp, "creation-name %s\nopen-path %s\nfilename %s\n",
            a_DMSG_QUOTE(ap->a_path_user), a_DMSG_QUOTE(ap->a_path),
            a_DMSG_QUOTE(ap->a_name));
         if((cp = ap->a_content_description) != NIL)
            fprintf(fp, "content-description %s\n", a_DMSG_QUOTE(cp));
         if(ap->a_content_id != NIL)
            fprintf(fp, "content-id %s\n", ap->a_content_id->n_name);
         if((cp = ap->a_content_type) != NIL)
            fprintf(fp, "content-type %s\n", a_DMSG_QUOTE(cp));
         if((cp = ap->a_content_disposition) != NIL)
            fprintf(fp, "content-disposition %s\n", a_DMSG_QUOTE(cp));
         cp = (putc('\n', fp) != EOF) ? su_empty : NIL;
      }
   }else if(su_cs_starts_with_case("attribute-at", cp)){
      uz i;

      if(args == NIL || args->ca_next != NIL)
         goto jecmd;

      if((su_idec_uz_cp(&i, cp = args->ca_arg.ca_str.s, 0, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0)
         goto j505invpos;

      for(ap = hp->h_attach; ap != NIL && --i != 0; ap = ap->a_flink)
         ;
      if(ap != NIL)
         goto jatt_att;
      goto j501;
   }else if(su_cs_starts_with_case("attribute-set", cp)){
      /* ATT-ID KEYWORD VALUE */
      if(args == NIL)
         goto jecmd;

      cp = args->ca_arg.ca_str.s;
      args = args->ca_next;

      if(args == NIL || args->ca_next == NIL || args->ca_next->ca_next != NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((ap = mx_attachments_find(hp->h_attach, cp, NIL)) == NIL)
         goto j501;

jatt_attset:
      if(ap->a_msgno > 0){
         if(fprintf(fp, "505 RFC822 message attachment: %s\n", cp) < 0)
            cp = NIL;
      }else{
         char c;
         char const *keyw, *xcp;

         keyw = args->ca_arg.ca_str.s;
         cp = args->ca_next->ca_arg.ca_str.s;

         for(xcp = cp; (c = *xcp) != '\0'; ++xcp)
            if(su_cs_is_cntrl(c))
               goto j505;
         c = *cp;

         if(!su_cs_cmp_case(keyw, "filename"))
            ap->a_name = (c == '\0') ? ap->a_path_bname : cp;
         else if(!su_cs_cmp_case(keyw, "content-description"))
            ap->a_content_description = (c == '\0') ? NIL : cp;
         else if(!su_cs_cmp_case(keyw, "content-id")){
            ap->a_content_id = NIL;

            if(c != '\0'){
               struct mx_name *np;

               /* XXX lextract->extract_single() */
               np = checkaddrs(lextract(cp, GREF),
                     /*EACM_STRICT | TODO '/' valid!! */ EACM_NOLOG |
                     EACM_NONAME, NIL);
               if(np != NIL && np->n_flink == NIL)
                  ap->a_content_id = np;
               else
                  cp = NIL;
            }
         }else if(!su_cs_cmp_case(keyw, "content-type")){
            if((ap->a_content_type = (c == '\0') ? NIL : cp) != NIL){
               char *cp2;

               for(cp2 = UNCONST(char*,cp); (c = *cp++) != '\0';)
                  *cp2++ = su_cs_to_lower(c);

               if(!mx_mime_type_is_valid(ap->a_content_type, TRU1, FAL0)){
                  ap->a_content_type = NIL;
                  goto j505;
               }
            }
         }else if(!su_cs_cmp_case(keyw, "content-disposition"))
            ap->a_content_disposition = (c == '\0') ? NIL : cp;
         else
            cp = NIL;

         if(cp != NIL){
            uz i;

            for(i = 0; ap != NIL; ++i, ap = ap->a_blink)
               ;
            if(fprintf(fp, "210 %" PRIuZ "\n", i) < 0)
               cp = NIL;
         }else{
            cp = xcp;
            goto j505; /* xxx jecmd; */
         }
      }
   }else if(su_cs_starts_with_case("attribute-set-at", cp)){
      uz i;

      cp = args->ca_arg.ca_str.s;
      args = args->ca_next;

      if(args == NIL || args->ca_next == NIL || args->ca_next->ca_next != NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((su_idec_uz_cp(&i, cp, 0, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0)
         goto j505invpos;

      for(ap = hp->h_attach; ap != NIL && --i != 0; ap = ap->a_flink)
         ;
      if(ap != NIL)
         goto jatt_attset;
      goto j501;
   }else if(su_cs_starts_with_case("insert", cp)){
      BITENUM_IS(u32,mx_attach_error) aerr;

      if(args == NIL || args->ca_next != NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      hp->h_attach = mx_attachments_append(hp->h_attach, args->ca_arg.ca_str.s,
            &aerr, &ap);
      switch(aerr){
      case mx_ATTACHMENTS_ERR_FILE_OPEN: cp = "505"; goto jatt__ins;
      case mx_ATTACHMENTS_ERR_ICONV_FAILED: cp = "506"; goto jatt__ins;
      case mx_ATTACHMENTS_ERR_ICONV_NAVAIL: /* FALLTHRU */
      case mx_ATTACHMENTS_ERR_OTHER: /* FALLTHRU */
      default:
         cp = "501";
jatt__ins:
         if(fprintf(fp, "%s %s\n", cp, a_DMSG_QUOTE(args->ca_arg.ca_str.s)
               ) < 0)
            cp = NIL;
         break;
      case mx_ATTACHMENTS_ERR_NONE:{
         uz i;

         for(i = 0; ap != NIL; ++i, ap = ap->a_blink)
            ;
         if(fprintf(fp, "210 %" PRIuZ "\n", i) < 0)
            cp = NIL;
         }break;
      }
   }else if(su_cs_starts_with_case("list", cp)){
jdefault:
      if(args != NIL)
         goto jecmd;

      if((ap = hp->h_attach) == NIL)
         goto j501;

      fputs("212\n", fp);
      do
         fprintf(fp, "%s\n", a_DMSG_QUOTE(ap->a_path_user));
      while((ap = ap->a_flink) != NIL);
      if(putc('\n', fp) == EOF)
         cp = NIL;
   }else if(su_cs_starts_with_case("remove", cp)){
      if(args == NIL || args->ca_next != NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      cp = args->ca_arg.ca_str.s;
      if((ap = mx_attachments_find(hp->h_attach, cp, &status)) == NIL)
         goto j501;
      if(status == TRUM1)
         goto j506;

      hp->h_attach = mx_attachments_remove(hp->h_attach, ap);
      if(fprintf(fp, "210 %s\n", a_DMSG_QUOTE(cp)) < 0)
         cp = NIL;
   }else if(su_cs_starts_with_case("remove-at", cp)){
      uz i;

      if(args == NIL || args->ca_next != NIL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((su_idec_uz_cp(&i, cp = args->ca_arg.ca_str.s, 0, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0)
         goto j505invpos;

      for(ap = hp->h_attach; ap != NIL && --i != 0; ap = ap->a_flink)
         ;
      if(ap != NIL){
         hp->h_attach = mx_attachments_remove(hp->h_attach, ap);
         if(fprintf(fp, "210 %s\n", cp) < 0)
            cp = NIL;
      }else
         goto j501;
   }else
      goto jecmd;

jleave:
   NYD2_OU;
   return (cp != NIL);

jecmd:
   if(fputs("500\n", fp) == EOF)
      cp = NIL;
   cp = NIL;
   goto jleave;
j501:
   if(fputs("501\n", fp) == EOF)
      cp = NIL;
   goto jleave;
j505:
   if(fputs("505\n", fp) == EOF)
      cp = NIL;
   goto jleave;
j505r:
   if(fprintf(fp, "505 read-only: %s\n", cp) < 0)
      cp = NIL;
   goto jleave;
j505invpos:
   if(fprintf(fp, "505 invalid position: %s\n", cp) < 0)
      cp = NIL;
   goto jleave;
j506:
   if(fputs("506\n", fp) == EOF)
      cp = NIL;
   goto jleave;
}

void
mx_dig_msg_on_mailbox_close(struct mailbox *mbp){ /* XXX HACK <- event! */
   struct mx_dig_msg_ctx *dmcp;
   NYD_IN;

   while((dmcp = mbp->mb_digmsg) != NIL){
      mbp->mb_digmsg = dmcp->dmc_next;
      if(dmcp->dmc_flags & mx_DIG_MSG_FCLOSE)
         fclose(dmcp->dmc_fp);
      if(dmcp->dmc_flags & mx_DIG_MSG_OWN_MEMBAG)
         su_mem_bag_gut(dmcp->dmc_membag);
      su_FREE(dmcp);
   }

   NYD_OU;
}

int
c_digmsg(void *vp){
   char const *cp, *emsg;
   struct mx_dig_msg_ctx *dmcp;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   n_pstate_err_no = su_ERR_NONE;
   cacp = vp;
   cap = cacp->cac_arg;

   if(su_cs_starts_with_case("create", cp = cap->ca_arg.ca_str.s)){
      if(cacp->cac_no < 2 || cacp->cac_no > 3) /* XXX argparse is stupid */
         goto jesynopsis;
      cap = cap->ca_next;

      /* Request to use STDOUT? */
      if(cacp->cac_no == 3){
         cp = cap->ca_next->ca_arg.ca_str.s;
         if(*cp != '-' || cp[1] != '\0'){
            emsg = N_("digmsg: create: invalid I/O channel: %s\n");
            goto jeinval_quote;
         }
      }

      /* First of all, our context object */
      switch(a_dmsg_find(cp = cap->ca_arg.ca_str.s, &dmcp, TRU1)){
      case su_ERR_INVAL:
         emsg = N_("digmsg: create: message number invalid: %s\n");
         goto jeinval_quote;
      case su_ERR_EXIST:
         emsg = N_("digmsg: create: message object already exists: %s\n");
         goto jeinval_quote;
      default:
         break;
      }

      if(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)
         dmcp->dmc_flags = mx_DIG_MSG_COMPOSE | mx_DIG_MSG_COMPOSE_DIGGED;
      else{
         FILE *fp;

         if((fp = setinput(&mb, dmcp->dmc_mp, NEED_HEADER)) == NIL){
            /* XXX Should have panicked before.. */
            su_FREE(dmcp);
            emsg = N_("digmsg: create: mailbox I/O error for message: %s\n");
            goto jeinval_quote;
         }

         su_mem_bag_push(mx_go_data->gdc_membag, dmcp->dmc_membag);
         /* XXX n_header_extract error!! */
         n_header_extract((n_HEADER_EXTRACT_FULL |
               n_HEADER_EXTRACT_PREFILL_RECEIVERS |
               n_HEADER_EXTRACT_IGNORE_FROM_ |
               ((dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)
                  ? n_HEADER_EXTRACT_COMPOSE_MODE : 0)),
               fp, dmcp->dmc_hp, NIL);
         su_mem_bag_pop(mx_go_data->gdc_membag, dmcp->dmc_membag);
      }

      if(cacp->cac_no == 3)
         dmcp->dmc_fp = n_stdout;
      /* For compose mode simply use FS_O_REGISTER, the number of dangling
       * deleted files with open descriptors until next fs_close_all()
       * should be very small; if this paradigm is changed
       * DIG_MSG_COMPOSE_GUT() needs to be adjusted */
      else if((dmcp->dmc_fp = mx_fs_tmp_open("digmsg", (mx_FS_O_RDWR |
               mx_FS_O_UNLINK | (dmcp->dmc_flags & mx_DIG_MSG_COMPOSE
                  ? mx_FS_O_REGISTER : 0)),
               NIL)) != NIL)
         dmcp->dmc_flags |= mx_DIG_MSG_HAVE_FP |
               (dmcp->dmc_flags & mx_DIG_MSG_COMPOSE ? 0 : mx_DIG_MSG_FCLOSE);
      else{
         n_err(_("digmsg: create: cannot create temporary file: %s\n"),
            su_err_doc(n_pstate_err_no = su_err_no()));
         vp = NIL;
         goto jeremove;
      }

      if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)){
         dmcp->dmc_last = NIL;
         if((dmcp->dmc_next = mb.mb_digmsg) != NIL)
            dmcp->dmc_next->dmc_last = dmcp;
         mb.mb_digmsg = dmcp;
      }
   }else if(su_cs_starts_with_case("remove", cp)){
      if(cacp->cac_no != 2)
         goto jesynopsis;
      cap = cap->ca_next;

      switch(a_dmsg_find(cp = cap->ca_arg.ca_str.s, &dmcp, FAL0)){
      case su_ERR_INVAL:
         emsg = N_("digmsg: remove: message number invalid: %s\n");
         goto jeinval_quote;
      default:
         if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE) ||
               (dmcp->dmc_flags & mx_DIG_MSG_COMPOSE_DIGGED))
            break;
         /* FALLTHRU */
      case su_ERR_NOENT:
         emsg = N_("digmsg: remove: no such message object: %s\n");
         goto jeinval_quote;
      }

      if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)){
         if(dmcp->dmc_last != NIL)
            dmcp->dmc_last->dmc_next = dmcp->dmc_next;
         else{
            ASSERT(dmcp == mb.mb_digmsg);
            mb.mb_digmsg = dmcp->dmc_next;
         }
         if(dmcp->dmc_next != NIL)
            dmcp->dmc_next->dmc_last = dmcp->dmc_last;
      }

      if((dmcp->dmc_flags & mx_DIG_MSG_HAVE_FP) &&
            mx_dig_msg_read_overlay == dmcp)
         mx_dig_msg_read_overlay = NIL;

      if(dmcp->dmc_flags & mx_DIG_MSG_FCLOSE)
         fclose(dmcp->dmc_fp);
jeremove:
      if(dmcp->dmc_flags & mx_DIG_MSG_OWN_MEMBAG)
         su_mem_bag_gut(dmcp->dmc_membag);

      if(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)
         dmcp->dmc_flags = mx_DIG_MSG_COMPOSE;
      else
         su_FREE(dmcp);
   }else{
      switch(a_dmsg_find(cp, &dmcp, FAL0)){
      case su_ERR_INVAL:
         emsg = N_("digmsg: message number invalid: %s\n");
         goto jeinval_quote;
      case su_ERR_NOENT:
         emsg = N_("digmsg: no such message object: %s\n");
         goto jeinval_quote;
      default:
         break;
      }
      cap = cap->ca_next;

      if(dmcp->dmc_flags & mx_DIG_MSG_HAVE_FP){
         rewind(dmcp->dmc_fp);
         ftruncate(fileno(dmcp->dmc_fp), 0);
      }

      su_mem_bag_push(mx_go_data->gdc_membag, dmcp->dmc_membag);
      if(!a_dmsg_cmd(dmcp->dmc_fp, dmcp, cap,
            ((cap != NIL) ? cap->ca_next : NIL)))
         vp = NIL;
      su_mem_bag_pop(mx_go_data->gdc_membag, dmcp->dmc_membag);

      if(dmcp->dmc_flags & mx_DIG_MSG_HAVE_FP){
         rewind(dmcp->dmc_fp);
         /* This will be reset by go_input() _if_ we read to EOF */
         mx_dig_msg_read_overlay = dmcp;
      }
   }

jleave:
   NYD_OU;
   return (vp == NIL);

jesynopsis:
   mx_cmd_print_synopsis(mx_cmd_by_arg_desc(cacp->cac_desc), NIL);
   goto jeinval;
jeinval_quote:
   emsg = V_(emsg);
   n_err(emsg, n_shexp_quote_cp(cp, FAL0));
jeinval:
   n_pstate_err_no = su_ERR_INVAL;
   vp = NIL;
   goto jleave;
}

boole
mx_dig_msg_circumflex(struct mx_dig_msg_ctx *dmcp, FILE *fp, char const *cmd){
   /* Identical to (subset of) c_digmsg() cmd-tab */
   mx_CMD_ARG_DESC_SUBCLASS_DEF_NAME(dm, "digmsg", 5, pseudo_cad){
      {mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_HONOUR_STOP,
         n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_TRIM_IFSSPACE},
      {mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION |
            mx_CMD_ARG_DESC_HONOUR_STOP,
         n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg1 */
      {mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION |
            mx_CMD_ARG_DESC_HONOUR_STOP,
         n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg2 */
      {mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION |
            mx_CMD_ARG_DESC_HONOUR_STOP,
         n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg3 */
      {mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION |
            mx_CMD_ARG_DESC_HONOUR_STOP |
            mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_GREEDY_JOIN,
         n_SHEXP_PARSE_TRIM_IFSSPACE} /* arg4 */
   }mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

   struct mx_cmd_arg_ctx cac;
   boole rv;
   NYD_IN;

   cac.cac_desc = mx_CMD_ARG_DESC_SUBCLASS_CAST(&pseudo_cad);
   cac.cac_indat = cmd;
   cac.cac_inlen = UZ_MAX;
   cac.cac_msgflag = cac.cac_msgmask = 0;

   if((rv = mx_cmd_arg_parse(&cac)))
      rv = a_dmsg_cmd(fp, dmcp, cac.cac_arg, cac.cac_arg->ca_next);

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_DIG_MSG
/* s-it-mode */
