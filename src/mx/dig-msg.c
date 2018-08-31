/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Dig message objects. TODO Very very restricted (especially non-compose)
 *@ On protocol change adjust config.h:n_DIG_MSG_PLUMBING_VERSION + `~^' manual
 *@ TODO - a_dmsg_cmd() should generate string lists, not perform real I/O.
 *@ TODO   I.e., drop FILE* arg, generate stringlist; up to callers...
 *@ TODO - With our own I/O there should then be a StringListDevice as the
 *@ TODO   owner and I/O overlay provider: NO temporary file (sic)!
 *@ XXX - Multiple objects per message could be possible (a_dmsg_find()),
 *@ XXX   except in compose mode
 *
 * Copyright (c) 2016 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/* Try to convert cp into an unsigned number that corresponds to an existing
 * message number (or ERR_INVAL), search for an existing object (ERR_EXIST if
 * oexcl and exists; ERR_NOENT if not oexcl and does not exist).
 * On oexcl success *dmcp will be n_alloc()ated with .dmc_msgno and .dmc_mp
 * etc. set; but not linked into mb.mb_digmsg and .dmc_fp not created etc. */
static si32_t a_dmsg_find(char const *cp, struct n_dig_msg_ctx **dmcpp,
               bool_t oexcl);

/* Subcommand drivers */
static bool_t a_dmsg_cmd(FILE *fp, struct n_dig_msg_ctx *dmcp, char const *cmd,
               uiz_t cmdl, char const *cp);

static bool_t a_dmsg__header(FILE *fp, struct n_dig_msg_ctx *dmcp,
               char const *cmda[3]);
static bool_t a_dmsg__attach(FILE *fp, struct n_dig_msg_ctx *dmcp,
               char const *cmda[3]);

static si32_t
a_dmsg_find(char const *cp, struct n_dig_msg_ctx **dmcpp, bool_t oexcl){
   struct n_dig_msg_ctx *dmcp;
   si32_t rv;
   ui32_t msgno;
   NYD2_IN;

   if(cp[0] == '-' && cp[1] == '\0'){
      if((dmcp = n_dig_msg_compose_ctx) != NULL){
         *dmcpp = dmcp;
         if(dmcp->dmc_flags & n_DIG_MSG_COMPOSE_DIGGED)
            rv = oexcl ? n_ERR_EXIST : n_ERR_NONE;
         else
            rv = oexcl ? n_ERR_NONE : n_ERR_NOENT;
      }else
         rv = n_ERR_INVAL;
      goto jleave;
   }

   if((n_idec_ui32_cp(&msgno, cp, 0, NULL
            ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
         ) != n_IDEC_STATE_CONSUMED ||
         msgno == 0 || UICMP(z, msgno, >, msgCount)){
      rv = n_ERR_INVAL;
      goto jleave;
   }

   for(dmcp = mb.mb_digmsg; dmcp != NULL; dmcp = dmcp->dmc_next)
      if(dmcp->dmc_msgno == msgno){
         *dmcpp = dmcp;
         rv = oexcl ? n_ERR_EXIST : n_ERR_NONE;
         goto jleave;
      }
   if(!oexcl){
      rv = n_ERR_NOENT;
      goto jleave;
   }

   *dmcpp = dmcp = n_calloc(1, n_ALIGN(sizeof *dmcp) + sizeof(struct header));
   dmcp->dmc_mp = &message[msgno - 1];
   dmcp->dmc_flags = n_DIG_MSG_OWN_MEMPOOL |
         ((TRU1/*TODO*/ || !(mb.mb_perm & MB_DELE))
            ? n_DIG_MSG_RDONLY : n_DIG_MSG_NONE);
   dmcp->dmc_msgno = msgno;
   dmcp->dmc_hp = (struct header*)PTR2SIZE(&dmcp[1]);
   dmcp->dmc_mempool = dmcp->dmc_mempool_buf;
   /* Rest done by caller */
   rv = n_ERR_NONE;
jleave:
   NYD2_OU;
   return rv;
}

static bool_t
a_dmsg_cmd(FILE *fp, struct n_dig_msg_ctx *dmcp, char const *cmd, uiz_t cmdl,
      char const *cp){
   char const *cmda[3];
   bool_t rv;
   NYD2_IN;

   /* C99 */{
      size_t i;

      /* TODO trim+strlist_split(_ifs?)() */
      for(i = 0; i < n_NELEM(cmda); ++i){
         while(blankchar(*cp))
            ++cp;
         if(*cp == '\0')
            cmda[i] = NULL;
         else{
            if(i < n_NELEM(cmda) - 1)
               for(cmda[i] = cp++; *cp != '\0' && !blankchar(*cp); ++cp)
                  ;
            else{
               /* Last slot takes all the rest of the line, less trailing WS */
               for(cmda[i] = cp++; *cp != '\0'; ++cp)
                  ;
               while(blankchar(cp[-1]))
                  --cp;
            }
            cmda[i] = savestrbuf(cmda[i], PTR2SIZE(cp - cmda[i]));
         }
      }
   }

   if(is_ascncaseprefix(cmd, "header", cmdl))
      rv = a_dmsg__header(fp, dmcp, cmda);
   else if(is_ascncaseprefix(cmd, "attachment", cmdl)){
      if(!(dmcp->dmc_flags & n_DIG_MSG_COMPOSE)) /* TODO attachment support */
         rv = (fprintf(fp,
               "505 `digmsg attachment' only in compose mode (yet)\n") > 0);
      else
         rv = a_dmsg__attach(fp, dmcp, cmda);
   }else if(is_ascncaseprefix(cmd, "version", cmdl)){
      if(cmda[0] != NULL)
         goto jecmd;
      rv = (fputs("210 " n_DIG_MSG_PLUMBING_VERSION "\n", fp) != EOF);
   }else{
jecmd:
      fputs("500\n", fp);
      rv = FAL0;
   }
   fflush(fp);

   NYD2_OU;
   return rv;
}

static bool_t
a_dmsg__header(FILE *fp, struct n_dig_msg_ctx *dmcp, char const *cmda[3]){
   uiz_t i;
   struct n_header_field *hfp;
   struct name *np, **npp;
   char const *cp;
   struct header *hp;
   NYD2_IN;

   hp = dmcp->dmc_hp;

   if((cp = cmda[0]) == NULL){
      cp = n_empty; /* xxx not NULL anyway */
      goto jdefault;
   }

   if(is_asccaseprefix(cp, "insert")){ /* TODO LOGIC BELONGS head.c
       * TODO That is: Header::factory(string) -> object (blahblah).
       * TODO I.e., as long as we don't have regular RFC compliant parsers
       * TODO which differentiate in between structured and unstructured
       * TODO header fields etc., a little workaround */
      struct name *xnp;
      si8_t aerr;
      enum expand_addr_check_mode eacm;
      enum gfield ntype;
      bool_t mult_ok;

      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
         goto j505r;

      /* Strip [\r\n] which would render a body invalid XXX all controls? */
      /* C99 */{
         char *xp, c;

         cmda[2] = xp = savestr(cmda[2]);
         for(; (c = *xp) != '\0'; ++xp)
            if(c == '\n' || c == '\r')
               *xp = ' ';
      }

      if(!asccasecmp(cmda[1], cp = "Subject")){
         if(cmda[2][0] != '\0'){
            if(hp->h_subject != NULL)
               hp->h_subject = savecatsep(hp->h_subject, ' ', cmda[2]);
            else
               hp->h_subject = n_UNCONST(cmda[2]);
            if(fprintf(fp, "210 %s 1\n", cp) < 0)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

      mult_ok = TRU1;
      ntype = GEXTRA | GFULL | GFULLEXTRA;
      eacm = EACM_STRICT;

      if(!asccasecmp(cmda[1], cp = "From")){
         npp = &hp->h_from;
jins:
         aerr = 0;
         /* todo As said above, this should be table driven etc., but.. */
         if(ntype & GBCC_IS_FCC){
            np = nalloc_fcc(cmda[2]);
            if(is_addr_invalid(np, eacm))
               goto jins_505;
         }else{
            if((np = lextract(cmda[2], ntype)) == NULL)
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
      if(!asccasecmp(cmda[1], cp = "Sender")){
         mult_ok = FAL0;
         npp = &hp->h_sender;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "To")){
         npp = &hp->h_to;
         ntype = GTO | GFULL;
         eacm = EACM_NORMAL | EAF_NAME;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "Cc")){
         npp = &hp->h_cc;
         ntype = GCC | GFULL;
         eacm = EACM_NORMAL | EAF_NAME;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "Bcc")){
         npp = &hp->h_bcc;
         ntype = GBCC | GFULL;
         eacm = EACM_NORMAL | EAF_NAME;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "Fcc")){
         npp = &hp->h_fcc;
         ntype = GBCC | GBCC_IS_FCC;
         eacm = EACM_NORMAL /* Not | EAF_FILE, depend on *expandaddr*! */;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "Reply-To")){
         npp = &hp->h_reply_to;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "Mail-Followup-To")){
         npp = &hp->h_mft;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "Message-ID")){
         mult_ok = FAL0;
         npp = &hp->h_message_id;
         ntype = GREF;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "References")){
         npp = &hp->h_ref;
         ntype = GREF;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmda[1], cp = "In-Reply-To")){
         npp = &hp->h_in_reply_to;
         ntype = GREF;
         eacm = EACM_NONAME;
         goto jins;
      }

      if((cp = n_header_is_known(cmda[1], UIZ_MAX)) != NULL)
         goto j505r;

      /* Free-form header fields */
      /* C99 */{
         size_t nl, bl;
         struct n_header_field **hfpp;

         for(cp = cmda[1]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmda[1];
               goto j501cp;
            }

         for(i = 0, hfpp = &hp->h_user_headers; *hfpp != NULL; ++i)
            hfpp = &(*hfpp)->hf_next;

         nl = strlen(cp = cmda[1]) +1;
         bl = strlen(cmda[2]) +1;
         *hfpp = hfp = n_autorec_alloc(n_VSTRUCT_SIZEOF(struct n_header_field,
               hf_dat) + nl + bl);
         hfp->hf_next = NULL;
         hfp->hf_nl = nl - 1;
         hfp->hf_bl = bl - 1;
         memcpy(&hfp->hf_dat[0], cp, nl);
         memcpy(&hfp->hf_dat[nl], cmda[2], bl);
         if(fprintf(fp, "210 %s %" PRIuZ "\n",
               &hfp->hf_dat[0], ++i) < 0)
            cp = NULL;
      }
   }else if(is_asccaseprefix(cp, "list")){
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
               }else if(!asccasecmp(&hfpx->hf_dat[0], &hfp->hf_dat[0]))
                  break;
         }
         if(putc('\n', fp) == EOF)
            cp = NULL;
         goto jleave;
      }

      if(cmda[2] != NULL)
         goto jecmd;

      if(!asccasecmp(cmda[1], cp = "Subject")){
         np = (hp->h_subject != NULL) ? (struct name*)-1 : NULL;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "From")){
         np = hp->h_from;
jlist:
         fprintf(fp, "%s %s\n", (np == NULL ? "501" : "210"), cp);
         goto jleave;
      }
      if(!asccasecmp(cmda[1], cp = "Sender")){
         np = hp->h_sender;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "To")){
         np = hp->h_to;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Cc")){
         np = hp->h_cc;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Bcc")){
         np = hp->h_bcc;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Fcc")){
         np = hp->h_fcc;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Reply-To")){
         np = hp->h_reply_to;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mail-Followup-To")){
         np = hp->h_mft;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Message-ID")){
         np = hp->h_message_id;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "References")){
         np = hp->h_ref;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "In-Reply-To")){
         np = hp->h_in_reply_to;
         goto jlist;
      }

      if(!asccasecmp(cmda[1], cp = "Mailx-Command")){
         np = (hp->h_mailx_command != NULL) ? (struct name*)-1 : NULL;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Raw-To")){
         np = hp->h_mailx_raw_to;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Raw-Cc")){
         np = hp->h_mailx_raw_cc;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Raw-Bcc")){
         np = hp->h_mailx_raw_bcc;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-From")){
         np = hp->h_mailx_orig_from;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-To")){
         np = hp->h_mailx_orig_to;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-Cc")){
         np = hp->h_mailx_orig_cc;
         goto jlist;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-Bcc")){
         np = hp->h_mailx_orig_bcc;
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
         else if(!asccasecmp(cp, &hfp->hf_dat[0])){
            if(fprintf(fp, "210 %s\n", &hfp->hf_dat[0]) < 0)
               cp = NULL;
            break;
         }
      }
   }else if(is_asccaseprefix(cp, "remove")){
      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
         goto j505r;

      if(!asccasecmp(cmda[1], cp = "Subject")){
         if(hp->h_subject != NULL){
            hp->h_subject = NULL;
            if(fprintf(fp, "210 %s\n", cp) < 0)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

      if(!asccasecmp(cmda[1], cp = "From")){
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
      if(!asccasecmp(cmda[1], cp = "Sender")){
         npp = &hp->h_sender;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "To")){
         npp = &hp->h_to;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "Cc")){
         npp = &hp->h_cc;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "Bcc")){
         npp = &hp->h_bcc;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "Fcc")){
         npp = &hp->h_fcc;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "Reply-To")){
         npp = &hp->h_reply_to;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "Mail-Followup-To")){
         npp = &hp->h_mft;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "Message-ID")){
         npp = &hp->h_message_id;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "References")){
         npp = &hp->h_ref;
         goto jrem;
      }
      if(!asccasecmp(cmda[1], cp = "In-Reply-To")){
         npp = &hp->h_in_reply_to;
         goto jrem;
      }

      if((cp = n_header_is_known(cmda[1], UIZ_MAX)) != NULL)
         goto j505r;

      /* Free-form header fields (note j501cp may print non-normalized name) */
      /* C99 */{
         struct n_header_field **hfpp;
         bool_t any;

         for(cp = cmda[1]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmda[1];
               goto j501cp;
            }
         cp = cmda[1];

         for(any = FAL0, hfpp = &hp->h_user_headers; (hfp = *hfpp) != NULL;){
            if(!asccasecmp(cp, &hfp->hf_dat[0])){
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
   }else if(is_asccaseprefix(cp, "remove-at")){
      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
         goto j505r;

      if((n_idec_uiz_cp(&i, cmda[2], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0){
         if(fprintf(fp, "505 invalid position: %s\n", cmda[2]) < 0)
            cp = NULL;
         goto jleave;
      }

      if(!asccasecmp(cmda[1], cp = "Subject")){
         if(hp->h_subject != NULL && i == 1){
            hp->h_subject = NULL;
            if(fprintf(fp, "210 %s 1\n", cp) < 0)
               cp = NULL;
            goto jleave;
         }else
            goto j501cp;
      }

      if(!asccasecmp(cmda[1], cp = "From")){
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
      if(!asccasecmp(cmda[1], cp = "Sender")){
         npp = &hp->h_sender;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "To")){
         npp = &hp->h_to;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "Cc")){
         npp = &hp->h_cc;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "Bcc")){
         npp = &hp->h_bcc;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "Fcc")){
         npp = &hp->h_fcc;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "Reply-To")){
         npp = &hp->h_reply_to;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "Mail-Followup-To")){
         npp = &hp->h_mft;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "Message-ID")){
         npp = &hp->h_message_id;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "References")){
         npp = &hp->h_ref;
         goto jremat;
      }
      if(!asccasecmp(cmda[1], cp = "In-Reply-To")){
         npp = &hp->h_in_reply_to;
         goto jremat;
      }

      if((cp = n_header_is_known(cmda[1], UIZ_MAX)) != NULL)
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
   }else if(is_asccaseprefix(cp, "show")){
      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;

      if(!asccasecmp(cmda[1], cp = "Subject")){
         if(hp->h_subject == NULL)
            goto j501cp;
         if(fprintf(fp, "212 %s\n%s\n\n", cp, hp->h_subject) < 0)
            cp = NULL;
         goto jleave;
      }

      if(!asccasecmp(cmda[1], cp = "From")){
         np = hp->h_from;
jshow:
         if(np != NULL){
            fprintf(fp, "211 %s\n", cp);
            do if(!(np->n_type & GDEL)){
               switch(np->n_flags & NAME_ADDRSPEC_ISMASK){
               case NAME_ADDRSPEC_ISFILE: cp = n_hy; break;
               case NAME_ADDRSPEC_ISPIPE: cp = "|"; break;
               case NAME_ADDRSPEC_ISNAME: cp = n_ns; break;
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
      if(!asccasecmp(cmda[1], cp = "Sender")){
         np = hp->h_sender;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "To")){
         np = hp->h_to;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Cc")){
         np = hp->h_cc;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Bcc")){
         np = hp->h_bcc;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Fcc")){
         np = hp->h_fcc;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Reply-To")){
         np = hp->h_reply_to;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Mail-Followup-To")){
         np = hp->h_mft;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Message-ID")){
         np = hp->h_message_id;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "References")){
         np = hp->h_ref;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "In-Reply-To")){
         np = hp->h_in_reply_to;
         goto jshow;
      }

      if(!asccasecmp(cmda[1], cp = "Mailx-Command")){
         if(hp->h_mailx_command == NULL)
            goto j501cp;
         if(fprintf(fp, "212 %s\n%s\n\n",
               cp, hp->h_mailx_command) < 0)
            cp = NULL;
         goto jleave;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Raw-To")){
         np = hp->h_mailx_raw_to;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Raw-Cc")){
         np = hp->h_mailx_raw_cc;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Raw-Bcc")){
         np = hp->h_mailx_raw_bcc;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-From")){
         np = hp->h_mailx_orig_from;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-To")){
         np = hp->h_mailx_orig_to;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-Cc")){
         np = hp->h_mailx_orig_cc;
         goto jshow;
      }
      if(!asccasecmp(cmda[1], cp = "Mailx-Orig-Bcc")){
         np = hp->h_mailx_orig_bcc;
         goto jshow;
      }

      /* Free-form header fields */
      /* C99 */{
         bool_t any;

         for(cp = cmda[1]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmda[1];
               goto j501cp;
            }
         cp = cmda[1];

         for(any = FAL0, hfp = hp->h_user_headers; hfp != NULL;
               hfp = hfp->hf_next){
            if(!asccasecmp(cp, &hfp->hf_dat[0])){
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

static bool_t
a_dmsg__attach(FILE *fp, struct n_dig_msg_ctx *dmcp, char const *cmda[3]){
   bool_t status;
   struct attachment *ap;
   char const *cp;
   struct header *hp;
   NYD2_IN;

   hp = dmcp->dmc_hp;

   if((cp = cmda[0]) == NULL){
      cp = n_empty; /* xxx not NULL anyway */
      goto jdefault;
   }

   if(is_asccaseprefix(cp, "attribute")){
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
   }else if(is_asccaseprefix(cp, "attribute-at")){
      uiz_t i;

      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;

      if((n_idec_uiz_cp(&i, cmda[1], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0){
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
   }else if(is_asccaseprefix(cp, "attribute-set")){
      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
         goto j505r;

      if((ap = n_attachment_find(hp->h_attach, cmda[1], NULL)) != NULL){
jatt_attset:
         if(ap->a_msgno > 0){
            if(fprintf(fp, "505 RFC822 message attachment: %s\n",
                  cmda[1]) < 0)
               cp = NULL;
         }else{
            char c, *keyw;

            cp = cmda[2];
            while((c = *cp) != '\0' && !blankchar(c))
               ++cp;
            keyw = savestrbuf(cmda[2], PTR2SIZE(cp - cmda[2]));
            if(c != '\0'){
               for(; (c = *++cp) != '\0' && blankchar(c);)
                  ;
               if(c != '\0'){
                  char *xp;

                  /* Strip [\r\n] which would render a parameter invalid XXX
                   * XXX all controls? */
                  cp = xp = savestr(cp);
                  for(; (c = *xp) != '\0'; ++xp)
                     if(c == '\n' || c == '\r')
                        *xp = ' ';
                  c = *cp;
               }
            }

            if(!asccasecmp(keyw, "filename"))
               ap->a_name = (c == '\0') ? ap->a_path_bname : cp;
            else if(!asccasecmp(keyw, "content-description"))
               ap->a_content_description = (c == '\0') ? NULL : cp;
            else if(!asccasecmp(keyw, "content-id")){
               ap->a_content_id = NULL;

               if(c != '\0'){
                  struct name *np;

                  np = checkaddrs(lextract(cp, GREF),
                        /*EACM_STRICT | TODO '/' valid!! */ EACM_NOLOG |
                        EACM_NONAME, NULL);
                  if(np != NULL && np->n_flink == NULL)
                     ap->a_content_id = np;
                  else
                     cp = NULL;
               }
            }else if(!asccasecmp(keyw, "content-type"))
               ap->a_content_type = (c == '\0') ? NULL : cp;
            else if(!asccasecmp(keyw, "content-disposition"))
               ap->a_content_disposition = (c == '\0') ? NULL : cp;
            else
               cp = NULL;

            if(cp != NULL){
               size_t i;

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
   }else if(is_asccaseprefix(cp, "attribute-set-at")){
      uiz_t i;

      if(cmda[1] == NULL || cmda[2] == NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
         goto j505r;

      if((n_idec_uiz_cp(&i, cmda[1], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0){
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
   }else if(is_asccaseprefix(cmda[0], "insert")){
      enum n_attach_error aerr;

      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
         goto j505r;

      hp->h_attach = n_attachment_append(hp->h_attach, cmda[1], &aerr, &ap);
      switch(aerr){
      case n_ATTACH_ERR_FILE_OPEN: cp = "505\n"; goto jatt_ins;
      case n_ATTACH_ERR_ICONV_FAILED: cp = "506\n"; goto jatt_ins;
      case n_ATTACH_ERR_ICONV_NAVAIL:
      case n_ATTACH_ERR_OTHER:
      default:
         cp = "501\n";
jatt_ins:
         if(fprintf(fp, "%s %s\n", cp, cmda[1]) < 0)
            cp = NULL;
         break;
      case n_ATTACH_ERR_NONE:{
         size_t i;

         for(i = 0; ap != NULL; ++i, ap = ap->a_blink)
            ;
         if(fprintf(fp, "210 %" PRIuZ "\n", i) < 0)
            cp = NULL;
         }break;
      }
      goto jleave;
   }else if(is_asccaseprefix(cp, "list")){
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
   }else if(is_asccaseprefix(cmda[0], "remove")){
      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
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
   }else if(is_asccaseprefix(cp, "remove-at")){
      uiz_t i;

      if(cmda[1] == NULL || cmda[2] != NULL)
         goto jecmd;
      if(dmcp->dmc_flags & n_DIG_MSG_RDONLY)
         goto j505r;

      if((n_idec_uiz_cp(&i, cmda[1], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0){
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

FL void
n_dig_msg_on_mailbox_close(struct mailbox *mbp){
   struct n_dig_msg_ctx *dmcp;
   NYD_IN;

   while((dmcp = mbp->mb_digmsg) != NULL){
      mbp->mb_digmsg = dmcp->dmc_next;
      if(dmcp->dmc_flags & n_DIG_MSG_FCLOSE)
         fclose(dmcp->dmc_fp);
      if(dmcp->dmc_flags & n_DIG_MSG_OWN_MEMPOOL){
         n_memory_pool_push(dmcp->dmc_mempool, FAL0);
         n_memory_pool_pop(dmcp->dmc_mempool, TRU1);
      }
      n_free(dmcp);
   }
   NYD_OU;
}

FL bool_t
n_dig_msg_circumflex(struct n_dig_msg_ctx *dmcp, FILE *fp, char const *cmd){
   bool_t rv;
   char const *cp, *cmd_top;
   NYD_IN;

   cp = cmd;
   while(blankchar(*cp))
      ++cp;
   cmd = cp;
   for(cmd_top = cp; *cp != '\0'; cmd_top = cp++)
      if(blankchar(*cp))
         break;

   rv = a_dmsg_cmd(fp, dmcp, cmd, PTR2SIZE(cmd_top - cmd), cp);
   NYD_OU;
   return rv;
}

FL int
c_digmsg(void *vp){
   char const *cp, *emsg;
   struct n_dig_msg_ctx *dmcp;
   struct n_cmd_arg *cap;
   struct n_cmd_arg_ctx *cacp;
   NYD_IN;

   n_pstate_err_no = n_ERR_NONE;
   cacp = vp;
   cap = cacp->cac_arg;

   if(is_asccaseprefix(cp = cap->ca_arg.ca_str.s, "create")){
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
      case n_ERR_INVAL:
         emsg = N_("`digmsg': create: message number invalid: %s\n");
         goto jeinval_quote;
      case n_ERR_EXIST:
         emsg = N_("`digmsg': create: message object already exists: %s\n");
         goto jeinval_quote;
      default:
         break;
      }

      if(dmcp->dmc_flags & n_DIG_MSG_COMPOSE)
         dmcp->dmc_flags = n_DIG_MSG_COMPOSE | n_DIG_MSG_COMPOSE_DIGGED;
      else{
         FILE *fp;

         if((fp = setinput(&mb, dmcp->dmc_mp, NEED_HEADER)) == NULL){
            /* XXX Should have paniced before.. */
            n_free(dmcp);
            emsg = N_("`digmsg': create: mailbox I/O error for message: %s\n");
            goto jeinval_quote;
         }

         n_memory_pool_push(dmcp->dmc_mempool, TRU1);
         /* XXX n_header_extract error!! */
         n_header_extract((n_HEADER_EXTRACT_FULL |
               n_HEADER_EXTRACT_PREFILL_RECEIVERS |
               n_HEADER_EXTRACT_IGNORE_FROM_), fp, dmcp->dmc_hp, NULL);
         n_memory_pool_pop(dmcp->dmc_mempool, FAL0);
      }

      if(cacp->cac_no == 3)
         dmcp->dmc_fp = n_stdout;
      /* For compose mode simply use OF_REGISTER, the number of dangling
       * deleted files with open descriptors until next close_all_files()
       * should be very small; if this paradigm is changed
       * n_DIG_MSG_COMPOSE_GUT() needs to be adjusted */
      else if((dmcp->dmc_fp = Ftmp(NULL, "digmsg", (OF_RDWR | OF_UNLINK |
               (dmcp->dmc_flags & n_DIG_MSG_COMPOSE ? OF_REGISTER : 0)))
            ) != NULL)
         dmcp->dmc_flags |= n_DIG_MSG_HAVE_FP |
               (dmcp->dmc_flags & n_DIG_MSG_COMPOSE ? 0 : n_DIG_MSG_FCLOSE);
      else{
         n_err(_("`digmsg': create: cannot create temporary file: %s\n"),
            n_err_to_doc(n_pstate_err_no = n_err_no));
         vp = NULL;
         goto jeremove;
      }

      if(!(dmcp->dmc_flags & n_DIG_MSG_COMPOSE)){
         dmcp->dmc_last = NULL;
         if((dmcp->dmc_next = mb.mb_digmsg) != NULL)
            dmcp->dmc_next->dmc_last = dmcp;
         mb.mb_digmsg = dmcp;
      }
   }else if(is_asccaseprefix(cp, "remove")){
      if(cacp->cac_no != 2)
         goto jesynopsis;
      cap = cap->ca_next;

      switch(a_dmsg_find(cp = cap->ca_arg.ca_str.s, &dmcp, FAL0)){
      case n_ERR_INVAL:
         emsg = N_("`digmsg': remove: message number invalid: %s\n");
         goto jeinval_quote;
      default:
         if(!(dmcp->dmc_flags & n_DIG_MSG_COMPOSE) ||
               (dmcp->dmc_flags & n_DIG_MSG_COMPOSE_DIGGED))
            break;
         /* FALLTHRU */
      case n_ERR_NOENT:
         emsg = N_("`digmsg': remove: no such message object: %s\n");
         goto jeinval_quote;
      }

      if(!(dmcp->dmc_flags & n_DIG_MSG_COMPOSE)){
         if(dmcp->dmc_last != NULL)
            dmcp->dmc_last->dmc_next = dmcp->dmc_next;
         else{
            assert(dmcp == mb.mb_digmsg);
            mb.mb_digmsg = dmcp->dmc_next;
         }
         if(dmcp->dmc_next != NULL)
            dmcp->dmc_next->dmc_last = dmcp->dmc_last;
      }

      if(dmcp->dmc_flags & n_DIG_MSG_FCLOSE)
         fclose(dmcp->dmc_fp);
jeremove:
      if(dmcp->dmc_flags & n_DIG_MSG_OWN_MEMPOOL){
         n_memory_pool_push(dmcp->dmc_mempool, FAL0);
         n_memory_pool_pop(dmcp->dmc_mempool, TRU1);
      }

      if(dmcp->dmc_flags & n_DIG_MSG_COMPOSE)
         dmcp->dmc_flags = n_DIG_MSG_COMPOSE;
      else
         n_free(dmcp);
   }else{
      switch(a_dmsg_find(cp, &dmcp, FAL0)){
      case n_ERR_INVAL:
         emsg = N_("`digmsg': message number invalid: %s\n");
         goto jeinval_quote;
      case n_ERR_NOENT:
         emsg = N_("`digmsg': no such message object: %s\n");
         goto jeinval_quote;
      default:
         break;
      }
      cap = cap->ca_next;

      if(dmcp->dmc_flags & n_DIG_MSG_HAVE_FP){
         rewind(dmcp->dmc_fp);
         ftruncate(fileno(dmcp->dmc_fp), 0);
      }

      n_memory_pool_push(dmcp->dmc_mempool, FAL0);
      /* C99 */{
         struct str cmds_b, *cmdsp;

         cp = n_empty;
         if(cap == NULL){ /* XXX cmd_arg_parse is stupid */
            cmdsp = &cmds_b;
            cmdsp->s = n_UNCONST(cp);
            cmdsp->l = 0;
         }else{
            cmdsp = &cap->ca_arg.ca_str;
            if((cap = cap->ca_next) != NULL)
               cp = cap->ca_arg.ca_str.s;
         }
         if(!a_dmsg_cmd(dmcp->dmc_fp, dmcp, cmdsp->s, cmdsp->l, cp))
            vp = NULL;
      }
      n_memory_pool_pop(dmcp->dmc_mempool, FAL0);

      if(dmcp->dmc_flags & n_DIG_MSG_HAVE_FP){
         rewind(dmcp->dmc_fp);
         n_digmsg_read_overlay = dmcp;
      }
   }

jleave:
   NYD_OU;
   return (vp == NULL);

jesynopsis:
   n_err(_("Synopsis: digmsg: <command> <-|msgno> [<:argument:>]\n"));
   goto jeinval;
jeinval_quote:
   n_err(V_(emsg), n_shexp_quote_cp(cp, FAL0));
jeinval:
   n_pstate_err_no = n_ERR_INVAL;
   vp = NULL;
   goto jleave;
}

/* s-it-mode */
