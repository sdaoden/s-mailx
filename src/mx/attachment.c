/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of attachments.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
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
#undef n_FILE
#define n_FILE attachment

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/* We use calloc() for struct attachment */
n_CTAV(AC_DEFAULT == 0);

/* Return >=0 if file denotes a valid message number */
static int a_attachment_is_msg(char const *file);

/* Fill in some basic attachment fields */
static struct attachment *a_attachment_setup_base(struct attachment *ap,
                           char const *file);

/* Setup ap to point to a message */
static struct attachment *a_attachment_setup_msg(struct attachment *ap,
                           char const *msgcp, int msgno);

/* Try to create temporary charset converted version */
#ifdef HAVE_ICONV
static bool_t a_attachment_iconv(struct attachment *ap, FILE *ifp);
#endif

/* */
static void a_attachment_yay(struct attachment const *ap);

static int
a_attachment_is_msg(char const *file){
   int rv;
   NYD2_IN;

   rv = -1;

   if(file[0] == '#'){
      uiz_t ib;

      /* TODO Message numbers should be size_t, and 0 may be a valid one */
      if(file[2] == '\0' && file[1] == '.'){
         if(dot != NULL)
            rv = (int)PTR2SIZE(dot - message + 1);
      }else if((n_idec_uiz_cp(&ib, &file[1], 10, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED || ib == 0 || UICMP(z, ib, >, msgCount))
         rv = -1;
      else
         rv = (int)ib;
   }
   NYD2_OU;
   return rv;
}

static struct attachment *
a_attachment_setup_base(struct attachment *ap, char const *file){
   NYD2_IN;
   ap->a_input_charset = ap->a_charset = NULL;
   ap->a_path_user = ap->a_path = ap->a_path_bname = ap->a_name = file;
   if((file = strrchr(file, '/')) != NULL)
      ap->a_path_bname = ap->a_name = ++file;
   else
      file = ap->a_name;
   ap->a_content_type = n_mimetype_classify_filename(file);
   ap->a_content_disposition = "attachment";
   ap->a_content_description = NULL;
   ap->a_content_id = NULL;
   NYD2_OU;
   return ap;
}

static struct attachment *
a_attachment_setup_msg(struct attachment *ap, char const *msgcp, int msgno){
   NYD2_IN;
   ap->a_path_user = ap->a_path = ap->a_path_bname = ap->a_name = msgcp;
   ap->a_msgno = msgno;
   ap->a_content_type =
   ap->a_content_description =
   ap->a_content_disposition = NULL;
   ap->a_content_id = NULL;
   NYD2_OU;
   return ap;
}

#ifdef HAVE_ICONV
static bool_t
a_attachment_iconv(struct attachment *ap, FILE *ifp){
   struct str oul = {NULL, 0}, inl = {NULL, 0};
   size_t cnt, lbsize;
   iconv_t icp;
   FILE *ofp;
   NYD_IN;

   hold_sigs(); /* TODO until we have signal manager (see TODO) */

   ofp = NULL;

   icp = n_iconv_open(ap->a_charset, ap->a_input_charset);
   if(icp == (iconv_t)-1){
      if(n_err_no == n_ERR_INVAL)
         goto jeconv;
      else
         n_perr(_("iconv_open"), 0);
      goto jerr;
   }

   cnt = (size_t)fsize(ifp);

   if((ofp = Ftmp(NULL, "atticonv", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==NULL){
      n_perr(_("Temporary attachment data file"), 0);
      goto jerr;
   }

   for(lbsize = 0;;){
      if(fgetline(&inl.s, &lbsize, &cnt, &inl.l, ifp, 0) == NULL){
         if(!cnt)
            break;
         n_perr(_("I/O read error occurred"), 0);
         goto jerr;
      }

      if(n_iconv_str(icp, n_ICONV_IGN_NOREVERSE, &oul, &inl, NULL) != 0)
         goto jeconv;
      if((inl.l = fwrite(oul.s, sizeof *oul.s, oul.l, ofp)) != oul.l){
         n_perr(_("I/O write error occurred"), 0);
         goto jerr;
      }
   }
   fflush_rewind(ofp);

   ap->a_tmpf = ofp;
jleave:
   if(inl.s != NULL)
      n_free(inl.s);
   if(oul.s != NULL)
      n_free(oul.s);
   if(icp != (iconv_t)-1)
      n_iconv_close(icp);
   Fclose(ifp);

   rele_sigs(); /* TODO until we have signal manager (see TODO) */
   NYD_OU;
   return (ofp != NULL);

jeconv:
   n_err(_("Cannot convert from %s to %s\n"),
      ap->a_input_charset, ap->a_charset);
jerr:
   if(ofp != NULL)
      Fclose(ofp);
   ofp = NULL;
   goto jleave;
}
#endif /* HAVE_ICONV */

static void
a_attachment_yay(struct attachment const *ap){
   NYD2_IN;
   if(ap->a_msgno > 0)
      fprintf(n_stdout, _("Added message/rfc822 attachment for message #%u\n"),
         ap->a_msgno);
   else
      fprintf(n_stdout, _("Added attachment %s (%s)\n"),
         n_shexp_quote_cp(ap->a_name, FAL0),
         n_shexp_quote_cp(ap->a_path_user, FAL0));
   NYD2_OU;
}

FL struct attachment *
n_attachment_append(struct attachment *aplist, char const *file,
      enum n_attach_error *aerr_or_null, struct attachment **newap_or_null){
#ifdef HAVE_ICONV
   FILE *cnvfp;
#endif
   int msgno;
   char const *file_user, *incs, *oucs;
   struct attachment *nap, *ap;
   enum n_attach_error aerr;
   NYD_IN;

#ifdef HAVE_ICONV
   cnvfp = NULL;
#endif
   aerr = n_ATTACH_ERR_NONE;
   nap = NULL;
   incs = oucs = NULL;

   if(*file == '\0'){
      aerr = n_ATTACH_ERR_OTHER;
      goto jleave;
   }
   file_user = savestr(file); /* TODO recreate after fexpand()!?! */

   if((msgno = a_attachment_is_msg(file)) < 0){
      int e;
      char const *cp, *ncp;

jrefexp:
      if((file = fexpand(file, FEXP_LOCAL | FEXP_NVAR)) == NULL){
         aerr = n_ATTACH_ERR_OTHER;
         goto jleave;
      }

#ifndef HAVE_ICONV
      if(oucs != NULL && oucs != (char*)-1){
         n_err(_("No iconv support, cannot do %s\n"),
            n_shexp_quote_cp(file_user, FAL0));
         aerr = n_ATTACH_ERR_ICONV_NAVAIL;
         goto jleave;
      }
#endif

      if((
#ifdef HAVE_ICONV
            (oucs != NULL && oucs != (char*)-1)
               ? (cnvfp = Fopen(file, "r")) == NULL :
#endif
               access(file, R_OK) != 0)){
         e = n_err_no;

         /* It may not have worked because of a character-set specification,
          * so try to extract that and retry once */
         if(incs == NULL && (cp = strrchr(file, '=')) != NULL){
            size_t i;
            char *nfp, c;

            nfp = savestrbuf(file, PTR2SIZE(cp - file));

            for(ncp = ++cp; (c = *cp) != '\0'; ++cp)
               if(!alnumchar(c) && !punctchar(c))
                  break;
               else if(c == '#'){
                  if(incs == NULL){
                     i = PTR2SIZE(cp - ncp);
                     if(i == 0 || (i == 1 && ncp[0] == '-'))
                        incs = (char*)-1;
                     else if((incs = n_iconv_normalize_name(savestrbuf(ncp, i))
                           ) == NULL){
                        e = n_ERR_INVAL;
                        goto jerr_fopen;
                     }
                     ncp = &cp[1];
                  }else
                     break;
               }
            if(c == '\0'){
               char *xp;

               i = PTR2SIZE(cp - ncp);
               if(i == 0 || (i == 1 && ncp[0] == '-'))
                  xp = (char*)-1;
               else if((xp = n_iconv_normalize_name(savestrbuf(ncp, i))
                     ) == NULL){
                  e = n_ERR_INVAL;
                  goto jerr_fopen;
               }
               if(incs == NULL)
                  incs = xp;
               else
                  oucs = xp;
               file = nfp;
               goto jrefexp;
            }
         }

jerr_fopen:
         n_err(_("Failed to access attachment %s: %s\n"),
            n_shexp_quote_cp(file, FAL0), n_err_to_doc(e));
         aerr = n_ATTACH_ERR_FILE_OPEN;
         goto jleave;
      }
   }

   nap = a_attachment_setup_base(n_autorec_calloc(1, sizeof *nap), file);
   nap->a_path_user = file_user;
   if(msgno >= 0)
      nap = a_attachment_setup_msg(nap, file, msgno);
   else{
      nap->a_input_charset = (incs == NULL || incs == (char*)-1)
            ? savestr(ok_vlook(ttycharset)) : incs;
#ifdef HAVE_ICONV
      if(cnvfp != NULL){
         nap->a_charset = oucs;
         if(!a_attachment_iconv(nap, cnvfp)){
            nap = NULL;
            aerr = n_ATTACH_ERR_ICONV_FAILED;
            goto jleave;
         }
         nap->a_conv = AC_TMPFILE;
      }else
#endif
            if(incs != NULL && oucs == NULL)
         nap->a_conv = AC_FIX_INCS;
      else
         nap->a_conv = AC_DEFAULT;
   }

   if(aplist != NULL){
      for(ap = aplist; ap->a_flink != NULL; ap = ap->a_flink)
         ;
      ap->a_flink = nap;
      nap->a_blink = ap;
   }else
      aplist = nap;

jleave:
   if(aerr_or_null != NULL)
      *aerr_or_null = aerr;
   if(newap_or_null != NULL)
      *newap_or_null = nap;
   NYD_OU;
   return aplist;
}

FL struct attachment *
n_attachment_append_list(struct attachment *aplist, char const *names){
   struct str shin;
   struct n_string shou, *shoup;
   NYD_IN;

   shoup = n_string_creat_auto(&shou);

   for(shin.s = n_UNCONST(names), shin.l = UIZ_MAX;;){
      struct attachment *nap;
      enum n_shexp_state shs;

      shs = n_shexp_parse_token((n_SHEXP_PARSE_TRUNC |
            n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_LOG |
            n_SHEXP_PARSE_IFS_ADD_COMMA | n_SHEXP_PARSE_IGNORE_EMPTY),
            shoup, &shin, NULL);
      if(shs & n_SHEXP_STATE_ERR_MASK)
         break;

      if(shs & n_SHEXP_STATE_OUTPUT){
         aplist = n_attachment_append(aplist, n_string_cp(shoup), NULL, &nap);
         if(nap != NULL){
            if(n_psonce & n_PSO_INTERACTIVE)
               a_attachment_yay(nap);
         }
      }

      if(shs & n_SHEXP_STATE_STOP)
         break;
   }
   n_string_gut(shoup);
   NYD_OU;
   return aplist;
}

FL struct attachment *
n_attachment_remove(struct attachment *aplist, struct attachment *ap){
   struct attachment *bap, *fap;
   NYD_IN;

#ifdef HAVE_DEVEL
   for(bap = aplist; aplist != NULL && aplist != ap; aplist = aplist->a_flink)
      ;
   assert(aplist != NULL);
   aplist = bap;
#endif

   if(ap == aplist){
      if((aplist = ap->a_flink) != NULL)
         aplist->a_blink = NULL;
   }else{
      bap = ap->a_blink;
      fap = ap->a_flink;
      if(bap != NULL)
         bap->a_flink = fap;
      if(fap != NULL)
         fap->a_blink = bap;
   }

   if(ap->a_conv == AC_TMPFILE)
      Fclose(ap->a_tmpf);
   NYD_OU;
   return aplist;
}

FL struct attachment *
n_attachment_find(struct attachment *aplist, char const *name,
      bool_t *stat_or_null){
   int msgno;
   char const *bname;
   bool_t status, sym;
   struct attachment *saved;
   NYD_IN;

   saved = NULL;
   status = FAL0;

   if((bname = strrchr(name, '/')) != NULL){
      for(++bname; aplist != NULL; aplist = aplist->a_flink)
         if(!strcmp(name, aplist->a_path)){
            status = TRU1;
            /* Exact match with path components: done */
            goto jleave;
         }else if(!strcmp(bname, aplist->a_path_bname)){
            if(!status){
               saved = aplist;
               status = TRU1;
            }else
               status = TRUM1;
         }
   }else if((msgno = a_attachment_is_msg(name)) < 0){
      for(sym = FAL0; aplist != NULL; aplist = aplist->a_flink){
         if(!strcmp(name, aplist->a_name)){
            if(!status || !sym){
               saved = aplist;
               sym = TRU1;
            }
         }else if(!strcmp(name, aplist->a_path_bname)){
            if(!status)
               saved = aplist;
         }else
            continue;
         status = status ? TRUM1 : TRU1;
      }
   }else{
      for(; aplist != NULL; aplist = aplist->a_flink){
         if(aplist->a_msgno > 0 && aplist->a_msgno == msgno){
            status = TRU1;
            goto jleave;
         }
      }
   }
   if(saved != NULL)
      aplist = saved;

jleave:
   if(stat_or_null != NULL)
      *stat_or_null = status;
   NYD_OU;
   return aplist;
}

FL struct attachment *
n_attachment_list_edit(struct attachment *aplist, enum n_go_input_flags gif){
   char prefix[32];
   struct str shin;
   struct n_string shou, *shoup;
   struct attachment *naplist, *ap;
   ui32_t attno;
   NYD_IN;

   if((n_psonce & (n_PSO_INTERACTIVE | n_PSO_ATTACH_QUOTE_NOTED)
         ) == n_PSO_INTERACTIVE){
      n_psonce |= n_PSO_ATTACH_QUOTE_NOTED;
      fprintf(n_stdout,
         _("# Only supports sh(1)ell-style quoting for file names\n"));
   }

   shoup = n_string_creat_auto(&shou);

   /* Modify already present ones?  Append some more? */
   attno = 1;

   for(naplist = NULL;;){
      snprintf(prefix, sizeof prefix, A_("#%" PRIu32 " filename: "), attno);

      if(aplist != NULL){
         /* TODO If we would create .a_path_user in append() after any
          * TODO expansion then we could avoid closing+rebuilding the temporary
          * TODO file if the new user input matches the original value! */
         if(aplist->a_conv == AC_TMPFILE)
            Fclose(aplist->a_tmpf);
         shin.s = n_shexp_quote_cp(aplist->a_path_user, FAL0);
      }else
         shin.s = n_UNCONST(n_empty);

      ap = NULL;
      if((shin.s = n_go_input_cp(gif, prefix, shin.s)) != NULL){
         enum n_shexp_state shs;
         char const *s_save;

         s_save = shin.s;
         shin.l = UIZ_MAX;
         shs = n_shexp_parse_token((n_SHEXP_PARSE_TRUNC |
               n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_LOG |
               n_SHEXP_PARSE_IGNORE_EMPTY),
               shoup, &shin, NULL);
         UIS(
         if(!(shs & n_SHEXP_STATE_STOP))
            n_err(_("# May be given one argument a time only: %s\n"),
               n_shexp_quote_cp(s_save, FAL0));
         )
         if((shs & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_STOP |
                  n_SHEXP_STATE_ERR_MASK)
               ) != (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_STOP))
            break;

         naplist = n_attachment_append(naplist, n_string_cp(shoup), NULL, &ap);
         if(ap != NULL){
            if(n_psonce & n_PSO_INTERACTIVE)
               a_attachment_yay(ap);
            ++attno;
         }
      }

      if(aplist != NULL){
         aplist = aplist->a_flink;
         /* In non-interactive or batch mode an empty line ends processing */
         if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_BATCH_FLAG))
            continue;
      }
      if(ap == NULL)
         break;
   }
   NYD_OU;
   return naplist;
}

FL ssize_t
n_attachment_list_print(struct attachment const *aplist, FILE *fp){
   struct attachment const *ap;
   ui32_t attno;
   ssize_t rv;
   NYD_IN;

   rv = 0;

   for(attno = 1, ap = aplist; ap != NULL; ++rv, ++attno, ap = ap->a_flink){
      if(ap->a_msgno > 0)
         fprintf(fp, "#%" PRIu32 ". message/rfc822: %u\n", attno, ap->a_msgno);
      else{
         char const *incs, *oucs;

         if(!(n_psonce & n_PSO_REPRODUCIBLE)){
            incs = ap->a_input_charset;
            oucs = ap->a_charset;
         }else
            incs = oucs = n_reproducible_name;

         fprintf(fp, "#%" PRIu32 ": %s [%s -- %s",
            attno, n_shexp_quote_cp(ap->a_name, FAL0),
            n_shexp_quote_cp(ap->a_path, FAL0),
            (ap->a_content_type != NULL
             ? ap->a_content_type : _("unclassified content")));

         if(ap->a_conv == AC_TMPFILE)
            /* I18N: input and output character set as given */
            fprintf(fp, _(", incs=%s -> oucs=%s (readily converted)"),
               incs, oucs);
         else if(ap->a_conv == AC_FIX_INCS)
            /* I18N: input character set as given, no conversion to apply */
            fprintf(fp, _(", incs=%s (no conversion)"), incs);
         else if(ap->a_conv == AC_DEFAULT){
            if(incs != NULL)
               /* I18N: input character set as given, output iterates */
               fprintf(fp, _(", incs=%s -> oucs=*sendcharsets*"), incs);
            else if(ap->a_content_type == NULL ||
                  !ascncasecmp(ap->a_content_type, "text/", 5))
               fprintf(fp, _(", default character set handling"));
         }
         fprintf(fp, "]\n");
      }
   }
   NYD_OU;
   return rv;
}

/* s-it-mode */
