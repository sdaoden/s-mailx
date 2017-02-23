/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message sending lifecycle, header composing, etc.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE sendout

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#undef SEND_LINESIZE
#define SEND_LINESIZE \
   ((1024 / B64_ENCODE_INPUT_PER_LINE) * B64_ENCODE_INPUT_PER_LINE)

enum fmt_flags{
   FMT_INC_INVADDR = 1<<0, /* _Do_ include invalid addresses */
   FMT_DOMIME = 1<<1,      /* Perform MIME conversion */
   FMT_COMMA = GCOMMA,
   FMT_FILES = GFILES,
   _FMT_GMASK = FMT_COMMA | FMT_FILES
};
n_CTA(!(_FMT_GMASK & (FMT_INC_INVADDR | FMT_DOMIME)),
   "Code-required condition not satisfied but actual bit carrier value");

static char const *__sendout_ident; /* TODO temporary hack; rewrite puthead() */
static char *  _sendout_boundary;
static si8_t   _sendout_error;

/* */
static enum okay     _putname(char const *line, enum gfield w,
                        enum sendaction action, size_t *gotcha,
                        char const *prefix, FILE *fo, struct name **xp,
                        enum gfield addflags);

/* Place Content-Type:, Content-Transfer-Encoding:, Content-Disposition:
 * headers, respectively */
static int           _put_ct(FILE *fo, char const *contenttype,
                        char const *charset);
SINLINE int          _put_cte(FILE *fo, enum conversion conv);
static int           _put_cd(FILE *fo, char const *cd, char const *filename);

/* Put all entries of the given header list */
static bool_t        _sendout_header_list(FILE *fo, struct n_header_field *hfp,
                        bool_t nodisp);

/* Write an attachment to the file buffer, converting to MIME */
static int a_sendout_attach_file(struct header *hp, struct attachment *ap,
            FILE *fo);
static int a_sendout__attach_file(struct header *hp, struct attachment *ap,
            FILE *fo);

/* There are non-local receivers, collect credentials etc. */
static bool_t        _sendbundle_setup_creds(struct sendbundle *sbpm,
                        bool_t signing_caps);

/* Attach a message to the file buffer */
static int a_sendout_attach_msg(struct header *hp, struct attachment *ap,
               FILE *fo);

/* Generate the body of a MIME multipart message */
static int           make_multipart(struct header *hp, int convert, FILE *fi,
                        FILE *fo, char const *contenttype, char const *charset);

/* Prepend a header in front of the collected stuff and return the new file */
static FILE *        infix(struct header *hp, FILE *fi);

/* Check whether Disposition-Notification-To: is desired */
static bool_t        _check_dispo_notif(struct name *mdn, struct header *hp,
                        FILE *fo);

/* Send mail to a bunch of user names.  The interface is through mail() */
static int           sendmail_internal(void *v, int recipient_record);

/* Deal with file and pipe addressees */
static struct name *a_sendout_file_a_pipe(struct name *names, FILE *fo,
                     bool_t *senderror);

/* Record outgoing mail if instructed to do so; in *record* unless to is set */
static bool_t        mightrecord(FILE *fp, struct name *to, bool_t resend);

static bool_t a_sendout__savemail(char const *name, FILE *fp, bool_t resend);

/*  */
static bool_t        _transfer(struct sendbundle *sbp);

static bool_t        __mta_start(struct sendbundle *sbp);
static char const ** __mta_prepare_args(struct name *to, struct header *hp);
static void          __mta_debug(struct sendbundle *sbp, char const *mta,
                        char const **args);

/* Create a Message-ID: header field.  Use either host name or from address */
static char const *a_sendout_random_id(struct header *hp, bool_t msgid);

/* Format the given header line to not exceed 72 characters */
static int           fmt(char const *str, struct name *np, FILE *fo,
                        enum fmt_flags ff);

/* Rewrite a message for resending, adding the Resent-Headers */
static int           infix_resend(FILE *fi, FILE *fo, struct message *mp,
                        struct name *to, int add_resent);

static enum okay
_putname(char const *line, enum gfield w, enum sendaction action,
   size_t *gotcha, char const *prefix, FILE *fo, struct name **xp,
   enum gfield addflags)
{
   struct name *np;
   enum okay rv = STOP;
   NYD_ENTER;

   np = lextract(line, GEXTRA | GFULL | addflags);
   if (xp != NULL)
      *xp = np;
   if (np == NULL)
      ;
   else if (fmt(prefix, np, fo, ((w & GCOMMA) |
         ((action != SEND_TODISP) ? FMT_DOMIME : 0))))
      rv = OKAY;
   else if (gotcha != NULL)
      ++(*gotcha);
   NYD_LEAVE;
   return rv;
}

static int
_put_ct(FILE *fo, char const *contenttype, char const *charset)
{
   int rv, i;
   NYD2_ENTER;

   if ((rv = fprintf(fo, "Content-Type: %s", contenttype)) < 0)
      goto jerr;

   if (charset == NULL)
      goto jend;

   if (putc(';', fo) == EOF)
      goto jerr;
   ++rv;

   if (strlen(contenttype) + sizeof("Content-Type: ;")-1 > 50) {
      if (putc('\n', fo) == EOF)
         goto jerr;
      ++rv;
   }

   /* C99 */{
      size_t l = strlen(charset);
      char *lcs = salloc(l +1);

      for(l = 0; *charset != '\0'; ++l, ++charset)
         lcs[l] = lowerconv(*charset);
      lcs[l] = '\0';
      charset = lcs;
   }
   if ((i = fprintf(fo, " charset=%s", charset)) < 0)
      goto jerr;
   rv += i;

jend:
   if (putc('\n', fo) == EOF)
      goto jerr;
   ++rv;
jleave:
   NYD2_LEAVE;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

SINLINE int
_put_cte(FILE *fo, enum conversion conv)
{
   int rv;
   NYD2_ENTER;

   /* RFC 2045, 6.1.:
    *    This is the default value -- that is,
    *    "Content-Transfer-Encoding: 7BIT" is assumed if the
    *     Content-Transfer-Encoding header field is not present.
    */
   rv = (conv == CONV_7BIT) ? 0
         : fprintf(fo, "Content-Transfer-Encoding: %s\n",
            mime_enc_from_conversion(conv));
   NYD2_LEAVE;
   return rv;
}

static int
_put_cd(FILE *fo, char const *cd, char const *filename)
{
   struct str f;
   si8_t mpc;
   int rv;
   NYD2_ENTER;

   f.s = NULL;

   /* xxx Ugly with the trailing space in case of wrap! */
   if ((rv = fprintf(fo, "Content-Disposition: %s; ", cd)) < 0)
      goto jerr;

   if (!(mpc = mime_param_create(&f, "filename", filename)))
      goto jerr;
   /* Always fold if result contains newlines */
   if (mpc < 0 || f.l + rv > MIME_LINELEN) { /* FIXME MIME_LINELEN_MAX */
      if (putc('\n', fo) == EOF || putc(' ', fo) == EOF)
         goto jerr;
      rv += 2;
   }
   if (fputs(f.s, fo) == EOF || putc('\n', fo) == EOF)
      goto jerr;
   rv += (int)++f.l;

jleave:
   NYD2_LEAVE;
   return rv;
jerr:
   rv = -1;
   goto jleave;

}

static bool_t
_sendout_header_list(FILE *fo, struct n_header_field *hfp, bool_t nodisp){
   bool_t rv;
   NYD2_ENTER;

   for(rv = TRU1; hfp != NULL; hfp = hfp->hf_next)
      if(fwrite(hfp->hf_dat, sizeof(char), hfp->hf_nl, fo) != hfp->hf_nl ||
            putc(':', fo) == EOF || putc(' ', fo) == EOF ||
            xmime_write(hfp->hf_dat + hfp->hf_nl +1, hfp->hf_bl, fo,
               (!nodisp ? CONV_NONE : CONV_TOHDR),
               (!nodisp ? TD_ISPR | TD_ICONV : TD_ICONV)) < 0 ||
            putc('\n', fo) == EOF){
         rv = FAL0;
         break;
      }
   NYD_LEAVE;
   return rv;
}

static int
a_sendout_attach_file(struct header *hp, struct attachment *ap, FILE *fo)
{
   /* TODO of course, the MIME classification needs to performed once
    * TODO only, not for each and every charset anew ... ;-// */
   char *charset_iter_orig[2];
   long offs;
   int err = 0;
   NYD_ENTER;

   /* Is this already in target charset?  Simply copy over */
   if (ap->a_conv == AC_TMPFILE) {
      err = a_sendout__attach_file(hp, ap, fo);
      Fclose(ap->a_tmpf);
      DBG( ap->a_tmpf = NULL; )
      goto jleave;
   }

   /* If we don't apply charset conversion at all (fixed input=ouput charset)
    * we also simply copy over, since it's the users desire */
   if (ap->a_conv == AC_FIX_INCS) {
      ap->a_charset = ap->a_input_charset;
      err = a_sendout__attach_file(hp, ap, fo);
      goto jleave;
   } else
      assert(ap->a_input_charset != NULL);

   /* Otherwise we need to iterate over all possible output charsets */
   if ((offs = ftell(fo)) == -1) {
      err = EIO;
      goto jleave;
   }
   charset_iter_recurse(charset_iter_orig);
   for (charset_iter_reset(NULL);; charset_iter_next()) {
      if (!charset_iter_is_valid()) {
         err = EILSEQ;
         break;
      }
      err = a_sendout__attach_file(hp, ap, fo);
      if (err == 0 || (err != EILSEQ && err != EINVAL))
         break;
      clearerr(fo);
      if (fseek(fo, offs, SEEK_SET) == -1) {
         err = EIO;
         break;
      }
      if (ap->a_conv != AC_DEFAULT) {
         err = EILSEQ;
         break;
      }
      ap->a_charset = NULL;
   }
   charset_iter_restore(charset_iter_orig);
jleave:
   NYD_LEAVE;
   return err;
}

static int
a_sendout__attach_file(struct header *hp, struct attachment *ap, FILE *fo)
{
   int err = 0, do_iconv;
   FILE *fi;
   char const *charset;
   enum conversion convert;
   char *buf;
   size_t bufsize, lncnt, inlen;
   NYD_ENTER;

   /* Either charset-converted temporary file, or plain path */
   if (ap->a_conv == AC_TMPFILE) {
      fi = ap->a_tmpf;
      assert(ftell(fi) == 0);
   } else if ((fi = Fopen(ap->a_path, "r")) == NULL) {
      err = errno;
      n_err(_("%s: %s\n"), n_shexp_quote_cp(ap->a_path, FAL0), strerror(err));
      goto jleave;
   }

   /* MIME part header for attachment */
   {  char const *ct, *cp;

      ct = ap->a_content_type;
      charset = ap->a_charset;
      convert = mime_type_classify_file(fi, (char const**)&ct,
         &charset, &do_iconv);
      if (charset == NULL || ap->a_conv == AC_FIX_INCS ||
            ap->a_conv == AC_TMPFILE)
         do_iconv = 0;

      if (fprintf(fo, "\n--%s\n", _sendout_boundary) < 0 ||
            _put_ct(fo, ct, charset) < 0 || _put_cte(fo, convert) < 0 ||
            _put_cd(fo, ap->a_content_disposition, ap->a_name) < 0)
         goto jerr_header;

      if((cp = ok_vlook(stealthmua)) == NULL || !strcmp(cp, "noagent")){
         struct name *np;

         if((np = ap->a_content_id) != NULL)
            cp = np->n_name;
         else
            cp = a_sendout_random_id(hp, FAL0);

         if(cp != NULL && fprintf(fo, "Content-ID: <%s>\n", cp) < 0)
            goto jerr_header;
      }

      if ((cp = ap->a_content_description) != NULL &&
            (fputs("Content-Description: ", fo) == EOF ||
             xmime_write(cp, strlen(cp), fo, CONV_TOHDR, (TD_ISPR | TD_ICONV)
               ) < 0 || putc('\n', fo) == EOF))
         goto jerr_header;

      if (putc('\n', fo) == EOF) {
jerr_header:
         err = errno;
         goto jerr_fclose;
      }
   }

#ifdef HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
   if (do_iconv) {
      if (asccasecmp(charset, ap->a_input_charset) &&
            (iconvd = n_iconv_open(charset, ap->a_input_charset)
               ) == (iconv_t)-1 && (err = errno) != 0) {
         if (err == EINVAL)
            n_err(_("Cannot convert from %s to %s\n"), ap->a_input_charset,
               charset);
         else
            n_err(_("iconv_open: %s\n"), strerror(err));
         goto jerr_fclose;
      }
   }
#endif

   bufsize = SEND_LINESIZE;
   buf = smalloc(bufsize);
   if (convert == CONV_TOQP
#ifdef HAVE_ICONV
         || iconvd != (iconv_t)-1
#endif
   )
      lncnt = fsize(fi);
   for (;;) {
      if (convert == CONV_TOQP
#ifdef HAVE_ICONV
            || iconvd != (iconv_t)-1
#endif
      ) {
         if (fgetline(&buf, &bufsize, &lncnt, &inlen, fi, 0) == NULL)
            break;
      } else if ((inlen = fread(buf, sizeof *buf, bufsize, fi)) == 0)
         break;
      if (xmime_write(buf, inlen, fo, convert, TD_ICONV) < 0) {
         err = errno;
         goto jerr;
      }
   }
   if (ferror(fi))
      err = EDOM;
jerr:
   free(buf);
jerr_fclose:
   if (ap->a_conv != AC_TMPFILE)
      Fclose(fi);
jleave:
   NYD_LEAVE;
   return err;
}

static bool_t
_sendbundle_setup_creds(struct sendbundle *sbp, bool_t signing_caps)
{
   bool_t v15, rv = FAL0;
   char *shost, *from;
#ifdef HAVE_SMTP
   char const *smtp;
#endif
   NYD_ENTER;

   v15 = ok_blook(v15_compat);
   shost = (v15 ? ok_vlook(smtp_hostname) : NULL);
   from = ((signing_caps || !v15 || shost == NULL)
         ? skin(myorigin(sbp->sb_hp)) : NULL);

   if (signing_caps) {
      if (from == NULL) {
#ifdef HAVE_SSL
         n_err(_("No *from* address for signing specified\n"));
         goto jleave;
#endif
      } else
         sbp->sb_signer.l = strlen(sbp->sb_signer.s = from);
   }

#ifdef HAVE_SMTP
   if ((smtp = ok_vlook(smtp)) == NULL) { /* TODO v15 url_creat(,ok_vlook(mta)*/
      char const *proto;

      /* *smtp* OBSOLETE message in mta_start() */
      if ((smtp = ok_vlook(mta)) == NULL ||
            (proto = n_servbyname(smtp, NULL)) == NULL || *proto == '\0') {
         rv = TRU1;
         goto jleave;
      }
   }

   if (!url_parse(&sbp->sb_url, CPROTO_SMTP, smtp))
      goto jleave;

   if (v15) {
      if (shost == NULL) {
         if (from == NULL)
            goto jenofrom;
         sbp->sb_url.url_u_h.l = strlen(sbp->sb_url.url_u_h.s = from);
      } else
         __sendout_ident = sbp->sb_url.url_u_h.s;
      if (!ccred_lookup(&sbp->sb_ccred, &sbp->sb_url))
         goto jleave;
   } else {
      if (sbp->sb_url.url_had_user || sbp->sb_url.url_pass.s != NULL) {
         n_err(_("New-style URL used without *v15-compat* being set\n"));
         goto jleave;
      }
      /* TODO part of the entire myorigin() disaster, get rid of this! */
      if (from == NULL) {
jenofrom:
         n_err(_("Your configuration requires a *from* address, "
            "but none was given\n"));
         goto jleave;
      }
      if (!ccred_lookup_old(&sbp->sb_ccred, CPROTO_SMTP, from))
         goto jleave;
      sbp->sb_url.url_u_h.l = strlen(sbp->sb_url.url_u_h.s = from);
   }

   rv = TRU1;
#endif /* HAVE_SMTP */
#if defined HAVE_SSL || defined HAVE_SMTP
jleave:
#endif
   NYD_LEAVE;
   return rv;
}

static int
a_sendout_attach_msg(struct header *hp, struct attachment *ap, FILE *fo)
{
   struct message *mp;
   char const *ccp;
   int rv;
   NYD_ENTER;
   n_UNUSED(hp);

   fprintf(fo, "\n--%s\nContent-Type: message/rfc822\n"
       "Content-Disposition: inline\n", _sendout_boundary);
   if ((ccp = ap->a_content_description) != NULL)
      fprintf(fo, "Content-Description: %s\n", ccp);/* TODO MIME! */
   putc('\n', fo);

   mp = message + ap->a_msgno - 1;
   touch(mp);
   rv = (sendmp(mp, fo, 0, NULL, SEND_RFC822, NULL) < 0) ? -1 : 0;
   NYD_LEAVE;
   return rv;
}

static int
make_multipart(struct header *hp, int convert, FILE *fi, FILE *fo,
   char const *contenttype, char const *charset)
{
   struct attachment *att;
   int rv = -1;
   NYD_ENTER;

   fputs("This is a multi-part message in MIME format.\n", fo);
   if (fsize(fi) != 0) {
      char const *cp;
      char *buf;
      size_t sz, bufsize, cnt;

      if (fprintf(fo, "\n--%s\n", _sendout_boundary) < 0 ||
            _put_ct(fo, contenttype, charset) < 0 ||
            _put_cte(fo, convert) < 0 ||
            fprintf(fo, "Content-Disposition: inline\n") < 0)
         goto jleave;
      if (((cp = ok_vlook(stealthmua)) == NULL || !strcmp(cp, "noagent")) &&
            (cp = a_sendout_random_id(hp, FAL0)) != NULL &&
            fprintf(fo, "Content-ID: <%s>\n", cp) < 0)
         goto jleave;
      if(putc('\n', fo) == EOF)
         goto jleave;

      buf = smalloc(bufsize = SEND_LINESIZE);
      if (convert == CONV_TOQP
#ifdef HAVE_ICONV
            || iconvd != (iconv_t)-1
#endif
      ) {
         fflush(fi);
         cnt = fsize(fi);
      }
      for (;;) {
         if (convert == CONV_TOQP
#ifdef HAVE_ICONV
               || iconvd != (iconv_t)-1
#endif
         ) {
            if (fgetline(&buf, &bufsize, &cnt, &sz, fi, 0) == NULL)
               break;
         } else if ((sz = fread(buf, sizeof *buf, bufsize, fi)) == 0)
            break;

         if (xmime_write(buf, sz, fo, convert, TD_ICONV) < 0) {
            free(buf);
            goto jleave;
         }
      }
      free(buf);

      if (ferror(fi))
         goto jleave;
   }

   for (att = hp->h_attach; att != NULL; att = att->a_flink) {
      if (att->a_msgno) {
         if (a_sendout_attach_msg(hp, att, fo) != 0)
            goto jleave;
      } else if (a_sendout_attach_file(hp, att, fo) != 0)
         goto jleave;
   }

   /* the final boundary with two attached dashes */
   fprintf(fo, "\n--%s--\n", _sendout_boundary);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static FILE *
infix(struct header *hp, FILE *fi) /* TODO check */
{
   FILE *nfo, *nfi = NULL;
   char *tempMail;
   char const *contenttype, *charset = NULL;
   enum conversion convert;
   int do_iconv = 0, err;
#ifdef HAVE_ICONV
   char const *tcs, *convhdr = NULL;
#endif
   NYD_ENTER;

   if ((nfo = Ftmp(&tempMail, "infix", OF_WRONLY | OF_HOLDSIGS | OF_REGISTER))
         == NULL) {
      n_perr(_("temporary mail file"), 0);
      goto jleave;
   }
   if ((nfi = Fopen(tempMail, "r")) == NULL) {
      n_perr(tempMail, 0);
      Fclose(nfo);
   }
   Ftmp_release(&tempMail);
   if (nfi == NULL)
      goto jleave;

   n_pstate &= ~n_PS_HEADER_NEEDED_MIME; /* TODO hack -> be carrier tracked */

   contenttype = "text/plain"; /* XXX mail body - always text/plain, want XX? */
   if((n_poption & n_PO_Mm_FLAG) && n_poption_arg_Mm != NULL)
      contenttype = n_poption_arg_Mm;
   convert = mime_type_classify_file(fi, &contenttype, &charset, &do_iconv);

#ifdef HAVE_ICONV
   tcs = ok_vlook(ttycharset);
   if ((convhdr = need_hdrconv(hp))) {
      if (iconvd != (iconv_t)-1) /* XXX  */
         n_iconv_close(iconvd);
      if (asccasecmp(convhdr, tcs) != 0 &&
            (iconvd = n_iconv_open(convhdr, tcs)) == (iconv_t)-1 &&
            (err = errno) != 0)
         goto jiconv_err;
   }
#endif
   if (puthead(FAL0, hp, nfo,
         (GTO | GSUBJECT | GCC | GBCC | GNL | GCOMMA | GUA | GMIME | GMSGID |
         GIDENT | GREF | GDATE), SEND_MBOX, convert, contenttype, charset))
      goto jerr;
#ifdef HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
#endif

#ifdef HAVE_ICONV
   if (do_iconv && charset != NULL) { /*TODO charset->mime_type_classify_file*/
      if (asccasecmp(charset, tcs) != 0 &&
            (iconvd = n_iconv_open(charset, tcs)) == (iconv_t)-1 &&
            (err = errno) != 0) {
jiconv_err:
         if (err == EINVAL)
            n_err(_("Cannot convert from %s to %s\n"), tcs, charset);
         else
            n_perr("iconv_open", 0);
         goto jerr;
      }
   }
#endif

   if (hp->h_attach != NULL) {
      if (make_multipart(hp, convert, fi, nfo, contenttype, charset) != 0)
         goto jerr;
   } else {
      size_t sz, bufsize, cnt;
      char *buf;

      if (convert == CONV_TOQP
#ifdef HAVE_ICONV
            || iconvd != (iconv_t)-1
#endif
      ) {
         fflush(fi);
         cnt = fsize(fi);
      }
      buf = smalloc(bufsize = SEND_LINESIZE);
      for (err = 0;;) {
         if (convert == CONV_TOQP
#ifdef HAVE_ICONV
               || iconvd != (iconv_t)-1
#endif
         ) {
            if (fgetline(&buf, &bufsize, &cnt, &sz, fi, 0) == NULL)
               break;
         } else if ((sz = fread(buf, sizeof *buf, bufsize, fi)) == 0)
            break;
         if (xmime_write(buf, sz, nfo, convert, TD_ICONV) < 0) {
            err = 1;
            break;
         }
      }
      free(buf);

      if (err || ferror(fi)) {
jerr:
         Fclose(nfo);
         Fclose(nfi);
#ifdef HAVE_ICONV
         if (iconvd != (iconv_t)-1)
            n_iconv_close(iconvd);
#endif
         nfi = NULL;
         goto jleave;
      }
   }

#ifdef HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
#endif

   fflush(nfo);
   if ((err = ferror(nfo)))
      n_perr(_("temporary mail file"), 0);
   Fclose(nfo);
   if (!err) {
      fflush_rewind(nfi);
      Fclose(fi);
   } else {
      Fclose(nfi);
      nfi = NULL;
   }
jleave:
   NYD_LEAVE;
   return nfi;
}

static bool_t
_check_dispo_notif(struct name *mdn, struct header *hp, FILE *fo)
{
   char const *from;
   bool_t rv = TRU1;
   NYD_ENTER;

   /* TODO smtp_disposition_notification (RFC 3798): relation to return-path
    * TODO not yet checked */
   if (!ok_blook(disposition_notification_send))
      goto jleave;

   if (mdn != NULL && mdn != (struct name*)0x1)
      from = mdn->n_name;
   else if ((from = myorigin(hp)) == NULL) {
      if (n_poption & n_PO_D_V)
         n_err(_("*disposition-notification-send*: *from* not set\n"));
      goto jleave;
   }

   if (fmt("Disposition-Notification-To:", nalloc(n_UNCONST(from), 0), fo, 0))
      rv = FAL0;
jleave:
   NYD_LEAVE;
   return rv;
}

static int
sendmail_internal(void *v, int recipient_record)
{
   struct header head;
   char *str = v;
   int rv;
   NYD_ENTER;

   memset(&head, 0, sizeof head);
   head.h_to = lextract(str, GTO | GFULL);
   rv = mail1(&head, 0, NULL, NULL, recipient_record, 0);
   NYD_LEAVE;
   return (rv == 0);
}

static struct name *
a_sendout_file_a_pipe(struct name *names, FILE *fo, bool_t *senderror){
   ui32_t pipecnt, xcnt, i;
   char const *sh;
   struct name *np;
   FILE *fp, **fppa;
   NYD_ENTER;

   fp = NULL;
   fppa = NULL;

   /* Look through all recipients and do a quick return if no file or pipe
    * addressee is found */
   for(pipecnt = xcnt = 0, np = names; np != NULL; np = np->n_flink){
      if(np->n_type & GDEL)
         continue;
      switch(np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE){
      case NAME_ADDRSPEC_ISFILE: ++xcnt; break;
      case NAME_ADDRSPEC_ISPIPE: ++pipecnt; break;
      }
   }
   if((pipecnt | xcnt) == 0)
      goto jleave;

   /* Otherwise create an array of file descriptors for each found pipe
    * addressee to get around the dup(2)-shared-file-offset problem, i.e.,
    * each pipe subprocess needs its very own file descriptor, and we need
    * to deal with that.
    * To make our life a bit easier let's just use the auto-reclaimed
    * string storage */
   if(pipecnt == 0 || (n_poption & n_PO_DEBUG)){
      pipecnt = 0;
      sh = NULL;
   }else{
      i = sizeof(FILE*) * pipecnt;
      fppa = n_lofi_alloc(i);
      memset(fppa, 0, i);
      sh = ok_vlook(SHELL);
   }

   for(np = names; np != NULL; np = np->n_flink){
      if(!(np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE) || (np->n_type & GDEL))
         continue;

      /* In days of old we removed the entry from the the list; now for sake of
       * header expansion we leave it in and mark it as deleted */
      np->n_type |= GDEL;

      if(n_poption & n_PO_D_VV)
         n_err(_(">>> Writing message via %s\n"),
            n_shexp_quote_cp(np->n_name, FAL0));
      /* We _do_ write to STDOUT, anyway! */
      if((n_poption & n_PO_DEBUG) && ((np->n_flags & NAME_ADDRSPEC_ISPIPE) ||
            np->n_name[0] != '-' || np->n_name[1] != '\0'))
         continue;

      /* See if we have copied the complete message out yet.  If not, do so */
      if(fp == NULL){
         int c;
         char *tempEdit;

         if((fp = Ftmp(&tempEdit, "outof", OF_RDWR | OF_HOLDSIGS | OF_REGISTER)
               ) == NULL){
            n_perr(_("Creation of temporary image"), 0);
            pipecnt = 0;
            goto jerror;
         }

         for(i = 0; i < pipecnt; ++i)
            if((fppa[i] = Fopen(tempEdit, "r")) == NULL){
               n_perr(_("Creation of pipe image descriptor"), 0);
               break;
            }

         Ftmp_release(&tempEdit);
         if(i != pipecnt){
            pipecnt = i;
            goto jerror;
         }

         fprintf(fp, "From %s %s", ok_vlook(LOGNAME), time_current.tc_ctime);
         c = EOF;
         while(i = c, (c = getc(fo)) != EOF)
            putc(c, fp);
         rewind(fo);
         if((int)i != '\n')
            putc('\n', fp);
         putc('\n', fp);
         fflush(fp);
         if(ferror(fp)){
            n_perr(_("Finalizing write of temporary image"), 0);
            goto jerror;
         }

         /* From now on use xcnt as a counter for pipecnt */
         xcnt = 0;
      }

      /* Now either copy "image" to the desired file or give it as the standard
       * input to the desired program as appropriate */
      if(np->n_flags & NAME_ADDRSPEC_ISPIPE){
         int pid;
         sigset_t nset;

         sigemptyset(&nset);
         sigaddset(&nset, SIGHUP);
         sigaddset(&nset, SIGINT);
         sigaddset(&nset, SIGQUIT);
         pid = start_command(sh, &nset, fileno(fppa[xcnt++]), COMMAND_FD_NULL,
               "-c", &np->n_name[1], NULL, NULL);
         if(pid < 0){
            n_err(_("Piping message to %s failed\n"),
               n_shexp_quote_cp(np->n_name, FAL0));
            goto jerror;
         }
         free_child(pid);
      }else{
         int c;
         FILE *fout;
         char const *fname, *fnameq;

         if((fname = fexpand(np->n_name, FEXP_LOCAL | FEXP_NOPROTO)) == NULL)
            goto jerror;
         fnameq = n_shexp_quote_cp(fname, FAL0);

         if(fname[0] == '-' && fname[1] == '\0')
            fout = n_stdout;
         else if((fout = Zopen(fname, "a")) == NULL){
            n_err(_("Writing message to %s failed: %s\n"),
               fnameq, strerror(errno));
            goto jerror;
         }

         rewind(fp);
         while((c = getc(fp)) != EOF)
            putc(c, fout);
         if(ferror(fout)){
            n_err(_("Writing message to %s failed: %s\n"),
               fnameq, _("write error"));
            *senderror = TRU1;
         }

         if(fout != n_stdout)
            Fclose(fout);
      }
   }

jleave:
   if(fp != NULL)
      Fclose(fp);
   if(fppa != NULL){
      for(i = 0; i < pipecnt; ++i)
         if((fp = fppa[i]) != NULL)
            Fclose(fp);
      n_lofi_free(fppa);
   }
   NYD_LEAVE;
   return names;

jerror:
   *senderror = TRU1;
   while(np != NULL){
      if(np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE)
         np->n_type |= GDEL;
      np = np->n_flink;
   }
   goto jleave;
}

static bool_t
mightrecord(FILE *fp, struct name *to, bool_t resend)
{
   char *cp, *cq;
   char const *ep;
   bool_t rv = TRU1;
   NYD_ENTER;

   if (n_poption & n_PO_DEBUG)
      cp = NULL;
   else if (to != NULL) {
      cp = savestr(skinned_name(to));
      for (cq = cp; *cq != '\0' && *cq != '@'; ++cq)
         ;
      *cq = '\0';
   } else
      cp = ok_vlook(record);

   if (cp != NULL) {
      if ((ep = fexpand(cp, FEXP_FULL)) == NULL) {
         ep = "NULL";
         goto jbail;
      }

      if (*ep != '/' && *ep != '+' && ok_blook(outfolder) &&
            which_protocol(ep) == PROTO_FILE) {
         size_t i = strlen(cp);
         cq = salloc(i + 1 +1);
         cq[0] = '+';
         memcpy(cq + 1, cp, i +1);
         cp = cq;
         if ((ep = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL) {
            ep = "NULL";
            goto jbail;
         }
      }

      if (!a_sendout__savemail(ep, fp, resend)) {
jbail:
         n_err(_("Failed to save message in %s - message not sent\n"),
            n_shexp_quote_cp(ep, FAL0));
         n_exit_status |= n_EXIT_ERR;
         savedeadletter(fp, 1);
         rv = FAL0;
      }
   }
   NYD_LEAVE;
   return rv;
}

static bool_t
a_sendout__savemail(char const *name, FILE *fp, bool_t resend){
   FILE *fo;
   size_t bufsize, buflen, cnt;
   bool_t rv, emptyline;
   char *buf;
   NYD_ENTER;

   buf = smalloc(bufsize = LINESIZE);
   rv = emptyline = FAL0;

   if((fo = Zopen(name, "a+")) == NULL){
      if((fo = Zopen(name, "wx")) == NULL){
         n_perr(name, 0);
         goto j_leave;
      }
      emptyline = TRU1;
   }

   /* TODO RETURN check, but be aware of protocols: v15: Mailbox->lock()! */
   n_file_lock(fileno(fo), FLT_WRITE, 0,0, UIZ_MAX);

   rv = TRU1;

   if(!emptyline){
      if(fseek(fo, -2L, SEEK_END) == 0){ /* TODO Should be Mailbox::->append */
         switch(fread(buf, sizeof *buf, 2, fo)){
         case 2:
            if(buf[1] != '\n')
               emptyline = TRU1;
            break;
         case 1:
            if(buf[0] != '\n')
               emptyline = TRU1;
            break;
         default:
            if(ferror(fo)){
               n_perr(name, 0);
               rv = FAL0;
               goto jleave;
            }
         }
         if(emptyline){
            putc('\n', fo);
            fflush(fo);
         }
      }
   }

   fflush_rewind(fp);

   fprintf(fo, "From %s %s", ok_vlook(LOGNAME), time_current.tc_ctime);
   for(emptyline = FAL0, buflen = 0, cnt = fsize(fp);
         fgetline(&buf, &bufsize, &cnt, &buflen, fp, 0) != NULL;){
      /* Only if we are resending it can happen that we have to quote From_
       * lines here; we don't generate messages which are ambiguous ourselves */
      if(resend){
         if(emptyline && is_head(buf, buflen, FAL0))
            putc('>', fo);
      }DBG(else assert(!is_head(buf, buflen, FAL0)); )

      emptyline = (buflen > 0 && *buf == '\n');
      fwrite(buf, sizeof *buf, buflen, fo);
   }
   if(buflen > 0 && buf[buflen - 1] != '\n')
      putc('\n', fo);
   putc('\n', fo);
   fflush(fo);
   if(ferror(fo)){
      n_perr(name, 0);
      rv = FAL0;
   }

jleave:
   really_rewind(fp);
   if(Fclose(fo) != 0)
      rv = FAL0;
j_leave:
   free(buf);
   NYD_LEAVE;
   return rv;
}

static bool_t
_transfer(struct sendbundle *sbp)
{
   struct name *np;
   ui32_t cnt;
   bool_t rv = TRU1;
   NYD_ENTER;

   for (cnt = 0, np = sbp->sb_to; np != NULL;) {
      char const k[] = "smime-encrypt-", *cp;
      size_t nl = strlen(np->n_name);
      char *vs = ac_alloc(sizeof(k)-1 + nl +1);
      memcpy(vs, k, sizeof(k) -1);
      memcpy(vs + sizeof(k) -1, np->n_name, nl +1);

      if ((cp = n_var_vlook(vs, FAL0)) != NULL) {
#ifdef HAVE_SSL
         FILE *ef;

         if ((ef = smime_encrypt(sbp->sb_input, cp, np->n_name)) != NULL) {
            FILE *fisave = sbp->sb_input;
            struct name *nsave = sbp->sb_to;

            sbp->sb_to = ndup(np, np->n_type & ~(GFULL | GSKIN));
            sbp->sb_input = ef;
            if (!__mta_start(sbp))
               rv = FAL0;
            sbp->sb_to = nsave;
            sbp->sb_input = fisave;

            Fclose(ef);
         } else {
#else
            n_err(_("No SSL support compiled in\n"));
            rv = FAL0;
#endif
            n_err(_("Message not sent to: %s\n"), np->n_name);
            _sendout_error = TRU1;
#ifdef HAVE_SSL
         }
#endif
         rewind(sbp->sb_input);

         if (np->n_flink != NULL)
            np->n_flink->n_blink = np->n_blink;
         if (np->n_blink != NULL)
            np->n_blink->n_flink = np->n_flink;
         if (np == sbp->sb_to)
            sbp->sb_to = np->n_flink;
         np = np->n_flink;
      } else {
         ++cnt;
         np = np->n_flink;
      }
      ac_free(vs);
   }

   if (cnt > 0 && (ok_blook(smime_force_encryption) || !__mta_start(sbp)))
      rv = FAL0;
   NYD_LEAVE;
   return rv;
}

static bool_t
__mta_start(struct sendbundle *sbp)
{
   pid_t pid;
   sigset_t nset;
   char const **args, *mta;
   bool_t rv;
   NYD_ENTER;

   /* Let rv mean "is smtp-based MTA" */
   if((mta = ok_vlook(smtp)) != NULL){
      n_OBSOLETE(_("please don't use *smtp*, but assign an SMTP URL to *mta*"));
      /* For *smtp* the smtp:// protocol was optional; be simple: don't check
       * that *smtp* is misused with file:// or so */
      if(n_servbyname(mta, NULL) == NULL)
         mta = savecat("smtp://", mta);
      rv = TRU1;
   }else{
      char const *proto;

      if((proto = ok_vlook(sendmail)) != NULL)
         n_OBSOLETE(_("please use *mta* instead of *sendmail*"));
      if((mta = ok_vlook(mta)) == NULL){ /* TODO v15: mta = ok_vlook(mta); */
         if(proto == NULL){
            mta = VAL_MTA;
            rv = FAL0;
         }else
            rv = TRU1;
      }
      /* TODO for now this is pretty hacky: in v15 we should simply create
       * TODO an URL object; i.e., be able to do so, and it does it right
       * TODO I.e.,: url_creat(&url, ok_vlook(mta)); */
      else if((proto = n_servbyname(mta, NULL)) != NULL){
         if(*proto == '\0'){
            mta += sizeof("file://") -1;
            rv = FAL0;
         }else
            rv = TRU1;
      }else
         rv = FAL0;
   }

   if(!rv){
      char const *mta_base;

      if((mta = fexpand(mta_base = mta, FEXP_LOCAL | FEXP_NOPROTO)) == NULL){
         n_err(_("*mta* variable expansion failure: %s\n"),
            n_shexp_quote_cp(mta_base, FAL0));
         goto jstop;
      }

      args = __mta_prepare_args(sbp->sb_to, sbp->sb_hp);
      if (n_poption & n_PO_DEBUG) {
         __mta_debug(sbp, mta, args);
         rv = TRU1;
         goto jleave;
      }
   } else {
      n_UNINIT(args, NULL);
#ifndef HAVE_SMTP
      n_err(_("No SMTP support compiled in\n"));
      goto jstop;
#else
      /* C99 */{
         struct name *np;

         for(np = sbp->sb_to; np != NULL; np = np->n_flink)
            if(!(np->n_type & GDEL) && (np->n_flags & NAME_ADDRSPEC_ISNAME)){
               n_err(_("SMTP *mta* cannot send to alias name: %s\n"),
                  n_shexp_quote_cp(np->n_name, FAL0));
               rv = FAL0;
            }
         if(!rv)
            goto jstop;
      }

      if (n_poption & n_PO_DEBUG) {
         (void)smtp_mta(sbp);
         rv = TRU1;
         goto jleave;
      }
#endif
   }

   /* Fork, set up the temporary mail file as standard input for "mail", and
    * exec with the user list we generated far above */
   if ((pid = fork_child()) == -1) {
      n_perr("fork", 0);
jstop:
      savedeadletter(sbp->sb_input, 0);
      _sendout_error = TRU1;
      goto jleave;
   }
   if (pid == 0) {
      sigemptyset(&nset);
      sigaddset(&nset, SIGHUP);
      sigaddset(&nset, SIGINT);
      sigaddset(&nset, SIGQUIT);
      sigaddset(&nset, SIGTSTP);
      sigaddset(&nset, SIGTTIN);
      sigaddset(&nset, SIGTTOU);
      /* n_stdin = */freopen("/dev/null", "r", stdin);
#ifdef HAVE_SMTP
      if (rv) {
         prepare_child(&nset, 0, 1);
         if (smtp_mta(sbp))
            _exit(n_EXIT_OK);
      } else
#endif
      {
         char const *ecp;
         int e;

         prepare_child(&nset, fileno(sbp->sb_input), -1);
         execv(mta, n_UNCONST(args));
         e = errno;
         ecp = (e != ENOENT) ? strerror(e)
               : _("executable not found (adjust *mta* variable)");
         n_err(_("Cannot start %s: %s\n"), n_shexp_quote_cp(mta, FAL0), ecp);
      }
      savedeadletter(sbp->sb_input, 1);
      n_err(_("... message not sent\n"));
      _exit(n_EXIT_ERR);
   }

   if ((n_poption & n_PO_D_V) || ok_blook(sendwait)) {
      if (!(rv = wait_child(pid, NULL)))
         _sendout_error = TRU1;
   } else {
      free_child(pid);
      rv = TRU1;
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static char const **
__mta_prepare_args(struct name *to, struct header *hp)
{
   size_t vas_cnt, i, j;
   char **vas;
   char const **args, *cp, *cp_v15compat;
   bool_t snda;
   NYD_ENTER;

   if((cp_v15compat = ok_vlook(sendmail_arguments)) != NULL)
      n_OBSOLETE(_("please use *mta-arguments*, not *sendmail-arguments*"));
   if((cp = ok_vlook(mta_arguments)) == NULL)
      cp = cp_v15compat;
   if ((cp /* TODO v15: = ok_vlook(mta_arguments)*/) == NULL) {
      vas_cnt = 0;
      vas = NULL;
   } else {
      /* Don't assume anything on the content but do allocate exactly j slots;
       * like this getrawlist will never overflow (and return -1) */
      j = strlen(cp);
      vas = n_lofi_alloc(sizeof(*vas) * j);
      vas_cnt = (size_t)getrawlist(TRU1, vas, j, cp, j);
   }

   i = 4 + n_smopts_cnt + vas_cnt + 4 + 1 + count(to) + 1;
   args = salloc(i * sizeof(char*));

   if((cp_v15compat = ok_vlook(sendmail_progname)) != NULL)
      n_OBSOLETE(_("please use *mta-argv0*, not *sendmail-progname*"));
   if((cp = ok_vlook(mta_argv0)) == NULL)
      cp = cp_v15compat;
   if(cp == NULL)
      cp = VAL_MTA_ARGV0;
   args[0] = cp/* TODO v15: = ok_vlook(mta_argv0) */;

   if ((snda = ok_blook(sendmail_no_default_arguments)))
      n_OBSOLETE(_("please use *mta-no-default-arguments*, "
         "not *sendmail-no-default-arguments*"));
   snda |= ok_blook(mta_no_default_arguments);
   if ((snda /* TODO v15: = ok_blook(mta_no_default_arguments)*/))
      i = 1;
   else {
      args[1] = "-i";
      i = 2;
      if (ok_blook(metoo))
         args[i++] = "-m";
      if (n_poption & n_PO_VERB)
         args[i++] = "-v";
   }

   for (j = 0; j < n_smopts_cnt; ++j, ++i)
      args[i] = n_smopts[j];

   for (j = 0; j < vas_cnt; ++j, ++i)
      args[i] = vas[j];

   /* -r option?  In conjunction with -t we act compatible to postfix(1) and
    * ignore it (it is -f / -F there) if the message specified From:/Sender:.
    * The interdependency with -t has been resolved in puthead() */
   if (!snda && ((n_poption & n_PO_r_FLAG) || ok_blook(r_option_implicit))) {
      struct name const *np;

      if (hp != NULL && (np = hp->h_from) != NULL) {
         /* However, what wasn't resolved there was the case that the message
          * specified multiple From: addresses and a Sender: */
         if ((n_psonce & n_PSO_t_FLAG) && hp->h_sender != NULL)
            np = hp->h_sender;

         if (np->n_fullextra != NULL) {
            args[i++] = "-F";
            args[i++] = np->n_fullextra;
         }
         cp = np->n_name;
      } else {
         assert(n_poption_arg_r == NULL);
         cp = skin(myorigin(NULL));
      }

      if (cp != NULL) {
         args[i++] = "-f";
         args[i++] = cp;
      }
   }

   /* Terminate option list to avoid false interpretation of system-wide
    * aliases that start with hyphen */
   if (!snda)
      args[i++] = "--";

   /* Receivers follow */
   for (; to != NULL; to = to->n_flink)
      if (!(to->n_type & GDEL))
         args[i++] = to->n_name;
   args[i] = NULL;

   if(vas != NULL)
      n_lofi_free(vas);
   NYD_LEAVE;
   return args;
}

static void
__mta_debug(struct sendbundle *sbp, char const *mta, char const **args)
{
   size_t cnt, bufsize, llen;
   char *buf;
   NYD_ENTER;

   n_err(_(">>> MTA: %s, arguments:"), n_shexp_quote_cp(mta, FAL0));
   for (; *args != NULL; ++args)
      n_err(" %s", n_shexp_quote_cp(*args, FAL0));
   n_err("\n");

   fflush_rewind(sbp->sb_input);

   cnt = fsize(sbp->sb_input);
   buf = NULL;
   bufsize = 0;
   while (fgetline(&buf, &bufsize, &cnt, &llen, sbp->sb_input, TRU1) != NULL) {
      buf[--llen] = '\0';
      n_err(">>> %s\n", buf);
   }
   if (buf != NULL)
      free(buf);
   NYD_LEAVE;
}

static char const *
a_sendout_random_id(struct header *hp, bool_t msgid)
{
   struct tm *tmp;
   char const *h;
   size_t rl, i;
   char *rv, sep;
   NYD_ENTER;

   rv = NULL;

   if(msgid && hp != NULL && hp->h_message_id != NULL){
      rv = hp->h_message_id->n_name;
      goto jleave;
   }

   if(ok_blook(message_id_disable))
      goto jleave;

   sep = '%';
   rl = 5;
   if((h = __sendout_ident) != NULL)
      goto jgen;
   if(ok_vlook(hostname) != NULL){
      h = nodename(1);
      sep = '@';
      rl = 8;
      goto jgen;
   }
   if(hp != NULL && (h = skin(myorigin(hp))) != NULL && strchr(h, '@') != NULL)
      goto jgen;
   goto jleave;

jgen:
   tmp = &time_current.tc_gm;
   i = sizeof("%04d%02d%02d%02d%02d%02d.%s%c%s") -1 + rl + strlen(h);
   rv = salloc(i +1);
   snprintf(rv, i, "%04d%02d%02d%02d%02d%02d.%s%c%s",
      tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
      tmp->tm_hour, tmp->tm_min, tmp->tm_sec,
      getrandstring(rl), sep, h);
   rv[i] = '\0'; /* Because we don't test snprintf(3) return */
jleave:
   NYD_LEAVE;
   return rv;
}

static int
fmt(char const *str, struct name *np, FILE *fo, enum fmt_flags ff)
{
   enum {
      m_INIT   = 1<<0,
      m_COMMA  = 1<<1,
      m_NOPF   = 1<<2,
      m_NONAME = 1<<3,
      m_CSEEN  = 1<<4
   } m = (ff & GCOMMA) ? m_COMMA : 0;
   ssize_t col, len;
   int rv = 1;
   NYD_ENTER;

   col = strlen(str);
   if (col) {
      fwrite(str, sizeof *str, col, fo);
#undef _X
#define _X(S)  (col == sizeof(S) -1 && !asccasecmp(str, S))
      if (ff & GFILES) {
         ;
      } else if (_X("reply-to:") || _X("mail-followup-to:") ||
            _X("references:") || _X("disposition-notification-to:"))
         m |= m_NOPF | m_NONAME;
      else if (ok_blook(add_file_recipients)) {
         ;
      } else if (_X("to:") || _X("cc:") || _X("bcc:") || _X("resent-to:"))
         m |= m_NOPF;
#undef _X
   }

   for (; np != NULL; np = np->n_flink) {
      if(np->n_type & GDEL)
         continue;
      if(is_addr_invalid(np,
               ((ff & FMT_INC_INVADDR ? 0 : EACM_NOLOG) |
                (m & m_NONAME ? EACM_NONAME : EACM_NONE))) &&
            !(ff & FMT_INC_INVADDR))
         continue;

      /* File and pipe addresses only printed with set *add-file-recipients* */
      if ((m & m_NOPF) && is_fileorpipe_addr(np))
         continue;

      if ((m & (m_INIT | m_COMMA)) == (m_INIT | m_COMMA)) {
         if (putc(',', fo) == EOF)
            goto jleave;
         m |= m_CSEEN;
         ++col;
      }

      len = strlen(np->n_fullname);
      if (np->n_type & GREF)
         len += 2;
      ++col; /* The separating space */
      if ((m & m_INIT) && /*col > 1 &&*/
            UICMP(z, col + len, >,
               (np->n_type & GREF ? MIME_LINELEN : 72))) {
         if (fputs("\n ", fo) == EOF)
            goto jleave;
         col = 1;
         m &= ~m_CSEEN;
      } else
         putc(' ', fo);
      m = (m & ~m_CSEEN) | m_INIT;

      /* C99 */{
         char *hb;

         /* GREF needs to be placed in angle brackets, but which are missing */
         hb = np->n_fullname;
         if(np->n_type & GREF){
            assert(UICMP(z, len, ==, strlen(np->n_fullname) + 2));
            hb = n_lofi_alloc(len +1);
            len -= 2;
            hb[0] = '<';
            hb[len + 1] = '>';
            hb[len + 2] = '\0';
            memcpy(&hb[1], np->n_fullname, len);
            len += 2;
         }
         len = xmime_write(hb, len, fo,
               ((ff & FMT_DOMIME) ? CONV_TOHDR_A : CONV_NONE), TD_ICONV);
         if(np->n_type & GREF)
            n_lofi_free(hb);
      }
      if (len < 0)
         goto jleave;
      col += len;
   }

   if (putc('\n', fo) != EOF)
      rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static int
infix_resend(FILE *fi, FILE *fo, struct message *mp, struct name *to,
   int add_resent)
{
   size_t cnt, c, bufsize = 0;
   char *buf = NULL;
   char const *cp;
   struct name *fromfield = NULL, *senderfield = NULL, *mdn;
   int rv = 1;
   NYD_ENTER;

   cnt = mp->m_size;

   /* Write the Resent-Fields */
   if (add_resent) {
      fputs("Resent-", fo);
      mkdate(fo, "Date");
      if ((cp = myaddrs(NULL)) != NULL) {
         if (_putname(cp, GCOMMA, SEND_MBOX, NULL, "Resent-From:", fo,
               &fromfield, 0))
            goto jleave;
      }
      /* TODO RFC 5322: Resent-Sender SHOULD NOT be used if it's EQ -From: */
      if ((cp = ok_vlook(sender)) != NULL) {
         if (_putname(cp, GCOMMA, SEND_MBOX, NULL, "Resent-Sender:", fo,
               &senderfield, 0))
            goto jleave;
      }
      if (fmt("Resent-To:", to, fo, FMT_COMMA))
         goto jleave;
      if (((cp = ok_vlook(stealthmua)) == NULL || !strcmp(cp, "noagent")) &&
            (cp = a_sendout_random_id(NULL, TRU1)) != NULL &&
            fprintf(fo, "Resent-Message-ID: <%s>\n", cp) < 0)
         goto jleave;
   }

   if ((mdn = n_UNCONST(check_from_and_sender(fromfield, senderfield))) == NULL)
      goto jleave;
   if (!_check_dispo_notif(mdn, NULL, fo))
      goto jleave;

   /* Write the original headers */
   while (cnt > 0) {
      if (fgetline(&buf, &bufsize, &cnt, &c, fi, 0) == NULL)
         break;
      if (ascncasecmp("status:", buf, 7) &&
            ascncasecmp("disposition-notification-to:", buf, 28) &&
            !is_head(buf, c, FAL0))
         fwrite(buf, sizeof *buf, c, fo);
      if (cnt > 0 && *buf == '\n')
         break;
   }

   /* Write the message body */
   while (cnt > 0) {
      if (fgetline(&buf, &bufsize, &cnt, &c, fi, 0) == NULL)
         break;
      if (cnt == 0 && *buf == '\n')
         break;
      fwrite(buf, sizeof *buf, c, fo);
   }
   if (buf != NULL)
      free(buf);
   if (ferror(fo)) {
      n_perr(_("temporary mail file"), 0);
      goto jleave;
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
mail(struct name *to, struct name *cc, struct name *bcc, char const *subject,
   struct attachment *attach, char const *quotefile, int recipient_record)
{
   struct header head;
   struct str in, out;
   NYD_ENTER;

   memset(&head, 0, sizeof head);

   /* The given subject may be in RFC1522 format. */
   if (subject != NULL) {
      in.s = n_UNCONST(subject);
      in.l = strlen(subject);
      mime_fromhdr(&in, &out, /* TODO ??? TD_ISPR |*/ TD_ICONV);
      head.h_subject = out.s;
   }
   head.h_to = to;
   head.h_cc = cc;
   head.h_bcc = bcc;
   head.h_attach = attach;

   mail1(&head, 0, NULL, quotefile, recipient_record, 0);

   if (subject != NULL)
      free(out.s);
   NYD_LEAVE;
   return 0;
}

FL int
c_sendmail(void *v)
{
   int rv;
   NYD_ENTER;

   rv = sendmail_internal(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Sendmail(void *v)
{
   int rv;
   NYD_ENTER;

   rv = sendmail_internal(v, 1);
   NYD_LEAVE;
   return rv;
}

FL enum okay
mail1(struct header *hp, int printheaders, struct message *quote,
   char const *quotefile, int recipient_record, int doprefix)
{
   struct n_sigman sm;
   struct sendbundle sb;
   struct name *to;
   bool_t dosign;
   FILE *mtf, *nmtf;
   enum okay rv;
   NYD_ENTER;

   _sendout_error = FAL0;
   __sendout_ident = NULL;
   rv = STOP;
   mtf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL) {
   case 0:
      break;
   default:
      goto jleave;
   }

   /* Update some globals we likely need first */
   time_current_update(&time_current, TRU1);

   /* Collect user's mail from standard input.  Get the result as mtf */
   mtf = collect(hp, printheaders, quote, quotefile, doprefix, &_sendout_error);
   if (mtf == NULL)
      goto jleave;

   dosign = TRUM1;

   /* */
   if(n_psonce & n_PSO_INTERACTIVE){
      if (ok_blook(asksign))
         dosign = getapproval(_("Sign this message"), TRU1);
   }

   if (fsize(mtf) == 0) {
      if (n_poption & n_PO_E_FLAG)
         goto jleave;
      if (hp->h_subject == NULL)
         fprintf(n_stdout, _("No message, no subject; hope that's ok\n"));
      else if (ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         fprintf(n_stdout, _("Null message body; hope that's ok\n"));
   }

   if (dosign == TRUM1)
      dosign = ok_blook(smime_sign); /* TODO USER@HOST <-> *from* +++!!! */
#ifndef HAVE_SSL
   if (dosign) {
      n_err(_("No SSL support compiled in\n"));
      goto jleave;
   }
#endif

   /* XXX Update time_current again; once collect() offers editing of more
    * XXX headers, including Date:, this must only happen if Date: is the
    * XXX same that it was before collect() (e.g., postponing etc.).
    * XXX But *do* update otherwise because the mail seems to be backdated
    * XXX if the user edited some time, which looks odd and it happened
    * XXX to me that i got mis-dated response mails due to that... */
   time_current_update(&time_current, TRU1);

   /* TODO hrmpf; the MIME/send layer rewrite MUST address the init crap:
    * TODO setup the header ONCE; note this affects edit.c, collect.c ...,
    * TODO but: offer a hook that rebuilds/expands/checks/fixates all
    * TODO header fields ONCE, call that ONCE after user editing etc. has
    * TODO completed (one edit cycle) */

   /* Take the user names from the combined to and cc lists and do all the
    * alias processing.  The POSIX standard says:
    *   The names shall be substituted when alias is used as a recipient
    *   address specified by the user in an outgoing message (that is,
    *   other recipients addressed indirectly through the reply command
    *   shall not be substituted in this manner).
    * S-nail thus violates POSIX, as has been pointed out correctly by
    * Martin Neitzel, but logic and usability of POSIX standards is not seldom
    * disputable anyway.  Go for user friendliness */

   to = namelist_vaporise_head(hp,
         (EACM_NORMAL |
          (!(expandaddr_to_eaf() & EAF_NAME) ? EACM_NONAME : EACM_NONE)),
         TRU1, &_sendout_error);
   if (to == NULL) {
      n_err(_("No recipients specified\n"));
      goto jfail_dead;
   }
   if (_sendout_error < 0) {
      n_err(_("Some addressees were classified as \"hard error\"\n"));
      goto jfail_dead;
   }

   /* */
   memset(&sb, 0, sizeof sb);
   sb.sb_hp = hp;
   sb.sb_to = to;
   sb.sb_input = mtf;
   if ((dosign || count_nonlocal(to) > 0) &&
         !_sendbundle_setup_creds(&sb, (dosign > 0)))
      /* TODO saving $DEAD and recovering etc is not yet well defined */
      goto jfail_dead;

   /* 'Bit ugly kind of control flow until we find a charset that does it */
   for (charset_iter_reset(hp->h_charset);; charset_iter_next()) {
      int err;

      if (!charset_iter_is_valid())
         ;
      else if ((nmtf = infix(hp, mtf)) != NULL)
         break;
      else if ((err = errno) == EILSEQ || err == EINVAL) {
         rewind(mtf);
         continue;
      }

      n_perr(_("Failed to create encoded message"), 0);
      goto jfail_dead;
   }
   mtf = nmtf;

   /*  */
#ifdef HAVE_SSL
   if (dosign) {
      if ((nmtf = smime_sign(mtf, sb.sb_signer.s)) == NULL)
         goto jfail_dead;
      Fclose(mtf);
      mtf = nmtf;
   }
#endif

   /* TODO truly - i still don't get what follows: (1) we deliver file
    * TODO and pipe addressees, (2) we mightrecord() and (3) we transfer
    * TODO even if (1) savedeadletter() etc.  To me this doesn't make sense? */

   /* Deliver pipe and file addressees */
   to = a_sendout_file_a_pipe(to, mtf, &_sendout_error);
   if (_sendout_error)
      savedeadletter(mtf, FAL0);

   to = elide(to); /* XXX only to drop GDELs due a_sendout_file_a_pipe()! */

   {  ui32_t cnt = count(to);
      if ((!recipient_record || cnt > 0) &&
            !mightrecord(mtf, (recipient_record ? to : NULL), FAL0))
         goto jleave;
      if (cnt > 0) {
         sb.sb_hp = hp;
         sb.sb_to = to;
         sb.sb_input = mtf;
         if (_transfer(&sb))
            rv = OKAY;
      } else if (!_sendout_error)
         rv = OKAY;
   }

   n_sigman_cleanup_ping(&sm);
jleave:
   if(mtf != NULL){
      Fclose(mtf);
      temporary_compose_mode_hook_unroll();
   }
   if (_sendout_error)
      n_exit_status |= n_EXIT_SEND_ERROR;
   NYD_LEAVE;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;

jfail_dead:
   _sendout_error = TRU1;
   savedeadletter(mtf, TRU1);
   n_err(_("... message not sent\n"));
   goto jleave;
}

FL int
mkdate(FILE *fo, char const *field)
{
   struct tm tmpgm, *tmptr;
   int tzdiff, tzdiff_hour, tzdiff_min, rv;
   NYD_ENTER;

   memcpy(&tmpgm, &time_current.tc_gm, sizeof tmpgm);
   tzdiff = time_current.tc_time - mktime(&tmpgm);
   tzdiff_hour = (int)(tzdiff / 60);
   tzdiff_min = tzdiff_hour % 60;
   tzdiff_hour /= 60;
   tmptr = &time_current.tc_local;
   if (tmptr->tm_isdst > 0)
      ++tzdiff_hour;
   rv = fprintf(fo, "%s: %s, %02d %s %04d %02d:%02d:%02d %+05d\n",
         field,
         n_weekday_names[tmptr->tm_wday],
         tmptr->tm_mday, n_month_names[tmptr->tm_mon],
         tmptr->tm_year + 1900, tmptr->tm_hour,
         tmptr->tm_min, tmptr->tm_sec,
         tzdiff_hour * 100 + tzdiff_min);
   NYD_LEAVE;
   return rv;
}

FL int
puthead(bool_t nosend_msg, struct header *hp, FILE *fo, enum gfield w,
   enum sendaction action, enum conversion convert, char const *contenttype,
   char const *charset)
{
#define FMT_CC_AND_BCC()   \
do {\
   if ((w & GCC) && (hp->h_cc != NULL || nosend_msg == TRUM1)) {\
      if (fmt("Cc:", hp->h_cc, fo, ff))\
         goto jleave;\
      ++gotcha;\
   }\
   if ((w & GBCC) && (hp->h_bcc != NULL || nosend_msg == TRUM1)) {\
      if (fmt("Bcc:", hp->h_bcc, fo, ff))\
         goto jleave;\
      ++gotcha;\
   }\
} while (0)

   char const *addr;
   size_t gotcha;
   struct name *np, *fromasender = NULL;
   int stealthmua, rv = 1;
   bool_t nodisp;
   enum fmt_flags ff;
   NYD_ENTER;

   if ((addr = ok_vlook(stealthmua)) != NULL)
      stealthmua = !strcmp(addr, "noagent") ? -1 : 1;
   else
      stealthmua = 0;
   gotcha = 0;
   nodisp = (action != SEND_TODISP);
   ff = (w & (GCOMMA | GFILES)) | (nodisp ? FMT_DOMIME : 0);
   if(nosend_msg)
      ff |= FMT_INC_INVADDR;

   if (w & GDATE)
      mkdate(fo, "Date"), ++gotcha;
   if (w & GIDENT) {
      if (hp->h_from == NULL || hp->h_sender == NULL)
         setup_from_and_sender(hp);

      if (hp->h_from != NULL) {
         if (fmt("From:", hp->h_from, fo, ff))
            goto jleave;
         ++gotcha;
      }

      if (hp->h_sender != NULL) {
         if (fmt("Sender:", hp->h_sender, fo, ff))
            goto jleave;
         ++gotcha;
      }

      fromasender = n_UNCONST(check_from_and_sender(hp->h_from, hp->h_sender));
      if (fromasender == NULL)
         goto jleave;
      /* Note that fromasender is (NULL,) 0x1 or real sender here */
   }

#if 1
   if ((w & GTO) && (hp->h_to != NULL || nosend_msg == TRUM1)) {
      if (fmt("To:", hp->h_to, fo, ff))
         goto jleave;
      ++gotcha;
   }
#else
   /* TODO Thought about undisclosed recipients:;, but would be such a fake
    * TODO given that we cannot handle group addresses.  Ridiculous */
   if (w & GTO) {
      struct name *xto;

      if ((xto = hp->h_to) != NULL) {
         char const ud[] = "To: Undisclosed recipients:;\n" /* TODO groups */;

         if (count_nonlocal(xto) != 0 || ok_blook(add_file_recipients) ||
               (hp->h_cc != NULL && count_nonlocal(hp->h_cc) > 0))
            goto jto_fmt;
         if (fwrite(ud, 1, sizeof(ud) -1, fo) != sizeof(ud) -1)
            goto jleave;
         ++gotcha;
      } else if (nosend_msg == TRUM1) {
jto_fmt:
         if (fmt("To:", hp->h_to, fo, ff))
            goto jleave;
         ++gotcha;
      }
   }
#endif

   if (!ok_blook(bsdcompat) && !ok_blook(bsdorder))
      FMT_CC_AND_BCC();

   if ((w & GSUBJECT) && (hp->h_subject != NULL || nosend_msg == TRUM1)) {
      if (fwrite("Subject: ", sizeof(char), 9, fo) != 9)
         goto jleave;
      if (hp->h_subject != NULL) {
         size_t sublen;
         char const *sub;

         sublen = strlen(sub = subject_re_trim(hp->h_subject));

         /* Trimmed something, (re-)add Re: */
         if (sub != hp->h_subject) {
            if (fwrite("Re: ", 1, 4, fo) != 4) /* RFC mandates eng. "Re: " */
               goto jleave;
            if (sublen > 0 &&
                  xmime_write(sub, sublen, fo,
                     (!nodisp ? CONV_NONE : CONV_TOHDR),
                     (!nodisp ? TD_ISPR | TD_ICONV : TD_ICONV)) < 0)
               goto jleave;
         }
         /* This may be, e.g., a Fwd: XXX yes, unfortunately we do like that */
         else if (*sub != '\0') {
            if (xmime_write(sub, sublen, fo, (!nodisp ? CONV_NONE : CONV_TOHDR),
                  (!nodisp ? TD_ISPR | TD_ICONV : TD_ICONV)) < 0)
               goto jleave;
         }
      }
      ++gotcha;
      putc('\n', fo);
   }

   if (ok_blook(bsdcompat) || ok_blook(bsdorder))
      FMT_CC_AND_BCC();

   if ((w & GMSGID) && stealthmua <= 0 &&
         (addr = a_sendout_random_id(hp, TRU1)) != NULL) {
      if (fprintf(fo, "Message-ID: <%s>\n", addr) < 0)
         goto jleave;
      ++gotcha;
   }

   if ((np = hp->h_ref) != NULL && (w & GREF)) {
      if (fmt("References:", np, fo, 0))
         goto jleave;
      if (hp->h_in_reply_to == NULL && np->n_name != NULL) {
         while (np->n_flink != NULL)
            np = np->n_flink;
         if (!is_addr_invalid(np, /* TODO check that on parser side! */
               /*EACM_STRICT | TODO '/' valid!! */ EACM_NOLOG | EACM_NONAME)) {
            if (fprintf(fo,
                  "In-Reply-To: <%s>\n", np->n_name /*TODO RFC5322 3.6.4*/) < 0)
               goto jleave;
            ++gotcha;
         } else {
            n_err(_("Invalid address in mail header: %s\n"), np->n_name);
            goto jleave;
         }
      }
   }
   if ((np = hp->h_in_reply_to) != NULL && (w & GREF)) {
      if (fprintf(fo,
            "In-Reply-To: <%s>\n", np->n_name /*TODO RFC 5322 3.6.4*/) < 0)
         goto jleave;
      ++gotcha;
   }

   if (w & GIDENT) {
      /* Reply-To:.  Be careful not to destroy a possible user input, duplicate
       * the list first.. TODO it is a terrible codebase.. */
      if ((np = hp->h_replyto) != NULL)
         np = namelist_dup(np, np->n_type);
      else if ((addr = ok_vlook(replyto)) != NULL)
         np = lextract(addr, GEXTRA | GFULL);
      if (np != NULL &&
            (np = elide(
               checkaddrs(usermap(np, TRU1), EACM_STRICT | EACM_NOLOG,
                  NULL))) != NULL) {
         if (fmt("Reply-To:", np, fo, ff))
            goto jleave;
         ++gotcha;
      }
   }

   if ((w & GIDENT) && !nosend_msg) {
      /* Mail-Followup-To: TODO factor out this huge block of code */
      /* Place ourselfs in there if any non-subscribed list is an addressee */
      if ((hp->h_flags & HF_LIST_REPLY) || hp->h_mft != NULL ||
            ok_blook(followup_to)) {
         enum {_ANYLIST=1<<(HF__NEXT_SHIFT+0), _HADMFT=1<<(HF__NEXT_SHIFT+1)};

         ui32_t f = hp->h_flags | (hp->h_mft != NULL ? _HADMFT : 0);
         struct name *mft, *x;

         /* But for that, we have to remove all incarnations of ourselfs first.
          * TODO It is total crap that we have delete_alternates(), is_myname()
          * TODO or whatever; these work only with variables, not with data
          * TODO that is _currently_ in some header fields!!!  v15.0: complete
          * TODO rewrite, object based, lazy evaluated, on-the-fly marked.
          * TODO then this should be a really cheap thing in here... */
         np = elide(delete_alternates(cat(
               namelist_dup(hp->h_to, GEXTRA | GFULL),
               namelist_dup(hp->h_cc, GEXTRA | GFULL))));
         addr = hp->h_list_post;

         for (mft = NULL; (x = np) != NULL;) {
            si8_t ml;
            np = np->n_flink;

            if ((ml = is_mlist(x->n_name, FAL0)) == MLIST_OTHER &&
                  addr != NULL && !asccasecmp(addr, x->n_name))
               ml = MLIST_KNOWN;

            /* Any non-subscribed list?  Add ourselves */
            switch (ml) {
            case MLIST_KNOWN:
               f |= HF_MFT_SENDER;
               /* FALLTHRU */
            case MLIST_SUBSCRIBED:
               f |= _ANYLIST;
               goto j_mft_add;
            case MLIST_OTHER:
               if (!(f & HF_LIST_REPLY)) {
j_mft_add:
                  if (!is_addr_invalid(x,
                        EACM_STRICT | EACM_NOLOG | EACM_NONAME)) {
                     x->n_flink = mft;
                     mft = x;
                  } /* XXX write some warning?  if verbose?? */
                  continue;
               }
               /* And if this is a reply that honoured a MFT: header then we'll
                * also add all members of the original MFT: that are still
                * addressed by us, regardless of all other circumstances */
               else if (f & _HADMFT) {
                  struct name *ox;
                  for (ox = hp->h_mft; ox != NULL; ox = ox->n_flink)
                     if (!asccasecmp(ox->n_name, x->n_name))
                        goto j_mft_add;
               }
               break;
            }
         }

         if (f & (_ANYLIST | _HADMFT) && mft != NULL) {
            if (((f & HF_MFT_SENDER) ||
                  ((f & (_ANYLIST | _HADMFT)) == _HADMFT)) &&
                  (np = fromasender) != NULL && np != (struct name*)0x1) {
               np = ndup(np, (np->n_type & ~GMASK) | GEXTRA | GFULL);
               np->n_flink = mft;
               mft = np;
            }

            if (fmt("Mail-Followup-To:", mft, fo, ff))
               goto jleave;
            ++gotcha;
         }
      }

      if (!_check_dispo_notif(fromasender, hp, fo))
         goto jleave;
   }

   if ((w & GUA) && stealthmua == 0) {
      if (fprintf(fo, "User-Agent: %s %s\n", n_uagent, ok_vlook(version)) < 0)
         goto jleave;
      ++gotcha;
   }

   /* The user may have placed headers when editing */
   if(1){
      struct n_header_field *hfp;

      if((hfp = hp->h_user_headers) != NULL){
         if(!_sendout_header_list(fo, hfp, nodisp))
            goto jleave;
         ++gotcha;
      }
   }

   /* Custom headers, as via *customhdr* */
   if(!nosend_msg){
      struct n_header_field *hfp;

      /* With iconv support we likely have a cached result */
      if((hfp = hp->h_custom_headers) == NULL)
         hp->h_custom_headers = hfp = n_customhdr_query();
      if(hfp != NULL){
         if(!_sendout_header_list(fo, hfp, nodisp))
            goto jleave;
         ++gotcha;
      }
   }

   /* We don't need MIME unless.. we need MIME?! */
   if ((w & GMIME) && ((n_pstate & n_PS_HEADER_NEEDED_MIME) ||
         hp->h_attach != NULL ||
         ((n_poption & n_PO_Mm_FLAG) && n_poption_arg_Mm != NULL) ||
         convert != CONV_7BIT || asccasecmp(charset, "US-ASCII"))) {
      ++gotcha;
      if (fputs("MIME-Version: 1.0\n", fo) == EOF)
         goto jleave;
      if (hp->h_attach != NULL) {
         _sendout_boundary = mime_param_boundary_create();/*TODO carrier*/
         if (fprintf(fo,
               "Content-Type: multipart/mixed;\n boundary=\"%s\"\n",
               _sendout_boundary) < 0)
            goto jleave;
      } else {
         if (_put_ct(fo, contenttype, charset) < 0 || _put_cte(fo, convert) < 0)
            goto jleave;
      }
   }

   if (gotcha && (w & GNL))
      if (putc('\n', fo) == EOF)
         goto jleave;
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
#undef FMT_CC_AND_BCC
}

FL enum okay
resend_msg(struct message *mp, struct name *to, int add_resent) /* TODO check */
{
   struct sendbundle sb;
   FILE *ibuf, *nfo, *nfi;
   char *tempMail;
   enum okay rv = STOP;
   NYD_ENTER;

   _sendout_error = FAL0;
   __sendout_ident = NULL;

   /* Update some globals we likely need first */
   time_current_update(&time_current, TRU1);

   if ((to = checkaddrs(to,
         (EACM_NORMAL |
          (!(expandaddr_to_eaf() & EAF_NAME) ? EACM_NONAME : EACM_NONE)),
         &_sendout_error)) == NULL)
      goto jleave;
   /* For the _sendout_error<0 case we want to wait until we can write DEAD! */
   if (_sendout_error < 0)
      n_err(_("Some addressees were classified as \"hard error\"\n"));

   if ((nfo = Ftmp(&tempMail, "resend", OF_WRONLY | OF_HOLDSIGS | OF_REGISTER))
         == NULL) {
      _sendout_error = TRU1;
      n_perr(_("temporary mail file"), 0);
      goto jleave;
   }
   if ((nfi = Fopen(tempMail, "r")) == NULL)
      n_perr(tempMail, 0);
   Ftmp_release(&tempMail);
   if (nfi == NULL)
      goto jerr_o;

   if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL)
      goto jerr_io;

   memset(&sb, 0, sizeof sb);
   sb.sb_to = to;
   sb.sb_input = nfi;
   if (count_nonlocal(to) > 0 && !_sendbundle_setup_creds(&sb, FAL0))
      /* ..wait until we can write DEAD */
      _sendout_error = -1;

   if (infix_resend(ibuf, nfo, mp, to, add_resent) != 0) {
jfail_dead:
      savedeadletter(nfi, TRU1);
      n_err(_("... message not sent\n"));
jerr_io:
      Fclose(nfi);
jerr_o:
      Fclose(nfo);
      _sendout_error = TRU1;
      goto jleave;
   }

   if (_sendout_error < 0)
      goto jfail_dead;

   Fclose(nfo);
   rewind(nfi);

   to = a_sendout_file_a_pipe(to, nfi, &_sendout_error);

   if (_sendout_error)
      savedeadletter(nfi, FAL0);

   if (count(to = elide(to)) != 0) {
      if (!ok_blook(record_resent) || mightrecord(nfi, NULL, TRU1)) {
         sb.sb_to = to;
         /*sb.sb_input = nfi;*/
         if (_transfer(&sb))
            rv = OKAY;
      }
   } else if (!_sendout_error)
      rv = OKAY;

   Fclose(nfi);
jleave:
   if (_sendout_error)
      n_exit_status |= n_EXIT_SEND_ERROR;
   NYD_LEAVE;
   return rv;
}

FL void
savedeadletter(FILE *fp, bool_t fflush_rewind_first){
   struct n_string line;
   int c;
   enum {a_NONE, a_INIT = 1<<0, a_BODY = 1<<1, a_NL = 1<<2} flags;
   ul_i bytes, lines;
   FILE *dbuf;
   char const *cp, *cpq;
   NYD_ENTER;

   if(!ok_blook(save))
      goto jleave;

   if(fflush_rewind_first){
      fflush(fp);
      rewind(fp);
   }
   if(fsize(fp) == 0)
      goto jleave;

   cp = n_getdeadletter();
   cpq = n_shexp_quote_cp(cp, FAL0);

   if(n_poption & n_PO_DEBUG){
      n_err(_(">>> Would (try to) write $DEAD %s\n"), cpq);
      goto jleave;
   }

   if((dbuf = Fopen(cp, "w")) == NULL){
      n_perr(_("Cannot save to $DEAD"), 0);
      goto jleave;
   }
   n_file_lock(fileno(dbuf), FLT_WRITE, 0,0, UIZ_MAX); /* XXX Natomic */

   fprintf(n_stdout, "%s ", cpq);
   fflush(n_stdout);

   /* TODO savedeadletter() non-conforming: should check whether we have any
    * TODO headers, if not we need to place "something", anything will do.
    * TODO MIME is completely missing, we use MBOXO quoting!!  Yuck.
    * TODO I/O error handling missing.  Yuck! */
   n_string_reserve(n_string_creat_auto(&line), 2 * SEND_LINESIZE);
   bytes = (ul_i)fprintf(dbuf, "From %s %s",
         ok_vlook(LOGNAME), time_current.tc_ctime);
   lines = 1;
   for(flags = a_NONE, c = '\0'; c != EOF; bytes += line.s_len, ++lines){
      n_string_trunc(&line, 0);
      while((c = getc(fp)) != EOF && c != '\n')
         n_string_push_c(&line, c);

      /* TODO It may be that we have only some plain text.  It may be that we
       * TODO have a complete MIME encoded message.  We don't know, and we
       * TODO have no usable mechanism to dig it!!  tWe need v15! */
      if(!(flags & a_INIT)){
         size_t i;

         /* Throw away leading empty lines! */
         if(line.s_len == 0)
            continue;
         for(i = 0; i < line.s_len; ++i){
            if(fieldnamechar(line.s_dat[i]))
               continue;
            if(line.s_dat[i] == ':'){
               flags |= a_INIT;
               break;
            }else{
               /* We have no headers, this is already a body line! */
               flags |= a_INIT | a_BODY;
               break;
            }
         }
         /* Well, i had to check whether the RFC allows this.  Assume we've
          * passed the headers, too, then! */
         if(i == line.s_len)
            flags |= a_INIT | a_BODY;
      }
      if(flags & a_BODY){
         if(line.s_len >= 5 && !memcmp(line.s_dat, "From ", 5))
            n_string_unshift_c(&line, '>');
      }
      if(line.s_len == 0)
         flags |= a_BODY | a_NL;
      else
         flags &= ~a_NL;

      n_string_push_c(&line, '\n');
      fwrite(line.s_dat, sizeof *line.s_dat, line.s_len, dbuf);
   }
   if(!(flags & a_NL)){
      putc('\n', dbuf);
      ++bytes;
      ++lines;
   }
   n_string_gut(&line);

   Fclose(dbuf);
   fprintf(n_stdout, "%lu/%lu\n", lines, bytes);
   fflush(n_stdout);

   rewind(fp);
jleave:
   NYD_LEAVE;
}

#undef SEND_LINESIZE

/* s-it-mode */
