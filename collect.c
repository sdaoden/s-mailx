/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Collect input from standard input, handling ~ escapes.
 *@ TODO This needs a complete rewrite, with carriers, etc.
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
#define n_FILE collect

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct a_coll_ocds_arg{
   sighandler_type coa_opipe;
   sighandler_type coa_oint;
   FILE *coa_stdin;  /* The descriptor (pipe(2)+Fdopen()) we read from */
   FILE *coa_stdout; /* The Popen()ed pipe through which we write to the hook */
   int coa_pipe[2];  /* ..backing .coa_stdin */
   si8_t *coa_senderr; /* Set to 1 on failure */
   char coa_cmd[n_VFIELD_SIZE(0)];
};

/* The following hookiness with global variables is so that on receipt of an
 * interrupt signal, the partial message can be salted away on *DEAD* */

static sighandler_type  _coll_saveint;    /* Previous SIGINT value */
static sighandler_type  _coll_savehup;    /* Previous SIGHUP value */
static FILE             *_coll_fp;        /* File for saving away */
static int volatile     _coll_hadintr;    /* Have seen one SIGINT so far */
static sigjmp_buf       _coll_jmp;        /* To get back to work */
static sigjmp_buf       _coll_abort;      /* To end collection with error */
static char const *a_coll_ocds__macname;  /* *on-compose-done* */

/* Handle `~:', `~_' and some hooks; hp may be NULL */
static void       _execute_command(struct header *hp, char const *linebuf,
                     size_t linesize);

/* */
static int        _include_file(char const *name, int *linecount,
                     int *charcount, bool_t indent);

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
 * indent stuff.  The flag argument is one of the command escapes: [mMfFuU].
 * Return a count of the number of characters now in the message, or -1 if an
 * error is encountered writing the message temporary */
static int        forward(char *ms, FILE *fp, int f);

/* ~^ mode */
static bool_t a_collect_plumbing(char const *ms, struct header *p);

/* On interrupt, come here to save the partial message in ~/dead.letter.
 * Then jump out of the collection loop */
static void       _collint(int s);

static void       collhup(int s);

static int        putesc(char const *s, FILE *stream); /* TODO wysh set! */

/* temporary_compose_mode_hook_call() setter hook */
static void a_coll__hook_setter(void *arg);

/* *on-compose-done* driver and *on-compose-done(-shell)?* finalizer */
static int a_coll_ocds__mac(void);
static void a_coll_ocds__finalize(void *vp);

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

   n_UNUSED(linesize);
   mnbuf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      goto jleave;
   }

   /* If the above todo is worked, remove or outsource to attachment.c! */
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
_include_file(char const *name, int *linecount, int *charcount, bool_t indent)
{
   FILE *fbuf;
   char const *indb;
   int ret = -1;
   char *linebuf = NULL; /* TODO line pool */
   size_t linesize = 0, indl, linelen, cnt;
   NYD_ENTER;

   /* The -M case is special */
   if (name == (char*)-1)
      fbuf = n_stdin;
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
   }
   if (fflush(_coll_fp))
      goto jleave;

   ret = 0;
jleave:
   if (linebuf != NULL)
      free(linebuf);
   if (fbuf != NULL && fbuf != n_stdin)
      Fclose(fbuf);
   NYD_LEAVE;
   return ret;
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
   char *lbuf;
   FILE *obuf;
   size_t cnt, linesize, linelen;
   NYD_ENTER;

   fflush_rewind(cf);
   cnt = (size_t)fsize(cf);

   if((obuf = Ftmp(NULL, "collfp", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("Can't create temporary file for `~p' command"), 0);
      goto jleave;
   }

   hold_all_sigs();

   fprintf(obuf, _("-------\nMessage contains:\n"));
   puthead(TRU1, hp, obuf,
      (GIDENT | GTO | GSUBJECT | GCC | GBCC | GNL | GFILES | GCOMMA),
      SEND_TODISP, CONV_NONE, NULL, NULL);

   lbuf = NULL;
   linesize = 0;
   while(fgetline(&lbuf, &linesize, &cnt, &linelen, cf, 1))
      prout(lbuf, linelen, obuf);
   if(lbuf != NULL)
      free(lbuf);

   if(hp->h_attach != NULL){
      fputs(_("-------\nAttachments:\n"), obuf);
      n_attachment_list_print(hp->h_attach, obuf);
   }

   rele_all_sigs();

   page_or_print(obuf, 0);
jleave:
   Fclose(obuf);
   NYD_LEAVE;
}

static int
exwrite(char const *name, FILE *fp, int f)
{
   FILE *of;
   int c, rv;
   long lc, cc;
   NYD_ENTER;

   if (f) {
      fprintf(n_stdout, "%s ", n_shexp_quote_cp(name, FAL0));
      fflush(n_stdout);
   }

   if ((of = Fopen(name, "a")) == NULL) {
      n_perr(name, 0);
      goto jerr;
   }

   lc = cc = 0;
   while ((c = getc(fp)) != EOF) {
      ++cc;
      if (c == '\n')
         ++lc;
      if (putc(c, of) == EOF) {
         n_perr(name, 0);
         goto jerr;
      }
   }
   fprintf(n_stdout, _("%ld/%ld\n"), lc, cc);

   rv = 0;
jleave:
   if(of != NULL)
      Fclose(of);
   fflush(n_stdout);
   NYD_LEAVE;
   return rv;
jerr:
   putc('-', n_stdout);
   putc('\n', n_stdout);
   rv = -1;
   goto jleave;
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
   if (checkaddr_err != NULL && *checkaddr_err != 0)
      goto jleave;

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

   if(!(saved = ok_blook(add_file_recipients)))
      ok_bset(add_file_recipients);

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

   if(!saved)
      ok_bclear(add_file_recipients);
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
      n_err(_("No bytes from %s !?\n"), n_shexp_quote_cp(cmd, FAL0));
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
   struct n_ignore const *itp;
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
      itp = n_IGNORE_ALL;
   else
      itp = upperchar(f) ? NULL : n_IGNORE_TYPE;
   action = (upperchar(f) && f != 'U') ? SEND_QUOTE_ALL : SEND_QUOTE;

   fprintf(n_stdout, _("Interpolating:"));
   srelax_hold();
   for (; *msgvec != 0; ++msgvec) {
      struct message *mp = message + *msgvec - 1;

      touch(mp);
      fprintf(n_stdout, " %d", *msgvec);
      fflush(n_stdout);
      if (sendmp(mp, fp, itp, tabst, action, NULL) < 0) {
         n_perr(_("temporary mail file"), 0);
         rv = -1;
         break;
      }
      srelax();
   }
   srelax_rele();
   fprintf(n_stdout, "\n");
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
a_collect_plumbing(char const *ms, struct header *hp){
   /* TODO _collect_plumbing: instead of fields the basic headers should
    * TODO be in an array and have IDs, like in termcap etc., so then this
    * TODO could be simplified as table-walks.  Also true for arg-checks! */
   char const *cp, *cmd[4];
   NYD2_ENTER;

   /* Protcol version for *on-compose-done-shell* -- update manual on change! */
#define a_COLL_PLUMBING_VERSION "0 0 1"
   cp = ms;

   /* C99 */{
      size_t i;

      for(i = 0; i < n_NELEM(cmd); ++i){
         while(blankchar(*cp))
            ++cp;
         if(*cp == '\0')
            cmd[i] = NULL;
         else{
            if(i < n_NELEM(cmd) - 1)
               for(cmd[i] = cp++; *cp != '\0' && !blankchar(*cp); ++cp)
                  ;
            else{
               /* Last slot takes all the rest of the line, less trailing WS */
               for(cmd[i] = cp++; *cp != '\0'; ++cp)
                  ;
               while(blankchar(cp[-1]))
                  --cp;
            }
            cmd[i] = savestrbuf(cmd[i], PTR2SIZE(cp - cmd[i]));
         }
      }
   }

   if(n_UNLIKELY(cmd[0] == NULL))
      goto jecmd;

   if(is_asccaseprefix(cmd[0], "header")){
      struct n_header_field *hfp;
      struct name *np, **npp;

      if(cmd[1] == NULL || is_asccaseprefix(cmd[1], "list")){
         if(cmd[2] == NULL){
            fputs("210", n_stdout);
            if(hp->h_from != NULL) fputs(" From", n_stdout);
            if(hp->h_sender != NULL) fputs(" Sender", n_stdout);
            if(hp->h_to != NULL) fputs(" To", n_stdout);
            if(hp->h_cc != NULL) fputs(" Cc", n_stdout);
            if(hp->h_bcc != NULL) fputs(" Bcc", n_stdout);
            if(hp->h_subject != NULL) fputs(" Subject", n_stdout);
            if(hp->h_replyto != NULL) fputs(" Reply-To", n_stdout);
            if(hp->h_mft != NULL) fputs(" Mail-Followup-To", n_stdout);
            if(hp->h_message_id != NULL) fputs(" Message-ID", n_stdout);
            if(hp->h_ref != NULL) fputs(" References", n_stdout);
            if(hp->h_in_reply_to != NULL) fputs(" In-Reply-To", n_stdout);
            for(hfp = hp->h_user_headers; hfp != NULL; hfp = hfp->hf_next){
               putc(' ', n_stdout);
               fputs(&hfp->hf_dat[0], n_stdout);
            }
            putc('\n', n_stdout);
            goto jleave;
         }

         if(cmd[3] != NULL)
            goto jecmd;

         if(!asccasecmp(cmd[2], "from")){
            np = hp->h_from;
jlist:
            fprintf(n_stdout, "%s %s\n", (np == NULL ? "501" : "210"), cp);
            goto jleave;
         }
         if(!asccasecmp(cmd[2], cp = "Sender")){
            np = hp->h_sender;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "To")){
            np = hp->h_to;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "Cc")){
            np = hp->h_cc;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "Bcc")){
            np = hp->h_bcc;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "Subject")){
            np = (struct name*)-1;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "Reply-To")){
            np = hp->h_replyto;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "Mail-Followup-To")){
            np = hp->h_mft;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "Message-ID")){
            np = hp->h_message_id;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "References")){
            np = hp->h_ref;
            goto jlist;
         }
         if(!asccasecmp(cmd[2], cp = "In-Reply-To")){
            np = hp->h_in_reply_to;
            goto jlist;
         }

         /* Primitive name normalization XXX header object should
          * XXX have a more sophisticated accessible one */
         /* C99 */{
            char *xp;

            cp = xp = savestr(cmd[2]);
            xp[0] = upperchar(xp[0]);
            while(*++xp != '\0')
               xp[0] = lowerchar(xp[0]);

            for(hfp = hp->h_user_headers;; hfp = hfp->hf_next){
               if(hfp == NULL)
                  goto j501cp;
               else if(!asccasecmp(cp, &hfp->hf_dat[0])){
                  fprintf(n_stdout, "210 %s\n", cp);
                  break;
               }
            }
         }
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "show")){
         if(cmd[2] == NULL || cmd[3] != NULL)
            goto jecmd;

         if(!asccasecmp(cmd[2], "from")){
            np = hp->h_from;
jshow:
            if(np != NULL){
               fprintf(n_stdout, "211 %s\n", cp);
               do if(!(np->n_type & GDEL))
                  fprintf(n_stdout, "%s %s\n", np->n_name, np->n_fullname);
               while((np = np->n_flink) != NULL);
               putc('\n', n_stdout);
               goto jleave;
            }else
               goto j501cp;
         }
         if(!asccasecmp(cmd[2], cp = "Sender")){
            np = hp->h_sender;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "To")){
            np = hp->h_to;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "Cc")){
            np = hp->h_cc;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "Bcc")){
            np = hp->h_bcc;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "Reply-To")){
            np = hp->h_replyto;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "Mail-Followup-To")){
            np = hp->h_mft;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "Message-ID")){
            np = hp->h_message_id;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "References")){
            np = hp->h_ref;
            goto jshow;
         }
         if(!asccasecmp(cmd[2], cp = "In-Reply-To")){
            np = hp->h_in_reply_to;
            goto jshow;
         }

         if(!asccasecmp(cmd[2], cp = "Subject")){
            if(hp->h_subject != NULL)
               fprintf(n_stdout, "212 %s\n%s\n\n", cp, hp->h_subject);
            else
               fprintf(n_stdout, "501 %s\n", cp);
            goto jleave;
         }

         /* Primitive name normalization XXX header object should
          * XXX have a more sophisticated accessible one */
         /* C99 */{
            bool_t any;
            char *xp;

            cp = xp = savestr(cmd[2]);
            xp[0] = upperchar(xp[0]);
            while(*++xp != '\0')
               xp[0] = lowerchar(xp[0]);

            for(any = FAL0, hfp = hp->h_user_headers; hfp != NULL;
                  hfp = hfp->hf_next){
               if(!asccasecmp(cp, &hfp->hf_dat[0])){
                  if(!any)
                     fprintf(n_stdout, "212 %s\n", cp);
                  any = TRU1;
                  fprintf(n_stdout, "%s\n", &hfp->hf_dat[hfp->hf_nl +1]);
               }
            }
            if(any)
               putc('\n', n_stdout);
            else
               goto j501cp;
         }
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "remove")){
         if(cmd[2] == NULL || cmd[3] != NULL)
            goto jecmd;

         if(!asccasecmp(cmd[2], "from")){
            npp = &hp->h_from;
jrem:
            if(*npp != NULL){
               *npp = NULL;
               fprintf(n_stdout, "210 %s\n", cp);
               goto jleave;
            }else
               goto j501cp;
         }
         if(!asccasecmp(cmd[2], cp = "Sender")){
            npp = &hp->h_sender;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "To")){
            npp = &hp->h_to;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "Cc")){
            npp = &hp->h_cc;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "Bcc")){
            npp = &hp->h_bcc;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "Reply-To")){
            npp = &hp->h_replyto;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "Mail-Followup-To")){
            npp = &hp->h_mft;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "Message-ID")){
            npp = &hp->h_message_id;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "References")){
            npp = &hp->h_ref;
            goto jrem;
         }
         if(!asccasecmp(cmd[2], cp = "In-Reply-To")){
            npp = &hp->h_in_reply_to;
            goto jrem;
         }

         if(!asccasecmp(cmd[2], cp = "Subject")){
            if(hp->h_subject != NULL){
               hp->h_subject = NULL;
               fprintf(n_stdout, "210 %s\n", cp);
               goto jleave;
            }else
               goto j501cp;
         }

         /* Primitive name normalization XXX header object should
          * XXX have a more sophisticated accessible one */
         /* C99 */{
            struct n_header_field **hfpp;
            bool_t any;
            char *xp;

            cp = xp = savestr(cmd[2]);
            xp[0] = upperchar(xp[0]);
            while(*++xp != '\0')
               xp[0] = lowerchar(xp[0]);

            for(any = FAL0, hfpp = &hp->h_user_headers; (hfp = *hfpp) != NULL;){
               if(!asccasecmp(cp, &hfp->hf_dat[0])){
                  *hfpp = hfp->hf_next;
                  if(!any)
                     fprintf(n_stdout, "210 %s\n", cp);
                  any = TRU1;
               }else
                  hfp = *(hfpp = &hfp->hf_next);
            }
            if(!any)
               goto j501cp;
         }
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "insert")){ /* TODO LOGIC BELONGS head.c
          * TODO That is: Header::factory(string) -> object (blahblah).
          * TODO I.e., as long as we don't have regular RFC compliant parsers
          * TODO which differentiate in between structured and unstructured
          * TODO header fields etc., a little workaround */
         si8_t aerr;
         enum expand_addr_check_mode eacm;
         enum gfield ntype;
         bool_t mult_ok;

         if(cmd[2] == NULL || cmd[3] == NULL)
            goto jecmd;

         /* Strip [\r\n] which would render a body invalid XXX all controls? */
         /* C99 */{
            char *xp, c;

            cmd[3] = xp = savestr(cmd[3]);
            for(; (c = *xp) != '\0'; ++xp)
               if(c == '\n' || c == '\r')
                  *xp = ' ';
         }

         mult_ok = TRU1;
         ntype = GEXTRA | GFULL | GFULLEXTRA;
         eacm = EACM_STRICT;

         if(!asccasecmp(cmd[2], "from")){
            npp = &hp->h_from;
jins:
            aerr = 0;
            if((np = lextract(cmd[3], ntype)) == NULL)
               goto j501cp;

            if((np = checkaddrs(np, eacm, &aerr), aerr != 0)){
               fprintf(n_stdout, "505 %s\n", cp);
               goto jleave;
            }

            if(!mult_ok){
               struct name *xnp;

               for(xnp = *npp; xnp != NULL && !(xnp->n_type & GDEL);
                     xnp = xnp->n_flink)
                  ;
               if(xnp != NULL || np->n_flink != NULL){
                  fprintf(n_stdout, "506 %s\n", cp);
                  goto jleave;
               }
            }

            *npp = cat(*npp, np);
            fprintf(n_stdout, "210 %s\n", cp);
            goto jleave;
         }
         if(!asccasecmp(cmd[2], cp = "Sender")){
            mult_ok = FAL0;
            npp = &hp->h_sender;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "To")){
            npp = &hp->h_to;
            ntype = GTO | GFULL;
            eacm = EACM_NORMAL | EAF_NAME;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "Cc")){
            npp = &hp->h_cc;
            ntype = GCC | GFULL;
            eacm = EACM_NORMAL | EAF_NAME;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "Bcc")){
            npp = &hp->h_bcc;
            ntype = GBCC | GFULL;
            eacm = EACM_NORMAL | EAF_NAME;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "Reply-To")){
            npp = &hp->h_replyto;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "Mail-Followup-To")){
            npp = &hp->h_mft;
            eacm = EACM_NONAME;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "Message-ID")){
            mult_ok = FAL0;
            npp = &hp->h_message_id;
            eacm = EACM_NONAME;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "References")){
            npp = &hp->h_ref;
            ntype = GREF;
            eacm = EACM_NONAME;
            goto jins;
         }
         if(!asccasecmp(cmd[2], cp = "In-Reply-To")){
            npp = &hp->h_in_reply_to;
            ntype = GREF;
            eacm = EACM_NONAME;
            goto jins;
         }

         if(!asccasecmp(cmd[2], cp = "Subject")){
            if(cmd[3][0] != '\0'){
               if(hp->h_subject != NULL)
                  hp->h_subject = savecatsep(hp->h_subject, ' ', cmd[3]);
               else
                  hp->h_subject = n_UNCONST(cmd[3]);
               fprintf(n_stdout, "210 %s\n", cp);
               goto jleave;
            }else
               goto j501cp;
         }

         /* Primitive name normalization XXX header object should
          * XXX have a more sophisticated accessible one */
         /* C99 */{
            size_t nl, bl;
            struct n_header_field **hfpp;

            for(cp = cmd[2]; *cp != '\0'; ++cp)
               if(!fieldnamechar(*cp)){
                  cp = cmd[2];
                  goto j501cp;
               }

            for(hfpp = &hp->h_user_headers; *hfpp != NULL;)
               hfpp = &(*hfpp)->hf_next;

            nl = strlen(cp = cmd[2]);
            bl = strlen(cmd[3]) +1;
            *hfpp = hfp = salloc(n_VSTRUCT_SIZEOF(struct n_header_field, hf_dat
                  ) + nl +1 + bl);
            hfp->hf_next = NULL;
            hfp->hf_nl = nl;
            hfp->hf_bl = bl - 1;
            memcpy(hfp->hf_dat, cp, nl);
               hfp->hf_dat[nl++] = '\0';
               memcpy(hfp->hf_dat + nl, cmd[3], bl);
            fprintf(n_stdout, "210 %s\n", cp);
         }
         goto jleave;
      }

      goto jecmd;
   }

   if(is_asccaseprefix(cmd[0], "attachment")){
      bool_t status;
      struct attachment *ap;

      if(cmd[1] == NULL || is_asccaseprefix(cmd[1], "list")){
         if(cmd[2] != NULL)
            goto jecmd;

         if((ap = hp->h_attach) != NULL){
            fputs("212\n", n_stdout);
            do
               fprintf(n_stdout, "%s\n", ap->a_path_user);
            while((ap = ap->a_flink) != NULL);
            putc('\n', n_stdout);
         }else
            fputs("501\n", n_stdout);
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "remove")){
         if(cmd[2] == NULL || cmd[3] != NULL)
            goto jecmd;

         if((ap = n_attachment_find(hp->h_attach, cmd[2], &status)) != NULL){
            if(status == TRUM1)
               fputs("506\n", n_stdout);
            else{
               hp->h_attach = n_attachment_remove(hp->h_attach, ap);
               fprintf(n_stdout, "210 %s\n", cmd[2]);
            }
         }else
            fputs("501\n", n_stdout);
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "remove-at")){
         uiz_t i;

         if(cmd[2] == NULL || cmd[3] != NULL)
            goto jecmd;

         if((n_idec_uiz_cp(&i, cmd[2], 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED || i == 0)
            fputs("505\n", n_stdout);
         else{
            for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
               ;
            if(ap != NULL){
               hp->h_attach = n_attachment_remove(hp->h_attach, ap);
               fprintf(n_stdout, "210 %s\n", cmd[2]);
            }else
               fputs("501\n", n_stdout);
         }
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "insert")){
         enum n_attach_error aerr;

         if(cmd[2] == NULL || cmd[3] != NULL)
            goto jecmd;

         hp->h_attach = n_attachment_append(hp->h_attach, cmd[2], &aerr, &ap);
         switch(aerr){
         case n_ATTACH_ERR_FILE_OPEN: cp = "505\n"; goto jatt_ins;
         case n_ATTACH_ERR_ICONV_FAILED: cp = "506\n"; goto jatt_ins;
         case n_ATTACH_ERR_ICONV_NAVAIL:
         case n_ATTACH_ERR_OTHER:
         default:
            cp = "501\n";
jatt_ins:
            fputs(cp, n_stdout);
            break;
         case n_ATTACH_ERR_NONE:{
            size_t i;

            for(i = 0; ap != NULL; ++i, ap = ap->a_blink)
               ;
            fprintf(n_stdout, "210 %" PRIuZ "\n", i);
         }  break;
         }
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "attribute")){
         if(cmd[2] == NULL || cmd[3] != NULL)
            goto jecmd;

         if((ap = n_attachment_find(hp->h_attach, cmd[2], NULL)) != NULL){
jatt_att:
            fprintf(n_stdout, "212 %s\n", cmd[2]);
            if(ap->a_msgno > 0)
               fprintf(n_stdout, "message-number %d\n\n", ap->a_msgno);
            else{
               fprintf(n_stdout,
                  "creation-name %s\nopen-path %s\nfilename %s\n",
                  ap->a_path_user, ap->a_path, ap->a_name);
               if(ap->a_content_description != NULL)
                  fprintf(n_stdout, "content-description %s\n",
                     ap->a_content_description);
               if(ap->a_content_id != NULL)
                  fprintf(n_stdout, "content-id %s\n",
                     ap->a_content_id->n_name);
               if(ap->a_content_type != NULL)
                  fprintf(n_stdout, "content-type %s\n", ap->a_content_type);
               if(ap->a_content_disposition != NULL)
                  fprintf(n_stdout, "content-disposition %s\n",
                     ap->a_content_disposition);
               putc('\n', n_stdout);
            }
         }else
            fputs("501\n", n_stdout);
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "attribute-at")){
         uiz_t i;

         if(cmd[2] == NULL || cmd[3] != NULL)
            goto jecmd;

         if((n_idec_uiz_cp(&i, cmd[2], 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED || i == 0)
            fputs("505\n", n_stdout);
         else{
            for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
               ;
            if(ap != NULL)
               goto jatt_att;
            else
               fputs("501\n", n_stdout);
         }
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "attribute-set")){
         if(cmd[2] == NULL || cmd[3] == NULL)
            goto jecmd;

         if((ap = n_attachment_find(hp->h_attach, cmd[2], NULL)) != NULL){
jatt_attset:
            if(ap->a_msgno > 0)
               fputs("505\n", n_stdout);
            else{
               char c, *keyw;

               cp = cmd[3];
               while((c = *cp) != '\0' && !blankchar(c))
                  ++cp;
               keyw = savestrbuf(cmd[3], PTR2SIZE(cp - cmd[3]));
               if(c != '\0'){
                  for(; (c = *++cp) != '\0' && blankchar(c);)
                     ;
                  if(c != '\0'){
                     char *xp;

                     /* Strip [\r\n] which would render a parameter invalid XXX
                      * XXX all controls? */
                     cp = xp = savestr(cp);
                     for(; (c = *xp) != '\0'; ++xp)
                        if(c == '\n' || c == '\r')
                           *xp = ' ';
                     c = *cp;
                  }
               }

               if(!asccasecmp(keyw, "filename"))
                  ap->a_name = (c == '\0') ? ap->a_path_bname : cp;
               else if(!asccasecmp(keyw, "content-description"))
                  ap->a_content_description = (c == '\0') ? NULL : cp;
               else if(!asccasecmp(keyw, "content-id")){
                  ap->a_content_id = NULL;

                  if(c != '\0'){
                     struct name *np;

                     np = checkaddrs(lextract(cp, GREF),
                           /*EACM_STRICT | TODO '/' valid!! */ EACM_NOLOG |
                           EACM_NONAME, NULL);
                     if(np != NULL && np->n_flink == NULL)
                        ap->a_content_id = np;
                     else
                        cp = NULL;
                  }
               }else if(!asccasecmp(keyw, "content-type"))
                  ap->a_content_type = (c == '\0') ? NULL : cp;
               else if(!asccasecmp(keyw, "content-disposition"))
                  ap->a_content_disposition = (c == '\0') ? NULL : cp;
               else
                  cp = NULL;

               if(cp != NULL){
                  size_t i;

                  for(i = 0; ap != NULL; ++i, ap = ap->a_blink)
                     ;
                  fprintf(n_stdout, "210 %" PRIuZ "\n", i);
               }else
                  fputs("505\n", n_stdout);
            }
         }else
            fputs("501\n", n_stdout);
         goto jleave;
      }

      if(is_asccaseprefix(cmd[1], "attribute-set-at")){
         uiz_t i;

         if(cmd[2] == NULL || cmd[3] == NULL)
            goto jecmd;

         if((n_idec_uiz_cp(&i, cmd[2], 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED || i == 0)
            fputs("505\n", n_stdout);
         else{
            for(ap = hp->h_attach; ap != NULL && --i != 0; ap = ap->a_flink)
               ;
            if(ap != NULL)
               goto jatt_attset;
            else
               fputs("501\n", n_stdout);
         }
         goto jleave;
      }

      goto jecmd;
   }

jecmd:
   fputs("500\n", n_stdout);
   ms = NULL;
jleave:
   fflush(n_stdout);
   NYD2_LEAVE;
   return (ms != NULL);

j501cp:
   fputs("501 ", n_stdout);
   fputs(cp, n_stdout);
   putc('\n', n_stdout);
   goto jleave;
}

static void
_collint(int s)
{
   NYD_X; /* Signal handler */

   /* the control flow is subtle, because we can be called from ~q */
   if (_coll_hadintr == 0) {
      if (ok_blook(ignore)) {
         fputs("@\n", n_stdout);
         fflush(n_stdout);
         clearerr(n_stdin);
      } else
         _coll_hadintr = 1;
      siglongjmp(_coll_jmp, 1);
   }
   n_exit_status |= n_EXIT_SEND_ERROR;
   if (s != 0)
      savedeadletter(_coll_fp, TRU1);
   /* Aborting message, no need to fflush() .. */
   siglongjmp(_coll_abort, 1);
}

static void
collhup(int s)
{
   NYD_X; /* Signal handler */
   n_UNUSED(s);

   savedeadletter(_coll_fp, TRU1);
   /* Let's pretend nobody else wants to clean up, a true statement at
    * this time */
   exit(n_EXIT_ERR);
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
      val = n_empty;
   ok_vset(compose_from, val);
   if((val = detract(hp->h_sender, 0)) == NULL)
      val = n_empty;
   ok_vset(compose_sender, val);
   if((val = detract(hp->h_to, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(compose_to, val);
   if((val = detract(hp->h_cc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(compose_cc, val);
   if((val = detract(hp->h_bcc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(compose_bcc, val);
   if((val = hp->h_subject) == NULL)
      val = n_empty;
   ok_vset(compose_subject, val);
   NYD2_LEAVE;
}

static int
a_coll_ocds__mac(void){
   /* Executes in a fork(2)ed child */
   setvbuf(n_stdout, NULL, _IOLBF, 0);
   n_psonce &= ~(n_PSO_INTERACTIVE | n_PSO_TTYIN | n_PSO_TTYOUT);
   n_pstate |= n_PS_COMPOSE_FORKHOOK;
   temporary_compose_mode_hook_call(a_coll_ocds__macname, NULL, NULL);
   _exit(n_EXIT_OK);
   /* NOTREACHED */
   return 0;
}

static void
a_coll_ocds__finalize(void *vp){
   /* Note we use this for destruction upon setup errors, thus */
   sighandler_type opipe;
   sighandler_type oint;
   struct a_coll_ocds_arg **coapp, *coap;
   NYD2_ENTER;

   temporary_compose_mode_hook_call((char*)-1, NULL, NULL);

   coap = *(coapp = vp);
   *coapp = (struct a_coll_ocds_arg*)-1;

   if(coap->coa_stdout != NULL)
      if(!Pclose(coap->coa_stdout, FAL0)){
         *coap->coa_senderr = TRU1;
         n_err(_("*on-compose-done(-shell)?* failed: %s\n"),
            n_shexp_quote_cp(coap->coa_cmd, FAL0));
      }

   if(coap->coa_stdin != NULL)
      Fclose(coap->coa_stdin);
   else if(coap->coa_pipe[0] != -1)
      close(coap->coa_pipe[0]);

   if(coap->coa_pipe[1] != -1)
      close(coap->coa_pipe[1]);

   opipe = coap->coa_opipe;
   oint = coap->coa_oint;

   n_lofi_free(coap);

   hold_all_sigs();
   safe_signal(SIGPIPE, opipe);
   safe_signal(SIGINT, oint);
   rele_all_sigs();
   NYD2_LEAVE;
}

FL FILE *
collect(struct header *hp, int printheaders, struct message *mp,
   char const *quotefile, int doprefix, si8_t *checkaddr_err)
{
   struct n_ignore const *quoteitp;
   struct a_coll_ocds_arg *coap;
   int lc, cc, c;
   int volatile t, eofcnt, getfields;
   char *linebuf, escape_saved, escape;
   char const *cp, *coapm;
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
   eofcnt = 0;
   coapm = NULL;
   coap = NULL;

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
   n_pstate |= n_PS_COMPOSE_MODE;
   sigprocmask(SIG_SETMASK, &oset, (sigset_t*)NULL);

   if ((_coll_fp = Ftmp(NULL, "collect", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL) {
      n_perr(_("temporary mail file"), 0);
      goto jerr;
   }

   /* If we are going to prompt for a subject, refrain from printing a newline
    * after the headers (since some people mind) */
   getfields = 0;
   if (!(n_poption & n_PO_t_FLAG)) {
      t = GTO | GSUBJECT | GCC | GNL;
      if (ok_blook(fullnames))
         t |= GCOMMA;

      if (n_psonce & n_PSO_INTERACTIVE) {
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
      n_UNINIT(t, 0);
   }

   escape_saved = escape = ((cp = ok_vlook(escape)) != NULL) ? *cp : n_ESCAPE;
   _coll_hadintr = 0;

   if (!sigsetjmp(_coll_jmp, 1)) {
      /* Ask for some headers first, as necessary */
      if (getfields)
         grab_headers(n_LEXINPUT_CTX_COMPOSE, hp, getfields, 1);

      /* Execute compose-enter TODO completely v15-compat intermediate!! */
      if((cp = ok_vlook(on_compose_enter)) != NULL){
         setup_from_and_sender(hp);
         temporary_compose_mode_hook_call(cp, &a_coll__hook_setter, hp);
      }

      /* Cannot do since it may require turning this into a multipart one */
      if(!(n_poption & n_PO_Mm_FLAG)){
         char const *cp_obsolete = ok_vlook(NAIL_HEAD);
         if(cp_obsolete != NULL)
            n_OBSOLETE(_("please use *message-inject-head*, not *NAIL_HEAD*"));

         if(((cp = ok_vlook(message_inject_head)) != NULL ||
            (cp = cp_obsolete) != NULL) && putesc(cp, _coll_fp) < 0)
         goto jerr;

         /* Quote an original message */
         if (mp != NULL && (doprefix || (cp = ok_vlook(quote)) != NULL)) {
            quoteitp = n_IGNORE_ALL;
            action = SEND_QUOTE;
            if (doprefix) {
               quoteitp = n_IGNORE_FWD;
               if ((cp = ok_vlook(fwdheading)) == NULL)
                  cp = "-------- Original Message --------";
               if (*cp != '\0' && fprintf(_coll_fp, "%s\n", cp) < 0)
                  goto jerr;
            } else if (!strcmp(cp, "noheading")) {
               /*EMPTY*/;
            } else if (!strcmp(cp, "headers")) {
               quoteitp = n_IGNORE_TYPE;
            } else if (!strcmp(cp, "allheaders")) {
               quoteitp = NULL;
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
            if (sendmp(mp, _coll_fp, quoteitp, cp, action, NULL) < 0)
               goto jerr;
         }
      }

      if (quotefile != NULL) {
         if (_include_file(quotefile, &lc, &cc, FAL0) != 0)
            goto jerr;
      }

      if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_Mm_FLAG)){
         /* Print what we have sofar also on the terminal (if useful) */
         if (!ok_blook(editalong)) {
            if (printheaders)
               puthead(TRU1, hp, n_stdout, t,
                  SEND_TODISP, CONV_NONE, NULL, NULL);

            rewind(_coll_fp);
            while ((c = getc(_coll_fp)) != EOF) /* XXX bytewise, yuck! */
               putc(c, n_stdout);
            if (fseek(_coll_fp, 0, SEEK_END))
               goto jerr;
         } else {
            rewind(_coll_fp);
            mesedit('e', hp);
            /* As mandated by the Mail Reference Manual, print "(continue)" */
jcont:
            if(n_psonce & n_PSO_INTERACTIVE)
               fputs(_("(continue)\n"), n_stdout);
         }
         fflush(n_stdout);
      }
   } else {
      /* Come here for printing the after-signal message.  Duplicate messages
       * won't be printed because the write is aborted if we get a SIGTTOU */
      if (_coll_hadintr)
         n_err(_("\n(Interrupt -- one more to kill letter)\n"));
   }

   /* If not under shell hook control */
   if(coap == NULL){
      /* We're done with -M or -m (because we are too simple minded) */
      if(n_poption & n_PO_Mm_FLAG)
         goto jout;
      /* No command escapes, interrupts not expected.  Simply copy STDIN */
      if(!(n_psonce & n_PSO_INTERACTIVE) &&
            !(n_poption & (n_PO_t_FLAG | n_PO_TILDE_FLAG))){
         linebuf = srealloc(linebuf, linesize = LINESIZE);
         while ((i = fread(linebuf, sizeof *linebuf, linesize, n_stdin)) > 0) {
            if (i != fwrite(linebuf, sizeof *linebuf, i, _coll_fp))
               goto jerr;
         }
         goto jout;
      }
   }

   /* The interactive collect loop */
   for(;;){
      /* C99 */{
         enum n_lexinput_flags lif;

         lif = n_LEXINPUT_CTX_COMPOSE;
         if((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_TILDE_FLAG)){
            if(!(n_poption & n_PO_t_FLAG))
               lif |= n_LEXINPUT_NL_ESC;
         }
         cnt = n_lex_input(lif, n_empty, &linebuf, &linesize, NULL);
      }

      if (cnt < 0) {
         if(coap != NULL)
            break;
         if(n_poption & n_PO_t_FLAG){
            fflush_rewind(_coll_fp);
            /* It is important to set n_PSO_t_FLAG before extract_header()
             * *and* keep n_PO_t_FLAG for the first parse of the message! */
            n_psonce |= n_PSO_t_FLAG;
            if (makeheader(_coll_fp, hp, checkaddr_err) != OKAY)
               goto jerr;
            n_poption &= ~n_PO_t_FLAG;
            continue;
         }else if((n_psonce & n_PSO_INTERACTIVE) && ok_blook(ignoreeof) &&
               ++eofcnt < 4){
            fprintf(n_stdout,
               _("*ignoreeof* set, use `~.' to terminate letter\n"));
            continue;
         }
         break;
      }

      _coll_hadintr = 0;

      cp = linebuf;
      if(cnt == 0)
         goto jputnl;
      else if(coap == NULL){
         if(!(n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_TILDE_FLAG))
            goto jputline;
         else if(cp[0] == '.'){
            if(cnt == 1 && (ok_blook(dot) || ok_blook(ignoreeof)))
               break;
         }
      }
      if(cp[0] != escape){
jputline:
         if(fwrite(cp, sizeof *cp, cnt, _coll_fp) != (size_t)cnt)
            goto jerr;
         /* TODO n_PS_READLINE_NL is a terrible hack to ensure that _in_all_-
          * TODO _code_paths_ a file without trailing newline isn't modified
          * TODO to continue one; the "saw-newline" needs to be part of an
          * TODO I/O input machinery object */
jputnl:
         if(n_pstate & n_PS_READLINE_NL){
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
         memmove(&linebuf[3], cp, (cnt -= i));
         linebuf[2] = ' ';
         linebuf[cnt] = '\0';

         if(cnt > 0){ /* TODO v15 no more trailing WS from lex_input please */
            cp = &linebuf[cnt];

            for(;; --cp){
               c = cp[-1];
               if(!blankspacechar(c))
                  break;
            }
            ((char*)n_UNCONST(cp))[0] = '\0';
            cnt = PTR2SIZE(cp - linebuf);
         }
      }

      switch((c = linebuf[1])){
      default:
         /* On double escape, send a single one.  Otherwise, it's an error */
         if(c == escape){
            cp = &linebuf[1];
            --cnt;
            goto jputline;
         }else{
            char buf[sizeof(n_UNIREPL)];

            if(asciichar(c))
               buf[0] = c, buf[1] = '\0';
            else if(n_psonce & n_PSO_UNICODE)
               memcpy(buf, n_unirepl, sizeof n_unirepl);
            else
               buf[0] = '?', buf[1] = '\0';
            n_err(_("Unknown command escape: ~%s\n"), buf);
            continue;
         }
jearg:
         n_err(_("Invalid command escape usage: %s\n"), linebuf);
         continue;
      case '!':
         /* Shell escape, send the balance of line to sh -c */
         if(cnt == 2 || coap != NULL)
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
         if(cnt != 2 || coap != NULL)
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
         exit(n_EXIT_ERR);
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
         /* Subject:; take care for Debian #419840 and strip any \r and \n */
         if(n_anyof_cp("\n\r", hp->h_subject = savestr(&linebuf[3]))){
            char *xp;

            n_err(_("-s: normalizing away invalid ASCII NL / CR bytes\n"));
            for(xp = hp->h_subject; *xp != '\0'; ++xp)
               if(*xp == '\n' || *xp == '\r')
                  *xp = ' ';
         }
         break;
      case '@':{
         struct attachment *aplist;

         /* Edit the attachment list */
         aplist = hp->h_attach;
         hp->h_attach = NULL;
         if(cnt != 2)
            hp->h_attach = n_attachment_append_list(aplist, &linebuf[3]);
         else
            hp->h_attach = n_attachment_list_edit(aplist,
                  n_LEXINPUT_CTX_COMPOSE);
      }  break;
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
            if((cp = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL)
               break;
         }
         if(is_dir(cp)){
            n_err(_("%s: is a directory\n"), n_shexp_quote_cp(cp, FAL0));
            break;
         }
         if(_include_file(cp, &lc, &cc, (c == 'R')) != 0){
            if(ferror(_coll_fp))
               goto jerr;
            break;
         }
         fprintf(n_stdout, _("%s %d/%d\n"), n_shexp_quote_cp(cp, FAL0), lc, cc);
         break;
      case 'i':
         /* Insert a variable into the file */
         if(cnt == 2)
            goto jearg;
         if((cp = n_var_vlook(&linebuf[3], TRU1)) == NULL || *cp == '\0')
            break;
         if(putesc(cp, _coll_fp) < 0) /* TODO v15: user resp upon `set' time */
            goto jerr;
         if((n_psonce & n_PSO_INTERACTIVE) && putesc(cp, n_stdout) < 0)
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
            if((n_psonce & n_PSO_INTERACTIVE) && putesc(cp, n_stdout) < 0)
               goto jerr;
         }
         break;
      case 'w':
         /* Write the message on a file */
         if(cnt == 2)
            goto jearg;
         if((cp = fexpand(&linebuf[3], FEXP_LOCAL | FEXP_NOPROTO)) == NULL){
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
         if(cnt != 2 || coap != NULL)
            goto jearg;
         rewind(_coll_fp);
         mesedit(c, ok_blook(editheaders) ? hp : NULL);
         goto jhistcont;
      case '^':
         if(!a_collect_plumbing(&linebuf[3], hp))
            goto jearg;
         if(n_psonce & n_PSO_INTERACTIVE)
            break;
         continue;
      case '?':
         /* Last the lengthy help string.  (Very ugly, but take care for
          * compiler supported string lengths :() */
         fputs(_(
"COMMAND ESCAPES (to be placed after a newline) excerpt:\n"
"~.            Commit and send message\n"
"~: <command>  Execute a mail command\n"
"~<! <command> Insert output of command\n"
"~@ [<files>]  Edit attachment list\n"
"~A            Insert *Sign* variable (`~a': insert *sign*)\n"
"~c <users>    Add users to Cc: list (`~b': to Bcc:)\n"
"~d            Read in $DEAD (dead.letter)\n"
"~e            Edit message via $EDITOR\n"
            ), n_stdout);
         fputs(_(
"~F <msglist>  Read in with headers, don't *indentprefix* lines\n"
"~f <msglist>  Like ~F, but honour `ignore' / `retain' configuration\n"
"~H            Edit From:, Reply-To: and Sender:\n"
"~h            Prompt for Subject:, To:, Cc: and \"blind\" Bcc:\n"
"~i <variable> Insert a value and a newline\n"
"~M <msglist>  Read in with headers, *indentprefix* (`~m': `retain' etc.)\n"
"~p            Show current message compose buffer\n"
"~r <file>     Read in a file (`~<': likewise; `~R': *indentprefix* lines)\n"
            ), n_stdout);
         fputs(_(
"~s <subject>  Set Subject:\n"
"~t <users>    Add users to To: list\n"
"~u <msglist>  Read in message(s) without headers (`~U': indent lines)\n"
"~v            Edit message via $VISUAL\n"
"~w <file>     Write message onto file\n"
"~x            Abort composition, discard message (`~q': save in $DEAD)\n"
"~| <command>  Pipe message through shell filter\n"
            ), n_stdout);
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
      if(n_psonce & n_PSO_INTERACTIVE)
         n_tty_addhist(linebuf, TRU1);
      if(c != '\0')
         goto jcont;
   }

jout:
   /* Do we have *on-compose-done-shell*, or *on-compose-done*?
    * TODO Usual f...ed up state of signals and terminal etc. */
   if(coap == NULL && (cp = ok_vlook(on_compose_done_shell)) != NULL) Jocds:{
      union {int (*ptf)(void); char const *sh;} u;
      char const *cmd;

      /* Reset *escape* to be available and guaranteed! */
      escape = n_ESCAPE;

      if(coapm != NULL){
         u.ptf = &a_coll_ocds__mac;
         cmd = (char*)-1;
         a_coll_ocds__macname = cp = coapm;
      }else{
         u.sh = ok_vlook(SHELL);
         cmd = cp;
      }

      i = strlen(cp) +1;
      coap = n_lofi_alloc(n_VSTRUCT_SIZEOF(struct a_coll_ocds_arg, coa_cmd
            ) + i);
      coap->coa_pipe[0] = coap->coa_pipe[1] = -1;
      coap->coa_stdin = coap->coa_stdout = NULL;
      coap->coa_senderr = checkaddr_err;
      memcpy(coap->coa_cmd, cp, i);

      hold_all_sigs();
      coap->coa_opipe = safe_signal(SIGPIPE, SIG_IGN);
      coap->coa_oint = safe_signal(SIGINT, SIG_IGN);
      rele_all_sigs();

      if(pipe_cloexec(coap->coa_pipe) != -1 &&
            (coap->coa_stdin = Fdopen(coap->coa_pipe[0], "r", FAL0)) != NULL &&
            (coap->coa_stdout = Popen(cmd, "W", u.sh, NULL, coap->coa_pipe[1])
             ) != NULL){
         close(coap->coa_pipe[1]);
         coap->coa_pipe[1] = -1;

         temporary_compose_mode_hook_call(NULL, NULL, NULL);
         n_source_slice_hack(coap->coa_cmd, coap->coa_stdin, coap->coa_stdout,
            (n_psonce & ~(n_PSO_INTERACTIVE | n_PSO_TTYIN | n_PSO_TTYOUT)),
            &a_coll_ocds__finalize, &coap);
         /* Hook version protocol for ~^: update manual upon change! */
         fputs(a_COLL_PLUMBING_VERSION "\n", coap->coa_stdout);
#undef a_COLL_PLUMBING_VERSION
         goto jcont;
      }

      c = errno;
      a_coll_ocds__finalize(coap);
      n_perr(_("Cannot invoke *on-compose-done(-shell)?*"), c);
      goto jerr;
   }
   if(*checkaddr_err != 0){
      *checkaddr_err = 0;
      goto jerr;
   }
   if(coapm == NULL && (coapm = ok_vlook(on_compose_done)) != NULL)
      goto Jocds;
   escape = escape_saved;

   /* Final chance to edit headers, if not already above */
   if (ok_blook(bsdcompat) || ok_blook(askatend)) {
      if (hp->h_cc == NULL && ok_blook(askcc))
         grab_headers(n_LEXINPUT_CTX_COMPOSE, hp, GCC, 1);
      if (hp->h_bcc == NULL && ok_blook(askbcc))
         grab_headers(n_LEXINPUT_CTX_COMPOSE, hp, GBCC, 1);
   }
   if (hp->h_attach == NULL && ok_blook(askattach))
      hp->h_attach = n_attachment_list_edit(hp->h_attach,
            n_LEXINPUT_CTX_COMPOSE);

   /* Execute compose-leave */
   if((cp = ok_vlook(on_compose_leave)) != NULL){
      setup_from_and_sender(hp);
      temporary_compose_mode_hook_call(cp, &a_coll__hook_setter, hp);
   }

   /* Add automatic receivers */
   if ((cp = ok_vlook(autocc)) != NULL && *cp != '\0')
      hp->h_cc = cat(hp->h_cc, checkaddrs(lextract(cp, GCC | GFULL),
            EACM_NORMAL, checkaddr_err));
   if ((cp = ok_vlook(autobcc)) != NULL && *cp != '\0')
      hp->h_bcc = cat(hp->h_bcc, checkaddrs(lextract(cp, GBCC | GFULL),
            EACM_NORMAL, checkaddr_err));
   if (*checkaddr_err != 0)
      goto jerr;

   /* TODO Cannot do since it may require turning this into a multipart one */
   if(n_poption & n_PO_Mm_FLAG)
      goto jskiptails;

   /* Place signature? */
   if((cp = ok_vlook(signature)) != NULL && *cp != '\0'){
      char const *cpq;

      if((cpq = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL){
         n_err(_("*signature* expands to invalid file: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         goto jerr;
      }
      cpq = n_shexp_quote_cp(cp = cpq, FAL0);

      if((sigfp = Fopen(cp, "r")) == NULL){
         n_err(_("Can't open *signature* %s: %s\n"), cpq, strerror(errno));
         goto jerr;
      }

      if(linebuf == NULL)
         linebuf = smalloc(linesize = LINESIZE);
      c = '\0';
      while((i = fread(linebuf, sizeof *linebuf, linesize, n_UNVOLATILE(sigfp)))
            > 0){
         c = linebuf[i - 1];
         if(i != fwrite(linebuf, sizeof *linebuf, i, _coll_fp))
            goto jerr;
      }

      /* C99 */{
         FILE *x = n_UNVOLATILE(sigfp);
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
         n_OBSOLETE(_("please use *message-inject-tail*, not *NAIL_TAIL*"));

   if((cp = ok_vlook(message_inject_tail)) != NULL ||
         (cp = cp_obsolete) != NULL){
      if(putesc(cp, _coll_fp) < 0)
         goto jerr;
      if((n_psonce & n_PSO_INTERACTIVE) && putesc(cp, n_stdout) < 0)
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
   sigfillset(&nset);
   sigprocmask(SIG_BLOCK, &nset, NULL);
   n_pstate &= ~n_PS_COMPOSE_MODE;
   safe_signal(SIGINT, _coll_saveint);
   safe_signal(SIGHUP, _coll_savehup);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD_LEAVE;
   return _coll_fp;

jerr:
   if(coap != NULL && coap != (struct a_coll_ocds_arg*)-1)
      n_source_slice_hack_remove_after_jump();
   if(sigfp != NULL)
      Fclose(n_UNVOLATILE(sigfp));
   if (_coll_fp != NULL) {
      Fclose(_coll_fp);
      _coll_fp = NULL;
   }
   assert(checkaddr_err != NULL);
   /* TODO We don't save in $DEAD upon error because msg not readily composed?
    * TODO But this no good, it should go ZOMBIE / DRAFT / POSTPONED or what! */
   if(*checkaddr_err != 0)
      n_err(_("Some addressees were classified as \"hard error\"\n"));
   else if(_coll_hadintr == 0){
      *checkaddr_err = TRU1; /* TODO ugly: "sendout_error" now.. */
      n_err(_("Failed to prepare composed message (I/O error, disk full?)\n"));
   }
   goto jleave;
}

/* s-it-mode */
