/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message content preparation (sendmp()).
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
#undef su_FILE
#define su_FILE send
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

static sigjmp_buf _send_pipejmp;

/* Going for user display, print Part: info string */
static void          _print_part_info(FILE *obuf, struct mimepart const *mpp,
                        struct n_ignore const *doitp, int level,
                        struct quoteflt *qf, ui64_t *stats);

/* Create a pipe; if mpp is not NULL, place some n_PIPEENV_* environment
 * variables accordingly */
static FILE *        _pipefile(struct mime_handler *mhp,
                        struct mimepart const *mpp, FILE **qbuf,
                        char const *tmpname, int term_infd);

/* Call mime_write() as approbiate and adjust statistics */
su_SINLINE ssize_t _out(char const *buf, size_t len, FILE *fp,
      enum conversion convert, enum sendaction action, struct quoteflt *qf,
      ui64_t *stats, struct str *outrest, struct str *inrest);

/* Simply (!) print out a LF */
static bool_t a_send_out_nl(FILE *fp, ui64_t *stats);

/* SIGPIPE handler */
static void          _send_onpipe(int signo);

/* Send one part */
static int           sendpart(struct message *zmp, struct mimepart *ip,
                        FILE *obuf, struct n_ignore const *doitp,
                        struct quoteflt *qf, enum sendaction action,
                        char **linedat, size_t *linesize,
                        ui64_t *stats, int level);

/* Get a file for an attachment */
static FILE *        newfile(struct mimepart *ip, bool_t volatile *ispipe);

static void          pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf,
                        struct quoteflt *qf, ui64_t *stats);

/* Output a reasonable looking status field */
static void          statusput(const struct message *mp, FILE *obuf,
                        struct quoteflt *qf, ui64_t *stats);
static void          xstatusput(const struct message *mp, FILE *obuf,
                        struct quoteflt *qf, ui64_t *stats);

static void          put_from_(FILE *fp, struct mimepart *ip, ui64_t *stats);

static void
_print_part_info(FILE *obuf, struct mimepart const *mpp, /* TODO strtofmt.. */
   struct n_ignore const *doitp, int level, struct quoteflt *qf, ui64_t *stats)
{
   char buf[64];
   struct str ti, to;
   bool_t want_ct, needsep;
   struct str const *cpre, *csuf;
   char const *cp;
   n_NYD2_IN;

   cpre = csuf = NULL;
#ifdef mx_HAVE_COLOUR
   if(n_COLOUR_IS_ACTIVE()){
      struct n_colour_pen *cpen;

      cpen = n_colour_pen_create(n_COLOUR_ID_VIEW_PARTINFO, NULL);
      if((cpre = n_colour_pen_to_str(cpen)) != NULL)
         csuf = n_colour_reset_to_str();
   }
#endif

   /* Take care of "99.99", i.e., 5 */
   if ((cp = mpp->m_partstring) == NULL || cp[0] == '\0')
      cp = n_qm;
   if (level || (cp[0] != '1' && cp[1] == '\0') || (cp[0] == '1' && /* TODO */
         cp[1] == '.' && cp[2] != '1')) /* TODO code should not look like so */
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);

   /* Part id, content-type, encoding, charset */
   if (cpre != NULL)
      _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
   _out("[-- #", 5, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
   _out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);

   to.l = snprintf(buf, sizeof buf, " %" PRIuZ "/%" PRIuZ " ",
         (uiz_t)mpp->m_lines, (uiz_t)mpp->m_size);
   _out(buf, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);

   needsep = FAL0;

    if((cp = mpp->m_ct_type_usr_ovwr) != NULL){
      _out("+", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      want_ct = TRU1;
   }else if((want_ct = n_ignore_is_ign(doitp,
         "content-type", sizeof("content-type") -1)))
      cp = mpp->m_ct_type_plain;
   if (want_ct &&
         (to.l = strlen(cp)) > 30 && is_asccaseprefix("application/", cp)) {
      size_t const al = sizeof("appl../") -1, fl = sizeof("application/") -1;
      size_t i = to.l - fl;
      char *x = n_autorec_alloc(al + i +1);

      memcpy(x, "appl../", al);
      memcpy(x + al, cp + fl, i +1);
      cp = x;
      to.l = al + i;
   }
   if(cp != NULL){
      _out(cp, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      needsep = TRU1;
   }

   if(mpp->m_multipart == NULL/* TODO */ && (cp = mpp->m_ct_enc) != NULL &&
         (!asccasecmp(cp, "7bit") ||
          n_ignore_is_ign(doitp, "content-transfer-encoding",
            sizeof("content-transfer-encoding") -1))){
      if(needsep)
         _out(", ", 2, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      if (to.l > 25 && !asccasecmp(cp, "quoted-printable"))
         cp = "qu.-pr.";
      _out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      needsep = TRU1;
   }

   if (want_ct && mpp->m_multipart == NULL/* TODO */ &&
         (cp = mpp->m_charset) != NULL) {
      if(needsep)
         _out(", ", 2, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      _out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
   }

   needsep = !needsep;
   _out(&" --]"[needsep], 4 - needsep,
      obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
   if (csuf != NULL)
      _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
   _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);

   /* */
   if (mpp->m_content_info & CI_MIME_ERRORS) {
      if (cpre != NULL)
         _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("[-- ", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);

      ti.l = strlen(ti.s = n_UNCONST(_("Defective MIME structure")));
      makeprint(&ti, &to);
      to.l = delctrl(to.s, to.l);
      _out(to.s, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      n_free(to.s);

      _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      if (csuf != NULL)
         _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
   }

   /* Content-Description */
   if (n_ignore_is_ign(doitp, "content-description", 19) &&
         (cp = mpp->m_content_description) != NULL && *cp != '\0') {
      if (cpre != NULL)
         _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("[-- ", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);

      ti.l = strlen(ti.s = n_UNCONST(mpp->m_content_description));
      mime_fromhdr(&ti, &to, TD_ISPR | TD_ICONV);
      _out(to.s, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      n_free(to.s);

      _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      if (csuf != NULL)
         _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
   }

   /* Filename */
   if (n_ignore_is_ign(doitp, "content-disposition", 19) &&
         mpp->m_filename != NULL && *mpp->m_filename != '\0') {
      if (cpre != NULL)
         _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("[-- ", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);

      ti.l = strlen(ti.s = mpp->m_filename);
      makeprint(&ti, &to);
      to.l = delctrl(to.s, to.l);
      _out(to.s, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      n_free(to.s);

      _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      if (csuf != NULL)
         _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
   }
   n_NYD2_OU;
}

static FILE *
_pipefile(struct mime_handler *mhp, struct mimepart const *mpp, FILE **qbuf,
   char const *tmpname, int term_infd)
{
   struct str s;
   char const *env_addon[9 +8/*v15*/], *cp, *sh;
   size_t i;
   FILE *rbuf;
   n_NYD_IN;

   rbuf = *qbuf;

   if (mhp->mh_flags & MIME_HDL_ISQUOTE) {
      if ((*qbuf = Ftmp(NULL, "sendp", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
            NULL) {
         n_perr(_("tmpfile"), 0);
         *qbuf = rbuf;
      }
   }

   if ((mhp->mh_flags & MIME_HDL_TYPE_MASK) == MIME_HDL_PTF) {
      union {int (*ptf)(void); char const *sh;} u;

      fflush(*qbuf);
      if (*qbuf != n_stdout) /* xxx never?  v15: it'll be a filter anyway */
         fflush(n_stdout);

      u.ptf = mhp->mh_ptf;
      if((rbuf = Popen((char*)-1, "W", u.sh, NULL, fileno(*qbuf))) == NULL)
         goto jerror;
      goto jleave;
   }

   i = 0;

   /* MAILX_FILENAME */
   if (mpp == NULL || (cp = mpp->m_filename) == NULL)
      cp = n_empty;
   env_addon[i++] = str_concat_csvl(&s, n_PIPEENV_FILENAME, "=", cp, NULL)->s;
env_addon[i++] = str_concat_csvl(&s, "NAIL_FILENAME", "=", cp, NULL)->s;/*v15*/

   /* MAILX_FILENAME_GENERATED *//* TODO pathconf NAME_MAX; but user can create
    * TODO a file wherever he wants!  *Do* create a zero-size temporary file
    * TODO and give *that* path as MAILX_FILENAME_TEMPORARY, clean it up once
    * TODO the pipe returns?  Like this we *can* verify path/name issues! */
   cp = n_random_create_cp(n_MIN(NAME_MAX / 4, 16), NULL);
   env_addon[i++] = str_concat_csvl(&s, n_PIPEENV_FILENAME_GENERATED, "=", cp,
         NULL)->s;
env_addon[i++] = str_concat_csvl(&s, "NAIL_FILENAME_GENERATED", "=", cp,/*v15*/
      NULL)->s;

   /* MAILX_CONTENT{,_EVIDENCE} */
   if (mpp == NULL || (cp = mpp->m_ct_type_plain) == NULL)
      cp = n_empty;
   env_addon[i++] = str_concat_csvl(&s, n_PIPEENV_CONTENT, "=", cp, NULL)->s;
env_addon[i++] = str_concat_csvl(&s, "NAIL_CONTENT", "=", cp, NULL)->s;/*v15*/

   if (mpp != NULL && mpp->m_ct_type_usr_ovwr != NULL)
      cp = mpp->m_ct_type_usr_ovwr;
   env_addon[i++] = str_concat_csvl(&s, n_PIPEENV_CONTENT_EVIDENCE, "=", cp,
         NULL)->s;
env_addon[i++] = str_concat_csvl(&s, "NAIL_CONTENT_EVIDENCE", "=", cp,/* v15 */
      NULL)->s;

   /* message/external-body, access-type=url */
   env_addon[i++] = str_concat_csvl(&s, n_PIPEENV_EXTERNAL_BODY_URL, "=",
         ((mpp != NULL && (cp = mpp->m_external_body_url) != NULL
            ) ? cp : n_empty), NULL)->s;

   /* MAILX_FILENAME_TEMPORARY? */
   if (tmpname != NULL) {
      env_addon[i++] = str_concat_csvl(&s,
            n_PIPEENV_FILENAME_TEMPORARY, "=", tmpname, NULL)->s;
env_addon[i++] = str_concat_csvl(&s,
         "NAIL_FILENAME_TEMPORARY", "=", tmpname, NULL)->s;/* v15 */
   }

   /* TODO we should include header information, especially From:, so
    * TODO that same-origin can be tested for e.g. external-body!!! */

   env_addon[i] = NULL;
   sh = ok_vlook(SHELL);

   if (mhp->mh_flags & MIME_HDL_NEEDSTERM) {
      sigset_t nset;
      int pid;

      sigemptyset(&nset);
      pid = n_child_run(sh, &nset, term_infd, n_CHILD_FD_PASS, "-c",
            mhp->mh_shell_cmd, NULL, env_addon, NULL);
      rbuf = (pid < 0) ? NULL : (FILE*)-1;
   } else {
      rbuf = Popen(mhp->mh_shell_cmd, "W", sh, env_addon,
            (mhp->mh_flags & MIME_HDL_ASYNC ? -1 : fileno(*qbuf)));
jerror:
      if (rbuf == NULL)
         n_err(_("Cannot run MIME type handler: %s: %s\n"),
            mhp->mh_msg, n_err_to_doc(n_err_no));
      else {
         fflush(*qbuf);
         if (*qbuf != n_stdout)
            fflush(n_stdout);
      }
   }
jleave:
   n_NYD_OU;
   return rbuf;
}

su_SINLINE ssize_t
_out(char const *buf, size_t len, FILE *fp, enum conversion convert, enum
   sendaction action, struct quoteflt *qf, ui64_t *stats, struct str *outrest,
   struct str *inrest)
{
   ssize_t sz = 0, n;
   int flags;
   n_NYD_IN;

   /* TODO We should not need is_head() here, i think in v15 the actual Mailbox
    * TODO subclass should detect such From_ cases and either reencode the part
    * TODO in question, or perform From_ quoting as necessary!?!?!?  How?!? */
   /* C99 */{
      bool_t from_;

      if((action == SEND_MBOX || action == SEND_DECRYPT) &&
            (from_ = is_head(buf, len, TRU1))){
         if(from_ != TRUM1 || ok_blook(mbox_rfc4155)){
            putc('>', fp);
            ++sz;
         }
      }
   }

   flags = ((int)action & _TD_EOF);
   action &= ~_TD_EOF;
   n = mime_write(buf, len, fp,
         action == SEND_MBOX ? CONV_NONE : convert,
         flags | ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
            action == SEND_TODISP_PARTS ||
            action == SEND_QUOTE || action == SEND_QUOTE_ALL)
         ?  TD_ISPR | TD_ICONV
         : (action == SEND_TOSRCH || action == SEND_TOPIPE ||
               action == SEND_TOFILE)
            ? TD_ICONV : (action == SEND_SHOW ? TD_ISPR : TD_NONE)),
         qf, outrest, inrest);
   if (n < 0)
      sz = n;
   else if (n > 0) {
      sz += n;
      if (stats != NULL)
         *stats += sz;
   }
   n_NYD_OU;
   return sz;
}

static bool_t
a_send_out_nl(FILE *fp, ui64_t *stats){
   struct quoteflt *qf;
   bool_t rv;
   n_NYD2_IN;

   quoteflt_reset(qf = quoteflt_dummy(), fp);
   rv = (_out("\n", 1, fp, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL) > 0);
   quoteflt_flush(qf);
   n_NYD2_OU;
   return rv;
}

static void
_send_onpipe(int signo)
{
   n_NYD_X; /* Signal handler */
   n_UNUSED(signo);
   siglongjmp(_send_pipejmp, 1);
}

static sigjmp_buf       __sendp_actjmp; /* TODO someday.. */
static int              __sendp_sig; /* TODO someday.. */
static sighandler_type  __sendp_opipe;
static void
__sendp_onsig(int sig) /* TODO someday, we won't need it no more */
{
   n_NYD_X; /* Signal handler */
   __sendp_sig = sig;
   siglongjmp(__sendp_actjmp, 1);
}

static int
sendpart(struct message *zmp, struct mimepart *ip, FILE * volatile obuf,
   struct n_ignore const *doitp, struct quoteflt *qf,
   enum sendaction volatile action,
   char **linedat, size_t *linesize, ui64_t * volatile stats, int level)
{
   int volatile rv = 0;
   struct mime_handler mh;
   struct str outrest, inrest;
   char *cp;
   char const * volatile tmpname = NULL;
   size_t linelen, cnt;
   int volatile dostat, term_infd;
   int c;
   struct mimepart * volatile np;
   FILE * volatile ibuf = NULL, * volatile pbuf = obuf,
      * volatile qbuf = obuf, *origobuf = obuf;
   enum conversion volatile convert;
   sighandler_type volatile oldpipe = SIG_DFL;
   n_NYD_IN;

   n_UNINIT(term_infd, 0);
   n_UNINIT(cnt, 0);

   quoteflt_reset(qf, obuf);

#if 0 /* TODO PART_INFO should be displayed here!! search PART_INFO */
   if(ip->m_mimecontent != MIME_DISCARD && level > 0)
      _print_part_info(obuf, ip, doitp, level, qf, stats);
#endif

   if (ip->m_mimecontent == MIME_PKCS7) {
      if (ip->m_multipart &&
            action != SEND_MBOX && action != SEND_RFC822 && action != SEND_SHOW)
         goto jheaders_skip;
   }

   dostat = 0;
   if (level == 0 && action != SEND_TODISP_PARTS) {
      if (doitp != NULL) {
         if (!n_ignore_is_ign(doitp, "status", 6))
            dostat |= 1;
         if (!n_ignore_is_ign(doitp, "x-status", 8))
            dostat |= 2;
      } else
         dostat = 3;
   }

   if ((ibuf = setinput(&mb, (struct message*)ip, NEED_BODY)) == NULL) {
      rv = -1;
      goto jleave;
   }

   if(action == SEND_TODISP || action == SEND_TODISP_ALL ||
         action == SEND_TODISP_PARTS ||
         action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
         action == SEND_TOSRCH)
      dostat |= 4;

   cnt = ip->m_size;

   if (ip->m_mimecontent == MIME_DISCARD)
      goto jheaders_skip;

   if (!(ip->m_flag & MNOFROM))
      while (cnt && (c = getc(ibuf)) != EOF) {
         cnt--;
         if (c == '\n')
            break;
      }
   convert = (dostat & 4) ? CONV_FROMHDR : CONV_NONE;

   /* Work the headers */
   /* C99 */{
   struct n_string hl, *hlp;
   size_t lineno = 0;
   bool_t hstop/*see below, hany*/;

   hlp = n_string_creat_auto(&hl); /* TODO pool [or, v15: filter!] */
   /* Reserve three lines, still not enough for references and DKIM etc. */
   hlp = n_string_reserve(hlp, n_MAX(MIME_LINELEN, MIME_LINELEN_RFC2047) * 3);

   for(hstop = /*see below hany =*/ FAL0; !hstop;){
      size_t lcnt;

      lcnt = cnt;
      if(fgetline(linedat, linesize, &cnt, &linelen, ibuf, 0) == NULL)
         break;
      ++lineno;
      if (linelen == 0 || (cp = *linedat)[0] == '\n')
         /* If line is blank, we've reached end of headers */
         break;
      if(cp[linelen - 1] == '\n'){
         cp[--linelen] = '\0';
         if(linelen == 0)
            break;
      }

      /* Are we in a header? */
      if(hlp->s_len > 0){
         if(!blankchar(*cp)){
            fseek(ibuf, -(off_t)(lcnt - cnt), SEEK_CUR);
            cnt = lcnt;
            goto jhdrput;
         }
         goto jhdrpush;
      }else{
         /* Pick up the header field if we have one */
         while((c = *cp) != ':' && !spacechar(c) && c != '\0')
            ++cp;
         for(;;){
            if(!spacechar(c) || c == '\0')
               break;
            c = *++cp;
         }
         if(c != ':'){
            /* That won't work with MIME when saving etc., before v15 */
            if (lineno != 1)
               /* XXX This disturbs, and may happen multiple times, and we
                * XXX cannot heal it for multipart except for display <v15 */
               n_err(_("Malformed message: headers and body not separated "
                  "(with empty line)\n"));
            if(level != 0)
               dostat &= ~(1 | 2);
            fseek(ibuf, -(off_t)(lcnt - cnt), SEEK_CUR);
            cnt = lcnt;
            break;
         }

         cp = *linedat;
jhdrpush:
         if(!(dostat & 4)){
            hlp = n_string_push_buf(hlp, cp, (ui32_t)linelen);
            hlp = n_string_push_c(hlp, '\n');
         }else{
            bool_t lblank, isblank;

            for(lblank = FAL0, lcnt = 0; lcnt < linelen; ++cp, ++lcnt){
               char c8;

               c8 = *cp;
               if(!(isblank = blankchar(c8)) || !lblank){
                  if((lblank = isblank))
                     c8 = ' ';
                  hlp = n_string_push_c(hlp, c8);
               }
            }
         }
         continue;
      }

jhdrput:
      /* If it is an ignored header, skip it */
      *(cp = memchr(hlp->s_dat, ':', hlp->s_len)) = '\0';
      /* C99 */{
         size_t i;

         i = PTR2SIZE(cp - hlp->s_dat);
         if((doitp != NULL && n_ignore_is_ign(doitp, hlp->s_dat, i)) ||
               !asccasecmp(hlp->s_dat, "status") ||
               !asccasecmp(hlp->s_dat, "x-status") ||
               (action == SEND_MBOX &&
                  (!asccasecmp(hlp->s_dat, "content-length") ||
                   !asccasecmp(hlp->s_dat, "lines")) &&
                !ok_blook(keep_content_length)))
            goto jhdrtrunc;
      }

      /* Dump it */
      n_COLOUR(
         if(n_COLOUR_IS_ACTIVE())
            n_colour_put(n_COLOUR_ID_VIEW_HEADER, hlp->s_dat);
      )
      *cp = ':';
      _out(hlp->s_dat, hlp->s_len, obuf, convert, action, qf, stats, NULL,NULL);
      n_COLOUR(
         if(n_COLOUR_IS_ACTIVE())
            n_colour_reset();
      )
      if(dostat & 4)
         _out("\n", sizeof("\n") -1, obuf, convert, action, qf, stats,
            NULL,NULL);
      /*see below hany = TRU1;*/

jhdrtrunc:
      hlp = n_string_trunc(hlp, 0);
   }
   hstop = TRU1;
   if(hlp->s_len > 0)
      goto jhdrput;

   /* We've reached end of headers, so eventually force out status: field and
    * note that we are no longer in header fields */
   if(dostat & 1){
      statusput(zmp, obuf, qf, stats);
      /*see below hany = TRU1;*/
   }
   if(dostat & 2){
      xstatusput(zmp, obuf, qf, stats);
      /*see below hany = TRU1;*/
   }
   if(/* TODO PART_INFO hany && */ doitp != n_IGNORE_ALL)
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
   } /* C99 */

   quoteflt_flush(qf);

   if(ferror(obuf)){
      rv = -1;
      goto jleave;
   }

jheaders_skip:
   memset(&mh, 0, sizeof mh);

   switch (ip->m_mimecontent) {
   case MIME_822:
      switch (action) {
      case SEND_TODISP_PARTS:
         goto jleave;
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         if (ok_blook(rfc822_body_from_)) {
            if (!qf->qf_bypass) {
               size_t i = fwrite(qf->qf_pfix, sizeof *qf->qf_pfix,
                     qf->qf_pfix_len, obuf);
               if (i == qf->qf_pfix_len && stats != NULL)
                  *stats += i;
            }
            put_from_(obuf, ip->m_multipart, stats);
         }
         /* FALLTHRU */
      case SEND_TOSRCH:
      case SEND_DECRYPT:
         goto jmulti;
      case SEND_TOFILE:
      case SEND_TOPIPE:
         put_from_(obuf, ip->m_multipart, stats);
         /* FALLTHRU */
      case SEND_MBOX:
      case SEND_RFC822:
      case SEND_SHOW:
         break;
      }
      break;
   case MIME_TEXT_HTML:
   case MIME_TEXT:
   case MIME_TEXT_PLAIN:
      switch (action) {
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_TODISP_PARTS:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         switch (n_mimetype_handler(&mh, ip, action)) {
         case MIME_HDL_NULL:
            if(action != SEND_TODISP_PARTS)
               break;
            /* FALLTHRU */
         case MIME_HDL_MSG:/* TODO these should be part of partinfo! */
            if(mh.mh_msg.l > 0)
               _out(mh.mh_msg.s, mh.mh_msg.l, obuf, CONV_NONE, SEND_MBOX,
                  qf, stats, NULL, NULL);
            /* We would print this as plain text, so better force going home */
            goto jleave;
         case MIME_HDL_CMD:
            if(action == SEND_TODISP_PARTS &&
                  (mh.mh_flags & MIME_HDL_COPIOUSOUTPUT))
               goto jleave;
            break;
         case MIME_HDL_TEXT:
         case MIME_HDL_PTF:
            if(action == SEND_TODISP_PARTS)
               goto jleave;
            break;
         default:
            break;
         }
         /* FALLTRHU */
      default:
         break;
      }
      break;
   case MIME_DISCARD:
      if (action != SEND_DECRYPT)
         goto jleave;
      break;
   case MIME_PKCS7:
      if (action != SEND_MBOX && action != SEND_RFC822 &&
            action != SEND_SHOW && ip->m_multipart != NULL)
         goto jmulti;
      /* FALLTHRU */
   default:
      switch (action) {
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_TODISP_PARTS:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         switch (n_mimetype_handler(&mh, ip, action)) {
         default:
         case MIME_HDL_NULL:
            if (action != SEND_TODISP && action != SEND_TODISP_ALL &&
                  (level != 0 || cnt))
               goto jleave;
            /* FALLTHRU */
         case MIME_HDL_MSG:/* TODO these should be part of partinfo! */
            if(mh.mh_msg.l > 0)
               _out(mh.mh_msg.s, mh.mh_msg.l, obuf, CONV_NONE, SEND_MBOX,
                  qf, stats, NULL, NULL);
            /* We would print this as plain text, so better force going home */
            goto jleave;
         case MIME_HDL_CMD:
            if(action == SEND_TODISP_PARTS){
               if(mh.mh_flags & MIME_HDL_COPIOUSOUTPUT)
                  goto jleave;
               else{
                  _print_part_info(obuf, ip, doitp, level, qf, stats);
                  /* Because: interactive OR batch mode, so */
                  if(!getapproval(_("Run MIME handler for this part?"),
                        su_state_has(su_STATE_REPRODUCIBLE)))
                     goto jleave;
               }
            }
            break;
         case MIME_HDL_TEXT:
         case MIME_HDL_PTF:
            if(action == SEND_TODISP_PARTS)
               goto jleave;
            break;
         }
         break;
      case SEND_TOFILE:
      case SEND_TOPIPE:
      case SEND_TOSRCH:
      case SEND_DECRYPT:
      case SEND_MBOX:
      case SEND_RFC822:
      case SEND_SHOW:
         break;
      }
      break;
   case MIME_ALTERNATIVE:
      if ((action == SEND_TODISP || action == SEND_QUOTE) &&
            !ok_blook(print_alternatives)) {
         /* XXX This (a) should not remain (b) should be own fun
          * TODO (despite the fact that v15 will do this completely differently
          * TODO by having an action-specific "manager" that will traverse the
          * TODO parsed MIME tree and decide for each part whether it'll be
          * TODO displayed or not *before* we walk the tree for doing action */
         struct mpstack {
            struct mpstack *outer;
            struct mimepart *mp;
         } outermost, * volatile curr, * volatile mpsp;
         bool_t volatile neednl, hadpart;
         struct n_sigman smalter;

         (curr = &outermost)->outer = NULL;
         curr->mp = ip;
         neednl = hadpart = FAL0;

         n_SIGMAN_ENTER_SWITCH(&smalter, n_SIGMAN_ALL) {
         case 0:
            break;
         default:
            rv = -1;
            goto jalter_leave;
         }

         for (np = ip->m_multipart;;) {
jalter_redo:
            for (; np != NULL; np = np->m_nextpart) {
               if (action != SEND_QUOTE && np->m_ct_type_plain != NULL) {
                  if (neednl)
                     _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats,
                        NULL, NULL);
                  _print_part_info(obuf, np, doitp, level, qf, stats);
               }
               neednl = TRU1;

               switch (np->m_mimecontent) {
               case MIME_ALTERNATIVE:
               case MIME_RELATED:
               case MIME_DIGEST:
               case MIME_SIGNED:
               case MIME_ENCRYPTED:
               case MIME_MULTI:
                  mpsp = n_autorec_alloc(sizeof *mpsp);
                  mpsp->outer = curr;
                  mpsp->mp = np->m_multipart;
                  curr->mp = np;
                  curr = mpsp;
                  np = mpsp->mp;
                  neednl = FAL0;
                  goto jalter_redo;
               default:
                  if (hadpart)
                     break;
                  switch (n_mimetype_handler(&mh, np, action)) {
                  default:
                     mh.mh_flags = MIME_HDL_NULL;
                     continue; /* break; break; */
                  case MIME_HDL_CMD:
                     if(!(mh.mh_flags & MIME_HDL_COPIOUSOUTPUT)){
                        mh.mh_flags = MIME_HDL_NULL;
                        continue; /* break; break; */
                     }
                     /* FALLTHRU */
                  case MIME_HDL_PTF:
                     if (!ok_blook(mime_alternative_favour_rich)) {/* TODO */
                        struct mimepart *x = np;

                        while ((x = x->m_nextpart) != NULL) {
                           struct mime_handler mhx;

                           if (x->m_mimecontent == MIME_TEXT_PLAIN ||
                                 n_mimetype_handler(&mhx, x, action) ==
                                    MIME_HDL_TEXT)
                              break;
                        }
                        if (x != NULL)
                           continue; /* break; break; */
                        goto jalter_plain;
                     }
                     /* FALLTHRU */
                  case MIME_HDL_TEXT:
                     break;
                  }
                  /* FALLTHRU */
               case MIME_TEXT_PLAIN:
                  if (hadpart)
                     break;
                  if (ok_blook(mime_alternative_favour_rich)) { /* TODO */
                     struct mimepart *x = np;

                     /* TODO twice TODO, we should dive into /related and
                      * TODO check whether that has rich parts! */
                     while ((x = x->m_nextpart) != NULL) {
                        struct mime_handler mhx;

                        switch (n_mimetype_handler(&mhx, x, action)) {
                        case MIME_HDL_CMD:
                           if(!(mhx.mh_flags & MIME_HDL_COPIOUSOUTPUT))
                              continue;
                           /* FALLTHRU */
                        case MIME_HDL_PTF:
                           break;
                        default:
                           continue;
                        }
                        break;
                     }
                     if (x != NULL)
                        continue; /* break; break; */
                  }
jalter_plain:
                  quoteflt_flush(qf);
                  if (action == SEND_QUOTE && hadpart)
                     /* XXX (void)*/a_send_out_nl(obuf, stats);
                  hadpart = TRU1;
                  neednl = FAL0;
                  rv = sendpart(zmp, np, obuf, doitp, qf, action,
                        linedat, linesize, stats, level + 1);
                  quoteflt_reset(qf, origobuf);

                  if (rv < 0)
                     curr = &outermost; /* Cause overall loop termination */
                  break;
               }
            }

            mpsp = curr->outer;
            if (mpsp == NULL)
               break;
            curr = mpsp;
            np = curr->mp->m_nextpart;
         }
jalter_leave:
         n_sigman_leave(&smalter, n_SIGMAN_VIPSIGS_NTTYOUT);
         goto jleave;
      }
      /* FALLTHRU */
   case MIME_RELATED:
   case MIME_DIGEST:
   case MIME_SIGNED:
   case MIME_ENCRYPTED:
   case MIME_MULTI:
      switch (action) {
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_TODISP_PARTS:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
      case SEND_TOFILE:
      case SEND_TOPIPE:
      case SEND_TOSRCH:
      case SEND_DECRYPT:
jmulti:
         if ((action == SEND_TODISP || action == SEND_TODISP_ALL) &&
             ip->m_multipart != NULL &&
             ip->m_multipart->m_mimecontent == MIME_DISCARD &&
             ip->m_multipart->m_nextpart == NULL) {
            char const *x = _("[Missing multipart boundary - use `show' "
                  "to display the raw message]\n");
            _out(x, strlen(x), obuf, CONV_NONE, SEND_MBOX, qf, stats,
               NULL,NULL);
         }

         for (np = ip->m_multipart; np != NULL; np = np->m_nextpart) {
            bool_t volatile ispipe;

            if (np->m_mimecontent == MIME_DISCARD && action != SEND_DECRYPT)
               continue;

            ispipe = FAL0;
            switch (action) {
            case SEND_TOFILE:
               if (np->m_partstring &&
                     np->m_partstring[0] == '1' && np->m_partstring[1] == '\0')
                  break;
               stats = NULL;
               /* TODO Always open multipart on /dev/null, it's a hack to be
                * TODO able to dive into that structure, and still better
                * TODO than asking the user for something stupid.
                * TODO oh, wait, we did ask for a filename for this MIME mail,
                * TODO and that outer container is useless anyway ;-P */
               if (np->m_multipart != NULL && np->m_mimecontent != MIME_822) {
                  if ((obuf = Fopen(n_path_devnull, "w")) == NULL)
                     continue;
               } else if ((obuf = newfile(np, &ispipe)) == NULL)
                  continue;
               if (!ispipe)
                  break;
               if (sigsetjmp(_send_pipejmp, 1)) {
                  rv = -1;
                  goto jpipe_close;
               }
               oldpipe = safe_signal(SIGPIPE, &_send_onpipe);
               break;
            case SEND_TODISP:
            case SEND_TODISP_ALL:
               if (ip->m_mimecontent != MIME_ALTERNATIVE &&
                     ip->m_mimecontent != MIME_RELATED &&
                     ip->m_mimecontent != MIME_DIGEST &&
                     ip->m_mimecontent != MIME_SIGNED &&
                     ip->m_mimecontent != MIME_ENCRYPTED &&
                     ip->m_mimecontent != MIME_MULTI)
                  break;
               _print_part_info(obuf, np, doitp, level, qf, stats);
               break;
            case SEND_TODISP_PARTS:
            case SEND_QUOTE:
            case SEND_QUOTE_ALL:
            case SEND_MBOX:
            case SEND_RFC822:
            case SEND_SHOW:
            case SEND_TOSRCH:
            case SEND_DECRYPT:
            case SEND_TOPIPE:
               break;
            }

            quoteflt_flush(qf);
            if ((action == SEND_QUOTE || action == SEND_QUOTE_ALL) &&
                  np->m_multipart == NULL && ip->m_parent != NULL)
               /*XXX (void)*/a_send_out_nl(obuf, stats);
            if (sendpart(zmp, np, obuf, doitp, qf, action, linedat, linesize,
                  stats, level+1) < 0)
               rv = -1;
            quoteflt_reset(qf, origobuf);

            if (action == SEND_QUOTE) {
               if (ip->m_mimecontent != MIME_RELATED)
                  break;
            }
            if (action == SEND_TOFILE && obuf != origobuf) {
               if (!ispipe)
                  Fclose(obuf);
               else {
jpipe_close:
                  safe_signal(SIGPIPE, SIG_IGN);
                  Pclose(obuf, TRU1);
                  safe_signal(SIGPIPE, oldpipe);
               }
            }
         }
         goto jleave;
      case SEND_MBOX:
      case SEND_RFC822:
      case SEND_SHOW:
         break;
      }
      break;
   }

   /* Copy out message body */
   if (doitp == n_IGNORE_ALL && level == 0) /* skip final blank line */
      --cnt;
   switch (ip->m_mime_enc) {
   case MIMEE_BIN:
   case MIMEE_7B:
   case MIMEE_8B:
      convert = CONV_NONE;
      break;
   case MIMEE_QP:
      convert = CONV_FROMQP;
      break;
   case MIMEE_B64:
      switch (ip->m_mimecontent) {
      case MIME_TEXT:
      case MIME_TEXT_PLAIN:
      case MIME_TEXT_HTML:
         convert = CONV_FROMB64_T;
         break;
      default:
         switch (mh.mh_flags & MIME_HDL_TYPE_MASK) {
         case MIME_HDL_TEXT:
         case MIME_HDL_PTF:
            convert = CONV_FROMB64_T;
            break;
         default:
            convert = CONV_FROMB64;
            break;
         }
         break;
      }
      break;
   default:
      convert = CONV_NONE;
   }

   /* TODO Unless we have filters, ensure iconvd==-1 so that mime.c:fwrite_td()
    * TODO cannot mess things up misusing outrest as line buffer */
#ifdef mx_HAVE_ICONV
   if (iconvd != (iconv_t)-1) {
      n_iconv_close(iconvd);
      iconvd = (iconv_t)-1;
   }
#endif

   if (action == SEND_DECRYPT || action == SEND_MBOX ||
         action == SEND_RFC822 || action == SEND_SHOW)
      convert = CONV_NONE;
#ifdef mx_HAVE_ICONV
   else if ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
            action == SEND_TODISP_PARTS ||
            action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
            action == SEND_TOSRCH || action == SEND_TOFILE) &&
         (ip->m_mimecontent == MIME_TEXT_PLAIN ||
            ip->m_mimecontent == MIME_TEXT_HTML ||
            ip->m_mimecontent == MIME_TEXT ||
            (mh.mh_flags & MIME_HDL_TYPE_MASK) == MIME_HDL_TEXT ||
            (mh.mh_flags & MIME_HDL_TYPE_MASK) == MIME_HDL_PTF)) {
      char const *tcs;

      tcs = ok_vlook(ttycharset);
      if (asccasecmp(tcs, ip->m_charset) &&
            asccasecmp(ok_vlook(charset_7bit), ip->m_charset)) {
         iconvd = n_iconv_open(tcs, ip->m_charset);
         if (iconvd == (iconv_t)-1 && n_err_no == n_ERR_INVAL) {
            n_err(_("Cannot convert from %s to %s\n"), ip->m_charset, tcs);
            /*rv = 1; goto jleave;*/
         }
      }
   }
#endif

   switch (mh.mh_flags & MIME_HDL_TYPE_MASK) {
   case MIME_HDL_CMD:
      if(!(mh.mh_flags & MIME_HDL_COPIOUSOUTPUT)){
         if(action != SEND_TODISP_PARTS)
            goto jmhp_default;
         /* Ach, what a hack!  We need filters.. v15! */
         if(convert != CONV_FROMB64_T)
            action = SEND_TOPIPE;
      }
      /* FALLTHRU */
   case MIME_HDL_PTF:
      tmpname = NULL;
      qbuf = obuf;

      term_infd = n_CHILD_FD_PASS;
      if (mh.mh_flags & (MIME_HDL_TMPF | MIME_HDL_NEEDSTERM)) {
         enum oflags of;

         of = OF_RDWR | OF_REGISTER;
         if (!(mh.mh_flags & MIME_HDL_TMPF)) {
            term_infd = 0;
            mh.mh_flags |= MIME_HDL_TMPF_FILL;
            of |= OF_UNLINK;
         } else if (mh.mh_flags & MIME_HDL_TMPF_UNLINK)
            of |= OF_REGISTER_UNLINK;

         if ((pbuf = Ftmp((mh.mh_flags & MIME_HDL_TMPF ? &cp : NULL),
               (mh.mh_flags & MIME_HDL_TMPF_FILL ? "mimehdlfill" : "mimehdl"),
               of)) == NULL)
            goto jesend;

         if (mh.mh_flags & MIME_HDL_TMPF) {
            tmpname = savestr(cp);
            Ftmp_free(&cp);
         }

         if (mh.mh_flags & MIME_HDL_TMPF_FILL) {
            if (term_infd == 0)
               term_infd = fileno(pbuf);
            goto jsend;
         }
      }

jpipe_for_real:
      pbuf = _pipefile(&mh, ip, n_UNVOLATILE(&qbuf), tmpname, term_infd);
      if (pbuf == NULL) {
jesend:
         pbuf = qbuf = NULL;
         rv = -1;
         goto jend;
      } else if ((mh.mh_flags & MIME_HDL_NEEDSTERM) && pbuf == (FILE*)-1) {
         pbuf = qbuf = NULL;
         goto jend;
      }
      tmpname = NULL;
      action = SEND_TOPIPE;
      if (pbuf != qbuf) {
         oldpipe = safe_signal(SIGPIPE, &_send_onpipe);
         if (sigsetjmp(_send_pipejmp, 1))
            goto jend;
      }
      break;

   default:
jmhp_default:
      mh.mh_flags = MIME_HDL_NULL;
      pbuf = qbuf = obuf;
      break;
   }

jsend:
   {
   bool_t volatile eof;
   bool_t save_qf_bypass = qf->qf_bypass;
   ui64_t *save_stats = stats;

   if (pbuf != origobuf) {
      qf->qf_bypass = TRU1;/* XXX legacy (remove filter instead) */
      stats = NULL;
   }
   eof = FAL0;
   outrest.s = inrest.s = NULL;
   outrest.l = inrest.l = 0;

   if (pbuf == qbuf) {
      __sendp_sig = 0;
      __sendp_opipe = safe_signal(SIGPIPE, &__sendp_onsig);
      if (sigsetjmp(__sendp_actjmp, 1)) {
         n_pstate &= ~n_PS_BASE64_STRIP_CR;/* (but protected by outer sigman) */
         if (outrest.s != NULL)
            n_free(outrest.s);
         if (inrest.s != NULL)
            n_free(inrest.s);
#ifdef mx_HAVE_ICONV
         if (iconvd != (iconv_t)-1)
            n_iconv_close(iconvd);
#endif
         safe_signal(SIGPIPE, __sendp_opipe);
         n_raise(__sendp_sig);
      }
   }

   quoteflt_reset(qf, pbuf);
   if((dostat & 4) && pbuf == origobuf) /* TODO */
      n_pstate |= n_PS_BASE64_STRIP_CR;
   while (!eof && fgetline(linedat, linesize, &cnt, &linelen, ibuf, 0)) {
joutln:
      if (_out(*linedat, linelen, pbuf, convert, action, qf, stats, &outrest,
            (action & _TD_EOF ? NULL : &inrest)) < 0 || ferror(pbuf)) {
         rv = -1; /* XXX Should bail away?! */
         break;
      }
   }
   if(eof <= FAL0 && rv >= 0 && (outrest.l != 0 || inrest.l != 0)){
      linelen = 0;
      if(eof || inrest.l == 0)
         action |= _TD_EOF;
      eof = eof ? TRU1 : TRUM1;
      goto joutln;
   }
   n_pstate &= ~n_PS_BASE64_STRIP_CR;
   action &= ~_TD_EOF;

   /* TODO HACK: when sending to the display we yet get fooled if a message
    * TODO doesn't end in a newline, because of our input/output 1:1.
    * TODO This should be handled automatically by a display filter, then */
   if(rv >= 0 && !qf->qf_nl_last &&
         (action == SEND_TODISP || action == SEND_TODISP_ALL ||
          action == SEND_QUOTE || action == SEND_QUOTE_ALL))
      rv = quoteflt_push(qf, "\n", 1);

   quoteflt_flush(qf);

   if (rv >= 0 && (mh.mh_flags & MIME_HDL_TMPF_FILL)) {
      mh.mh_flags &= ~MIME_HDL_TMPF_FILL;
      fflush(pbuf);
      really_rewind(pbuf);
      /* Don't Fclose() the Ftmp() thing due to OF_REGISTER_UNLINK++ */
      goto jpipe_for_real;
   }

   if (pbuf == qbuf)
      safe_signal(SIGPIPE, __sendp_opipe);

   if (outrest.s != NULL)
      n_free(outrest.s);
   if (inrest.s != NULL)
      n_free(inrest.s);

   if (pbuf != origobuf) {
      qf->qf_bypass = save_qf_bypass;
      stats = save_stats;
   }
   }

jend:
   if (pbuf != qbuf) {
      safe_signal(SIGPIPE, SIG_IGN);
      Pclose(pbuf, !(mh.mh_flags & MIME_HDL_ASYNC));
      safe_signal(SIGPIPE, oldpipe);
      if (rv >= 0 && qbuf != NULL && qbuf != obuf)
         pipecpy(qbuf, obuf, origobuf, qf, stats);
   }
#ifdef mx_HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
#endif
jleave:
   n_NYD_OU;
   return rv;
}

static FILE *
newfile(struct mimepart *ip, bool_t volatile *ispipe)
{
   struct str in, out;
   char *f;
   FILE *fp;
   n_NYD_IN;

   f = ip->m_filename;
   *ispipe = FAL0;

   if (f != NULL && f != (char*)-1) {
      in.s = f;
      in.l = strlen(f);
      makeprint(&in, &out);
      out.l = delctrl(out.s, out.l);
      f = savestrbuf(out.s, out.l);
      n_free(out.s);
   }

   /* In interactive mode, let user perform all kind of expansions as desired,
    * and offer |SHELL-SPEC pipe targets, too */
   if (n_psonce & n_PSO_INTERACTIVE) {
      struct str prompt;
      struct n_string shou, *shoup;
      char *f2, *f3;

      shoup = n_string_creat_auto(&shou);

      /* TODO Generic function which asks for filename.
       * TODO If the current part is the first textpart the target
       * TODO is implicit from outer `write' etc! */
      /* I18N: Filename input prompt with file type indication */
      str_concat_csvl(&prompt, _("Enter filename for part "),
         (ip->m_partstring != NULL ? ip->m_partstring : n_qm),
         " (", ip->m_ct_type_plain, "): ", NULL);
jgetname:
      f2 = n_go_input_cp(n_GO_INPUT_CTX_DEFAULT | n_GO_INPUT_HIST_ADD,
            prompt.s, ((f != (char*)-1 && f != NULL)
               ? n_shexp_quote_cp(f, FAL0) : NULL));
      if(f2 != NULL){
         in.s = n_UNCONST(f2);
         in.l = UIZ_MAX;
         if((n_shexp_parse_token((n_SHEXP_PARSE_TRUNC |
                  n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_TRIM_IFSSPACE |
                  n_SHEXP_PARSE_LOG | n_SHEXP_PARSE_IGNORE_EMPTY),
                  shoup, &in, NULL
               ) & (n_SHEXP_STATE_STOP |
                  n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_ERR_MASK)
               ) != (n_SHEXP_STATE_STOP | n_SHEXP_STATE_OUTPUT))
            goto jgetname;
         f2 = n_string_cp(shoup);
      }
      if (f2 == NULL || *f2 == '\0') {
         if (n_poption & n_PO_D_V)
            n_err(_("... skipping this\n"));
         n_string_gut(shoup);
         fp = NULL;
         goto jleave;
      }

      if (*f2 == '|')
         /* Pipes are expanded by the shell */
         f = f2;
      else if ((f3 = fexpand(f2, FEXP_LOCAL | FEXP_NVAR)) == NULL)
         /* (Error message written by fexpand()) */
         goto jgetname;
      else
         f = f3;

      n_string_gut(shoup);
   }

   if (f == NULL || f == (char*)-1 || *f == '\0')
      fp = NULL;
   else if (n_psonce & n_PSO_INTERACTIVE) {
      if (*f == '|') {
         fp = Popen(&f[1], "w", ok_vlook(SHELL), NULL, 1);
         if (!(*ispipe = (fp != NULL)))
            n_perr(f, 0);
      } else if ((fp = Fopen(f, "w")) == NULL)
         n_err(_("Cannot open %s\n"), n_shexp_quote_cp(f, FAL0));
   } else {
      /* Be very picky in non-interactive mode: actively disallow pipes,
       * prevent directory separators, and any filename member that would
       * become expanded by the shell if the name would be echo(1)ed */
      if(n_anyof_cp("/" n_SHEXP_MAGIC_PATH_CHARS, f)){
         char c;

         for(out.s = n_autorec_alloc((strlen(f) * 3) +1), out.l = 0;
               (c = *f++) != '\0';)
            if(strchr("/" n_SHEXP_MAGIC_PATH_CHARS, c)){
               out.s[out.l++] = '%';
               n_c_to_hex_base16(&out.s[out.l], c);
               out.l += 2;
            }else
               out.s[out.l++] = c;
         out.s[out.l] = '\0';
         f = out.s;
      }

      /* Avoid overwriting of existing files */
      while((fp = Fopen(f, "wx")) == NULL){
         int e;

         if((e = n_err_no) != n_ERR_EXIST){
            n_err(_("Cannot open %s: %s\n"),
               n_shexp_quote_cp(f, FAL0), n_err_to_doc(e));
            break;
         }

         if(ip->m_partstring != NULL)
            f = savecatsep(f, '#', ip->m_partstring);
         else
            f = savecat(f, "#.");
      }
   }
jleave:
   n_NYD_OU;
   return fp;
}

static void
pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf, struct quoteflt *qf,
   ui64_t *stats)
{
   char *line = NULL; /* TODO line pool */
   size_t linesize = 0, linelen, cnt;
   ssize_t all_sz, sz;
   n_NYD_IN;

   fflush(pipebuf);
   rewind(pipebuf);
   cnt = (size_t)fsize(pipebuf);
   all_sz = 0;

   quoteflt_reset(qf, outbuf);
   while (fgetline(&line, &linesize, &cnt, &linelen, pipebuf, 0) != NULL) {
      if ((sz = quoteflt_push(qf, line, linelen)) < 0)
         break;
      all_sz += sz;
   }
   if ((sz = quoteflt_flush(qf)) > 0)
      all_sz += sz;
   if (line)
      n_free(line);

   if (all_sz > 0 && outbuf == origobuf && stats != NULL)
      *stats += all_sz;
   Fclose(pipebuf);
   n_NYD_OU;
}

static void
statusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
   ui64_t *stats)
{
   char statout[3], *cp = statout;
   n_NYD_IN;

   if (mp->m_flag & MREAD)
      *cp++ = 'R';
   if (!(mp->m_flag & MNEW))
      *cp++ = 'O';
   *cp = 0;
   if (statout[0]) {
      int i = fprintf(obuf, "%.*sStatus: %s\n", (int)qf->qf_pfix_len,
            (qf->qf_bypass ? NULL : qf->qf_pfix), statout);
      if (i > 0 && stats != NULL)
         *stats += i;
   }
   n_NYD_OU;
}

static void
xstatusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
   ui64_t *stats)
{
   char xstatout[4];
   char *xp = xstatout;
   n_NYD_IN;

   if (mp->m_flag & MFLAGGED)
      *xp++ = 'F';
   if (mp->m_flag & MANSWERED)
      *xp++ = 'A';
   if (mp->m_flag & MDRAFTED)
      *xp++ = 'T';
   *xp = 0;
   if (xstatout[0]) {
      int i = fprintf(obuf, "%.*sX-Status: %s\n", (int)qf->qf_pfix_len,
            (qf->qf_bypass ? NULL : qf->qf_pfix), xstatout);
      if (i > 0 && stats != NULL)
         *stats += i;
   }
   n_NYD_OU;
}

static void
put_from_(FILE *fp, struct mimepart *ip, ui64_t *stats)
{
   char const *froma, *date, *nl;
   int i;
   n_NYD_IN;

   if (ip != NULL && ip->m_from != NULL) {
      froma = ip->m_from;
      date = n_time_ctime(ip->m_time, NULL);
      nl = "\n";
   } else {
      froma = ok_vlook(LOGNAME);
      date = time_current.tc_ctime;
      nl = n_empty;
   }

   n_COLOUR(
      if(n_COLOUR_IS_ACTIVE())
         n_colour_put(n_COLOUR_ID_VIEW_FROM_, NULL);
   )
   i = fprintf(fp, "From %s %s%s", froma, date, nl);
   n_COLOUR(
      if(n_COLOUR_IS_ACTIVE())
         n_colour_reset();
   )
   if (i > 0 && stats != NULL)
      *stats += i;
   n_NYD_OU;
}

FL int
sendmp(struct message *mp, FILE *obuf, struct n_ignore const *doitp,
   char const *prefix, enum sendaction action, ui64_t *stats)
{
   struct n_sigman linedat_protect;
   struct quoteflt qf;
   FILE *ibuf;
   enum mime_parse_flags mpf;
   struct mimepart *ip;
   size_t linesize, cnt, sz, i;
   char *linedat;
   int rv, c;
   n_NYD_IN;

   time_current_update(&time_current, TRU1);
   rv = -1;
   linedat = NULL;
   linesize = 0;
   quoteflt_init(&qf, prefix, (prefix == NULL));

   n_SIGMAN_ENTER_SWITCH(&linedat_protect, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      goto jleave;
   }

   if (mp == dot && action != SEND_TOSRCH)
      n_pstate |= n_PS_DID_PRINT_DOT;
   if (stats != NULL)
      *stats = 0;

   /* First line is the From_ line, so no headers there to worry about */
   if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL)
      goto jleave;

   cnt = mp->m_size;
   sz = 0;
   {
   bool_t nozap;
   char const *cpre = n_empty, *csuf = n_empty;

#ifdef mx_HAVE_COLOUR
   if(n_COLOUR_IS_ACTIVE()){
      struct n_colour_pen *cpen;
      struct str const *sp;

      cpen = n_colour_pen_create(n_COLOUR_ID_VIEW_FROM_,NULL);
      if((sp = n_colour_pen_to_str(cpen)) != NULL){
         cpre = sp->s;
         sp = n_colour_reset_to_str();
         if(sp != NULL)
            csuf = sp->s;
      }
   }
#endif

   nozap = (doitp != n_IGNORE_ALL && doitp != n_IGNORE_FWD &&
         action != SEND_RFC822 &&
         !n_ignore_is_ign(doitp, "from_", sizeof("from_") -1));
   if (mp->m_flag & MNOFROM) {
      if (nozap)
         sz = fprintf(obuf, "%s%.*sFrom %s %s%s\n",
               cpre, (int)qf.qf_pfix_len,
               (qf.qf_bypass ? n_empty : qf.qf_pfix), fakefrom(mp),
               n_time_ctime(mp->m_time, NULL), csuf);
   } else if (nozap) {
      if (!qf.qf_bypass) {
         i = fwrite(qf.qf_pfix, sizeof *qf.qf_pfix, qf.qf_pfix_len, obuf);
         if (i != qf.qf_pfix_len)
            goto jleave;
         sz += i;
      }
#ifdef mx_HAVE_COLOUR
      if(*cpre != '\0'){
         fputs(cpre, obuf);
         cpre = (char const*)0x1;
      }
#endif

      while (cnt > 0 && (c = getc(ibuf)) != EOF) {
#ifdef mx_HAVE_COLOUR
         if(c == '\n' && *csuf != '\0'){
            cpre = (char const*)0x1;
            fputs(csuf, obuf);
         }
#endif
         putc(c, obuf);
         ++sz;
         --cnt;
         if (c == '\n')
            break;
      }

#ifdef mx_HAVE_COLOUR
      if(*csuf != '\0' && cpre != (char const*)0x1 && *cpre != '\0')
         fputs(csuf, obuf);
#endif
   } else {
      while (cnt > 0 && (c = getc(ibuf)) != EOF) {
         --cnt;
         if (c == '\n')
            break;
      }
   }
   }
   if (sz > 0 && stats != NULL)
      *stats += sz;

   mpf = MIME_PARSE_NONE;
   if (action != SEND_MBOX && action != SEND_RFC822 && action != SEND_SHOW)
      mpf |= MIME_PARSE_PARTS | MIME_PARSE_DECRYPT;
   if(action == SEND_TODISP || action == SEND_TODISP_ALL ||
         action == SEND_QUOTE || action == SEND_QUOTE_ALL)
      mpf |= MIME_PARSE_FOR_USER_CONTEXT;
   if ((ip = mime_parse_msg(mp, mpf)) == NULL)
      goto jleave;

   rv = sendpart(mp, ip, obuf, doitp, &qf, action, &linedat, &linesize,
         stats, 0);

   n_sigman_cleanup_ping(&linedat_protect);
jleave:
   n_pstate &= ~n_PS_BASE64_STRIP_CR;
   quoteflt_destroy(&qf);
   if(linedat != NULL)
      n_free(linedat);
   n_NYD_OU;
   n_sigman_leave(&linedat_protect, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

/* s-it-mode */
