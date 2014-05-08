/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Mail to others.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <fcntl.h>

#undef SEND_LINESIZE
#define SEND_LINESIZE \
   ((1024 / B64_ENCODE_INPUT_PER_LINE) * B64_ENCODE_INPUT_PER_LINE)

static char *  _sendout_boundary;
static bool_t  _sendout_error;

static enum okay     _putname(char const *line, enum gfield w,
                        enum sendaction action, size_t *gotcha,
                        char const *prefix, FILE *fo, struct name **xp);

/* Get an encoding flag based on the given string */
static char const *  _get_encoding(const enum conversion convert);

/* Write an attachment to the file buffer, converting to MIME */
static int           _attach_file(struct attachment *ap, FILE *fo);
static int           __attach_file(struct attachment *ap, FILE *fo);

/* There are non-local receivers, collect credentials etc. */
static bool_t        _sendbundle_setup_creds(struct sendbundle *sbpm,
                        bool_t signing_caps);

/* Prepare arguments for the MTA (non *smtp* mode) */
static char const ** _prepare_mta_args(struct name *to, struct header *hp);

/* Fix the header by glopping all of the expanded names from the distribution
 * list into the appropriate fields */
static struct name * fixhead(struct header *hp, struct name *tolist);

/* Put the signature file at fo. TODO layer rewrite: *integrate in body*!! */
static int           put_signature(FILE *fo, int convert);

/* Attach a message to the file buffer */
static int           attach_message(struct attachment *ap, FILE *fo);

/* Generate the body of a MIME multipart message */
static int           make_multipart(struct header *hp, int convert, FILE *fi,
                        FILE *fo, char const *contenttype, char const *charset);

/* Prepend a header in front of the collected stuff and return the new file */
static FILE *        infix(struct header *hp, FILE *fi);

/* Save the outgoing mail on the passed file */
static int           savemail(char const *name, FILE *fi);

/* Send mail to a bunch of user names.  The interface is through mail() */
static int           sendmail_internal(void *v, int recipient_record);

/*  */
static bool_t        _transfer(struct sendbundle *sbp);

/* Start the MTA mailing */
static bool_t        start_mta(struct sendbundle *sbp);

/* Record outgoing mail if instructed to do so; in *record* unless to is set */
static bool_t        mightrecord(FILE *fp, struct name *to);

/* Create a Message-Id: header field.  Use either host name or from address */
static void          message_id(FILE *fo, struct header *hp);

/* Format the given header line to not exceed 72 characters */
static int           fmt(char const *str, struct name *np, FILE *fo, int comma,
                        int dropinvalid, int domime);

/* Rewrite a message for resending, adding the Resent-Headers */
static int           infix_resend(FILE *fi, FILE *fo, struct message *mp,
                        struct name *to, int add_resent);

static enum okay
_putname(char const *line, enum gfield w, enum sendaction action,
   size_t *gotcha, char const *prefix, FILE *fo, struct name **xp)
{
   struct name *np;
   enum okay rv = STOP;
   NYD_ENTER;

   np = lextract(line, GEXTRA | GFULL);
   if (xp != NULL)
      *xp = np;
   if (np == NULL)
      ;
   else if (fmt(prefix, np, fo, w & GCOMMA, 0, (action != SEND_TODISP)))
      rv = OKAY;
   else if (gotcha != NULL)
      ++(*gotcha);
   NYD_LEAVE;
   return rv;
}

static char const *
_get_encoding(enum conversion const convert)
{
   char const *rv;
   NYD_ENTER;

   switch (convert) {
   case CONV_7BIT:   rv = "7bit"; break;
   case CONV_8BIT:   rv = "8bit"; break;
   case CONV_TOQP:   rv = "quoted-printable"; break;
   case CONV_TOB64:  rv = "base64"; break;
   default:          rv = NULL; break;
   }
   NYD_LEAVE;
   return rv;
}

static int
_attach_file(struct attachment *ap, FILE *fo)
{
   /* TODO of course, the MIME classification needs to performed once
    * TODO only, not for each and every charset anew ... ;-// */
   char *charset_iter_orig[2];
   long offs;
   int err = 0;
   NYD_ENTER;

   /* Is this already in target charset?  Simply copy over */
   if (ap->a_conv == AC_TMPFILE) {
      err = __attach_file(ap, fo);
      Fclose(ap->a_tmpf);
      DBG( ap->a_tmpf = NULL; )
      goto jleave;
   }

   /* If we don't apply charset conversion at all (fixed input=ouput charset)
    * we also simply copy over, since it's the users desire */
   if (ap->a_conv == AC_FIX_INCS) {
      ap->a_charset = ap->a_input_charset;
      err = __attach_file(ap, fo);
      goto jleave;
   }

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
      err = __attach_file(ap, fo);
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
__attach_file(struct attachment *ap, FILE *fo) /* XXX linelength */
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
   } else if ((fi = Fopen(ap->a_name, "r")) == NULL) {
      err = errno;
      perror(ap->a_name);
      goto jleave;
   }

   /* MIME part header for attachment */
   {  char const *bn = ap->a_name, *ct;

      if ((ct = strrchr(bn, '/')) != NULL)
         bn = ++ct;
      ct = ap->a_content_type;
      charset = ap->a_charset;
      convert = mime_classify_file(fi, (char const**)&ct, &charset, &do_iconv);
      if (charset == NULL || ap->a_conv == AC_FIX_INCS ||
            ap->a_conv == AC_TMPFILE)
         do_iconv = 0;

      if (fprintf(fo, "\n--%s\nContent-Type: %s", _sendout_boundary, ct) == -1)
         goto jerr_header;

      if (charset == NULL) {
         if (putc('\n', fo) == EOF)
            goto jerr_header;
      } else if (fprintf(fo, "; charset=%s\n", charset) == -1)
         goto jerr_header;

      if (fprintf(fo, "Content-Transfer-Encoding: %s\n"
            "Content-Disposition: %s;\n filename=\"",
            _get_encoding(convert), ap->a_content_disposition) == -1)
         goto jerr_header;
      if (xmime_write(bn, strlen(bn), fo, CONV_TOHDR, TD_NONE, NULL) < 0)
         goto jerr_header;
      if (fwrite("\"\n", sizeof(char), 2, fo) != 2 * sizeof(char))
         goto jerr_header;

      if ((bn = ap->a_content_id) != NULL &&
            fprintf(fo, "Content-ID: %s\n", bn) == -1)
         goto jerr_header;

      if ((bn = ap->a_content_description) != NULL &&
            fprintf(fo, "Content-Description: %s\n", bn) == -1)
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
      char const *tcs = charset_get_lc();
      if (asccasecmp(charset, tcs) &&
            (iconvd = n_iconv_open(charset, tcs)) == (iconv_t)-1 &&
            (err = errno) != 0) {
         if (err == EINVAL)
            fprintf(stderr, tr(179, "Cannot convert from %s to %s\n"),
               tcs, charset);
         else
            perror("iconv_open");
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
      if (xmime_write(buf, inlen, fo, convert, TD_ICONV, NULL) < 0) {
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
   char *shost, *from, *smtp;
   NYD_ENTER;

   v15 = ok_blook(v15_compat);
   shost = (v15 ? ok_vlook(smtp_hostname) : NULL);
   from = ((signing_caps || !v15 || shost == NULL)
         ? skin(myorigin(sbp->sb_hp)) : NULL);

   if (signing_caps) {
      if (from == NULL) {
#ifdef HAVE_SSL
         fprintf(stderr, tr(531, "No *from* address for signing specified\n"));
         goto jleave;
#endif
      } else
         sbp->sb_signer.l = strlen(sbp->sb_signer.s = from);
   }

   if ((smtp = ok_vlook(smtp)) == NULL) {
      rv = TRU1;
      goto jleave;
   }

   if (!url_parse(&sbp->sb_url, CPROTO_SMTP, smtp))
      goto jleave;

   if (v15) {
      if (shost == NULL) {
         assert(from != NULL);
         sbp->sb_url.url_uh.l = strlen(sbp->sb_url.url_uh.s = from);
      }
      if (!ccred_lookup(&sbp->sb_ccred, &sbp->sb_url))
         goto jleave;
   } else {
      if (sbp->sb_url.url_had_user || sbp->sb_url.url_pass.s != NULL) {
         fprintf(stderr, "New-style URL used without *v15-compat* being set\n");
         goto jleave;
      }
      if (!ccred_lookup_old(&sbp->sb_ccred, CPROTO_SMTP, smtp))
         goto jleave;
      assert(from != NULL);
      sbp->sb_url.url_uh.l = strlen(sbp->sb_url.url_uh.s = from);
   }

   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

static char const **
_prepare_mta_args(struct name *to, struct header *hp)
{
   size_t j, i;
   char const **args;
   NYD_ENTER;

   i = 4 + smopts_count + 2 + count(to) + 1;
   args = salloc(i * sizeof(char*));

   args[0] = ok_vlook(sendmail_progname);
   if (args[0] == NULL || *args[0] == '\0')
      args[0] = SENDMAIL_PROGNAME;

   args[1] = "-i";
   i = 2;
   if (ok_blook(metoo))
      args[i++] = "-m";
   if (options & OPT_VERBOSE)
      args[i++] = "-v";

   for (j = 0; j < smopts_count; ++j, ++i)
      args[i] = smopts[j];

   /* -r option?  We may only pass skinned addresses */
   if (options & OPT_r_FLAG) {
      char const *froma;

      if (option_r_arg[0] != '\0')
         froma = option_r_arg;
      else if (hp != NULL) {
         /* puthead() did it, then */
         assert(hp->h_from != NULL);
         froma = hp->h_from->n_name;
      } else
         froma = skin(myorigin(NULL)); /* XXX ugh! ugh!! */
      if (froma != NULL) { /* XXX ugh! */
         args[i++] = "-r";
         args[i++] = froma;
      }
   }

   /* Receivers follow */
   for (; to != NULL; to = to->n_flink)
      if (!(to->n_type & GDEL))
         args[i++] = to->n_name;
   args[i] = NULL;
   NYD_LEAVE;
   return args;
}

static struct name *
fixhead(struct header *hp, struct name *tolist)
{
   struct name **npp, *np;
   NYD_ENTER;

   tolist = elide(tolist);

   hp->h_to = hp->h_cc = hp->h_bcc = NULL;
   for (np = tolist; np != NULL; np = np->n_flink) {
      switch (np->n_type & (GDEL | GMASK)) {
      case GTO:   npp = &hp->h_to; break;
      case GCC:   npp = &hp->h_cc; break;
      case GBCC:  npp = &hp->h_bcc; break;
      default:    continue;
      }
      *npp = cat(*npp, ndup(np, np->n_type | GFULL));
   }
   NYD_LEAVE;
   return tolist;
}

static int
put_signature(FILE *fo, int convert)
{
   char buf[SEND_LINESIZE], *sig, c = '\n';
   FILE *fsig;
   size_t sz;
   int rv;
   NYD_ENTER;

   if ((sig = ok_vlook(signature)) == NULL || *sig == '\0') {
      rv = 0;
      goto jleave;
   }
   rv = -1;

   if ((sig = file_expand(sig)) == NULL)
      goto jleave;

   if ((fsig = Fopen(sig, "r")) == NULL) {
      perror(sig);
      goto jleave;
   }
   while ((sz = fread(buf, sizeof *buf, SEND_LINESIZE, fsig)) != 0) {
      c = buf[sz - 1];
      if (xmime_write(buf, sz, fo, convert, TD_NONE, NULL) < 0)
         goto jerr;
   }
   if (ferror(fsig)) {
jerr:
      perror(sig);
      Fclose(fsig);
      goto jleave;
   }
   Fclose(fsig);
   if (c != '\n')
      putc('\n', fo);

   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static int
attach_message(struct attachment *ap, FILE *fo)
{
   struct message *mp;
   char const *ccp;
   int rv;
   NYD_ENTER;

   fprintf(fo, "\n--%s\nContent-Type: message/rfc822\n"
       "Content-Disposition: inline\n", _sendout_boundary);
   if ((ccp = ap->a_content_description) != NULL)
      fprintf(fo, "Content-Description: %s\n", ccp);
   fputc('\n', fo);

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
      char *buf;
      size_t sz, bufsize, cnt;

      fprintf(fo, "\n--%s\n", _sendout_boundary);
      fprintf(fo, "Content-Type: %s", contenttype);
      if (charset != NULL)
         fprintf(fo, "; charset=%s", charset);
      fprintf(fo, "\nContent-Transfer-Encoding: %s\n"
         "Content-Disposition: inline\n\n", _get_encoding(convert));

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

         if (xmime_write(buf, sz, fo, convert, TD_ICONV, NULL) < 0) {
            free(buf);
            goto jleave;
         }
      }
      free(buf);

      if (ferror(fi))
         goto jleave;
      if (charset != NULL)
         put_signature(fo, convert);
   }

   for (att = hp->h_attach; att != NULL; att = att->a_flink) {
      if (att->a_msgno) {
         if (attach_message(att, fo) != 0)
            goto jleave;
      } else if (_attach_file(att, fo) != 0)
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

   if ((nfo = Ftmp(&tempMail, "infix", OF_WRONLY | OF_HOLDSIGS | OF_REGISTER,
         0600)) == NULL) {
      perror(tr(178, "temporary mail file"));
      goto jleave;
   }
   if ((nfi = Fopen(tempMail, "r")) == NULL) {
      perror(tempMail);
      Fclose(nfo);
   }
   Ftmp_release(&tempMail);
   if (nfi == NULL)
      goto jleave;

   contenttype = "text/plain"; /* XXX mail body - always text/plain, want XX? */
   convert = mime_classify_file(fi, &contenttype, &charset, &do_iconv);

#ifdef HAVE_ICONV
   tcs = charset_get_lc();
   if ((convhdr = need_hdrconv(hp, GTO | GSUBJECT | GCC | GBCC | GIDENT))) {
      if (iconvd != (iconv_t)-1) /* XXX  */
         n_iconv_close(iconvd);
      if (asccasecmp(convhdr, tcs) != 0 &&
            (iconvd = n_iconv_open(convhdr, tcs)) == (iconv_t)-1 &&
            (err = errno) != 0)
         goto jiconv_err;
   }
#endif
   if (puthead(hp, nfo,
         (GTO | GSUBJECT | GCC | GBCC | GNL | GCOMMA | GUA | GMIME | GMSGID |
         GIDENT | GREF | GDATE), SEND_MBOX, convert, contenttype, charset))
      goto jerr;
#ifdef HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
#endif

#ifdef HAVE_ICONV
   if (do_iconv && charset != NULL) { /*TODO charset->mime_classify_file*/
      if (asccasecmp(charset, tcs) != 0 &&
            (iconvd = n_iconv_open(charset, tcs)) == (iconv_t)-1 &&
            (err = errno) != 0) {
jiconv_err:
         if (err == EINVAL)
            fprintf(stderr, tr(179, "Cannot convert from %s to %s\n"),
               tcs, charset);
         else
            perror("iconv_open");
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
         if (xmime_write(buf, sz, nfo, convert, TD_ICONV, NULL) < 0) {
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
      if (charset != NULL)
         put_signature(nfo, convert); /* XXX if (text/) !! */
   }

#ifdef HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
#endif

   fflush(nfo);
   if ((err = ferror(nfo)))
      perror(tr(180, "temporary mail file"));
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

static int
savemail(char const *name, FILE *fi)
{
   FILE *fo;
   char *buf;
   size_t bufsize, buflen, cnt;
   int prependnl = 0, rv = -1;
   NYD_ENTER;

   buf = smalloc(bufsize = LINESIZE);

   if ((fo = Zopen(name, "a+", NULL)) == NULL) {
      if ((fo = Zopen(name, "wx", NULL)) == NULL) {
         perror(name);
         goto jleave;
      }
   } else {
      if (fseek(fo, -2L, SEEK_END) == 0) {
         switch (fread(buf, sizeof *buf, 2, fo)) {
         case 2:
            if (buf[1] != '\n') {
               prependnl = 1;
               break;
            }
            /* FALLTHRU */
         case 1:
            if (buf[0] != '\n')
               prependnl = 1;
            break;
         default:
            if (ferror(fo)) {
               perror(name);
               goto jleave;
            }
         }
         if (prependnl) {
            putc('\n', fo);
         }
         fflush(fo);
      }
   }

   fprintf(fo, "From %s %s", myname, time_current.tc_ctime);
   fflush_rewind(fi);
   cnt = fsize(fi);
   buflen = 0;
   while (fgetline(&buf, &bufsize, &cnt, &buflen, fi, 0) != NULL) {
#ifdef HAVE_DEBUG /* TODO assert legacy */
      assert(!is_head(buf, buflen));
#else
      if (is_head(buf, buflen))
         putc('>', fo);
#endif
      fwrite(buf, sizeof *buf, buflen, fo);
   }
   if (buflen && *(buf + buflen - 1) != '\n')
      putc('\n', fo);
   putc('\n', fo);
   fflush(fo);

   rv = 0;
   if (ferror(fo)) {
      perror(name);
      rv = -1;
   }
   if (Fclose(fo) != 0)
      rv = -1;
   fflush_rewind(fi);
jleave:
   free(buf);
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
      char const k[] = "smime-encrypt-";
      size_t nl = strlen(np->n_name);
      char *cp, *vs = ac_alloc(sizeof(k)-1 + nl +1);
      memcpy(vs, k, sizeof(k) -1);
      memcpy(vs + sizeof(k) -1, np->n_name, nl +1);

      if ((cp = vok_vlook(vs)) != NULL) {
#ifdef HAVE_SSL
         FILE *ef;

         if ((ef = smime_encrypt(sbp->sb_input, cp, np->n_name)) != NULL) {
            FILE *fisave = sbp->sb_input;
            struct name *nsave = sbp->sb_to;

            sbp->sb_to = ndup(np, np->n_type & ~(GFULL | GSKIN));
            sbp->sb_input = ef;
            if (!start_mta(sbp))
               rv = FAL0;
            sbp->sb_to = nsave;
            sbp->sb_input = fisave;

            Fclose(ef);
         } else {
#else
            fprintf(stderr, tr(225, "No SSL support compiled in.\n"));
            rv = FAL0;
#endif
            fprintf(stderr, tr(38, "Message not sent to <%s>\n"), np->n_name);
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

   if (cnt > 0 && (ok_blook(smime_force_encryption) || !start_mta(sbp)))
      rv = FAL0;
   NYD_LEAVE;
   return rv;
}

static bool_t
start_mta(struct sendbundle *sbp)
{
   char const **args = NULL, **t, *mta;
   char *smtp;
   pid_t pid;
   sigset_t nset;
   bool_t rv = FAL0;
   NYD_ENTER;

   if ((smtp = ok_vlook(smtp)) == NULL) {
      if ((mta = ok_vlook(sendmail)) != NULL) {
         if ((mta = file_expand(mta)) == NULL)
            goto jstop;
      } else
         mta = SENDMAIL;

      args = _prepare_mta_args(sbp->sb_to, sbp->sb_hp);
      if (options & OPT_DEBUG) {
         printf(tr(181, "Sendmail arguments:"));
         for (t = args; *t != NULL; ++t)
            printf(" \"%s\"", *t);
         printf("\n");
         rv = TRU1;
         goto jleave;
      }
   } else {
      mta = NULL; /* Silence cc */
#ifndef HAVE_SMTP
      fputs(tr(194, "No SMTP support compiled in.\n"), stderr);
      goto jstop;
#else
      /* XXX assert that sendbundle is setup? */
#endif
   }

   /* Fork, set up the temporary mail file as standard input for "mail", and
    * exec with the user list we generated far above */
   if ((pid = fork()) == -1) {
      perror("fork");
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
      freopen("/dev/null", "r", stdin);
#ifdef HAVE_SMTP
      if (smtp != NULL) {
         prepare_child(&nset, 0, 1);
         if (smtp_mta(sbp))
            _exit(0);
      } else {
#endif
         prepare_child(&nset, fileno(sbp->sb_input), -1);
         /* If *record* is set then savemail() will move the file position;
          * it'll call rewind(), but that may optimize away the systemcall if
          * possible, and since dup2() shares the position with the original FD
          * the MTA may end up reading nothing */
         lseek(0, 0, SEEK_SET);
         execv(mta, UNCONST(args));
         perror(mta);
#ifdef HAVE_SMTP
      }
#endif
      savedeadletter(sbp->sb_input, 1);
      fputs(tr(182, "... message not sent.\n"), stderr);
      _exit(1);
   }
   if ((options & (OPT_DEBUG | OPT_VERBOSE | OPT_BATCH_FLAG)) ||
         ok_blook(sendwait)) {
      if (wait_child(pid, NULL))
         rv = TRU1;
      else
         _sendout_error = TRU1;
   } else {
      rv = TRU1;
      free_child(pid);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
mightrecord(FILE *fp, struct name *to)
{
   char *cp, *cq;
   char const *ep;
   bool_t rv = TRU1;
   NYD_ENTER;

   if (to != NULL) {
      cp = savestr(skinned_name(to));
      for (cq = cp; *cq != '\0' && *cq != '@'; ++cq)
         ;
      *cq = '\0';
   } else
      cp = ok_vlook(record);

   if (cp != NULL) {
      if ((ep = expand(cp)) == NULL) {
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
         if ((ep = file_expand(cp)) == NULL) {
            ep = "NULL";
            goto jbail;
         }
      }

      if (savemail(ep, fp) != 0) {
jbail:
         fprintf(stderr, tr(285, "Failed to save message in %s - "
            "message not sent\n"), ep);
         exit_status |= EXIT_ERR;
         savedeadletter(fp, 1);
         rv = FAL0;
      }
   }
   NYD_LEAVE;
   return rv;
}

static void
message_id(FILE *fo, struct header *hp)
{
   char const *h;
   size_t rl;
   struct tm *tmp;
   NYD_ENTER;

   if (ok_blook(message_id_disable))
      goto jleave;

   if ((h = ok_vlook(hostname)) != NULL)
      rl = 24;
   else if ((h = skin(myorigin(hp))) != NULL && strchr(h, '@') != NULL)
      rl = 16;
   else
      /* Up to MTA */
      goto jleave;

   tmp = &time_current.tc_gm;
   fprintf(fo, "Message-ID: <%04d%02d%02d%02d%02d%02d.%s%c%s>\n",
      tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
      tmp->tm_hour, tmp->tm_min, tmp->tm_sec,
      getrandstring(rl), (rl == 16 ? '%' : '@'), h);
jleave:
   NYD_LEAVE;
}

static int
fmt(char const *str, struct name *np, FILE *fo, int flags, int dropinvalid,
   int domime)
{
   enum {
      m_INIT   = 1<<0,
      m_COMMA  = 1<<1,
      m_NOPF   = 1<<2,
      m_CSEEN  = 1<<3
   } m = (flags & GCOMMA) ? m_COMMA : 0;
   ssize_t col, len;
   int rv = 1;
   NYD_ENTER;

   col = strlen(str);
   if (col) {
      fwrite(str, sizeof *str, col, fo);
      if (flags & GFILES)
         goto jstep;
      if (col == 9 && !asccasecmp(str, "reply-to:")) {
         m |= m_NOPF;
         goto jstep;
      }
      if (ok_blook(add_file_recipients))
         goto jstep;
      if ((col == 3 && (!asccasecmp(str, "to:") || !asccasecmp(str, "cc:"))) ||
            (col == 4 && !asccasecmp(str, "bcc:")) ||
            (col == 10 && !asccasecmp(str, "Resent-To:")))
         m |= m_NOPF;
   }
jstep:
   for (; np != NULL; np = np->n_flink) {
      if ((m & m_NOPF) && is_fileorpipe_addr(np))
         continue;
      if (is_addr_invalid(np, !dropinvalid)) {
         if (dropinvalid)
            continue;
         else
            goto jleave;
      }
      if ((m & (m_INIT | m_COMMA)) == (m_INIT | m_COMMA)) {
         putc(',', fo);
         m |= m_CSEEN;
         ++col;
      }
      len = strlen(np->n_fullname);
      ++col; /* The separating space */
      if ((m & m_INIT) && col > 1 && col + len > 72) {
         fputs("\n ", fo);
         col = 1;
         m &= ~m_CSEEN;
      } else
         putc(' ', fo);
      m = (m & ~m_CSEEN) | m_INIT;
      len = xmime_write(np->n_fullname, len, fo,
            (domime ? CONV_TOHDR_A : CONV_NONE), TD_ICONV, NULL);
      if (len < 0)
         goto jleave;
      col += len;
   }
   putc('\n', fo);
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
   struct name *fromfield = NULL, *senderfield = NULL;
   int rv = 1;
   NYD_ENTER;

   cnt = mp->m_size;

   /* Write the Resent-Fields */
   if (add_resent) {
      fputs("Resent-", fo);
      mkdate(fo, "Date");
      if ((cp = myaddrs(NULL)) != NULL) {
         if (_putname(cp, GCOMMA, SEND_MBOX, NULL, "Resent-From:", fo,
               &fromfield))
            goto jleave;
      }
      if ((cp = ok_vlook(sender)) != NULL) {
         if (_putname(cp, GCOMMA, SEND_MBOX, NULL, "Resent-Sender:", fo,
               &senderfield))
            goto jleave;
      }
      if (fmt("Resent-To:", to, fo, 1, 1, 0))
         goto jleave;
      if ((cp = ok_vlook(stealthmua)) == NULL || !strcmp(cp, "noagent")) {
         fputs("Resent-", fo);
         message_id(fo, NULL);
      }
   }
   if (check_from_and_sender(fromfield, senderfield))
      goto jleave;

   /* Write the original headers */
   while (cnt > 0) {
      if (fgetline(&buf, &bufsize, &cnt, &c, fi, 0) == NULL)
         break;
      /* XXX more checks: The From_ line may be seen when resending */
      /* During headers is_head() is actually overkill, so ^From_ is sufficient
       * && !is_head(buf, c) */
      if (ascncasecmp("status: ", buf, 8) && strncmp("From ", buf, 5))
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
      perror(tr(188, "temporary mail file"));
      goto jleave;
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
mail(struct name *to, struct name *cc, struct name *bcc, char *subject,
   struct attachment *attach, char *quotefile, int recipient_record)
{
   struct header head;
   struct str in, out;
   NYD_ENTER;

   memset(&head, 0, sizeof head);

   /* The given subject may be in RFC1522 format. */
   if (subject != NULL) {
      in.s = subject;
      in.l = strlen(subject);
      mime_fromhdr(&in, &out, /* TODO ??? TD_ISPR |*/ TD_ICONV);
      head.h_subject = out.s;
   }
   if (!(options & OPT_t_FLAG)) {
      head.h_to = to;
      head.h_cc = cc;
      head.h_bcc = bcc;
   }
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
   char *quotefile, int recipient_record, int doprefix)
{
   struct sendbundle sb;
   struct name *to;
   FILE *mtf, *nmtf;
   int dosign = -1, err;
   char const *cp;
   enum okay rv = STOP;
   NYD_ENTER;

   _sendout_error = FAL0;

   /* Update some globals we likely need first */
   time_current_update(&time_current, TRU1);

   /*  */
   if ((cp = ok_vlook(autocc)) != NULL && *cp != '\0')
      hp->h_cc = cat(hp->h_cc, checkaddrs(lextract(cp, GCC | GFULL)));
   if ((cp = ok_vlook(autobcc)) != NULL && *cp != '\0')
      hp->h_bcc = cat(hp->h_bcc, checkaddrs(lextract(cp, GBCC | GFULL)));

   /* Collect user's mail from standard input.  Get the result as mtf */
   mtf = collect(hp, printheaders, quote, quotefile, doprefix);
   if (mtf == NULL)
      goto j_leave;

   if (options & OPT_INTERACTIVE) {
      err = (ok_blook(bsdcompat) || ok_blook(askatend));
      if (err == 0)
         goto jaskeot;
      if (ok_blook(askcc))
         ++err, grab_headers(hp, GCC, 1);
      if (ok_blook(askbcc))
         ++err, grab_headers(hp, GBCC, 1);
      if (ok_blook(askattach))
         ++err, edit_attachments(&hp->h_attach);
      if (ok_blook(asksign))
         ++err, dosign = getapproval(tr(35, "Sign this message (y/n)? "), TRU1);
      if (err == 1) {
jaskeot:
         printf(tr(183, "EOT\n"));
         fflush(stdout);
      }
   }

   if (fsize(mtf) == 0) {
      if (options & OPT_E_FLAG)
         goto jleave;
      if (hp->h_subject == NULL)
         printf(tr(184, "No message, no subject; hope that's ok\n"));
      else if (ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         printf(tr(185, "Null message body; hope that's ok\n"));
   }

   if (dosign < 0)
      dosign = ok_blook(smime_sign);
#ifndef HAVE_SSL
   if (dosign) {
      fprintf(stderr, tr(225, "No SSL support compiled in.\n"));
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

   /* Do alias expansion on Reply-To: members, too */
   /* TODO puthead() YET (!!! see ONCE note above) expands the value, but
    * TODO doesn't perform alias expansion; encapsulate in the ONCE-o */
   if (hp->h_replyto == NULL && (cp = ok_vlook(replyto)) != NULL)
      hp->h_replyto = checkaddrs(lextract(cp, GEXTRA | GFULL));
   if (hp->h_replyto != NULL)
      hp->h_replyto = elide(usermap(hp->h_replyto, TRU1));

   /* TODO what happens now is that all recipients are merged into
    * TODO a duplicated list with expanded aliases, then this list is
    * TODO splitted again into the three individual recipient lists (with
    * TODO duplicates removed).
    * TODO later on we use the merged list for outof() pipe/file saving,
    * TODO then we eliminate duplicates (again) and then we use that one
    * TODO for mightrecord() and _transfer(), and again.  ... Please ... */

   /* NOTE: Due to elide() in fixhead(), ENSURE to,cc,bcc order of to!,
    * because otherwise the recipients will be "degraded" if they occur
    * multiple times */
   to = usermap(cat(hp->h_to, cat(hp->h_cc, hp->h_bcc)), FAL0);
   if (to == NULL) {
      fprintf(stderr, tr(186, "No recipients specified\n"));
      _sendout_error = TRU1;
   }
   to = fixhead(hp, to);

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
      if (!charset_iter_is_valid())
         ;
      else if ((nmtf = infix(hp, mtf)) != NULL)
         break;
      else if ((err = errno) == EILSEQ || err == EINVAL) {
         rewind(mtf);
         continue;
      }

      perror("");
jfail_dead:
      _sendout_error = TRU1;
      savedeadletter(mtf, TRU1);
      fputs(tr(182, "... message not sent.\n"), stderr);
      goto jleave;
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
   to = outof(to, mtf, &_sendout_error);
   if (_sendout_error)
      savedeadletter(mtf, FAL0);

   to = elide(to); /* XXX needed only to drop GDELs due to outof()! */
   {  ui32_t cnt = count(to);
      if ((!recipient_record || cnt > 0) &&
            !mightrecord(mtf, (recipient_record ? to : NULL)))
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
jleave:
   Fclose(mtf);
j_leave:
   if (_sendout_error)
      exit_status |= EXIT_SEND_ERROR;
   NYD_LEAVE;
   return rv;
}

FL int
mkdate(FILE *fo, char const *field)
{
   struct tm *tmptr;
   int tzdiff, tzdiff_hour, tzdiff_min, rv;
   NYD_ENTER;

   tzdiff = time_current.tc_time - mktime(&time_current.tc_gm);
   tzdiff_hour = (int)(tzdiff / 60);
   tzdiff_min = tzdiff_hour % 60;
   tzdiff_hour /= 60;
   tmptr = &time_current.tc_local;
   if (tmptr->tm_isdst > 0)
      ++tzdiff_hour;
   rv = fprintf(fo, "%s: %s, %02d %s %04d %02d:%02d:%02d %+05d\n",
         field,
         weekday_names[tmptr->tm_wday],
         tmptr->tm_mday, month_names[tmptr->tm_mon],
         tmptr->tm_year + 1900, tmptr->tm_hour,
         tmptr->tm_min, tmptr->tm_sec,
         tzdiff_hour * 100 + tzdiff_min);
   NYD_LEAVE;
   return rv;
}

FL int
puthead(struct header *hp, FILE *fo, enum gfield w, enum sendaction action,
   enum conversion convert, char const *contenttype, char const *charset)
{
#define FMT_CC_AND_BCC()   \
do {\
   if (hp->h_cc != NULL && (w & GCC)) {\
      if (fmt("Cc:", hp->h_cc, fo, (w & (GCOMMA | GFILES)), 0,\
            (action != SEND_TODISP)))\
         goto jleave;\
      ++gotcha;\
   }\
   if (hp->h_bcc != NULL && (w & GBCC)) {\
      if (fmt("Bcc:", hp->h_bcc, fo, (w & (GCOMMA | GFILES)), 0,\
            (action != SEND_TODISP)))\
         goto jleave;\
      ++gotcha;\
   }\
} while (0)

   char const *addr;
   size_t gotcha, l;
   struct name *np, *fromfield = NULL, *senderfield = NULL;
   int stealthmua, rv = 1;
   bool_t nodisp;
   NYD_ENTER;

   if ((addr = ok_vlook(stealthmua)) != NULL)
      stealthmua = !strcmp(addr, "noagent") ? -1 : 1;
   else
      stealthmua = 0;
   gotcha = 0;
   nodisp = (action != SEND_TODISP);

   if (w & GDATE)
      mkdate(fo, "Date"), ++gotcha;
   if (w & GIDENT) {
      if (hp->h_from != NULL) {
         if (fmt("From:", hp->h_from, fo, (w & (GCOMMA | GFILES)), 0, nodisp))
            goto jleave;
         ++gotcha;
         fromfield = hp->h_from;
      } else if ((addr = myaddrs(hp)) != NULL) {
         if (_putname(addr, w, action, &gotcha, "From:", fo, &fromfield))
            goto jleave;
         hp->h_from = fromfield;
      }

      if (((addr = hp->h_organization) != NULL ||
            (addr = ok_vlook(ORGANIZATION)) != NULL) &&
            (l = strlen(addr)) > 0) {
         fwrite("Organization: ", sizeof(char), 14, fo);
         if (xmime_write(addr, l, fo, (!nodisp ? CONV_NONE : CONV_TOHDR),
               (!nodisp ? TD_ISPR | TD_ICONV : TD_ICONV), NULL) < 0)
            goto jleave;
         ++gotcha;
         putc('\n', fo);
      }

      /* TODO see the ONCE TODO note somewhere around this file;
       * TODO but anyway, do NOT perform alias expansion UNLESS
       * TODO we are actually sending out! */
      if (hp->h_replyto != NULL) {
         if (fmt("Reply-To:", hp->h_replyto, fo, w & GCOMMA, 0, nodisp))
            goto jleave;
         ++gotcha;
      } else if ((addr = ok_vlook(replyto)) != NULL)
         if (_putname(addr, w, action, &gotcha, "Reply-To:", fo, NULL))
            goto jleave;

      if (hp->h_sender != NULL) {
         if (fmt("Sender:", hp->h_sender, fo, w & GCOMMA, 0, nodisp))
            goto jleave;
         ++gotcha;
         senderfield = hp->h_sender;
      } else if ((addr = ok_vlook(sender)) != NULL)
         if (_putname(addr, w, action, &gotcha, "Sender:", fo, &senderfield))
            goto jleave;

      if (check_from_and_sender(fromfield, senderfield))
         goto jleave;
   }

   if (hp->h_to != NULL && w & GTO) {
      if (fmt("To:", hp->h_to, fo, (w & (GCOMMA | GFILES)), 0, nodisp))
         goto jleave;
      ++gotcha;
   }

   if (!ok_blook(bsdcompat) && !ok_blook(bsdorder))
      FMT_CC_AND_BCC();

   if (hp->h_subject != NULL && (w & GSUBJECT)) {
      fwrite("Subject: ", sizeof (char), 9, fo);
      if (!ascncasecmp(hp->h_subject, "re: ", 4)) {/* TODO localizable */
         fwrite("Re: ", sizeof(char), 4, fo);
         if (strlen(hp->h_subject + 4) > 0 &&
               xmime_write(hp->h_subject + 4, strlen(hp->h_subject + 4), fo,
                  (!nodisp ? CONV_NONE : CONV_TOHDR),
                  (!nodisp ? TD_ISPR | TD_ICONV : TD_ICONV), NULL) < 0)
            goto jleave;
      } else if (*hp->h_subject != '\0') {
         if (xmime_write(hp->h_subject, strlen(hp->h_subject), fo,
               (!nodisp ? CONV_NONE : CONV_TOHDR),
               (!nodisp ? TD_ISPR | TD_ICONV : TD_ICONV), NULL) < 0)
            goto jleave;
      }
      ++gotcha;
      fwrite("\n", sizeof (char), 1, fo);
   }

   if (ok_blook(bsdcompat) || ok_blook(bsdorder))
      FMT_CC_AND_BCC();

   if ((w & GMSGID) && stealthmua <= 0)
      message_id(fo, hp), ++gotcha;

   if ((np = hp->h_ref) != NULL && (w & GREF)) {
      fmt("References:", np, fo, 0, 1, 0);
      if (np->n_name != NULL) {
         while (np->n_flink != NULL)
            np = np->n_flink;
         if (!is_addr_invalid(np, 0)) {
            fprintf(fo, "In-Reply-To: %s\n", np->n_name);
            ++gotcha;
         }
      }
   }

   if ((w & GUA) && stealthmua == 0)
      fprintf(fo, "User-Agent: %s %s\n", uagent, version), ++gotcha;

   if (w & GMIME) {
      fputs("MIME-Version: 1.0\n", fo), ++gotcha;
      if (hp->h_attach != NULL) {
         _sendout_boundary = mime_create_boundary();/*TODO carrier*/
         fprintf(fo, "Content-Type: multipart/mixed;\n boundary=\"%s\"\n",
            _sendout_boundary);
      } else {
         fprintf(fo, "Content-Type: %s", contenttype);
         if (charset != NULL)
            fprintf(fo, "; charset=%s", charset);
         fprintf(fo, "\nContent-Transfer-Encoding: %s\n",
            _get_encoding(convert));
      }
   }

   if (gotcha && (w & GNL))
      putc('\n', fo);
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

   /* Update some globals we likely need first */
   time_current_update(&time_current, TRU1);

   if ((to = checkaddrs(to)) == NULL) {
      _sendout_error = TRU1;
      goto jleave;
   }

   if ((nfo = Ftmp(&tempMail, "resend", OF_WRONLY | OF_HOLDSIGS | OF_REGISTER,
         0600)) == NULL) {
      _sendout_error = TRU1;
      perror(tr(189, "temporary mail file"));
      goto jleave;
   }
   if ((nfi = Fopen(tempMail, "r")) == NULL) {
      _sendout_error = TRU1;
      perror(tempMail);
   }
   Ftmp_release(&tempMail);
   if (nfi == NULL)
      goto jerr_o;

   if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL)
      goto jerr_all;

   memset(&sb, 0, sizeof sb);
   sb.sb_to = to;
   sb.sb_input = nfi;
   if (count_nonlocal(to) > 0 && !_sendbundle_setup_creds(&sb, FAL0))
      /* TODO saving $DEAD and recovering etc is not yet well defined */
      goto jerr_all;

   if (infix_resend(ibuf, nfo, mp, to, add_resent) != 0) {
      savedeadletter(nfi, TRU1);
      fputs(tr(182, "... message not sent.\n"), stderr);
jerr_all:
      Fclose(nfi);
jerr_o:
      Fclose(nfo);
      _sendout_error = TRU1;
      goto jleave;
   }
   Fclose(nfo);
   rewind(nfi);

   to = outof(to, nfi, &_sendout_error);
   if (_sendout_error)
      savedeadletter(nfi, FAL0);

   to = elide(to); /* TODO should have been done in fixhead()? */
   if (count(to) != 0) {
      if (!ok_blook(record_resent) || mightrecord(nfi, to)) {
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
      exit_status |= EXIT_SEND_ERROR;
   NYD_LEAVE;
   return rv;
}

#undef SEND_LINESIZE

/* vim:set fenc=utf-8:s-it-mode */
