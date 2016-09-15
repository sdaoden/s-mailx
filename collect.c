/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Collect input from standard input, handling ~ escapes.
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
#define n_FILE collect

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* The following hookiness with global variables is so that on receipt of an
 * interrupt signal, the partial message can be salted away on *DEAD* */

static sighandler_type  _coll_saveint;    /* Previous SIGINT value */
static sighandler_type  _coll_savehup;    /* Previous SIGHUP value */
static FILE             *_coll_fp;        /* File for saving away */
static int volatile     _coll_hadintr;    /* Have seen one SIGINT so far */
static sigjmp_buf       _coll_jmp;        /* To get back to work */
static sigjmp_buf       _coll_abort;      /* To end collection with error */
static sigjmp_buf       _coll_pipejmp;    /* On broken pipe */

/* Handle `~:', `~_' and some hooks; hp may be NULL */
static void       _execute_command(struct header *hp, char const *linebuf,
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

/* Parse off the message header from fp and store relevant fields in hp,
 * replace _coll_fp with a shiny new version without any header */
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

/* On interrupt, come here to save the partial message in ~/dead.letter.
 * Then jump out of the collection loop */
static void       _collint(int s);

static void       collhup(int s);

static int        putesc(char const *s, FILE *stream); /* TODO wysh set! */

/* call_compose_mode_hook() setter hook */
static void a_coll__hook_setter(void *arg);

static void
_execute_command(struct header *hp, char const *linebuf, size_t linesize){
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
   struct n_sigman sm;
   struct attachment *ap;
   char * volatile mnbuf;
   NYD_ENTER;

   UNUSED(linesize);
   mnbuf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      goto jleave;
   }

   /* If the above todo is worked, remove or outsource to attachments.c! */
   if(hp != NULL && (ap = hp->h_attach) != NULL) do
      if(ap->a_msgno){
         mnbuf = sstrdup(mailname);
         break;
      }
   while((ap = ap->a_flink) != NULL);

   n_source_command(n_LEXINPUT_CTX_COMPOSE, linebuf);

   n_sigman_cleanup_ping(&sm);
jleave:
   if(mnbuf != NULL){
      if(strcmp(mnbuf, mailname))
         n_err(_("Mailbox changed: it is likely that existing "
            "rfc822 attachments became invalid!\n"));
      free(mnbuf);
   }
   NYD_LEAVE;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
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

   if (name == (char*)-1)
      fbuf = stdin;
   else if ((fbuf = Fopen(name, "r")) == NULL) {
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
   if (fbuf != NULL && fbuf != stdin)
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
   int c;
   NYD_ENTER;

   if ((ibuf = Popen(cmd, "r", ok_vlook(SHELL), NULL, 0)) != NULL) {
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
   FILE * volatile obuf = stdout;
   struct attachment *ap;
   char const *cp;
   enum gfield gf;
   size_t linesize = 0, linelen, cnt, cnt2;
   NYD_ENTER;

   fflush_rewind(cf);
   cnt = cnt2 = (size_t)fsize(cf);

   sigint = safe_signal(SIGINT, SIG_IGN);

   if ((options & OPT_INTERACTIVE) && (cp = ok_vlook(crt)) != NULL) {
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

      l = (*cp == '\0') ? (size_t)screensize() : strtoul(cp, NULL, 0);
      if (m > l)
         goto jpager;
      l -= m;

      for (m = 0; fgetline(&lbuf, &linesize, &cnt2, NULL, cf, 0); ++m)
         ;
      rewind(cf);
      if (l < m) {
jpager:
         if (sigsetjmp(_coll_pipejmp, 1))
            goto jendpipe;
         if ((obuf = n_pager_open()) == NULL)
            obuf = stdout;
         else
            safe_signal(SIGPIPE, &_collect_onpipe);
      }
   }

   fprintf(obuf, _("-------\nMessage contains:\n"));
   gf = GIDENT | GTO | GSUBJECT | GCC | GBCC | GNL | GFILES | GCOMMA;
   puthead(TRU1, hp, obuf, gf, SEND_TODISP, CONV_NONE, NULL, NULL);
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
            fprintf(obuf, " - [%s, %s%s] %s\n", cp, csi, cs,
               n_shell_quote_cp(ap->a_name, FAL0));
         }
      }
   }

jendpipe:
   if (obuf != stdout)
      n_pager_close(obuf);
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
      printf("%s ", n_shell_quote_cp(name, FAL0));
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

   if ((nf = Ftmp(NULL, "colhead", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==NULL) {
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
   nf = run_editor(_coll_fp, (off_t)-1, c, FAL0, hp, NULL, SEND_MBOX, sigint);
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
   NYD_ENTER;

   sigint = safe_signal(SIGINT, SIG_IGN);

   if ((nf = Ftmp(NULL, "colpipe", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==NULL) {
      n_perr(_("temporary mail edit file"), 0);
      goto jout;
   }

   /* stdin = current message.  stdout = new message */
   fflush(_coll_fp);
   if (run_command(ok_vlook(SHELL), 0, fileno(_coll_fp), fileno(nf), "-c",
         cmd, NULL, NULL) < 0) {
      Fclose(nf);
      goto jout;
   }

   if (fsize(nf) == 0) {
      n_err(_("No bytes from %s !?\n"), n_shell_quote_cp(cmd, FAL0));
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
      savedeadletter(_coll_fp, TRU1);
   /* Aborting message, no need to fflush() .. */
   siglongjmp(_coll_abort, 1);
}

static void
collhup(int s)
{
   NYD_X; /* Signal handler */
   UNUSED(s);

   savedeadletter(_coll_fp, TRU1);
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

static void
a_coll__hook_setter(void *arg){ /* TODO v15: drop */
   struct header *hp;
   char const *val;
   NYD2_ENTER;

   hp = arg;

   if((val = detract(hp->h_from, GNAMEONLY)) == NULL)
      val = "";
   ok_vset(compose_from, val);
   if((val = detract(hp->h_sender, 0)) == NULL)
      val = "";
   ok_vset(compose_sender, val);
   if((val = detract(hp->h_to, GNAMEONLY)) == NULL)
      val = "";
   ok_vset(compose_to, val);
   if((val = detract(hp->h_cc, GNAMEONLY)) == NULL)
      val = "";
   ok_vset(compose_cc, val);
   if((val = detract(hp->h_bcc, GNAMEONLY)) == NULL)
      val = "";
   ok_vset(compose_bcc, val);
   if((val = hp->h_subject) == NULL)
      val = "";
   ok_vset(compose_subject, val);
   NYD2_LEAVE;
}

FL FILE *
collect(struct header *hp, int printheaders, struct message *mp,
   char *quotefile, int doprefix, si8_t *checkaddr_err)
{
   struct ignoretab *quoteig;
   int lc, cc, c;
   int volatile t;
   int volatile escape, getfields;
   char *linebuf;
   char const *cp;
   size_t i, linesize; /* TODO line pool */
   long cnt;
   enum sendaction action;
   sigset_t oset, nset;
   FILE * volatile sigfp;
   NYD_ENTER;

   _coll_fp = NULL;
   sigfp = NULL;
   linesize = 0;
   linebuf = NULL;

   /* Start catching signals from here, but we're still die on interrupts
    * until we're in the main loop */
   sigfillset(&nset);
   sigprocmask(SIG_BLOCK, &nset, &oset);
/* FIXME have dropped handlerpush() and localized onintr() in lex.c! */
   if ((_coll_saveint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGINT, &_collint);
   if ((_coll_savehup = safe_signal(SIGHUP, SIG_IGN)) != SIG_IGN)
      safe_signal(SIGHUP, collhup);
   if (sigsetjmp(_coll_abort, 1))
      goto jerr;
   if (sigsetjmp(_coll_jmp, 1))
      goto jerr;
   pstate |= PS_RECURSED;
   sigprocmask(SIG_SETMASK, &oset, (sigset_t*)NULL);

   ++noreset;
   if ((_coll_fp = Ftmp(NULL, "collect", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL) {
      n_perr(_("temporary mail file"), 0);
      goto jerr;
   }

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
   } else {
      UNINIT(t, 0);
   }

   escape = ((cp = ok_vlook(escape)) != NULL) ? *cp : ESCAPE;
   _coll_hadintr = 0;

   if (!sigsetjmp(_coll_jmp, 1)) {
      /* Ask for some headers first, as necessary */
      if (getfields)
         grab_headers(n_LEXINPUT_CTX_COMPOSE, hp, getfields, 1);

      /* Execute compose-enter TODO completely v15-compat intermediate!! */
      if((cp = ok_vlook(on_compose_enter)) != NULL){
         setup_from_and_sender(hp);
         call_compose_mode_hook(cp, &a_coll__hook_setter, hp);
      }

      if(!(options & OPT_Mm_FLAG)){
         char const *cp_obsolete = ok_vlook(NAIL_HEAD);
         if(cp_obsolete != NULL)
            OBSOLETE(_("please use *message-inject-head* "
               "instead of *NAIL_HEAD*"));

         if(((cp = ok_vlook(message_inject_head)) != NULL ||
            (cp = cp_obsolete) != NULL) && putesc(cp, _coll_fp) < 0)
         goto jerr;

         /* Quote an original message */
         if (mp != NULL && (doprefix || (cp = ok_vlook(quote)) != NULL)) {
            quoteig = allignore;
            action = SEND_QUOTE;
            if (doprefix) {
               quoteig = fwdignore;
               if ((cp = ok_vlook(fwdheading)) == NULL)
                  cp = "-------- Original Message --------";
               if (*cp != '\0' && fprintf(_coll_fp, "%s\n", cp) < 0)
                  goto jerr;
            } else if (!strcmp(cp, "noheading")) {
               /*EMPTY*/;
            } else if (!strcmp(cp, "headers")) {
               quoteig = ignore;
            } else if (!strcmp(cp, "allheaders")) {
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
      }

      if (quotefile != NULL) {
         if (_include_file(quotefile, &lc, &cc,
               !(options & OPT_Mm_FLAG), FAL0) != 0)
            goto jerr;
      }

      if ((options & (OPT_Mm_FLAG | OPT_INTERACTIVE)) == OPT_INTERACTIVE) {
         /* Print what we have sofar also on the terminal (if useful) */
         if (!ok_blook(editalong)) {
            if (printheaders)
               puthead(TRU1, hp, stdout, t, SEND_TODISP, CONV_NONE, NULL, NULL);

            rewind(_coll_fp);
            while ((c = getc(_coll_fp)) != EOF) /* XXX bytewise, yuck! */
               putc(c, stdout);
            if (fseek(_coll_fp, 0, SEEK_END))
               goto jerr;

            /* Ensure this is clean xxx not really necessary? */
            fflush(stdout);
         } else {
            rewind(_coll_fp);
            mesedit('e', hp);
            /* As mandated by the Mail Reference Manual, print "(continue)" */
jcont:
            if(options & OPT_INTERACTIVE){
               printf(_("(continue)\n"));
               fflush(stdout);
            }
         }
      }
   } else {
      /* Come here for printing the after-signal message.  Duplicate messages
       * won't be printed because the write is aborted if we get a SIGTTOU */
      if (_coll_hadintr)
         n_err(_("\n(Interrupt -- one more to kill letter)\n"));
   }

   /* We're done with -M or -m */
   if(options & OPT_Mm_FLAG)
      goto jout;
   /* No tilde escapes, interrupts not expected.  Simply copy STDIN */
   if (!(options & (OPT_INTERACTIVE | OPT_t_FLAG | OPT_TILDE_FLAG))) {
      linebuf = srealloc(linebuf, linesize = LINESIZE);
      while ((i = fread(linebuf, sizeof *linebuf, linesize, stdin)) > 0) {
         if (i != fwrite(linebuf, sizeof *linebuf, i, _coll_fp))
            goto jerr;
      }
      goto jout;
   }

   /* The interactive collect loop.
    * All commands which come here are forbidden when sourcing! */
   assert(_coll_hadintr || !(pstate & PS_SOURCING));
   for(;;){
      /* C99 */{
         enum n_lexinput_flags lif;

         lif = n_LEXINPUT_CTX_COMPOSE;
         if(options & (OPT_INTERACTIVE | OPT_TILDE_FLAG)){
            if(!(options & OPT_t_FLAG))
               lif |= n_LEXINPUT_NL_ESC;
         }
         cnt = n_lex_input(lif, "", &linebuf, &linesize, NULL);
      }

      if (cnt < 0) {
         assert(!(pstate & PS_SOURCING));
         if (options & OPT_t_FLAG) {
            fflush_rewind(_coll_fp);
            /* It is important to set PS_t_FLAG before extract_header() *and*
             * keep OPT_t_FLAG for the first parse of the message, too! */
            pstate |= PS_t_FLAG;
            if (makeheader(_coll_fp, hp, checkaddr_err) != OKAY)
               goto jerr;
            options &= ~OPT_t_FLAG;
            continue;
         } else if ((options & OPT_INTERACTIVE) && ok_blook(ignoreeof)) {
            printf(_("*ignoreeof* set, use `~.' to terminate letter\n"));
            continue;
         }
         break;
      }

      _coll_hadintr = 0;

      cp = linebuf;
      if(cnt == 0)
         goto jputnl;
      else if(!(options & (OPT_INTERACTIVE | OPT_TILDE_FLAG)))
         goto jputline;
      else if(cp[0] == '.'){
         if(cnt == 1 && (ok_blook(dot) || ok_blook(ignoreeof)))
            break;
      }
      if(cp[0] != escape){
jputline:
         if(fwrite(cp, sizeof *cp, cnt, _coll_fp) != (size_t)cnt)
            goto jerr;
         /* TODO PS_READLINE_NL is a terrible hack to ensure that _in_all_-
          * TODO _code_paths_ a file without trailing newline isn't modified
          * TODO to continue one; the "saw-newline" needs to be part of an
          * TODO I/O input machinery object */
jputnl:
         if(pstate & PS_READLINE_NL){
            if(putc('\n', _coll_fp) == EOF)
               goto jerr;
         }
         continue;
      }

      /* Cleanup the input string: like this we can perform a little bit of
       * usage testing and also have somewhat normalized history entries */
      for(cp = &linebuf[2]; (c = *cp) != '\0' && blankspacechar(c); ++cp)
         continue;
      if(c == '\0'){
         linebuf[2] = '\0';
         cnt = 2;
      }else{
         i = PTR2SIZE(cp - linebuf) - 3;
         memcpy(&linebuf[3], cp, (cnt -= i));
         linebuf[2] = ' ';
         linebuf[cnt] = '\0';

         while(*cp != '\0') /* TODO trailing WS from lex_input */
            ++cp;
         for(;;){
            c = cp[-1];
            if(!blankspacechar(c))
               break;
         }
         ((char*)UNCONST(cp))[0] = '\0';
         cnt = PTR2SIZE(cp - linebuf);
      }

      switch((c = linebuf[1])){
      default:
         /* On double escape, send a single one.  Otherwise, it's an error */
         if(c == escape){
            cp = &linebuf[1];
            --cnt;
            goto jputline;
         }
         n_err(_("Unknown tilde escape: ~%c\n"), asciichar(c) ? c : '?');
         continue;
jearg:
         n_err(_("Invalid tilde escape usage: %s\n"), linebuf);
         continue;
      case '!':
         /* Shell escape, send the balance of line to sh -c */
         if(cnt == 2)
            goto jearg;
         c_shell(&linebuf[3]);
         goto jhistcont;
      case ':':
         /* FALLTHRU */
      case '_':
         /* Escape to command mode, but be nice! *//* TODO command expansion
          * TODO should be handled here so that we have unique history! */
         if(cnt == 2)
            goto jearg;
         _execute_command(hp, &linebuf[3], cnt -= 3);
         break;
      case '.':
         /* Simulate end of file on input */
         if(cnt != 2)
            goto jearg;
         goto jout;
      case 'x':
         /* Same as 'q', but no *DEAD* saving */
         /* FALLTHRU */
      case 'q':
         /* Force a quit, act like an interrupt had happened */
         if(cnt != 2)
            goto jearg;
         ++_coll_hadintr;
         _collint((c == 'x') ? 0 : SIGINT);
         exit(EXIT_ERR);
         /*NOTREACHED*/
      case 'h':
         /* Grab a bunch of headers */
         if(cnt != 2)
            goto jearg;
         do
            grab_headers(n_LEXINPUT_CTX_COMPOSE, hp,
              (GTO | GSUBJECT | GCC | GBCC),
              (ok_blook(bsdcompat) && ok_blook(bsdorder)));
         while(hp->h_to == NULL);
         break;
      case 'H':
         /* Grab extra headers */
         if(cnt != 2)
            goto jearg;
         do
            grab_headers(n_LEXINPUT_CTX_COMPOSE, hp, GEXTRA, 0);
         while(check_from_and_sender(hp->h_from, hp->h_sender) == NULL);
         break;
      case 't':
         /* Add to the To: list */
         if(cnt == 2)
            goto jearg;
         hp->h_to = cat(hp->h_to,
               checkaddrs(lextract(&linebuf[3], GTO | GFULL), EACM_NORMAL,
                  NULL));
         break;
      case 's':
         /* Set the Subject list */
         if(cnt == 2)
            goto jearg;
         hp->h_subject = savestr(&linebuf[3]);
         break;
#ifdef HAVE_MEMORY_DEBUG
      case 'S':
         if(cnt != 2)
            goto jearg;
         c_sstats(NULL);
         break;
#endif
      case '@':
         /* Edit the attachment list */
         if(cnt != 2)
            append_attachments(n_LEXINPUT_CTX_COMPOSE, &hp->h_attach,
               &linebuf[3]);
         else
            edit_attachments(n_LEXINPUT_CTX_COMPOSE, &hp->h_attach);
         break;
      case 'c':
         /* Add to the CC list */
         if(cnt == 2)
            goto jearg;
         hp->h_cc = cat(hp->h_cc,
               checkaddrs(lextract(&linebuf[3], GCC | GFULL), EACM_NORMAL,
               NULL));
         break;
      case 'b':
         /* Add stuff to blind carbon copies list */
         if(cnt == 2)
            goto jearg;
         hp->h_bcc = cat(hp->h_bcc,
               checkaddrs(lextract(&linebuf[3], GBCC | GFULL), EACM_NORMAL,
                  NULL));
         break;
      case 'd':
         if(cnt != 2)
            goto jearg;
         cp = n_getdeadletter();
         if(0){
            /*FALLTHRU*/
      case 'R':
      case 'r':
      case '<':
            /* Invoke a file: Search for the file name, then open it and copy
             * the contents to _coll_fp */
            if(cnt == 2){
               n_err(_("Interpolate what file?\n"));
               break;
            }
            if(*(cp = &linebuf[3]) == '!'){
               insertcommand(_coll_fp, ++cp);
               goto jhistcont;
            }
            if((cp = file_expand(cp)) == NULL)
               break;
         }
         if(is_dir(cp)){
            n_err(_("%s: is a directory\n"), n_shell_quote_cp(cp, FAL0));
            break;
         }
         if(_include_file(cp, &lc, &cc, FAL0, (c == 'R')) != 0){
            if(ferror(_coll_fp))
               goto jerr;
            break;
         }
         printf(_("%s %d/%d\n"), n_shell_quote_cp(cp, FAL0), lc, cc);
         break;
      case 'i':
         /* Insert a variable into the file */
         if(cnt == 2)
            goto jearg;
         if((cp = vok_vlook(&linebuf[3])) == NULL || *cp == '\0')
            break;
         if(putesc(cp, _coll_fp) < 0) /* TODO v15: user resp upon `set' time */
            goto jerr;
         if((options & OPT_INTERACTIVE) && putesc(cp, stdout) < 0)
            goto jerr;
         break;
      case 'a':
      case 'A':
         /* Insert the contents of a signature variable */
         if(cnt != 2)
            goto jearg;
         cp = (c == 'a') ? ok_vlook(sign) : ok_vlook(Sign);
         if(cp != NULL && *cp != '\0'){
            if(putesc(cp, _coll_fp) < 0) /* TODO v15: user upon `set' time */
               goto jerr;
            if((options & OPT_INTERACTIVE) && putesc(cp, stdout) < 0)
               goto jerr;
         }
         break;
      case 'w':
         /* Write the message on a file */
         if(cnt == 2)
            goto jearg;
         if((cp = file_expand(&linebuf[3])) == NULL){
            n_err(_("Write what file!?\n"));
            break;
         }
         rewind(_coll_fp);
         if(exwrite(cp, _coll_fp, 1) < 0)
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
         if(cnt == 2)
            goto jearg;
         if(forward(&linebuf[3], _coll_fp, c) < 0)
            break;
         break;
      case 'p':
         /* Print current state of the message without altering anything */
         if(cnt != 2)
            goto jearg;
         print_collf(_coll_fp, hp);
         break;
      case '|':
         /* Pipe message through command. Collect output as new message */
         if(cnt == 2)
            goto jearg;
         rewind(_coll_fp);
         mespipe(&linebuf[3]);
         goto jhistcont;
      case 'v':
      case 'e':
         /* Edit the current message.  'e' -> use EDITOR, 'v' -> use VISUAL */
         if(cnt != 2)
            goto jearg;
         rewind(_coll_fp);
         mesedit(c, ok_blook(editheaders) ? hp : NULL);
         goto jhistcont;
      case '?':
         /* Last the lengthy help string.  (Very ugly, but take care for
          * compiler supported string lengths :() */
         puts(_(
"TILDE ESCAPES (to be placed after a newline) excerpt:\n"
"~.            Commit and send message\n"
"~: <command>  Execute a mail command\n"
"~<! <command> Insert output of command\n"
"~@ [<files>]  Edit attachment list\n"
"~A            Insert *Sign* variable (`~a' inserts *sign*)\n"
"~c <users>    Add users to Cc: list (`~b' for Bcc:)\n"
"~d            Read in *DEAD* (dead.letter)\n"
"~e            Edit message via *EDITOR*"
         ));
         puts(_(
"~F <msglist>  Read in with headers, don't *indentprefix* lines\n"
"~f <msglist>  Like ~F, but honour `ignore' / `retain' configuration\n"
"~H            Edit From:, Reply-To: and Sender:\n"
"~h            Prompt for Subject:, To:, Cc: and blind Bcc:\n"
"~i <variable> Insert a value and a newline\n"
"~M <msglist>  Read in with headers, *indentprefix* (`~m': `retain' etc.)\n"
"~p            Print current message compose buffer\n"
"~r <file>     Read in a file (`~R' *indentprefix* lines)"
         ));
         puts(_(
"~s <subject>  Set Subject:\n"
"~t <users>    Add users to To: list\n"
"~u <msglist>  Read in message(s) without headers (`~U' indents lines)\n"
"~v            Edit message via *VISUAL*\n"
"~w <file>     Write message onto file\n"
"~x            Abort composition, discard message (`~q' saves in *DEAD*)\n"
"~| <command>  Pipe message through shell filter"
         ));
         if(cnt != 2)
            goto jearg;
         break;
      }

      /* Finally place an entry in history as applicable */
      if(0){
jhistcont:
         c = '\1';
      }else
         c = '\0';
      if(!(options & OPT_t_FLAG))
         n_tty_addhist(linebuf, TRU1);
      if(c != '\0')
         goto jcont;
   }

jout:
   /* Execute compose-leave TODO completely v15-compat intermediate!! */
   if((cp = ok_vlook(on_compose_leave)) != NULL){
      setup_from_and_sender(hp);
      call_compose_mode_hook(cp, &a_coll__hook_setter, hp);
   }

   /* Final change to edit headers, if not already above */
   if (ok_blook(bsdcompat) || ok_blook(askatend)) {
      if (hp->h_cc == NULL && ok_blook(askcc))
         grab_headers(n_LEXINPUT_CTX_COMPOSE, hp, GCC, 1);
      if (hp->h_bcc == NULL && ok_blook(askbcc))
         grab_headers(n_LEXINPUT_CTX_COMPOSE, hp, GBCC, 1);
   }
   if (hp->h_attach == NULL && ok_blook(askattach))
      edit_attachments(n_LEXINPUT_CTX_COMPOSE, &hp->h_attach);

   /* Add automatic receivers */
   if ((cp = ok_vlook(autocc)) != NULL && *cp != '\0')
      hp->h_cc = cat(hp->h_cc, checkaddrs(lextract(cp, GCC | GFULL),
            EACM_NORMAL, checkaddr_err));
   if ((cp = ok_vlook(autobcc)) != NULL && *cp != '\0')
      hp->h_bcc = cat(hp->h_bcc, checkaddrs(lextract(cp, GBCC | GFULL),
            EACM_NORMAL, checkaddr_err));
   if (*checkaddr_err != 0)
      goto jerr;

   if(options & OPT_Mm_FLAG)
      goto jskiptails;

   /* Place signature? */
   if((cp = ok_vlook(signature)) != NULL && *cp != '\0'){
      char const *cpq;

      if((cpq = file_expand(cp)) == NULL){
         n_err(_("*signature* expands to invalid file: %s\n"),
            n_shell_quote_cp(cp, FAL0));
         goto jerr;
      }
      cpq = n_shell_quote_cp(cp = cpq, FAL0);

      if((sigfp = Fopen(cp, "r")) == NULL){
         n_err(_("Can't open *signature* %s: %s\n"), cpq, strerror(errno));
         goto jerr;
      }

      if(linebuf == NULL)
         linebuf = smalloc(linesize = LINESIZE);
      c = '\0';
      while((i = fread(linebuf, sizeof *linebuf, linesize, UNVOLATILE(sigfp)))
            > 0){
         c = linebuf[i - 1];
         if(i != fwrite(linebuf, sizeof *linebuf, i, _coll_fp))
            goto jerr;
      }

      /* C99 */{
         FILE *x = UNVOLATILE(sigfp);
         int e = errno, ise = ferror(x);

         sigfp = NULL;
         Fclose(x);

         if(ise){
            n_err(_("Errors while reading *signature* %s: %s\n"),
               cpq, strerror(e));
            goto jerr;
         }
      }

      if(c != '\0' && c != '\n')
         putc('\n', _coll_fp);
   }

   {  char const *cp_obsolete = ok_vlook(NAIL_TAIL);

      if(cp_obsolete != NULL)
         OBSOLETE(_("please use *message-inject-tail* instead of *NAIL_TAIL*"));

   if((cp = ok_vlook(message_inject_tail)) != NULL ||
         (cp = cp_obsolete) != NULL){
      if(putesc(cp, _coll_fp) < 0)
         goto jerr;
      if((options & OPT_INTERACTIVE) && putesc(cp, stdout) < 0)
         goto jerr;
   }
   }

jskiptails:
   if(fflush(_coll_fp))
      goto jerr;
   rewind(_coll_fp);

jleave:
   if (linebuf != NULL)
      free(linebuf);
   --noreset;
   sigfillset(&nset);
   sigprocmask(SIG_BLOCK, &nset, NULL);
   pstate &= ~PS_RECURSED;
   safe_signal(SIGINT, _coll_saveint);
   safe_signal(SIGHUP, _coll_savehup);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD_LEAVE;
   return _coll_fp;

jerr:
   if(sigfp != NULL)
      Fclose(UNVOLATILE(sigfp));
   if (_coll_fp != NULL) {
      Fclose(_coll_fp);
      _coll_fp = NULL;
   }
   goto jleave;
}

/* s-it-mode */
