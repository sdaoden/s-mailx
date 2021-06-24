/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message content preparation (sendmp()).
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
#define su_FILE send
#define mx_SOURCE
#define mx_SOURCE_SEND

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/path.h>

#include "mx/child.h"
#include "mx/compat.h"
#include "mx/colour.h"
#include "mx/file-streams.h"
#include "mx/filter-html.h"
/* TODO but only for creating chain! */
#include "mx/filter-quote.h"
#include "mx/go.h"
#include "mx/iconv.h"
#include "mx/ignore.h"
#include "mx/mime.h"
#include "mx/mime-enc.h"
#include "mx/mime-parse.h"
#include "mx/mime-type.h"
#include "mx/random.h"
#include "mx/sigs.h"
#include "mx/tty.h"
#include "mx/ui-str.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

static sigjmp_buf _send_pipejmp;

/* Going for user display, print Part: info string */
static void          _print_part_info(FILE *obuf, struct mimepart const *mpp,
                        struct mx_ignore const *doitp, int level,
                        struct quoteflt *qf, u64 *stats);

/* Create a pipe; if mpp is not NULL, place some n_PIPEENV_* environment
 * variables accordingly */
static FILE *a_send_pipefile(enum sendaction action,
      struct mx_mime_type_handler *mhp, struct mimepart const *mpp,
      FILE **qbuf, char const *tmpname, int term_infd);

/* Call mime_write() as appropriate and adjust statistics */
su_SINLINE sz _out(char const *buf, uz len, FILE *fp,
      enum conversion convert, enum sendaction action, struct quoteflt *qf,
      u64 *stats, struct str *outrest, struct str *inrest);

/* Simply (!) print out a LF (via qf if not NIL) */
static boole a_send_out_nl(FILE *fp, struct quoteflt *qf, u64 *stats);

/* SIGPIPE handler */
static void          _send_onpipe(int signo);

/* Send one part */
static int           sendpart(struct message *zmp, struct mimepart *ip,
                        FILE *obuf, struct mx_ignore const *doitp,
                        struct quoteflt *qf, enum sendaction action,
                        char **linedat, uz *linesize,
                        u64 *stats, int level, boole *anyoutput/* XXX fake*/);

/* Dependent on *mime-alternative-favour-rich* (favour_rich) do a tree walk
 * and check whether there are any such down mpp, which is a .m_multipart of
 * an /alternative container.. */
static boole        _send_al7ive_have_better(struct mimepart *mpp,
                        enum sendaction action, boole want_rich);

/* Get a file for an attachment */
static FILE *        newfile(struct mimepart *ip, boole volatile *ispipe);

static boole a_send_pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf,
      struct quoteflt *qf, u64 *stats);

/* Output a reasonable looking status field */
static void          statusput(const struct message *mp, FILE *obuf,
                        struct quoteflt *qf, u64 *stats);
static void          xstatusput(const struct message *mp, FILE *obuf,
                        struct quoteflt *qf, u64 *stats);

static void          put_from_(FILE *fp, struct mimepart *ip, u64 *stats);

su_SINLINE sz
_out(char const *buf, uz len, FILE *fp, enum conversion convert, enum
   sendaction action, struct quoteflt *qf, u64 *stats, struct str *outrest,
   struct str *inrest)
{
   sz size = 0, n;
   int flags;
   NYD_IN;

   /* TODO We should not need is_head() here, i think in v15 the actual Mailbox
    * TODO subclass should detect From_ cases and either re-encode the part
    * TODO in question, or perform From_ quoting as necessary!?!?!?  How?!? */
   /* C99 */{
      boole from_;

      if((action == SEND_MBOX || action == SEND_DECRYPT) &&
            (from_ = is_head(buf, len, TRU1))){
         if(from_ != TRUM1 || (mb.mb_active & MB_BAD_FROM_) ||
               ok_blook(mbox_rfc4155)){
            putc('>', fp);
            ++size;
         }
      }
   }

   flags = (S(u32,action) & mx__MIME_DISPLAY_EOF);
   action &= ~mx__MIME_DISPLAY_EOF;
   n = mx_mime_write(buf, len, fp,
         action == SEND_MBOX ? CONV_NONE : convert,
         flags | ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
            action == SEND_TODISP_PARTS ||
            action == SEND_QUOTE || action == SEND_QUOTE_ALL)
         ?  mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT
         : (action == SEND_TOSRCH || action == SEND_TOPIPE ||
               action == SEND_TOFILE)
            ? mx_MIME_DISPLAY_ICONV
            : (action == SEND_SHOW ? mx_MIME_DISPLAY_ISPRINT
                  : mx_MIME_DISPLAY_NONE)),
         qf, outrest, inrest);
   if (n < 0)
      size = n;
   else if (n > 0) {
      size += n;
      if (stats != NULL)
         *stats += size;
   }
   NYD_OU;
   return size;
}

static void
_print_part_info(FILE *obuf, struct mimepart const *mpp, /* TODO strtofmt.. */
   struct mx_ignore const *doitp, int level, struct quoteflt *qf, u64 *stats)
{
   char buf[64];
   struct str ti, to;
   boole want_ct, needsep;
   struct str const *cpre, *csuf;
   char const *cp;
   NYD2_IN;

   cpre = csuf = NULL;
#ifdef mx_HAVE_COLOUR
   if(mx_COLOUR_IS_ACTIVE()){
      struct mx_colour_pen *cpen;

      cpen = mx_colour_pen_create(mx_COLOUR_ID_VIEW_PARTINFO, NULL);
      if((cpre = mx_colour_pen_to_str(cpen)) != NIL)
         csuf = mx_colour_reset_to_str();
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
   _out(cp, su_cs_len(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);

   to.l = snprintf(buf, sizeof buf, " %" PRIuZ "/%" PRIuZ " ",
         (uz)mpp->m_lines, (uz)mpp->m_size);
   _out(buf, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);

   needsep = FAL0;

    if((cp = mpp->m_ct_type_usr_ovwr) != NULL){
      _out("+", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      want_ct = TRU1;
   }else if((want_ct = mx_ignore_is_ign(doitp, "content-type")))
      cp = mpp->m_ct_type_plain;
   if (want_ct && (to.l = su_cs_len(cp)) > 30 &&
            su_cs_starts_with_case(cp, "application/")) {
      uz const al = sizeof("appl../") -1, fl = sizeof("application/") -1;
      uz i = to.l - fl;
      char *x = n_autorec_alloc(al + i +1);

      su_mem_copy(x, "appl../", al);
      su_mem_copy(x + al, cp + fl, i +1);
      cp = x;
      to.l = al + i;
   }
   if(cp != NULL){
      _out(cp, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      needsep = TRU1;
   }

   if(mpp->m_multipart == NULL/* TODO */ && (cp = mpp->m_ct_enc) != NULL &&
         (!su_cs_cmp_case(cp, "7bit") ||
          mx_ignore_is_ign(doitp, "content-transfer-encoding"))){
      if(needsep)
         _out(", ", 2, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      if (to.l > 25 && !su_cs_cmp_case(cp, "quoted-printable"))
         cp = "qu.-pr.";
      _out(cp, su_cs_len(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats,
         NULL,NULL);
      needsep = TRU1;
   }

   if (want_ct && mpp->m_multipart == NULL/* TODO */ &&
         (cp = mpp->m_charset) != NULL) {
      if(needsep)
         _out(", ", 2, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL,NULL);
      _out(cp, su_cs_len(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats,
         NULL,NULL);
   }

   needsep = !needsep;
   _out(&" --]"[su_S(su_u8,needsep)], 4 - needsep,
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

      ti.l = su_cs_len(ti.s = n_UNCONST(_("Defective MIME structure")));
      mx_makeprint(&ti, &to);
      to.l = mx_del_cntrl(to.s, to.l);
      _out(to.s, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      n_free(to.s);

      _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      if (csuf != NULL)
         _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
   }

   /* Content-Description */
   if (mx_ignore_is_ign(doitp, "content-description") &&
         (cp = mpp->m_content_description) != NULL && *cp != '\0') {
      if (cpre != NULL)
         _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("[-- ", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);

      ti.l = su_cs_len(ti.s = n_UNCONST(mpp->m_content_description));
      mx_mime_display_from_header(&ti, &to,
         mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT);
      _out(to.s, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      n_free(to.s);

      _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      if (csuf != NULL)
         _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
   }

   /* Filename */
   if (mx_ignore_is_ign(doitp, "content-disposition") &&
         mpp->m_filename != NULL && *mpp->m_filename != '\0') {
      if (cpre != NULL)
         _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("[-- ", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);

      ti.l = su_cs_len(ti.s = mpp->m_filename);
      mx_makeprint(&ti, &to);
      to.l = mx_del_cntrl(to.s, to.l);
      _out(to.s, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      n_free(to.s);

      _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
      if (csuf != NULL)
         _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats,
            NULL, NULL);
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL, NULL);
   }
   NYD2_OU;
}

static FILE *
a_send_pipefile(enum sendaction action, struct mx_mime_type_handler *mthp,
      struct mimepart const *mpp, FILE **qbuf, char const *tmpname,
      int term_infd)
{
   static u32 reprocnt;
   struct str s;
   char const *env_addon[9 +8/*v15*/], *cp, *sh;
   uz i;
   FILE *rbuf;
   NYD_IN;

   rbuf = *qbuf;

   if(action == SEND_QUOTE || action == SEND_QUOTE_ALL){
      if((*qbuf = mx_fs_tmp_open(NIL, "sendp", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
               NIL)) == NIL){
         n_perr(_("tmpfile"), 0);
         *qbuf = rbuf;
      }
   }

#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
   if((mthp->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK) == mx_MIME_TYPE_HDL_HTML){
      union {int (*ptf)(void); char const *sh;} u;

      fflush(*qbuf);
      if (*qbuf != n_stdout) /* xxx never?  v15: it'll be a filter anyway */
         fflush(n_stdout);

      u.ptf = &mx_flthtml_process_main;
      if((rbuf = mx_fs_pipe_open(R(char*,-1), mx_FS_PIPE_WRITE, u.sh, NIL,
               fileno(*qbuf))) == NIL)
         goto jerror;
      goto jleave;
   }
#endif

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
   cp = mx_random_create_cp(MIN(NAME_MAX - 3, 16), &reprocnt);
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

   if(mthp->mth_flags & mx_MIME_TYPE_HDL_NEEDSTERM){
      struct mx_child_ctx cc;
      sigset_t nset;

      sigemptyset(&nset);
      mx_child_ctx_setup(&cc);
      cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
      cc.cc_mask = &nset;
      cc.cc_fds[mx_CHILD_FD_IN] = term_infd;
      cc.cc_cmd = sh;
      cc.cc_args[0] = "-c";
      cc.cc_args[1] = mthp->mth_shell_cmd;
      cc.cc_env_addon = env_addon;

      rbuf = !mx_child_run(&cc) ? NIL : R(FILE*,-1);
   }else{
      rbuf = mx_fs_pipe_open(mthp->mth_shell_cmd, mx_FS_PIPE_WRITE, sh,
            env_addon, ((mthp->mth_flags & mx_MIME_TYPE_HDL_ASYNC)
               ? mx_CHILD_FD_NULL : fileno(*qbuf)));
jerror:
      if(rbuf == NIL)
         n_err(_("Cannot run MIME type handler: %s: %s\n"),
            mthp->mth_msg, su_err_doc(su_err_no()));
      else{
         fflush(*qbuf);
         if(*qbuf != n_stdout)
            fflush(n_stdout);
      }
   }
jleave:
   NYD_OU;
   return rbuf;
}

static boole
a_send_out_nl(FILE *fp, struct quoteflt *qf, u64 *stats){
   boole rv;
   NYD2_IN;

   if(qf == NIL)
      qf = quoteflt_dummy();

   quoteflt_reset(qf, fp);
   rv = (_out("\n", 1, fp, CONV_NONE, SEND_MBOX, qf, stats, NIL,NIL) > 0);
   quoteflt_flush(qf);
   NYD2_OU;
   return rv;
}

static void
_send_onpipe(int signo)
{
   NYD; /* Signal handler */
   UNUSED(signo);
   siglongjmp(_send_pipejmp, 1);
}

static sigjmp_buf       __sendp_actjmp; /* TODO someday.. */
static int              __sendp_sig; /* TODO someday.. */
static n_sighdl_t  __sendp_opipe;
static void
__sendp_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD; /* Signal handler */
   __sendp_sig = sig;
   siglongjmp(__sendp_actjmp, 1);
}

static int
sendpart(struct message *zmp, struct mimepart *ip, FILE * volatile obuf,
   struct mx_ignore const *doitp, struct quoteflt *qf,
   enum sendaction volatile action,
   char **linedat, uz *linesize, u64 * volatile stats, int volatile level,
   boole *anyoutput)
{
   int volatile rv = 0;
   struct mx_mime_type_handler mth_stack, * volatile mthp;
   struct str outrest, inrest;
   boole hany, hign;
   enum sendaction oaction;
   char *cp;
   char const * volatile tmpname = NULL;
   uz linelen, cnt;
   int volatile dostat, term_infd;
   int c;
   struct mimepart * volatile np;
   FILE * volatile ibuf = NULL, * volatile pbuf = obuf,
      * volatile qbuf = obuf, *origobuf = obuf;
   enum conversion volatile convert;
   n_sighdl_t volatile oldpipe = SIG_DFL;
   NYD_IN;

   UNINIT(term_infd, 0);
   UNINIT(cnt, 0);
   oaction = action;
   hany = hign = FAL0;

   quoteflt_reset(qf, obuf);

   if((ibuf = setinput(&mb, R(struct message*,ip), NEED_BODY)) == NIL){
      rv = -1;
      goto jleave;
   }

   cnt = ip->m_size;
   dostat = 0;

   if(action == SEND_TODISP || action == SEND_TODISP_ALL ||
         action == SEND_TODISP_PARTS ||
         action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
         action == SEND_TOSRCH){
      dostat |= 4;

      if(ip->m_mime_type != mx_MIME_TYPE_DISCARD && level != 0 &&
            action != SEND_QUOTE && action != SEND_QUOTE_ALL){
         _print_part_info(obuf, ip, doitp, level, qf, stats);
         hany = TRU1;
      }
      if(ip->m_parent != NIL && ip->m_parent->m_mime_type == mx_MIME_TYPE_822){
         ASSERT(ip->m_flag & MNOFROM);
         hign = TRU1;
      }
   }

   if(ip->m_mime_type == mx_MIME_TYPE_DISCARD)
      goto jheaders_skip;

   if (ip->m_mime_type == mx_MIME_TYPE_PKCS7) {
      if (ip->m_multipart &&
            action != SEND_MBOX && action != SEND_RFC822 && action != SEND_SHOW)
         goto jheaders_skip;
   }

   if(!(ip->m_flag & MNOFROM))
      while(cnt > 0 && (c = getc(ibuf)) != EOF){
         --cnt;
         if(c == '\n')
            break;
      }

   if(!hign && level == 0 && action != SEND_TODISP_PARTS){
      if(doitp != NIL){
         if(!mx_ignore_is_ign(doitp, "status"))
            dostat |= 1;
         if(!mx_ignore_is_ign(doitp, "x-status"))
            dostat |= 2;
      }else
         dostat |= 3;
   }

jhdr_redo:
   convert = (dostat & 4) ? CONV_FROMHDR : CONV_NONE;

   /* Work the headers */
   /* C99 */{
   struct n_string hl, *hlp;
   uz lineno;
   boole hstop;

   hlp = n_string_creat_auto(&hl); /* TODO pool [or, v15: filter!] */
   /* Reserve three lines, still not enough for references and DKIM etc. */
   hlp = n_string_reserve(hlp, MAX(MIME_LINELEN, MIME_LINELEN_RFC2047) * 3);
   lineno = 0;

   for(hstop = FAL0; !hstop;){
      uz lcnt;

      lcnt = cnt;
      if(fgetline(linedat, linesize, &cnt, &linelen, ibuf, FAL0) == NIL)
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
         if(!su_cs_is_blank(*cp)){
            fseek(ibuf, -(off_t)(lcnt - cnt), SEEK_CUR);
            cnt = lcnt;
            goto jhdrput;
         }
         goto jhdrpush;
      }else{
         /* Pick up the header field if we have one */
         while((c = *cp) != ':' && !su_cs_is_space(c) && c != '\0')
            ++cp;
         for(;;){
            if(!su_cs_is_space(c) || c == '\0')
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
            hlp = n_string_push_buf(hlp, cp, (u32)linelen);
            hlp = n_string_push_c(hlp, '\n');
         }else{
            boole lblank, xblank;

            for(lblank = FAL0, lcnt = 0; lcnt < linelen; ++cp, ++lcnt){
               char c8;

               c8 = *cp;
               if(!(xblank = su_cs_is_blank(c8)) || !lblank){
                  if((lblank = xblank))
                     c8 = ' ';
                  hlp = n_string_push_c(hlp, c8);
               }
            }
         }
         continue;
      }

jhdrput:
      /* If it is an ignored header, skip it */
      *(cp = su_mem_find(hlp->s_dat, ':', hlp->s_len)) = '\0';
      /* C99 */{
         if(hign || (doitp != NULL &&
                  mx_ignore_is_ign(doitp, hlp->s_dat)) ||
               !su_cs_cmp_case(hlp->s_dat, "status") ||
               !su_cs_cmp_case(hlp->s_dat, "x-status") ||
               (action == SEND_MBOX &&
                  (!su_cs_cmp_case(hlp->s_dat, "content-length") ||
                   !su_cs_cmp_case(hlp->s_dat, "lines")) &&
                !ok_blook(keep_content_length)))
            goto jhdrtrunc;
      }

      /* Dump it */
      if(!hany && (dostat & 4) && level > 0)
         a_send_out_nl(obuf, NIL, stats);
      mx_COLOUR(
         if(mx_COLOUR_IS_ACTIVE())
            mx_colour_put(mx_COLOUR_ID_VIEW_HEADER, hlp->s_dat);
      )
      *cp = ':';
      _out(hlp->s_dat, hlp->s_len, obuf, convert, action, qf, stats, NIL,NIL);
      mx_COLOUR(
         if(mx_COLOUR_IS_ACTIVE())
            mx_colour_reset();
      )
      if(dostat & 4)
         a_send_out_nl(obuf, qf, stats);
      hany = TRU1;

jhdrtrunc:
      hlp = n_string_trunc(hlp, 0);
   }
   hstop = TRU1;
   if(hlp->s_len > 0)
      goto jhdrput;

   if(hign /*|| (!hany && (dostat & (1 | 2)))*/){
      a_send_out_nl(obuf, qf, stats);
      if(hign)
         goto jheaders_skip;
   }

   /* We have reached end of headers, so eventually force out status: field
    * and note that we are no longer in header fields */
   if(dostat & 1){
      statusput(zmp, obuf, qf, stats);
      hany = TRU1;
   }
   if(dostat & 2){
      xstatusput(zmp, obuf, qf, stats);
      hany = TRU1;
   }
   if((hany /*&& doitp != IGNORE_ALL*/) ||
         (oaction == SEND_DECRYPT && ip->m_parent != NIL &&
          ip != ip->m_multipart) ||
         ((oaction == SEND_QUOTE || oaction == SEND_QUOTE_ALL) &&
          level != 0 && *anyoutput &&
           (ip->m_mime_type != mx_MIME_TYPE_DISCARD &&
            ip->m_mime_type != mx_MIME_TYPE_PKCS7 &&
            ip->m_mime_type != mx_MIME_TYPE_PKCS7 &&
            ip->m_mime_type != mx_MIME_TYPE_ALTERNATIVE &&
            ip->m_mime_type != mx_MIME_TYPE_RELATED &&
            ip->m_mime_type != mx_MIME_TYPE_DIGEST &&
            ip->m_mime_type != mx_MIME_TYPE_MULTI &&
            ip->m_mime_type != mx_MIME_TYPE_SIGNED &&
            ip->m_mime_type != mx_MIME_TYPE_ENCRYPTED))){
         if(ip->m_mime_type != mx_MIME_TYPE_822 || (dostat & 16)){
            /*XXX (void)*/a_send_out_nl(obuf, NIL, stats);
            hany = TRU1;
         }
      }
   } /* C99 */

   quoteflt_flush(qf);

   if(ferror(ibuf) || ferror(obuf)){
      rv = -1;
      goto jleave;
   }

   *anyoutput = hany;
jheaders_skip:
   su_mem_set(mthp = &mth_stack, 0, sizeof mth_stack);

   if(oaction == SEND_MBOX){
      convert = CONV_NONE;
      goto jsend;
   }

   switch(ip->m_mime_type){
   case mx_MIME_TYPE_822:
      switch (action) {
      case SEND_TODISP_PARTS:
         goto jleave;
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         if(!(dostat & 16)){ /* XXX */
            dostat |= 16;
            a_send_out_nl(obuf, qf, stats);
            if(ok_blook(rfc822_body_from_)){
               if(!qf->qf_bypass){
                  uz i;

                  i = fwrite(qf->qf_pfix, sizeof *qf->qf_pfix, qf->qf_pfix_len,
                        obuf);
                  if(i == qf->qf_pfix_len && stats != NIL)
                     *stats += i;
               }
               put_from_(obuf, ip->m_multipart, stats);
               hany = TRU1;
            }
            goto jhdr_redo;
         }
         goto jmulti;
      case SEND_TOSRCH:
         goto jmulti;
      case SEND_DECRYPT:
         goto jmulti;
      case SEND_TOFILE:
      case SEND_TOPIPE:
         put_from_(obuf, ip->m_multipart, stats);
         /* FALLTHRU */
      default:
         break;
      }
      break;
   case mx_MIME_TYPE_TEXT_HTML:
   case mx_MIME_TYPE_TEXT:
   case mx_MIME_TYPE_TEXT_PLAIN:
      switch (action) {
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_TODISP_PARTS:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         if((mthp = ip->m_handler) == NIL)
            mx_mime_type_handler(mthp =
               ip->m_handler = su_AUTO_ALLOC(sizeof(*mthp)), ip, oaction);
         switch(mthp->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK){
         case mx_MIME_TYPE_HDL_NIL:
            if(oaction != SEND_TODISP_PARTS)
               break;
            /* FALLTHRU */
         case mx_MIME_TYPE_HDL_MSG:/* TODO these should be part of partinfo! */
            if(mthp->mth_msg.l > 0)
               _out(mthp->mth_msg.s, mthp->mth_msg.l, obuf, CONV_NONE,
                  SEND_MBOX, qf, stats, NULL, NULL);
            /* We would print this as plain text, so better force going home */
            goto jleave;
         case mx_MIME_TYPE_HDL_CMD:
            if(oaction == SEND_TODISP_PARTS){
               if(mthp->mth_flags & mx_MIME_TYPE_HDL_COPIOUSOUTPUT)
                  goto jleave;
               else{
                  /* Because: interactive OR batch mode, so */
                  if(!mx_tty_yesorno(_("Run MIME handler for this part?"),
                        su_state_has(su_STATE_REPRODUCIBLE)))
                     goto jleave;
               }
            }
            break;
         case mx_MIME_TYPE_HDL_TEXT:
         case mx_MIME_TYPE_HDL_HTML:
            if(oaction == SEND_TODISP_PARTS)
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
   case mx_MIME_TYPE_DISCARD:
      if(oaction != SEND_DECRYPT)
         goto jleave;
      break;
   case mx_MIME_TYPE_PKCS7:
      if(oaction != SEND_RFC822 && oaction != SEND_SHOW &&
            ip->m_multipart != NIL)
         goto jmulti;
      /* FALLTHRU */
   default:
      switch (action) {
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_TODISP_PARTS:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         if((mthp = ip->m_handler) == NIL)
            mx_mime_type_handler(mthp = ip->m_handler =
               su_AUTO_ALLOC(sizeof(*mthp)), ip, oaction);
         switch(mthp->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK){
         default:
         case mx_MIME_TYPE_HDL_NIL:
            if (oaction != SEND_TODISP && oaction != SEND_TODISP_ALL &&
                  (level != 0 || cnt))
               goto jleave;
            /* FALLTHRU */
         case mx_MIME_TYPE_HDL_MSG:/* TODO these should be part of partinfo! */
            if(mthp->mth_msg.l > 0)
               _out(mthp->mth_msg.s, mthp->mth_msg.l, obuf, CONV_NONE,
                  SEND_MBOX, qf, stats, NULL, NULL);
            /* We would print this as plain text, so better force going home */
            goto jleave;
         case mx_MIME_TYPE_HDL_CMD:
            if(oaction == SEND_TODISP_PARTS){
               if(mthp->mth_flags & mx_MIME_TYPE_HDL_COPIOUSOUTPUT)
                  goto jleave;
               else{
                  /* Because: interactive OR batch mode, so */
                  if(!mx_tty_yesorno(_("Run MIME handler for this part?"),
                        su_state_has(su_STATE_REPRODUCIBLE)))
                     goto jleave;
               }
            }
            break;
         case mx_MIME_TYPE_HDL_TEXT:
         case mx_MIME_TYPE_HDL_HTML:
            if(oaction == SEND_TODISP_PARTS)
               goto jleave;
            break;
         }
         break;
      default:
         break;
      }
      break;
   case mx_MIME_TYPE_ALTERNATIVE:
      if ((oaction == SEND_TODISP || oaction == SEND_QUOTE) &&
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
         enum {
            _NONE,
            _DORICH  = 1<<0,  /* We are looking for rich parts */
            _HADPART = 1<<1,  /* Did print a part already */
            _NEEDNL  = 1<<3   /* Need a visual separator */
         } flags;
         struct n_sigman smalter;

         (curr = &outermost)->outer = NULL;
         curr->mp = ip;
         flags = ok_blook(mime_alternative_favour_rich) ? _DORICH : _NONE;
         if(!_send_al7ive_have_better(ip->m_multipart, action,
               ((flags & _DORICH) != 0)))
            flags ^= _DORICH;

         n_SIGMAN_ENTER_SWITCH(&smalter, n_SIGMAN_ALL) {
         case 0:
            break;
         default:
            rv = -1;
            goto jalter_leave;
         }

         for(np = ip->m_multipart;;){
jalter_redo:
            level = -ABS(level);
            for(; np != NIL; np = np->m_nextpart){
               level = -ABS(level);
               flags |= _NEEDNL;

               switch(np->m_mime_type){
               case mx_MIME_TYPE_ALTERNATIVE:
               case mx_MIME_TYPE_RELATED:
               case mx_MIME_TYPE_DIGEST:
               case mx_MIME_TYPE_SIGNED:
               case mx_MIME_TYPE_ENCRYPTED:
               case mx_MIME_TYPE_MULTI:
                  np->m_flag &= ~MDISPLAY;
                  mpsp = n_autorec_alloc(sizeof *mpsp);
                  mpsp->outer = curr;
                  mpsp->mp = np->m_multipart;
                  curr->mp = np;
                  curr = mpsp;
                  np = mpsp->mp;
                  flags &= ~_NEEDNL;
                  goto jalter_redo;
               default:
                  if(!(np->m_flag & MDISPLAY)){
                     if(np->m_mime_type != mx_MIME_TYPE_DISCARD &&
                           (action == SEND_TODISP ||
                            action == SEND_TODISP_ALL ||
                            action == SEND_TODISP_PARTS))
                        _print_part_info(obuf, np, doitp, level, qf, stats);
                     break;
                  }

                  /* This thing we are going to do */
                  quoteflt_flush(qf);
                  flags |= _HADPART;
                  flags &= ~_NEEDNL;
                  rv = ABS(level) + 1;
                  if(level < 0){
                     level = -level;
                     rv = -rv;
                  }
                  rv = sendpart(zmp, np, obuf, doitp, qf, oaction,
                        linedat, linesize, stats, rv, anyoutput);
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
   case mx_MIME_TYPE_RELATED:
   case mx_MIME_TYPE_DIGEST:
   case mx_MIME_TYPE_SIGNED:
   case mx_MIME_TYPE_ENCRYPTED:
   case mx_MIME_TYPE_MULTI:
      switch(action){
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
         if((oaction == SEND_TODISP || oaction == SEND_TODISP_ALL) &&
             ip->m_multipart != NIL &&
             ip->m_multipart->m_mime_type == mx_MIME_TYPE_DISCARD &&
             ip->m_multipart->m_nextpart == NULL) {
            char const *x = _("[Missing multipart boundary - use `show' "
                  "to display the raw message]\n");
            _out(x, su_cs_len(x), obuf, CONV_NONE, SEND_MBOX, qf, stats,
               NULL,NULL);
         }

         level = -ABS(level);
         for (np = ip->m_multipart; np != NULL; np = np->m_nextpart) {
            boole volatile ispipe;

            if(np->m_mime_type == mx_MIME_TYPE_DISCARD &&
                  oaction != SEND_DECRYPT)
               continue;

            ispipe = FAL0;
            switch(oaction){
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
               if(np->m_multipart != NULL &&
                     np->m_mime_type != mx_MIME_TYPE_822){
                  if((obuf = mx_fs_open(su_path_dev_null, (mx_FS_O_WRONLY |
                           mx_FS_O_CREATE | mx_FS_O_TRUNC))) == NIL)
                     continue;
               }else if((obuf = newfile(np, &ispipe)) == NIL)
                  continue;
               if(ispipe){
                  oldpipe = safe_signal(SIGPIPE, &_send_onpipe);
                  if(sigsetjmp(_send_pipejmp, 1)){
                     rv = -1;
                     goto jpipe_close;
                  }
               }
               break;
            default:
               break;
            }

            quoteflt_flush(qf);
            {
               int nlvl = ABS(level) + 1;
               if(level < 0){
                  level = -level;
                  nlvl = -nlvl;
               }
               if(sendpart(zmp, np, obuf, doitp, qf, oaction, linedat,
                     linesize, stats, nlvl, anyoutput) < 0)
                  rv = -1;
            }
            quoteflt_reset(qf, origobuf);

            if(oaction == SEND_QUOTE){
               if(ip->m_mime_type != mx_MIME_TYPE_RELATED)
                  break;
            }
            if(oaction == SEND_TOFILE && obuf != origobuf){
               if(!ispipe)
                  mx_fs_close(obuf);
               else {
jpipe_close:
                  mx_fs_pipe_close(obuf, TRU1);
                  safe_signal(SIGPIPE, oldpipe);
               }
            }
         }
         goto jleave;
      default:
         break;
      }
      break;
   }

   /* Copy out message body */
   if (doitp == mx_IGNORE_ALL && level == 0) /* skip final blank line */
      --cnt;
   switch (ip->m_mime_enc) {
   case mx_MIME_ENC_BIN:
   case mx_MIME_ENC_7B:
   case mx_MIME_ENC_8B:
      convert = CONV_NONE;
      break;
   case mx_MIME_ENC_QP:
      convert = CONV_FROMQP;
      break;
   case mx_MIME_ENC_B64:
      switch(ip->m_mime_type){
      case mx_MIME_TYPE_TEXT:
      case mx_MIME_TYPE_TEXT_PLAIN:
      case mx_MIME_TYPE_TEXT_HTML:
         convert = CONV_FROMB64_T;
         break;
      default:
         switch (mthp->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK) {
         case mx_MIME_TYPE_HDL_TEXT:
         case mx_MIME_TYPE_HDL_HTML:
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

   if(oaction == SEND_DECRYPT || oaction == SEND_MBOX ||
         oaction == SEND_RFC822 || oaction == SEND_SHOW)
      convert = CONV_NONE;
#ifdef mx_HAVE_ICONV
   else if((oaction == SEND_TODISP || oaction == SEND_TODISP_ALL ||
            oaction == SEND_TODISP_PARTS ||
            oaction == SEND_QUOTE || oaction == SEND_QUOTE_ALL ||
            oaction == SEND_TOSRCH || oaction == SEND_TOFILE) &&
         (ip->m_mime_type == mx_MIME_TYPE_TEXT_PLAIN ||
            ip->m_mime_type == mx_MIME_TYPE_TEXT_HTML ||
            ip->m_mime_type == mx_MIME_TYPE_TEXT ||
            (mthp->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK
               ) == mx_MIME_TYPE_HDL_TEXT ||
            (mthp->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK
               ) == mx_MIME_TYPE_HDL_HTML)) {
      char const *tcs;

      tcs = ok_vlook(ttycharset);
      if (su_cs_cmp_case(tcs, ip->m_charset) &&
            su_cs_cmp_case(ok_vlook(charset_7bit), ip->m_charset)) {
         iconvd = n_iconv_open(tcs, ip->m_charset);
         if (iconvd == (iconv_t)-1 && su_err_no() == su_ERR_INVAL) {
            n_err(_("Cannot convert from %s to %s\n"), ip->m_charset, tcs);
            /*rv = 1; goto jleave;*/
         }
      }
   }
#endif

   switch (mthp->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK) {
   case mx_MIME_TYPE_HDL_CMD:
      if(!(mthp->mth_flags & mx_MIME_TYPE_HDL_COPIOUSOUTPUT)){
         if(oaction != SEND_TODISP_PARTS)
            goto jmthp_default;
         /* FIXME Ach, what a hack!  We need filters.. v15! */
         if(convert != CONV_FROMB64_T)
            action = SEND_TOPIPE;
      }
      /* FALLTHRU */
   case mx_MIME_TYPE_HDL_HTML:
      tmpname = NULL;
      qbuf = obuf;

      term_infd = mx_CHILD_FD_PASS;
      if(mthp->mth_flags &
            (mx_MIME_TYPE_HDL_TMPF | mx_MIME_TYPE_HDL_NEEDSTERM)){
         struct mx_fs_tmp_ctx *fstcp;
         char const *pref;
         BITENUM_IS(u32,mx_fs_oflags) of;

         of = mx_FS_O_RDWR;

         if(!(mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF)){
            term_infd = 0;
            mthp->mth_flags |= mx_MIME_TYPE_HDL_TMPF_FILL;
            of |= mx_FS_O_UNLINK;
            pref = "mtanonfill";
         }else{
            /* (async and unlink are mutual exclusive) */
            if(mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF_UNLINK)
               of |= mx_FS_O_REGISTER_UNLINK;

            if(mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF_NAMETMPL){
               pref = mthp->mth_tmpf_nametmpl;
               if(mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF_NAMETMPL_SUFFIX)
                  of |= mx_FS_O_SUFFIX;
            }else if(mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF_FILL)
               pref = "mimetypefill";
            else
               pref = "mimetype";
         }

         if((pbuf = mx_fs_tmp_open(NIL, pref, of,
                  (mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF ? &fstcp : NIL))
               ) == NIL)
            goto jesend;

         if(mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF)
            tmpname = fstcp->fstc_filename; /* In autorec storage! */

         if(mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF_FILL){
            action = SEND_TOPIPE;
            if(term_infd == 0)
               term_infd = fileno(pbuf);
            goto jsend;
         }
      }

jpipe_for_real:
      pbuf = a_send_pipefile(oaction, mthp, ip, UNVOLATILE(FILE**,&qbuf),
            tmpname, term_infd);
      if(pbuf == NIL){
jesend:
         pbuf = qbuf = NIL;
         rv = -1;
         goto jend;
      }else if((mthp->mth_flags & mx_MIME_TYPE_HDL_NEEDSTERM) &&
            pbuf == R(FILE*,-1)){
         pbuf = qbuf = NIL;
         goto jend;
      }
      tmpname = NIL;

      action = SEND_TOPIPE;
      if (pbuf != qbuf) {
         oldpipe = safe_signal(SIGPIPE, &_send_onpipe);
         if (sigsetjmp(_send_pipejmp, 1))
            goto jend;
      }
      break;

   default:
jmthp_default:
      mthp->mth_flags = mx_MIME_TYPE_HDL_NIL;
      pbuf = qbuf = obuf;
      break;
   }

jsend:
   {
   boole volatile eof;
   boole save_qf_bypass = qf->qf_bypass;
   u64 *save_stats = stats;

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
         n_pstate &= ~n_PS_BASE64_STRIP_CR;/* (but outer sigman protected) */
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
   if(dostat & 4){
      if(pbuf == origobuf) /* TODO */
         n_pstate |= n_PS_BASE64_STRIP_CR;
   }
   while(!eof && fgetline(linedat, linesize, &cnt, &linelen, ibuf, FAL0)){
joutln:
      if (_out(*linedat, linelen, pbuf, convert, action, qf, stats, &outrest,
               (action & mx__MIME_DISPLAY_EOF ? NIL : &inrest)) < 0 ||
            ferror(pbuf)) {
         rv = -1; /* XXX Should bail away?! */
         break;
      }
   }
   if(ferror(ibuf))
      rv = -1;
   if(eof <= FAL0 && rv >= 0 && (outrest.l != 0 || inrest.l != 0)){
      linelen = 0;
      if(eof || inrest.l == 0)
         action |= mx__MIME_DISPLAY_EOF;
      eof = eof ? TRU1 : TRUM1;
      goto joutln;
   }
   n_pstate &= ~n_PS_BASE64_STRIP_CR;
   action &= ~mx__MIME_DISPLAY_EOF;

   /* TODO HACK: when sending to the display we yet get fooled if a message
    * TODO doesn't end in a newline, because of our input/output 1:1.
    * TODO This should be handled automatically by a display filter, then */
   if(rv >= 0 && !qf->qf_nl_last &&
         (action == SEND_TODISP || action == SEND_TODISP_ALL ||
          action == SEND_QUOTE || action == SEND_QUOTE_ALL))
      rv = quoteflt_push(qf, "\n", 1);

   quoteflt_flush(qf);

   if(!(qf->qf_bypass = save_qf_bypass))
      *anyoutput = TRU1;
   stats = save_stats;

   if (rv >= 0 && (mthp->mth_flags & mx_MIME_TYPE_HDL_TMPF_FILL)) {
      int e;

      mthp->mth_flags &= ~mx_MIME_TYPE_HDL_TMPF_FILL;
      fflush(pbuf);
      really_rewind(pbuf, e);
      if(!e)
         /* No fs_close() a tmp_open() thing due to FS_O_UNREGISTER_UNLINK++ */
         goto jpipe_for_real;
      rv = -1;
   }

   if (pbuf == qbuf)
      safe_signal(SIGPIPE, __sendp_opipe);

   if (outrest.s != NULL)
      n_free(outrest.s);
   if (inrest.s != NULL)
      n_free(inrest.s);
   }

jend:
   if(pbuf != qbuf){
      mx_fs_pipe_close(pbuf, !(mthp->mth_flags & mx_MIME_TYPE_HDL_ASYNC));
      safe_signal(SIGPIPE, oldpipe);
      if (rv >= 0 && qbuf != NULL && qbuf != obuf){
         *anyoutput = TRU1;
         if(!a_send_pipecpy(qbuf, obuf, origobuf, qf, stats))
            rv = -1;
      }
   }

#ifdef mx_HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
#endif

jleave:
   NYD_OU;
   return rv;
}

static boole
_send_al7ive_have_better(struct mimepart *mpp, enum sendaction action,
      boole want_rich){
   struct mimepart *plain, *rich;
   boole rv;
   NYD_IN;

   rv = FAL0;
   plain = rich = NIL;

   for(; mpp != NIL; mpp = mpp->m_nextpart){
      switch(mpp->m_mime_type){
      case mx_MIME_TYPE_TEXT_PLAIN:
         plain = mpp;
         if(!want_rich)
            goto jfound;
         continue;
      case mx_MIME_TYPE_ALTERNATIVE:
      case mx_MIME_TYPE_RELATED:
      case mx_MIME_TYPE_DIGEST:
      case mx_MIME_TYPE_SIGNED:
      case mx_MIME_TYPE_ENCRYPTED:
      case mx_MIME_TYPE_MULTI:
         /* Be simple and recurse */
         if(_send_al7ive_have_better(mpp->m_multipart, action, want_rich))
            goto jleave;
         continue;
      default:
         break;
      }

      if(mpp->m_handler == NIL)
         mx_mime_type_handler(mpp->m_handler =
            su_AUTO_ALLOC(sizeof(*mpp->m_handler)), mpp, action);

      switch(mpp->m_handler->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK){
      case mx_MIME_TYPE_HDL_TEXT:
         if(!want_rich)
            goto jfound;
         if(plain == NIL)
            plain = mpp;
         break;
      case mx_MIME_TYPE_HDL_HTML:
         if(want_rich)
            goto jfound;
         if(rich == NIL ||
               (rich->m_handler->mth_flags & mx_MIME_TYPE_HDL_TYPE_MASK
                  ) != mx_MIME_TYPE_HDL_HTML)
            rich = mpp;
         break;
      case mx_MIME_TYPE_HDL_CMD:
         if(mpp->m_handler->mth_flags & mx_MIME_TYPE_HDL_COPIOUSOUTPUT){
            if(want_rich)
               goto jfound;
            if(rich == NIL)
               rich = mpp;
         }
         /* FALLTHRU */
      default:
         break;
      }
   }

   /* Without plain part at all, choose an existing rich no matter what */
   if((mpp = plain) != NIL || (mpp = rich) != NIL){
jfound:
      mpp->m_flag |= MDISPLAY;
      ASSERT(mpp->m_parent != NIL);
      mpp->m_parent->m_flag |= MDISPLAY;
      rv = TRU1;
   }

jleave:
   NYD_OU;
   return rv;
}

static FILE *
newfile(struct mimepart *ip, boole volatile *ispipe)
{
   struct str in, out;
   char *f;
   FILE *fp;
   NYD_IN;

   f = ip->m_filename;
   *ispipe = FAL0;

   if (f != NULL && f != (char*)-1) {
      in.s = f;
      in.l = su_cs_len(f);
      mx_makeprint(&in, &out);
      out.l = mx_del_cntrl(out.s, out.l);
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

      /* TODO If the current part is the first textpart the target
       * TODO is implicit from outer `write' etc! */
      /* I18N: Filename input prompt with file type indication */
      str_concat_csvl(&prompt, _("Enter filename for part "),
         (ip->m_partstring != NULL ? ip->m_partstring : n_qm),
         " (", ip->m_ct_type_plain, "): ", NULL);
jgetname:
      while(mx_tty_getfilename(shoup,
            (mx_GO_INPUT_CTX_DEFAULT | mx_GO_INPUT_HIST_ADD), prompt.s,
            ((f != R(char*,-1) && f != NIL) ? n_shexp_quote_cp(f, FAL0) : NIL
            )) < TRU1){
      }

      f2 = n_string_cp(shoup);
      if(*f2 == '\0') {
         if(n_poption & n_PO_D_V)
            n_err(_("... skipping this\n"));
         n_string_gut(shoup);
         fp = NIL;
         goto jleave;
      }

      if(*f2 == '|')
         /* Pipes are expanded by the shell */
         f = f2;
      else if((f3 = fexpand(f2, (FEXP_LOCAL_FILE | FEXP_NVAR))) == NIL)
         /* (Error message written by fexpand()) */
         goto jgetname;
      else
         f = f3;

      n_string_gut(shoup);
   }

   if (f == NULL || f == (char*)-1 || *f == '\0')
      fp = NULL;
   else if(n_psonce & n_PSO_INTERACTIVE){
      if(*f == '|'){
         fp = mx_fs_pipe_open(&f[1], mx_FS_PIPE_WRITE_CHILD_PASS,
               ok_vlook(SHELL), NIL, -1);
         if(!(*ispipe = (fp != NIL)))
            n_perr(f, 0);
      }else if((fp = mx_fs_open(f, (mx_FS_O_WRONLY | mx_FS_O_CREATE |
               mx_FS_O_TRUNC))) == NIL)
         n_err(_("Cannot open %s\n"), n_shexp_quote_cp(f, FAL0));
   }else{
      /* Be very picky in non-interactive mode: actively disallow pipes,
       * prevent directory separators, and any filename member that would
       * become expanded by the shell if the name would be echo(1)ed */
      if(su_cs_first_of(f, "/" n_SHEXP_MAGIC_PATH_CHARS) != su_UZ_MAX){
         char c;

         for(out.s = n_autorec_alloc((su_cs_len(f) * 3) +1), out.l = 0;
               (c = *f++) != '\0';)
            if(su_cs_find_c("/" n_SHEXP_MAGIC_PATH_CHARS, c)){
               out.s[out.l++] = '%';
               n_c_to_hex_base16(&out.s[out.l], c);
               out.l += 2;
            }else
               out.s[out.l++] = c;
         out.s[out.l] = '\0';
         f = out.s;
      }

      /* Avoid overwriting of existing files */
      while((fp = mx_fs_open(f, (mx_FS_O_WRONLY | mx_FS_O_CREATE |
               mx_FS_O_EXCL))) == NIL){
         int e;

         if((e = su_err_no()) != su_ERR_EXIST){
            n_err(_("Cannot open %s: %s\n"),
               n_shexp_quote_cp(f, FAL0), su_err_doc(e));
            break;
         }

         if(ip->m_partstring != NULL)
            f = savecatsep(f, '#', ip->m_partstring);
         else
            f = savecat(f, "#.");
      }
   }
jleave:
   NYD_OU;
   return fp;
}

static boole
a_send_pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf,
      struct quoteflt *qf, u64 *stats){
   sz all_sz, i;
   uz linesize, linelen, cnt;
   char *line;
   boole rv;
   NYD_IN;

   rv = TRU1;
   mx_fs_linepool_aquire(&line, &linesize);
   quoteflt_reset(qf, outbuf);

   fflush_rewind(pipebuf);
   cnt = S(uz,fsize(pipebuf));
   all_sz = 0;
   while(fgetline(&line, &linesize, &cnt, &linelen, pipebuf, FAL0) != NIL){
      if((i = quoteflt_push(qf, line, linelen)) == -1){
         rv = FAL0;
         break;
      }
      all_sz += i;
   }
   if((i = quoteflt_flush(qf)) != -1){
      all_sz += i;
      if(all_sz > 0 && outbuf == origobuf && stats != NIL)
         *stats += all_sz;
   }else
      rv = FAL0;

   mx_fs_linepool_release(line, linesize);

   if(ferror(pipebuf))
      rv = FAL0;
   if(!mx_fs_close(pipebuf))
      rv = FAL0;

   NYD_OU;
   return rv;
}

static void
statusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
   u64 *stats)
{
   char statout[3], *cp = statout;
   NYD_IN;

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
   NYD_OU;
}

static void
xstatusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
   u64 *stats)
{
   char xstatout[4];
   char *xp = xstatout;
   NYD_IN;

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
   NYD_OU;
}

static void
put_from_(FILE *fp, struct mimepart *ip, u64 *stats)
{
   char const *froma, *date, *nl;
   int i;
   NYD_IN;

   if (ip != NULL && ip->m_from != NULL) {
      froma = ip->m_from;
      date = n_time_ctime(ip->m_time, NULL);
      nl = "\n";
   } else {
      froma = ok_vlook(LOGNAME);
      date = time_current.tc_ctime;
      nl = n_empty;
   }

   mx_COLOUR(
      if(mx_COLOUR_IS_ACTIVE())
         mx_colour_put(mx_COLOUR_ID_VIEW_FROM_, NULL);
   )
   i = fprintf(fp, "From %s %s%s", froma, date, nl);
   mx_COLOUR(
      if(mx_COLOUR_IS_ACTIVE())
         mx_colour_reset();
   )
   if (i > 0 && stats != NULL)
      *stats += i;
   NYD_OU;
}

FL int
sendmp(struct message *mp, FILE *obuf, struct mx_ignore const *doitp,
   char const *prefix, enum sendaction action, u64 *stats)
{
   struct n_sigman linedat_protect;
   struct quoteflt qf;
   boole anyoutput;
   FILE *ibuf;
   BITENUM_IS(u32,mx_mime_parse_flags) mpf;
   struct mimepart *ip;
   uz linesize, cnt, size, i;
   char *linedat;
   int rv, c;
   NYD_IN;

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
   size = 0;
   {
   boole nozap;
   char const *cpre = n_empty, *csuf = n_empty;

#ifdef mx_HAVE_COLOUR
   if(mx_COLOUR_IS_ACTIVE()){
      struct mx_colour_pen *cpen;
      struct str const *s;

      cpen = mx_colour_pen_create(mx_COLOUR_ID_VIEW_FROM_,NULL);
      if((s = mx_colour_pen_to_str(cpen)) != NIL){
         cpre = s->s;
         s = mx_colour_reset_to_str();
         if(s != NIL)
            csuf = s->s;
      }
   }
#endif

   nozap = (doitp != mx_IGNORE_ALL && doitp != mx_IGNORE_FWD &&
         action != SEND_RFC822 && !mx_ignore_is_ign(doitp, "from_"));
   if (mp->m_flag & (MNOFROM | MBADFROM_)) {
      if (nozap)
         size = fprintf(obuf, "%s%.*sFrom %s %s%s\n",
               cpre, (int)qf.qf_pfix_len,
               (qf.qf_bypass ? n_empty : qf.qf_pfix), fakefrom(mp),
               n_time_ctime(mp->m_time, NULL), csuf);
   } else if (nozap) {
      if (!qf.qf_bypass) {
         i = fwrite(qf.qf_pfix, sizeof *qf.qf_pfix, qf.qf_pfix_len, obuf);
         if (i != qf.qf_pfix_len)
            goto jleave;
         size += i;
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
         ++size;
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
   if (size > 0 && stats != NULL)
      *stats += size;

   mpf = mx_MIME_PARSE_NONE;
   if(action != SEND_MBOX && action != SEND_RFC822 && action != SEND_SHOW)
      mpf |= mx_MIME_PARSE_PARTS | mx_MIME_PARSE_DECRYPT;
   if(action == SEND_TODISP || action == SEND_TODISP_ALL ||
         action == SEND_QUOTE || action == SEND_QUOTE_ALL)
      mpf |= mx_MIME_PARSE_FOR_USER_CONTEXT;
   if((ip = mx_mime_parse_msg(mp, mpf)) == NIL)
      goto jleave;

   anyoutput = FAL0;
   rv = sendpart(mp, ip, obuf, doitp, &qf, action, &linedat, &linesize,
         stats, 0, &anyoutput);

   n_sigman_cleanup_ping(&linedat_protect);
jleave:
   n_pstate &= ~n_PS_BASE64_STRIP_CR;
   quoteflt_destroy(&qf);
   if(linedat != NULL)
      n_free(linedat);
   NYD_OU;
   n_sigman_leave(&linedat_protect, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_SEND
/* s-it-mode */
