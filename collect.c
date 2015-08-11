/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Collect input from standard input, handling ~ escapes.
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
#define n_FILE collect

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* The following hookiness with global variables is so that on receipt of an
 * interrupt signal, the partial message can be salted away on *DEAD* */

static sighandler_type  _coll_saveint;    /* Previous SIGINT value */
static sighandler_type  _coll_savehup;    /* Previous SIGHUP value */
static sighandler_type  _coll_savetstp;   /* Previous SIGTSTP value */
static sighandler_type  _coll_savettou;   /* Previous SIGTTOU value */
static sighandler_type  _coll_savettin;   /* Previous SIGTTIN value */
static FILE             *_coll_fp;        /* File for saving away */
static int volatile     _coll_hadintr;    /* Have seen one SIGINT so far */
static sigjmp_buf       _coll_jmp;        /* To get back to work */
static int              _coll_jmp_p;      /* whether to long jump */
static sigjmp_buf       _coll_abort;      /* To end collection with error */
static sigjmp_buf       _coll_pipejmp;    /* On broken pipe */

/* Handle `~:', `~_' */
static void       _execute_command(struct header *hp, char *linebuf,
                     size_t linesize);

/* If *interactive* is set and *doecho* is, too, also dump to *stdout* */
static int        _include_file(char const *name, int *linecount,
                     int *charcount, bool_t doecho, bool_t indent);

static void       _collect_onpipe(int signo);

/* Execute cmd and insert its standard output into fp */
static void       insertcommand(FILE *fp, char const *cmd);

/* ~p command */
static void       print_collf(FILE *collf, struct header *hp);

/* Write a file, ex-like if f set */
static int        exwrite(char const *name, FILE *fp, int f);

static enum okay  makeheader(FILE *fp, struct header *hp, si8_t *checkaddr_err);

/* Edit the message being collected on fp.  On return, make the edit file the
 * new temp file */
static void       mesedit(int c, struct header *hp);

/* Pipe the message through the command.  Old message is on stdin of command,
 * new message collected from stdout.  Shell must return 0 to accept new msg */
static void       mespipe(char *cmd);

/* Interpolate the named messages into the current message, possibly doing
 * indent stuff.  The flag argument is one of the tilde escapes: [mMfFuU].
 * Return a count of the number of characters now in the message, or -1 if an
 * error is encountered writing the message temporary */
static int        forward(char *ms, FILE *fp, int f);

/* Print (continue) when continued after ^Z */
static void       collstop(int s);

/* On interrupt, come here to save the partial message in ~/dead.letter.
 * Then jump out of the collection loop */
static void       _collint(int s);

static void       collhup(int s);

static int        putesc(char const *s, FILE *stream);

static void
_execute_command(struct header *hp, char *linebuf, size_t linesize)
{
   /* The problem arises if there are rfc822 message attachments and the
    * user uses `~:' to change the current file.  TODO Unfortunately we
    * TODO cannot simply keep a pointer to, or increment a reference count
    * TODO of the current `file' (mailbox that is) object, because the
    * TODO codebase doesn't deal with that at all; so, until some far
    * TODO later time, copy the name of the path, and warn the user if it
    * TODO changed; we COULD use the AC_TMPFILE attachment type, i.e.,
    * TODO copy the message attachments over to temporary files, but that
    * TODO would require more changes so that the user still can recognize
    * TODO in `~@' etc. that its a rfc822 message attachment; see below */
   char *mnbuf = NULL;
   size_t mnlen = 0 /* silence CC */;
   struct attachment *ap;
   NYD_ENTER;

   /* If the above todo is worked, remove or outsource to attachments.c! */
   if ((ap = hp->h_attach) != NULL) do
      if (ap->a_msgno) {
         mnlen = strlen(mailname) +1;
         mnbuf = ac_alloc(mnlen);
         memcpy(mnbuf, mailname, mnlen);
         break;
      }
   while ((ap = ap->a_flink) != NULL);

   pstate &= ~PS_HOOK_MASK;
   execute(linebuf, linesize);

   if (mnbuf != NULL) {
      if (strncmp(mnbuf, mailname, mnlen))
         n_err(_("Mailbox changed: it is likely that existing "
            "rfc822 attachments became invalid!\n"));
      ac_free(mnbuf);
   }
   NYD_LEAVE;
}

static int
_include_file(char const *name, int *linecount, int *charcount,
   bool_t doecho, bool_t indent)
{
   FILE *fbuf;
   char const *indb;
   int ret = -1;
   char *linebuf = NULL; /* TODO line pool */
   size_t linesize = 0, indl, linelen, cnt;
   NYD_ENTER;

   if ((fbuf = Fopen(name, "r")) == NULL) {
      n_perr(name, 0);
      goto jleave;
   }

   if (!indent)
      indb = NULL, indl = 0;
   else {
      if ((indb = ok_vlook(indentprefix)) == NULL)
         indb = INDENT_DEFAULT;
      indl = strlen(indb);
   }

   *linecount = *charcount = 0;
   cnt = fsize(fbuf);
   while (fgetline(&linebuf, &linesize, &cnt, &linelen, fbuf, 0) != NULL) {
      if (indl > 0 && fwrite(indb, sizeof *indb, indl, _coll_fp) != indl)
         goto jleave;
      if (fwrite(linebuf, sizeof *linebuf, linelen, _coll_fp) != linelen)
         goto jleave;
      ++(*linecount);
      (*charcount) += linelen + indl;
      if ((options & OPT_INTERACTIVE) && doecho) {
         if (indl > 0)
            fwrite(indb, sizeof *indb, indl, stdout);
         fwrite(linebuf, sizeof *linebuf, linelen, stdout);
      }
   }
   if (fflush(_coll_fp))
      goto jleave;
   if ((options & OPT_INTERACTIVE) && doecho)
      fflush(stdout);

   ret = 0;
jleave:
   if (linebuf != NULL)
      free(linebuf);
   if (fbuf != NULL)
      Fclose(fbuf);
   NYD_LEAVE;
   return ret;
}

static void
_collect_onpipe(int signo)
{
   NYD_X; /* Signal handler */
   UNUSED(signo);
   siglongjmp(_coll_pipejmp, 1);
}

static void
insertcommand(FILE *fp, char const *cmd)
{
   FILE *ibuf = NULL;
   char const *cp;
   int c;
   NYD_ENTER;

   cp = ok_vlook(SHELL);
   if (cp == NULL)
      cp = XSHELL;

   if ((ibuf = Popen(cmd, "r", cp, NULL, 0)) != NULL) {
      while ((c = getc(ibuf)) != EOF) /* XXX bytewise, yuck! */
         putc(c, fp);
      Pclose(ibuf, TRU1);
   } else
      n_perr(cmd, 0);
   NYD_LEAVE;
}

static void
print_collf(FILE *cf, struct header *hp)
{
   char *lbuf = NULL; /* TODO line pool */
   sighandler_type sigint;
   FILE *volatile obuf = stdout;
   struct attachment *ap;
   char const *cp;
   enum gfield gf;
   size_t linesize = 0, linelen, cnt, cnt2;
   NYD_ENTER;

   fflush_rewind(cf);
   cnt = cnt2 = fsize(cf);

   sigint = safe_signal(SIGINT, SIG_IGN);

   if (IS_TTY_SESSION() && (cp = ok_vlook(crt)) != NULL) {
      size_t l, m;

      m = 4;
      if (hp->h_to != NULL)
         ++m;
      if (hp->h_subject != NULL)
         ++m;
      if (hp->h_cc != NULL)
         ++m;
      if (hp->h_bcc != NULL)
         ++m;
      if (hp->h_attach != NULL)
         ++m;
      m += (hp->h_from != NULL || myaddrs(hp) != NULL);
      m += (hp->h_sender != NULL || ok_vlook(sender) != NULL);
      m += (hp->h_replyto != NULL || ok_vlook(replyto) != NULL);
      m += (hp->h_organization != NULL || ok_vlook(ORGANIZATION) != NULL);

      l = (*cp == '\0') ? screensize() : atoi(cp);
      if (m > l)
         goto jpager;
      l -= m;

      for (m = 0; fgetline(&lbuf, &linesize, &cnt2, NULL, cf, 0); ++m)
         ;
      rewind(cf);
      if (l < m) {
jpager:
         cp = get_pager(NULL);
         if (sigsetjmp(_coll_pipejmp, 1))
            goto jendpipe;
         obuf = Popen(cp, "w", NULL, NULL, 1);
         if (obuf == NULL) {
            n_perr(cp, 0);
            obuf = stdout;
         } else
            safe_signal(SIGPIPE, &_collect_onpipe);
      }
   }

   fprintf(obuf, _("-------\nMessage contains:\n"));
   gf = GIDENT | GTO | GSUBJECT | GCC | GBCC | GNL | GFILES | GCOMMA;
   puthead(hp, obuf, gf, SEND_TODISP, CONV_NONE, NULL, NULL);
   while (fgetline(&lbuf, &linesize, &cnt, &linelen, cf, 1))
      prout(lbuf, linelen, obuf);
   if (hp->h_attach != NULL) {
      fputs(_("-------\nAttachments:\n"), obuf);
      for (ap = hp->h_attach; ap != NULL; ap = ap->a_flink) {
         if (ap->a_msgno)
            fprintf(obuf, " - message %u\n", ap->a_msgno);
         else {
            /* TODO after MIME/send layer rewrite we *know*
             * TODO the details of the attachment here,
             * TODO so adjust this again, then */
            char const *cs, *csi = "-> ";

            if ((cs = ap->a_charset) == NULL &&
                  (csi = "<- ", cs = ap->a_input_charset) == NULL)
               cs = charset_get_lc();
            if ((cp = ap->a_content_type) == NULL)
               cp = "?";
            else if (ascncasecmp(cp, "text/", 5))
               csi = "";
            fprintf(obuf, " - [%s, %s%s] %s\n", cp, csi, cs, ap->a_name);
         }
      }
   }

jendpipe:
   if (obuf != stdout) {
      safe_signal(SIGPIPE, SIG_IGN);
      Pclose(obuf, TRU1);
      safe_signal(SIGPIPE, dflpipe);
   }
   if (lbuf != NULL)
      free(lbuf);
   safe_signal(SIGINT, sigint);
   NYD_LEAVE;
}

static int
exwrite(char const *name, FILE *fp, int f)
{
   FILE *of;
   int c, lc, rv = -1;
   long cc;
   NYD_ENTER;

   if (f) {
      printf("\"%s\" ", name);
      fflush(stdout);
   }
   if ((of = Fopen(name, "a")) == NULL) {
      n_perr(NULL, 0);
      goto jleave;
   }

   lc = 0;
   cc = 0;
   while ((c = getc(fp)) != EOF) {
      ++cc;
      if (c == '\n')
         ++lc;
      putc(c, of);
      if (ferror(of)) {
         n_perr(name, 0);
         Fclose(of);
         goto jleave;
      }
   }
   Fclose(of);
   printf(_("%d/%ld\n"), lc, cc);
   fflush(stdout);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
makeheader(FILE *fp, struct header *hp, si8_t *checkaddr_err)
{
   FILE *nf;
   int c;
   enum okay rv = STOP;
   NYD_ENTER;

   if ((nf = Ftmp(NULL, "colhead", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      n_perr(_("temporary mail edit file"), 0);
      goto jleave;
   }

   extract_header(fp, hp, checkaddr_err);

   while ((c = getc(fp)) != EOF) /* XXX bytewise, yuck! */
      putc(c, nf);
   if (fp != _coll_fp)
      Fclose(_coll_fp);
   Fclose(fp);
   _coll_fp = nf;
   if (check_from_and_sender(hp->h_from, hp->h_sender) == NULL)
      goto jleave;
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

static void
mesedit(int c, struct header *hp)
{
   bool_t saved;
   sighandler_type sigint;
   FILE *nf;
   NYD_ENTER;

   saved = ok_blook(add_file_recipients);
   ok_bset(add_file_recipients, TRU1);

   sigint = safe_signal(SIGINT, SIG_IGN);
   nf = run_editor(_coll_fp, (off_t)-1, c, 0, hp, NULL, SEND_MBOX, sigint);
   if (nf != NULL) {
      if (hp) {
         rewind(nf);
         makeheader(nf, hp, NULL);
      } else {
         fseek(nf, 0L, SEEK_END);
         Fclose(_coll_fp);
         _coll_fp = nf;
      }
   }
   safe_signal(SIGINT, sigint);

   ok_bset(add_file_recipients, saved);
   NYD_LEAVE;
}

static void
mespipe(char *cmd)
{
   FILE *nf;
   sighandler_type sigint;
   char const *sh;
   NYD_ENTER;

   sigint = safe_signal(SIGINT, SIG_IGN);

   if ((nf = Ftmp(NULL, "colpipe", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      n_perr(_("temporary mail edit file"), 0);
      goto jout;
   }

   /* stdin = current message.  stdout = new message */
   if ((sh = ok_vlook(SHELL)) == NULL)
      sh = XSHELL;
   fflush(_coll_fp);
   if (run_command(sh, 0, fileno(_coll_fp), fileno(nf), "-c", cmd, NULL) < 0) {
      Fclose(nf);
      goto jout;
   }

   if (fsize(nf) == 0) {
      n_err(_("No bytes from \"%s\" !?\n"), cmd);
      Fclose(nf);
      goto jout;
   }

   /* Take new files */
   fseek(nf, 0L, SEEK_END);
   Fclose(_coll_fp);
   _coll_fp = nf;
jout:
   safe_signal(SIGINT, sigint);
   NYD_LEAVE;
}

static int
forward(char *ms, FILE *fp, int f)
{
   int *msgvec, rv = 0;
   struct ignoretab *ig;
   char const *tabst;
   enum sendaction action;
   NYD_ENTER;

   msgvec = salloc((size_t)(msgCount + 1) * sizeof *msgvec);
   if (getmsglist(ms, msgvec, 0) < 0)
      goto jleave;
   if (*msgvec == 0) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         n_err(_("No appropriate messages\n"));
         goto jleave;
      }
      msgvec[1] = 0;
   }

   if (f == 'f' || f == 'F' || f == 'u')
      tabst = NULL;
   else if ((tabst = ok_vlook(indentprefix)) == NULL)
      tabst = INDENT_DEFAULT;
   if (f == 'u' || f == 'U')
      ig = allignore;
   else
      ig = upperchar(f) ? NULL : ignore;
   action = (upperchar(f) && f != 'U') ? SEND_QUOTE_ALL : SEND_QUOTE;

   printf(_("Interpolating:"));
   for (; *msgvec != 0; ++msgvec) {
      struct message *mp = message + *msgvec - 1;

      touch(mp);
      printf(" %d", *msgvec);
      fflush(stdout);
      if (sendmp(mp, fp, ig, tabst, action, NULL) < 0) {
         n_perr(_("temporary mail file"), 0);
         rv = -1;
         break;
      }
   }
   printf("\n");
jleave:
   NYD_LEAVE;
   return rv;
}

static void
collstop(int s)
{
   sighandler_type old_action;
   sigset_t nset;
   NYD_X; /* Signal handler */

   old_action = safe_signal(s, SIG_DFL);

   sigemptyset(&nset);
   sigaddset(&nset, s);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);
   n_raise(s);
   sigprocmask(SIG_BLOCK, &nset, NULL);

   safe_signal(s, old_action);
   if (_coll_jmp_p) {
      _coll_jmp_p = 0;
      _coll_hadintr = 0;
      siglongjmp(_coll_jmp, 1);
   }
}

static void
_collint(int s)
{
   NYD_X; /* Signal handler */

   /* the control flow is subtle, because we can be called from ~q */
   if (_coll_hadintr == 0) {
      if (ok_blook(ignore)) {
         puts("@");
         fflush(stdout);
         clearerr(stdin);
      } else
         _coll_hadintr = 1;
      siglongjmp(_coll_jmp, 1);
   }
   exit_status |= EXIT_SEND_ERROR;
   if (s != 0)
      savedeadletter(_coll_fp, 1);
   /* Aborting message, no need to fflush() .. */
   siglongjmp(_coll_abort, 1);
}

static void
collhup(int s)
{
   NYD_X; /* Signal handler */
   UNUSED(s);

   savedeadletter(_coll_fp, 1);
   /* Let's pretend nobody else wants to clean up, a true statement at
    * this time */
   exit(EXIT_ERR);
}

static int
putesc(char const *s, FILE *stream)
{
   int n = 0, rv = -1;
   NYD_ENTER;

   while (s[0] != '\0') {
      if (s[0] == '\\') {
         if (s[1] == 't') {
            if (putc('\t', stream) == EOF)
               goto jleave;
            ++n;
            s += 2;
            continue;
         }
         if (s[1] == 'n') {
            if (putc('\n', stream) == EOF)
               goto jleave;
            ++n;
            s += 2;
            continue;
         }
      }
      if (putc(s[0], stream) == EOF)
         goto jleave;
      ++n;
      ++s;
   }
   if (putc('\n', stream) == EOF)
      goto jleave;
   rv = ++n;
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
collect(struct header *hp, int printheaders, struct message *mp,
   char *quotefile, int doprefix, si8_t *checkaddr_err)
{
   struct ignoretab *quoteig;
   int lc, cc, c, t;
   int volatile escape, getfields;
   char *linebuf = NULL, *quote = NULL;
   char const *cp;
   size_t linesize = 0; /* TODO line pool */
   long cnt;
   enum sendaction action;
   sigset_t oset, nset;
   sighandler_type savedtop;
   NYD_ENTER;

   _coll_fp = NULL;
   /* Start catching signals from here, but we're still die on interrupts
    * until we're in the main loop */
   sigemptyset(&nset);
   sigaddset(&nset, SIGINT);
   sigaddset(&nset, SIGHUP);
   sigprocmask(SIG_BLOCK, &nset, &oset);
   handlerpush(&_collint);
   if ((_coll_saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_collint);
   if ((_coll_savehup = safe_signal(SIGHUP, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGHUP, collhup);
   /* TODO We do a lot of redundant signal handling, especially
    * TODO with the command line editor(s); try to merge this */
   _coll_savetstp = safe_signal(SIGTSTP, collstop);
   _coll_savettou = safe_signal(SIGTTOU, collstop);
   _coll_savettin = safe_signal(SIGTTIN, collstop);
   if (sigsetjmp(_coll_abort, 1))
      goto jerr;
   if (sigsetjmp(_coll_jmp, 1))
      goto jerr;
   sigprocmask(SIG_SETMASK, &oset, (sigset_t*)NULL);

   ++noreset;
   if ((_coll_fp = Ftmp(NULL, "collect", OF_RDWR | OF_UNLINK | OF_REGISTER,
         0600)) == NULL) {
      n_perr(_("temporary mail file"), 0);
      goto jerr;
   }

   if ((cp = ok_vlook(NAIL_HEAD)) != NULL && putesc(cp, _coll_fp) < 0)
      goto jerr;

   /* If we are going to prompt for a subject, refrain from printing a newline
    * after the headers (since some people mind) */
   getfields = 0;
   if (!(options & OPT_t_FLAG)) {
      t = GTO | GSUBJECT | GCC | GNL;
      if (ok_blook(fullnames))
         t |= GCOMMA;

      if (options & OPT_INTERACTIVE) {
         if (hp->h_subject == NULL && (ok_blook(ask) || ok_blook(asksub)))
            t &= ~GNL, getfields |= GSUBJECT;

         if (hp->h_to == NULL)
            t &= ~GNL, getfields |= GTO;

         if (!ok_blook(bsdcompat) && !ok_blook(askatend)) {
            if (hp->h_bcc == NULL && ok_blook(askbcc))
               t &= ~GNL, getfields |= GBCC;
            if (hp->h_cc == NULL && ok_blook(askcc))
               t &= ~GNL, getfields |= GCC;
         }
      }

      if (printheaders && !ok_blook(editalong)) {
         puthead(hp, stdout, t, SEND_TODISP, CONV_NONE, NULL, NULL);
         fflush(stdout);
      }
   }

   /* Quote an original message */
   if (mp != NULL && (doprefix || (quote = ok_vlook(quote)) != NULL)) {
      quoteig = allignore;
      action = SEND_QUOTE;
      if (doprefix) {
         quoteig = fwdignore;
         if ((cp = ok_vlook(fwdheading)) == NULL)
            cp = "-------- Original Message --------";
         if (*cp != '\0' && fprintf(_coll_fp, "%s\n", cp) < 0)
            goto jerr;
      } else if (!strcmp(quote, "noheading")) {
         /*EMPTY*/;
      } else if (!strcmp(quote, "headers")) {
         quoteig = ignore;
      } else if (!strcmp(quote, "allheaders")) {
         quoteig = NULL;
         action = SEND_QUOTE_ALL;
      } else {
         cp = hfield1("from", mp);
         if (cp != NULL && (cnt = (long)strlen(cp)) > 0) {
            if (xmime_write(cp, cnt, _coll_fp, CONV_FROMHDR, TD_NONE) < 0)
               goto jerr;
            if (fprintf(_coll_fp, _(" wrote:\n\n")) < 0)
               goto jerr;
         }
      }
      if (fflush(_coll_fp))
         goto jerr;
      if (doprefix)
         cp = NULL;
      else if ((cp = ok_vlook(indentprefix)) == NULL)
         cp = INDENT_DEFAULT;
      if (sendmp(mp, _coll_fp, quoteig, cp, action, NULL) < 0)
         goto jerr;
   }

   /* Print what we have sofar also on the terminal (if useful) */
   if ((options & OPT_INTERACTIVE) && !ok_blook(editalong)) {
      rewind(_coll_fp);
      while ((c = getc(_coll_fp)) != EOF) /* XXX bytewise, yuck! */
         putc(c, stdout);
      if (fseek(_coll_fp, 0, SEEK_END))
         goto jerr;
      /* Ensure this is clean xxx not really necessary? */
      fflush(stdout);
   }

   escape = ((cp = ok_vlook(escape)) != NULL) ? *cp : ESCAPE;
   _coll_hadintr = 0;

   if (!sigsetjmp(_coll_jmp, 1)) {
      if (getfields)
         grab_headers(hp, getfields, 1);
      if (quotefile != NULL) {
         if (_include_file(quotefile, &lc, &cc, TRU1, FAL0) != 0)
            goto jerr;
      }
      if ((options & OPT_INTERACTIVE) && ok_blook(editalong)) {
         rewind(_coll_fp);
         mesedit('e', hp);
         goto jcont;
      }
   } else {
      /* Come here for printing the after-signal message.  Duplicate messages
       * won't be printed because the write is aborted if we get a SIGTTOU */
      if (_coll_hadintr) {
         n_err(_("\n(Interrupt -- one more to kill letter)\n"));
      } else {
jcont:
         printf(_("(continue)\n"));
         fflush(stdout);
      }
   }

   /* No tilde escapes, interrupts not expected.  Simply copy STDIN */
   if (!(options & (OPT_INTERACTIVE | OPT_t_FLAG | OPT_TILDE_FLAG))) {
      linebuf = srealloc(linebuf, linesize = LINESIZE);
      while ((cnt = fread(linebuf, sizeof *linebuf, linesize, stdin)) > 0) {
         if ((size_t)cnt != fwrite(linebuf, sizeof *linebuf, cnt, _coll_fp))
            goto jerr;
      }
      if (fflush(_coll_fp))
         goto jerr;
      goto jout;
   }

   /* The interactive collect loop.
    * All commands which come here are forbidden when sourcing! */
   assert(_coll_hadintr || !(pstate & PS_SOURCING));
   for (;;) {
      _coll_jmp_p = 1;
      cnt = readline_input("", FAL0, &linebuf, &linesize, NULL);
      _coll_jmp_p = 0;

      if (cnt < 0) {
         assert(!(pstate & PS_SOURCING));
         if (options & OPT_t_FLAG) {
            fflush_rewind(_coll_fp);
            /* It is important to set PS_t_FLAG before extract_header() *and*
             * keep OPT_t_FLAG for the first parse of the message, too! */
            pstate |= PS_t_FLAG;
            if (makeheader(_coll_fp, hp, checkaddr_err) != OKAY)
               goto jerr;
            rewind(_coll_fp);
            options &= ~OPT_t_FLAG;
            continue;
         } else if ((options & OPT_INTERACTIVE) && ok_blook(ignoreeof)) {
            printf(_("*ignoreeof* set, use \".\" to terminate letter\n"));
            continue;
         }
         break;
      }

      _coll_hadintr = 0;

      if (cnt == 0 || !(options & (OPT_INTERACTIVE | OPT_TILDE_FLAG))) {
jputline:
         /* TODO calls putline(), which *always* appends LF;
          * TODO thus, STDIN with -t will ALWAYS end with LF,
          * TODO even if no trailing LF and QP encoding.
          * TODO when finally changed, update cc-test.sh */
         if (putline(_coll_fp, linebuf, cnt) < 0)
            goto jerr;
         continue;
      } else if (linebuf[0] == '.') {
         if (linebuf[1] == '\0' && (ok_blook(dot) || ok_blook(ignoreeof)))
            break;
      }
      if (linebuf[0] != escape)
         goto jputline;

      tty_addhist(linebuf, TRU1);

      c = linebuf[1];
      switch (c) {
      default:
         /* On double escape, send a single one.  Otherwise, it's an error */
         if (c == escape) {
            if (putline(_coll_fp, linebuf + 1, cnt - 1) < 0)
               goto jerr;
            else
               break;
         }
         n_err(_("Unknown tilde escape: ~%c\n"), asciichar(c) ? c : '?');
         break;
      case '!':
         /* Shell escape, send the balance of line to sh -c */
         c_shell(linebuf + 2);
         break;
      case ':':
         /* FALLTHRU */
      case '_':
         /* Escape to command mode, but be nice! */
         _execute_command(hp, linebuf + 2, cnt - 2);
         goto jcont;
      case '.':
         /* Simulate end of file on input */
         goto jout;
      case 'x':
         /* Same as 'q', but no *DEAD* saving */
         /* FALLTHRU */
      case 'q':
         /* Force a quit, act like an interrupt had happened */
         ++_coll_hadintr;
         _collint((c == 'x') ? 0 : SIGINT);
         exit(EXIT_ERR);
         /*NOTREACHED*/
      case 'h':
         /* Grab a bunch of headers */
         do
            grab_headers(hp, GTO | GSUBJECT | GCC | GBCC,
                  (ok_blook(bsdcompat) && ok_blook(bsdorder)));
         while (hp->h_to == NULL);
         goto jcont;
      case 'H':
         /* Grab extra headers */
         do
            grab_headers(hp, GEXTRA, 0);
         while (check_from_and_sender(hp->h_from, hp->h_sender) == NULL);
         goto jcont;
      case 't':
         /* Add to the To list */
         hp->h_to = cat(hp->h_to,
               checkaddrs(lextract(linebuf + 2, GTO | GFULL), EACM_NORMAL,
                  NULL));
         break;
      case 's':
         /* Set the Subject list */
         cp = linebuf + 2;
         while (whitechar(*cp))
            ++cp;
         hp->h_subject = savestr(cp);
         break;
#ifdef HAVE_DEBUG
      case 'S':
         c_sstats(NULL);
         break;
#endif
      case '@':
         /* Edit the attachment list */
         if (linebuf[2] != '\0')
            append_attachments(&hp->h_attach, linebuf + 2);
         else
            edit_attachments(&hp->h_attach);
         break;
      case 'c':
         /* Add to the CC list */
         hp->h_cc = cat(hp->h_cc,
               checkaddrs(lextract(linebuf + 2, GCC | GFULL), EACM_NORMAL,
               NULL));
         break;
      case 'b':
         /* Add stuff to blind carbon copies list */
         hp->h_bcc = cat(hp->h_bcc,
               checkaddrs(lextract(linebuf + 2, GBCC | GFULL), EACM_NORMAL,
                  NULL));
         break;
      case 'd':
         strncpy(linebuf + 2, getdeadletter(), linesize - 2);
         linebuf[linesize - 1] = '\0';
         /*FALLTHRU*/
      case 'R':
      case 'r':
      case '<':
         /* Invoke a file: Search for the file name, then open it and copy the
          * contents to _coll_fp */
         cp = linebuf + 2;
         while (whitechar(*cp))
            ++cp;
         if (*cp == '\0') {
            n_err(_("Interpolate what file?\n"));
            break;
         }
         if (*cp == '!') {
            insertcommand(_coll_fp, cp + 1);
            break;
         }
         if ((cp = file_expand(cp)) == NULL)
            break;
         if (is_dir(cp)) {
            n_err(_("\"%s\": Directory\n"), cp);
            break;
         }
         printf(_("\"%s\" "), cp);
         fflush(stdout);
         if (_include_file(cp, &lc, &cc, FAL0, (c == 'R')) != 0)
            goto jerr;
         printf(_("%d/%d\n"), lc, cc);
         break;
      case 'i':
         /* Insert a variable into the file */
         cp = linebuf + 2;
         while (whitechar(*cp))
            ++cp;
         if ((cp = vok_vlook(cp)) == NULL || *cp == '\0')
            break;
         if (putesc(cp, _coll_fp) < 0)
            goto jerr;
         if ((options & OPT_INTERACTIVE) && putesc(cp, stdout) < 0)
            goto jerr;
         break;
      case 'a':
      case 'A':
         /* Insert the contents of a signature variable */
         cp = (c == 'a') ? ok_vlook(sign) : ok_vlook(Sign);
         if (cp != NULL && *cp != '\0') {
            if (putesc(cp, _coll_fp) < 0)
               goto jerr;
            if ((options & OPT_INTERACTIVE) && putesc(cp, stdout) < 0)
               goto jerr;
         }
         break;
      case 'w':
         /* Write the message on a file */
         cp = linebuf + 2;
         while (blankchar(*cp))
            ++cp;
         if (*cp == '\0' || (cp = file_expand(cp)) == NULL) {
            n_err(_("Write what file!?\n"));
            break;
         }
         rewind(_coll_fp);
         if (exwrite(cp, _coll_fp, 1) < 0)
            goto jerr;
         break;
      case 'm':
      case 'M':
      case 'f':
      case 'F':
      case 'u':
      case 'U':
         /* Interpolate the named messages, if we are in receiving mail mode.
          * Does the standard list processing garbage.  If ~f is given, we
          * don't shift over */
         if (forward(linebuf + 2, _coll_fp, c) < 0)
            goto jerr;
         goto jcont;
      case 'p':
         /* Print current state of the message without altering anything */
         print_collf(_coll_fp, hp);
         goto jcont;
      case '|':
         /* Pipe message through command. Collect output as new message */
         rewind(_coll_fp);
         mespipe(linebuf + 2);
         goto jcont;
      case 'v':
      case 'e':
         /* Edit the current message.  'e' -> use EDITOR, 'v' -> use VISUAL */
         rewind(_coll_fp);
         mesedit(c, ok_blook(editheaders) ? hp : NULL);
         goto jcont;
      case '?':
         /* Last the lengthy help string.  (Very ugly, but take care for
          * compiler supported string lengths :() */
         puts(_(
"-------------------- ~ ESCAPES ----------------------------\n"
"~~             Quote a single tilde\n"
"~@ [file ...]  Edit attachment list\n"
"~b users       Add users to \"blind\" Bcc: list\n"
"~c users       Add users to Cc: list\n"
"~d             Read in dead.letter\n"
"~e             Edit the message buffer\n"
"~F messages    Read in messages including all headers, don't indent lines\n"
"~f messages    Like ~F, but honour the `ignore' / `retain' configuration\n"
"~h             Prompt for Subject:, To:, Cc: and \"blind\" Bcc:\n"));
         puts(_(
"~R file        Read in a file, indent lines\n"
"~r file        Read in a file\n"
"~p             Print the message buffer\n"
"~q             Abort message composition and save text to DEAD\n"
"~M messages    Read in messages, keep all header lines, indent lines\n"
"~m messages    Like ~M, but honour the `ignore' / `retain' configuration\n"
"~s subject     Set Subject:\n"
"~t users       Add users to To: list\n"));
         puts(_(
"~U messages    Read in message(s) without any headers, indent lines\n"
"~u messages    Read in message(s) without any headers\n"
"~v             Invoke alternate editor ($VISUAL) on message\n"
"~w file        Write message onto file\n"
"~x             Abort message composition and discard message\n"
"~!command      Invoke the shell\n"
"~:command      Execute a regular command\n"
"-----------------------------------------------------------\n"));
         break;
      }
   }

jout:
   if (_coll_fp != NULL) {
      if ((cp = ok_vlook(NAIL_TAIL)) != NULL) {
         if (putesc(cp, _coll_fp) < 0)
            goto jerr;
         if ((options & OPT_INTERACTIVE) && putesc(cp, stdout) < 0)
            goto jerr;
      }
      rewind(_coll_fp);
   }
   if (linebuf != NULL)
      free(linebuf);
   handlerpop();
   --noreset;
   sigemptyset(&nset);
   sigaddset(&nset, SIGINT);
   sigaddset(&nset, SIGHUP);
   sigprocmask(SIG_BLOCK, &nset, NULL);
   safe_signal(SIGINT, _coll_saveint);
   safe_signal(SIGHUP, _coll_savehup);
   safe_signal(SIGTSTP, _coll_savetstp);
   safe_signal(SIGTTOU, _coll_savettou);
   safe_signal(SIGTTIN, _coll_savettin);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD_LEAVE;
   return _coll_fp;

jerr:
   if (_coll_fp != NULL) {
      Fclose(_coll_fp);
      _coll_fp = NULL;
   }
   goto jout;
}

FL void
savedeadletter(FILE *fp, int fflush_rewind_first)
{
   char const *cp;
   int c;
   FILE *dbuf;
   ul_i lines, bytes;
   NYD_ENTER;

   if (!ok_blook(save))
      goto jleave;

   if (fflush_rewind_first) {
      fflush(fp);
      rewind(fp);
   }
   if (fsize(fp) == 0)
      goto jleave;

   cp = getdeadletter();
   c = umask(077);
   dbuf = Fopen(cp, "a");
   umask(c);
   if (dbuf == NULL)
      goto jleave;

   really_rewind(fp);

   printf("\"%s\" ", cp);
   for (lines = bytes = 0; (c = getc(fp)) != EOF; ++bytes) {
      putc(c, dbuf);
      if (c == '\n')
         ++lines;
   }
   printf("%lu/%lu\n", lines, bytes);

   Fclose(dbuf);
   rewind(fp);
jleave:
   NYD_LEAVE;
}

/* s-it-mode */
