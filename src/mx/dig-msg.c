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
 * Copyright (c) 2016 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE dig_msg
#define mx_SOURCE
#define mx_SOURCE_DIG_MSG

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/cmd.h"
#include "mx/file-streams.h"
#include "mx/names.h"

#include "mx/dig-msg.h"
#include "su/code-in.h"

struct mx_dig_msg_ctx *mx_dig_msg_read_overlay; /* XXX HACK */
struct mx_dig_msg_ctx *mx_dig_msg_compose_ctx; /* Or NIL XXX HACK*/

/* Try to convert cp into an unsigned number that corresponds to an existing
 * message number (or ERR_INVAL), search for an existing object (ERR_EXIST if
 * oexcl and exists; ERR_NOENT if not oexcl and does not exist).
 * On oexcl success *dmcp will be n_alloc()ated with .dmc_msgno and .dmc_mp
 * etc. set; but not linked into mb.mb_digmsg and .dmc_fp not created etc. */
static s32 a_dmsg_find(char const *cp, struct mx_dig_msg_ctx **dmcpp,
      boole oexcl);

/* Subcommand drivers */
static boole a_dmsg_cmd(FILE *fp, struct mx_dig_msg_ctx *dmcp, char const *cmd,
      uz cmdl, char const *cp);

static boole a_dmsg__header(FILE *fp, struct mx_dig_msg_ctx *dmcp,
      char *cmda[3]);
static boole a_dmsg__attach(FILE *fp, struct mx_dig_msg_ctx *dmcp,
      char *cmda[3]);

static s32
a_dmsg_find(char const *cp, struct mx_dig_msg_ctx **dmcpp, boole oexcl){
   struct mx_dig_msg_ctx *dmcp;
   s32 rv;
   u32 msgno;
   NYD2_IN;

   if(cp[0] == '-' && cp[1] == '\0'){
      if((dmcp = mx_dig_msg_compose_ctx) != NULL){
         *dmcpp = dmcp;
         if(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE_DIGGED)
            rv = oexcl ? su_ERR_EXIST : su_ERR_NONE;
         else
            rv = oexcl ? su_ERR_NONE : su_ERR_NOENT;
      }else
         rv = su_ERR_INVAL;
      goto jleave;
   }

   if((su_idec_u32_cp(&msgno, cp, 0, NULL
            ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED ||
         msgno == 0 || UCMP(z, msgno, >, msgCount)){
      rv = su_ERR_INVAL;
      goto jleave;
   }

   for(dmcp = mb.mb_digmsg; dmcp != NULL; dmcp = dmcp->dmc_next)
      if(dmcp->dmc_msgno == msgno){
         *dmcpp = dmcp;
         rv = oexcl ? su_ERR_EXIST : su_ERR_NONE;
         goto jleave;
      }
   if(!oexcl){
      rv = su_ERR_NOENT;
      goto jleave;
   }

   *dmcpp = dmcp = n_calloc(1, Z_ALIGN(sizeof *dmcp) + sizeof(struct header));
   dmcp->dmc_mp = &message[msgno - 1];
   dmcp->dmc_flags = mx_DIG_MSG_OWN_MEMBAG |
         ((TRU1/*TODO*/ || !(mb.mb_perm & MB_DELE))
            ? mx_DIG_MSG_RDONLY : mx_DIG_MSG_NONE);
   dmcp->dmc_msgno = msgno;
   dmcp->dmc_hp = (struct header*)P2UZ(&dmcp[1]);
   dmcp->dmc_membag = su_mem_bag_create(&dmcp->dmc__membag_buf[0], 0);
   /* Rest done by caller */
   rv = su_ERR_NONE;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_dmsg_cmd(FILE *fp, struct mx_dig_msg_ctx *dmcp, char const *cmd, uz cmdl,
      char const *cp){
   char *cmda[3];
   boole rv;
   NYD2_IN;

   /* C99 */{
      uz i;

      /* TODO trim+strlist_split(_ifs?)() */
      for(i = 0; i < NELEM(cmda); ++i){
         while(su_cs_is_blank(*cp))
            ++cp;
         if(*cp == '\0')
            cmda[i] = NULL;
         else{
            char const *xp;

            if(i < NELEM(cmda) - 1)
               for(xp = cp++; *cp != '\0' && !su_cs_is_blank(*cp); ++cp)
                  ;
            else{
               /* Last slot takes all the rest of the line, less trailing WS */
               for(xp = cp++; *cp != '\0'; ++cp)
                  ;
               while(su_cs_is_blank(cp[-1]))
                  --cp;
            }
            cmda[i] = savestrbuf(xp, P2UZ(cp - xp));
         }
      }
   }

   if(su_cs_starts_with_case_n("header", cmd, cmdl))
      rv = a_dmsg__header(fp, dmcp, cmda);
   else if(su_cs_starts_with_case_n("attachment", cmd, cmdl)){
      if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)) /* TODO attachment support */
         rv = (fprintf(fp,
               "505 `digmsg attachment' only in compose mode (yet)\n") > 0);
      else
         rv = a_dmsg__attach(fp, dmcp, cmda);
   }else if(su_cs_starts_with_case_n("version", cmd, cmdl)){
      if(cmda[0] != NULL)
         goto jecmd;
      rv = (fputs("210 " mx_DIG_MSG_PLUMBING_VERSION "\n", fp) != EOF);
   }else if((cmdl == 1 && cmd[0] == '?') ||
         su_cs_starts_with_case_n("help", cmd, cmdl)){
      if(cmda[0] != NULL)
         goto jecmd;
      rv = (fputs("211 Omnia vincit Amor et nos cedamos Amori\n", fp) != EOF &&
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
      rv = FAL0;
   }
   fflush(fp);

   NYD2_OU;
   return rv;
}

static boole
a_dmsg__header(FILE *fp, struct mx_dig_msg_ctx *dmcp, char *cmda[3]){
   uz i;
   struct n_header_field *hfp;
   struct mx_name *np, **npp;
   char const *cp;
   struct header *hp;
   NYD2_IN;

   hp = dmcp->dmc_hp;

   /* Strip the optional colon from header names */
   if((cp = cmda[1]) != su_NIL){
      for(i = 0; cp[i] != '\0'; ++i)
         ;
      if(i > 0 && cp[i - 1] == ':'){
         --i;
         cmda[1][i] = '\0';
      }
   }

   if((cp = cmda[0]) == NULL){
      ASSERT(cmda[1] == NULL);
      cp = n_empty; /* xxx not NULL anyway */
      goto jdefault;
   }

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

      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      /* Strip [\r\n] which would render a body invalid XXX all controls? */
      /* C99 */{
         char c;

         for(cp = cmda[2]; (c = *cp) != '\0'; ++cp)
            if(c == '\n' || c == '\r')
               *su_UNCONST(char*,cp) = ' ';
      }

      if(!su_cs_cmp_case(cmda[1], cp = "Subject")){
         if(cmda[2][0] != '\0'){
            if(hp->h_subject != NULL)
               hp->h_subject = savecatsep(hp->h_subject, ' ', cmda[2]);
            else
               hp->h_subject = su_UNCONST(char*,cmda[2]);
            if(fprintf(fp, "210 %s 1\n", cp) < 0)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

      mult_ok = TRU1;
      ntype = GEXTRA | GFULL | GFULLEXTRA;
      eacm = EACM_STRICT;
      mod_suff = su_NIL;

      if(!su_cs_cmp_case(cmda[1], cp = "From")){
         npp = &hp->h_from;
jins:
         aerr = 0;
         /* todo As said above, this should be table driven etc., but.. */
         if(ntype & GBCC_IS_FCC){
            np = nalloc_fcc(cmda[2]);
            if(is_addr_invalid(np, eacm))
               goto jins_505;
         }else{
            if((np = (mult_ok > FAL0 ? lextract : n_extract_single)(cmda[2],
                  ntype | GNULL_OK)) == NULL)
               goto j501cp;

            if((np = checkaddrs(np, eacm, &aerr), aerr != 0)){
jins_505:
               if(fprintf(fp, "505 %s\n", cp) < 0)
                  cp = NULL;
               goto jleave;
            }
         }

         /* Go to the end of the list, track whether it contains any
          * non-deleted entries */
         i = 0;
         if((xnp = *npp) != NULL)
            for(;; xnp = xnp->n_flink){
               if(!(xnp->n_type & GDEL))
                  ++i;
               if(xnp->n_flink == NULL)
                  break;
            }

         if(!mult_ok && (i != 0 || np->n_flink != NULL)){
            if(fprintf(fp, "506 %s\n", cp) < 0)
               cp = NULL;
         }else{
            if(xnp == NULL)
               *npp = np;
            else
               xnp->n_flink = np;
            np->n_blink = xnp;
            if(fprintf(fp, "210 %s %" PRIuZ "\n", cp, ++i) < 0)
               cp = NULL;
         }
         goto jleave;
      }

#undef a_X
#define a_X(F,H,INS) \
   if(!su_cs_cmp_case(cmda[1], cp = F)) {npp = &hp->H; INS; goto jins;}

      if((cp = su_cs_find_c(cmda[1], '?')) != su_NIL){
         mod_suff = cp;
         cmda[1][P2UZ(cp - cmda[1])] = '\0';
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

      if((cp = mod_suff) != su_NIL)
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

      if((cp = n_header_is_known(cmda[1], UZ_MAX)) != NULL)
         goto j505r;

      /* Free-form header fields */
      /* C99 */{
         uz nl, bl;
         struct n_header_field **hfpp;

         for(cp = cmda[1]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmda[1];
               goto j501cp;
            }

         for(i = 0, hfpp = &hp->h_user_headers; *hfpp != NULL; ++i)
            hfpp = &(*hfpp)->hf_next;

         nl = su_cs_len(cp = cmda[1]) +1;
         bl = su_cs_len(cmda[2]) +1;
         *hfpp = hfp = n_autorec_alloc(VSTRUCT_SIZEOF(struct n_header_field,
               hf_dat) + nl + bl);
         hfp->hf_next = NULL;
         hfp->hf_nl = nl - 1;
         hfp->hf_bl = bl - 1;
         su_mem_copy(&hfp->hf_dat[0], cp, nl);
         su_mem_copy(&hfp->hf_dat[nl], cmda[2], bl);
         if(fprintf(fp, "210 %s %" PRIuZ "\n",
               &hfp->hf_dat[0], ++i) < 0)
            cp = NULL;
      }
   }else if(su_cs_starts_with_case("list", cp)){
jdefault:
      if(cmda[1] == NULL){
         fputs("210", fp);
         if(hp->h_subject != NULL) fputs(" Subject", fp);
         if(hp->h_from != NULL) fputs(" From", fp);
         if(hp->h_sender != NULL) fputs(" Sender", fp);
         if(hp->h_to != NULL) fputs(" To", fp);
         if(hp->h_cc != NULL) fputs(" Cc", fp);
         if(hp->h_bcc != NULL) fputs(" Bcc", fp);
         if(hp->h_fcc != NULL) fputs(" Fcc", fp);
         if(hp->h_reply_to != NULL) fputs(" Reply-To", fp);
         if(hp->h_mft != NULL) fputs(" Mail-Followup-To", fp);
         if(hp->h_message_id != NULL) fputs(" Message-ID", fp);
         if(hp->h_ref != NULL) fputs(" References", fp);
         if(hp->h_in_reply_to != NULL) fputs(" In-Reply-To", fp);
         if(hp->h_mailx_command != NULL)
            fputs(" Mailx-Command", fp);
         if(hp->h_mailx_raw_to != NULL) fputs(" Mailx-Raw-To", fp);
         if(hp->h_mailx_raw_cc != NULL) fputs(" Mailx-Raw-Cc", fp);
         if(hp->h_mailx_raw_bcc != NULL)
            fputs(" Mailx-Raw-Bcc", fp);
         if(hp->h_mailx_orig_sender != NULL)
            fputs(" Mailx-Orig-Sender", fp);
         if(hp->h_mailx_orig_from != NULL)
            fputs(" Mailx-Orig-From", fp);
         if(hp->h_mailx_orig_to != NULL)
            fputs(" Mailx-Orig-To", fp);
         if(hp->h_mailx_orig_cc != NULL)
            fputs(" Mailx-Orig-Cc", fp);
         if(hp->h_mailx_orig_bcc != NULL)
            fputs(" Mailx-Orig-Bcc", fp);

         /* Print only one instance of each free-form header */
         for(hfp = hp->h_user_headers; hfp != NULL; hfp = hfp->hf_next){
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
            cp = NULL;
         goto jleave;
      }

      if(cmda[2] != NULL)
         goto jecmd;

      if(!su_cs_cmp_case(cmda[1], cp = "Subject")){
         np = (hp->h_subject != NULL) ? (struct mx_name*)-1 : NULL;
         goto jlist;
      }
      if(!su_cs_cmp_case(cmda[1], cp = "From")){
         np = hp->h_from;
jlist:
         fprintf(fp, "%s %s\n", (np == NULL ? "501" : "210"), cp);
         goto jleave;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(cmda[1], cp = F)) {np = hp->H; goto jlist;}

      a_X("Sender", h_sender);
      a_X("To", h_to);
      a_X("Cc", h_cc);
      a_X("Bcc", h_bcc);
      a_X("Fcc", h_fcc);
      a_X("Reply-To", h_reply_to);
      a_X("Mail-Followup-To", h_mft);
      a_X("Message-ID", h_message_id);
      a_X("References", h_ref);
      a_X("In-Reply-To", h_in_reply_to);

      a_X("Mailx-Raw-To", h_mailx_raw_to);
      a_X("Mailx-Raw-Cc", h_mailx_raw_cc);
      a_X("Mailx-Raw-Bcc", h_mailx_raw_bcc);
      a_X("Mailx-Orig-Sender", h_mailx_orig_sender);
      a_X("Mailx-Orig-From", h_mailx_orig_from);
      a_X("Mailx-Orig-To", h_mailx_orig_to);
      a_X("Mailx-Orig-Cc", h_mailx_orig_cc);
      a_X("Mailx-Orig-Bcc", h_mailx_orig_bcc);

#undef a_X

      if(!su_cs_cmp_case(cmda[1], cp = "Mailx-Command")){
         np = (hp->h_mailx_command != NULL) ? (struct mx_name*)-1 : NULL;
         goto jlist;
      }

      /* Free-form header fields */
      for(cp = cmda[1]; *cp != '\0'; ++cp)
         if(!fieldnamechar(*cp)){
            cp = cmda[1];
            goto j501cp;
         }
      cp = cmda[1];
      for(hfp = hp->h_user_headers;; hfp = hfp->hf_next){
         if(hfp == NULL)
            goto j501cp;
         else if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
            if(fprintf(fp, "210 %s\n", &hfp->hf_dat[0]) < 0)
               cp = NULL;
            break;
         }
      }
   }else if(su_cs_starts_with_case("remove", cp)){
      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if(!su_cs_cmp_case(cmda[1], cp = "Subject")){
         if(hp->h_subject != NULL){
            hp->h_subject = NULL;
            if(fprintf(fp, "210 %s\n", cp) < 0)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

      if(!su_cs_cmp_case(cmda[1], cp = "From")){
         npp = &hp->h_from;
jrem:
         if(*npp != NULL){
            *npp = NULL;
            if(fprintf(fp, "210 %s\n", cp) < 0)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(cmda[1], cp = F)) {npp = &hp->H; goto jrem;}

      a_X("Sender", h_sender);
      a_X("To", h_to);
      a_X("Cc", h_cc);
      a_X("Bcc", h_bcc);
      a_X("Fcc", h_fcc);
      a_X("Reply-To", h_reply_to);
      a_X("Mail-Followup-To", h_mft);
      a_X("Message-ID", h_message_id);
      a_X("References", h_ref);
      a_X("In-Reply-To", h_in_reply_to);

#undef a_X

      if((cp = n_header_is_known(cmda[1], UZ_MAX)) != NULL)
         goto j505r;

      /* Free-form header fields (note j501cp may print non-normalized name) */
      /* C99 */{
         struct n_header_field **hfpp;
         boole any;

         for(cp = cmda[1]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmda[1];
               goto j501cp;
            }
         cp = cmda[1];

         for(any = FAL0, hfpp = &hp->h_user_headers; (hfp = *hfpp) != NULL;){
            if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
               *hfpp = hfp->hf_next;
               if(!any){
                  if(fprintf(fp, "210 %s\n", &hfp->hf_dat[0]) < 0){
                     cp = NULL;
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
      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((su_idec_uz_cp(&i, cmda[2], 0, NULL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0){
         if(fprintf(fp, "505 invalid position: %s\n", cmda[2]) < 0)
            cp = NULL;
         goto jleave;
      }

      if(!su_cs_cmp_case(cmda[1], cp = "Subject")){
         if(hp->h_subject != NULL && i == 1){
            hp->h_subject = NULL;
            if(fprintf(fp, "210 %s 1\n", cp) < 0)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

      if(!su_cs_cmp_case(cmda[1], cp = "From")){
         npp = &hp->h_from;
jremat:
         if((np = *npp) == NULL)
            goto j501cp;
         while(--i != 0 && np != NULL)
            np = np->n_flink;
         if(np == NULL)
            goto j501cp;

         if(np->n_blink != NULL)
            np->n_blink->n_flink = np->n_flink;
         else
            *npp = np->n_flink;
         if(np->n_flink != NULL)
            np->n_flink->n_blink = np->n_blink;

         if(fprintf(fp, "210 %s\n", cp) < 0)
            cp = NULL;
         goto jleave;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(cmda[1], cp = F)) {npp = &hp->H; goto jremat;}

      a_X("Sender", h_sender);
      a_X("To", h_to);
      a_X("Cc", h_cc);
      a_X("Bcc", h_bcc);
      a_X("Fcc", h_fcc);
      a_X("Reply-To", h_reply_to);
      a_X("Mail-Followup-To", h_mft);
      a_X("Message-ID", h_message_id);
      a_X("References", h_ref);
      a_X("In-Reply-To", h_in_reply_to);

#undef a_X

      if((cp = n_header_is_known(cmda[1], UZ_MAX)) != NULL)
         goto j505r;

      /* Free-form header fields */
      /* C99 */{
         struct n_header_field **hfpp;

         for(cp = cmda[1]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmda[1];
               goto j501cp;
            }
         cp = cmda[1];

         for(hfpp = &hp->h_user_headers; (hfp = *hfpp) != NULL;){
            if(--i == 0){
               *hfpp = hfp->hf_next;
               if(fprintf(fp, "210 %s %" PRIuZ "\n",
                     &hfp->hf_dat[0], i) < 0){
                  cp = NULL;
                  goto jleave;
               }
               break;
            }else
               hfpp = &hfp->hf_next;
         }
         if(hfp == NULL)
            goto j501cp;
      }
   }else if(su_cs_starts_with_case("show", cp)){
      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;

      if(!su_cs_cmp_case(cmda[1], cp = "Subject")){
         if(hp->h_subject == NULL)
            goto j501cp;
         if(fprintf(fp, "212 %s\n%s\n\n", cp, hp->h_subject) < 0)
            cp = NULL;
         goto jleave;
      }

      if(!su_cs_cmp_case(cmda[1], cp = "From")){
         np = hp->h_from;
jshow:
         if(np != NULL){
            fprintf(fp, "211 %s\n", cp);
            do if(!(np->n_type & GDEL)){
               switch(np->n_flags & mx_NAME_ADDRSPEC_ISMASK){
               case mx_NAME_ADDRSPEC_ISFILE: cp = n_hy; break;
               case mx_NAME_ADDRSPEC_ISPIPE: cp = "|"; break;
               case mx_NAME_ADDRSPEC_ISNAME: cp = n_ns; break;
               default: cp = np->n_name; break;
               }
               fprintf(fp, "%s %s\n", cp, np->n_fullname);
            }while((np = np->n_flink) != NULL);
            if(putc('\n', fp) == EOF)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

#undef a_X
#define a_X(F,H) \
   if(!su_cs_cmp_case(cmda[1], cp = F)) {np = hp->H; goto jshow;}

      a_X("Sender", h_sender);
      a_X("To", h_to);
      a_X("Cc", h_cc);
      a_X("Bcc", h_bcc);
      a_X("Fcc", h_fcc);
      a_X("Reply-To", h_reply_to);
      a_X("Mail-Followup-To", h_mft);
      a_X("Message-ID", h_message_id);
      a_X("References", h_ref);
      a_X("In-Reply-To", h_in_reply_to);

      a_X("Mailx-Raw-To", h_mailx_raw_to);
      a_X("Mailx-Raw-Cc", h_mailx_raw_cc);
      a_X("Mailx-Raw-Bcc", h_mailx_raw_bcc);
      a_X("Mailx-Orig-Sender", h_mailx_orig_sender);
      a_X("Mailx-Orig-From", h_mailx_orig_from);
      a_X("Mailx-Orig-To", h_mailx_orig_to);
      a_X("Mailx-Orig-Cc", h_mailx_orig_cc);
      a_X("Mailx-Orig-Bcc", h_mailx_orig_bcc);

#undef a_X

      if(!su_cs_cmp_case(cmda[1], cp = "Mailx-Command")){
         if(hp->h_mailx_command == NULL)
            goto j501cp;
         if(fprintf(fp, "212 %s\n%s\n\n",
               cp, hp->h_mailx_command) < 0)
            cp = NULL;
         goto jleave;
      }


      /* Free-form header fields */
      /* C99 */{
         boole any;

         for(cp = cmda[1]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmda[1];
               goto j501cp;
            }
         cp = cmda[1];

         for(any = FAL0, hfp = hp->h_user_headers; hfp != NULL;
               hfp = hfp->hf_next){
            if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
               if(!any)
                  fprintf(fp, "212 %s\n", &hfp->hf_dat[0]);
               any = TRU1;
               fprintf(fp, "%s\n", &hfp->hf_dat[hfp->hf_nl +1]);
            }
         }
         if(any){
            if(putc('\n', fp) == EOF)
               cp = NULL;
         }else
            goto j501cp;
      }
   }else
      goto jecmd;

jleave:
   NYD2_OU;
   return (cp != NULL);

jecmd:
   fputs("500\n", fp);
   cp = NULL;
   goto jleave;
j505r:
   if(fprintf(fp, "505 read-only: %s\n", cp) < 0)
      cp = NULL;
   goto jleave;
j501cp:
   if(fprintf(fp, "501 %s\n", cp) < 0)
      cp = NULL;
   goto jleave;
}

static boole
a_dmsg__attach(FILE *fp, struct mx_dig_msg_ctx *dmcp, char *cmda[3]){
   boole status;
   struct attachment *ap;
   char const *cp;
   struct header *hp;
   NYD2_IN;

   hp = dmcp->dmc_hp;

   if((cp = cmda[0]) == NULL){
      cp = n_empty; /* xxx not NULL anyway */
      goto jdefault;
   }

   if(su_cs_starts_with_case("attribute", cp)){
      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;

      if((ap = n_attachment_find(hp->h_attach, cmda[1], NULL)) != NULL){
jatt_att:
         fprintf(fp, "212 %s\n", cmda[1]);
         if(ap->a_msgno > 0){
            if(fprintf(fp, "message-number %d\n\n", ap->a_msgno) < 0)
               cp = NULL;
         }else{
            fprintf(fp,
               "creation-name %s\nopen-path %s\nfilename %s\n",
               ap->a_path_user, ap->a_path, ap->a_name);
            if(ap->a_content_description != NULL)
               fprintf(fp, "content-description %s\n",
                  ap->a_content_description);
            if(ap->a_content_id != NULL)
               fprintf(fp, "content-id %s\n",
                  ap->a_content_id->n_name);
            if(ap->a_content_type != NULL)
               fprintf(fp, "content-type %s\n", ap->a_content_type);
            if(ap->a_content_disposition != NULL)
               fprintf(fp, "content-disposition %s\n",
                  ap->a_content_disposition);
            if(putc('\n', fp) == EOF)
               cp = NULL;
         }
      }else{
         if(fputs("501\n", fp) == EOF)
            cp = NULL;
      }
   }else if(su_cs_starts_with_case("attribute-at", cp)){
      uz i;

      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;

      if((su_idec_uz_cp(&i, cmda[1], 0, NULL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0){
         if(fprintf(fp, "505 invalid position: %s\n", cmda[1]) < 0)
            cp = NULL;
      }else{
         for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
            ;
         if(ap != NULL)
            goto jatt_att;
         else{
            if(fputs("501\n", fp) == EOF)
               cp = NULL;
         }
      }
   }else if(su_cs_starts_with_case("attribute-set", cp)){
      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((ap = n_attachment_find(hp->h_attach, cmda[1], NULL)) != NULL){
jatt_attset:
         if(ap->a_msgno > 0){
            if(fprintf(fp, "505 RFC822 message attachment: %s\n",
                  cmda[1]) < 0)
               cp = NULL;
         }else{
            char c, *keyw;

            cp = keyw = cmda[2];
            while((c = *cp) != '\0' && !su_cs_is_blank(c))
               ++cp;
            *UNCONST(char*,cp++) = '\0';

            if(c != '\0'){
               while((c = *cp) != '\0' && su_cs_is_blank(c))
                  ++cp;
               if(c != '\0'){
                  char *xp;

                  /* Strip [\r\n] which would render a parameter invalid XXX
                   * XXX all controls? */
                  for(xp = su_UNCONST(char*,cp); (c = *xp) != '\0'; ++xp)
                     if(c == '\n' || c == '\r')
                        *xp = ' ';
                  c = *cp;
               }
            }

            if(!su_cs_cmp_case(keyw, "filename"))
               ap->a_name = (c == '\0') ? ap->a_path_bname : cp;
            else if(!su_cs_cmp_case(keyw, "content-description"))
               ap->a_content_description = (c == '\0') ? NULL : cp;
            else if(!su_cs_cmp_case(keyw, "content-id")){
               ap->a_content_id = NULL;

               if(c != '\0'){
                  struct mx_name *np;

                  /* XXX lextract->extract_single() */
                  np = checkaddrs(lextract(cp, GREF),
                        /*EACM_STRICT | TODO '/' valid!! */ EACM_NOLOG |
                        EACM_NONAME, NULL);
                  if(np != NULL && np->n_flink == NULL)
                     ap->a_content_id = np;
                  else
                     cp = NULL;
               }
            }else if(!su_cs_cmp_case(keyw, "content-type"))
               ap->a_content_type = (c == '\0') ? NULL : cp;
            else if(!su_cs_cmp_case(keyw, "content-disposition"))
               ap->a_content_disposition = (c == '\0') ? NULL : cp;
            else
               cp = NULL;

            if(cp != NULL){
               uz i;

               for(i = 0; ap != NULL; ++i, ap = ap->a_blink)
                  ;
               if(fprintf(fp, "210 %" PRIuZ "\n", i) < 0)
                  cp = NULL;
            }else{
               if(fputs("505\n", fp) == EOF)
                  cp = NULL;
            }
         }
      }else{
         if(fputs("501\n", fp) == EOF)
            cp = NULL;
      }
   }else if(su_cs_starts_with_case("attribute-set-at", cp)){
      uz i;

      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((su_idec_uz_cp(&i, cmda[1], 0, NULL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0){
         if(fprintf(fp, "505 invalid position: %s\n", cmda[1]) < 0)
            cp = NULL;
      }else{
         for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
            ;
         if(ap != NULL)
            goto jatt_attset;
         else{
            if(fputs("501\n", fp) == EOF)
               cp = NULL;
         }
      }
   }else if(su_cs_starts_with_case("insert", cp)){
      BITENUM_IS(u32,n_attach_error) aerr;

      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      hp->h_attach = n_attachment_append(hp->h_attach, cmda[1], &aerr, &ap);
      switch(aerr){
      case n_ATTACH_ERR_FILE_OPEN: cp = "505"; goto jatt_ins;
      case n_ATTACH_ERR_ICONV_FAILED: cp = "506"; goto jatt_ins;
      case n_ATTACH_ERR_ICONV_NAVAIL: /* FALLTHRU */
      case n_ATTACH_ERR_OTHER: /* FALLTHRU */
      default:
         cp = "501";
jatt_ins:
         if(fprintf(fp, "%s %s\n", cp, cmda[1]) < 0)
            cp = NULL;
         break;
      case n_ATTACH_ERR_NONE:{
         uz i;

         for(i = 0; ap != NULL; ++i, ap = ap->a_blink)
            ;
         if(fprintf(fp, "210 %" PRIuZ "\n", i) < 0)
            cp = NULL;
         }break;
      }
      goto jleave;
   }else if(su_cs_starts_with_case("list", cp)){
jdefault:
      if(cmda[1] != NULL)
         goto jecmd;

      if((ap = hp->h_attach) != NULL){
         fputs("212\n", fp);
         do
            fprintf(fp, "%s\n", ap->a_path_user);
         while((ap = ap->a_flink) != NULL);
         if(putc('\n', fp) == EOF)
            cp = NULL;
      }else{
         if(fputs("501\n", fp) == EOF)
            cp = NULL;
      }
   }else if(su_cs_starts_with_case("remove", cp)){
      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((ap = n_attachment_find(hp->h_attach, cmda[1], &status)) != NULL){
         if(status == TRUM1){
            if(fputs("506\n", fp) == EOF)
               cp = NULL;
         }else{
            hp->h_attach = n_attachment_remove(hp->h_attach, ap);
            if(fprintf(fp, "210 %s\n", cmda[1]) < 0)
               cp = NULL;
         }
      }else{
         if(fputs("501\n", fp) == EOF)
            cp = NULL;
      }
   }else if(su_cs_starts_with_case("remove-at", cp)){
      uz i;

      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & mx_DIG_MSG_RDONLY)
         goto j505r;

      if((su_idec_uz_cp(&i, cmda[1], 0, NULL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || i == 0){
         if(fprintf(fp, "505 invalid position: %s\n", cmda[1]) < 0)
            cp = NULL;
      }else{
         for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
            ;
         if(ap != NULL){
            hp->h_attach = n_attachment_remove(hp->h_attach, ap);
            if(fprintf(fp, "210 %s\n", cmda[1]) < 0)
               cp = NULL;
         }else{
            if(fputs("501\n", fp) == EOF)
               cp = NULL;
         }
      }
   }else
      goto jecmd;

jleave:
   NYD2_OU;
   return (cp != NULL);
jecmd:
   (void)fputs("500\n", fp);
   cp = NULL;
   goto jleave;
j505r:
   if(fprintf(fp, "505 read-only: %s\n", cp) < 0)
      cp = NULL;
   goto jleave;
}

void
mx_dig_msg_on_mailbox_close(struct mailbox *mbp){ /* XXX HACK <- event! */
   struct mx_dig_msg_ctx *dmcp;
   NYD_IN;

   while((dmcp = mbp->mb_digmsg) != NULL){
      mbp->mb_digmsg = dmcp->dmc_next;
      if(dmcp->dmc_flags & mx_DIG_MSG_FCLOSE)
         fclose(dmcp->dmc_fp);
      if(dmcp->dmc_flags & mx_DIG_MSG_OWN_MEMBAG)
         su_mem_bag_gut(dmcp->dmc_membag);
      n_free(dmcp);
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
      if(cacp->cac_no < 2 || cacp->cac_no > 3)
         goto jesynopsis;
      cap = cap->ca_next;

      /* Request to use STDOUT? */
      if(cacp->cac_no == 3){
         cp = cap->ca_next->ca_arg.ca_str.s;
         if(*cp != '-' || cp[1] != '\0'){
            emsg = N_("`digmsg': create: invalid I/O channel: %s\n");
            goto jeinval_quote;
         }
      }

      /* First of all, our context object */
      switch(a_dmsg_find(cp = cap->ca_arg.ca_str.s, &dmcp, TRU1)){
      case su_ERR_INVAL:
         emsg = N_("`digmsg': create: message number invalid: %s\n");
         goto jeinval_quote;
      case su_ERR_EXIST:
         emsg = N_("`digmsg': create: message object already exists: %s\n");
         goto jeinval_quote;
      default:
         break;
      }

      if(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)
         dmcp->dmc_flags = mx_DIG_MSG_COMPOSE | mx_DIG_MSG_COMPOSE_DIGGED;
      else{
         FILE *fp;

         if((fp = setinput(&mb, dmcp->dmc_mp, NEED_HEADER)) == NULL){
            /* XXX Should have paniced before.. */
            n_free(dmcp);
            emsg = N_("`digmsg': create: mailbox I/O error for message: %s\n");
            goto jeinval_quote;
         }

         su_mem_bag_push(n_go_data->gdc_membag, dmcp->dmc_membag);
         /* XXX n_header_extract error!! */
         n_header_extract((n_HEADER_EXTRACT_FULL |
               n_HEADER_EXTRACT_PREFILL_RECEIVERS |
               n_HEADER_EXTRACT_IGNORE_FROM_), fp, dmcp->dmc_hp, NULL);
         su_mem_bag_pop(n_go_data->gdc_membag, dmcp->dmc_membag);
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
         n_err(_("`digmsg': create: cannot create temporary file: %s\n"),
            su_err_doc(n_pstate_err_no = su_err_no()));
         vp = NULL;
         goto jeremove;
      }

      if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)){
         dmcp->dmc_last = NULL;
         if((dmcp->dmc_next = mb.mb_digmsg) != NULL)
            dmcp->dmc_next->dmc_last = dmcp;
         mb.mb_digmsg = dmcp;
      }
   }else if(su_cs_starts_with_case("remove", cp)){
      if(cacp->cac_no != 2)
         goto jesynopsis;
      cap = cap->ca_next;

      switch(a_dmsg_find(cp = cap->ca_arg.ca_str.s, &dmcp, FAL0)){
      case su_ERR_INVAL:
         emsg = N_("`digmsg': remove: message number invalid: %s\n");
         goto jeinval_quote;
      default:
         if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE) ||
               (dmcp->dmc_flags & mx_DIG_MSG_COMPOSE_DIGGED))
            break;
         /* FALLTHRU */
      case su_ERR_NOENT:
         emsg = N_("`digmsg': remove: no such message object: %s\n");
         goto jeinval_quote;
      }

      if(!(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)){
         if(dmcp->dmc_last != NULL)
            dmcp->dmc_last->dmc_next = dmcp->dmc_next;
         else{
            ASSERT(dmcp == mb.mb_digmsg);
            mb.mb_digmsg = dmcp->dmc_next;
         }
         if(dmcp->dmc_next != NULL)
            dmcp->dmc_next->dmc_last = dmcp->dmc_last;
      }

      if(dmcp->dmc_flags & mx_DIG_MSG_FCLOSE)
         fclose(dmcp->dmc_fp);
jeremove:
      if(dmcp->dmc_flags & mx_DIG_MSG_OWN_MEMBAG)
         su_mem_bag_gut(dmcp->dmc_membag);

      if(dmcp->dmc_flags & mx_DIG_MSG_COMPOSE)
         dmcp->dmc_flags = mx_DIG_MSG_COMPOSE;
      else
         n_free(dmcp);
   }else{
      switch(a_dmsg_find(cp, &dmcp, FAL0)){
      case su_ERR_INVAL:
         emsg = N_("`digmsg': message number invalid: %s\n");
         goto jeinval_quote;
      case su_ERR_NOENT:
         emsg = N_("`digmsg': no such message object: %s\n");
         goto jeinval_quote;
      default:
         break;
      }
      cap = cap->ca_next;

      if(dmcp->dmc_flags & mx_DIG_MSG_HAVE_FP){
         rewind(dmcp->dmc_fp);
         ftruncate(fileno(dmcp->dmc_fp), 0);
      }

      su_mem_bag_push(n_go_data->gdc_membag, dmcp->dmc_membag);
      /* C99 */{
         struct str cmds_b, *cmdsp;

         cp = n_empty;
         if(cap == NULL){ /* XXX cmd_arg_parse is stupid */
            cmdsp = &cmds_b;
            cmdsp->s = su_UNCONST(char*,cp);
            cmdsp->l = 0;
         }else{
            cmdsp = &cap->ca_arg.ca_str;
            if((cap = cap->ca_next) != NULL)
               cp = cap->ca_arg.ca_str.s;
         }
         if(!a_dmsg_cmd(dmcp->dmc_fp, dmcp, cmdsp->s, cmdsp->l, cp))
            vp = NULL;
      }
      su_mem_bag_pop(n_go_data->gdc_membag, dmcp->dmc_membag);

      if(dmcp->dmc_flags & mx_DIG_MSG_HAVE_FP){
         rewind(dmcp->dmc_fp);
         mx_dig_msg_read_overlay = dmcp;
      }
   }

jleave:
   NYD_OU;
   return (vp == NULL);

jesynopsis:
   mx_cmd_print_synopsis(mx_cmd_firstfit("digmsg"), NIL);
   goto jeinval;
jeinval_quote:
   emsg = V_(emsg);
   n_err(emsg, n_shexp_quote_cp(cp, FAL0));
jeinval:
   n_pstate_err_no = su_ERR_INVAL;
   vp = NULL;
   goto jleave;
}

boole
mx_dig_msg_circumflex(struct mx_dig_msg_ctx *dmcp, FILE *fp, char const *cmd){
   boole rv;
   char c;
   char const *cp, *cmd_top;
   NYD_IN;

   cp = cmd;
   while(su_cs_is_blank(*cp))
      ++cp;
   cmd = cp;
   for(cmd_top = cp; (c = *cp) != '\0'; cmd_top = ++cp)
      if(su_cs_is_blank(c))
         break;

   rv = a_dmsg_cmd(fp, dmcp, cmd, P2UZ(cmd_top - cmd), cp);
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
