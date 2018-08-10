/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Collect input from standard input, handling ~ escapes.
 *@ TODO This needs a complete rewrite, with carriers, etc.
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
#undef n_FILE
#define n_FILE collect

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct a_coll_fmt_ctx{ /* xxx This is temporary until v15 has objects */
   char const *cfc_fmt;
   FILE *cfc_fp;
   struct message *cfc_mp;
   char *cfc_cumul;
   char *cfc_addr;
   char *cfc_real;
   char *cfc_full;
   char *cfc_date;
   char const *cfc_msgid;  /* Or NULL */
};

struct a_coll_ocs_arg{
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
static char const *a_coll_ocs__macname;   /* *on-compose-splice* */

/* Handle `~:', `~_' and some hooks; hp may be NULL */
static void       _execute_command(struct header *hp, char const *linebuf,
                     size_t linesize);

/* Return errno */
static si32_t a_coll_include_file(char const *name, bool_t indent,
               bool_t writestat);

/* Execute cmd and insert its standard output into fp, return errno */
static si32_t a_coll_insert_cmd(FILE *fp, char const *cmd);

/* ~p command */
static void       print_collf(FILE *collf, struct header *hp);

/* Write a file, ex-like if f set */
static si32_t a_coll_write(char const *name, FILE *fp, int f);

/* *message-inject-head* */
static bool_t a_coll_message_inject_head(FILE *fp);

/* With bells and whistles */
static bool_t a_coll_quote_message(FILE *fp, struct message *mp, bool_t isfwd);

/* *{forward,quote}-inject-{head,tail}*.
 * fmt may be NULL or the empty string, in which case no output is produced */
static bool_t a_coll__fmt_inj(struct a_coll_fmt_ctx const *cfcp);

/* Parse off the message header from fp and store relevant fields in hp,
 * replace _coll_fp with a shiny new version without any header.
 * Takes care for closing of fp and _coll_fp as necessary */
static bool_t a_coll_makeheader(FILE *fp, struct header *hp,
               si8_t *checkaddr_err, bool_t do_delayed_due_t);

/* Edit the message being collected on fp.
 * If c=='|' pipecmd must be set and is passed through to n_run_editor().
 * On successful return, make the edit file the new temp file; return errno */
static si32_t a_coll_edit(int c, struct header *hp, char const *pipecmd);

/* Pipe the message through the command.  Old message is on stdin of command,
 * new message collected from stdout.  Shell must return 0 to accept new msg */
static si32_t a_coll_pipe(char const *cmd);

/* Interpolate the named messages into the current message, possibly doing
 * indent stuff.  The flag argument is one of the command escapes: [mMfFuU].
 * Return errno */
static si32_t a_coll_forward(char const *ms, FILE *fp, int f);

/* On interrupt, come here to save the partial message in ~/dead.letter.
 * Then jump out of the collection loop */
static void       _collint(int s);

static void       collhup(int s);

/* ~[AaIi], *message-inject-**: put value, expand \[nt] if *posix* */
static bool_t a_coll_putesc(char const *s, bool_t addnl, FILE *stream);

/* *on-compose-splice* driver and *on-compose-splice(-shell)?* finalizer */
static int a_coll_ocs__mac(void);
static void a_coll_ocs__finalize(void *vp);

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
   NYD_IN;

   n_UNUSED(linesize);
   mnbuf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_HUP | n_SIGMAN_INT | n_SIGMAN_QUIT){
   case 0:
      break;
   default:
      n_pstate_err_no = n_ERR_INTR;
      n_pstate_ex_no = 1;
      goto jleave;
   }

   /* If the above todo is worked, remove or outsource to attachment.c! */
   if(hp != NULL && (ap = hp->h_attach) != NULL) do
      if(ap->a_msgno){
         mnbuf = sstrdup(mailname);
         break;
      }
   while((ap = ap->a_flink) != NULL);

   n_go_command(n_GO_INPUT_CTX_COMPOSE, linebuf);

   n_sigman_cleanup_ping(&sm);
jleave:
   if(mnbuf != NULL){
      if(strcmp(mnbuf, mailname))
         n_err(_("Mailbox changed: it is likely that existing "
            "rfc822 attachments became invalid!\n"));
      n_free(mnbuf);
   }
   NYD_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
}

static si32_t
a_coll_include_file(char const *name, bool_t indent, bool_t writestat){
   FILE *fbuf;
   char const *heredb, *indb;
   size_t linesize, heredl, indl, cnt, linelen;
   char *linebuf;
   si64_t lc, cc;
   si32_t rv;
   NYD_IN;

   rv = n_ERR_NONE;
   lc = cc = 0;
   linebuf = NULL; /* TODO line pool */
   linesize = 0;
   heredb = NULL;
   heredl = 0;

   /* The -M case is special */
   if(name == (char*)-1){
      fbuf = n_stdin;
      name = n_hy;
   }else if(name[0] == '-' &&
         (name[1] == '\0' || blankspacechar(name[1]))){
      fbuf = n_stdin;
      if(name[1] == '\0'){
         if(!(n_psonce & n_PSO_INTERACTIVE)){
            n_err(_("~< -: HERE-delimiter required in non-interactive mode\n"));
            rv = n_ERR_INVAL;
            goto jleave;
         }
      }else{
         for(heredb = &name[2]; *heredb != '\0' && blankspacechar(*heredb);
               ++heredb)
            ;
         if((heredl = strlen(heredb)) == 0){
jdelim_empty:
            n_err(_("~< - HERE-delimiter: delimiter must not be empty\n"));
            rv = n_ERR_INVAL;
            goto jleave;
         }

         if(*heredb == '\''){
            for(indb = ++heredb; *indb != '\0' && *indb != '\''; ++indb)
               ;
            if(*indb == '\0'){
               n_err(_("~< - HERE-delimiter: missing trailing quote\n"));
               rv = n_ERR_INVAL;
               goto jleave;
            }else if(indb[1] != '\0'){
               n_err(_("~< - HERE-delimiter: trailing characters after "
                  "quote\n"));
               rv = n_ERR_INVAL;
               goto jleave;
            }
            if((heredl = PTR2SIZE(indb - heredb)) == 0)
               goto jdelim_empty;
            heredb = savestrbuf(heredb, heredl);
         }
      }
      name = n_hy;
   }else if((fbuf = Fopen(name, "r")) == NULL){
      n_perr(name, rv = n_err_no);
      goto jleave;
   }

   indl = indent ? strlen(indb = ok_vlook(indentprefix)) : 0;

   if(fbuf != n_stdin)
      cnt = fsize(fbuf);
   while(fgetline(&linebuf, &linesize, (fbuf == n_stdin ? NULL : &cnt),
         &linelen, fbuf, 0) != NULL){
      if(heredl > 0 && heredl == linelen - 1 &&
            !memcmp(heredb, linebuf, heredl)){
         heredb = NULL;
         break;
      }

      if(indl > 0){
         if(fwrite(indb, sizeof *indb, indl, _coll_fp) != indl){
            rv = n_err_no;
            goto jleave;
         }
         cc += indl;
      }

      if(fwrite(linebuf, sizeof *linebuf, linelen, _coll_fp) != linelen){
         rv = n_err_no;
         goto jleave;
      }
      cc += linelen;
      ++lc;
   }
   if(fflush(_coll_fp)){
      rv = n_err_no;
      goto jleave;
   }

   if(heredb != NULL)
      rv = n_ERR_NOTOBACCO;
jleave:
   if(linebuf != NULL)
      n_free(linebuf);
   if(fbuf != NULL){
      if(fbuf != n_stdin)
         Fclose(fbuf);
      else if(heredl > 0)
         clearerr(n_stdin);
   }

   if(writestat)
      fprintf(n_stdout, "%s%s %" PRId64 "/%" PRId64 "\n",
         n_shexp_quote_cp(name, FAL0), (rv ? " " n_ERROR : n_empty), lc, cc);
   NYD_OU;
   return rv;
}

static si32_t
a_coll_insert_cmd(FILE *fp, char const *cmd){
   FILE *ibuf;
   si64_t lc, cc;
   si32_t rv;
   NYD_IN;

   rv = n_ERR_NONE;
   lc = cc = 0;

   if((ibuf = Popen(cmd, "r", ok_vlook(SHELL), NULL, 0)) != NULL){
      int c;

      while((c = getc(ibuf)) != EOF){ /* XXX bytewise, yuck! */
         if(putc(c, fp) == EOF){
            rv = n_err_no;
            break;
         }
         ++cc;
         if(c == '\n')
            ++lc;
      }
      if(!feof(ibuf) || ferror(ibuf)){
         if(rv == n_ERR_NONE)
            rv = n_ERR_IO;
      }
      if(!Pclose(ibuf, TRU1)){
         if(rv == n_ERR_NONE)
            rv = n_ERR_IO;
      }
   }else
      n_perr(cmd, rv = n_err_no);

   fprintf(n_stdout, "CMD%s %" PRId64 "/%" PRId64 "\n",
      (rv == n_ERR_NONE ? n_empty : " " n_ERROR), lc, cc);
   NYD_OU;
   return rv;
}

static void
print_collf(FILE *cf, struct header *hp)
{
   char *lbuf;
   FILE *obuf;
   size_t cnt, linesize, linelen;
   NYD_IN;

   fflush_rewind(cf);
   cnt = (size_t)fsize(cf);

   if((obuf = Ftmp(NULL, "collfp", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("Can't create temporary file for `~p' command"), 0);
      goto jleave;
   }

   hold_all_sigs();

   fprintf(obuf, _("-------\nMessage contains:\n")); /* XXX112 */
   n_puthead(TRU1, hp, obuf,
      (GIDENT | GTO | GSUBJECT | GCC | GBCC | GBCC_IS_FCC | GNL | GFILES |
       GCOMMA), SEND_TODISP, CONV_NONE, NULL, NULL);

   lbuf = NULL;
   linesize = 0;
   while(fgetline(&lbuf, &linesize, &cnt, &linelen, cf, 1))
      prout(lbuf, linelen, obuf);
   if(lbuf != NULL)
      n_free(lbuf);

   if(hp->h_attach != NULL){
      fputs(_("-------\nAttachments:\n"), obuf);
      n_attachment_list_print(hp->h_attach, obuf);
   }

   rele_all_sigs();

   page_or_print(obuf, 0);

   Fclose(obuf);
jleave:
   NYD_OU;
}

static si32_t
a_coll_write(char const *name, FILE *fp, int f)
{
   FILE *of;
   int c;
   si64_t lc, cc;
   si32_t rv;
   NYD_IN;

   rv = n_ERR_NONE;

   if(f) {
      fprintf(n_stdout, "%s ", n_shexp_quote_cp(name, FAL0));
      fflush(n_stdout);
   }

   if ((of = Fopen(name, "a")) == NULL) {
      n_perr(name, rv = n_err_no);
      goto jerr;
   }

   lc = cc = 0;
   while ((c = getc(fp)) != EOF) {
      ++cc;
      if (c == '\n')
         ++lc;
      if (putc(c, of) == EOF) {
         n_perr(name, rv = n_err_no);
         goto jerr;
      }
   }
   fprintf(n_stdout, "%" PRId64 "/%" PRId64 "\n", lc, cc);

jleave:
   if(of != NULL)
      Fclose(of);
   fflush(n_stdout);
   NYD_OU;
   return rv;
jerr:
   putc('-', n_stdout);
   putc('\n', n_stdout);
   goto jleave;
}

static bool_t
a_coll_message_inject_head(FILE *fp){
   bool_t rv;
   char const *cp, *cp_obsolete;
   NYD2_IN;

   cp_obsolete = ok_vlook(NAIL_HEAD);
   if(cp_obsolete != NULL)
      n_OBSOLETE(_("please use *message-inject-head*, not *NAIL_HEAD*"));

   if(((cp = ok_vlook(message_inject_head)) != NULL ||
         (cp = cp_obsolete) != NULL) && !a_coll_putesc(cp, TRU1, fp))
      rv = FAL0;
   else
      rv = TRU1;
   NYD2_OU;
   return rv;
}

static bool_t
a_coll_quote_message(FILE *fp, struct message *mp, bool_t isfwd){
   struct a_coll_fmt_ctx cfc;
   char const *cp;
   struct n_ignore const *quoteitp;
   enum sendaction action;
   bool_t rv;
   NYD_IN;

   rv = FAL0;

   if(isfwd || (cp = ok_vlook(quote)) != NULL){
      quoteitp = n_IGNORE_ALL;
      action = SEND_QUOTE;

      if(isfwd){
         char const *cp_v15compat;

         if((cp_v15compat = ok_vlook(fwdheading)) != NULL)
            n_OBSOLETE(_("please use *forward-inject-head* instead of "
               "*fwdheading*"));
         if((cp = ok_vlook(forward_inject_head)) == NULL &&
               (cp = cp_v15compat) == NULL)
            cp = n_FORWARD_INJECT_HEAD;
         quoteitp = n_IGNORE_FWD;
      }else{
         if(!strcmp(cp, "noheading")){
            cp = NULL;
         }else if(!strcmp(cp, "headers")){
            quoteitp = n_IGNORE_TYPE;
            cp = NULL;
         }else if(!strcmp(cp, "allheaders")){
            quoteitp = NULL;
            action = SEND_QUOTE_ALL;
            cp = NULL;
         }else if((cp = ok_vlook(quote_inject_head)) == NULL)
            cp = n_QUOTE_INJECT_HEAD;
      }
      /* We we pass through our formatter? */
      if((cfc.cfc_fmt = cp) != NULL){
         /* TODO In v15 [-textual_-]sender_info() should only create a list
          * TODO of matching header objects, and the formatter should simply
          * TODO iterate over this list and call OBJ->to_ui_str(FLAGS) or so.
          * TODO For now fully initialize this thing once (grrrr!!) */
         cfc.cfc_fp = fp;
         cfc.cfc_mp = mp;
         n_header_textual_sender_info(cfc.cfc_mp = mp, &cfc.cfc_cumul,
            &cfc.cfc_addr, &cfc.cfc_real, &cfc.cfc_full, NULL);
         cfc.cfc_date = n_header_textual_date_info(mp, NULL);
         /* C99 */{
            struct name *np;
            char const *msgid;

            if((msgid = hfield1("message-id", mp)) != NULL &&
                  (np = lextract(msgid, GREF)) != NULL)
               msgid = np->n_name;
            else
               msgid = NULL;
            cfc.cfc_msgid = msgid;
         }

         if(!a_coll__fmt_inj(&cfc) || fflush(fp))
            goto jleave;
      }

      if(sendmp(mp, fp, quoteitp, (isfwd ? NULL : ok_vlook(indentprefix)),
            action, NULL) < 0)
         goto jleave;

      if(isfwd){
         if((cp = ok_vlook(forward_inject_tail)) == NULL)
             cp = n_FORWARD_INJECT_TAIL;
      }else if(cp != NULL && (cp = ok_vlook(quote_inject_tail)) == NULL)
          cp = n_QUOTE_INJECT_TAIL;
      if((cfc.cfc_fmt = cp) != NULL && (!a_coll__fmt_inj(&cfc) || fflush(fp)))
         goto jleave;
   }

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
}

static bool_t
a_coll__fmt_inj(struct a_coll_fmt_ctx const *cfcp){
   struct quoteflt qf;
   struct n_string s_b, *sp;
   char c;
   char const *fmt;
   NYD_IN;

   if((fmt = cfcp->cfc_fmt) == NULL || *fmt == '\0')
      goto jleave;

   sp = n_string_book(n_string_creat_auto(&s_b), 127);

   while((c = *fmt++) != '\0'){
      if(c != '%' || (c = *fmt++) == '%'){
jwrite_char:
         sp = n_string_push_c(sp, c);
      }else switch(c){
      case 'a':
         sp = n_string_push_cp(sp, cfcp->cfc_addr);
         break;
      case 'd':
         sp = n_string_push_cp(sp, cfcp->cfc_date);
         break;
      case 'f':
         sp = n_string_push_cp(sp, cfcp->cfc_full);
         break;
      case 'i':
         if(cfcp->cfc_msgid != NULL)
            sp = n_string_push_cp(sp, cfcp->cfc_msgid);
         break;
      case 'n':
         sp = n_string_push_cp(sp, cfcp->cfc_cumul);
         break;
      case 'r':
         sp = n_string_push_cp(sp, cfcp->cfc_real);
         break;
      case '\0':
         --fmt;
         c = '%';
         goto jwrite_char;
      default:
         n_err(_("*{forward,quote}-inject-{head,tail}*: "
            "unknown format: %c (in: %s)\n"),
            c, n_shexp_quote_cp(cfcp->cfc_fmt, FAL0));
         goto jwrite_char;
      }
   }

   quoteflt_init(&qf, NULL, FAL0);
   quoteflt_reset(&qf, cfcp->cfc_fp);
   if(quoteflt_push(&qf, sp->s_dat, sp->s_len) < 0 || quoteflt_flush(&qf) < 0)
      cfcp = NULL;
   quoteflt_destroy(&qf);

   /*n_string_gut(sp);*/
jleave:
   NYD_OU;
   return (cfcp != NULL);
}

static bool_t
a_coll_makeheader(FILE *fp, struct header *hp, si8_t *checkaddr_err,
   bool_t do_delayed_due_t)
{
   FILE *nf;
   int c;
   bool_t rv;
   NYD_IN;

   rv = FAL0;

   if ((nf = Ftmp(NULL, "colhead", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==NULL) {
      n_perr(_("temporary mail edit file"), 0);
      goto jleave;
   }

   n_header_extract(((do_delayed_due_t
            ? n_HEADER_EXTRACT_FULL | n_HEADER_EXTRACT_PREFILL_RECEIVERS
            : n_HEADER_EXTRACT_EXTENDED) |
         n_HEADER_EXTRACT_IGNORE_SHELL_COMMENTS), fp, hp, checkaddr_err);
   if (checkaddr_err != NULL && *checkaddr_err != 0)
      goto jleave;

   /* In template mode some things have been delayed until the template has
    * been read */
   if(do_delayed_due_t){
      char const *cp;

      if((cp = ok_vlook(on_compose_enter)) != NULL){
         setup_from_and_sender(hp);
         temporary_compose_mode_hook_call(cp, &n_temporary_compose_hook_varset,
            hp);
      }

      if(!a_coll_message_inject_head(nf))
         goto jleave;
   }

   while ((c = getc(fp)) != EOF) /* XXX bytewise, yuck! */
      putc(c, nf);

   if (fp != _coll_fp)
      Fclose(_coll_fp);
   Fclose(fp);
   _coll_fp = nf;
   nf = NULL;

   if (check_from_and_sender(hp->h_from, hp->h_sender) == NULL)
      goto jleave;
   rv = TRU1;
jleave:
   if(nf != NULL)
      Fclose(nf);
   NYD_OU;
   return rv;
}

static si32_t
a_coll_edit(int c, struct header *hp, char const *pipecmd) /* TODO errret */
{
   struct n_sigman sm;
   FILE *nf;
   sighandler_type volatile sigint;
   struct name *saved_in_reply_to;
   bool_t saved_filrec;
   si32_t volatile rv;
   NYD_IN;

   rv = n_ERR_NONE;
   n_UNINIT(sigint, SIG_ERR);
   saved_filrec = ok_blook(add_file_recipients);

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      sigint = safe_signal(SIGINT, SIG_IGN);
      break;
   default:
      rv = n_ERR_INTR;
      goto jleave;
   }

   if(!saved_filrec)
      ok_bset(add_file_recipients);

   saved_in_reply_to = NULL;
   if(hp != NULL){
      struct name *np;

      if((np = hp->h_in_reply_to) == NULL)
         hp->h_in_reply_to = np = n_header_setup_in_reply_to(hp);
      if(np != NULL)
         saved_in_reply_to = ndup(np, np->n_type);
   }

   rewind(_coll_fp);
   nf = n_run_editor(_coll_fp, (off_t)-1, c, FAL0, hp, NULL, SEND_MBOX, sigint,
         pipecmd);
   if(nf != NULL){
      if(hp != NULL){
         /* Overtaking of nf->_coll_fp is done by a_coll_makeheader()! */
         if(!a_coll_makeheader(nf, hp, NULL, FAL0))
            rv = n_ERR_INVAL;
         /* Break the thread if In-Reply-To: has been modified */
         if(hp->h_in_reply_to == NULL || (saved_in_reply_to != NULL &&
               asccasecmp(hp->h_in_reply_to->n_fullname,
                  saved_in_reply_to->n_fullname)))
               hp->h_ref = NULL;
      }else{
         fseek(nf, 0L, SEEK_END);
         Fclose(_coll_fp);
         _coll_fp = nf;
      }
   }else
      rv = n_ERR_CHILD;

   n_sigman_cleanup_ping(&sm);
jleave:
   if(!saved_filrec)
      ok_bclear(add_file_recipients);
   safe_signal(SIGINT, sigint);
   NYD_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

static si32_t
a_coll_pipe(char const *cmd)
{
   int ws;
   FILE *nf;
   sighandler_type sigint;
   si32_t rv;
   NYD_IN;

   rv = n_ERR_NONE;
   sigint = safe_signal(SIGINT, SIG_IGN);

   if ((nf = Ftmp(NULL, "colpipe", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==NULL) {
jperr:
      n_perr(_("temporary mail edit file"), rv = n_err_no);
      goto jout;
   }

   /* stdin = current message.  stdout = new message */
   if(fflush(_coll_fp) == EOF)
      goto jperr;
   rewind(_coll_fp);
   if (n_child_run(ok_vlook(SHELL), 0, fileno(_coll_fp), fileno(nf), "-c",
         cmd, NULL, NULL, &ws) < 0 || WEXITSTATUS(ws) != 0) {
      Fclose(nf);
      rv = n_ERR_CHILD;
      goto jout;
   }

   if (fsize(nf) == 0) {
      n_err(_("No bytes from %s !?\n"), n_shexp_quote_cp(cmd, FAL0));
      Fclose(nf);
      rv = n_ERR_NODATA;
      goto jout;
   }

   /* Take new files */
   fseek(nf, 0L, SEEK_END);
   Fclose(_coll_fp);
   _coll_fp = nf;
jout:
   safe_signal(SIGINT, sigint);
   NYD_OU;
   return rv;
}

static si32_t
a_coll_forward(char const *ms, FILE *fp, int f)
{
   int *msgvec, rv = 0;
   struct n_ignore const *itp;
   char const *tabst;
   enum sendaction action;
   NYD_IN;

   if ((rv = n_getmsglist(ms, n_msgvec, 0, NULL)) < 0) {
      rv = n_ERR_NOENT; /* XXX not really, should be handled there! */
      goto jleave;
   }
   if (rv == 0) {
      *n_msgvec = first(0, MMNORM);
      if (*n_msgvec == 0) {
         n_err(_("No appropriate messages\n"));
         rv = n_ERR_NOENT;
         goto jleave;
      }
      rv = 1;
   }
   msgvec = n_autorec_calloc(rv +1, sizeof *msgvec);
   while(rv-- > 0)
      msgvec[rv] = n_msgvec[rv];
   rv = 0;

   if (f == 'f' || f == 'F' || f == 'u')
      tabst = NULL;
   else
      tabst = ok_vlook(indentprefix);
   if (f == 'u' || f == 'U')
      itp = n_IGNORE_ALL;
   else
      itp = upperchar(f) ? NULL : n_IGNORE_TYPE;
   action = (upperchar(f) && f != 'U') ? SEND_QUOTE_ALL : SEND_QUOTE;

   fprintf(n_stdout, A_("Interpolating:"));
   srelax_hold();
   for(; *msgvec != 0; ++msgvec){
      struct message *mp;

      mp = &message[*msgvec - 1];
      touch(mp);

      fprintf(n_stdout, " %d", *msgvec);
      fflush(n_stdout);
      if(f == 'Q'){
         if(!a_coll_quote_message(fp, mp, FAL0)){
            rv = n_ERR_IO;
            break;
         }
      }else if(sendmp(mp, fp, itp, tabst, action, NULL) < 0){
         n_perr(_("forward: temporary mail file"), 0);
         rv = n_ERR_IO;
         break;
      }
      srelax();
   }
   srelax_rele();
   fprintf(n_stdout, "\n");
jleave:
   NYD_OU;
   return rv;
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

static bool_t
a_coll_putesc(char const *s, bool_t addnl, FILE *stream){
   char c1, c2;
   bool_t isposix;
   NYD2_IN;

   isposix = ok_blook(posix);

   while((c1 = *s++) != '\0'){
      if(c1 == '\\' && ((c2 = *s) == 't' || c2 == 'n')){
         if(!isposix){
            isposix = TRU1; /* TODO v15 OBSOLETE! */
            n_err(_("Compose mode warning: expanding \\t or \\n in variable "
                  "without *posix*!\n"
               "  Support remains only for ~A,~a,~I,~i in *posix* mode!\n"
               "  Please use \"wysh set X=y..\" instead\n"));
         }
         ++s;
         c1 = (c2 == 't') ? '\t' : '\n';
      }

      if(putc(c1, stream) == EOF)
         goto jleave;
   }

   if(addnl && putc('\n', stream) == EOF)
      goto jleave;

jleave:
   NYD2_OU;
   return (c1 == '\0');
}

static int
a_coll_ocs__mac(void){
   /* Executes in a fork(2)ed child  TODO if remains, global MASKs for those! */
   setvbuf(n_stdin, NULL, _IOLBF, 0);
   setvbuf(n_stdout, NULL, _IOLBF, 0);
   n_psonce &= ~(n_PSO_INTERACTIVE | n_PSO_TTYIN | n_PSO_TTYOUT);
   n_pstate |= n_PS_COMPOSE_FORKHOOK;
   n_readctl_read_overlay = NULL; /* TODO need OnForkEvent! See c_readctl() */
   n_digmsg_read_overlay = NULL; /* TODO need OnForkEvent! See c_digmsg() */
   if(n_poption & n_PO_D_VV){
      char buf[128];

      snprintf(buf, sizeof buf, "[%d]%s", getpid(), ok_vlook(log_prefix));
      ok_vset(log_prefix, buf);
   }
   /* TODO If that uses `!' it will effectively SIG_IGN SIGINT, ...and such */
   temporary_compose_mode_hook_call(a_coll_ocs__macname, NULL, NULL);
   return 0;
}

static void
a_coll_ocs__finalize(void *vp){
   /* Note we use this for destruction upon setup errors, thus */
   sighandler_type opipe;
   sighandler_type oint;
   struct a_coll_ocs_arg **coapp, *coap;
   NYD2_IN;

   temporary_compose_mode_hook_call((char*)-1, NULL, NULL);

   coap = *(coapp = vp);
   *coapp = (struct a_coll_ocs_arg*)-1;

   if(coap->coa_stdin != NULL)
      Fclose(coap->coa_stdin);
   else if(coap->coa_pipe[0] != -1)
      close(coap->coa_pipe[0]);

   if(coap->coa_stdout != NULL && !Pclose(coap->coa_stdout, TRU1))
      *coap->coa_senderr = 111;
   if(coap->coa_pipe[1] != -1)
      close(coap->coa_pipe[1]);

   opipe = coap->coa_opipe;
   oint = coap->coa_oint;

   n_lofi_free(coap);

   hold_all_sigs();
   safe_signal(SIGPIPE, opipe);
   safe_signal(SIGINT, oint);
   rele_all_sigs();
   NYD2_OU;
}

FL void
n_temporary_compose_hook_varset(void *arg){ /* TODO v15: drop */
   struct header *hp;
   char const *val;
   NYD2_IN;

   hp = arg;

   if((val = hp->h_subject) == NULL)
      val = n_empty;
   ok_vset(mailx_subject, val);
   if((val = detract(hp->h_from, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_from, val);
   if((val = detract(hp->h_sender, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_sender, val);
   if((val = detract(hp->h_to, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_to, val);
   if((val = detract(hp->h_cc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_cc, val);
   if((val = detract(hp->h_bcc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_bcc, val);

   if((val = hp->h_mailx_command) == NULL)
      val = n_empty;
   ok_vset(mailx_command, val);

   if((val = detract(hp->h_mailx_raw_to, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_raw_to, val);
   if((val = detract(hp->h_mailx_raw_cc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_raw_cc, val);
   if((val = detract(hp->h_mailx_raw_bcc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_raw_bcc, val);

   if((val = detract(hp->h_mailx_orig_from, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_orig_from, val);
   if((val = detract(hp->h_mailx_orig_to, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_orig_to, val);
   if((val = detract(hp->h_mailx_orig_cc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_orig_cc, val);
   if((val = detract(hp->h_mailx_orig_bcc, GNAMEONLY)) == NULL)
      val = n_empty;
   ok_vset(mailx_orig_bcc, val);
   NYD2_OU;
}

FL FILE *
n_collect(enum n_mailsend_flags msf, struct header *hp, struct message *mp,
   char const *quotefile, si8_t *checkaddr_err)
{
   struct n_dig_msg_ctx dmc;
   struct n_string s, * volatile sp;
   struct a_coll_ocs_arg *coap;
   int c;
   int volatile t, eofcnt, getfields;
   char volatile escape;
   enum{
      a_NONE,
      a_ERREXIT = 1u<<0,
      a_IGNERR = 1u<<1,
      a_COAP_NOSIGTERM = 1u<<8
#define a_HARDERR() ((flags & (a_ERREXIT | a_IGNERR)) == a_ERREXIT)
   } volatile flags;
   char *linebuf;
   char const *cp, *cp_base, * volatile coapm, * volatile ifs_saved;
   size_t i, linesize; /* TODO line pool */
   long cnt;
   sigset_t oset, nset;
   FILE * volatile sigfp;
   NYD_IN;

   n_DIG_MSG_COMPOSE_CREATE(&dmc, hp);
   _coll_fp = NULL;

   sigfp = NULL;
   linesize = 0;
   linebuf = NULL;
   flags = a_NONE;
   eofcnt = 0;
   ifs_saved = coapm = NULL;
   coap = NULL;
   sp = NULL;

   /* Start catching signals from here, but we still die on interrupts
    * until we're in the main loop */
   sigfillset(&nset);
   sigprocmask(SIG_BLOCK, &nset, &oset);
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
      n_perr(_("collect: temporary mail file"), 0);
      goto jerr;
   }

   /* If we are going to prompt for a subject, refrain from printing a newline
    * after the headers (since some people mind) */
   getfields = 0;
   if(!(n_poption & n_PO_t_FLAG)){
      t = GTO | GSUBJECT | GCC | GNL;
      if(ok_blook(fullnames))
         t |= GCOMMA;

      if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
         if(hp->h_subject == NULL && ok_blook(asksub)/* *ask* auto warped! */)
            t &= ~GNL, getfields |= GSUBJECT;

         if(hp->h_to == NULL)
            t &= ~GNL, getfields |= GTO;

         if(!ok_blook(bsdcompat) && !ok_blook(askatend)){
            if(ok_blook(askbcc))
               t &= ~GNL, getfields |= GBCC;
            if(ok_blook(askcc))
               t &= ~GNL, getfields |= GCC;
         }
      }
   }else{
      n_UNINIT(t, 0);
   }

   _coll_hadintr = 0;

   if (!sigsetjmp(_coll_jmp, 1)) {
      /* Ask for some headers first, as necessary */
      if (getfields)
         grab_headers(n_GO_INPUT_CTX_COMPOSE, hp, getfields, 1);

      /* Execute compose-enter; delayed for -t mode */
      if(!(n_poption & n_PO_t_FLAG) &&
            (cp = ok_vlook(on_compose_enter)) != NULL){
         setup_from_and_sender(hp);
         temporary_compose_mode_hook_call(cp, &n_temporary_compose_hook_varset,
            hp);
      }

      /* TODO Mm: nope since it may require turning this into a multipart one */
      if(!(n_poption & (n_PO_Mm_FLAG | n_PO_t_FLAG))){
         if(!a_coll_message_inject_head(_coll_fp))
            goto jerr;

         /* Quote an original message */
         if(mp != NULL && !a_coll_quote_message(_coll_fp, mp,
               ((msf & n_MAILSEND_IS_FWD) != 0)))
            goto jerr;
      }

      if (quotefile != NULL) {
         if((n_pstate_err_no = a_coll_include_file(quotefile, FAL0, FAL0)
               ) != n_ERR_NONE)
            goto jerr;
      }

      if(n_psonce & n_PSO_INTERACTIVE){
         if(!(n_pstate & n_PS_SOURCING)){
            sp = n_string_creat_auto(&s);
            sp = n_string_reserve(sp, 80);
         }

         if(!(n_poption & n_PO_Mm_FLAG) && !(n_pstate & n_PS_ROBOT)){
            /* Print what we have sofar also on the terminal (if useful) */
            if((cp = ok_vlook(editalong)) == NULL){
               if(msf & n_MAILSEND_HEADERS_PRINT)
                  n_puthead(TRU1, hp, n_stdout, t, SEND_TODISP, CONV_NONE,
                     NULL, NULL);

               rewind(_coll_fp);
               while ((c = getc(_coll_fp)) != EOF) /* XXX bytewise, yuck! */
                  putc(c, n_stdout);
               if (fseek(_coll_fp, 0, SEEK_END))
                  goto jerr;
            }else{
               if(a_coll_edit(((*cp == 'v') ? 'v' : 'e'), hp, NULL
                     ) != n_ERR_NONE)
                  goto jerr;
               /* Print msg mandated by the Mail Reference Manual */
jcont:
               if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT))
                  fputs(_("(continue)\n"), n_stdout);
            }
            fflush(n_stdout);
         }
      }
   } else {
      /* Come here for printing the after-signal message.  Duplicate messages
       * won't be printed because the write is aborted if we get a SIGTTOU */
      if(_coll_hadintr && (n_psonce & n_PSO_INTERACTIVE) &&
            !(n_pstate & n_PS_ROBOT))
         n_err(_("\n(Interrupt -- one more to kill letter)\n"));
   }

   /* If not under shell hook control */
   if(coap == NULL){
      /* We're done with -M or -m TODO because: we are too stupid yet, above */
      if(n_poption & n_PO_Mm_FLAG)
         goto jout;
      /* No command escapes, interrupts not expected? */
      if(!(n_psonce & n_PSO_INTERACTIVE) &&
            !(n_poption & (n_PO_t_FLAG | n_PO_TILDE_FLAG))){
         /* Need to go over n_go_input() to handle injections */
         for(;;){
            cnt = n_go_input(n_GO_INPUT_CTX_COMPOSE, n_empty,
                  &linebuf, &linesize, NULL, NULL);
            if(cnt < 0){
               if(!n_go_input_is_eof())
                  goto jerr;
               break;
            }
            i = (size_t)cnt;
            if(i != fwrite(linebuf, sizeof *linebuf, i, _coll_fp))
               goto jerr;
            /* TODO n_PS_READLINE_NL is a hack to ensure that _in_all_-
             * TODO _code_paths_ a file without trailing NL isn't modified
             * TODO to contain one; the "saw-newline" needs to be part of an
             * TODO I/O input machinery object */
            if(n_pstate & n_PS_READLINE_NL){
               if(putc('\n', _coll_fp) == EOF)
                  goto jerr;
            }
         }
         goto jout;
      }
   }

   /* The interactive collect loop */
   if(coap == NULL)
      escape = *ok_vlook(escape);
   flags = ok_blook(errexit) ? a_ERREXIT : a_NONE;

   for(;;){
      enum {a_HIST_NONE, a_HIST_ADD = 1u<<0, a_HIST_GABBY = 1u<<1} hist;

      /* C99 */{
         enum n_go_input_flags gif;
         bool_t histadd;

         /* TODO optimize: no need to evaluate that anew for each loop tick! */
         gif = n_GO_INPUT_CTX_COMPOSE;
         histadd = (sp != NULL);
         if((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_TILDE_FLAG)){
            if(!(n_poption & n_PO_t_FLAG) || (n_psonce & n_PSO_t_FLAG_DONE))
               gif |= n_GO_INPUT_NL_ESC;
         }
         cnt = n_go_input(gif, n_empty, &linebuf, &linesize, NULL, &histadd);
         hist = histadd ? a_HIST_ADD | a_HIST_GABBY : a_HIST_NONE;
      }

      if(cnt < 0){ /* TODO n_go_input_is_eof()!  Could be error!! */
         if(coap != NULL)
            break;
         if((n_poption & n_PO_t_FLAG) && !(n_psonce & n_PSO_t_FLAG_DONE)){
            fflush_rewind(_coll_fp);
            n_psonce |= n_PSO_t_FLAG_DONE;
            if(!a_coll_makeheader(_coll_fp, hp, checkaddr_err, TRU1))
               goto jerr;
            continue;
         }else if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT) &&
               ok_blook(ignoreeof) && ++eofcnt < 4){
            fprintf(n_stdout,
               _("*ignoreeof* set, use `~.' to terminate letter\n"));
            n_go_input_clearerr();
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
            if(cnt == 1 && (ok_blook(dot) ||
                  (ok_blook(posix) && ok_blook(ignoreeof))))
               break;
         }
      }
      if(cp[0] != escape){
jputline:
         if(fwrite(cp, sizeof *cp, cnt, _coll_fp) != (size_t)cnt)
            goto jerr;
         /* TODO n_PS_READLINE_NL is a terrible hack to ensure that _in_all_-
          * TODO _code_paths_ a file without trailing newline isn't modified
          * TODO to contain one; the "saw-newline" needs to be part of an
          * TODO I/O input machinery object */
jputnl:
         if(n_pstate & n_PS_READLINE_NL){
            if(putc('\n', _coll_fp) == EOF)
               goto jerr;
         }
         continue;
      }

      c = *(cp_base = ++cp);
      if(--cnt == 0)
         goto jearg;

      /* Avoid history entry? */
      while(spacechar(c)){
         hist = a_HIST_NONE;
         c = *(cp_base = ++cp);
         if(--cnt == 0)
            goto jearg;
      }

      /* It may just be an escaped escaped character, do that quick */
      if(c == escape)
         goto jputline;

      /* Avoid hard *errexit*? */
      flags &= ~a_IGNERR;
      if(c == '-'){
         flags ^= a_IGNERR;
         c = *++cp;
         if(--cnt == 0)
            goto jearg;
      }

      /* Trim input, also to gain a somewhat normalized history entry */
      ++cp;
      if(--cnt > 0){
         struct str x;

         x.s = n_UNCONST(cp);
         x.l = (size_t)cnt;
         n_str_trim_ifs(&x, TRU1);
         x.s[x.l] = '\0';
         cp = x.s;
         cnt = (int)/*XXX*/x.l;
      }

      if(hist != a_HIST_NONE){
         sp = n_string_assign_c(sp, escape);
         if(flags & a_IGNERR)
            sp = n_string_push_c(sp, '-');
         sp = n_string_push_c(sp, c);
         if(cnt > 0){
            sp = n_string_push_c(sp, ' ');
            sp = n_string_push_buf(sp, cp, cnt);
         }
      }

      /* Switch over all command escapes */
      switch(c){
      default:
         if(1){
            char buf[sizeof(n_UNIREPL)];

            if(asciichar(c))
               buf[0] = c, buf[1] = '\0';
            else if(n_psonce & n_PSO_UNICODE)
               memcpy(buf, n_unirepl, sizeof n_unirepl);
            else
               buf[0] = '?', buf[1] = '\0';
            n_err(_("Unknown command escape: `%c%s'\n"), escape, buf);
         }else
jearg:
            n_err(_("Invalid command escape usage: %s\n"),
               n_shexp_quote_cp(linebuf, FAL0));
         if(a_HARDERR())
            goto jerr;
         n_pstate_err_no = n_ERR_INVAL;
         n_pstate_ex_no = 1;
         continue;
      case '!':
         /* Shell escape, send the balance of line to sh -c */
         if(cnt == 0 || coap != NULL)
            goto jearg;
         else{
            char const *argv[2];

            argv[0] = cp;
            argv[1] = NULL;
            n_pstate_ex_no = c_shell(argv); /* TODO history norm.; errexit? */
         }
         goto jhistcont;
      case '.':
         /* Simulate end of file on input */
         if(cnt != 0 || coap != NULL)
            goto jearg;
         goto jout; /* TODO does not enter history, thus */
      case ':':
      case '_':
         /* Escape to command mode, but be nice! *//* TODO command expansion
          * TODO should be handled here so that we have unique history! */
         if(cnt == 0)
            goto jearg;
         _execute_command(hp, cp, cnt);
         if(ok_blook(errexit))
            flags |= a_ERREXIT;
         else
            flags &= ~a_ERREXIT;
         if(n_pstate_ex_no != 0 && a_HARDERR())
            goto jerr;
         if(coap == NULL)
            escape = *ok_vlook(escape);
         hist &= ~a_HIST_GABBY;
         break;
      /* case '<': <> 'd' */
      case '?':
#ifdef HAVE_UISTRINGS
         fputs(_(
"COMMAND ESCAPES (to be placed after a newline) excerpt:\n"
"~.            Commit and send message\n"
"~: <command>  Execute an internal command\n"
"~< <file>     Insert <file> (\"~<! <command>\": insert shell command)\n"
"~@ [<files>]  Edit [Add] attachments (file[=in-charset[#out-charset]])\n"
"~c <users>    Add users to Cc: list (`~b': to Bcc:)\n"
"~e, ~v        Edit message via $EDITOR / $VISUAL\n"
            ), n_stdout);
         fputs(_(
"~F <msglist>  Read in with headers, do not *indentprefix* lines\n"
"~f <msglist>  Like `~F', but honour `headerpick' configuration\n"
"~H            Edit From:, Reply-To: and Sender:\n"
"~h            Prompt for Subject:, To:, Cc: and Bcc:\n"
"~i <variable> Insert a value and a newline (`~I': insert value)\n"
"~M <msglist>  Read in with headers, *indentprefix* (`~m': use `headerpick')\n"
"~p            Show current message compose buffer\n"
"~Q <msglist>  Read in using normal *quote* algorithm\n"
            ), n_stdout);
         fputs(_(
"~r <file>     Insert <file> (`~R': *indentprefix* lines)\n"
"              <file> may also be <- [HERE-DELIMITER]>\n"
"~s <subject>  Set Subject:\n"
"~t <users>    Add users to To: list\n"
"~u <msglist>  Read in without headers (`~U': *indentprefix* lines)\n"
"~w <file>     Write message onto file\n"
"~x            Abort composition, discard message (`~q': save in $DEAD)\n"
"~| <command>  Pipe message through shell filter (`~||': with headers)\n"
            ), n_stdout);
#endif /* HAVE_UISTRINGS */
         if(cnt != 0)
            goto jearg;
         n_pstate_err_no = n_ERR_NONE;
         n_pstate_ex_no = 0;
         break;
      case '@':{
         struct attachment *aplist;

         /* Edit the attachment list */
         aplist = hp->h_attach;
         hp->h_attach = NULL;
         if(cnt != 0)
            hp->h_attach = n_attachment_append_list(aplist, cp);
         else
            hp->h_attach = n_attachment_list_edit(aplist,
                  n_GO_INPUT_CTX_COMPOSE);
         n_pstate_err_no = n_ERR_NONE; /* XXX ~@ does NOT handle $!/$?! */
         n_pstate_ex_no = 0; /* XXX */
         }break;
      case '^':
         if(!n_dig_msg_circumflex(&dmc, n_stdout, cp)){
            if(ferror(_coll_fp))
               goto jerr;
            goto jearg;
         }
         n_pstate_err_no = n_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         hist &= ~a_HIST_GABBY;
         break;
      /* case '_': <> ':' */
      case '|':
         /* Pipe message through command. Collect output as new message */
         if(cnt == 0)
            goto jearg;
         /* Is this request to do a "stream equivalent" to 'e' and 'v'? */
         if(*cp == '|'){
            ++cp;
            goto jev_go;
         }
         if((n_pstate_err_no = a_coll_pipe(cp)) == n_ERR_NONE)
            n_pstate_ex_no = 0;
         else if(ferror(_coll_fp))
            goto jerr;
         else if(a_HARDERR())
            goto jerr;
         else
            n_pstate_ex_no = 1;
         hist &= ~a_HIST_GABBY;
         goto jhistcont;
      case 'A':
      case 'a':
         /* Insert the contents of a sign variable */
         if(cnt != 0)
            goto jearg;
         cp = (c == 'a') ? ok_vlook(sign) : ok_vlook(Sign);
         goto jIi_putesc;
      case 'b':
         /* Add stuff to blind carbon copies list TODO join 'c' */
         if(cnt == 0)
            goto jearg;
         else{
            struct name *np;
            si8_t soe;

            soe = 0;
            if((np = checkaddrs(lextract(cp, GBCC | GFULL), EACM_NORMAL, &soe)
                  ) != NULL)
               hp->h_bcc = cat(hp->h_bcc, np);
            if(soe == 0){
               n_pstate_err_no = n_ERR_NONE;
               n_pstate_ex_no = 0;
            }else{
               n_pstate_ex_no = 1;
               n_pstate_err_no = (soe < 0) ? n_ERR_PERM : n_ERR_INVAL;
            }
         }
         hist &= ~a_HIST_GABBY;
         break;
      case 'c':
         /* Add to the CC list TODO join 'b' */
         if(cnt == 0)
            goto jearg;
         else{
            struct name *np;
            si8_t soe;

            soe = 0;
            if((np = checkaddrs(lextract(cp, GCC | GFULL), EACM_NORMAL, &soe)
                  ) != NULL)
               hp->h_cc = cat(hp->h_cc, np);
            if(soe == 0){
               n_pstate_err_no = n_ERR_NONE;
               n_pstate_ex_no = 0;
            }else{
               n_pstate_ex_no = 1;
               n_pstate_err_no = (soe < 0) ? n_ERR_PERM : n_ERR_INVAL;
            }
         }
         hist &= ~a_HIST_GABBY;
         break;
      case 'd':
         if(cnt != 0)
            goto jearg;
         cp = n_getdeadletter();
         if(0){
      case '<':
      case 'R':
      case 'r':
            /* Invoke a file: Search for the file name, then open it and copy
             * the contents to _coll_fp */
            if(cnt == 0){
               n_err(_("Interpolate what file?\n"));
               if(a_HARDERR())
                  goto jerr;
               n_pstate_err_no = n_ERR_NOENT;
               n_pstate_ex_no = 1;
               break;
            }
            if(*cp == '!' && c == '<'){
               /* TODO hist. normalization */
               if((n_pstate_err_no = a_coll_insert_cmd(_coll_fp, ++cp)
                     ) != n_ERR_NONE){
                  if(ferror(_coll_fp))
                     goto jerr;
                  if(a_HARDERR())
                     goto jerr;
                  n_pstate_ex_no = 1;
                  break;
               }
               goto jhistcont;
            }
            /* Note this also expands things like
             *    !:vput vexpr delim random 0
             *    !< - $delim */
            if((cp = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO | FEXP_NSHELL)
                  ) == NULL){
               if(a_HARDERR())
                  goto jerr;
               n_pstate_err_no = n_ERR_INVAL;
               n_pstate_ex_no = 1;
               break;
            }
         }
         /* XXX race, and why not test everywhere, then? */
         if(n_is_dir(cp, FAL0)){
            n_err(_("%s: is a directory\n"), n_shexp_quote_cp(cp, FAL0));
            if(a_HARDERR())
               goto jerr;
            n_pstate_err_no = n_ERR_ISDIR;
            n_pstate_ex_no = 1;
            break;
         }
         if((n_pstate_err_no = a_coll_include_file(cp, (c == 'R'), TRU1)
               ) != n_ERR_NONE){
            if(ferror(_coll_fp))
               goto jerr;
            if(a_HARDERR())
               goto jerr;
            n_pstate_ex_no = 1;
            break;
         }
         n_pstate_err_no = n_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      case 'e':
      case 'v':
         /* Edit the current message.  'e' -> use EDITOR, 'v' -> use VISUAL */
         if(cnt != 0 || coap != NULL)
            goto jearg;
jev_go:
         if((n_pstate_err_no = a_coll_edit(c,
                ((c == '|' || ok_blook(editheaders)) ? hp : NULL), cp)
               ) == n_ERR_NONE)
            n_pstate_ex_no = 0;
         else if(ferror(_coll_fp))
            goto jerr;
         else if(a_HARDERR())
            goto jerr;
         else
            n_pstate_ex_no = 1;
         goto jhistcont;
      case 'F':
      case 'f':
      case 'M':
      case 'm':
      case 'Q':
      case 'U':
      case 'u':
         /* Interpolate the named messages, if we are in receiving mail mode.
          * Does the standard list processing garbage.  If ~f is given, we
          * don't shift over */
         if((n_pstate_err_no = a_coll_forward(cp, _coll_fp, c)) == n_ERR_NONE)
            n_pstate_ex_no = 0;
         else if(ferror(_coll_fp))
            goto jerr;
         else if(a_HARDERR())
            goto jerr;
         else
            n_pstate_ex_no = 1;
         break;
      case 'H':
         /* Grab extra headers */
         if(cnt != 0)
            goto jearg;
         do
            grab_headers(n_GO_INPUT_CTX_COMPOSE, hp, GEXTRA, 0);
         while(check_from_and_sender(hp->h_from, hp->h_sender) == NULL);
         n_pstate_err_no = n_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      case 'h':
         /* Grab a bunch of headers */
         if(cnt != 0)
            goto jearg;
         do
            grab_headers(n_GO_INPUT_CTX_COMPOSE, hp,
              (GTO | GSUBJECT | GCC | GBCC),
              (ok_blook(bsdcompat) && ok_blook(bsdorder)));
         while(hp->h_to == NULL);
         n_pstate_err_no = n_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      case 'I':
      case 'i':
         /* Insert a variable into the file */
         if(cnt == 0)
            goto jearg;
         cp = n_var_vlook(cp, TRU1);
jIi_putesc:
         if(cp == NULL || *cp == '\0')
            break;
         if(!a_coll_putesc(cp, (c != 'I'), _coll_fp))
            goto jerr;
         if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT) &&
               (!a_coll_putesc(cp, (c != 'I'), n_stdout) ||
                fflush(n_stdout) == EOF))
            goto jerr;
         n_pstate_err_no = n_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      /* case 'M': <> 'F' */
      /* case 'm': <> 'f' */
      case 'p':
         /* Print current state of the message without altering anything */
         if(cnt != 0)
            goto jearg;
         print_collf(_coll_fp, hp); /* XXX pstate_err_no ++ */
         if(ferror(_coll_fp))
            goto jerr;
         n_pstate_err_no = n_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      /* case 'Q': <> 'F' */
      case 'q':
      case 'x':
         /* Force a quit, act like an interrupt had happened */
         if(cnt != 0)
            goto jearg;
         /* If we are running a splice hook, assume it quits on its own now,
          * otherwise we (no true out-of-band IPC to signal this state, XXX sic)
          * have to SIGTERM it in order to stop this wild beast */
         flags |= a_COAP_NOSIGTERM;
         ++_coll_hadintr;
         _collint((c == 'x') ? 0 : SIGINT);
         exit(n_EXIT_ERR);
         /*NOTREACHED*/
      /* case 'R': <> 'd' */
      /* case 'r': <> 'd' */
      case 's':
         /* Set the Subject list */
         if(cnt == 0)
            goto jearg;
         /* Subject:; take care for Debian #419840 and strip any \r and \n */
         if(n_anyof_cp("\n\r", hp->h_subject = savestr(cp))){
            char *xp;

            n_err(_("-s: normalizing away invalid ASCII NL / CR bytes\n"));
            for(xp = hp->h_subject; *xp != '\0'; ++xp)
               if(*xp == '\n' || *xp == '\r')
                  *xp = ' ';
            n_pstate_err_no = n_ERR_INVAL;
            n_pstate_ex_no = 1;
         }else{
            n_pstate_err_no = n_ERR_NONE;
            n_pstate_ex_no = 0;
         }
         break;
      case 't':
         /* Add to the To: list TODO join 'b', 'c' */
         if(cnt == 0)
            goto jearg;
         else{
            struct name *np;
            si8_t soe;

            soe = 0;
            if((np = checkaddrs(lextract(cp, GTO | GFULL), EACM_NORMAL, &soe)
                  ) != NULL)
               hp->h_to = cat(hp->h_to, np);
            if(soe == 0){
               n_pstate_err_no = n_ERR_NONE;
               n_pstate_ex_no = 0;
            }else{
               n_pstate_ex_no = 1;
               n_pstate_err_no = (soe < 0) ? n_ERR_PERM : n_ERR_INVAL;
            }
         }
         hist &= ~a_HIST_GABBY;
         break;
      /* case 'U': <> 'F' */
      /* case 'u': <> 'f' */
      /* case 'v': <> 'e' */
      case 'w':
         /* Write the message on a file */
         if(cnt == 0)
            goto jearg;
         if((cp = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL){
            n_err(_("Write what file!?\n"));
            if(a_HARDERR())
               goto jerr;
            n_pstate_err_no = n_ERR_INVAL;
            n_pstate_ex_no = 1;
            break;
         }
         rewind(_coll_fp);
         if((n_pstate_err_no = a_coll_write(cp, _coll_fp, 1)) == n_ERR_NONE)
            n_pstate_ex_no = 0;
         else if(ferror(_coll_fp))
            goto jerr;
         else if(a_HARDERR())
            goto jerr;
         else
            n_pstate_ex_no = 1;
         break;
      /* case 'x': <> 'q' */
      }

      /* Finally place an entry in history as applicable */
      if(0){
jhistcont:
         c = '\1';
      }else
         c = '\0';
      if(hist & a_HIST_ADD){
         /* Do not add *escape* to the history in order to allow history search
          * to be handled generically in the MLE regardless of actual *escape*
          * settings etc. */
         n_tty_addhist(&n_string_cp(sp)[1], (n_GO_INPUT_CTX_COMPOSE |
            (hist & a_HIST_GABBY ? n_GO_INPUT_HIST_GABBY : n_GO_INPUT_NONE)));
      }
      if(c != '\0')
         goto jcont;
   }

jout:
   /* Do we have *on-compose-splice-shell*, or *on-compose-splice*?
    * TODO Usual f...ed up state of signals and terminal etc. */
   if(coap == NULL && (cp = ok_vlook(on_compose_splice_shell)) != NULL) Jocs:{
      union {int (*ptf)(void); char const *sh;} u;
      char const *cmd;

      /* Reset *escape* and more to their defaults.  On change update manual! */
      if(ifs_saved == NULL)
         ifs_saved = savestr(ok_vlook(ifs));
      escape = n_ESCAPE[0];
      ok_vclear(ifs);

      if(coapm != NULL){
         /* XXX Due Popen() fflush(NULL) in PTF mode, ensure nothing to flush */
         /*if(!n_real_seek(_coll_fp, 0, SEEK_END))
          *  goto jerr;*/
         u.ptf = &a_coll_ocs__mac;
         cmd = (char*)-1;
         a_coll_ocs__macname = cp = coapm;
      }else{
         u.sh = ok_vlook(SHELL);
         cmd = cp;
      }

      i = strlen(cp) +1;
      coap = n_lofi_alloc(n_VSTRUCT_SIZEOF(struct a_coll_ocs_arg, coa_cmd
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
         n_go_splice_hack(coap->coa_cmd, coap->coa_stdin, coap->coa_stdout,
            (n_psonce & ~(n_PSO_INTERACTIVE | n_PSO_TTYIN | n_PSO_TTYOUT)),
            &a_coll_ocs__finalize, &coap);
         /* Hook version protocol for ~^: update manual upon change! */
         fputs(n_DIG_MSG_PLUMBING_VERSION "\n", n_stdout/*coap->coa_stdout*/);
         goto jcont;
      }

      c = n_err_no;
      a_coll_ocs__finalize(coap);
      n_perr(_("Cannot invoke *on-compose-splice(-shell)?*"), c);
      goto jerr;
   }
   if(*checkaddr_err != 0){
      *checkaddr_err = 0;
      goto jerr;
   }
   if(coapm == NULL && (coapm = ok_vlook(on_compose_splice)) != NULL)
      goto Jocs;
   if(coap != NULL){
      ok_vset(ifs, ifs_saved);
      ifs_saved = NULL;
   }

   /*
    * Note: the order of the following steps is documented for `~.'.
    * Adjust the manual on change!!
    */

   /* Final chance to edit headers, if not already above; and *asksend* */
   if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
      if(ok_blook(bsdcompat) || ok_blook(askatend)){
         enum gfield gf;

         gf = GNONE;
         if(ok_blook(askcc))
            gf |= GCC;
         if(ok_blook(askbcc))
            gf |= GBCC;
         if(gf != 0)
            grab_headers(n_GO_INPUT_CTX_COMPOSE, hp, gf, 1);
      }

      if(ok_blook(askattach))
         hp->h_attach = n_attachment_list_edit(hp->h_attach,
               n_GO_INPUT_CTX_COMPOSE);

      if(ok_blook(asksend)){
         bool_t b;

         ifs_saved = coapm = NULL;
         coap = NULL;

         fprintf(n_stdout, _("-------\nEnvelope contains:\n")); /* XXX112 */
         if(!n_puthead(TRU1, hp, n_stdout,
               GIDENT | GREF_IRT  | GSUBJECT | GTO | GCC | GBCC | GBCC_IS_FCC |
               GCOMMA, SEND_TODISP, CONV_NONE, NULL, NULL))
            goto jerr;

jreasksend:
         if(n_go_input(n_GO_INPUT_CTX_COMPOSE | n_GO_INPUT_NL_ESC,
               _("Send this message [yes/no, empty: recompose]? "),
               &linebuf, &linesize, NULL, NULL) < 0){
            if(!n_go_input_is_eof())
               goto jerr;
            cp = n_1;
         }

         if((b = n_boolify(linebuf, UIZ_MAX, TRUM1)) < FAL0)
            goto jreasksend;
         if(b == TRU2)
            goto jcont;
         if(!b)
            goto jerr;
      }
   }

   /* Execute compose-leave */
   if((cp = ok_vlook(on_compose_leave)) != NULL){
      setup_from_and_sender(hp);
      temporary_compose_mode_hook_call(cp, &n_temporary_compose_hook_varset,
         hp);
   }

   /* Add automatic receivers */
   if ((cp = ok_vlook(autocc)) != NULL && *cp != '\0')
      hp->h_cc = cat(hp->h_cc, checkaddrs(lextract(cp, GCC |
            (ok_blook(fullnames) ? GFULL | GSKIN : GSKIN)),
            EACM_NORMAL, checkaddr_err));
   if ((cp = ok_vlook(autobcc)) != NULL && *cp != '\0')
      hp->h_bcc = cat(hp->h_bcc, checkaddrs(lextract(cp, GBCC |
            (ok_blook(fullnames) ? GFULL | GSKIN : GSKIN)),
            EACM_NORMAL, checkaddr_err));
   if (*checkaddr_err != 0)
      goto jerr;

   /* TODO Cannot do since it may require turning this into a multipart one */
   if(n_poption & n_PO_Mm_FLAG)
      goto jskiptails;

   /* Place signature? */
   if((cp = ok_vlook(signature)) != NULL && *cp != '\0'){ /* TODO OBSOLETE */
      char const *cpq;

      n_OBSOLETE(_("please use *on-compose-{leave,splice}* and/or "
         "*message-inject-tail*, not *signature*"));

      if((cpq = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL){
         n_err(_("*signature* expands to invalid file: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         goto jerr;
      }
      cpq = n_shexp_quote_cp(cp = cpq, FAL0);

      if((sigfp = Fopen(cp, "r")) == NULL){
         n_err(_("Can't open *signature* %s: %s\n"),
            cpq, n_err_to_doc(n_err_no));
         goto jerr;
      }

      if(linebuf == NULL)
         linebuf = n_alloc(linesize = LINESIZE);
      c = '\0';
      while((i = fread(linebuf, sizeof *linebuf, linesize, n_UNVOLATILE(sigfp)))
            > 0){
         c = linebuf[i - 1];
         if(i != fwrite(linebuf, sizeof *linebuf, i, _coll_fp))
            goto jerr;
      }

      /* C99 */{
         FILE *x = n_UNVOLATILE(sigfp);
         int e = n_err_no, ise = ferror(x);

         sigfp = NULL;
         Fclose(x);

         if(ise){
            n_err(_("Errors while reading *signature* %s: %s\n"),
               cpq, n_err_to_doc(e));
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
      if(!a_coll_putesc(cp, TRU1, _coll_fp))
         goto jerr;
      if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT) &&
            (!a_coll_putesc(cp, TRU1, n_stdout) || fflush(n_stdout) == EOF))
         goto jerr;
   }
   }

jskiptails:
   if(fflush(_coll_fp))
      goto jerr;
   rewind(_coll_fp);

   if(mp != NULL && ok_blook(quote_as_attachment)){
      struct attachment *ap;

      ap = n_autorec_calloc(1, sizeof *ap);
      if((ap->a_flink = hp->h_attach) != NULL)
         hp->h_attach->a_blink = ap;
      hp->h_attach = ap;
      ap->a_msgno = (int)PTR2SIZE(mp - message + 1);
      ap->a_content_description = _("Original message content");
   }

jleave:
   if (linebuf != NULL)
      n_free(linebuf);
   sigprocmask(SIG_BLOCK, &nset, NULL);
   n_DIG_MSG_COMPOSE_GUT(&dmc);
   n_pstate &= ~n_PS_COMPOSE_MODE;
   safe_signal(SIGINT, _coll_saveint);
   safe_signal(SIGHUP, _coll_savehup);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD_OU;
   return _coll_fp;

jerr:
   hold_all_sigs();

   if(coap != NULL && coap != (struct a_coll_ocs_arg*)-1){
      if(!(flags & a_COAP_NOSIGTERM))
         n_psignal(coap->coa_stdout, SIGTERM);
      n_go_splice_hack_remove_after_jump();
      coap = NULL;
   }
   if(ifs_saved != NULL){
      ok_vset(ifs, ifs_saved);
      ifs_saved = NULL;
   }
   if(sigfp != NULL){
      Fclose(n_UNVOLATILE(sigfp));
      sigfp = NULL;
   }
   if(_coll_fp != NULL){
      Fclose(_coll_fp);
      _coll_fp = NULL;
   }

   rele_all_sigs();

   assert(checkaddr_err != NULL);
   /* TODO We don't save in $DEAD upon error because msg not readily composed?
    * TODO But this no good, it should go ZOMBIE / DRAFT / POSTPONED or what! */
   if(*checkaddr_err != 0){
      if(*checkaddr_err == 111)
         n_err(_("Compose mode splice hook failure\n"));
      else
         n_err(_("Some addressees were classified as \"hard error\"\n"));
   }else if(_coll_hadintr == 0){
      *checkaddr_err = TRU1; /* TODO ugly: "sendout_error" now.. */
      n_err(_("Failed to prepare composed message\n"));
   }
   goto jleave;

#undef a_HARDERR
}

/* s-it-mode */
