/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message content preparation (sendmp()).
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE send

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

static sigjmp_buf _send_pipejmp;

/* Going for user display, print Part: info string */
static void          _print_part_info(FILE *obuf, struct mimepart const *mpp,
                        struct ignoretab *doign, int level,
                        struct quoteflt *qf, ui64_t *stats);

/* Create a pipe; if mpp is not NULL, place some NAILENV_* environment
 * variables accordingly */
static FILE *        _pipefile(struct mime_handler *mhp,
                        struct mimepart const *mpp, FILE **qbuf,
                        char const *tmpname, int term_infd);

/* Call mime_write() as approbiate and adjust statistics */
SINLINE ssize_t      _out(char const *buf, size_t len, FILE *fp,
                        enum conversion convert, enum sendaction action,
                        struct quoteflt *qf, ui64_t *stats, struct str *rest);

/* SIGPIPE handler */
static void          _send_onpipe(int signo);

/* Send one part */
static int           sendpart(struct message *zmp, struct mimepart *ip,
                        FILE *obuf, struct ignoretab *doign,
                        struct quoteflt *qf, enum sendaction action,
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
   struct ignoretab *doign, int level, struct quoteflt *qf, ui64_t *stats)
{
   char buf[64];
   struct str ti = {NULL, 0}, to;
   struct str const *cpre, *csuf;
   char const *cp;
   NYD2_ENTER;

#ifdef HAVE_COLOUR
   {
   struct n_colour_pen *cpen = n_colour_pen_create(n_COLOUR_ID_VIEW_PARTINFO,
         NULL);
   if ((cpre = n_colour_pen_to_str(cpen)) != NULL)
      csuf = n_colour_reset_to_str();
   else
      csuf = NULL;
   }
#else
   cpre = csuf = NULL;
#endif

   /* Take care of "99.99", i.e., 5 */
   if ((cp = mpp->m_partstring) == NULL || cp[0] == '\0')
      cp = "?";
   if (level || (cp[0] != '1' && cp[1] == '\0'))
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   if (cpre != NULL)
      _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   _out("[-- #", 5, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   _out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);

   to.l = snprintf(buf, sizeof buf, " %" PRIuZ "/%" PRIuZ " ",
         (uiz_t)mpp->m_lines, (uiz_t)mpp->m_size);
   _out(buf, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);

    if ((cp = mpp->m_ct_type_usr_ovwr) != NULL)
      _out("+", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   else
      cp = mpp->m_ct_type_plain;
   if ((to.l = strlen(cp)) > 30 && is_asccaseprefix(cp, "application/")) {
      size_t const al = sizeof("appl../") -1, fl = sizeof("application/") -1;
      size_t i = to.l - fl;
      char *x = salloc(al + i +1);

      memcpy(x, "appl../", al);
      memcpy(x + al, cp + fl, i +1);
      cp = x;
      to.l = al + i;
   }
   _out(cp, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);

   if (mpp->m_multipart == NULL/* TODO */ && (cp = mpp->m_ct_enc) != NULL) {
      _out(", ", 2, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
      if (to.l > 25 && !asccasecmp(cp, "quoted-printable"))
         cp = "qu.-pr.";
      _out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   }

   if (mpp->m_multipart == NULL/* TODO */ && (cp = mpp->m_charset) != NULL) {
      _out(", ", 2, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
      _out(cp, strlen(cp), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   }

   _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   if (csuf != NULL)
      _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
   _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);

   if (is_ign("content-disposition", 19, doign) && mpp->m_filename != NULL &&
         *mpp->m_filename != '\0') {
      makeprint(n_str_add_cp(&ti, mpp->m_filename), &to);
      free(ti.s);
      to.l = delctrl(to.s, to.l);

      if (cpre != NULL)
         _out(cpre->s, cpre->l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
      _out("[-- ", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
      _out(to.s, to.l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
      _out(" --]", 4, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
      if (csuf != NULL)
         _out(csuf->s, csuf->l, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
      _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);

      free(to.s);
   }
   NYD2_LEAVE;
}

static FILE *
_pipefile(struct mime_handler *mhp, struct mimepart const *mpp, FILE **qbuf,
   char const *tmpname, int term_infd)
{
   struct str s;
   char const *env_addon[8], *cp, *sh;
   FILE *rbuf;
   NYD_ENTER;

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
      if (*qbuf != stdout) /* xxx never?  v15: it'll be a filter anyway */
         fflush(stdout);

      u.ptf = mhp->mh_ptf;
      if((rbuf = Popen((char*)-1, "W", u.sh, NULL, fileno(*qbuf))) == NULL)
         goto jerror;
      goto jleave;
   }

   /* NAIL_FILENAME */
   if (mpp == NULL || (cp = mpp->m_filename) == NULL)
      cp = "";
   env_addon[0] = str_concat_csvl(&s, NAILENV_FILENAME, "=", cp, NULL)->s;

   /* NAIL_FILENAME_GENERATED *//* TODO pathconf NAME_MAX; but user can create
    * TODO a file wherever he wants!  *Do* create a zero-size temporary file
    * TODO and give *that* path as NAIL_FILENAME_TEMPORARY, clean it up once
    * TODO the pipe returns?  Like this we *can* verify path/name issues! */
   env_addon[1] = str_concat_csvl(&s, NAILENV_FILENAME_GENERATED, "=",
         getrandstring(MIN(NAME_MAX / 4, 16)), NULL)->s;

   /* NAIL_CONTENT{,_EVIDENCE} */
   if (mpp == NULL || (cp = mpp->m_ct_type_plain) == NULL)
      cp = "";
   env_addon[2] = str_concat_csvl(&s, NAILENV_CONTENT, "=", cp, NULL)->s;

   if (mpp != NULL && mpp->m_ct_type_usr_ovwr != NULL)
      cp = mpp->m_ct_type_usr_ovwr;
   env_addon[3] = str_concat_csvl(&s, NAILENV_CONTENT_EVIDENCE, "=", cp,
         NULL)->s;

   cp = ok_vlook(TMPDIR);
   env_addon[4] = str_concat_csvl(&s, NAILENV_TMPDIR, "=", cp, NULL)->s;
   env_addon[5] = str_concat_csvl(&s, "TMPDIR", "=", cp, NULL)->s;

   env_addon[6] = NULL;

   /* NAIL_FILENAME_TEMPORARY? */
   if (tmpname != NULL) {
      env_addon[6] = str_concat_csvl(&s, NAILENV_FILENAME_TEMPORARY, "=",
            tmpname, NULL)->s;
      env_addon[7] = NULL;
   }

   sh = ok_vlook(SHELL);

   if (mhp->mh_flags & MIME_HDL_NEEDSTERM) {
      sigset_t nset;
      int pid;

      sigemptyset(&nset);
      pid = run_command(sh, NULL, term_infd, COMMAND_FD_PASS, "-c",
            mhp->mh_shell_cmd, NULL, env_addon);
      rbuf = (pid < 0) ? NULL : (FILE*)-1;
   } else {
      rbuf = Popen(mhp->mh_shell_cmd, "W", sh, env_addon,
            (mhp->mh_flags & MIME_HDL_ASYNC ? -1 : fileno(*qbuf)));
jerror:
      if (rbuf == NULL)
         n_err(_("Cannot run MIME type handler: %s: %s\n"),
            mhp->mh_msg, strerror(errno));
      else {
         fflush(*qbuf);
         if (*qbuf != stdout)
            fflush(stdout);
      }
   }
jleave:
   NYD_LEAVE;
   return rbuf;
}

SINLINE ssize_t
_out(char const *buf, size_t len, FILE *fp, enum conversion convert, enum
   sendaction action, struct quoteflt *qf, ui64_t *stats, struct str *rest)
{
   ssize_t sz = 0, n;
   int flags;
   NYD_ENTER;

#if 0
   Well ... it turns out to not work like that since of course a valid
   RFC 4155 compliant parser, like S-nail, takes care for From_ lines only
   after an empty line has been seen, which cannot be detected that easily
   right here!
ifdef HAVE_DEBUG /* TODO assert legacy */
   /* TODO if at all, this CAN only happen for SEND_DECRYPT, since all
    * TODO other input situations handle RFC 4155 OR, if newly generated,
    * TODO enforce quoted-printable if there is From_, as "required" by
    * TODO RFC 5751.  The SEND_DECRYPT case is not yet overhauled;
    * TODO if it may happen in this path, we should just treat decryption
    * TODO as we do for the other input paths; i.e., handle it in SSL!! */
   if (action == SEND_MBOX || action == SEND_DECRYPT)
      assert(!is_head(buf, len, TRU1));
#else
   if ((/*action == SEND_MBOX ||*/ action == SEND_DECRYPT) &&
         is_head(buf, len, TRU1)) {
      putc('>', fp);
      ++sz;
   }
#endif

   flags = ((int)action & _TD_EOF);
   action &= ~_TD_EOF;
   n = mime_write(buf, len, fp,
         action == SEND_MBOX ? CONV_NONE : convert,
         flags | ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
            action == SEND_QUOTE || action == SEND_QUOTE_ALL)
         ?  TD_ISPR | TD_ICONV
         : (action == SEND_TOSRCH || action == SEND_TOPIPE)
            ? TD_ICONV : (action == SEND_SHOW ?  TD_ISPR : TD_NONE)),
         qf, rest);
   if (n < 0)
      sz = n;
   else if (n > 0) {
      sz += n;
      if (stats != NULL)
         *stats += sz;
   }
   NYD_LEAVE;
   return sz;
}

static void
_send_onpipe(int signo)
{
   NYD_X; /* Signal handler */
   UNUSED(signo);
   siglongjmp(_send_pipejmp, 1);
}

static sigjmp_buf       __sendp_actjmp; /* TODO someday.. */
static int              __sendp_sig; /* TODO someday.. */
static sighandler_type  __sendp_opipe;
static void
__sendp_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __sendp_sig = sig;
   siglongjmp(__sendp_actjmp, 1);
}

static int
sendpart(struct message *zmp, struct mimepart *ip, FILE * volatile obuf,
   struct ignoretab *doign, struct quoteflt *qf,
   enum sendaction volatile action, ui64_t * volatile stats, int level)
{
   int volatile rv = 0;
   struct mime_handler mh;
   struct str rest;
   char *line = NULL, *cp, *cp2, *start;
   char const * volatile tmpname = NULL;
   size_t linesize = 0, linelen, cnt;
   int volatile term_infd;
   int dostat, c;
   struct mimepart *volatile np;
   FILE * volatile ibuf = NULL, * volatile pbuf = obuf,
      * volatile qbuf = obuf, *origobuf = obuf;
   enum conversion volatile convert;
   sighandler_type volatile oldpipe = SIG_DFL;
   NYD_ENTER;

   UNINIT(term_infd, 0);
   UNINIT(cnt, 0);

   if (ip->m_mimecontent == MIME_PKCS7 && ip->m_multipart &&
         action != SEND_MBOX && action != SEND_RFC822 && action != SEND_SHOW)
      goto jskip;

   dostat = 0;
   if (level == 0) {
      if (doign != NULL) {
         if (!is_ign("status", 6, doign))
            dostat |= 1;
         if (!is_ign("x-status", 8, doign))
            dostat |= 2;
      } else
         dostat = 3;
   }
   if ((ibuf = setinput(&mb, (struct message*)ip, NEED_BODY)) == NULL) {
      rv = -1;
      goto jleave;
   }
   cnt = ip->m_size;

   if (ip->m_mimecontent == MIME_DISCARD)
      goto jskip;

   if (!(ip->m_flag & MNOFROM))
      while (cnt && (c = getc(ibuf)) != EOF) {
         cnt--;
         if (c == '\n')
            break;
      }
   convert = (action == SEND_TODISP || action == SEND_TODISP_ALL ||
         action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
         action == SEND_TOSRCH)
         ? CONV_FROMHDR : CONV_NONE;

   /* Work the headers */
   quoteflt_reset(qf, obuf);
   /* C99 */{
   enum {
      HPS_NONE = 0,
      HPS_IN_FIELD = 1<<0,
      HPS_IGNORE = 1<<1,
      HPS_ISENC_1 = 1<<2,
      HPS_ISENC_2 = 1<<3
   } hps = HPS_NONE;
   size_t lineno = 0;

   while (fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0)) {
      ++lineno;
      if (line[0] == '\n') {
         /* If line is blank, we've reached end of headers, so force out
          * status: field and note that we are no longer in header fields */
         if (dostat & 1)
            statusput(zmp, obuf, qf, stats);
         if (dostat & 2)
            xstatusput(zmp, obuf, qf, stats);
         if (doign != allignore)
            _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
         break;
      }

      hps &= ~HPS_ISENC_1;
      if ((hps & HPS_IN_FIELD) && blankchar(line[0])) {
         /* If this line is a continuation (SP / HT) of a previous header
          * field, determine if the start of the line is a MIME encoded word */
         if (hps & HPS_ISENC_2) {
            for (cp = line; blankchar(*cp); ++cp)
               ;
            if (cp > line && linelen - PTR2SIZE(cp - line) > 8 &&
                  cp[0] == '=' && cp[1] == '?')
               hps |= HPS_ISENC_1;
         }
      } else {
         /* Pick up the header field if we have one */
         for (cp = line; (c = *cp & 0377) && c != ':' && !spacechar(c); ++cp)
            ;
         cp2 = cp;
         while (spacechar(*cp))
            ++cp;
         if (cp[0] != ':') {
            if (lineno != 1)
               n_err(_("Malformed message: headers and body not separated "
                  "(with empty line)\n"));
            /* Not a header line, force out status: This happens in uucp style
             * mail where there are no headers at all */
            if (level == 0 /*&& lineno == 1*/) {
               if (dostat & 1)
                  statusput(zmp, obuf, qf, stats);
               if (dostat & 2)
                  xstatusput(zmp, obuf, qf, stats);
            }
            if (doign != allignore)
               _out("\n", 1, obuf, CONV_NONE,SEND_MBOX, qf, stats, NULL);
            break;
         }

         /* If it is an ignored field and we care about such things, skip it.
          * Misuse dostat also for another bit xxx use a bitenum + for more */
         if (ok_blook(keep_content_length))
            dostat |= 1 << 2;
         c = *cp2;
         *cp2 = 0; /* temporarily null terminate */
         if ((doign && is_ign(line, PTR2SIZE(cp2 - line), doign)) ||
               (action == SEND_MBOX && !(dostat & (1 << 2)) &&
                (!asccasecmp(line, "content-length") ||
                !asccasecmp(line, "lines"))))
            hps |= HPS_IGNORE;
         else if (!asccasecmp(line, "status")) {
             /* If field is "status," go compute and print real Status: field */
            if (dostat & 1) {
               statusput(zmp, obuf, qf, stats);
               dostat &= ~1;
               hps |= HPS_IGNORE;
            }
         } else if (!asccasecmp(line, "x-status")) {
            /* If field is "status," go compute and print real Status: field */
            if (dostat & 2) {
               xstatusput(zmp, obuf, qf, stats);
               dostat &= ~2;
               hps |= HPS_IGNORE;
            }
         } else {
            hps &= ~HPS_IGNORE;
            /* For colourization we need the complete line, so save it */
            /* XXX This is all temporary (colour belongs into backend), so
             * XXX use tmpname as a temporary storage in the meanwhile */
#ifdef HAVE_COLOUR
            if (pstate & PS_COLOUR_ACTIVE)
               tmpname = savestrbuf(line, PTR2SIZE(cp2 - line));
#endif
         }
         *cp2 = c;
         dostat &= ~(1 << 2);
         hps |= HPS_IN_FIELD;
      }

      /* Determine if the end of the line is a MIME encoded word */
      /* TODO geeeh!  all this lengthy stuff that follows is about is dealing
       * TODO with header follow lines, and it should be up to the backend
       * TODO what happens and what not, i.e., it doesn't matter whether it's
       * TODO a MIME-encoded word or not, as long as a single separating space
       * TODO remains in between lines (the MIME stuff will correctly remove
       * TODO whitespace in between multiple adjacent encoded words) */
      hps &= ~HPS_ISENC_2;
      if (cnt && (c = getc(ibuf)) != EOF) {
         if (blankchar(c)) {
            cp = line + linelen - 1;
            if (linelen > 0 && *cp == '\n')
               --cp;
            while (cp >= line && whitechar(*cp))
               --cp;
            if (PTR2SIZE(cp - line > 8) && cp[0] == '=' && cp[-1] == '?')
               hps |= HPS_ISENC_2;
         }
         ungetc(c, ibuf);
      }

      if (!(hps & HPS_IGNORE)) {
         size_t len = linelen;
         start = line;
         if (action == SEND_TODISP || action == SEND_TODISP_ALL ||
               action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
               action == SEND_TOSRCH) {
            /* Strip blank characters if two MIME-encoded words follow on
             * continuing lines */
            if (hps & HPS_ISENC_1)
               while (len > 0 && blankchar(*start)) {
                  ++start;
                  --len;
               }
            if (hps & HPS_ISENC_2)
               if (len > 0 && start[len - 1] == '\n')
                  --len;
            while (len > 0 && blankchar(start[len - 1]))
               --len;
         }
#ifdef HAVE_COLOUR
         {
         bool_t colour_stripped = FAL0;
         if (tmpname != NULL) {
            n_colour_put(obuf, n_COLOUR_ID_VIEW_HEADER, tmpname);
            if (len > 0 && start[len - 1] == '\n') {
               colour_stripped = TRU1;
               --len;
            }
         }
#endif
         _out(start, len, obuf, convert, action, qf, stats, NULL);
#ifdef HAVE_COLOUR
         if (tmpname != NULL) {
            n_colour_reset(obuf);
            if (colour_stripped)
               putc('\n', obuf);
         }
         }
#endif
         if (ferror(obuf)) {
            free(line);
            rv = -1;
            goto jleave;
         }
      }
   }
   } /* C99 */
   quoteflt_flush(qf);
   free(line);
   line = NULL;
   tmpname = NULL;

jskip:
   memset(&mh, 0, sizeof mh);

   switch (ip->m_mimecontent) {
   case MIME_822:
      switch (action) {
      case SEND_TODISP:
      case SEND_TODISP_ALL:
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         if (ok_blook(rfc822_body_from_)) {
            if (qf->qf_pfix_len > 0) {
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
         if (ok_blook(rfc822_body_from_))
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
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         switch (mime_type_handler(&mh, ip, action)) {
         case MIME_HDL_MSG:
            _out(mh.mh_msg.s, mh.mh_msg.l, obuf, CONV_NONE, SEND_MBOX, qf,
               stats, NULL);
            /* We would print this as plain text, so better force going home */
            goto jleave;
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
      case SEND_QUOTE:
      case SEND_QUOTE_ALL:
         switch (mime_type_handler(&mh, ip, action)) {
         case MIME_HDL_MSG:
            _out(mh.mh_msg.s, mh.mh_msg.l, obuf, CONV_NONE, SEND_MBOX, qf,
               stats, NULL);
            /* We would print this as plain text, so better force going home */
            goto jleave;
         case MIME_HDL_CMD:
            /* FIXME WE NEED TO DO THAT IF WE ARE THE ONLY MAIL
             * FIXME CONTENT !! */
         case MIME_HDL_TEXT:
            break;
         default:
         case MIME_HDL_NULL:
            if (level == 0 && cnt) {
               char const *x = _("[-- Binary content --]\n");
               _out(x, strlen(x), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
            }
            goto jleave;
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
                     _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
                  _print_part_info(obuf, np, doign, level, qf, stats);
               }
               neednl = TRU1;

               switch (np->m_mimecontent) {
               case MIME_ALTERNATIVE:
               case MIME_RELATED:
               case MIME_MULTI:
               case MIME_DIGEST:
                  mpsp = salloc(sizeof *mpsp);
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
                  switch (mime_type_handler(&mh, np, action)) {
                  default:
                     mh.mh_flags = MIME_HDL_NULL;
                     continue; /* break; break; */
                  case MIME_HDL_PTF:
                     if (!ok_blook(mime_alternative_favour_rich)) {/* TODO */
                        struct mimepart *x = np;

                        while ((x = x->m_nextpart) != NULL) {
                           struct mime_handler mhx;

                           if (x->m_mimecontent == MIME_TEXT_PLAIN ||
                                 mime_type_handler(&mhx, x, action) ==
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

                        switch (mime_type_handler(&mhx, x, action)) {
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
                  if (action == SEND_QUOTE && hadpart) {
                     struct quoteflt *dummy = quoteflt_dummy();
                     _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, dummy, stats,
                        NULL);
                     quoteflt_flush(dummy);
                  }
                  hadpart = TRU1;
                  neednl = FAL0;
                  rv = sendpart(zmp, np, obuf, doign, qf, action, stats,
                        level + 1);
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
   case MIME_MULTI:
   case MIME_DIGEST:
   case MIME_RELATED:
      switch (action) {
      case SEND_TODISP:
      case SEND_TODISP_ALL:
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
            char const *x = _("[Missing multipart boundary - use show "
                  "to display the raw message]\n");
            _out(x, strlen(x), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
         }

         for (np = ip->m_multipart; np != NULL; np = np->m_nextpart) {
            bool_t volatile ispipe;

            if (np->m_mimecontent == MIME_DISCARD && action != SEND_DECRYPT)
               continue;

            ispipe = FAL0;
            switch (action) {
            case SEND_TOFILE:
               if (np->m_partstring && !strcmp(np->m_partstring, "1"))
                  break;
               stats = NULL;
               /* TODO Always open multipart on /dev/null, it's a hack to be
                * TODO able to dive into that structure, and still better
                * TODO than asking the user for something stupid.
                * TODO oh, wait, we did ask for a filename for this MIME mail,
                * TODO and that outer container is useless anyway ;-P */
               if (np->m_multipart != NULL) {
                  if ((obuf = Fopen("/dev/null", "w")) == NULL)
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
                     ip->m_mimecontent != MIME_MULTI)
                  break;
               _print_part_info(obuf, np, doign, level, qf, stats);
               break;
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
                  np->m_multipart == NULL && ip->m_parent != NULL) {
               struct quoteflt *dummy = quoteflt_dummy();
               _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, dummy, stats,
                  NULL);
               quoteflt_flush(dummy);
            }
            if (sendpart(zmp, np, obuf, doign, qf, action, stats, level+1) < 0)
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
   if (doign == allignore && level == 0) /* skip final blank line */
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

   if (action == SEND_DECRYPT || action == SEND_MBOX ||
         action == SEND_RFC822 || action == SEND_SHOW)
      convert = CONV_NONE;
#ifdef HAVE_ICONV
   if ((action == SEND_TODISP || action == SEND_TODISP_ALL ||
         action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
         action == SEND_TOSRCH) &&
         (ip->m_mimecontent == MIME_TEXT_PLAIN ||
          ip->m_mimecontent == MIME_TEXT_HTML ||
          ip->m_mimecontent == MIME_TEXT ||
          (mh.mh_flags & MIME_HDL_TYPE_MASK) == MIME_HDL_TEXT ||
          (mh.mh_flags & MIME_HDL_TYPE_MASK) == MIME_HDL_PTF)) {
      char const *tcs = charset_get_lc();

      if (iconvd != (iconv_t)-1)
         n_iconv_close(iconvd);
      /* TODO Since Base64 has an odd 4:3 relation in between input
       * TODO and output an input line may end with a partial
       * TODO multibyte character; this is no problem at all unless
       * TODO we send to the display or whatever, i.e., ensure
       * TODO makeprint() or something; to avoid this trap, *force*
       * TODO iconv(), in which case this layer will handle leftovers
       * TODO correctly */
      if (convert == CONV_FROMB64_T || (asccasecmp(tcs, ip->m_charset) &&
            asccasecmp(charset_get_7bit(), ip->m_charset))) {
         iconvd = n_iconv_open(tcs, ip->m_charset);
         /*
          * TODO errors should DEFINETELY not be scrolled away!
          * TODO what about an error buffer (think old shsp(1)),
          * TODO re-dump errors since last snapshot when the
          * TODO command loop enters again?  i.e., at least print
          * TODO "There were errors ?" before the next prompt,
          * TODO so that the user can look at the error buffer?
          */
         if (iconvd == (iconv_t)-1 && errno == EINVAL) {
            n_err(_("Cannot convert from %s to %s\n"), ip->m_charset, tcs);
            /*rv = 1; goto jleave;*/
         }
      }
   }
#endif

   switch (mh.mh_flags & MIME_HDL_TYPE_MASK) {
   case MIME_HDL_CMD:
   case MIME_HDL_PTF:
      tmpname = NULL;
      qbuf = obuf;

      term_infd = COMMAND_FD_PASS;
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
      pbuf = _pipefile(&mh, ip, UNVOLATILE(&qbuf), tmpname, term_infd);
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
      mh.mh_flags = MIME_HDL_NULL;
      pbuf = qbuf = obuf;
      break;
   }

jsend:
   {
   bool_t volatile eof;
   ui32_t save_qf_pfix_len = qf->qf_pfix_len;
   ui64_t *save_stats = stats;

   if (pbuf != origobuf) {
      qf->qf_pfix_len = 0; /* XXX legacy (remove filter instead) */
      stats = NULL;
   }
   eof = FAL0;
   rest.s = NULL;
   rest.l = 0;

   if (pbuf == qbuf) {
      __sendp_sig = 0;
      __sendp_opipe = safe_signal(SIGPIPE, &__sendp_onsig);
      if (sigsetjmp(__sendp_actjmp, 1)) {
         if (rest.s != NULL)
            free(rest.s);
         free(line);
#ifdef HAVE_ICONV
         if (iconvd != (iconv_t)-1)
            n_iconv_close(iconvd);
#endif
         safe_signal(SIGPIPE, __sendp_opipe);
         n_raise(__sendp_sig);
      }
   }

   quoteflt_reset(qf, pbuf);
   while (!eof && fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0)) {
joutln:
      if (_out(line, linelen, pbuf, convert, action, qf, stats, &rest) < 0 ||
            ferror(pbuf)) {
         rv = -1; /* XXX Should bail away?! */
         break;
      }
   }
   if (!eof && rv >= 0 && rest.l != 0) {
      linelen = 0;
      eof = TRU1;
      action |= _TD_EOF;
      goto joutln;
   }

   /* TODO HACK: when sending to the display we yet get fooled if a message
    * TODO doesn't end in a newline, because of our input/output 1:1.
    * TODO This should be handled automatically by a display filter, then */
   if(rv >= 0 && !qf->qf_nl_last &&
         (action == SEND_TODISP || action == SEND_TODISP_ALL))
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

   if (rest.s != NULL)
      free(rest.s);

   if (pbuf != origobuf) {
      qf->qf_pfix_len = save_qf_pfix_len;
      stats = save_stats;
   }
   }

jend:
   if (line != NULL)
      free(line);
   if (pbuf != qbuf) {
      safe_signal(SIGPIPE, SIG_IGN);
      Pclose(pbuf, !(mh.mh_flags & MIME_HDL_ASYNC));
      safe_signal(SIGPIPE, oldpipe);
      if (rv >= 0 && qbuf != NULL && qbuf != obuf)
         pipecpy(qbuf, obuf, origobuf, qf, stats);
   }
#ifdef HAVE_ICONV
   if (iconvd != (iconv_t)-1)
      n_iconv_close(iconvd);
#endif
jleave:
   NYD_LEAVE;
   return rv;
}

static FILE *
newfile(struct mimepart *ip, bool_t volatile *ispipe)
{
   struct str in, out;
   char *f;
   FILE *fp;
   NYD_ENTER;

   f = ip->m_filename;
   *ispipe = FAL0;

   if (f != NULL && f != (char*)-1) {
      in.s = f;
      in.l = strlen(f);
      makeprint(&in, &out);
      out.l = delctrl(out.s, out.l);
      f = savestrbuf(out.s, out.l);
      free(out.s);
   }

   if (options & OPT_INTERACTIVE) {
      struct str prompt;
      struct n_string shou, *shoup;
      char *f2, *f3;

      shoup = n_string_creat_auto(&shou);

      /* TODO Generic function which asks for filename.
       * TODO If the current part is the first textpart the target
       * TODO is implicit from outer `write' etc! */
      /* I18N: Filename input prompt with file type indication */
      str_concat_csvl(&prompt, _("Enter filename for part "),
         (ip->m_partstring != NULL) ? ip->m_partstring : _("?"),
         _(" ("), ip->m_ct_type_plain, _("): "), NULL);
jgetname:
      f2 = n_lex_input_cp((n_LEXINPUT_CTX_BASE | n_LEXINPUT_HIST_ADD),
            prompt.s, ((f != (char*)-1 && f != NULL)
               ? n_shell_quote_cp(f, FAL0) : NULL));
      if(f2 != NULL){
         in.s = UNCONST(f2);
         in.l = UIZ_MAX;
         if((n_shell_parse_token(shoup, &in, n_SHEXP_PARSE_TRUNC |
                  n_SHEXP_PARSE_TRIMSPACE | n_SHEXP_PARSE_LOG |
                  n_SHEXP_PARSE_IGNORE_EMPTY) &
                (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_ERR_MASK)) !=
                n_SHEXP_STATE_OUTPUT)
            goto jgetname;
         if(in.l != 0)
            goto jgetname;
         f2 = n_string_cp(shoup);
      }
      if (f2 == NULL || *f2 == '\0') {
         if (options & OPT_D_V)
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
   if (f == NULL || f == (char*)-1) {
      fp = NULL;
      goto jleave;
   }

   if (*f == '|') {
      fp = Popen(f + 1, "w", ok_vlook(SHELL), NULL, 1);
      if (!(*ispipe = (fp != NULL)))
         n_perr(f, 0);
   } else {
      if ((fp = Fopen(f, "w")) == NULL)
         n_err(_("Cannot open %s\n"), n_shell_quote_cp(f, FAL0));
   }
jleave:
   NYD_LEAVE;
   return fp;
}

static void
pipecpy(FILE *pipebuf, FILE *outbuf, FILE *origobuf, struct quoteflt *qf,
   ui64_t *stats)
{
   char *line = NULL; /* TODO line pool */
   size_t linesize = 0, linelen, cnt;
   ssize_t all_sz, sz;
   NYD_ENTER;

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
      free(line);

   if (all_sz > 0 && outbuf == origobuf && stats != NULL)
      *stats += all_sz;
   Fclose(pipebuf);
   NYD_LEAVE;
}

static void
statusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
   ui64_t *stats)
{
   char statout[3], *cp = statout;
   NYD_ENTER;

   if (mp->m_flag & MREAD)
      *cp++ = 'R';
   if (!(mp->m_flag & MNEW))
      *cp++ = 'O';
   *cp = 0;
   if (statout[0]) {
      int i = fprintf(obuf, "%.*sStatus: %s\n", (int)qf->qf_pfix_len,
            (qf->qf_pfix_len > 0 ? qf->qf_pfix : 0), statout);
      if (i > 0 && stats != NULL)
         *stats += i;
   }
   NYD_LEAVE;
}

static void
xstatusput(const struct message *mp, FILE *obuf, struct quoteflt *qf,
   ui64_t *stats)
{
   char xstatout[4];
   char *xp = xstatout;
   NYD_ENTER;

   if (mp->m_flag & MFLAGGED)
      *xp++ = 'F';
   if (mp->m_flag & MANSWERED)
      *xp++ = 'A';
   if (mp->m_flag & MDRAFTED)
      *xp++ = 'T';
   *xp = 0;
   if (xstatout[0]) {
      int i = fprintf(obuf, "%.*sX-Status: %s\n", (int)qf->qf_pfix_len,
            (qf->qf_pfix_len > 0 ? qf->qf_pfix : 0), xstatout);
      if (i > 0 && stats != NULL)
         *stats += i;
   }
   NYD_LEAVE;
}

static void
put_from_(FILE *fp, struct mimepart *ip, ui64_t *stats)
{
   char const *froma, *date, *nl;
   int i;
   NYD_ENTER;

   if (ip != NULL && ip->m_from != NULL) {
      froma = ip->m_from;
      date = fakedate(ip->m_time);
      nl = "\n";
   } else {
      froma = myname;
      date = time_current.tc_ctime;
      nl = "";
   }

   n_COLOUR( n_colour_put(fp, n_COLOUR_ID_VIEW_FROM_, NULL); )
   i = fprintf(fp, "From %s %s%s", froma, date, nl);
   n_COLOUR( n_colour_reset(fp); )
   if (i > 0 && stats != NULL)
      *stats += i;
   NYD_LEAVE;
}

FL int
sendmp(struct message *mp, FILE *obuf, struct ignoretab *doign,
   char const *prefix, enum sendaction action, ui64_t *stats)
{
   struct quoteflt qf;
   size_t cnt, sz, i;
   FILE *ibuf;
   enum mime_parse_flags mpf;
   struct mimepart *ip;
   int rv = -1, c;
   NYD_ENTER;

   time_current_update(&time_current, TRU1);

   if (mp == dot && action != SEND_TOSRCH)
      pstate |= PS_DID_PRINT_DOT;
   if (stats != NULL)
      *stats = 0;
   quoteflt_init(&qf, prefix);

   /* First line is the From_ line, so no headers there to worry about */
   if ((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL)
      goto jleave;

   cnt = mp->m_size;
   sz = 0;
   {
   bool_t nozap;
   char const *cpre = "", *csuf = "";
#ifdef HAVE_COLOUR
   struct n_colour_pen *cpen = n_colour_pen_create(n_COLOUR_ID_VIEW_FROM_,NULL);
   struct str const *sp = n_colour_pen_to_str(cpen);

   if (sp != NULL) {
      cpre = sp->s;
      sp = n_colour_reset_to_str();
      if (sp != NULL)
         csuf = sp->s;
   }
#endif

   nozap = (doign != allignore && doign != fwdignore && action != SEND_RFC822 &&
            !is_ign("from_", sizeof("from_") -1, doign));
   if (mp->m_flag & MNOFROM) {
      if (nozap)
         sz = fprintf(obuf, "%s%.*sFrom %s %s%s\n",
               cpre, (int)qf.qf_pfix_len,
               (qf.qf_pfix_len != 0 ? qf.qf_pfix : ""), fakefrom(mp),
               fakedate(mp->m_time), csuf);
   } else if (nozap) {
      if (qf.qf_pfix_len > 0) {
         i = fwrite(qf.qf_pfix, sizeof *qf.qf_pfix, qf.qf_pfix_len, obuf);
         if (i != qf.qf_pfix_len)
            goto jleave;
         sz += i;
      }
#ifdef HAVE_COLOUR
      if (cpre != NULL) {
         fputs(cpre, obuf);
         cpre = (char const*)0x1;
      }
#endif

      while (cnt > 0 && (c = getc(ibuf)) != EOF) {
#ifdef HAVE_COLOUR
         if (c == '\n' && csuf != NULL) {
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

#ifdef HAVE_COLOUR
      if (csuf != NULL && cpre != (char const*)0x1)
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
      mpf |= MIME_PARSE_DECRYPT | MIME_PARSE_PARTS;
   if ((ip = mime_parse_msg(mp, mpf)) == NULL)
      goto jleave;

   rv = sendpart(mp, ip, obuf, doign, &qf, action, stats, 0);
jleave:
   quoteflt_destroy(&qf);
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
