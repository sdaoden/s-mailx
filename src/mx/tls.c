/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Generic TLS / S/MIME commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause TODO ISC (is taken from book!)
 */
/*
 * Copyright (c) 2002
 * Gunnar Ritter.  All rights reserved.
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
 * This product includes software developed by Gunnar Ritter
 * and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef su_FILE
#define su_FILE tls
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_TLS
#include <su/cs.h>
#include <su/mem.h>

#include "mx/cmd.h"
#include "mx/file-streams.h"
#include "mx/net-socket.h"
#include "mx/tty.h"
#include "mx/url.h"

/* TODO fake */
#include "su/code-in.h"

struct a_tls_verify_levels{
   char const tv_name[8];
   enum n_tls_verify_level tv_level;
};

/* Supported SSL/TLS verification methods: update manual on change! */
static struct a_tls_verify_levels const a_tls_verify_levels[] = {
   {"strict", n_TLS_VERIFY_STRICT},
   {"ask", n_TLS_VERIFY_ASK},
   {"warn", n_TLS_VERIFY_WARN},
   {"ignore", n_TLS_VERIFY_IGNORE}
};

FL void
n_tls_set_verify_level(struct mx_url const *urlp){
   uz i;
   char const *cp;
   NYD2_IN;

   n_tls_verify_level = n_TLS_VERIFY_ASK;

   if((cp = xok_vlook(tls_verify, urlp, OXM_ALL)) != NULL ||
         (cp = xok_vlook(ssl_verify, urlp, OXM_ALL)) != NULL){
      for(i = 0;;)
         if(!su_cs_cmp_case(a_tls_verify_levels[i].tv_name, cp)){
            n_tls_verify_level = a_tls_verify_levels[i].tv_level;
            break;
         }else if(++i >= NELEM(a_tls_verify_levels)){
            n_err(_("Invalid value of *tls-verify*: %s\n"), cp);
            break;
         }
   }
   NYD2_OU;
}

FL boole
n_tls_verify_decide(void){
   boole rv;
   NYD2_IN;

   switch(n_tls_verify_level){
   default:
   case n_TLS_VERIFY_STRICT:
      rv = FAL0;
      break;
   case n_TLS_VERIFY_ASK:
      rv = mx_tty_yesorno(NIL, FAL0);
      break;
   case n_TLS_VERIFY_WARN:
   case n_TLS_VERIFY_IGNORE:
      rv = TRU1;
      break;
   }
   NYD2_OU;
   return rv;
}

FL enum okay
smime_split(FILE *ip, FILE **hp, FILE **bp, long xcount, int keep)
{
   struct myline {
      struct myline  *ml_next;
      uz         ml_len;
      char           ml_buf[VFIELD_SIZE(0)];
   } *head, *tail;
   char *buf;
   uz bufsize, buflen, cnt;
   int c;
   enum okay rv = STOP;
   NYD_IN;

   if((*hp = mx_fs_tmp_open("smimeh", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL)
      goto jetmp;
   if((*bp = mx_fs_tmp_open("smimeb", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL){
      mx_fs_close(*hp);
jetmp:
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   head = tail = NULL;
   buf = n_alloc(bufsize = LINESIZE);
   cnt = (xcount < 0) ? fsize(ip) : xcount;

   while (fgetline(&buf, &bufsize, &cnt, &buflen, ip, 0) != NULL &&
         *buf != '\n') {
      if (!su_cs_cmp_case_n(buf, "content-", 8)) {
         if (keep)
            fputs("X-Encoded-", *hp);
         for (;;) {
            struct myline *ml = n_alloc(VSTRUCT_SIZEOF(struct myline, ml_buf
                  ) + buflen +1);
            if (tail != NULL)
               tail->ml_next = ml;
            else
               head = ml;
            tail = ml;
            ml->ml_next = NULL;
            ml->ml_len = buflen;
            su_mem_copy(ml->ml_buf, buf, buflen +1);
            if (keep)
               fwrite(buf, sizeof *buf, buflen, *hp);
            c = getc(ip);
            ungetc(c, ip);
            if (!su_cs_is_blank(c))
               break;
            fgetline(&buf, &bufsize, &cnt, &buflen, ip, 0);
         }
         continue;
      }
      fwrite(buf, sizeof *buf, buflen, *hp);
   }
   fflush_rewind(*hp);

   while (head != NULL) {
      fwrite(head->ml_buf, sizeof *head->ml_buf, head->ml_len, *bp);
      tail = head;
      head = head->ml_next;
      n_free(tail);
   }
   putc('\n', *bp);
   while (fgetline(&buf, &bufsize, &cnt, &buflen, ip, 0) != NULL)
      fwrite(buf, sizeof *buf, buflen, *bp);
   fflush_rewind(*bp);

   n_free(buf);
   rv = OKAY;
jleave:
   NYD_OU;
   return rv;
}

FL FILE *
smime_sign_assemble(FILE *hp, FILE *bp, FILE *tsp, char const *message_digest)
{
   char *boundary;
   int c, lastc = EOF;
   FILE *op;
   NYD_IN;

   if((op = mx_fs_tmp_open("smimea", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL){
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   while ((c = getc(hp)) != EOF) {
      if (c == '\n' && lastc == '\n')
         break;
      putc(c, op);
      lastc = c;
   }

   boundary = mime_param_boundary_create();
   fprintf(op, "Content-Type: multipart/signed;\n"
      " protocol=\"application/pkcs7-signature\"; micalg=%s;\n"
      " boundary=\"%s\"\n\n", message_digest, boundary);
   fprintf(op, "This is a S/MIME signed message.\n\n--%s\n", boundary);
   while ((c = getc(bp)) != EOF)
      putc(c, op);

   fprintf(op, "\n--%s\n", boundary);
   fputs("Content-Type: application/pkcs7-signature; name=\"smime.p7s\"\n"
      "Content-Transfer-Encoding: base64\n"
      "Content-Disposition: attachment; filename=\"smime.p7s\"\n"
      "Content-Description: S/MIME digital signature\n\n", op);
   while ((c = getc(tsp)) != EOF) {
      if (c == '-') {
         while ((c = getc(tsp)) != EOF && c != '\n');
         continue;
      }
      putc(c, op);
   }

   fprintf(op, "\n--%s--\n", boundary);

   mx_fs_close(hp);
   mx_fs_close(bp);
   mx_fs_close(tsp);

   fflush(op);
   if (ferror(op)) {
      n_perr(_("signed output data"), 0);
      mx_fs_close(op);
      op = NULL;
      goto jleave;
   }
   rewind(op);
jleave:
   NYD_OU;
   return op;
}

FL FILE *
smime_encrypt_assemble(FILE *hp, FILE *yp)
{
   FILE *op;
   int c, lastc = EOF;
   NYD_IN;

   if((op = mx_fs_tmp_open("smimee", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL){
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   while ((c = getc(hp)) != EOF) {
      if (c == '\n' && lastc == '\n')
         break;
      putc(c, op);
      lastc = c;
   }

   fputs("Content-Type: application/pkcs7-mime; name=\"smime.p7m\"\n"
      "Content-Transfer-Encoding: base64\n"
      "Content-Disposition: attachment; filename=\"smime.p7m\"\n"
      "Content-Description: S/MIME encrypted message\n\n", op);
   while ((c = getc(yp)) != EOF) {
      if (c == '-') {
         while ((c = getc(yp)) != EOF && c != '\n');
         continue;
      }
      putc(c, op);
   }

   mx_fs_close(hp);
   mx_fs_close(yp);

   fflush(op);
   if (ferror(op)) {
      n_perr(_("encrypted output data"), 0);
      mx_fs_close(op);
      op = NULL;
      goto jleave;
   }
   rewind(op);
jleave:
   NYD_OU;
   return op;
}

FL struct message *
smime_decrypt_assemble(struct message *m, FILE *hp, FILE *bp)
{
   u32 lastnl = 0;
   int binary = 0;
   char *buf = NULL;
   uz bufsize = 0, buflen, cnt;
   long lns = 0, octets = 0;
   struct message *x;
   off_t offset;
   NYD_IN;

   x = n_autorec_alloc(sizeof *x);
   *x = *m;
   fflush(mb.mb_otf);
   fseek(mb.mb_otf, 0L, SEEK_END);
   offset = ftell(mb.mb_otf);

   cnt = fsize(hp);
   while (fgetline(&buf, &bufsize, &cnt, &buflen, hp, 0) != NULL) {
      char const *cp;
      if (buf[0] == '\n')
         break;
      if ((cp = n_header_get_field(buf, "content-transfer-encoding", su_NIL)
            ) != NULL)
         if (!su_cs_cmp_case_n(cp, "binary", 7))
            binary = 1;
      fwrite(buf, sizeof *buf, buflen, mb.mb_otf);
      octets += buflen;
      ++lns;
   }

   {  struct time_current save = time_current;
      time_current_update(&time_current, TRU1);
      octets += mkdate(mb.mb_otf, "X-Decoding-Date");
      time_current = save;
   }
   ++lns;

   cnt = fsize(bp);
   while (fgetline(&buf, &bufsize, &cnt, &buflen, bp, 0) != NULL) {
      lns++;
      if (!binary && buf[buflen - 1] == '\n' && buf[buflen - 2] == '\r')
         buf[--buflen - 1] = '\n';
      fwrite(buf, sizeof *buf, buflen, mb.mb_otf);
      octets += buflen;
      if (buf[0] == '\n')
         ++lastnl;
      else if (buf[buflen - 1] == '\n')
         lastnl = 1;
      else
         lastnl = 0;
   }

   while (!binary && lastnl < 2) {
      putc('\n', mb.mb_otf);
      ++lns;
      ++octets;
      ++lastnl;
   }

   mx_fs_close(hp);
   mx_fs_close(bp);
   n_free(buf);

   fflush(mb.mb_otf);
   if (ferror(mb.mb_otf)) {
      n_perr(_("decrypted output data"), 0);
      x = NULL;
   }else{
      x->m_size = x->m_xsize = octets;
      x->m_lines = x->m_xlines = lns;
      x->m_block = mailx_blockof(offset);
      x->m_offset = mailx_offsetof(offset);
   }
   NYD_OU;
   return x;
}

FL int
c_certsave(void *vp){
   FILE *fp;
   int *msgvec, *ip;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   cacp = vp;
   ASSERT(cacp->cac_no == 2);

   msgvec = cacp->cac_arg->ca_arg.ca_msglist;
   /* C99 */{
      char *file, *cp;

      file = cacp->cac_arg->ca_next->ca_arg.ca_str.s;
      if((cp = fexpand(file, FEXP_LOCAL_FILE | FEXP_NOPROTO)) == NULL ||
            *cp == '\0'){
         n_err(_("`certsave': file expansion failed: %s\n"),
            n_shexp_quote_cp(file, FAL0));
         vp = NULL;
         goto jleave;
      }
      file = cp;

      if((fp = mx_fs_open(file, "a")) == NIL){
         n_perr(file, 0);
         vp = NULL;
         goto jleave;
      }
   }

   for(ip = msgvec; *ip != 0; ++ip)
      if(smime_certsave(&message[*ip - 1], *ip, fp) != OKAY)
         vp = NULL;

   mx_fs_close(fp);

   if(vp != NULL)
      fprintf(n_stdout, "Certificate(s) saved\n");
jleave:
   NYD_OU;
   return (vp != NULL);
}

FL boole
n_tls_rfc2595_hostname_match(char const *host, char const *pattern){
   boole rv;
   NYD_IN;

   if(pattern[0] == '*' && pattern[1] == '.'){
      ++pattern;
      while(*host && *host != '.')
         ++host;
   }
   rv = (su_cs_cmp_case(host, pattern) == 0);
   NYD_OU;
   return rv;
}

FL int
c_tls(void *vp){
   uz i;
   char const **argv, *varname, *varres, *cp;
   NYD_IN;

   argv = vp;
   vp = NULL; /* -> return value (boolean) */
   varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *argv++ : NULL;
   varres = n_empty;

   if((cp = argv[0])[0] == '\0')
      goto jesubcmd;
   else if(su_cs_starts_with_case("fingerprint", cp)){
#ifndef mx_HAVE_NET
      n_err(_("`tls': fingerprint: no +sockets in *features*\n"));
      n_pstate_err_no = su_ERR_OPNOTSUPP;
      goto jleave;
#else
      struct mx_socket so;
      struct mx_url url;

      if(argv[1] == NULL || argv[2] != NULL)
         goto jesynopsis;
      if((i = su_cs_len(*++argv)) >= U32_MAX)
         goto jeoverflow; /* TODO generic for ALL commands!! */
      if(!mx_url_parse(&url, CPROTO_CERTINFO, *argv))
         goto jeinval;
      if(!mx_socket_open(&so, &url)){ /* auto-close 4 CPROTO_CERTINFO if ok */
         n_pstate_err_no = su_err_no();
         goto jleave;
      }
      if(so.s_tls_finger == NULL)
         goto jeinval;
      varres = so.s_tls_finger;
#endif /* mx_HAVE_NET */
   }else
      goto jesubcmd;

   n_pstate_err_no = su_ERR_NONE;
   vp = (char*)-1;
jleave:
   if(varname == NULL){
      if(fprintf(n_stdout, "%s\n", varres) < 0){
         n_pstate_err_no = su_err_no();
         vp = NULL;
      }
   }else if(!n_var_vset(varname, (up)varres)){
      n_pstate_err_no = su_ERR_NOTSUP;
      vp = NULL;
   }
   NYD_OU;
   return (vp == NULL);

jeoverflow:
   n_err(_("`tls': string length or offset overflows datatype\n"));
   n_pstate_err_no = su_ERR_OVERFLOW;
   goto jleave;

jesubcmd:
   n_err(_("`tls': invalid subcommand: %s\n"),
      n_shexp_quote_cp(*argv, FAL0));
jesynopsis:
   mx_cmd_print_synopsis(mx_cmd_firstfit("tls"), NIL);
jeinval:
   n_pstate_err_no = su_ERR_INVAL;
   goto jleave;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_TLS */
/* s-it-mode */
