/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message sending lifecycle, header composing, etc.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE sendout
#define mx_SOURCE
#define mx_SOURCE_SENDOUT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/time.h>

#include "mx/attachments.h"
#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/cmd-mlist.h"
#include "mx/compat.h"
#include "mx/cred-auth.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/iconv.h"
#include "mx/mime.h"
#include "mx/mime-enc.h"
#include "mx/mime-param.h"
#include "mx/mime-type.h"
#include "mx/names.h"
#include "mx/net-smtp.h"
#include "mx/privacy.h"
#include "mx/random.h"
#include "mx/sigs.h"
#include "mx/tty.h"
#include "mx/url.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#undef SEND_LINESIZE
#define SEND_LINESIZE \
   ((1024 / mx_B64_ENC_INPUT_PER_LINE) * mx_B64_ENC_INPUT_PER_LINE)

enum a_sendout_addrline_flags{
   a_SENDOUT_AL_INC_INVADDR = 1<<0, /* _Do_ include invalid addresses */
   a_SENDOUT_AL_DOMIME = 1<<1,      /* Perform MIME conversion */
   a_SENDOUT_AL_COMMA = GCOMMA,
   a_SENDOUT_AL_FILES = GFILES,
   _a_SENDOUT_AL_GMASK = a_SENDOUT_AL_COMMA | a_SENDOUT_AL_FILES
};
CTA(!(_a_SENDOUT_AL_GMASK & (a_SENDOUT_AL_INC_INVADDR|a_SENDOUT_AL_DOMIME)),
   "Code-required condition not satisfied but actual bit carrier value");

enum a_sendout_sendwait_flags{
   a_SENDOUT_SWF_NONE,
   a_SENDOUT_SWF_MTA = 1u<<0,
   a_SENDOUT_SWF_PCC = 1u<<1,
   a_SENDOUT_SWF_MASK = a_SENDOUT_SWF_MTA | a_SENDOUT_SWF_PCC
};

static char const *__sendout_ident; /* TODO temporary; rewrite n_puthead() */
static char *  _sendout_boundary;
static s8   _sendout_error;

/* */
static u32 a_sendout_sendwait_to_swf(void);

/* *fullnames* appears after command line arguments have been parsed */
static struct mx_name *a_sendout_fullnames_cleanup(struct mx_name *np);

/* */
static boole a_sendout_put_name(char const *line, enum gfield w,
      enum sendaction action, char const *prefix, FILE *fo,
      struct mx_name **xp);

/* Place Content-Type:, Content-Transfer-Encoding:, Content-Disposition:
 * headers, respectively */
static int a_sendout_put_ct(FILE *fo, char const *contenttype,
               char const *charset);
su_SINLINE int a_sendout_put_cte(FILE *fo, enum conversion conv);
static int a_sendout_put_cd(FILE *fo, char const *cd, char const *filename);

/* Put all entries of the given header list */
static boole        _sendout_header_list(FILE *fo, struct n_header_field *hfp,
                        boole nodisp);

/* */
static s32 a_sendout_body(FILE *fo, FILE *fi, enum conversion convert);

/* Write an attachment to the file buffer, converting to MIME */
static s32 a_sendout_attach_file(struct header *hp, struct mx_attachment *ap,
      FILE *fo, boole force);
static s32 a_sendout__attach_file(struct header *hp, struct mx_attachment *ap,
      FILE *f, boole force);

/* There are non-local receivers, collect credentials etc. */
static boole a_sendout_setup_creds(struct mx_send_ctx *scp, boole sign_caps);

/* Attach a message to the file buffer */
static s32 a_sendout_attach_msg(struct header *hp, struct mx_attachment *ap,
               FILE *fo);

/* Generate the body of a MIME multipart message */
static s32 make_multipart(struct header *hp, int convert, FILE *fi,
   FILE *fo, char const *contenttype, char const *charset, boole force);

/* Prepend a header in front of the collected stuff and return the new file */
static FILE *a_sendout_infix(struct header *hp, FILE *fi, boole dosign,
      boole force);

/* Check whether Disposition-Notification-To: is desired */
static boole        _check_dispo_notif(struct mx_name *mdn, struct header *hp,
                        FILE *fo);

/* Send mail to a bunch of user names.  The interface is through mail() */
static int a_sendout_sendmail(void *v, enum n_mailsend_flags msf);

/* Deal with file and pipe addressees */
static struct mx_name *a_sendout_file_a_pipe(struct mx_name *names, FILE *fo,
                     boole *senderror);

/* Record outgoing mail if instructed to do so; in *record* unless to is set */
static boole a_sendout_mightrecord(FILE *fp, struct mx_name *to, boole resend);

static boole a_sendout__savemail(char const *name, FILE *fp, boole resend);

/* Move a message over to non-local recipients (via MTA) */
static boole a_sendout_transfer(struct mx_send_ctx *scp, boole resent,
      boole *senderror);

/* Actual MTA interaction */
static boole a_sendout_mta_start(struct mx_send_ctx *scp);
static char const **a_sendout_mta_file_args(struct mx_name *to,
      struct header *hp);
static void a_sendout_mta_file_debug(struct mx_send_ctx *scp, char const *mta,
      char const **args);
static boole a_sendout_mta_test(struct mx_send_ctx *scp, char const *mta);

/* Create a Message-ID: header field.  Use either host name or from address */
static char const *a_sendout_random_id(struct header *hp, boole msgid);

/* Format the given header line to not exceed 72 characters */
static boole a_sendout_put_addrline(char const *hname, struct mx_name *np,
               FILE *fo, enum a_sendout_addrline_flags saf);

/* Rewrite a message for resending, adding the Resent-Headers */
static boole a_sendout_infix_resend(FILE *fi, FILE *fo, struct message *mp,
      struct mx_name *to, int add_resent);

static u32
a_sendout_sendwait_to_swf(void){ /* TODO should happen at var assign time */
   char *buf;
   u32 rv;
   char const *cp;
   NYD2_IN;

   if((cp = ok_vlook(sendwait)) == NIL)
      rv = a_SENDOUT_SWF_NONE;
   else if(*cp == '\0')
      rv = a_SENDOUT_SWF_MASK;
   else{
      rv = a_SENDOUT_SWF_NONE;

      for(buf = savestr(cp); (cp = su_cs_sep_c(&buf, ',', TRU1)) != NIL;){
         if(!su_cs_cmp_case(cp, "mta"))
            rv |= a_SENDOUT_SWF_MTA;
         else if(!su_cs_cmp_case(cp, "pcc"))
            rv |= a_SENDOUT_SWF_PCC;
         else if(n_poption & n_PO_D_V)
            n_err(_("Unknown *sendwait* content: %s\n"),
               n_shexp_quote_cp(cp, FAL0));
      }
   }
   NYD2_OU;
   return rv;
}

static struct mx_name *
a_sendout_fullnames_cleanup(struct mx_name *np){
   struct mx_name *xp;
   NYD2_IN;

   for(xp = np; xp != NULL; xp = xp->n_flink){
      xp->n_type &= ~(GFULL | GFULLEXTRA);
      xp->n_fullname = xp->n_name;
      xp->n_fullextra = NULL;
   }
   NYD2_OU;
   return np;
}

static boole
a_sendout_put_name(char const *line, enum gfield w, enum sendaction action,
   char const *prefix, FILE *fo, struct mx_name **xp){
   boole rv;
   struct mx_name *np;
   NYD_IN;

   np = (w & GNOT_A_LIST ? n_extract_single : lextract)(line, GEXTRA | GFULL);
   if(xp != NULL)
      *xp = np;

   if(np == NULL)
      rv = FAL0;
   else
      rv = a_sendout_put_addrline(prefix, np, fo, ((w & GCOMMA) |
            ((action != SEND_TODISP) ? a_SENDOUT_AL_DOMIME : 0)));
   NYD_OU;
   return rv;
}

static int
a_sendout_put_ct(FILE *fo, char const *contenttype, char const *charset){
   int rv;
   NYD2_IN;

   if((rv = fprintf(fo, "Content-Type: %s", contenttype)) < 0)
      goto jerr;

   if(charset == NULL)
      goto jend;

   if(putc(';', fo) == EOF)
      goto jerr;
   ++rv;

   if(su_cs_len(contenttype) + sizeof("Content-Type: ;")-1 > 50){
      if(putc('\n', fo) == EOF)
         goto jerr;
      ++rv;
   }

   /* C99 */{
      int i;

      i = fprintf(fo, " charset=%s", charset);
      if(i < 0)
         goto jerr;
      rv += i;
   }

jend:
   if(putc('\n', fo) == EOF)
      goto jerr;
   ++rv;
jleave:
   NYD2_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

su_SINLINE int
a_sendout_put_cte(FILE *fo, enum conversion conv){
   int rv;
   NYD2_IN;

   /* RFC 2045, 6.1.:
    *    This is the default value -- that is,
    *    "Content-Transfer-Encoding: 7BIT" is assumed if the
    *     Content-Transfer-Encoding header field is not present.
    */
   rv = (conv == CONV_7BIT) ? 0
         : fprintf(fo, "Content-Transfer-Encoding: %s\n",
            mx_mime_enc_from_conversion(conv));
   NYD2_OU;
   return rv;
}

static int
a_sendout_put_cd(FILE *fo, char const *cd, char const *filename){
   struct str f;
   s8 mpc;
   int rv;
   NYD2_IN;

   f.s = NULL;

   /* xxx Ugly with the trailing space in case of wrap! */
   if((rv = fprintf(fo, "Content-Disposition: %s; ", cd)) < 0)
      goto jerr;

   if(!(mpc = mx_mime_param_create(&f, "filename", filename)))
      goto jerr;
   /* Always fold if result contains newlines */
   if(mpc < 0 || f.l + rv > MIME_LINELEN) { /* FIXME MIME_LINELEN_MAX */
      if(putc('\n', fo) == EOF || putc(' ', fo) == EOF)
         goto jerr;
      rv += 2;
   }
   if(fputs(f.s, fo) == EOF || putc('\n', fo) == EOF)
      goto jerr;
   rv += (int)++f.l;

jleave:
   NYD2_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;

}

static boole
_sendout_header_list(FILE *fo, struct n_header_field *hfp, boole nodisp){
   boole rv;
   NYD2_IN;

   for(rv = TRU1; hfp != NULL; hfp = hfp->hf_next)
      if(fwrite(hfp->hf_dat, sizeof(char), hfp->hf_nl, fo) != hfp->hf_nl ||
            putc(':', fo) == EOF || putc(' ', fo) == EOF ||
            mx_xmime_write(hfp->hf_dat + hfp->hf_nl +1, hfp->hf_bl, fo,
               (!nodisp ? CONV_NONE : CONV_TOHDR),
               (!nodisp ? mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT
                : mx_MIME_DISPLAY_ICONV), NIL, NIL) < 0 ||
            putc('\n', fo) == EOF){
         rv = FAL0;
         break;
      }
   NYD2_OU;
   return rv;
}

static s32
a_sendout_body(FILE *fo, FILE *fi, enum conversion convert){
   struct str outrest, inrest;
   boole iseof, seenempty;
   char *buf;
   uz size, bufsize, cnt;
   s32 rv;
   NYD2_IN;

   rv = su_ERR_INVAL;
   mx_fs_linepool_aquire(&buf, &bufsize);
   outrest.s = inrest.s = NIL;
   outrest.l = inrest.l = 0;

   if(convert == CONV_TOQP || convert == CONV_8BIT || convert == CONV_7BIT
#ifdef mx_HAVE_ICONV
         || iconvd != (iconv_t)-1
#endif
   ){
      fflush(fi);
      cnt = fsize(fi);
   }

   seenempty = iseof = FAL0;
   while(!iseof){
      if(convert == CONV_TOQP || convert == CONV_8BIT || convert == CONV_7BIT
#ifdef mx_HAVE_ICONV
            || iconvd != (iconv_t)-1
#endif
      ){
         if(fgetline(&buf, &bufsize, &cnt, &size, fi, FAL0) == NIL)
            break;
         if(convert != CONV_TOQP && seenempty && is_head(buf, size, FAL0)){
            if(bufsize - 1 >= size + 1){
               bufsize += 32;
               buf = su_MEM_REALLOC(buf, bufsize);
            }
            su_mem_move(&buf[1], &buf[0], ++size);
            buf[0] = '>';
            seenempty = FAL0;
         }else
            seenempty = (size == 1 /*&& buf[0] == '\n'*/);
      }else if((size = fread(buf, sizeof *buf, bufsize, fi)) == 0)
         break;
joutln:
      if(mx_xmime_write(buf, size, fo, convert, mx_MIME_DISPLAY_ICONV,
            &outrest, (iseof > FAL0 ? NULL : &inrest)) < 0)
         goto jleave;
   }
   if(iseof <= FAL0 && (outrest.l != 0 || inrest.l != 0)){
      size = 0;
      iseof = (iseof || inrest.l == 0) ? TRU1 : TRUM1;
      goto joutln;
   }

   rv = ferror(fi) ? su_ERR_IO : su_ERR_NONE;
jleave:
   if(outrest.s != NIL)
      su_FREE(outrest.s);
   if(inrest.s != NIL)
      su_FREE(inrest.s);
   mx_fs_linepool_release(buf, bufsize);

   NYD2_OU;
   return rv;
}

static s32
a_sendout_attach_file(struct header *hp, struct mx_attachment *ap, FILE *fo,
   boole force)
{
   /* TODO of course, the MIME classification needs to performed once
    * TODO only, not for each and every charset anew ... ;-// */
   char *charset_iter_orig[2];
   boole any;
   long offs;
   s32 err;
   NYD_IN;

   err = su_ERR_NONE;

   /* Is this already in target charset?  Simply copy over */
   if(ap->a_conv == mx_ATTACHMENTS_CONV_TMPFILE){
      err = a_sendout__attach_file(hp, ap, fo, force);
      mx_fs_close(ap->a_tmpf);
      su_DBG( ap->a_tmpf = NIL; )
      goto jleave;
   }

   /* If we don't apply charset conversion at all (fixed input=output charset)
    * we also simply copy over, since it's the users desire */
   if (ap->a_conv == mx_ATTACHMENTS_CONV_FIX_INCS) {
      ap->a_charset = ap->a_input_charset;
      err = a_sendout__attach_file(hp, ap, fo, force);
      goto jleave;
   } else
      ASSERT(ap->a_input_charset != NULL);

   /* Otherwise we need to iterate over all possible output charsets */
   if ((offs = ftell(fo)) == -1) {
      err = su_ERR_IO;
      goto jleave;
   }

   mx_mime_charset_iter_recurse(charset_iter_orig);
   for(any = FAL0, mx_mime_charset_iter_reset(NIL);;
         any = TRU1, mx_mime_charset_iter_next()){
      boole myforce;

      myforce = FAL0;
      if(!mx_mime_charset_iter_is_valid()) {
         if(!any || !(myforce = force)){
            err = su_ERR_NOENT;
            break;
         }
      }
      err = a_sendout__attach_file(hp, ap, fo, myforce);
      if (err == su_ERR_NONE || (err != su_ERR_ILSEQ && err != su_ERR_INVAL))
         break;
      clearerr(fo);
      if (fseek(fo, offs, SEEK_SET) == -1) {
         err = su_ERR_IO;
         break;
      }
      if (ap->a_conv != mx_ATTACHMENTS_CONV_DEFAULT) {
         err = su_ERR_ILSEQ;
         break;
      }
      ap->a_charset = NULL;
   }
   mx_mime_charset_iter_restore(charset_iter_orig);

jleave:
   NYD_OU;
   return err;
}

static s32
a_sendout__attach_file(struct header *hp, struct mx_attachment *ap, FILE *fo,
   boole force)
{
   FILE *fi;
   char const *charset;
   enum conversion convert;
   boole do_iconv;
   s32 err;
   NYD_IN;

   err = su_ERR_NONE;

   /* Either charset-converted temporary file, or plain path */
   if(ap->a_conv == mx_ATTACHMENTS_CONV_TMPFILE){
      fi = ap->a_tmpf;
      ASSERT(ftell(fi) == 0);
   }else if((fi = mx_fs_open(ap->a_path, mx_FS_O_RDONLY)) == NIL){
      err = su_err_no();
      n_err(_("%s: %s\n"), n_shexp_quote_cp(ap->a_path, FAL0),
         su_err_doc(err));
      goto jleave;
   }

   /* MIME part header for attachment */
   {  char const *ct, *cp;

      /* No MBOXO quoting here, never!! */
      ct = ap->a_content_type;
      charset = ap->a_charset;
      convert = mx_mime_type_classify_file(fi, (char const**)&ct,
            &charset, &do_iconv, TRU1);

      if(charset == NIL || ap->a_conv == mx_ATTACHMENTS_CONV_FIX_INCS ||
            ap->a_conv == mx_ATTACHMENTS_CONV_TMPFILE)
         do_iconv = FAL0;

      if(force && do_iconv){
         convert = CONV_TOB64;
         ap->a_content_type = ct = "application/octet-stream";
         ap->a_charset = charset = NULL;
         do_iconv = FAL0;
      }

      if (fprintf(fo, "\n--%s\n", _sendout_boundary) < 0 ||
            a_sendout_put_ct(fo, ct, charset) < 0 ||
            a_sendout_put_cte(fo, convert) < 0 ||
            a_sendout_put_cd(fo, ap->a_content_disposition, ap->a_name) < 0)
         goto jerr_header;

      if((cp = ok_vlook(stealthmua)) == NULL || !su_cs_cmp(cp, "noagent")){
         struct mx_name *np;

         /* TODO RFC 2046 specifies that the same Content-ID should be used
          * TODO for identical data; this is too hard for use right now,
          * TODO because if done right it should be checksum based!?! */
         if((np = ap->a_content_id) != NULL)
            cp = np->n_name;
         else
            cp = a_sendout_random_id(hp, FAL0);

         if(cp != NULL && fprintf(fo, "Content-ID: <%s>\n", cp) < 0)
            goto jerr_header;
      }

      if ((cp = ap->a_content_description) != NULL &&
            (fputs("Content-Description: ", fo) == EOF ||
             mx_xmime_write(cp, su_cs_len(cp), fo, CONV_TOHDR,
                  (mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT), NIL, NIL
               ) < 0 || putc('\n', fo) == EOF))
         goto jerr_header;

      if (putc('\n', fo) == EOF) {
jerr_header:
         err = su_err_no();
         goto jerr_fclose;
      }
   }

#ifdef mx_HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
   if (do_iconv) {
      /* Do not avoid things like utf-8 -> utf-8 to be able to detect encoding
       * errors XXX also this should be !iconv_is_same_charset(), and THAT.. */
      if (/*su_cs_cmp_case(charset, ap->a_input_charset) &&*/
            (iconvd = n_iconv_open(charset, ap->a_input_charset)
               ) == (iconv_t)-1 && (err = su_err_no()) != 0) {
         if (err == su_ERR_INVAL)
            n_err(_("Cannot convert from %s to %s\n"), ap->a_input_charset,
               charset);
         else
            n_err(_("iconv_open: %s\n"), su_err_doc(err));
         goto jerr_fclose;
      }
   }
#endif

   err = a_sendout_body(fo, fi, convert);
jerr_fclose:
   if(ap->a_conv != mx_ATTACHMENTS_CONV_TMPFILE)
      mx_fs_close(fi);

jleave:
   NYD_OU;
   return err;
}

static boole
a_sendout_setup_creds(struct mx_send_ctx *scp, boole sign_caps){
   boole rv;
   char *shost, *from;
   NYD_IN;

   rv = FAL0;
   shost = ok_vlook(smtp_hostname);
   from = ((sign_caps || shost == NIL) ? skin(myorigin(scp->sc_hp)) : NIL);

   if(sign_caps){
      if(from == NIL){
#ifdef mx_HAVE_SMIME
         n_err(_("No *from* address for signing specified\n"));
         goto jleave;
#endif
      }
      scp->sc_signer.l = su_cs_len(scp->sc_signer.s = from);
   }

   /* file:// and test:// MTAs do not need credentials */
   if(scp->sc_urlp->url_cproto == CPROTO_NONE){
      rv = TRU1;
      goto jleave;
   }

#ifdef mx_HAVE_SMTP
   if(shost == NIL){
      if(from == NIL){
         n_err(_("Your configuration requires a *from* address, "
            "but none was given\n"));
         goto jleave;
      }
      scp->sc_urlp->url_u_h.l = su_cs_len(scp->sc_urlp->url_u_h.s = from);
   }else
      __sendout_ident = scp->sc_urlp->url_u_h.s;

   if(!mx_cred_auth_lookup(scp->sc_credp, scp->sc_urlp))
      goto jleave;

   rv = TRU1;
#endif

jleave:
   NYD_OU;
   return rv;
}

static s32
a_sendout_attach_msg(struct header *hp, struct mx_attachment *ap, FILE *fo)
{
   struct message *mp;
   char const *ccp;
   s32 err;
   NYD_IN;
   UNUSED(hp);

   err = su_ERR_NONE;

   if(fprintf(fo, "\n--%s\nContent-Type: message/rfc822\n"
         "Content-Disposition: inline\n", _sendout_boundary) < 0)
      goto jerr;

   if((ccp = ok_vlook(stealthmua)) == NULL || !su_cs_cmp(ccp, "noagent")){
      struct mx_name *np;

      /* TODO RFC 2046 specifies that the same Content-ID should be used
       * TODO for identical data; this is too hard for use right now,
       * TODO because if done right it should be checksum based!?! */
      if((np = ap->a_content_id) != NULL)
         ccp = np->n_name;
      else
         ccp = a_sendout_random_id(hp, FAL0);

      if(ccp != NULL && fprintf(fo, "Content-ID: <%s>\n", ccp) < 0)
         goto jerr;
   }

   if((ccp = ap->a_content_description) != NULL &&
         (fputs("Content-Description: ", fo) == EOF ||
          mx_xmime_write(ccp, su_cs_len(ccp), fo, CONV_TOHDR,
               (mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT), NIL, NIL
            ) < 0 || putc('\n', fo) == EOF))
      goto jerr;
   if(putc('\n', fo) == EOF)
      goto jerr;

   mp = message + ap->a_msgno - 1;
   touch(mp);
   if(sendmp(mp, fo, 0, NULL, SEND_RFC822, NULL) < 0)
jerr:
      if((err = su_err_no()) == su_ERR_NONE)
         err = su_ERR_IO;
   NYD_OU;
   return err;
}

static s32
make_multipart(struct header *hp, int convert, FILE *fi, FILE *fo,
   char const *contenttype, char const *charset, boole force)
{
   struct mx_attachment *att;
   s32 err;
   NYD_IN;

   err = su_ERR_NONE;

   if(fputs("This is a multi-part message in MIME format.\n", fo) == EOF)
      goto jerr;

   if(fsize(fi) != 0){
      char const *cp;

      if(fprintf(fo, "\n--%s\n", _sendout_boundary) < 0 ||
            a_sendout_put_ct(fo, contenttype, charset) < 0 ||
            a_sendout_put_cte(fo, convert) < 0 ||
            fprintf(fo, "Content-Disposition: inline\n") < 0)
         goto jerr;
      if (((cp = ok_vlook(stealthmua)) == NULL || !su_cs_cmp(cp, "noagent")) &&
            (cp = a_sendout_random_id(hp, FAL0)) != NULL &&
            fprintf(fo, "Content-ID: <%s>\n", cp) < 0)
         goto jerr;
      if(putc('\n', fo) == EOF)
         goto jerr;

      if((err = a_sendout_body(fo, fi, convert)) != su_ERR_NONE)
         goto jleave;

      if(ferror(fi))
         goto jerr;
   }

   for (att = hp->h_attach; att != NULL; att = att->a_flink) {
      if (att->a_msgno) {
         if ((err = a_sendout_attach_msg(hp, att, fo)) != su_ERR_NONE)
            goto jleave;
      }else if((err = a_sendout_attach_file(hp, att, fo, force)
            ) != su_ERR_NONE)
         goto jleave;
   }

   /* the final boundary with two attached dashes */
   if(fprintf(fo, "\n--%s--\n", _sendout_boundary) < 0)
jerr:
      if((err = su_err_no()) == su_ERR_NONE)
         err = su_ERR_IO;
jleave:
   NYD_OU;
   return err;
}

static FILE *
a_sendout_infix(struct header *hp, FILE *fi, boole dosign, boole force)
{
   struct mx_fs_tmp_ctx *fstcp;
   enum conversion convert;
   int err;
   boole do_iconv;
   char const *contenttype, *charset;
   FILE *nfo, *nfi;
   NYD_IN;

   nfi = NULL;
   charset = NULL;
   do_iconv = FAL0;
   err = su_ERR_NONE;

   if((nfo = mx_fs_tmp_open(NIL, "infix", (mx_FS_O_WRONLY | mx_FS_O_HOLDSIGS),
            &fstcp)) == NIL){
      n_perr(_("infix: temporary mail file"), err = su_err_no());
      goto jleave;
   }

   if((nfi = mx_fs_open(fstcp->fstc_filename, mx_FS_O_RDONLY)) == NIL){
      n_perr(fstcp->fstc_filename, err = su_err_no());
      mx_fs_close(nfo);
   }

   mx_fs_tmp_release(fstcp);

   if(nfi == NIL)
      goto jleave;

   n_pstate &= ~n_PS_HEADER_NEEDED_MIME; /* TODO hack -> be carrier tracked */

   /* C99 */{
      boole no_mboxo;

      no_mboxo = dosign;

      /* Will be NULL for text/plain */
      if((n_poption & n_PO_Mm_FLAG) && n_poption_arg_Mm != NULL){
         contenttype = n_poption_arg_Mm;
         no_mboxo = TRU1;
      }else
         contenttype = "text/plain";

      convert = mx_mime_type_classify_file(fi, &contenttype, &charset,
            &do_iconv, no_mboxo);
   }

#ifdef mx_HAVE_ICONV
   /* C99 */{
   char const *tcs;
   boole gut_iconv;

   /* This is the logic behind *charset-force-transport*.  XXX very weird
    * XXX as said a thousand times, Part==object, has dump_to_{wire,user}, and
    * XXX does it (including _force_) for _itself_ only; the global header
    * XXX has then to become spliced in (for multipart messages) */
   if(force && do_iconv){
      convert = CONV_TOB64;
      contenttype = "application/octet-stream";
      charset = NIL;
      do_iconv = FAL0;
   }

   tcs = ok_vlook(ttycharset);

   if((gut_iconv = mx_header_needs_mime(hp))){
      char const *convhdr;

      convhdr = mx_mime_charset_iter_or_fallback();

      if(iconvd != R(iconv_t,-1)) /* XXX  */
         n_iconv_close(iconvd);
      /* Do not avoid things like utf-8 -> utf-8 to be able to detect encoding
       * errors XXX also this should be !iconv_is_same_charset(), and THAT.. */
      if(/*su_cs_cmp_case(convhdr, tcs) != 0 &&*/
            (iconvd = n_iconv_open(convhdr, tcs)) == R(iconv_t,-1) &&
            (err = su_err_no()) != su_ERR_NONE){
         charset = convhdr;
         goto jiconv_err;
      }
   }
#endif /* mx_HAVE_ICONV */

   if(!n_puthead(FAL0, hp, nfo,
         (GTO | GSUBJECT | GCC | GBCC | GNL | GCOMMA | GUA | GMIME | GMSGID |
         GIDENT | GREF | GDATE), SEND_MBOX, convert, contenttype, charset)){
      if((err = su_err_no()) == su_ERR_NONE)
         err = su_ERR_IO;
      goto jerr;
   }

#ifdef mx_HAVE_ICONV
   if(gut_iconv)
      n_iconv_close(iconvd);

   if(do_iconv && charset != NIL){ /*TODO charset->mimetype_classify_file*/
      /* Do not avoid things like utf-8 -> utf-8 to be able to detect encoding
       * errors XXX also this should be !iconv_is_same_charset(), and THAT.. */
      if(/*su_cs_cmp_case(charset, tcs) != 0 &&*/
            (iconvd = n_iconv_open(charset, tcs)) == R(iconv_t,-1) &&
            (err = su_err_no()) != su_ERR_NONE){
jiconv_err:
         if(err == su_ERR_INVAL)
            n_err(_("Cannot convert from %s to %s\n"), tcs, charset);
         else
            n_perr("iconv_open", err);
         goto jerr;
      }
   }

   }
#endif /* mx_HAVE_ICONV */

   if(hp->h_attach != NULL){
      if((err = make_multipart(hp, convert, fi, nfo, contenttype, charset,
            force)) != su_ERR_NONE)
         goto jerr;
   }else if((err = a_sendout_body(nfo, fi, convert)) != su_ERR_NONE)
      goto jerr;

   if(fflush(nfo) == EOF)
      err = su_err_no();
jerr:
   mx_fs_close(nfo);

   if(err == su_ERR_NONE){
      fflush_rewind(nfi);
      mx_fs_close(fi);
   }else{
      mx_fs_close(nfi);
      nfi = NIL;
   }

jleave:
#ifdef mx_HAVE_ICONV
   if(iconvd != R(iconv_t,-1))
      n_iconv_close(iconvd);
#endif
   if(nfi == NIL)
      su_err_set_no(err);

   NYD_OU;
   return nfi;
}

static boole
_check_dispo_notif(struct mx_name *mdn, struct header *hp, FILE *fo)
{
   char const *from;
   boole rv = TRU1;
   NYD_IN;

   /* TODO smtp_disposition_notification (RFC 3798): relation to return-path
    * TODO not yet checked */
   if (!ok_blook(disposition_notification_send))
      goto jleave;

   if (mdn != NULL && mdn != (struct mx_name*)0x1)
      from = mdn->n_name;
   else if ((from = myorigin(hp)) == NULL) {
      if (n_poption & n_PO_D_V)
         n_err(_("*disposition-notification-send*: *from* not set\n"));
      goto jleave;
   }

   if (!a_sendout_put_addrline("Disposition-Notification-To:",
         nalloc(n_UNCONST(from), 0), fo, 0))
      rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

static int
a_sendout_sendmail(void *v, enum n_mailsend_flags msf)
{
   struct header head;
   char *str = v;
   int rv;
   NYD_IN;

   su_mem_set(&head, 0, sizeof head);
   head.h_flags = HF_CMD_mail;
   if((head.h_to = lextract(str, GTO |
         (ok_blook(fullnames) ? GFULL | GSKIN : GSKIN))) != NULL)
      head.h_mailx_raw_to = n_namelist_dup(head.h_to, head.h_to->n_type);

   rv = n_mail1(msf, &head, NIL, NIL, ((n_pstate & n_PS_ARGMOD_LOCAL) != 0));

   NYD_OU;
   return (rv != OKAY); /* reverse! */
}

static struct mx_name *
a_sendout_file_a_pipe(struct mx_name *names, FILE *fo, boole *senderror){
   boole mfap;
   char const *sh;
   u32 pipecnt, xcnt, i, swf;
   struct mx_name *np;
   FILE *fp, **fppa;
   NYD_IN;

   fp = NIL;
   fppa = NIL;

   /* Look through all recipients and do a quick return if no file or pipe
    * addressee is found */
   for(pipecnt = xcnt = 0, np = names; np != NIL; np = np->n_flink){
      if(np->n_type & GDEL)
         continue;
      switch(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE){
      case mx_NAME_ADDRSPEC_ISFILE: ++xcnt; break;
      case mx_NAME_ADDRSPEC_ISPIPE: ++pipecnt; break;
      }
   }
   if((pipecnt | xcnt) == 0)
      goto jleave;

   /* Otherwise create an array of file descriptors for each found pipe
    * addressee to get around the dup(2)-shared-file-offset problem, i.e.,
    * each pipe subprocess needs its very own file descriptor, and we need
    * to deal with that.
    * This is true even if *sendwait* requires fully synchronous mode, since
    * the shell handlers can fork away and pass the descriptor around, so we
    * cannot simply use a single one and rewind that after the top children
    * shell has returned.
    * To make our life a bit easier let's just use the auto-reclaimed
    * string storage */
   if(pipecnt == 0 || (n_poption & n_PO_D)){
      pipecnt = 0;
      sh = NIL;
   }else{
      i = sizeof(FILE*) * pipecnt;
      fppa = n_lofi_alloc(i);
      su_mem_set(fppa, 0, i);

      sh = ok_vlook(SHELL);
   }

   mfap = ok_blook(mbox_fcc_and_pcc);
   swf = a_sendout_sendwait_to_swf();

   for(np = names; np != NIL; np = np->n_flink){
      if(!(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE) || (np->n_type & GDEL))
         continue;

      /* In days of old we removed the entry from the the list; now for sake of
       * header expansion we leave it in and mark it as deleted */
      np->n_type |= GDEL;

      if(n_poption & n_PO_D_VV)
         n_err(_(">>> Writing message via %s\n"),
            n_shexp_quote_cp(np->n_name, FAL0));
      /* We _do_ write to STDOUT, anyway! */
      if((n_poption & n_PO_D) &&
            ((np->n_flags & mx_NAME_ADDRSPEC_ISPIPE) ||
               np->n_name[0] != '-' || np->n_name[1] != '\0'))
         continue;

      /* See if we have copied the complete message out yet.  If not, do so */
      if(fp == NIL){
         int c;
         struct mx_fs_tmp_ctx *fstcp;

         if((fp = mx_fs_tmp_open(NIL, "outof", (mx_FS_O_RDWR |
                  mx_FS_O_HOLDSIGS), &fstcp)) == NIL){
            n_perr(_("Creation of temporary image"), 0);
            pipecnt = 0;
            goto jerror;
         }

         for(i = 0; i < pipecnt; ++i)
            if((fppa[i] = mx_fs_open(fstcp->fstc_filename, mx_FS_O_RDONLY)
                  ) == NIL){
               n_perr(_("Creation of pipe image descriptor"), 0);
               break;
            }

         mx_fs_tmp_release(fstcp);

         if(i != pipecnt){
            pipecnt = i;
            goto jerror;
         }

         if(mfap)
            fprintf(fp, "From %s %s",
               ok_vlook(LOGNAME), time_current.tc_ctime);
         c = EOF;
         while(i = c, (c = getc(fo)) != EOF)
            putc(c, fp);
         rewind(fo);
         if((int)i != '\n')
            putc('\n', fp);
         if(mfap)
            putc('\n', fp);
         fflush(fp);
         if(ferror(fp)){
            n_perr(_("Finalizing write of temporary image"), 0);
            goto jerror;
         }

         /* From now on use xcnt as a counter for pipecnt */
         xcnt = 0;
      }

      /* Now either copy "image" to the desired file or give it as the
       * standard input to the desired program as appropriate */
      if(np->n_flags & mx_NAME_ADDRSPEC_ISPIPE){
         struct mx_child_ctx cc;
         sigset_t nset;

         sigemptyset(&nset);
         sigaddset(&nset, SIGHUP);
         sigaddset(&nset, SIGINT);
         sigaddset(&nset, SIGQUIT);

         mx_child_ctx_setup(&cc);
         cc.cc_flags = mx_CHILD_SPAWN_CONTROL |
               (swf & a_SENDOUT_SWF_PCC ? mx_CHILD_RUN_WAIT_LIFE : 0);
         cc.cc_mask = &nset;
         cc.cc_fds[mx_CHILD_FD_IN] = fileno(fppa[xcnt]);
         cc.cc_fds[mx_CHILD_FD_OUT] = mx_CHILD_FD_NULL;
         cc.cc_cmd = sh;
         cc.cc_args[0] = "-c";
         cc.cc_args[1] = &np->n_name[1];

         if(!mx_child_run(&cc)){
            n_err(_("Piping message to %s failed\n"),
               n_shexp_quote_cp(np->n_name, FAL0));
            goto jerror;
         }

         /* C99 */{
            FILE *tmp;

            tmp = fppa[xcnt];
            fppa[xcnt++] = NIL;
            mx_fs_close(tmp);
         }

         if(!(swf & a_SENDOUT_SWF_PCC))
            mx_child_forget(&cc);
      }else{
         int c;
         FILE *fout;
         char const *fname, *fnameq;

         if((fname = fexpand(np->n_name, FEXP_NSHELL)) == NIL) /* TODO */
            goto jerror;
         fnameq = n_shexp_quote_cp(fname, FAL0);

         if(fname[0] == '-' && fname[1] == '\0')
            fout = n_stdout;
         else{
            int xerr;
            BITENUM_IS(u32,mx_fs_open_state) fs;

            if((fout = mx_fs_open_any(fname, (mx_FS_O_CREATE |
                     (mfap ? mx_FS_O_RDWR | mx_FS_O_APPEND
                      : mx_FS_O_WRONLY | mx_FS_O_TRUNC)), &fs)) == NIL){
jefileeno:
               xerr = su_err_no();
jefile:
               n_err(_("Writing message to %s failed: %s\n"),
                  fnameq, su_err_doc(xerr));
               goto jerror;
            }

            if((fs & (n_PROTO_MASK | mx_FS_OPEN_STATE_EXISTS)) ==
                  (n_PROTO_FILE | mx_FS_OPEN_STATE_EXISTS)){
               if(!mx_file_lock(fileno(fout), (mx_FILE_LOCK_MODE_TEXCL |
                     mx_FILE_LOCK_MODE_RETRY)))
                  goto jefileeno;
               if(mfap && (xerr = n_folder_mbox_prepare_append(fout, FAL0, NIL)
                     ) != su_ERR_NONE)
                  goto jefile;
            }
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
            mx_fs_close(fout);
         else
            clearerr(fout);
      }
   }

jleave:
   if(fp != NIL)
      mx_fs_close(fp);

   if(fppa != NIL){
      for(i = 0; i < pipecnt; ++i)
         if((fp = fppa[i]) != NIL)
            mx_fs_close(fp);
      n_lofi_free(fppa);
   }

   NYD_OU;
   return names;

jerror:
   *senderror = TRU1;
   while(np != NIL){
      if(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE)
         np->n_type |= GDEL;
      np = np->n_flink;
   }
   goto jleave;
}

static boole
a_sendout_mightrecord(FILE *fp, struct mx_name *to, boole resend){
   char const *ccp;
   char *cp;
   boole rv;
   NYD2_IN;

   rv = TRU1;

   if(to != NIL){
      ccp = cp = savestr(to->n_name);
      while(*cp != '\0' && *cp != '@')
         ++cp;
      *cp = '\0';
   }else
      ccp = ok_vlook(record);

   if(ccp == NIL)
      goto jleave;

   if((cp = fexpand(ccp, FEXP_NSHELL)) == NIL) /* TODO */
      goto jbail;

   switch(which_protocol(ccp, FAL0, FAL0, NIL)){
   case n_PROTO_EML:
   case n_PROTO_POP3:
   case n_PROTO_UNKNOWN:
      goto jbail;
   default:
      break;
   }

   switch(*(ccp = cp)){
   case '.':
      if(cp[1] != '/'){ /* DIRSEP */
   default:
         if(ok_blook(outfolder)){
            struct str s;
            char const *nccp, *folder;

            switch(which_protocol(ccp, TRU1, FAL0, &nccp)){
            case PROTO_FILE:
               ccp = "file://";
               if(0){
               /* FALLTHRU */
            case PROTO_MAILDIR:
#ifdef mx_HAVE_MAILDIR
                  ccp = "maildir://";
#else
                  n_err(_("*record*: *outfolder*: no Maildir directory "
                     "support compiled in\n"));
                  goto jbail;
#endif
               }
               folder = n_folder_query();
#ifdef mx_HAVE_IMAP
               if(which_protocol(folder, FAL0, FAL0, NIL) == PROTO_IMAP){
                  n_err(_("*record*: *outfolder* set, *folder* is IMAP "
                     "based: only one protocol per file is possible\n"));
                  goto jbail;
               }
#endif
               ccp = str_concat_csvl(&s, ccp, folder, nccp, NIL)->s;
               /* FALLTHRU */
            case n_PROTO_IMAP:
               break;
            default:
               goto jbail;
            }
         }
      }
      /* FALLTHRU */
   case '/':
      break;
   }

   if(n_poption & n_PO_D_VV)
      n_err(_(">>> Writing message via %s\n"), n_shexp_quote_cp(ccp, FAL0));

   if(!(n_poption & n_PO_D) && !a_sendout__savemail(ccp, fp, resend)){
jbail:
      n_err(_("Failed to save message in %s - message not sent\n"),
         n_shexp_quote_cp(ccp, FAL0));
      n_exit_status |= n_EXIT_ERR;
      savedeadletter(fp, 1);
      rv = FAL0;
   }

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_sendout__savemail(char const *name, FILE *fp, boole resend){
   FILE *fo;
   uz bufsize, buflen, cnt;
   BITENUM_IS(u32,mx_fs_open_state) fs;
   char *buf;
   boole rv, emptyline;
   NYD_IN;
   UNUSED(resend);

   rv = FAL0;
   mx_fs_linepool_aquire(&buf, &bufsize);

   if((fo = mx_fs_open_any(name, (mx_FS_O_RDWR | mx_FS_O_APPEND |
            mx_FS_O_CREATE), &fs)) == NIL){
      n_perr(name, 0);
      goto j_leave;
   }

   if((fs & (n_PROTO_MASK | mx_FS_OPEN_STATE_EXISTS)) ==
         (n_PROTO_FILE | mx_FS_OPEN_STATE_EXISTS)){
      int xerr;

      /* TODO RETURN check, but be aware of protocols: v15: Mailbox->lock()!
       * TODO BETTER yet: should be returned in lock state already! */
      if(!mx_file_lock(fileno(fo), (mx_FILE_LOCK_MODE_TEXCL |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
         xerr = su_err_no();
         goto jeappend;
      }

      if((xerr = n_folder_mbox_prepare_append(fo, FAL0, NIL)) != su_ERR_NONE){
jeappend:
         n_perr(name, xerr);
         goto jleave;
      }
   }

   if(fprintf(fo, "From %s %s", ok_vlook(LOGNAME), time_current.tc_ctime) < 0)
      goto jleave;

   rv = TRU1;

   fflush_rewind(fp);
   for(emptyline = FAL0, buflen = 0, cnt = fsize(fp);
         fgetline(&buf, &bufsize, &cnt, &buflen, fp, FAL0) != NIL;){
      /* Only if we are resending it can happen that we have to quote From_
       * lines here; we don't generate messages which are ambiguous ourselves.
       * xxx No longer true after (Reintroduce ^From_ MBOXO with
       * xxx *mime-encoding*=8b (too many!)[..]) */
      /*if(resend){*/
         if(emptyline && is_head(buf, buflen, FAL0)){
            if(putc('>', fo) == EOF){
               rv = FAL0;
               break;
            }
         }
      /*}su_DBG(else ASSERT(!is_head(buf, buflen, FAL0)); )*/

      emptyline = (buflen > 0 && *buf == '\n');
      if(fwrite(buf, sizeof *buf, buflen, fo) != buflen){
         rv = FAL0;
         break;
      }
   }
   if(rv){
      if(buflen > 0 && buf[buflen - 1] != '\n'){
         if(putc('\n', fo) == EOF)
            rv = FAL0;
      }
      if(rv && (putc('\n', fo) == EOF || fflush(fo)))
         rv = FAL0;
   }

   if(!rv){
      n_perr(name, su_ERR_IO);
      rv = FAL0;
   }

jleave:
   /* C99 */{
      int e;

      really_rewind(fp, e);
      if(e)
         rv = FAL0;
   }
   if(!mx_fs_close(fo))
      rv = FAL0;

j_leave:
   mx_fs_linepool_release(buf, bufsize);

   NYD_OU;
   return rv;
}

static boole
a_sendout_transfer(struct mx_send_ctx *scp, boole resent, boole *senderror){
   u32 cnt;
   struct mx_name *np;
   FILE *input_save;
   boole rv;
   NYD_IN;

   rv = FAL0;

   /* Do we need to create a Bcc: free overlay?
    * TODO In v15 we would have an object tree with dump-to-wire, we have our
    * TODO file stream which acts upon an I/O device that stores so-and-so-much
    * TODO memory, excess in a temporary file; either each object knows its
    * TODO offset where it placed its dump-to-wire, or we create a list overlay
    * TODO which records these offsets.  Then, in our non-blocking eventloop
    * TODO which writes data to the MTA child as it goes we simply not write
    * TODO the Bcc: as necessary; how about that? */
   input_save = scp->sc_input;
   if((resent || (scp->sc_hp != NIL && scp->sc_hp->h_bcc != NIL)) &&
         !ok_blook(mta_bcc_ok)){
      boole inhdr, inskip;
      uz bufsize, bcnt, llen;
      char *buf;
      FILE *fp;

      if((fp = mx_fs_tmp_open(NIL, "mtabccok", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
               NIL)) == NIL){
jewritebcc:
         n_perr(_("Creation of *mta-write-bcc* message"), 0);
         *senderror = TRU1;
         goto jleave;
      }
      scp->sc_input = fp;

      mx_fs_linepool_aquire(&buf, &bufsize);
      bcnt = fsize(input_save);
      inhdr = TRU1;
      inskip = FAL0;
      while(fgetline(&buf, &bufsize, &bcnt, &llen, input_save, TRU1) != NIL){
         if(inhdr){
            if(llen == 1 && *buf == '\n')
               inhdr = FAL0;
            else{
               if(inskip && *buf == ' ')
                  continue;
               inskip = FAL0;
               /* (We need _case for resent only);
                * xxx We do not resent that yet , but place the logic today */
               if(su_cs_starts_with_case(buf, "bcc:") ||
                     (resent && su_cs_starts_with_case(buf, "resent-bcc:"))){
                  inskip = TRU1;
                  continue;
               }
            }
         }
         if(fwrite(buf, 1, llen, fp) != llen)
            goto jewritebcc;
      }
      mx_fs_linepool_release(buf, bufsize);

      if(ferror(input_save)){
         *senderror = TRU1;
         goto jleave;
      }
      fflush_rewind(fp);
   }

   rv = TRU1;

   for(cnt = 0, np = scp->sc_to; np != NIL;){
      FILE *ef;

      if((ef = mx_privacy_encrypt_try(scp->sc_input, np->n_name)
            ) != R(FILE*,-1)){
         if(ef != NIL){
            struct mx_name *nsave;
            FILE *fisave;

            fisave = scp->sc_input;
            nsave = scp->sc_to;

            scp->sc_to = ndup(np, np->n_type & ~(GFULL | GFULLEXTRA | GSKIN));
            scp->sc_input = ef;
            if(!a_sendout_mta_start(scp))
               rv = FAL0;
            scp->sc_to = nsave;
            scp->sc_input = fisave;

            mx_fs_close(ef);
         }else{
            n_err(_("Message not sent to: %s\n"), np->n_name);
            _sendout_error = TRU1;
         }

         rewind(scp->sc_input);

         if(np->n_flink != NIL)
            np->n_flink->n_blink = np->n_blink;
         if(np->n_blink != NIL)
            np->n_blink->n_flink = np->n_flink;
         if(np == scp->sc_to)
            scp->sc_to = np->n_flink;
         np = np->n_flink;
      }else{
         ++cnt;
         np = np->n_flink;
      }
   }

   if(cnt > 0 && (mx_privacy_encrypt_is_forced() || !a_sendout_mta_start(scp)))
      rv = FAL0;

jleave:
   if(input_save != scp->sc_input){
      mx_fs_close(scp->sc_input);
      rewind(scp->sc_input = input_save);
   }

   NYD_OU;
   return rv;
}

static boole
a_sendout_mta_start(struct mx_send_ctx *scp)
{
   struct mx_child_ctx cc;
   sigset_t nset;
   char const **args, *mta;
   boole rv, dowait;
   NYD_IN;

   /* Let rv be: TRU1=SMTP, FAL0=file, TRUM1=test */
   mta = scp->sc_urlp->url_input;
   if(scp->sc_urlp->url_cproto == CPROTO_NONE){
      if(scp->sc_urlp->url_portno == 0)
         rv = FAL0;
      else{
         ASSERT(scp->sc_urlp->url_portno == U16_MAX);
         rv = TRUM1;
      }
   }else
      rv = TRU1;

   sigemptyset(&nset);
   sigaddset(&nset, SIGHUP);
   sigaddset(&nset, SIGINT);
   sigaddset(&nset, SIGQUIT);
   sigaddset(&nset, SIGTSTP);
   sigaddset(&nset, SIGTTIN);
   sigaddset(&nset, SIGTTOU);
   mx_child_ctx_setup(&cc);
   dowait = ((n_poption & n_PO_D_V) ||
         (a_sendout_sendwait_to_swf() & a_SENDOUT_SWF_MTA));
   if(rv != TRU1 || dowait)
      cc.cc_flags |= mx_CHILD_SPAWN_CONTROL;
   cc.cc_mask = &nset;

   if(rv != TRU1){
      char const *mta_base;

      if((mta = fexpand(mta_base = mta, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL))) == NIL){
         n_err(_("*mta* variable file expansion failure: %s\n"),
            n_shexp_quote_cp(mta_base, FAL0));
         goto jstop;
      }

      if(rv == TRUM1){
         rv = a_sendout_mta_test(scp, mta);
         goto jleave;
      }

      args = a_sendout_mta_file_args(scp->sc_to, scp->sc_hp);
      if(n_poption & n_PO_D){
         a_sendout_mta_file_debug(scp, mta, args);
         rv = TRU1;
         goto jleave;
      }

      /* Wait with control pipe close until after exec */
      ASSERT(cc.cc_flags & mx_CHILD_SPAWN_CONTROL);
      cc.cc_flags |= mx_CHILD_SPAWN_CONTROL_LINGER;
      cc.cc_fds[mx_CHILD_FD_IN] = fileno(scp->sc_input);
   }else/* if(rv == TRU1)*/{
      UNINIT(args, NULL);
#ifndef mx_HAVE_SMTP
      n_err(_("No SMTP support compiled in\n"));
      goto jstop;
#else
      if(n_poption & n_PO_D){
         (void)mx_smtp_mta(scp);
         rv = TRU1;
         goto jleave;
      }

      cc.cc_fds[mx_CHILD_FD_IN] = mx_CHILD_FD_NULL;
      cc.cc_fds[mx_CHILD_FD_OUT] = mx_CHILD_FD_NULL;
#endif
   }

   /* Fork, set up the temporary mail file as standard input for "mail", and
    * exec with the user list we generated far above */
   if(!mx_child_fork(&cc)){
      if(cc.cc_flags & mx_CHILD_SPAWN_CONTROL_LINGER){
         char const *ecp;

         ecp = (cc.cc_error != su_ERR_NOENT) ? su_err_doc(cc.cc_error)
               : _("executable not found (adjust *mta* variable)");
         n_err(_("Cannot start %s: %s\n"), n_shexp_quote_cp(mta, FAL0), ecp);
      }
jstop:
      savedeadletter(scp->sc_input, TRU1);
      n_err(_("... message not sent\n"));
      _sendout_error = TRU1;
   }else if(cc.cc_pid == 0)
      goto jkid;
   else if(dowait){
      /* TODO Now with SPAWN_CONTROL we could actually (1) handle $DEAD only
       * TODO in the parent, and (2) report the REAL child error status!! */
      rv = (mx_child_wait(&cc) && cc.cc_exit_status == 0);
      if(!rv)
         goto jstop;
   }else{
      mx_child_forget(&cc);
      rv = TRU1;
   }

jleave:
   NYD_OU;
   return rv;

jkid:
   mx_child_in_child_setup(&cc);

#ifdef mx_HAVE_SMTP
   if(rv == TRU1){
      if(mx_smtp_mta(scp))
         _exit(n_EXIT_OK);
      savedeadletter(scp->sc_input, TRU1);
      if(!dowait)
         n_err(_("... message not sent\n"));
   }else
#endif
        {
      execv(mta, n_UNCONST(args));
      mx_child_in_child_exec_failed(&cc, su_err_no());
   }
   for(;;)
      _exit(n_EXIT_ERR);
}

static char const **
a_sendout_mta_file_args(struct mx_name *to, struct header *hp)
{
   uz vas_cnt, i, j;
   char **vas;
   char const **args, *cp, *cp_v15compat;
   boole snda;
   NYD_IN;

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
      j = su_cs_len(cp);
      vas = n_lofi_alloc(sizeof(*vas) * j);
      vas_cnt = (uz)getrawlist(TRU1, vas, j, cp, j);
   }

   i = 4 + n_smopts_cnt + vas_cnt + 4 + 1 + count(to) + 1;
   args = n_autorec_alloc(i * sizeof(char*));

   if((cp_v15compat = ok_vlook(sendmail_progname)) != NULL)
      n_OBSOLETE(_("please use *mta-argv0*, not *sendmail-progname*"));
   cp = ok_vlook(mta_argv0);
   if(cp_v15compat != NULL && !su_cs_cmp(cp, VAL_MTA_ARGV0))
      cp = cp_v15compat;
   args[0] = cp/* TODO v15 only : = ok_vlook(mta_argv0) */;

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
      if (n_poption & n_PO_V)
         args[i++] = "-v";
   }

   for (j = 0; j < n_smopts_cnt; ++j, ++i)
      args[i] = n_smopts[j];

   for (j = 0; j < vas_cnt; ++j, ++i)
      args[i] = vas[j];

   /* -r option?  In conjunction with -t we act compatible to postfix(1) and
    * ignore it (it is -f / -F there) if the message specified From:/Sender:.
    * The interdependency with -t has been resolved in n_puthead() */
   if (!snda && ((n_poption & n_PO_r_FLAG) || ok_blook(r_option_implicit))) {
      struct mx_name const *np;

      if (hp != NULL && (np = hp->h_from) != NULL) {
         /* However, what wasn't resolved there was the case that the message
          * specified multiple From: addresses and a Sender: */
         if((n_poption & n_PO_t_FLAG) && hp->h_sender != NULL)
            np = hp->h_sender;

         if (np->n_fullextra != NULL) {
            args[i++] = "-F";
            args[i++] = np->n_fullextra;
         }
         cp = np->n_name;
      } else {
         ASSERT(n_poption_arg_r == NULL);
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
   if(!ok_blook(mta_no_receiver_arguments))
      for (; to != NULL; to = to->n_flink)
         if (!(to->n_type & GDEL))
            args[i++] = to->n_name;
   args[i] = NULL;

   if(vas != NULL)
      n_lofi_free(vas);
   NYD_OU;
   return args;
}

static void
a_sendout_mta_file_debug(struct mx_send_ctx *scp, char const *mta,
      char const **args){
   uz cnt, bufsize, llen;
   char *buf;
   NYD_IN;

   n_err(_(">>> MTA: %s, arguments:"), n_shexp_quote_cp(mta, FAL0));
   for(; *args != NIL; ++args)
      n_err(" %s", n_shexp_quote_cp(*args, FAL0));
   n_err("\n");

   fflush_rewind(scp->sc_input);

   mx_fs_linepool_aquire(&buf, &bufsize);
   cnt = fsize(scp->sc_input);
   while(fgetline(&buf, &bufsize, &cnt, &llen, scp->sc_input, TRU1) != NIL){
      buf[--llen] = '\0';
      n_err(">>> %s\n", buf);
   }
   mx_fs_linepool_release(buf, bufsize);
   NYD_OU;
}

static boole
a_sendout_mta_test(struct mx_send_ctx *scp, char const *mta)
{
   enum{
      a_OK = 0,
      a_ERR = 1u<<0,
      a_MAFC = 1u<<1,
      a_ANY = 1u<<2,
      a_LASTNL = 1u<<3
   };
   FILE *fp;
   s32 f;
   uz bufsize, cnt, llen;
   char *buf;
   NYD_IN;

   mx_fs_linepool_aquire(&buf, &bufsize);

   if(*mta == '\0')
      fp = n_stdout;
   else{
      if((fp = mx_fs_open(mta, mx_FS_O_RDWR | mx_FS_O_CREATE)) == NIL)
         goto jeno;
      if(!mx_file_lock(fileno(fp), (mx_FILE_LOCK_MODE_TEXCL |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
         f = su_ERR_NOLCK;
         goto jefo;
      }
      if((f = n_folder_mbox_prepare_append(fp, FAL0, NIL)) != su_ERR_NONE)
         goto jefo;
   }

   fflush_rewind(scp->sc_input);
   cnt = fsize(scp->sc_input);
   f = ok_blook(mbox_fcc_and_pcc) ? a_MAFC : a_OK;

   if((f & a_MAFC) &&
         fprintf(fp, "From %s %s", ok_vlook(LOGNAME), time_current.tc_ctime
            ) < 0)
      goto jeno;
   while(fgetline(&buf, &bufsize, &cnt, &llen, scp->sc_input, TRU1) != NIL){
      if(fwrite(buf, 1, llen, fp) != llen)
         goto jeno;
      if(f & a_MAFC){
         f |= a_ANY;
         if(llen > 0 && buf[llen - 1] == '\0')
            f |= a_LASTNL;
         else
            f &= ~a_LASTNL;
      }
   }
   if(ferror(scp->sc_input))
      goto jefo;
   if((f & (a_ANY | a_LASTNL)) == a_ANY && putc('\n', fp) == EOF)
      goto jeno;

jdone:
   if(fp != n_stdout)
      mx_fs_close(fp);
   else
      clearerr(fp);

jleave:
   mx_fs_linepool_release(buf, bufsize);

   NYD_OU;
   return ((f & a_ERR) == 0);

jeno:
   f = su_err_no();
jefo:
   n_err(_("test MTA: cannot open/prepare/write: %s: %s\n"),
      n_shexp_quote_cp(mta, FAL0), su_err_doc(f));
   f = a_ERR;
   if(fp != NIL)
      goto jdone;
   goto jleave;
}

static char const *
a_sendout_random_id(struct header *hp, boole msgid)
{
   static u32 reprocnt;
   struct tm *tmp;
   char const *h;
   uz rl, i;
   char *rv, sep;
   NYD_IN;

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
      h = n_nodename(TRU1);
      sep = '@';
      rl = 8;
      goto jgen;
   }
   if(hp != NULL && (h = skin(myorigin(hp))) != NULL &&
         su_cs_find_c(h, '@') != NULL)
      goto jgen;
   goto jleave;

jgen:
   tmp = &time_current.tc_gm;
   i = sizeof("%04d%02d%02d%02d%02d%02d.%s%c%s") -1 + rl + su_cs_len(h);
   rv = n_autorec_alloc(i +1);
   snprintf(rv, i, "%04d%02d%02d%02d%02d%02d.%s%c%s",
      tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
      tmp->tm_hour, tmp->tm_min, tmp->tm_sec,
      mx_random_create_cp(rl, &reprocnt), sep, h);
   rv[i] = '\0'; /* Because we don't test snprintf(3) return */
jleave:
   NYD_OU;
   return rv;
}

static boole
a_sendout_put_addrline(char const *hname, struct mx_name *np, FILE *fo,
   enum a_sendout_addrline_flags saf)
{
   sz hnlen, col, len;
   enum{
      m_ERROR = 1u<<0,
      m_INIT = 1u<<1,
      m_COMMA = 1u<<2,
      m_NOPF = 1u<<3,
      m_NONAME = 1u<<4,
      m_CSEEN = 1u<<5
   } m;
   NYD_IN;

   m = (saf & GCOMMA) ? m_ERROR | m_COMMA : m_ERROR;

   if((col = hnlen = su_cs_len(hname)) > 0){
#undef _X
#define _X(S)  (col == sizeof(S) -1 && !su_cs_cmp_case(hname, S))
      if (saf & GFILES) {
         ;
      } else if (_X("reply-to:") || _X("mail-followup-to:") ||
            _X("references:") || _X("in-reply-to:") ||
            _X("disposition-notification-to:"))
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
               ((saf & a_SENDOUT_AL_INC_INVADDR ? 0 : EACM_NOLOG) |
                (m & m_NONAME ? EACM_NONAME : EACM_NONE))) &&
            !(saf & a_SENDOUT_AL_INC_INVADDR))
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

      len = su_cs_len(np->n_fullname);
      if (np->n_type & GREF)
         len += 2;
      ++col; /* The separating space */
      if ((m & m_INIT) && /*col > 1 &&*/
            UCMP(z, col + len, >,
               (np->n_type & GREF ? MIME_LINELEN : 72))) {
         if (fputs("\n ", fo) == EOF)
            goto jleave;
         col = 1;
         m &= ~m_CSEEN;
      } else {
         if(!(m & m_INIT) && fwrite(hname, sizeof *hname, hnlen, fo
               ) != sizeof *hname * hnlen)
            goto jleave;
         if(putc(' ', fo) == EOF)
            goto jleave;
      }
      m = (m & ~m_CSEEN) | m_INIT;

      /* C99 */{
         char *hb;

         /* GREF needs to be placed in angle brackets, but which are missing */
         hb = np->n_fullname;
         if(np->n_type & GREF){
            ASSERT(UCMP(z, len, ==, su_cs_len(np->n_fullname) + 2));
            hb = n_lofi_alloc(len +1);
            len -= 2;
            hb[0] = '<';
            hb[len + 1] = '>';
            hb[len + 2] = '\0';
            su_mem_copy(&hb[1], np->n_fullname, len);
            len += 2;
         }
         len = mx_xmime_write(hb, len, fo,
               ((saf & a_SENDOUT_AL_DOMIME) ? CONV_TOHDR_A : CONV_NONE),
               mx_MIME_DISPLAY_ICONV, NIL, NIL);
         if(np->n_type & GREF)
            n_lofi_free(hb);
      }
      if (len < 0)
         goto jleave;
      col += len;
   }

   if(!(m & m_INIT) || putc('\n', fo) != EOF)
      m ^= m_ERROR;
jleave:
   NYD_OU;
   return ((m & m_ERROR) == 0);
}

static boole
a_sendout_infix_resend(FILE *fi, FILE *fo, struct message *mp,
   struct mx_name *to, int add_resent)
{
   uz cnt, c, bufsize;
   char *buf;
   char const *cp;
   struct mx_name *fromfield = NULL, *senderfield = NULL, *mdn;
   boole rv;
   NYD_IN;

   rv = FAL0;
   mx_fs_linepool_aquire(&buf, &bufsize);
   cnt = mp->m_size;

   /* Write the Resent-Fields */
   if (add_resent) {
      if(fputs("Resent-", fo) == EOF)
         goto jleave;
      if(mkdate(fo, "Date") == -1)
         goto jleave;
      if ((cp = myaddrs(NULL)) != NULL) {
         if (!a_sendout_put_name(cp, GCOMMA, SEND_MBOX, "Resent-From:", fo,
               &fromfield))
            goto jleave;
      }
      /* TODO RFC 5322: Resent-Sender SHOULD NOT be used if it's EQ -From: */
      if ((cp = ok_vlook(sender)) != NULL) {
         if (!a_sendout_put_name(cp, GCOMMA | GNOT_A_LIST, SEND_MBOX,
               "Resent-Sender:", fo, &senderfield))
            goto jleave;
      }
      if (!a_sendout_put_addrline("Resent-To:", to, fo, a_SENDOUT_AL_COMMA))
         goto jleave;
      if (((cp = ok_vlook(stealthmua)) == NULL || !su_cs_cmp(cp, "noagent")) &&
            (cp = a_sendout_random_id(NULL, TRU1)) != NULL &&
            fprintf(fo, "Resent-Message-ID: <%s>\n", cp) < 0)
         goto jleave;
   }

   if((mdn = n_UNCONST(check_from_and_sender(fromfield, senderfield))) == NIL)
      goto jleave;
   if (!_check_dispo_notif(mdn, NULL, fo))
      goto jleave;

   /* Write the original headers */
   while (cnt > 0) {
      if(fgetline(&buf, &bufsize, &cnt, &c, fi, FAL0) == NIL){
         if(ferror(fi))
            goto jleave;
         break;
      }
      if (su_cs_cmp_case_n("status:", buf, 7) &&
            su_cs_cmp_case_n("disposition-notification-to:", buf, 28) &&
            !is_head(buf, c, FAL0)){
         if(fwrite(buf, sizeof *buf, c, fo) != c)
            goto jleave;
      }
      if (cnt > 0 && *buf == '\n')
         break;
   }

   /* Write the message body */
   while(cnt > 0){
      if(fgetline(&buf, &bufsize, &cnt, &c, fi, FAL0) == NIL){
         if(ferror(fi))
            goto jleave;
         break;
      }
      if(cnt == 0 && *buf == '\n')
         break;
      if(fwrite(buf, sizeof *buf, c, fo) != c)
         goto jleave;
   }

   rv = TRU1;
jleave:
   mx_fs_linepool_release(buf, bufsize);

   if(!rv)
      n_err(_("infix_resend: creation of temporary mail file failed\n"));
   NYD_OU;
   return rv;
}

FL boole
mx_sendout_mta_url(struct mx_url *urlp){
   /* TODO In v15 this should simply be url_creat(,ok_vlook(mta).
    * TODO Register a "test" protocol handler, and if the protocol ends up
    * TODO as file and the path is "test", then this is also test.
    * TODO I.e., CPROTO_FILE and CPROTO_TEST.  Until then this is messy */
   char const *mta;
   boole rv;
   NYD_IN;

   rv = FAL0;

   if((mta = ok_vlook(smtp)) == NIL){
      boole issnd;
      u16 pno;
      char const *proto;

      mta = ok_vlook(mta);

      if((proto = ok_vlook(sendmail)) != NIL){ /* v15-compat */
         n_OBSOLETE(_("please use *mta* instead of *sendmail*"));
         /* But use it in favour of default value, then */
         if(!su_cs_cmp(mta, VAL_MTA))
            mta = proto;
      }

      if(su_cs_find_c(mta, ':') == NIL){
         if(su_cs_cmp(mta, "test")){
            pno = 0;
            goto jisfile;
         }
         pno = U16_MAX;
         goto jistest;
      }else if((proto = mx_url_servbyname(mta, &pno, &issnd)) == NIL){
         goto jemta;
      }else if(*proto == '\0'){
         if(pno == 0)
            mta += sizeof("file://") -1;
         else{
            /* test -> stdout, test://X -> X */
            ASSERT(pno == U16_MAX);
jistest:
            mta += sizeof("test") -1;
            if(mta[0] == ':' && mta[1] == '/' && mta[2] == '/')
               mta += 3;
         }
jisfile:
         su_mem_set(urlp, 0, sizeof *urlp);
         urlp->url_input = mta;
         urlp->url_portno = pno;
         urlp->url_cproto = CPROTO_NONE;
         rv = TRUM1;
         goto jleave;
      }else if(!issnd)
         goto jemta;
   }else{
      n_OBSOLETE(_("please do not use *smtp*, instead "
         "assign a smtp:// URL to *mta*!"));
      /* For *smtp* the smtp:// protocol was optional; be simple: do not check
       * that *smtp* is misused with file:// or so */
      if(mx_url_servbyname(mta, NIL, NIL) == NIL)
         mta = savecat("smtp://", mta);
   }

#ifdef mx_HAVE_NET
   rv = mx_url_parse(urlp, CPROTO_SMTP, mta);
#endif

   if(!rv)
jemta:
      n_err(_("*mta*: invalid or unsupported value: %s\n"),
         n_shexp_quote_cp(mta, FAL0));
jleave:
   NYD_OU;
   return rv;
}

FL int
n_mail(enum n_mailsend_flags msf, struct mx_name *to, struct mx_name *cc,
   struct mx_name *bcc, char const *subject, struct mx_attachment *attach,
   char const *quotefile)
{
   struct header head;
   struct str in, out;
   boole fullnames;
   NYD_IN;

   su_mem_set(&head, 0, sizeof head);

   /* The given subject may be in RFC1522 format. */
   if (subject != NULL) {
      in.s = n_UNCONST(subject);
      in.l = su_cs_len(subject);
      if(mx_mime_display_from_header(&in, &out,
            /* TODO ???_ISPRINT |*/ mx_MIME_DISPLAY_ICONV))
         head.h_subject = savestrbuf(out.s, out.l);
   }

   fullnames = ok_blook(fullnames);

   head.h_flags = HF_CMD_mail;
   if((head.h_to = to) != NULL){
      if(!fullnames)
         head.h_to = to = a_sendout_fullnames_cleanup(to);
      head.h_mailx_raw_to = n_namelist_dup(to, to->n_type);
   }
   if((head.h_cc = cc) != NULL){
      if(!fullnames)
         head.h_cc = cc = a_sendout_fullnames_cleanup(cc);
      head.h_mailx_raw_cc = n_namelist_dup(cc, cc->n_type);
   }
   if((head.h_bcc = bcc) != NULL){
      if(!fullnames)
         head.h_bcc = bcc = a_sendout_fullnames_cleanup(bcc);
      head.h_mailx_raw_bcc = n_namelist_dup(bcc, bcc->n_type);
   }

   head.h_attach = attach;

   /* TODO n_exit_status only!!?? */n_mail1(msf, &head, NIL, quotefile, FAL0);

   NYD_OU;
   return 0;
}

FL int
c_sendmail(void *v)
{
   int rv;
   NYD_IN;

   rv = a_sendout_sendmail(v, n_MAILSEND_NONE);
   NYD_OU;
   return rv;
}

FL int
c_Sendmail(void *v)
{
   int rv;
   NYD_IN;

   rv = a_sendout_sendmail(v, n_MAILSEND_RECORD_RECIPIENT);
   NYD_OU;
   return rv;
}

FL enum okay
n_mail1(enum n_mailsend_flags msf, struct header *hp, struct message *quote,
   char const *quotefile, boole local)
{
#ifdef mx_HAVE_NET
   struct mx_cred_ctx cc;
#endif
   struct mx_url url, *urlp = &url;
   struct n_sigman sm;
   struct mx_send_ctx sctx;
   struct mx_name *to;
   boole dosign, mta_isexe;
   FILE * volatile mtf, *nmtf;
   enum okay volatile rv;
   NYD_IN;

   _sendout_error = FAL0;
   __sendout_ident = NULL;
   n_pstate_err_no = su_ERR_INVAL;
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

   temporary_compose_mode_hook_control(TRU1, local);

   /* Collect user's mail from standard input.  Get the result as mtf */
   mtf = n_collect(msf, hp, quote, quotefile, &_sendout_error);
   if (mtf == NULL)
      goto jleave;
   /* TODO All custom headers should be joined here at latest
    * TODO In fact that should happen before we enter compose mode, so that the
    * TODO -C headers can be managed (removed etc.) via ~^, too, but the
    * TODO *customhdr* ones are fixated at this very place here, no sooner! */

   /* */
#ifdef mx_HAVE_PRIVACY
   dosign = TRUM1;
#else
   dosign = FAL0;
#endif

   if(n_psonce & n_PSO_INTERACTIVE){
#ifdef mx_HAVE_PRIVACY
      if(ok_blook(asksign))
         dosign = mx_tty_yesorno(_("Sign this message"), TRU1);
#endif
   }

   if(fsize(mtf) == 0){
      if(n_poption & n_PO_E_FLAG){
         n_pstate_err_no = su_ERR_NONE;
         rv = OKAY;
         goto jleave;
      }

      if(hp->h_subject == NULL)
         n_err(_("No message, no subject; hope that's ok\n"));
      else if(ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         n_err(_("Null message body; hope that's ok\n"));
   }

#ifdef mx_HAVE_PRIVACY
   if(dosign == TRUM1)
      dosign = mx_privacy_sign_is_desired(); /* TODO USER@HOST, *from*++!!! */
#endif

   /* XXX Update time_current again; once n_collect() offers editing of more
    * XXX headers, including Date:, this must only happen if Date: is the
    * XXX same that it was before n_collect() (e.g., postponing etc.).
    * XXX But *do* update otherwise because the mail seems to be backdated
    * XXX if the user edited some time, which looks odd and it happened
    * XXX to me that i got mis-dated response mails due to that... */
   time_current_update(&time_current, TRU1);

   /* TODO hrmpf; the MIME/send layer rewrite MUST address the init crap:
    * TODO setup the header ONCE; note this affects edit.c, collect.c ...,
    * TODO but: offer a hook that rebuilds/expands/checks/fixates all
    * TODO header fields ONCE, call that ONCE after user editing etc. has
    * TODO completed (one edit cycle) */

   if(!(mta_isexe = mx_sendout_mta_url(urlp)))
      goto jfail_dead;
   mta_isexe = (mta_isexe != TRU1);

   /* Take the user names from the combined to and cc lists and do all the
    * alias processing.  The POSIX standard says:
    *   The names shall be substituted when alias is used as a recipient
    *   address specified by the user in an outgoing message (that is,
    *   other recipients addressed indirectly through the reply command
    *   shall not be substituted in this manner).
    * XXX S-nail thus violates POSIX, as has been pointed out correctly by
    * XXX Martin Neitzel, but logic and usability of POSIX standards is
    * XXX sometimes disputable: go for user friendliness */
   to = n_namelist_vaporise_head(hp, TRU1, ((quote != NULL &&
            (msf & n_MAILSEND_IS_FWD) == 0) || !ok_blook(posix)),
         (EACM_NORMAL | EACM_DOMAINCHECK |
            (mta_isexe ? EACM_NONE : EACM_NONAME | EACM_NONAME_OR_FAIL)),
         &_sendout_error);

   if(_sendout_error < 0){
      n_err(_("Some addressees were classified as \"hard error\"\n"));
      n_pstate_err_no = su_ERR_PERM;
      goto jfail_dead;
   }
   if(to == NULL){
      n_err(_("No recipients specified\n"));
      n_pstate_err_no = su_ERR_DESTADDRREQ;
      goto jfail_dead;
   }

   /* */
   su_mem_set(&sctx, 0, sizeof sctx);
   sctx.sc_hp = hp;
   sctx.sc_to = to;
   sctx.sc_input = mtf;
   sctx.sc_urlp = urlp;
#ifdef mx_HAVE_NET
   sctx.sc_credp = &cc;
#endif

   if((dosign || count_nonlocal(to) > 0) &&
         !a_sendout_setup_creds(&sctx, (dosign > 0))){
      /* TODO saving $DEAD and recovering etc is not yet well defined */
      n_pstate_err_no = su_ERR_INVAL;
      goto jfail_dead;
   }

   /* 'Bit ugly kind of control flow until we find a charset that does it */
   /* C99 */{
      boole any;

      for(any = FAL0, mx_mime_charset_iter_reset(hp->h_charset);;
            any = TRU1, mx_mime_charset_iter_next()){
         int err;
         boole volatile force;

         force = FAL0;
         if(!mx_mime_charset_iter_is_valid() &&
               (!any || !(force = ok_blook(mime_force_sendout))))
            err = su_ERR_NOENT;
         else if((nmtf = a_sendout_infix(hp, mtf, dosign, force)) != NULL)
            break;
#ifdef mx_HAVE_ICONV
         else if((err = n_iconv_err_no) == su_ERR_ILSEQ ||
               err == su_ERR_INVAL || err == su_ERR_NOENT){
            rewind(mtf);
            continue;
         }
#endif

         n_perr(_("Cannot find a usable character set to encode message"),
            err);
         n_pstate_err_no = su_ERR_NOTSUP;
         goto jfail_dead;
      }
   }
   mtf = nmtf;

   /*  */
#ifdef mx_HAVE_PRIVACY
   if(dosign){
      if((nmtf = mx_privacy_sign(mtf, sctx.sc_signer.s)) == NIL)
         goto jfail_dead;
      mx_fs_close(mtf);
      mtf = nmtf;
   }
#endif

   /* TODO truly - i still don't get what follows: (1) we deliver file
    * TODO and pipe addressees, (2) we mightrecord() and (3) we transfer
    * TODO even if (1) savedeadletter() etc.  To me this doesn't make sense? */

   /* C99 */{
      u32 cnt;
      boole b;

      /* Deliver pipe and file addressees */
      b = (ok_blook(record_files) && count(to) > 0);
      to = a_sendout_file_a_pipe(to, mtf, &_sendout_error);

      if (_sendout_error)
         savedeadletter(mtf, FAL0);

      to = elide(to); /* XXX only to drop GDELs due a_sendout_file_a_pipe()! */
      cnt = count(to);

      if(((msf & n_MAILSEND_RECORD_RECIPIENT) || b || cnt > 0) &&
            !a_sendout_mightrecord(mtf,
               (msf & n_MAILSEND_RECORD_RECIPIENT ? to : NIL), FAL0))
         goto jleave;
      if (cnt > 0) {
         sctx.sc_hp = hp;
         sctx.sc_to = to;
         sctx.sc_input = mtf;
         b = FAL0;
         if(a_sendout_transfer(&sctx, FAL0, &b))
            rv = OKAY;
         else if(b && _sendout_error == 0){
            _sendout_error = b;
            savedeadletter(mtf, FAL0);
         }
      } else if (!_sendout_error)
         rv = OKAY;
   }

   n_sigman_cleanup_ping(&sm);
jleave:
   if(mtf != NIL){
      char const *cp;

      mx_fs_close(mtf);

      if((cp = ok_vlook(on_compose_cleanup)) != NIL)
         temporary_compose_mode_hook_call(cp);
   }

   temporary_compose_mode_hook_control(FAL0, FAL0);

   if(_sendout_error){
      n_psonce |= n_PSO_SEND_ERROR;
      n_exit_status |= n_EXIT_SEND_ERROR;
   }
   if(rv == OKAY)
      n_pstate_err_no = su_ERR_NONE;

   NYD_OU;
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
   struct tm *tmptr;
   int tzdiff_hour, tzdiff_min, rv;
   NYD2_IN;

   tmptr = &time_current.tc_local;

   tzdiff_min = S(int,n_time_tzdiff(time_current.tc_time, NIL, tmptr));
   tzdiff_min /= su_TIME_MIN_SECS;
   tzdiff_hour = tzdiff_min / su_TIME_HOUR_MINS;
   tzdiff_min %= su_TIME_HOUR_MINS;

   rv = fprintf(fo, "%s: %s, %02d %s %04d %02d:%02d:%02d %+05d\n",
         field,
         su_time_weekday_names_abbrev[tmptr->tm_wday],
         tmptr->tm_mday, su_time_month_names_abbrev[tmptr->tm_mon],
         tmptr->tm_year + 1900, tmptr->tm_hour,
         tmptr->tm_min, tmptr->tm_sec,
         tzdiff_hour * 100 + tzdiff_min);
   if(rv < 0)
      rv = -1;

   NYD2_OU;
   return rv;
}

FL boole
n_puthead(boole nosend_msg, struct header *hp, FILE *fo, enum gfield w,
   enum sendaction action, enum conversion convert, char const *contenttype,
   char const *charset)
{
#define a_SENDOUT_PUT_CC_BCC_FCC() \
do{\
   if((w & GCC) && (hp->h_cc != NIL || nosend_msg == TRUM1)){\
      if(!a_sendout_put_addrline("Cc:", hp->h_cc, fo, saf))\
         goto jleave;\
      ++gotcha;\
   }\
   if((w & GBCC) && (hp->h_bcc != NIL || nosend_msg == TRUM1)){\
      if(!a_sendout_put_addrline("Bcc:", hp->h_bcc, fo, saf))\
         goto jleave;\
      ++gotcha;\
   }\
   if((w & GBCC_IS_FCC) && nosend_msg){\
      for(np = hp->h_fcc; np != NIL; np = np->n_flink){\
         if(fprintf(fo, "Fcc: %s\n", np->n_name) < 0)\
            goto jleave;\
         ++gotcha;\
      }\
   }\
}while(0)

   char const *addr;
   uz gotcha;
   int stealthmua;
   boole nodisp;
   enum a_sendout_addrline_flags saf;
   struct mx_name *np, *fromasender, *mft, **mftp;
   boole rv;
   NYD_IN;

   mftp = NIL;
   fromasender = mft = NIL;
   rv = FAL0;

   if(nosend_msg && (hp->h_flags & HF_COMPOSE_MODE) &&
         !(hp->h_flags & HF_USER_EDITED)){
      if(fputs(_("# Message will be discarded unless file is saved\n"),
            fo) == EOF)
         goto jleave;
   }

   if ((addr = ok_vlook(stealthmua)) != NULL)
      stealthmua = !su_cs_cmp(addr, "noagent") ? -1 : 1;
   else
      stealthmua = 0;
   gotcha = 0;
   nodisp = (action != SEND_TODISP);
   saf = (w & (GCOMMA | GFILES)) | (nodisp ? a_SENDOUT_AL_DOMIME : 0);
   if(nosend_msg)
      saf |= a_SENDOUT_AL_INC_INVADDR;

   if (w & GDATE)
      mkdate(fo, "Date"), ++gotcha;
   if (w & GIDENT) {
      if (hp->h_from == NULL || hp->h_sender == NULL)
         setup_from_and_sender(hp);

      if(hp->h_from != NIL){
         if(hp->h_author != NIL){
            if(!a_sendout_put_addrline("Author:", hp->h_author, fo, saf))
               goto jleave;
         }else if(!a_sendout_put_addrline("Author:", hp->h_from, fo, saf))
            goto jleave;
         if(!a_sendout_put_addrline("From:", hp->h_from, fo, saf))
            goto jleave;
         ++gotcha;
      }

      if (hp->h_sender != NULL) {
         if (!a_sendout_put_addrline("Sender:", hp->h_sender, fo, saf))
            goto jleave;
         ++gotcha;
      }

      fromasender = n_UNCONST(check_from_and_sender(hp->h_from, hp->h_sender));
      if (fromasender == NULL)
         goto jleave;
      /* Note that fromasender is (NULL,) 0x1 or real sender here */
   }

   /* M-F-T: check this now, and possibly place us in Cc: */
   if((w & GIDENT) && !nosend_msg){
      /* Mail-Followup-To: TODO factor out this huge block of code.
       * TODO Also, this performs multiple expensive list operations, which
       * TODO hopefully can be heavily optimized later on! */
      /* Place ourselves in there if any non-subscribed list is an addressee */
      if((hp->h_flags & HF_LIST_REPLY) || hp->h_mft != NIL ||
            ok_blook(followup_to)){
         enum{
            a_HADMFT = 1u<<(HF__NEXT_SHIFT + 0),
            a_WASINMFT = 1u<<(HF__NEXT_SHIFT + 1),
            a_ANYLIST = 1u<<(HF__NEXT_SHIFT + 2),
            a_OTHER = 1u<<(HF__NEXT_SHIFT + 3)
         };
         struct mx_name *x;
         u32 f;

         f = hp->h_flags | (hp->h_mft != NIL ? a_HADMFT : 0);
         if(f & a_HADMFT){
            /* Detect whether we were part of the former MFT:.
             * Throw away MFT: if we were the sole member (kidding) */
            hp->h_mft = mft = elide(hp->h_mft);
            mft = mx_alternates_remove(n_namelist_dup(mft, GNONE), FAL0);
            if(mft == NIL)
               f ^= a_HADMFT;
            else for(x = hp->h_mft; x != NIL;
                  x = x->n_flink, mft = mft->n_flink){
               if(mft == NIL){
                  f |= a_WASINMFT;
                  break;
               }
            }
         }

         /* But for that, remove all incarnations of ourselves first.
          * TODO It is total crap that we have alternates_remove(), is_myname()
          * TODO or whatever; these work only with variables, not with data
          * TODO that is _currently_ in some header fields!!!  v15.0: complete
          * TODO rewrite, object based, lazy evaluated, on-the-fly marked.
          * TODO then this should be a really cheap thing in here... */
         np = elide(mx_alternates_remove(cat(
               n_namelist_dup(hp->h_to, GEXTRA | GFULL),
               n_namelist_dup(hp->h_cc, GEXTRA | GFULL)), FAL0));
         addr = hp->h_list_post;
         mft = NIL;
         mftp = &mft;

         while((x = np) != NIL){
            s8 ml;

            np = np->n_flink;

            /* Automatically make MLIST_KNOWN List-Post: address */
            /* XXX mx_mlist_query_mp()?? */
            if(((ml = mx_mlist_query(x->n_name, FAL0)) == mx_MLIST_OTHER ||
                     ml == mx_MLIST_POSSIBLY) &&
                  addr != NIL && !su_cs_cmp_case(addr, x->n_name))
               ml = mx_MLIST_KNOWN;

            /* Any non-subscribed list?  Add ourselves */
            switch(ml){
            case mx_MLIST_KNOWN:
               f |= HF_MFT_SENDER;
               /* FALLTHRU */
            case mx_MLIST_SUBSCRIBED:
               f |= a_ANYLIST;
               goto j_mft_add;
            case mx_MLIST_OTHER:
            case mx_MLIST_POSSIBLY:
               f |= a_OTHER;
               if(!(f & HF_LIST_REPLY)){
j_mft_add:
                  if(!is_addr_invalid(x,
                        EACM_STRICT | EACM_NOLOG | EACM_NONAME)){
                     x->n_blink = *mftp;
                     x->n_flink = NIL;
                     *mftp = x;
                     mftp = &x->n_flink;
                  } /* XXX write some warning?  if verbose?? */
                  continue;
               }
               /* And if this is a reply that honoured a M-F-T: header then
                * we'll also add all members of the original M-F-T: that are
                * still addressed by us, regardless of other circumstances */
               /* TODO If the user edited this list, then we should include
                * TODO whatever she did therewith, even if _LIST_REPLY! */
               else if(f & a_HADMFT){
                  struct mx_name *ox;

                  for(ox = hp->h_mft; ox != NIL; ox = ox->n_flink)
                     if(!su_cs_cmp_case(ox->n_name, x->n_name))
                        goto j_mft_add;
               }
               break;
            }
         }

         if((f & (a_ANYLIST | a_HADMFT)) && mft != NIL){
            if(((f & HF_MFT_SENDER) ||
                     ((f & (a_ANYLIST | a_HADMFT)) == a_HADMFT)) &&
                  (np = fromasender) != NIL && np != R(struct mx_name*,0x1)){
               *mftp = ndup(np, (np->n_type & ~GMASK) | GEXTRA | GFULL);

               /* Place ourselves in the Cc: if we will be a member of M-F-T:,
                * and we are not subscribed (and are no addressee yet)? */
               /* TODO This entire block is much to expensive and should
                * TODO be somewhere else (like name_unite(), or so) */
               if(ok_blook(followup_to_add_cc)){
                  struct mx_name **npp;

                  np = ndup(np, (np->n_type & ~GMASK) | GCC | GFULL);
                  np = cat(cat(hp->h_to, hp->h_cc), np);
                  np = mx_alternates_remove(np, TRU1);
                  np = elide(np);
                  hp->h_to = hp->h_cc = NIL;
                  for(; np != NIL; np = np->n_flink){
                     switch(np->n_type & (GDEL | GMASK)){
                     case GTO: npp = &hp->h_to; break;
                     case GCC: npp = &hp->h_cc; break;
                     default: continue;
                     }
                     *npp = cat(*npp, ndup(np, np->n_type | GFULL));
                  }
               }
            }
         }else
            mft = NIL;
      }
   }

   if(nosend_msg && (hp->h_flags & HF_COMPOSE_MODE) &&
         !(hp->h_flags & HF_USER_EDITED)){
      if(fputs(_("# To:, Cc: and Bcc: support a ?single modifier: "
            "To?: exa, <m@ple>\n"), fo) == EOF)
         goto jleave;
   }

   if((w & GTO) && (hp->h_to != NULL || (nosend_msg &&
         (hp->h_flags & HF_COMPOSE_MODE)))) {
      if(!a_sendout_put_addrline("To:", hp->h_to, fo, saf))
         goto jleave;
      ++gotcha;
   }

   if(!ok_blook(bsdcompat) && !ok_blook(bsdorder))
      a_SENDOUT_PUT_CC_BCC_FCC();

   if((w & GSUBJECT) && ((hp->h_subject != NIL && *hp->h_subject != '\0') ||
         (nosend_msg && (hp->h_flags & HF_COMPOSE_MODE)))){
      if(fwrite("Subject: ", sizeof(char), 9, fo) != 9 ||
            (hp->h_subject != NIL &&
             mx_xmime_write(hp->h_subject, su_cs_len(hp->h_subject), fo,
               (!nodisp ? CONV_NONE : CONV_TOHDR),
               (!nodisp ? mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT
                  : mx_MIME_DISPLAY_ICONV), NIL,NIL) < 0))
         goto jleave;
      ++gotcha;
      putc('\n', fo);
   }

   if (ok_blook(bsdcompat) || ok_blook(bsdorder))
      a_SENDOUT_PUT_CC_BCC_FCC();

   if ((w & GMSGID) && stealthmua <= 0 &&
         (addr = a_sendout_random_id(hp, TRU1)) != NULL) {
      if (fprintf(fo, "Message-ID: <%s>\n", addr) < 0)
         goto jleave;
      ++gotcha;
   }

   if(w & (GREF | GREF_IRT)){
      if((np = hp->h_in_reply_to) == NULL)
         hp->h_in_reply_to = np = n_header_setup_in_reply_to(hp);
      if(np != NULL){
         if(nosend_msg && (hp->h_flags & HF_COMPOSE_MODE) &&
               !(hp->h_flags & HF_USER_EDITED)){
            if(fputs(_("# Removing or modifying In-Reply-To: "
                     "breaks the old, and starts a new thread.\n"
                  "# Assigning hyphen-minus - creates a thread of only the "
                     "replied-to message\n"), fo) == EOF)
               goto jleave;
         }
         if(!a_sendout_put_addrline("In-Reply-To:", np, fo, 0))
            goto jleave;
         ++gotcha;
      }

      if((w & GREF) && (np = hp->h_ref) != NULL){
         if(!a_sendout_put_addrline("References:", np, fo, 0))
            goto jleave;
         ++gotcha;
      }
   }

   if (w & GIDENT) {
      /* Reply-To:.  Be careful not to destroy a possible user input, duplicate
       * the list first.. TODO it is a terrible codebase.. */
      if((np = hp->h_reply_to) != NULL)
         np = n_namelist_dup(np, np->n_type);
      else{
         char const *v15compat;

         if((v15compat = ok_vlook(replyto)) != NULL)
            n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
         if((addr = ok_vlook(reply_to)) == NULL)
            addr = v15compat;
         np = lextract(addr, GEXTRA |
               (ok_blook(fullnames) ? GFULL | GSKIN : GSKIN));
      }
      if (np != NULL &&
            (np = elide(
               checkaddrs(usermap(np, TRU1), EACM_STRICT | EACM_NOLOG,
                  NULL))) != NULL) {
         if (!a_sendout_put_addrline("Reply-To:", np, fo, saf))
            goto jleave;
         ++gotcha;
      }
   }

   if((w & GIDENT) && !nosend_msg){
      if(mft != NIL){
         if(!a_sendout_put_addrline("Mail-Followup-To:", mft, fo, saf))
            goto jleave;
         ++gotcha;
      }

      if(!_check_dispo_notif(fromasender, hp, fo))
         goto jleave;
   }

   if ((w & GUA) && stealthmua == 0) {
      if (fprintf(fo, "User-Agent: %s %s\n", n_uagent,
            (su_state_has(su_STATE_REPRODUCIBLE)
               ? su_reproducible_build : ok_vlook(version))) < 0)
         goto jleave;
      ++gotcha;
   }

   /* Custom headers, as via -C and *customhdr* TODO JOINED AFTER COMPOSE! */
   if(!nosend_msg){
      struct n_header_field *chlp[2], *hfp;
      u32 i;

      chlp[0] = n_poption_arg_C;
      chlp[1] = n_customhdr_list;

      for(i = 0; i < NELEM(chlp); ++i)
         if((hfp = chlp[i]) != NULL){
            if(!_sendout_header_list(fo, hfp, nodisp))
               goto jleave;
            ++gotcha;
         }
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

   /* We don't need MIME unless.. we need MIME?! */
   if ((w & GMIME) && ((n_pstate & n_PS_HEADER_NEEDED_MIME) ||
         hp->h_attach != NULL ||
         ((n_poption & n_PO_Mm_FLAG) && n_poption_arg_Mm != NULL) ||
         convert != CONV_7BIT || !n_iconv_name_is_ascii(charset))) {
      ++gotcha;
      if (fputs("MIME-Version: 1.0\n", fo) == EOF)
         goto jleave;
      if (hp->h_attach != NULL) {
         _sendout_boundary = mx_mime_param_boundary_create();/*TODO carrier*/
         if (fprintf(fo,
               "Content-Type: multipart/mixed;\n boundary=\"%s\"\n",
               _sendout_boundary) < 0)
            goto jleave;
      } else {
         if(a_sendout_put_ct(fo, contenttype, charset) < 0 ||
               a_sendout_put_cte(fo, convert) < 0)
            goto jleave;
      }
   }

   if (gotcha && (w & GNL))
      if (putc('\n', fo) == EOF)
         goto jleave;

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
#undef a_SENDOUT_PUT_CC_BCC_FCC
}

FL enum okay
n_resend_msg(struct message *mp, struct mx_url *urlp, struct header *hp,
   boole add_resent, boole local)
{
#ifdef mx_HAVE_NET
   struct mx_cred_ctx cc;
#endif
   struct n_sigman sm;
   struct mx_send_ctx sctx;
   FILE * volatile ibuf, *nfo, * volatile nfi;
   struct mx_fs_tmp_ctx *fstcp;
   struct mx_name *to;
   enum okay volatile rv;
   NYD_IN;

   _sendout_error = FAL0;
   __sendout_ident = NULL;
   n_pstate_err_no = su_ERR_INVAL;

   rv = STOP;
   to = hp->h_to;
   ASSERT(hp->h_cc == NIL);
   ASSERT(hp->h_bcc == NIL);
   nfi = ibuf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL) {
   case 0:
      break;
   default:
      goto jleave;
   }

   /* Update some globals we likely need first */
   time_current_update(&time_current, TRU1);

   if((nfo = mx_fs_tmp_open(NIL, "resend", (mx_FS_O_WRONLY | mx_FS_O_HOLDSIGS),
            &fstcp)) == NIL){
      _sendout_error = TRU1;
      n_perr(_("resend_msg: temporary mail file"), 0);
      n_pstate_err_no = su_ERR_IO;
      goto jleave;
   }

   if((nfi = mx_fs_open(fstcp->fstc_filename, mx_FS_O_RDONLY)) == NIL){
      n_perr(fstcp->fstc_filename, 0);
      n_pstate_err_no = su_ERR_IO;
   }

   mx_fs_tmp_release(fstcp);

   if(nfi == NIL)
      goto jerr_o;

   if((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL){
      n_pstate_err_no = su_ERR_IO;
      goto jerr_io;
   }

   temporary_compose_mode_hook_control(TRU1, local);

   /* C99 */{
      char const *cp;

      if((cp = ok_vlook(on_resend_enter)) != NIL){
         /*setup_from_and_sender(hp);*/
         temporary_compose_mode_hook_call(cp);
      }
   }

   su_mem_set(&sctx, 0, sizeof sctx);
   sctx.sc_to = to;
   sctx.sc_input = nfi;
   sctx.sc_urlp = urlp;
#ifdef mx_HAVE_NET
   sctx.sc_credp = &cc;
#endif

   /* All the complicated address massage things happened in the callee(s) */
   if(!_sendout_error &&
         count_nonlocal(to) > 0 && !a_sendout_setup_creds(&sctx, FAL0)){
      /* ..wait until we can write DEAD */
      n_pstate_err_no = su_ERR_INVAL;
      _sendout_error = -1;
   }

   if(!a_sendout_infix_resend(ibuf, nfo, mp, to, add_resent)){
jfail_dead:
      savedeadletter(nfi, TRU1);
      n_err(_("... message not sent\n"));
jerr_io:
      mx_fs_close(nfi);
      nfi = NIL;
jerr_o:
      mx_fs_close(nfo);
      _sendout_error = TRU1;
      goto jleave;
   }

   if(_sendout_error < 0)
      goto jfail_dead;

   mx_fs_close(nfo);
   rewind(nfi);

   /* C99 */{
      boole b, c;

      /* Deliver pipe and file addressees */
      b = (ok_blook(record_files) && count(to) > 0);
      to = a_sendout_file_a_pipe(to, nfi, &_sendout_error);

      if(_sendout_error)
         savedeadletter(nfi, FAL0);

      to = elide(to); /* XXX only to drop GDELs due a_sendout_file_a_pipe()! */
      c = (count(to) > 0);

      if(b || c){
         if(!ok_blook(record_resent) || a_sendout_mightrecord(nfi, NIL, TRU1)){
            sctx.sc_to = to;
            /*sctx.sc_input = nfi;*/
            if(!c || a_sendout_transfer(&sctx, TRU1, &b))
               rv = OKAY;
            else if(b && _sendout_error == 0){
               _sendout_error = b;
               savedeadletter(nfi, FAL0);
            }
         }
      }else if(!_sendout_error)
         rv = OKAY;
   }

   n_sigman_cleanup_ping(&sm);
jleave:
   if(nfi != NIL){
      char const *cp;

      mx_fs_close(nfi);

      if(ibuf != NIL){
         if((cp = ok_vlook(on_resend_cleanup)) != NIL)
            temporary_compose_mode_hook_call(cp);

         temporary_compose_mode_hook_control(FAL0, FAL0);
      }
   }

   if(_sendout_error){
      n_psonce |= n_PSO_SEND_ERROR;
      n_exit_status |= n_EXIT_SEND_ERROR;
   }
   if(rv == OKAY)
      n_pstate_err_no = su_ERR_NONE;

   NYD_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

FL void
savedeadletter(FILE *fp, boole fflush_rewind_first){
   struct n_string line;
   int c;
   enum {a_NONE, a_INIT = 1<<0, a_BODY = 1<<1, a_NL = 1<<2} flags;
   ul bytes, lines;
   FILE *dbuf;
   char const *cp, *cpq;
   NYD_IN;

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

   if(n_poption & n_PO_D){
      n_err(_(">>> Would (try to) write $DEAD %s\n"), cpq);
      goto jleave;
   }

   if((dbuf = mx_fs_open(cp, (mx_FS_O_WRONLY | mx_FS_O_CREATE | mx_FS_O_TRUNC))
            ) == NIL || !mx_file_lock(fileno(dbuf), (mx_FILE_LOCK_MODE_TEXCL |
         mx_FILE_LOCK_MODE_RETRY))){
      n_perr(_("Cannot save to $DEAD"), 0);

      if(dbuf != NIL)
         mx_fs_close(dbuf);
      goto jleave;
   }

   fprintf(n_stdout, "%s ", cpq);
   fflush(n_stdout);

   /* TODO savedeadletter() non-conforming: should check whether we have any
    * TODO headers, if not we need to place "something", anything will do.
    * TODO MIME is completely missing, we use MBOXO quoting!!  Yuck.
    * TODO I/O error handling missing.  Yuck! */
   n_string_reserve(n_string_creat_auto(&line), 2 * SEND_LINESIZE);
   bytes = (ul)fprintf(dbuf, "From %s %s",
         ok_vlook(LOGNAME), time_current.tc_ctime);
   lines = 1;
   for(flags = a_NONE, c = '\0'; c != EOF; bytes += line.s_len, ++lines){
      n_string_trunc(&line, 0);
      while((c = getc(fp)) != EOF && c != '\n')
         n_string_push_c(&line, c);

      /* TODO It may be that we have only some plain text.  It may be that we
       * TODO have a complete MIME encoded message.  We don't know, and we
       * TODO have no usable mechanism to dig it!!  We need v15! */
      if(!(flags & a_INIT)){
         uz i;

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
         if(line.s_len >= 5 && !su_mem_cmp(line.s_dat, "From ", 5))
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

   mx_fs_close(dbuf);
   fprintf(n_stdout, "%lu/%lu\n", lines, bytes);
   fflush(n_stdout);

   rewind(fp);
jleave:
   NYD_OU;
}

#ifdef mx_HAVE_REGEX
FL boole
mx_sendout_temporary_digdump(FILE *ofp, struct mimepart *mp,
      struct header *envelope_or_nil, boole is_main_mp){
   /* It is a terrible hack; we need a DOM and just dump_to_wire() */
   FILE *ifp;
   uz linesize, cnt, len, i;
   char *linedat, *cp;
   boole rv;
   NYD2_IN;

   rv = FAL0;
   mx_fs_linepool_aquire(&linedat, &linesize);

   if((ifp = setinput(&mb, R(struct message*,mp), NEED_BODY)) == NIL)
      goto jleave;

   cnt = mp->m_size;
   while(fgetline(&linedat, &linesize, &cnt, &len, ifp, TRU1) != NIL){
      if(!is_main_mp){
         for(cp = linedat;;){
            if((i = fwrite(cp, 1, len, ofp)) == 0)
               break;
            len -= i;
            if(len == 0)
               break;
            cp += i;
         }
         if(len != 0)
            goto jleave;
      }else if(len == 1)
         is_main_mp = FAL0;

      if(envelope_or_nil != NIL) /* xxx From_ line aka postfix */
         break;
   }

   if(envelope_or_nil != NIL){
# define a_SENDOUT_PUT_CC_BCC_FCC() \
do{\
   if(hp->h_cc != NIL && !a_sendout_put_addrline("Cc:", hp->h_cc, ofp, saf))\
      goto jleave;\
   if(hp->h_bcc != NIL && !a_sendout_put_addrline("Bcc:", hp->h_bcc, ofp,saf))\
      goto jleave;\
}while(0)

      BITENUM_IS(u32,enum a_sendout_addrline_flags) const saf =
            a_SENDOUT_AL_DOMIME | a_SENDOUT_AL_COMMA;

      struct n_header_field *chlp[3], *hfp;
      struct mx_name *np;
      struct header *hp;

      hp = envelope_or_nil;
      envelope_or_nil = NIL;

      if(hp->h_from == NIL || hp->h_sender == NIL)
         setup_from_and_sender(hp);

      if((np = hp->h_author) != NIL &&
            !a_sendout_put_addrline("Author:", np, ofp, saf))
         goto jleave;
      if((np = hp->h_from) != NIL){
         if(hp->h_author == NIL &&
               !a_sendout_put_addrline("Author:", np, ofp, saf))
            goto jleave;
         if(!a_sendout_put_addrline("From:", np, ofp, saf))
            goto jleave;
      }
      if((np = hp->h_sender) != NIL &&
            !a_sendout_put_addrline("Sender:", np, ofp, saf))
         goto jleave;

      if((np = hp->h_to) != NIL &&
            !a_sendout_put_addrline("To:", np, ofp, saf))
         goto jleave;
      if(!ok_blook(bsdcompat) && !ok_blook(bsdorder))
         a_SENDOUT_PUT_CC_BCC_FCC();

      if(fwrite("Subject: ", sizeof(char), 9, ofp) != 9 ||
            (hp->h_subject != NIL &&
             mx_xmime_write(hp->h_subject,
               su_cs_len(hp->h_subject), ofp,
               CONV_TOHDR, mx_MIME_DISPLAY_ICONV, NIL,NIL) < 0))
         goto jleave;
      putc('\n', ofp);

      if(ok_blook(bsdcompat) || ok_blook(bsdorder))
         a_SENDOUT_PUT_CC_BCC_FCC();

      if((np = hp->h_in_reply_to) != NIL &&
            !a_sendout_put_addrline("In-Reply-To:", np, ofp, saf))
         goto jleave;
      if((np = hp->h_ref) != NIL &&
            !a_sendout_put_addrline("References:", np, ofp, saf))
         goto jleave;
      if((np = hp->h_reply_to) != NIL &&
            !a_sendout_put_addrline("Reply-To:", np, ofp, saf))
         goto jleave;

      chlp[0] = n_poption_arg_C;
      chlp[1] = n_customhdr_list;
      chlp[2] = hp->h_user_headers;
      for(i = 0; i < NELEM(chlp); ++i){
         if((hfp = chlp[i]) != NIL && !_sendout_header_list(ofp, hfp, TRU1))
            goto jleave;
      }

      if(putc('\n', ofp) == EOF)
         goto jleave;
# undef a_SENDOUT_PUT_CC_BCC_FCC
   }

   rv = TRU1;
jleave:
   mx_fs_linepool_release(linedat, linesize);

   NYD2_OU;
   return rv;
}
#endif /* mx_HAVE_REGEX */

#undef SEND_LINESIZE

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_SENDOUT
/* s-it-mode */
