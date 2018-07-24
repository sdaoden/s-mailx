/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Dig message objects. TODO Very very restricted (especially non-compose)
 *@ On protocol change adjust config.h:n_DIG_MSG_PLUMBING_VERSION + `~^' manual
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
#undef n_FILE
#define n_FILE dig_msg

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* ~^ mode */
static bool_t a_dmsg_plumbing(struct n_dig_msg_ctx *dmcp, char const *cmd);

static bool_t a_dmsg__plumb_header(struct n_dig_msg_ctx *dmcp,
               char const *cmdarr[4]);
static bool_t a_dmsg__plumb_attach(struct n_dig_msg_ctx *dmcp,
               char const *cmdarr[4]);

static bool_t
a_dmsg_plumbing(struct n_dig_msg_ctx *dmcp, char const *cmd){
   /* TODO _dmsg_plumbing: instead of fields the basic headers should
    * TODO be in an array and have IDs, like in termcap etc., so then this
    * TODO could be simplified as table-walks.  Also true for arg-checks! */
   bool_t rv;
   char const *cp, *cmdarr[4];
   NYD2_ENTER;

   cp = cmd;

   /* C99 */{
      size_t i;

      /* TODO trim+strlist_split(_ifs?)() */
      for(i = 0; i < n_NELEM(cmdarr); ++i){
         while(blankchar(*cp))
            ++cp;
         if(*cp == '\0')
            cmdarr[i] = NULL;
         else{
            if(i < n_NELEM(cmdarr) - 1)
               for(cmdarr[i] = cp++; *cp != '\0' && !blankchar(*cp); ++cp)
                  ;
            else{
               /* Last slot takes all the rest of the line, less trailing WS */
               for(cmdarr[i] = cp++; *cp != '\0'; ++cp)
                  ;
               while(blankchar(cp[-1]))
                  --cp;
            }
            cmdarr[i] = savestrbuf(cmdarr[i], PTR2SIZE(cp - cmdarr[i]));
         }
      }
   }

   if(n_UNLIKELY(cmdarr[0] == NULL))
      goto jecmd;
   if(is_asccaseprefix(cmdarr[0], "header"))
      rv = a_dmsg__plumb_header(dmcp, cmdarr);
   else if(is_asccaseprefix(cmdarr[0], "attachment"))
      rv = a_dmsg__plumb_attach(dmcp, cmdarr);
   else{
jecmd:
      fputs("500\n", dmcp->dmc_fp);
      rv = FAL0;
   }
   fflush(dmcp->dmc_fp);

   NYD2_LEAVE;
   return rv;
}

static bool_t
a_dmsg__plumb_header(struct n_dig_msg_ctx *dmcp, char const *cmdarr[4]){
   uiz_t i;
   struct n_header_field *hfp;
   struct name *np, **npp;
   char const *cp;
   struct header *hp;
   NYD2_ENTER;

   hp = dmcp->dmc_hp;

   if((cp = cmdarr[1]) == NULL){
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

      if(cmdarr[2] == NULL || cmdarr[3] == NULL)
         goto jecmd;

      /* Strip [\r\n] which would render a body invalid XXX all controls? */
      /* C99 */{
         char *xp, c;

         cmdarr[3] = xp = savestr(cmdarr[3]);
         for(; (c = *xp) != '\0'; ++xp)
            if(c == '\n' || c == '\r')
               *xp = ' ';
      }

      if(!asccasecmp(cmdarr[2], cp = "Subject")){
         if(cmdarr[3][0] != '\0'){
            if(hp->h_subject != NULL)
               hp->h_subject = savecatsep(hp->h_subject, ' ', cmdarr[3]);
            else
               hp->h_subject = n_UNCONST(cmdarr[3]);
            fprintf(dmcp->dmc_fp, "210 %s 1\n", cp);
            goto jleave;
         }else
            goto j501cp;
      }

      mult_ok = TRU1;
      ntype = GEXTRA | GFULL | GFULLEXTRA;
      eacm = EACM_STRICT;

      if(!asccasecmp(cmdarr[2], cp = "From")){
         npp = &hp->h_from;
jins:
         aerr = 0;
         /* todo As said above, this should be table driven etc., but.. */
         if(ntype & GBCC_IS_FCC){
            np = nalloc_fcc(cmdarr[3]);
            if(is_addr_invalid(np, eacm))
               goto jins_505;
         }else{
            if((np = lextract(cmdarr[3], ntype)) == NULL)
               goto j501cp;

            if((np = checkaddrs(np, eacm, &aerr), aerr != 0)){
jins_505:
               fprintf(dmcp->dmc_fp, "505 %s\n", cp);
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

         if(!mult_ok && (i != 0 || np->n_flink != NULL))
            fprintf(dmcp->dmc_fp, "506 %s\n", cp);
         else{
            if(xnp == NULL)
               *npp = np;
            else
               xnp->n_flink = np;
            np->n_blink = xnp;
            fprintf(dmcp->dmc_fp, "210 %s %" PRIuZ "\n", cp, ++i);
         }
         goto jleave;
      }
      if(!asccasecmp(cmdarr[2], cp = "Sender")){
         mult_ok = FAL0;
         npp = &hp->h_sender;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "To")){
         npp = &hp->h_to;
         ntype = GTO | GFULL;
         eacm = EACM_NORMAL | EAF_NAME;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "Cc")){
         npp = &hp->h_cc;
         ntype = GCC | GFULL;
         eacm = EACM_NORMAL | EAF_NAME;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "Bcc")){
         npp = &hp->h_bcc;
         ntype = GBCC | GFULL;
         eacm = EACM_NORMAL | EAF_NAME;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "Fcc")){
         npp = &hp->h_fcc;
         ntype = GBCC | GBCC_IS_FCC;
         eacm = EACM_NORMAL /* Not | EAF_FILE, depend on *expandaddr*! */;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "Reply-To")){
         npp = &hp->h_reply_to;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mail-Followup-To")){
         npp = &hp->h_mft;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "Message-ID")){
         mult_ok = FAL0;
         npp = &hp->h_message_id;
         ntype = GREF;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "References")){
         npp = &hp->h_ref;
         ntype = GREF;
         eacm = EACM_NONAME;
         goto jins;
      }
      if(!asccasecmp(cmdarr[2], cp = "In-Reply-To")){
         npp = &hp->h_in_reply_to;
         ntype = GREF;
         eacm = EACM_NONAME;
         goto jins;
      }

      if((cp = n_header_is_known(cmdarr[2], UIZ_MAX)) != NULL){
         fprintf(dmcp->dmc_fp, "505 %s\n", cp);
         goto jleave;
      }

      /* Free-form header fields */
      /* C99 */{
         size_t nl, bl;
         struct n_header_field **hfpp;

         for(cp = cmdarr[2]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmdarr[2];
               goto j501cp;
            }

         for(i = 0, hfpp = &hp->h_user_headers; *hfpp != NULL; ++i)
            hfpp = &(*hfpp)->hf_next;

         nl = strlen(cp = cmdarr[2]) +1;
         bl = strlen(cmdarr[3]) +1;
         *hfpp = hfp = n_autorec_alloc(n_VSTRUCT_SIZEOF(struct n_header_field,
               hf_dat) + nl + bl);
         hfp->hf_next = NULL;
         hfp->hf_nl = nl - 1;
         hfp->hf_bl = bl - 1;
         memcpy(&hfp->hf_dat[0], cp, nl);
         memcpy(&hfp->hf_dat[nl], cmdarr[3], bl);
         fprintf(dmcp->dmc_fp, "210 %s %" PRIuZ "\n", &hfp->hf_dat[0], ++i);
      }
   }else if(is_asccaseprefix(cp, "list")){
jdefault:
      if(cmdarr[2] == NULL){
         fputs("210", dmcp->dmc_fp);
         if(hp->h_subject != NULL) fputs(" Subject", dmcp->dmc_fp);
         if(hp->h_from != NULL) fputs(" From", dmcp->dmc_fp);
         if(hp->h_sender != NULL) fputs(" Sender", dmcp->dmc_fp);
         if(hp->h_to != NULL) fputs(" To", dmcp->dmc_fp);
         if(hp->h_cc != NULL) fputs(" Cc", dmcp->dmc_fp);
         if(hp->h_bcc != NULL) fputs(" Bcc", dmcp->dmc_fp);
         if(hp->h_fcc != NULL) fputs(" Fcc", dmcp->dmc_fp);
         if(hp->h_reply_to != NULL) fputs(" Reply-To", dmcp->dmc_fp);
         if(hp->h_mft != NULL) fputs(" Mail-Followup-To", dmcp->dmc_fp);
         if(hp->h_message_id != NULL) fputs(" Message-ID", dmcp->dmc_fp);
         if(hp->h_ref != NULL) fputs(" References", dmcp->dmc_fp);
         if(hp->h_in_reply_to != NULL) fputs(" In-Reply-To", dmcp->dmc_fp);
         if(hp->h_mailx_command != NULL)
            fputs(" Mailx-Command", dmcp->dmc_fp);
         if(hp->h_mailx_raw_to != NULL) fputs(" Mailx-Raw-To", dmcp->dmc_fp);
         if(hp->h_mailx_raw_cc != NULL) fputs(" Mailx-Raw-Cc", dmcp->dmc_fp);
         if(hp->h_mailx_raw_bcc != NULL)
            fputs(" Mailx-Raw-Bcc", dmcp->dmc_fp);
         if(hp->h_mailx_orig_from != NULL)
            fputs(" Mailx-Orig-From", dmcp->dmc_fp);
         if(hp->h_mailx_orig_to != NULL)
            fputs(" Mailx-Orig-To", dmcp->dmc_fp);
         if(hp->h_mailx_orig_cc != NULL)
            fputs(" Mailx-Orig-Cc", dmcp->dmc_fp);
         if(hp->h_mailx_orig_bcc != NULL)
            fputs(" Mailx-Orig-Bcc", dmcp->dmc_fp);

         /* Print only one instance of each free-form header */
         for(hfp = hp->h_user_headers; hfp != NULL; hfp = hfp->hf_next){
            struct n_header_field *hfpx;

            for(hfpx = hp->h_user_headers;; hfpx = hfpx->hf_next)
               if(hfpx == hfp){
                  putc(' ', dmcp->dmc_fp);
                  fputs(&hfp->hf_dat[0], dmcp->dmc_fp);
                  break;
               }else if(!asccasecmp(&hfpx->hf_dat[0], &hfp->hf_dat[0]))
                  break;
         }
         putc('\n', dmcp->dmc_fp);
         goto jleave;
      }

      if(cmdarr[3] != NULL)
         goto jecmd;

      if(!asccasecmp(cmdarr[2], cp = "Subject")){
         np = (hp->h_subject != NULL) ? (struct name*)-1 : NULL;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "From")){
         np = hp->h_from;
jlist:
         fprintf(dmcp->dmc_fp, "%s %s\n", (np == NULL ? "501" : "210"), cp);
         goto jleave;
      }
      if(!asccasecmp(cmdarr[2], cp = "Sender")){
         np = hp->h_sender;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "To")){
         np = hp->h_to;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Cc")){
         np = hp->h_cc;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Bcc")){
         np = hp->h_bcc;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Fcc")){
         np = hp->h_fcc;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Reply-To")){
         np = hp->h_reply_to;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mail-Followup-To")){
         np = hp->h_mft;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Message-ID")){
         np = hp->h_message_id;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "References")){
         np = hp->h_ref;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "In-Reply-To")){
         np = hp->h_in_reply_to;
         goto jlist;
      }

      if(!asccasecmp(cmdarr[2], cp = "Mailx-Command")){
         np = (hp->h_mailx_command != NULL) ? (struct name*)-1 : NULL;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Raw-To")){
         np = hp->h_mailx_raw_to;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Raw-Cc")){
         np = hp->h_mailx_raw_cc;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Raw-Bcc")){
         np = hp->h_mailx_raw_bcc;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-From")){
         np = hp->h_mailx_orig_from;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-To")){
         np = hp->h_mailx_orig_to;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-Cc")){
         np = hp->h_mailx_orig_cc;
         goto jlist;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-Bcc")){
         np = hp->h_mailx_orig_bcc;
         goto jlist;
      }

      /* Free-form header fields */
      for(cp = cmdarr[2]; *cp != '\0'; ++cp)
         if(!fieldnamechar(*cp)){
            cp = cmdarr[2];
            goto j501cp;
         }
      cp = cmdarr[2];
      for(hfp = hp->h_user_headers;; hfp = hfp->hf_next){
         if(hfp == NULL)
            goto j501cp;
         else if(!asccasecmp(cp, &hfp->hf_dat[0])){
            fprintf(dmcp->dmc_fp, "210 %s\n", &hfp->hf_dat[0]);
            break;
         }
      }
   }else if(is_asccaseprefix(cp, "remove")){
      if(cmdarr[2] == NULL || cmdarr[3] != NULL)
         goto jecmd;

      if(!asccasecmp(cmdarr[2], cp = "Subject")){
         if(hp->h_subject != NULL){
            hp->h_subject = NULL;
            fprintf(dmcp->dmc_fp, "210 %s\n", cp);
            goto jleave;
         }else
            goto j501cp;
      }

      if(!asccasecmp(cmdarr[2], cp = "From")){
         npp = &hp->h_from;
jrem:
         if(*npp != NULL){
            *npp = NULL;
            fprintf(dmcp->dmc_fp, "210 %s\n", cp);
            goto jleave;
         }else
            goto j501cp;
      }
      if(!asccasecmp(cmdarr[2], cp = "Sender")){
         npp = &hp->h_sender;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "To")){
         npp = &hp->h_to;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "Cc")){
         npp = &hp->h_cc;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "Bcc")){
         npp = &hp->h_bcc;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "Fcc")){
         npp = &hp->h_fcc;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "Reply-To")){
         npp = &hp->h_reply_to;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mail-Followup-To")){
         npp = &hp->h_mft;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "Message-ID")){
         npp = &hp->h_message_id;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "References")){
         npp = &hp->h_ref;
         goto jrem;
      }
      if(!asccasecmp(cmdarr[2], cp = "In-Reply-To")){
         npp = &hp->h_in_reply_to;
         goto jrem;
      }

      if((cp = n_header_is_known(cmdarr[2], UIZ_MAX)) != NULL){
         fprintf(dmcp->dmc_fp, "505 %s\n", cp);
         goto jleave;
      }

      /* Free-form header fields (note j501cp may print non-normalized name) */
      /* C99 */{
         struct n_header_field **hfpp;
         bool_t any;

         for(cp = cmdarr[2]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmdarr[2];
               goto j501cp;
            }
         cp = cmdarr[2];

         for(any = FAL0, hfpp = &hp->h_user_headers; (hfp = *hfpp) != NULL;){
            if(!asccasecmp(cp, &hfp->hf_dat[0])){
               *hfpp = hfp->hf_next;
               if(!any)
                  fprintf(dmcp->dmc_fp, "210 %s\n", &hfp->hf_dat[0]);
               any = TRU1;
            }else
               hfpp = &hfp->hf_next;
         }
         if(!any)
            goto j501cp;
      }
   }else if(is_asccaseprefix(cp, "remove-at")){
      if(cmdarr[2] == NULL || cmdarr[3] == NULL)
         goto jecmd;

      if((n_idec_uiz_cp(&i, cmdarr[3], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0){
         fputs("505\n", dmcp->dmc_fp);
         goto jleave;
      }

      if(!asccasecmp(cmdarr[2], cp = "Subject")){
         if(hp->h_subject != NULL && i == 1){
            hp->h_subject = NULL;
            fprintf(dmcp->dmc_fp, "210 %s 1\n", cp);
            goto jleave;
         }else
            goto j501cp;
      }

      if(!asccasecmp(cmdarr[2], cp = "From")){
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

         fprintf(dmcp->dmc_fp, "210 %s\n", cp);
         goto jleave;
      }
      if(!asccasecmp(cmdarr[2], cp = "Sender")){
         npp = &hp->h_sender;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "To")){
         npp = &hp->h_to;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "Cc")){
         npp = &hp->h_cc;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "Bcc")){
         npp = &hp->h_bcc;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "Fcc")){
         npp = &hp->h_fcc;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "Reply-To")){
         npp = &hp->h_reply_to;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mail-Followup-To")){
         npp = &hp->h_mft;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "Message-ID")){
         npp = &hp->h_message_id;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "References")){
         npp = &hp->h_ref;
         goto jremat;
      }
      if(!asccasecmp(cmdarr[2], cp = "In-Reply-To")){
         npp = &hp->h_in_reply_to;
         goto jremat;
      }

      if((cp = n_header_is_known(cmdarr[2], UIZ_MAX)) != NULL){
         fprintf(dmcp->dmc_fp, "505 %s\n", cp);
         goto jleave;
      }

      /* Free-form header fields */
      /* C99 */{
         struct n_header_field **hfpp;

         for(cp = cmdarr[2]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmdarr[2];
               goto j501cp;
            }
         cp = cmdarr[2];

         for(hfpp = &hp->h_user_headers; (hfp = *hfpp) != NULL;){
            if(--i == 0){
               *hfpp = hfp->hf_next;
               fprintf(dmcp->dmc_fp,
                  "210 %s %" PRIuZ "\n", &hfp->hf_dat[0], i);
               break;
            }else
               hfpp = &hfp->hf_next;
         }
         if(hfp == NULL)
            goto j501cp;
      }
   }else if(is_asccaseprefix(cp, "show")){
      if(cmdarr[2] == NULL || cmdarr[3] != NULL)
         goto jecmd;

      if(!asccasecmp(cmdarr[2], cp = "Subject")){
         if(hp->h_subject == NULL)
            goto j501cp;
         fprintf(dmcp->dmc_fp, "212 %s\n%s\n\n", cp, hp->h_subject);
         goto jleave;
      }

      if(!asccasecmp(cmdarr[2], cp = "From")){
         np = hp->h_from;
jshow:
         if(np != NULL){
            fprintf(dmcp->dmc_fp, "211 %s\n", cp);
            do if(!(np->n_type & GDEL)){
               switch(np->n_flags & NAME_ADDRSPEC_ISMASK){
               case NAME_ADDRSPEC_ISFILE: cp = n_hy; break;
               case NAME_ADDRSPEC_ISPIPE: cp = "|"; break;
               case NAME_ADDRSPEC_ISNAME: cp = n_ns; break;
               default: cp = np->n_name; break;
               }
               fprintf(dmcp->dmc_fp, "%s %s\n", cp, np->n_fullname);
            }while((np = np->n_flink) != NULL);
            putc('\n', dmcp->dmc_fp);
            goto jleave;
         }else
            goto j501cp;
      }
      if(!asccasecmp(cmdarr[2], cp = "Sender")){
         np = hp->h_sender;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "To")){
         np = hp->h_to;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Cc")){
         np = hp->h_cc;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Bcc")){
         np = hp->h_bcc;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Fcc")){
         np = hp->h_fcc;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Reply-To")){
         np = hp->h_reply_to;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mail-Followup-To")){
         np = hp->h_mft;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Message-ID")){
         np = hp->h_message_id;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "References")){
         np = hp->h_ref;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "In-Reply-To")){
         np = hp->h_in_reply_to;
         goto jshow;
      }

      if(!asccasecmp(cmdarr[2], cp = "Mailx-Command")){
         if(hp->h_mailx_command == NULL)
            goto j501cp;
         fprintf(dmcp->dmc_fp, "212 %s\n%s\n\n", cp, hp->h_mailx_command);
         goto jleave;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Raw-To")){
         np = hp->h_mailx_raw_to;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Raw-Cc")){
         np = hp->h_mailx_raw_cc;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Raw-Bcc")){
         np = hp->h_mailx_raw_bcc;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-From")){
         np = hp->h_mailx_orig_from;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-To")){
         np = hp->h_mailx_orig_to;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-Cc")){
         np = hp->h_mailx_orig_cc;
         goto jshow;
      }
      if(!asccasecmp(cmdarr[2], cp = "Mailx-Orig-Bcc")){
         np = hp->h_mailx_orig_bcc;
         goto jshow;
      }

      /* Free-form header fields */
      /* C99 */{
         bool_t any;

         for(cp = cmdarr[2]; *cp != '\0'; ++cp)
            if(!fieldnamechar(*cp)){
               cp = cmdarr[2];
               goto j501cp;
            }
         cp = cmdarr[2];

         for(any = FAL0, hfp = hp->h_user_headers; hfp != NULL;
               hfp = hfp->hf_next){
            if(!asccasecmp(cp, &hfp->hf_dat[0])){
               if(!any)
                  fprintf(dmcp->dmc_fp, "212 %s\n", &hfp->hf_dat[0]);
               any = TRU1;
               fprintf(dmcp->dmc_fp, "%s\n", &hfp->hf_dat[hfp->hf_nl +1]);
            }
         }
         if(any)
            putc('\n', dmcp->dmc_fp);
         else
            goto j501cp;
      }
   }else
      goto jecmd;

jleave:
   NYD2_LEAVE;
   return (cp != NULL);

jecmd:
   fputs("500\n", dmcp->dmc_fp);
   cp = NULL;
   goto jleave;
j501cp:
   fputs("501 ", dmcp->dmc_fp);
   fputs(cp, dmcp->dmc_fp);
   putc('\n', dmcp->dmc_fp);
   goto jleave;
}

static bool_t
a_dmsg__plumb_attach(struct n_dig_msg_ctx *dmcp, char const *cmdarr[4]){
   bool_t status;
   struct attachment *ap;
   char const *cp;
   struct header *hp;
   NYD2_ENTER;

   hp = dmcp->dmc_hp;

   if((cp = cmdarr[1]) == NULL){
      cp = n_empty; /* xxx not NULL anyway */
      goto jdefault;
   }

   if(is_asccaseprefix(cp, "attribute")){
      if(cmdarr[2] == NULL || cmdarr[3] != NULL)
         goto jecmd;

      if((ap = n_attachment_find(hp->h_attach, cmdarr[2], NULL)) != NULL){
jatt_att:
         fprintf(dmcp->dmc_fp, "212 %s\n", cmdarr[2]);
         if(ap->a_msgno > 0)
            fprintf(dmcp->dmc_fp, "message-number %d\n\n", ap->a_msgno);
         else{
            fprintf(dmcp->dmc_fp,
               "creation-name %s\nopen-path %s\nfilename %s\n",
               ap->a_path_user, ap->a_path, ap->a_name);
            if(ap->a_content_description != NULL)
               fprintf(dmcp->dmc_fp, "content-description %s\n",
                  ap->a_content_description);
            if(ap->a_content_id != NULL)
               fprintf(dmcp->dmc_fp, "content-id %s\n",
                  ap->a_content_id->n_name);
            if(ap->a_content_type != NULL)
               fprintf(dmcp->dmc_fp, "content-type %s\n", ap->a_content_type);
            if(ap->a_content_disposition != NULL)
               fprintf(dmcp->dmc_fp, "content-disposition %s\n",
                  ap->a_content_disposition);
            putc('\n', dmcp->dmc_fp);
         }
      }else
         fputs("501\n", dmcp->dmc_fp);
   }else if(is_asccaseprefix(cp, "attribute-at")){
      uiz_t i;

      if(cmdarr[2] == NULL || cmdarr[3] != NULL)
         goto jecmd;

      if((n_idec_uiz_cp(&i, cmdarr[2], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0)
         fputs("505\n", dmcp->dmc_fp);
      else{
         for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
            ;
         if(ap != NULL)
            goto jatt_att;
         else
            fputs("501\n", dmcp->dmc_fp);
      }
   }else if(is_asccaseprefix(cp, "attribute-set")){
      if(cmdarr[2] == NULL || cmdarr[3] == NULL)
         goto jecmd;

      if((ap = n_attachment_find(hp->h_attach, cmdarr[2], NULL)) != NULL){
jatt_attset:
         if(ap->a_msgno > 0)
            fputs("505\n", dmcp->dmc_fp);
         else{
            char c, *keyw;

            cp = cmdarr[3];
            while((c = *cp) != '\0' && !blankchar(c))
               ++cp;
            keyw = savestrbuf(cmdarr[3], PTR2SIZE(cp - cmdarr[3]));
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
               fprintf(dmcp->dmc_fp, "210 %" PRIuZ "\n", i);
            }else
               fputs("505\n", dmcp->dmc_fp);
         }
      }else
         fputs("501\n", dmcp->dmc_fp);
   }else if(is_asccaseprefix(cp, "attribute-set-at")){
      uiz_t i;

      if(cmdarr[2] == NULL || cmdarr[3] == NULL)
         goto jecmd;

      if((n_idec_uiz_cp(&i, cmdarr[2], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0)
         fputs("505\n", dmcp->dmc_fp);
      else{
         for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
            ;
         if(ap != NULL)
            goto jatt_attset;
         else
            fputs("501\n", dmcp->dmc_fp);
      }
   }else if(is_asccaseprefix(cmdarr[1], "insert")){
      enum n_attach_error aerr;

      if(cmdarr[2] == NULL || cmdarr[3] != NULL)
         goto jecmd;

      hp->h_attach = n_attachment_append(hp->h_attach, cmdarr[2], &aerr, &ap);
      switch(aerr){
      case n_ATTACH_ERR_FILE_OPEN: cp = "505\n"; goto jatt_ins;
      case n_ATTACH_ERR_ICONV_FAILED: cp = "506\n"; goto jatt_ins;
      case n_ATTACH_ERR_ICONV_NAVAIL:
      case n_ATTACH_ERR_OTHER:
      default:
         cp = "501\n";
jatt_ins:
         fputs(cp, dmcp->dmc_fp);
         break;
      case n_ATTACH_ERR_NONE:{
         size_t i;

         for(i = 0; ap != NULL; ++i, ap = ap->a_blink)
            ;
         fprintf(dmcp->dmc_fp, "210 %" PRIuZ "\n", i);
         }break;
      }
      goto jleave;
   }else if(is_asccaseprefix(cp, "list")){
jdefault:
      if(cmdarr[2] != NULL)
         goto jecmd;

      if((ap = hp->h_attach) != NULL){
         fputs("212\n", dmcp->dmc_fp);
         do
            fprintf(dmcp->dmc_fp, "%s\n", ap->a_path_user);
         while((ap = ap->a_flink) != NULL);
         putc('\n', dmcp->dmc_fp);
      }else
         fputs("501\n", dmcp->dmc_fp);
   }else if(is_asccaseprefix(cmdarr[1], "remove")){
      if(cmdarr[2] == NULL || cmdarr[3] != NULL)
         goto jecmd;

      if((ap = n_attachment_find(hp->h_attach, cmdarr[2], &status)) != NULL){
         if(status == TRUM1)
            fputs("506\n", dmcp->dmc_fp);
         else{
            hp->h_attach = n_attachment_remove(hp->h_attach, ap);
            fprintf(dmcp->dmc_fp, "210 %s\n", cmdarr[2]);
         }
      }else
         fputs("501\n", dmcp->dmc_fp);
   }else if(is_asccaseprefix(cp, "remove-at")){
      uiz_t i;

      if(cmdarr[2] == NULL || cmdarr[3] != NULL)
         goto jecmd;

      if((n_idec_uiz_cp(&i, cmdarr[2], 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || i == 0)
         fputs("505\n", dmcp->dmc_fp);
      else{
         for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
            ;
         if(ap != NULL){
            hp->h_attach = n_attachment_remove(hp->h_attach, ap);
            fprintf(dmcp->dmc_fp, "210 %s\n", cmdarr[2]);
         }else
            fputs("501\n", dmcp->dmc_fp);
      }
   }else
      goto jecmd;

jleave:
   NYD2_LEAVE;
   return (cp != NULL);
jecmd:
   fputs("500\n", dmcp->dmc_fp);
   cp = NULL;
   goto jleave;
}

FL bool_t
n_dig_msg_command(struct n_dig_msg_ctx *dmcp, char const *cmd){
   bool_t rv;
   NYD_ENTER;

   rv = a_dmsg_plumbing(dmcp, cmd);
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
