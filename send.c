/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Mail to mail folders and displays.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

enum pipeflags {
   PIPE_NULL,  /* No pipe- mimetype handler */
   PIPE_COMM,  /* Normal command */
   PIPE_ASYNC, /* Normal command, run asynchronous */
   PIPE_TEXT,  /* @ special command to force treatment as text */
   PIPE_MSG    /* Display message (returned as command string) */
};

static sigjmp_buf _send_pipejmp;

/* Going for user display, print Part: info string */
static void          _print_part_info(FILE *obuf, struct mimepart const *mpp,
                        struct ignoretab *doign, int level,
                        struct quoteflt *qf, ui64_t *stats);

/* Query possible pipe command for MIME part */
static enum pipeflags _pipecmd(char const **result, struct mimepart const *mpp);

/* Create a pipe; if mpp is not NULL, place some NAILENV_* environment
 * variables accordingly */
static FILE *        _pipefile(char const *pipecomm, struct mimepart const *mpp,
                        FILE **qbuf, bool_t quote, bool_t async);

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
static FILE *        newfile(struct mimepart *ip, int *ispipe);

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
   cpre = colour_get(COLOURSPEC_PARTINFO);
   csuf = colour_get(COLOURSPEC_RESET);
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

static enum pipeflags
_pipecmd(char const **result, struct mimepart const *mpp)
{
   enum pipeflags rv;
   char const *cp;
   NYD2_ENTER;

   *result = NULL;

   /* Do we have any handler for this part? */
   if ((cp = mime_type_mimepart_handler(mpp)) == NULL)
      rv = PIPE_NULL;
   else if (cp == MIME_TYPE_HANDLER_TEXT)
      rv = PIPE_TEXT;
   else if (
#ifdef HAVE_FILTER_HTML_TAGSOUP
         cp == MIME_TYPE_HANDLER_HTML ||
#endif
         *cp != '@') {
      *result = cp;
      rv = PIPE_COMM;
   } else if (*++cp == '\0') {
      /* Treat as plain text */
      rv = PIPE_TEXT;
   } else if (!(pstate & PS_MSGLIST_DIRECT)) {
      /* Viewing multiple messages in one go, don't block system */
      *result = _("[Directly address message only to display this]\n");
      rv = PIPE_MSG;
   } else {
      /* Viewing a single message only */
      /* TODO send/MIME layer rewrite: when we have a single-pass parser
       * TODO then the parsing phase and the send phase will be separated;
       * TODO that allows us to ask a user *before* we start the send, i.e.,
       * TODO *before* a pager pipe is setup */
      if (*cp == '&')
         /* Asynchronous command, normal command line */
         *result = ++cp, rv = PIPE_ASYNC;
      else
         *result = cp, rv = PIPE_COMM;
   }
   NYD2_LEAVE;
   return rv;
}

static FILE *
_pipefile(char const *pipecomm, struct mimepart const *mpp, FILE **qbuf,
   bool_t quote, bool_t async)
{
   struct str s;
   char const *env_addon[8], *cp, *sh;
   FILE *rbuf;
   NYD_ENTER;

   rbuf = *qbuf;

   if (quote) {
      if ((*qbuf = Ftmp(NULL, "sendp", OF_RDWR | OF_UNLINK | OF_REGISTER,
            0600)) == NULL) {
         n_perr(_("tmpfile"), 0);
         *qbuf = rbuf;
      }
      async = FAL0;
   }

#ifdef HAVE_FILTER_HTML_TAGSOUP
   if (pipecomm == MIME_TYPE_HANDLER_HTML) {
      union {int (*ptf)(void); char const *sh;} u;
      u.ptf = &htmlflt_process_main;
      rbuf = Popen((char*)-1, "W", u.sh, NULL, fileno(*qbuf));
      pipecomm = "Builtin HTML tagsoup filter";
      goto jafter_tagsoup_hack;
   }
#endif

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

   env_addon[4] = str_concat_csvl(&s, NAILENV_TMPDIR, "=", tempdir, NULL)->s;
   env_addon[5] = str_concat_csvl(&s, "TMPDIR", "=", tempdir, NULL)->s;

   env_addon[6] = NULL;

   if ((sh = ok_vlook(SHELL)) == NULL)
      sh = XSHELL;

   rbuf = Popen(pipecomm, "W", sh, env_addon, (async ? -1 : fileno(*qbuf)));
#ifdef HAVE_FILTER_HTML_TAGSOUP
jafter_tagsoup_hack:
#endif
   if (rbuf == NULL)
      n_err(_("Cannot run MIME type handler \"%s\": %s\n"),
         pipecomm, strerror(errno));
   else {
      fflush(*qbuf);
      if (*qbuf != stdout)
         fflush(stdout);
   }
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

static sigjmp_buf       __sndalter_actjmp; /* TODO someday.. */
static int              __sndalter_sig; /* TODO someday.. */
static void
__sndalter_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __sndalter_sig = sig;
   siglongjmp(__sndalter_actjmp, 1);
}

static int
sendpart(struct message *zmp, struct mimepart *ip, FILE * volatile obuf,
   struct ignoretab *doign, struct quoteflt *qf,
   enum sendaction volatile action, ui64_t * volatile stats, int level)
{
   int volatile ispipe, rv = 0;
   struct str rest;
   char *line = NULL, *cp, *cp2, *start;
   char const *pipecomm = NULL;
   size_t linesize = 0, linelen, cnt;
   int dostat, infld = 0, ignoring = 1, isenc, c;
   struct mimepart *volatile np;
   FILE * volatile ibuf = NULL, * volatile pbuf = obuf, * volatile qbuf = obuf,
      *origobuf = obuf;
   enum conversion volatile convert;
   sighandler_type volatile oldpipe = SIG_DFL;
   long lineno = 0;
   NYD_ENTER;

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
   isenc = 0;
   convert = (action == SEND_TODISP || action == SEND_TODISP_ALL ||
         action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
         action == SEND_TOSRCH)
         ? CONV_FROMHDR : CONV_NONE;

   /* Work the headers */
   quoteflt_reset(qf, obuf);
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

      isenc &= ~1;
      if (infld && blankchar(line[0])) {
         /* If this line is a continuation (SP / HT) of a previous header
          * field, determine if the start of the line is a MIME encoded word */
         if (isenc & 2) {
            for (cp = line; blankchar(*cp); ++cp);
            if (cp > line && linelen - PTR2SIZE(cp - line) > 8 &&
                  cp[0] == '=' && cp[1] == '?')
               isenc |= 1;
         }
      } else {
         /* Pick up the header field if we have one */
         for (cp = line; (c = *cp & 0377) && c != ':' && !spacechar(c); ++cp)
            ;
         cp2 = cp;
         while (spacechar(*cp))
            ++cp;
         if (cp[0] != ':' && level == 0 && lineno == 1) {
            /* Not a header line, force out status: This happens in uucp style
             * mail where there are no headers at all */
            if (dostat & 1)
               statusput(zmp, obuf, qf, stats);
            if (dostat & 2)
               xstatusput(zmp, obuf, qf, stats);
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
            ignoring = 1;
         else if (!asccasecmp(line, "status")) {
             /* If field is "status," go compute and print real Status: field */
            if (dostat & 1) {
               statusput(zmp, obuf, qf, stats);
               dostat &= ~1;
               ignoring = 1;
            }
         } else if (!asccasecmp(line, "x-status")) {
            /* If field is "status," go compute and print real Status: field */
            if (dostat & 2) {
               xstatusput(zmp, obuf, qf, stats);
               dostat &= ~2;
               ignoring = 1;
            }
         } else {
            ignoring = 0;
            /* For colourization we need the complete line, so save it */
            /* XXX This is all temporary (colour belongs into backend), so
             * XXX use pipecomm as a temporary storage in the meanwhile */
#ifdef HAVE_COLOUR
            if (colour_table != NULL)
               pipecomm = savestrbuf(line, PTR2SIZE(cp2 - line));
#endif
         }
         *cp2 = c;
         dostat &= ~(1 << 2);
         infld = 1;
      }

      /* Determine if the end of the line is a MIME encoded word */
      /* TODO geeeh!  all this lengthy stuff that follows is about is dealing
       * TODO with header follow lines, and it should be up to the backend
       * TODO what happens and what not, i.e., it doesn't matter wether it's
       * TODO a MIME-encoded word or not, as long as a single separating space
       * TODO remains in between lines (the MIME stuff will correctly remove
       * TODO whitespace in between multiple adjacent encoded words) */
      isenc &= ~2;
      if (cnt && (c = getc(ibuf)) != EOF) {
         if (blankchar(c)) {
            cp = line + linelen - 1;
            if (linelen > 0 && *cp == '\n')
               --cp;
            while (cp >= line && whitechar(*cp))
               --cp;
            if (PTR2SIZE(cp - line > 8) && cp[0] == '=' && cp[-1] == '?')
               isenc |= 2;
         }
         ungetc(c, ibuf);
      }

      if (!ignoring) {
         size_t len = linelen;
         start = line;
         if (action == SEND_TODISP || action == SEND_TODISP_ALL ||
               action == SEND_QUOTE || action == SEND_QUOTE_ALL ||
               action == SEND_TOSRCH) {
            /* Strip blank characters if two MIME-encoded words follow on
             * continuing lines */
            if (isenc & 1)
               while (len > 0 && blankchar(*start)) {
                  ++start;
                  --len;
               }
            if (isenc & 2)
               if (len > 0 && start[len - 1] == '\n')
                  --len;
            while (len > 0 && blankchar(start[len - 1]))
               --len;
         }
#ifdef HAVE_COLOUR
         {
         bool_t colour_stripped = FAL0;
         if (pipecomm != NULL) {
            colour_put_header(obuf, pipecomm);
            if (len > 0 && start[len - 1] == '\n') {
               colour_stripped = TRU1;
               --len;
            }
         }
#endif
         _out(start, len, obuf, convert, action, qf, stats, NULL);
#ifdef HAVE_COLOUR
         if (pipecomm != NULL) {
            colour_reset(obuf); /* XXX reset after \n!! */
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
   quoteflt_flush(qf);
   free(line);
   line = NULL;
   pipecomm = NULL;

jskip:
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
         ispipe = TRU1;
         switch (_pipecmd(&pipecomm, ip)) {
         case PIPE_MSG:
            _out(pipecomm, strlen(pipecomm), obuf, CONV_NONE, SEND_MBOX, qf,
               stats, NULL);
            /* We would print this as plain text, so better force going home */
            goto jleave;
         case PIPE_TEXT:
         case PIPE_COMM:
         case PIPE_NULL:
            break;
         case PIPE_ASYNC:
            ispipe = FAL0;
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
         ispipe = TRU1;
         switch (_pipecmd(&pipecomm, ip)) {
         case PIPE_MSG:
            _out(pipecomm, strlen(pipecomm), obuf, CONV_NONE, SEND_MBOX, qf,
               stats, NULL);
            pipecomm = NULL;
            break;
         case PIPE_ASYNC:
            ispipe = FAL0;
            /* FALLTHRU */
         case PIPE_COMM:
         case PIPE_NULL:
            break;
         case PIPE_TEXT:
            goto jcopyout; /* break; break; */
         }
         if (pipecomm != NULL)
            break;
         if (level == 0 && cnt) {
            char const *x = _("[Binary content]\n");
            _out(x, strlen(x), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
         }
         goto jleave;
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
   case MIME_RELATED:
      if ((action == SEND_TODISP || action == SEND_QUOTE) &&
            !ok_blook(print_alternatives)) {
         /* XXX This (a) should not remain (b) should be own fun */
         struct mpstack {
            struct mpstack    *outer;
            struct mimepart   *mp;
         } outermost, * volatile curr = &outermost, * volatile mpsp;
         sighandler_type volatile opsh, oish, ohsh;
         size_t volatile partcnt = 0/* silence CC */;
         bool_t volatile neednl = FAL0;

         curr->outer = NULL;
         curr->mp = ip;

         __sndalter_sig = 0;
         opsh = safe_signal(SIGPIPE, &__sndalter_onsig);
         oish = safe_signal(SIGINT, &__sndalter_onsig);
         ohsh = safe_signal(SIGHUP, &__sndalter_onsig);
         if (sigsetjmp(__sndalter_actjmp, 1)) {
            rv = -1;
            goto jalter_unroll;
         }

         for (np = ip->m_multipart;;) {
            partcnt = 0;
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
                  mpsp = ac_alloc(sizeof *mpsp);
                  mpsp->outer = curr;
                  mpsp->mp = np->m_multipart;
                  curr->mp = np;
                  curr = mpsp;
                  np = mpsp->mp;
                  neednl = FAL0;
                  goto jalter_redo;
               default:
                  switch (_pipecmd(&pipecomm, np)) {
                  default:
                     continue;
                  case PIPE_TEXT:
                     break;
                  }
                  /* FALLTHRU */
               case MIME_TEXT_PLAIN:
                  ++partcnt;
                  if (action == SEND_QUOTE && partcnt > 1 &&
                        ip->m_mimecontent == MIME_ALTERNATIVE)
                     break;
                  quoteflt_flush(qf);
                  if (action == SEND_QUOTE && partcnt > 1) {
                     struct quoteflt *dummy = quoteflt_dummy();
                     _out("\n", 1, obuf, CONV_NONE, SEND_MBOX, dummy, stats,
                        NULL);
                     quoteflt_flush(dummy);
                  }
                  neednl = FAL0;
                  rv = sendpart(zmp, np, obuf, doign, qf, action, stats,
                        level + 1);
                  quoteflt_reset(qf, origobuf);

                  if (rv < 0) {
jalter_unroll:
                     for (;; curr = mpsp) {
                        if ((mpsp = curr->outer) == NULL)
                           break;
                        ac_free(curr);
                     }
                  }
                  break;
               }
            }

            mpsp = curr->outer;
            if (mpsp == NULL)
               break;
            ac_free(curr);
            curr = mpsp;
            np = curr->mp->m_nextpart;
         }
         safe_signal(SIGHUP, ohsh);
         safe_signal(SIGINT, oish);
         safe_signal(SIGPIPE, opsh);
         if (__sndalter_sig != 0)
            n_raise(__sndalter_sig);
         goto jleave;
      }
      /* FALLTHRU */
   case MIME_MULTI:
   case MIME_DIGEST:
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
            char const *x = _("[Missing multipart boundary - use \"show\" "
                  "to display the raw message]\n");
            _out(x, strlen(x), obuf, CONV_NONE, SEND_MBOX, qf, stats, NULL);
         }

         for (np = ip->m_multipart; np != NULL; np = np->m_nextpart) {
            if (np->m_mimecontent == MIME_DISCARD && action != SEND_DECRYPT)
               continue;
            ispipe = FAL0;
            switch (action) {
            case SEND_TOFILE:
               if (np->m_partstring && !strcmp(np->m_partstring, "1"))
                  break;
               stats = NULL;
               if ((obuf = newfile(np, UNVOLATILE(&ispipe))) == NULL)
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
            case SEND_QUOTE_ALL:
               if (ip->m_mimecontent != MIME_MULTI &&
                     ip->m_mimecontent != MIME_ALTERNATIVE &&
                     ip->m_mimecontent != MIME_RELATED &&
                     ip->m_mimecontent != MIME_DIGEST)
                  break;
               _print_part_info(obuf, np, doign, level, qf, stats);
               break;
            case SEND_MBOX:
            case SEND_RFC822:
            case SEND_SHOW:
            case SEND_TOSRCH:
            case SEND_QUOTE:
            case SEND_DECRYPT:
            case SEND_TOPIPE:
               break;
            }

            quoteflt_flush(qf);
            if (sendpart(zmp, np, obuf, doign, qf, action, stats, level+1) < 0)
               rv = -1;
            quoteflt_reset(qf, origobuf);

            if (action == SEND_QUOTE)
               break;
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
   }

   /* Copy out message body */
jcopyout:
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
         convert = CONV_FROMB64;
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
          ip->m_mimecontent == MIME_TEXT)) {
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

   if (pipecomm != NULL && (action == SEND_TODISP ||
         action == SEND_TODISP_ALL || action == SEND_QUOTE ||
         action == SEND_QUOTE_ALL)) {
      qbuf = obuf;
      pbuf = _pipefile(pipecomm, ip, UNVOLATILE(&qbuf),
            (action == SEND_QUOTE || action == SEND_QUOTE_ALL), !ispipe);
      if (pbuf == NULL) {
#ifdef HAVE_ICONV
         if (iconvd != (iconv_t)-1)
            n_iconv_close(iconvd);
#endif
         rv = -1;
         goto jleave;
      }
      action = SEND_TOPIPE;
      if (pbuf != qbuf) {
         oldpipe = safe_signal(SIGPIPE, &_send_onpipe);
         if (sigsetjmp(_send_pipejmp, 1))
            goto jend;
      }
   } else
      pbuf = qbuf = obuf;

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
   if (!eof && rest.l != 0) {
      linelen = 0;
      eof = TRU1;
      action |= _TD_EOF;
      goto joutln;
   }
   if (pbuf == qbuf)
      safe_signal(SIGPIPE, __sendp_opipe);

   quoteflt_flush(qf);
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
      Pclose(pbuf, ispipe);
      safe_signal(SIGPIPE, oldpipe);
      if (qbuf != NULL && qbuf != obuf)
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
newfile(struct mimepart *ip, int *ispipe)
{
   struct str in, out;
   char *f;
   FILE *fp;
   NYD_ENTER;

   f = ip->m_filename;
   *ispipe = 0;

   if (f != NULL && f != (char*)-1) {
      in.s = f;
      in.l = strlen(f);
      makeprint(&in, &out);
      out.l = delctrl(out.s, out.l);
      f = savestrbuf(out.s, out.l);
      free(out.s);
   }

   if (options & OPT_INTERACTIVE) {
      char *f2, *f3;
jgetname:
      printf(_("Enter filename for part %s (%s)"),
         (ip->m_partstring != NULL) ? ip->m_partstring : "?",
         ip->m_ct_type_plain);
      f2 = n_input_cp_addhist(": ", ((f != (char*)-1 && f != NULL)
            ? fexpand_nshell_quote(f) : NULL), TRU1);
      if (f2 == NULL || *f2 == '\0') {
         n_err(_("... skipping this\n"));
         fp = NULL;
         goto jleave;
      } else if (*f2 == '|')
         /* Pipes are expanded by the shell */
         f = f2;
      else if ((f3 = fexpand(f2, FEXP_LOCAL | FEXP_NSHELL)) == NULL)
         /* (Error message written by fexpand()) */
         goto jgetname;
      else
         f = f3;
   }
   if (f == NULL || f == (char*)-1) {
      fp = NULL;
      goto jleave;
   }

   if (*f == '|') {
      char const *cp;
      cp = ok_vlook(SHELL);
      if (cp == NULL)
         cp = XSHELL;
      fp = Popen(f + 1, "w", cp, NULL, 1);
      if (!(*ispipe = (fp != NULL)))
         n_perr(f, 0);
   } else {
      if ((fp = Fopen(f, "w")) == NULL)
         n_err(_("Cannot open \"%s\"\n"), f);
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
   cnt = fsize(pipebuf);
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

   colour_put(fp, COLOURSPEC_FROM_);
   i = fprintf(fp, "From %s %s%s", froma, date, nl);
   colour_reset(fp);
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
   struct str const *cpre, *csuf;
#ifdef HAVE_COLOUR
   cpre = colour_get(COLOURSPEC_FROM_);
   csuf = colour_get(COLOURSPEC_RESET);
#else
   cpre = csuf = NULL;
#endif
   if (mp->m_flag & MNOFROM) {
      if (doign != allignore && doign != fwdignore && action != SEND_RFC822)
         sz = fprintf(obuf, "%s%.*sFrom %s %s%s\n",
               (cpre != NULL ? cpre->s : ""),
               (int)qf.qf_pfix_len, (qf.qf_pfix_len != 0 ? qf.qf_pfix : ""),
               fakefrom(mp), fakedate(mp->m_time),
               (csuf != NULL ? csuf->s : ""));
   } else {
      if (doign != allignore && doign != fwdignore && action != SEND_RFC822) {
         if (qf.qf_pfix_len > 0) {
            i = fwrite(qf.qf_pfix, sizeof *qf.qf_pfix, qf.qf_pfix_len, obuf);
            if (i != qf.qf_pfix_len)
               goto jleave;
            sz += i;
         }
#ifdef HAVE_COLOUR
         if (cpre != NULL) {
            fputs(cpre->s, obuf);
            cpre = (struct str const*)0x1;
         }
#endif
      }

      while (cnt > 0 && (c = getc(ibuf)) != EOF) {
         if (doign != allignore && doign != fwdignore &&
               action != SEND_RFC822) {
#ifdef HAVE_COLOUR
            if (c == '\n' && csuf != NULL) {
               cpre = (struct str const*)0x1;
               fputs(csuf->s, obuf);
            }
#endif
            putc(c, obuf);
            sz++;
         }
         --cnt;
         if (c == '\n')
            break;
      }

#ifdef HAVE_COLOUR
      if (csuf != NULL && cpre != (struct str const*)0x1)
         fputs(csuf->s, obuf);
#endif
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
