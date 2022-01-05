/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Collect input from standard input, handling ~ escapes.
 *@ TODO This needs a complete rewrite, with carriers, etc.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE collect
#define mx_SOURCE
#define mx_SOURCE_COLLECT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>
#include <su/utf.h>

#include "mx/attachments.h"
#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/cmd-edit.h"
#include "mx/cmd-misc.h"
#include "mx/compat.h"
#include "mx/dig-msg.h"
#include "mx/file-streams.h"
#include "mx/filter-quote.h"
#include "mx/go.h"
#include "mx/ignore.h"
#include "mx/names.h"
#include "mx/sigs.h"
#include "mx/tty.h"
#include "mx/ui-str.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct a_coll_fmt_ctx{ /* xxx This is temporary until v15 has objects */
   char const *cfc_fmt;
   FILE *cfc_fp;
   char const *cfc_indent_prefix;
   struct message *cfc_mp;
   char *cfc_cumul;
   char *cfc_addr;
   char *cfc_real;
   char *cfc_full;
   char *cfc_date;
   char const *cfc_msgid;  /* Or NULL */
};

struct a_coll_ocs_arg{
   n_sighdl_t coa_opipe;
   n_sighdl_t coa_oint;
   FILE *coa_stdin; /* The descriptor (pipe(2)+fs_fd_open()) we read from */
   FILE *coa_stdout; /* The pipe_open() through which we write to the hook */
   sz coa_pipe[2]; /* ..backing .coa_stdin */
   s8 *coa_senderr; /* Set to 1 on failure */
   char coa_cmd[VFIELD_SIZE(0)];
};

struct a_coll_quote_ctx{
   struct su_mem_bag *cqc_membag_persist; /* Or NIL */
   FILE *cqc_fp;
   struct header *cqc_hp;
   struct mx_ignore const *cqc_quoteitp; /* Or NIL */
   boole cqc_add_cc; /* Honour *{forward,quote}-add-cc* (not initial quote)? */
   boole cqc_is_forward; /* Forwarding rather than quoting */
   boole cqc_do_quote; /* Forced ~Q, not initial reply */
   u8 cqc__pad[1];
   enum sendaction cqc_action;
   char const *cqc_indent_prefix;
   struct message *cqc_mp;
};

/* The following hookiness with global variables is so that on receipt of an
 * interrupt signal, the partial message can be salted away on *DEAD* */

static n_sighdl_t  _coll_saveint;    /* Previous SIGINT value */
static n_sighdl_t  _coll_savehup;    /* Previous SIGHUP value */
static FILE             *_coll_fp;        /* File for saving away */
static int volatile     _coll_hadintr;    /* Have seen one SIGINT so far */
static sigjmp_buf       _coll_jmp;        /* To get back to work */
static sigjmp_buf       _coll_abort;      /* To end collection with error */
static char const *a_coll_ocs__macname;   /* *on-compose-splice* */

/* Handle `~:', `~_' and some hooks; hp may be NULL */
static void       _execute_command(struct header *hp, char const *linebuf,
                     uz linesize);

/* Return errno */
static s32 a_coll_include_file(char const *name, boole indent,
               boole writestat);

/* Execute cmd and insert its standard output into fp, return errno */
static s32 a_coll_insert_cmd(FILE *fp, char const *cmd);

/* ~p command */
static boole a_coll_print(FILE *collf, struct header *hp);

/* Write a file, ex-like if f set */
static s32 a_coll_write(char const *name, FILE *fp, int f);

/* *message-inject-head* */
static boole a_coll_message_inject_head(FILE *fp);

/* If mp==NIL, we try to use hp->h_mailx_orig_sender */
static void a_collect_add_sender_to_cc(struct header *hp, struct message *mp);

/* With bells and whistles */
static boole a_coll_quote_message(struct a_coll_quote_ctx *cqcp);

/* *{forward,quote}-inject-{head,tail}*.
 * fmt may be NULL or the empty string, in which case no output is produced */
static boole a_coll__fmt_inj(struct a_coll_fmt_ctx const *cfcp);

/* Parse off the message header from fp and store relevant fields in hp,
 * replace _coll_fp with a shiny new version without any header.
 * Takes care for closing of fp and _coll_fp as necessary */
static boole a_coll_makeheader(FILE *fp, struct header *hp,
               s8 *checkaddr_err, boole do_delayed_due_t);

/* Edit the message being collected on fp.
 * If c=='|' pipecmd must be set and is passed through to n_run_editor().
 * On successful return, make the edit file the new temp file; return errno */
static s32 a_coll_edit(int c, struct header *hp, char const *pipecmd);

/* Pipe the message through the command.  Old message is on stdin of command,
 * new message collected from stdout.  Shell must return 0 to accept new msg */
static s32 a_coll_pipe(char const *cmd);

/* Interpolate the named messages into the current message, possibly doing
 * indent stuff.  The flag argument is one of the command escapes: [mMfFuU].
 * Return errno */
static s32 a_coll_forward(char const *ms, FILE *fp, struct header *hp, int f);

/* On interrupt, come here to save the partial message in ~/dead.letter.
 * Then jump out of the collection loop */
static void       _collint(int s);

static void       collhup(int s);

/* ~[AaIi], *message-inject-**: put value, expand \[nt] if *posix* */
static boole a_coll_putesc(char const *s, boole addnl, FILE *stream);

/* *on-compose-splice* driver and *on-compose-splice(-shell)?* finalizer */
static int a_coll_ocs__mac(void);
static void a_coll_ocs__finalize(void *vp);

static void
_execute_command(struct header *hp, char const *linebuf, uz linesize){
   /* The problem arises if there are rfc822 message attachments and the
    * user uses `~:' to change the current file.  TODO Unfortunately we
    * TODO cannot simply keep a pointer to, or increment a reference count
    * TODO of the current `file' (mailbox that is) object, because the
    * TODO codebase doesn't deal with that at all; so, until some far
    * TODO later time, copy the name of the path, and warn the user if it
    * TODO changed; COULD use ATTACHMENTS_CONV_TMPFILE attachment type, i.e.,
    * TODO copy the message attachments over to temporary files, but that
    * TODO would require more changes so that the user still can recognize
    * TODO in `~@' etc. that its a rfc822 message attachment; see below */
   struct n_sigman sm;
   struct mx_attachment *ap;
   char * volatile mnbuf;
   NYD_IN;

   UNUSED(linesize);
   mnbuf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_HUP | n_SIGMAN_INT | n_SIGMAN_QUIT){
   case 0:
      break;
   default:
      n_pstate_err_no = su_ERR_INTR;
      n_pstate_ex_no = 1;
      goto jleave;
   }

   /* If the above todo is worked, remove or outsource to attachments.c! */
   if(hp != NIL && (ap = hp->h_attach) != NIL) do
      if(ap->a_msgno){
         mnbuf = su_cs_dup(mailname, 0);
         break;
      }
   while((ap = ap->a_flink) != NIL);

   mx_go_command(mx_GO_INPUT_CTX_COMPOSE, linebuf);

   n_sigman_cleanup_ping(&sm);
jleave:
   if(mnbuf != NULL){
      if(su_cs_cmp(mnbuf, mailname))
         n_err(_("Mailbox changed: it is likely that existing "
            "rfc822 attachments became invalid!\n"));
      n_free(mnbuf);
   }
   NYD_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
}

static s32
a_coll_include_file(char const *name, boole indent, boole writestat){
   FILE *fbuf;
   char const *heredb, *indb;
   uz linesize, heredl, indl, cnt, linelen;
   char *linebuf;
   s64 lc, cc;
   s32 rv;
   NYD_IN;

   rv = su_ERR_NONE;
   lc = cc = 0;
   mx_fs_linepool_aquire(&linebuf, &linesize);
   heredb = NIL;
   heredl = 0;
   UNINIT(indb, NIL);

   /* The -M case is special */
   if(name == R(char*,-1)){
      fbuf = n_stdin;
      name = n_hy;
   }else if(name[0] == '-' && (name[1] == '\0' || su_cs_is_space(name[1]))){
      fbuf = n_stdin;
      if(name[1] == '\0'){
         if(!(n_psonce & n_PSO_INTERACTIVE)){
            n_err(_("~< -: HERE-delimiter "
               "required in non-interactive mode\n"));
            rv = su_ERR_INVAL;
            goto jleave;
         }
      }else{
         for(heredb = &name[2]; *heredb != '\0' && su_cs_is_space(*heredb);
               ++heredb)
            ;
         if((heredl = su_cs_len(heredb)) == 0){
jdelim_empty:
            n_err(_("~< - HERE-delimiter: delimiter must not be empty\n"));
            rv = su_ERR_INVAL;
            goto jleave;
         }

         if(*heredb == '\''){
            for(indb = ++heredb; *indb != '\0' && *indb != '\''; ++indb)
               ;
            if(*indb == '\0'){
               n_err(_("~< - HERE-delimiter: missing trailing quote\n"));
               rv = su_ERR_INVAL;
               goto jleave;
            }else if(indb[1] != '\0'){
               n_err(_("~< - HERE-delimiter: trailing characters after "
                  "quote\n"));
               rv = su_ERR_INVAL;
               goto jleave;
            }
            if((heredl = P2UZ(indb - heredb)) == 0)
               goto jdelim_empty;
            heredb = savestrbuf(heredb, heredl);
         }
      }
      name = n_hy;
   }else if((fbuf = mx_fs_open(name, mx_FS_O_RDONLY)) == NIL){
      n_perr(name, rv = su_err_no());
      goto jleave;
   }

   indl = indent ? su_cs_len(indb = ok_vlook(indentprefix)) : 0;

   if(fbuf != n_stdin)
      cnt = fsize(fbuf);
   while(fgetline(&linebuf, &linesize, (fbuf == n_stdin ? NIL : &cnt),
         &linelen, fbuf, FAL0) != NIL){
      if(heredl > 0 && heredl == linelen - 1 &&
            !su_mem_cmp(heredb, linebuf, heredl)){
         heredb = NIL;
         break;
      }

      if(indl > 0){
         if(fwrite(indb, sizeof *indb, indl, _coll_fp) != indl)
            goto jerrno;
         cc += indl;
      }

      if(fwrite(linebuf, sizeof *linebuf, linelen, _coll_fp) != linelen)
         goto jerrno;

      cc += linelen;
      ++lc;
   }
   if(ferror(fbuf)){
      rv = su_ERR_IO;
      goto jleave;
   }

   if(fflush(_coll_fp)){
jerrno:
      rv = su_err_no_by_errno();
      goto jleave;
   }

   if(heredb != NIL)
      rv = su_ERR_NOTOBACCO;
jleave:
   mx_fs_linepool_release(linebuf, linesize);

   if(fbuf != NIL){
      if(fbuf != n_stdin)
         mx_fs_close(fbuf);
      else if(heredl > 0)
         clearerr(n_stdin);
   }

   if(writestat)
      fprintf(n_stdout, "%s%s %" PRId64 "/%" PRId64 "\n",
         n_shexp_quote_cp(name, FAL0), (rv ? " " n_ERROR : su_empty), lc, cc);

   NYD_OU;
   return rv;
}

static s32
a_coll_insert_cmd(FILE *fp, char const *cmd){
   FILE *ibuf;
   s64 lc, cc;
   s32 rv;
   NYD_IN;

   rv = su_ERR_NONE;
   lc = cc = 0;

   if((ibuf = mx_fs_pipe_open(cmd, mx_FS_PIPE_READ, ok_vlook(SHELL), NIL,
            -1)) != NIL){
      int c;

      while((c = getc(ibuf)) != EOF){ /* XXX bytewise, yuck! */
         if(putc(c, fp) == EOF){
            rv = su_err_no_by_errno();
            break;
         }
         ++cc;
         if(c == '\n')
            ++lc;
      }
      if(!feof(ibuf) || ferror(ibuf)){
         if(rv == su_ERR_NONE)
            rv = su_ERR_IO;
      }
      if(!mx_fs_pipe_close(ibuf, TRU1)){
         if(rv == su_ERR_NONE)
            rv = su_ERR_IO;
      }
   }else
      n_perr(cmd, rv = su_err_no());

   fprintf(n_stdout, "CMD%s %" PRId64 "/%" PRId64 "\n",
      (rv == su_ERR_NONE ? n_empty : " " n_ERROR), lc, cc);

   NYD_OU;
   return rv;
}

static boole
a_coll_print(FILE *cf, struct header *hp){
   FILE *obuf;
   char *linebuf;
   uz linesize, cnt, linelen;
   boole rv;
   NYD_IN;

   rv = FAL0;
   mx_fs_linepool_aquire(&linebuf, &linesize);

   if((obuf = mx_fs_tmp_open(NIL, "collfp", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
         NIL)) == NIL)
      obuf = n_stdout;

   if(fprintf(obuf, _("-------\nMessage contains:\n")) < 0)
      goto jleave;

   if(!n_puthead(TRU1, hp, obuf,
         (GIDENT | GTO | GSUBJECT | GCC | GBCC | GBCC_IS_FCC | GNL | GFILES |
          GCOMMA), SEND_TODISP, CONV_NONE, NIL, NIL))
      goto jleave;

   fflush_rewind(cf);
   cnt = S(uz,fsize(cf));
   while(fgetline(&linebuf, &linesize, &cnt, &linelen, cf, TRU1) != NIL)
      if(mx_makeprint_write_fp(linebuf, linelen, obuf) < 0)
         goto jleave;
   if(ferror(cf))
      goto jleave;

   if(hp->h_attach != NIL){
      if(fputs(_("-------\nAttachments:\n"), obuf) == EOF)
         goto jleave;
      if(mx_attachments_list_print(hp->h_attach, obuf) == -1)
         goto jleave;
   }

   if(obuf != n_stdout)
      page_or_print(obuf, 0);

   rv = TRU1;
jleave:
   if(obuf != n_stdout)
      mx_fs_close(obuf);
   else
      clearerr(obuf);

   mx_fs_linepool_release(linebuf, linesize);

   NYD_OU;
   return rv;
}

static s32
a_coll_write(char const *name, FILE *fp, int f)
{
   FILE *of;
   int c;
   s64 lc, cc;
   s32 rv;
   NYD_IN;

   rv = su_ERR_NONE;

   if(f) {
      fprintf(n_stdout, "%s ", n_shexp_quote_cp(name, FAL0));
      fflush(n_stdout);
   }

   if((of = mx_fs_open(name, (mx_FS_O_WRONLY | mx_FS_O_APPEND |
            mx_FS_O_CREATE))) == NIL){
      n_perr(name, rv = su_err_no());
      goto jerr;
   }

   lc = cc = 0;
   while ((c = getc(fp)) != EOF) {
      ++cc;
      if (c == '\n')
         ++lc;
      if (putc(c, of) == EOF) {
         n_perr(name, rv = su_err_no_by_errno());
         goto jerr;
      }
   }
   fprintf(n_stdout, "%" PRId64 "/%" PRId64 "\n", lc, cc);

jleave:
   if(of != NIL)
      mx_fs_close(of);
   fflush(n_stdout);

   NYD_OU;
   return rv;
jerr:
   putc('-', n_stdout);
   putc('\n', n_stdout);
   goto jleave;
}

static boole
a_coll_message_inject_head(FILE *fp){
   boole rv;
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

static void
a_collect_add_sender_to_cc(struct header *hp, struct message *mp){
   struct mx_name *addcc;
   NYD_IN;

   addcc = (mp != NIL) ? mx_header_sender_of(mp, 0) : hp->h_mailx_orig_sender;

   if(addcc != NIL){
      u32 gf;

      gf = GCC | GSKIN;
      if(ok_blook(fullnames))
         gf |= GFULL;
      hp->h_cc = cat(hp->h_cc, ndup(addcc, gf));
   }

   NYD_OU;
}

static boole
a_coll_quote_message(struct a_coll_quote_ctx *cqcp){
   struct a_coll_fmt_ctx cfc;
   char const *cp;
   boole rv;
   NYD_IN;

   rv = FAL0;

   if(cqcp->cqc_is_forward){
      char const *cp_v15compat;

      if(cqcp->cqc_add_cc && cqcp->cqc_hp != NIL && ok_blook(forward_add_cc)){
         if(cqcp->cqc_membag_persist != NIL)
            su_mem_bag_push(mx_go_data->gdc_membag, cqcp->cqc_membag_persist);
         a_collect_add_sender_to_cc(cqcp->cqc_hp, cqcp->cqc_mp);
         if(cqcp->cqc_membag_persist != NIL)
            su_mem_bag_pop(mx_go_data->gdc_membag, cqcp->cqc_membag_persist);
      }

      if((cp_v15compat = ok_vlook(fwdheading)) != NULL)
         n_OBSOLETE(_("please use *forward-inject-head* instead of "
            "*fwdheading*"));
      if((cp = ok_vlook(forward_inject_head)) == NIL &&
            (cp = cp_v15compat) == NIL)
         cp = n_FORWARD_INJECT_HEAD; /* v15compat: make auto-defval */
   }else if((cp = ok_vlook(quote)) == NIL && !cqcp->cqc_do_quote){
      rv = TRU1;
      goto jleave;
   }else{
      if(cqcp->cqc_add_cc && cqcp->cqc_hp != NIL && ok_blook(quote_add_cc)){
         if(cqcp->cqc_membag_persist != NIL)
            su_mem_bag_push(mx_go_data->gdc_membag, cqcp->cqc_membag_persist);
         a_collect_add_sender_to_cc(cqcp->cqc_hp, cqcp->cqc_mp);
         if(cqcp->cqc_membag_persist != NIL)
            su_mem_bag_pop(mx_go_data->gdc_membag, cqcp->cqc_membag_persist);
      }

      if(cqcp->cqc_quoteitp == NIL)
         cqcp->cqc_quoteitp = mx_IGNORE_ALL;

      if(cp == NIL || !su_cs_cmp(cp, "noheading"))
         ;
      else if(!su_cs_cmp(cp, "headers"))
         cqcp->cqc_quoteitp = mx_IGNORE_TYPE;
      /* TODO *quote*=all* series should separate the bodies visually */
      else if(!su_cs_cmp(cp, "allheaders")){
         cqcp->cqc_quoteitp = NIL;
         cqcp->cqc_action = SEND_QUOTE_ALL;
      }else if(!su_cs_cmp(cp, "allbodies")){
         cqcp->cqc_quoteitp = mx_IGNORE_ALL;
         cqcp->cqc_action = SEND_QUOTE_ALL;
      }

      if((cp = ok_vlook(quote_inject_head)) == NIL)
         cp = n_QUOTE_INJECT_HEAD; /* v15compat: make auto-defval */
   }

   /* We pass through our formatter? */
   if((cfc.cfc_fmt = cp) != NIL){
      /* TODO In v15 [-textual_-]sender_info() should only create a list
       * TODO of matching header objects, and the formatter should simply
       * TODO iterate over this list and call OBJ->to_ui_str(FLAGS) or so.
       * TODO For now fully initialize this thing once (grrrr!!) */
      cfc.cfc_fp = cqcp->cqc_fp;
      cfc.cfc_indent_prefix = NIL;/*cqcp->cqc_indent_prefix;*/
      cfc.cfc_mp = cqcp->cqc_mp;
      n_header_textual_sender_info(cfc.cfc_mp = cqcp->cqc_mp,
         (cqcp->cqc_do_quote ? NIL : cqcp->cqc_hp),
         &cfc.cfc_cumul, &cfc.cfc_addr, &cfc.cfc_real, &cfc.cfc_full, NIL);
      cfc.cfc_date = n_header_textual_date_info(cqcp->cqc_mp, NIL);
      /* C99 */{
         struct mx_name *np;
         char const *msgid;

         if((msgid = hfield1("message-id", cqcp->cqc_mp)) != NIL &&
               (np = lextract(msgid, GREF)) != NIL)
            msgid = np->n_name;
         else
            msgid = NIL;
         cfc.cfc_msgid = msgid;
      }

      if(!a_coll__fmt_inj(&cfc) || fflush(cqcp->cqc_fp))
         goto jleave;
   }

   if(sendmp(cqcp->cqc_mp, cqcp->cqc_fp, cqcp->cqc_quoteitp,
         cqcp->cqc_indent_prefix, cqcp->cqc_action, NIL) < 0)
      goto jleave;

   if(cqcp->cqc_is_forward){
      if((cp = ok_vlook(forward_inject_tail)) == NIL)
          cp = n_FORWARD_INJECT_TAIL;
   }else if((cp = ok_vlook(quote_inject_tail)) == NIL)
       cp = n_QUOTE_INJECT_TAIL;
   if((cfc.cfc_fmt = cp) != NIL &&
         (!a_coll__fmt_inj(&cfc) || fflush(cqcp->cqc_fp)))
      goto jleave;

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
}

static boole
a_coll__fmt_inj(struct a_coll_fmt_ctx const *cfcp){
   struct quoteflt qf;
   struct n_string s_b, *s;
   char c;
   char const *fmt, *cp;
   NYD_IN;

   if((fmt = cfcp->cfc_fmt) == NULL || *fmt == '\0')
      goto jleave;

   s = n_string_book(n_string_creat_auto(&s_b), 127);

   while((c = *fmt++) != '\0'){
      if(c != '%' || (c = *fmt++) == '%'){
jwrite_char:
         s = n_string_push_c(s, c);
      }else switch(c){
      case 'a':
         cp = cfcp->cfc_addr;
jwrite_cp:
         s = n_string_push_cp(s, cp);
         break;
      case 'd':
         cp = cfcp->cfc_date;
         goto jwrite_cp;
         break;
      case 'f':
         cp = cfcp->cfc_full;
         goto jwrite_cp;
      case 'i':
         if((cp = cfcp->cfc_msgid) != NIL)
            goto jwrite_cp;
         break;
      case 'n':
         cp = cfcp->cfc_cumul;
         goto jwrite_cp;
         break;
      case 'r':
         cp = cfcp->cfc_real;
         goto jwrite_cp;
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

   quoteflt_init(&qf, cfcp->cfc_indent_prefix, FAL0); /* XXX terrible wrap */
   quoteflt_reset(&qf, cfcp->cfc_fp);
   if(quoteflt_push(&qf, s->s_dat, s->s_len) < 0 || quoteflt_flush(&qf) < 0)
      cfcp = NIL;
   quoteflt_destroy(&qf);

   /*n_string_gut(s);*/
jleave:
   NYD_OU;
   return (cfcp != NIL);
}

static boole
a_coll_makeheader(FILE *fp, struct header *hp, s8 *checkaddr_err,
   boole do_delayed_due_t)
{
   FILE *nf;
   int c;
   boole rv;
   NYD_IN;

   rv = FAL0;

   if((nf = mx_fs_tmp_open(NIL, "colhead", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL){
      n_perr(_("temporary mail edit file"), 0);
      goto jleave;
   }

   n_header_extract(((do_delayed_due_t
            ? n_HEADER_EXTRACT_FULL | n_HEADER_EXTRACT_PREFILL_RECEIVERS
            : n_HEADER_EXTRACT_EXTENDED) |
         n_HEADER_EXTRACT_COMPOSE_MODE |
         n_HEADER_EXTRACT_IGNORE_SHELL_COMMENTS), fp, hp, checkaddr_err);
   if (checkaddr_err != NULL && *checkaddr_err != 0)
      goto jleave;

   /* In template mode some things have been delayed until the template has
    * been read */
   if(do_delayed_due_t){
      char const *cp;

      if((cp = ok_vlook(on_compose_enter)) != NIL){
         setup_from_and_sender(hp);
         temporary_compose_mode_hook_call(cp);
      }

      if(!a_coll_message_inject_head(nf))
         goto jleave;
   }

   while ((c = getc(fp)) != EOF) /* XXX bytewise, yuck! */
      putc(c, nf);

   if(fp != _coll_fp)
      mx_fs_close(_coll_fp);
   mx_fs_close(fp);
   _coll_fp = nf;
   nf = NIL;

   if (check_from_and_sender(hp->h_from, hp->h_sender) == NULL)
      goto jleave;

   rv = TRU1;
jleave:
   if(nf != NIL)
      mx_fs_close(nf);

   NYD_OU;
   return rv;
}

static s32
a_coll_edit(int c, struct header *hp, char const *pipecmd) /* TODO errret */
{
   struct n_sigman sm;
   FILE *nf;
   n_sighdl_t volatile sigint;
   boole saved_filrec;
   s32 volatile rv;
   NYD_IN;

   rv = su_ERR_NONE;
   saved_filrec = ok_blook(add_file_recipients);

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      sigint = safe_signal(SIGINT, SIG_IGN);
      break;
   default:
      sigint = SIG_ERR;
      rv = su_ERR_INTR;
      goto jleave;
   }

   if(!saved_filrec)
      ok_bset(add_file_recipients);

   if(hp != NIL){
      hp->h_flags |= HF_COMPOSE_MODE;
      if(hp->h_in_reply_to == NIL)
         hp->h_in_reply_to = n_header_setup_in_reply_to(hp);
   }

   rewind(_coll_fp);
   nf = n_run_editor(_coll_fp, (off_t)-1, c, FAL0, hp, NULL, SEND_MBOX, sigint,
         pipecmd);
   if(nf != NULL){
      if(hp != NULL){
         /* Overtaking of nf->_coll_fp is done by a_coll_makeheader()! */
         if(a_coll_makeheader(nf, hp, NIL, FAL0))
            hp->h_flags |= HF_USER_EDITED;
         else
            rv = su_ERR_INVAL;
      }else{
         fseek(nf, 0L, SEEK_END);
         mx_fs_close(_coll_fp);
         _coll_fp = nf;
      }
   }else
      rv = su_ERR_CHILD;

   n_sigman_cleanup_ping(&sm);
jleave:
   if(!saved_filrec)
      ok_bclear(add_file_recipients);

   if(hp != NIL)
      hp->h_flags &= ~HF_COMPOSE_MODE;

   if(sigint != SIG_ERR)
      safe_signal(SIGINT, sigint);

   NYD_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

static s32
a_coll_pipe(char const *cmd)
{
   FILE *nf;
   n_sighdl_t sigint;
   s32 rv;
   NYD_IN;

   rv = su_ERR_NONE;
   sigint = safe_signal(SIGINT, SIG_IGN);

   if((nf = mx_fs_tmp_open(NIL, "colpipe", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL){
      rv = su_err_no();
jperr:
      n_perr(_("temporary mail edit file"), rv);
      goto jout;
   }

   /* stdin = current message.  stdout = new message */
   if(fflush(_coll_fp) == EOF){
      rv = su_err_no_by_errno();
      goto jperr;
   }
   rewind(_coll_fp);

   /* C99 */{
      struct mx_child_ctx cc;

      mx_child_ctx_setup(&cc);
      cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
      cc.cc_fds[mx_CHILD_FD_IN] = fileno(_coll_fp);
      cc.cc_fds[mx_CHILD_FD_OUT] = fileno(nf);
      mx_child_ctx_set_args_for_sh(&cc, NIL, cmd);
      if(!mx_child_run(&cc) || cc.cc_exit_status != su_EX_OK){
         mx_fs_close(nf);
         rv = su_ERR_CHILD;
         goto jout;
      }
   }

   if (fsize(nf) == 0) {
      n_err(_("No bytes from %s !?\n"), n_shexp_quote_cp(cmd, FAL0));
      mx_fs_close(nf);
      rv = su_ERR_NODATA;
      goto jout;
   }

   /* Take new files */
   fseek(nf, 0L, SEEK_END);
   mx_fs_close(_coll_fp);
   _coll_fp = nf;
jout:
   safe_signal(SIGINT, sigint);
   NYD_OU;
   return rv;
}

static s32
a_coll_forward(char const *ms, FILE *fp, struct header *hp, int f){
   struct a_coll_quote_ctx cqc;
   struct su_mem_bag membag;
   int rv, *msgvec;
   NYD_IN;

   if((rv = n_getmsglist(ms, n_msgvec, 0, NIL)) < 0){
      rv = n_pstate_err_no; /* XXX not really, should be handled there! */
      goto jleave;
   }
   if(rv == 0){
      *n_msgvec = first(0, MMNORM); /* TODO integrate mode into getmsglist */
      if(*n_msgvec == 0){
         n_err(_("No appropriate messages\n"));
         rv = su_ERR_NOMSG;
         goto jleave;
      }
      rv = 1;
   }

   msgvec = n_autorec_calloc(rv +1, sizeof *msgvec);
   su_mem_copy(msgvec, n_msgvec, sizeof(*msgvec) * S(uz,rv));

   su_mem_set(&cqc, 0, sizeof cqc);
   cqc.cqc_membag_persist = su_mem_bag_top(mx_go_data->gdc_membag);
   su_mem_bag_push(mx_go_data->gdc_membag, su_mem_bag_create(&membag, 0));
   cqc.cqc_fp = fp;
   cqc.cqc_hp = hp;
   cqc.cqc_add_cc = TRU1;
   if(f != 'Q')
      cqc.cqc_is_forward = TRU1;
   else
      cqc.cqc_do_quote = TRU1;
   cqc.cqc_action = (f == 'F' || f == 'M') ? SEND_QUOTE_ALL : SEND_QUOTE;
   cqc.cqc_indent_prefix = ((f == 'F' || f == 'f' || f == 'u') ? NIL
         : ok_vlook(indentprefix));
   if(f == 'U' || f == 'u'){
      cqc.cqc_quoteitp = mx_IGNORE_ALL;
   }else if(su_cs_is_upper(f)){
      ;
   }else if((f == 'f' || f == 'F') && !ok_blook(posix))
      cqc.cqc_quoteitp = mx_IGNORE_FWD;
   else
      cqc.cqc_quoteitp = mx_IGNORE_TYPE;

   rv = 0;
   fprintf(n_stdout, A_("Interpolating:"));
   n_autorec_relax_create();

   for(; *msgvec != 0; ++msgvec){
      cqc.cqc_mp = &message[*msgvec - 1];
      touch(cqc.cqc_mp);

      fprintf(n_stdout, " %d", *msgvec);
      fflush(n_stdout);

      if(!a_coll_quote_message(&cqc)){
         rv = su_ERR_IO;
         break;
      }
      n_autorec_relax_unroll();
   }

   n_autorec_relax_gut();
   fprintf(n_stdout, "\n");

   su_mem_bag_pop(mx_go_data->gdc_membag, &membag);
   su_mem_bag_gut(&membag);

jleave:
   NYD_OU;
   return rv;
}

static void
_collint(int s)
{
   NYD; /* Signal handler */

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
   NYD; /* Signal handler */
   UNUSED(s);

   /* That makes the difference to our standard HUP handler */
   savedeadletter(_coll_fp, TRU1);
   exit(su_EX_ERR);
}

static boole
a_coll_putesc(char const *s, boole addnl, FILE *stream){
   char c1, c2;
   boole isposix;
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
   /* Execs in a fork(2)ed child  TODO if remains, global MASKs for those! */
   setvbuf(n_stdin, NULL, _IOLBF, 0);
   setvbuf(n_stdout, NULL, _IOLBF, 0);
   n_psonce &= ~(n_PSO_INTERACTIVE | n_PSO_TTYANY);
   n_pstate |= n_PS_COMPOSE_FORKHOOK;
   n_readctl_read_overlay = NULL; /* TODO need OnForkEvent! See c_readctl() */
   mx_dig_msg_read_overlay = NIL; /* TODO need OnForkEvent! See c_digmsg() */
   if(n_poption & n_PO_D_VV){
      char buf[128];

      snprintf(buf, sizeof buf, "[%d]%s", getpid(), ok_vlook(log_prefix));
      ok_vset(log_prefix, buf);
   }
   /* TODO If that uses `!' it will effectively SIG_IGN SIGINT, ...and such */
   temporary_compose_mode_hook_call(a_coll_ocs__macname);
   return 0;
}

static void
a_coll_ocs__finalize(void *vp){
   /* Note we use this for destruction upon setup errors, thus */
   n_sighdl_t opipe;
   n_sighdl_t oint;
   struct a_coll_ocs_arg **coapp, *coap;
   NYD2_IN;

   temporary_compose_mode_hook_call(R(char*,-1));

   coap = *(coapp = vp);
   *coapp = (struct a_coll_ocs_arg*)-1;

   if(coap->coa_stdin != NIL)
      mx_fs_close(coap->coa_stdin);
   else if(coap->coa_pipe[0] != -1)
      close(S(int,coap->coa_pipe[0]));

   if(coap->coa_stdout != NIL && !mx_fs_pipe_close(coap->coa_stdout, TRU1))
      *coap->coa_senderr = 1;
   if(coap->coa_pipe[1] != -1)
      close(S(int,coap->coa_pipe[1]));

   opipe = coap->coa_opipe;
   oint = coap->coa_oint;

   n_lofi_free(coap);

   mx_sigs_all_holdx();
   safe_signal(SIGPIPE, opipe);
   safe_signal(SIGINT, oint);
   mx_sigs_all_rele();
   NYD2_OU;
}

FL FILE *
n_collect(enum n_mailsend_flags msf, struct header *hp, struct message *mp,
   char const *quotefile, s8 *checkaddr_err)
{
   enum{
      a_NONE,
      a_ERREXIT = 1u<<0,
      a_IGNERR = 1u<<1, /* - modifier */
#define a_HARDERR() ((flags & (a_ERREXIT | a_IGNERR)) == a_ERREXIT)

      a_EVAL = 1u<<8, /* $ modifier */

      a_MODIFIER_MASK = a_IGNERR | a_EVAL,

      a_COAP_NOSIGTERM = 1u<<9,

      a_NEED_INJECT_RESTART = 1u<<16,
      a_CAN_DELAY_INJECT = 1u<<17,
      a_EVER_LEFT_INPUT_LOOPS = 1u<<18,

      a_ROUND_MASK = a_NEED_INJECT_RESTART | a_CAN_DELAY_INJECT |
            a_EVER_LEFT_INPUT_LOOPS
   };

   struct mx_dig_msg_ctx dmc;
   struct n_string s_b, * volatile s;
   struct a_coll_ocs_arg *coap;
   int c;
   int volatile gfield, eofcnt, getfields;
   char volatile escape;
   char *linebuf;
   char const *cp, * volatile coapm, * volatile ifs_saved;
   uz i, linesize;
   long cnt;
   sigset_t oset, nset;
   u32 volatile flags;
   FILE * volatile sigfp;
   NYD_IN;

   mx_DIG_MSG_COMPOSE_CREATE(&dmc, hp);
   _coll_fp = NULL;

   sigfp = NULL;
   flags = a_CAN_DELAY_INJECT;
   eofcnt = 0;
   ifs_saved = coapm = NULL;
   coap = NULL;
   s = NULL;

   mx_fs_linepool_aquire(&linebuf, &linesize);

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

   if((_coll_fp = mx_fs_tmp_open(NIL, "collect", (mx_FS_O_RDWR |
            mx_FS_O_UNLINK), NIL)) == NIL){
      n_perr(_("collect: temporary mail file"), 0);
      goto jerr;
   }

   /* If we are going to prompt for a subject, refrain from printing a newline
    * after the headers (since some people mind) */
   getfields = 0;
   if(!(n_poption & n_PO_t_FLAG)){
      gfield = GTO | GSUBJECT | GCC | GBCC | GNL;
      if(ok_blook(fullnames))
         gfield |= GCOMMA;

      if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
         if(hp->h_subject == NIL && ok_blook(asksub)/* *ask* auto warped! */)
            gfield &= ~(GSUBJECT | GNL), getfields |= GSUBJECT;

         if(hp->h_to == NULL)
            gfield &= ~(GTO | GNL), getfields |= GTO;

         if(!ok_blook(bsdcompat) && !ok_blook(askatend)){
            if(ok_blook(askbcc))
               gfield &= ~(GBCC | GNL), getfields |= GBCC;
            if(ok_blook(askcc))
               gfield &= ~(GCC | GNL), getfields |= GCC;
         }
      }
   }else{
      UNINIT(gfield, 0);
   }

   _coll_hadintr = 0;

   if (!sigsetjmp(_coll_jmp, 1)) {
      /* Ask for some headers first, as necessary */
      if(getfields)
         grab_headers(mx_GO_INPUT_CTX_COMPOSE, hp, getfields, 1);

      /* Execute compose-enter; delayed for -t mode */
      if(!(n_poption & n_PO_t_FLAG) &&
            (cp = ok_vlook(on_compose_enter)) != NIL){
         setup_from_and_sender(hp);
         temporary_compose_mode_hook_call(cp);
      }

      /* TODO Mm: nope: it may require turning this into a multipart one */
      if(!(n_poption & (n_PO_Mm_FLAG | n_PO_t_FLAG))){
         if(!a_coll_message_inject_head(_coll_fp))
            goto jerr;

         /* Quote an original message */
         if(mp != NIL){
            struct a_coll_quote_ctx cqc;

            su_mem_set(&cqc, 0, sizeof cqc);
            cqc.cqc_fp = _coll_fp;
            cqc.cqc_hp = hp;
            if(msf & n_MAILSEND_IS_FWD){
               cqc.cqc_quoteitp = mx_IGNORE_FWD;
               cqc.cqc_add_cc = cqc.cqc_is_forward = TRU1;
            }else{
               cqc.cqc_quoteitp = mx_IGNORE_ALL;
               cqc.cqc_indent_prefix = ok_vlook(indentprefix);
            }
            cqc.cqc_action = SEND_QUOTE;
            cqc.cqc_mp = mp;

            if(!a_coll_quote_message(&cqc))
               goto jerr;
         }
      }

      if (quotefile != NULL) {
         if((n_pstate_err_no = a_coll_include_file(quotefile, FAL0, FAL0)
               ) != su_ERR_NONE)
            goto jerr;
      }

      if(n_psonce & n_PSO_INTERACTIVE){
         if(!(n_pstate & n_PS_ROBOT)){
            s = n_string_creat_auto(&s_b);
            s = n_string_reserve(s, 80);
         }

         if(!(n_poption & n_PO_Mm_FLAG) && !(n_pstate & n_PS_ROBOT)){
            /* Print what we have sofar also on the terminal (if useful) */
            if(mx_go_input_have_injections()){
               flags |= a_NEED_INJECT_RESTART;
               flags &= ~a_CAN_DELAY_INJECT;
            }else{
jinject_restart:
               if((cp = ok_vlook(editalong)) == NIL){
                  if(msf & n_MAILSEND_HEADERS_PRINT)
                     n_puthead(TRU1, hp, n_stdout, gfield, SEND_TODISP,
                        CONV_NONE, NIL, NIL);

                  rewind(_coll_fp);
                  while((c = getc(_coll_fp)) != EOF) /* XXX bytewise, yuck! */
                     putc(c, n_stdout);
                  if(fseek(_coll_fp, 0, SEEK_END))
                     goto jerr;
                  fflush(n_stdout);
               }else{
                  if(a_coll_edit(((*cp == 'v') ? 'v' : 'e'), hp, NIL
                        ) != su_ERR_NONE)
                     goto jerr;

                  /* Print msg mandated by the Mail Reference Manual */
jcont:
                  if((n_psonce & n_PSO_INTERACTIVE) &&
                        !(n_pstate & n_PS_ROBOT) &&
                        !(flags & a_NEED_INJECT_RESTART))
                     fputs(_("(continue)\n"), n_stdout);
                  fflush(n_stdout);
               }
            }
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
   if(coap == NIL){
      /* We're done with -M or -m TODO because: we are too stupid yet, above */
      if(n_poption & n_PO_Mm_FLAG)
         goto jout;
      /* No command escapes, interrupts not expected? */
      if(!(n_psonce & n_PSO_INTERACTIVE) &&
            !(n_poption & (n_PO_t_FLAG | n_PO_TILDE_FLAG))){
         /* Need to go over mx_go_input() to handle injections nonetheless */
         BITENUM_IS(u32,mx_go_input_flags) gif;

         ASSERT(!(flags & a_NEED_INJECT_RESTART));
         for(gif = mx_GO_INPUT_CTX_COMPOSE | mx_GO_INPUT_DELAY_INJECTIONS;;){
            cnt = mx_go_input(gif, su_empty, &linebuf, &linesize, NIL, NIL);
            if(cnt < 0){
               if(!mx_go_input_is_eof())
                  goto jerr;
               if(mx_go_input_have_injections()){
                  gif &= ~mx_GO_INPUT_DELAY_INJECTIONS;
                  continue;
               }
               break;
            }

            i = S(uz,cnt);
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

      escape = *ok_vlook(escape);
   }

   /* The "interactive" collect loop */
   flags &= a_ROUND_MASK;
   if(ok_blook(errexit))
      flags |= a_ERREXIT;

   for(;;){
      u32 eval_cnt;
      enum {a_HIST_NONE, a_HIST_ADD = 1u<<0, a_HIST_GABBY = 1u<<1} hist;

      /* C99 */{
         BITENUM_IS(u32,mx_go_input_flags) gif;
         boole histadd;

         /* TODO optimize: no need to evaluate that anew for each loop tick! */
         histadd = (s != NIL);
         gif = mx_GO_INPUT_CTX_COMPOSE;
         if(flags & a_CAN_DELAY_INJECT)
            gif |= mx_GO_INPUT_DELAY_INJECTIONS;

         if((n_poption & n_PO_t_FLAG) && !(n_psonce & n_PSO_t_FLAG_DONE)){
            ASSERT(!(flags & a_NEED_INJECT_RESTART));
         }else{
            if(n_psonce & n_PSO_INTERACTIVE){
               gif |= mx_GO_INPUT_NL_ESC;
               if(UNLIKELY((flags & a_NEED_INJECT_RESTART) &&
                     !mx_go_input_have_injections())){
                  flags &= ~a_NEED_INJECT_RESTART;
                  goto jinject_restart;
               }

               /* We want any error to appear once for each tick */
               n_pstate |= n_PS_ERRORS_NEED_PRINT_ONCE;
            }else{
               ASSERT(!(flags & a_NEED_INJECT_RESTART));
               if(n_poption & n_PO_TILDE_FLAG)
                  gif |= mx_GO_INPUT_NL_ESC;
            }
         }

         cnt = mx_go_input(gif, n_empty, &linebuf, &linesize, NIL, &histadd);
         hist = histadd ? a_HIST_ADD | a_HIST_GABBY : a_HIST_NONE;
      }

      if(cnt < 0){ /* TODO mx_go_input_is_eof()!  Could be error!! */
         if(coap != NIL)
            break;

         if((n_poption & n_PO_t_FLAG) && !(n_psonce & n_PSO_t_FLAG_DONE)){
            fflush_rewind(_coll_fp);
            n_psonce |= n_PSO_t_FLAG_DONE;
            flags &= ~a_CAN_DELAY_INJECT;
            if(!a_coll_makeheader(_coll_fp, hp, checkaddr_err, TRU1))
               goto jerr;
            continue;
         }

         if((flags & a_CAN_DELAY_INJECT) && mx_go_input_have_injections()){
            flags &= ~a_CAN_DELAY_INJECT;
            continue;
         }

         if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT) &&
               ok_blook(ignoreeof) && ++eofcnt < 4){
            fprintf(n_stdout,
               _("*ignoreeof* set, use `~.' to terminate letter\n"));
            mx_go_input_clearerr();
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
         if(fwrite(cp, sizeof *cp, cnt, _coll_fp) != S(uz,cnt))
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

      if(--cnt == 0)
         goto jearg;
      c = *++cp;

      /* It may just be an escaped escaped character, do that quick */
      if(c == escape)
         goto jputline;

      /* Skip leading whitespace, if we see any: no history entry */
      while(su_cs_is_space(c)){
         hist = a_HIST_NONE;
         if(--cnt == 0)
            goto jearg;
         c = *++cp;
      }

      /* Avoid hard *errexit*, evaluate modifier ? */
      eval_cnt = 0;
      flags &= ~a_MODIFIER_MASK;
      for(;;){
         if(c == '-')
            flags |= a_IGNERR;
         else if(c == '$'){
            flags |= a_EVAL;
            ++eval_cnt;
         }else if(su_cs_is_space(c)){
            /* Must have seen modifier, ok */;
         }else
            break;
         if(--cnt == 0)
            goto jearg;
         c = *++cp;
      }

      /* Trim whitespace, also for a somewhat normalized history entry.
       * Also step cp over the command */
      if(flags & a_MODIFIER_MASK){
         while(su_cs_is_space(c)){
            if(--cnt == 0)
               goto jearg;
            c = *++cp;
         }
      }
      if(cnt > 0){
         ++cp;
         while(--cnt > 0 && su_cs_is_space(*cp))
            ++cp;
         while(cnt > 0 && su_cs_is_space(cp[cnt - 1]))
            --cnt;
         UNCONST(char*,cp)[cnt] = '\0';
      }

      /* Prepare history entry */
      if(hist != a_HIST_NONE){
         s = n_string_assign_c(s, escape);
         if(flags & a_IGNERR)
            s = n_string_push_c(s, '-');
         if(flags & a_EVAL){
            for(i = eval_cnt; i-- != 0;)
               s = n_string_push_c(s, '$');
         }
         s = n_string_push_c(s, c);
         if(cnt > 0){
            s = n_string_push_c(s, ' ');
            s = n_string_push_buf(s, cp, cnt);
         }
      }

      /* This may be an eval request */
      if((flags & a_EVAL) && cnt > 0){
         struct str io;

         io.s = UNCONST(char*,cp);
         io.l = S(uz,cnt);
         if(!mx_cmd_eval(&io, eval_cnt, NIL))
            goto jearg;
         cp = io.s;
         cnt = S(long,io.l);
      }

      /* Switch over all command escapes */
      switch(c){
      default:
         if(1){
            char buf[sizeof(su_UTF8_REPLACER)];

            if(su_cs_is_ascii(c))
               buf[0] = c, buf[1] = '\0';
            else if(n_psonce & n_PSO_UNICODE)
               su_mem_copy(buf, su_utf8_replacer,
                  sizeof su_utf8_replacer);
            else
               buf[0] = '?', buf[1] = '\0';
            n_err(_("Unknown command escape: `%c%s'\n"), escape, buf);
         }else
jearg:
            n_err(_("Invalid command escape usage: %s\n"),
               n_shexp_quote_cp(linebuf, FAL0));
         if(a_HARDERR())
            goto jerr;
         n_pstate_err_no = su_ERR_INVAL;
         n_pstate_ex_no = 1;
         continue;
      case '!':
         /* Shell escape, send the balance of line to sh -c */
         if(cnt == 0 || coap != NULL)
            goto jearg;
         else{
            /* TODO should NOT call c_shell() directly */
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
#ifdef mx_HAVE_UISTRINGS
         fputs(_(
"COMMAND ESCAPES (to be placed after a newline; excerpt).\n"
"~: <command>  Execute command\n"
"~< <file>     Insert <file> (also: ~<! <shellcmd>)\n"
"~@ [<files>]  Edit [Add] attachments (file[=in-charset[#out-charset]], #no)\n"
"~| <shellcmd> Pipe message through shell filter (~||: with headers)\n"
"~^ help       Help for ^ (`digmsg') message control\n"
"~c <users>    Add receivers to Cc: (~b: Bcc:) list\n"
"~e, ~v        Edit message via $EDITOR / $VISUAL\n"
            ), n_stdout);
         fputs(_(
"~F <msglist>  Insert with (~f: `headerpick'ed) headers\n"
"~H            Edit From:, Reply-To: and Sender:\n"
"~h            Prompt for Subject:, To:, Cc: and Bcc:\n"
"~i <variable> Insert value, (~I: do not) append a newline\n"
"~M <msglist>  Insert with (~m: `headerpick'ed) headers, use *indentprefix*\n"
"~p            Print message content\n"
"~Q <msglist>  Insert with *quote* algorithm\n"
            ), n_stdout);
         fputs(_(
"~r <file>     Insert <file> / <- [HERE-DELIM]> (~R: use *indentprefix*)\n"
"~s <subject>  Set Subject:\n"
"~t <users>    Add receivers to To: list\n"
"~u <msglist>  Insert without headers (~U: use *indentprefix*)\n"
"~w <file>     Write message onto file\n"
"~x, ~q, ~.    Discard, discard and save to $DEAD, send message\n"
"Modifiers: - (ignerr), $ (evaluate), e.g: \"~- $ @ $TMPDIR/file\"\n"
            ), n_stdout);
#endif /* mx_HAVE_UISTRINGS */
         if(cnt != 0)
            goto jearg;
         n_pstate_err_no = su_ERR_NONE;
         n_pstate_ex_no = 0;
         break;
      case '@':{
         struct mx_attachment *aplist;

         /* Edit the attachment list */
         aplist = hp->h_attach;
         hp->h_attach = NIL;
         if(cnt != 0)
            hp->h_attach = mx_attachments_append_list(aplist, cp);
         else
            hp->h_attach = mx_attachments_list_edit(aplist,
                  mx_GO_INPUT_CTX_COMPOSE);
         n_pstate_err_no = su_ERR_NONE; /* XXX ~@ does NOT handle $!/$?! */
         n_pstate_ex_no = 0; /* XXX */
         }break;
      case '^':
         if(!mx_dig_msg_circumflex(&dmc, n_stdout, cp)){
            if(ferror(_coll_fp))
               goto jerr;
            goto jearg;
         }
         n_pstate_err_no = su_ERR_NONE; /* XXX */
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
         if((n_pstate_err_no = a_coll_pipe(cp)) == su_ERR_NONE)
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
            struct mx_name *np;
            s8 soe;

            soe = 0;
            if((np = checkaddrs(lextract(cp, GBCC | GFULL), EACM_NORMAL, &soe)
                  ) != NULL)
               hp->h_bcc = cat(hp->h_bcc, np);
            if(soe == 0){
               n_pstate_err_no = su_ERR_NONE;
               n_pstate_ex_no = 0;
            }else{
               n_pstate_ex_no = 1;
               n_pstate_err_no = (soe < 0) ? su_ERR_PERM : su_ERR_INVAL;
            }
         }
         hist &= ~a_HIST_GABBY;
         break;
      case 'c':
         /* Add to the CC list TODO join 'b' */
         if(cnt == 0)
            goto jearg;
         else{
            struct mx_name *np;
            s8 soe;

            soe = 0;
            if((np = checkaddrs(lextract(cp, GCC | GFULL), EACM_NORMAL, &soe)
                  ) != NULL)
               hp->h_cc = cat(hp->h_cc, np);
            if(soe == 0){
               n_pstate_err_no = su_ERR_NONE;
               n_pstate_ex_no = 0;
            }else{
               n_pstate_ex_no = 1;
               n_pstate_err_no = (soe < 0) ? su_ERR_PERM : su_ERR_INVAL;
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
            if(cnt > 0 && c == '<' && *cp == '!'){
               /* TODO hist. normalization */
               if((n_pstate_err_no = a_coll_insert_cmd(_coll_fp, ++cp)
                     ) != su_ERR_NONE){
                  if(ferror(_coll_fp))
                     goto jerr;
                  if(a_HARDERR())
                     goto jerr;
                  n_pstate_ex_no = 1;
                  break;
               }
               goto jhistcont;
            }else if(c != '<' && *cp == '-' &&
                  (cp[1] == '\0' || su_cs_is_space(cp[1]))){
               /* xxx ugly special treatment for HERE-delimiter stuff */
            }else{
               struct n_string s2, *s2p = &s2;

               s2p = n_string_creat_auto(s2p);

               if(n_shexp_unquote_one(s2p, cp) != TRU1){
                  n_err(_("Interpolate what file?\n"));
                  if(a_HARDERR())
                     goto jerr;
                  n_pstate_err_no = su_ERR_NOENT;
                  n_pstate_ex_no = 1;
                  break;
               }

               cp = n_string_cp(s2p);

               if((cp = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL | FEXP_NVAR))
                     ) == NIL){
                  if(a_HARDERR())
                     goto jerr;
                  n_pstate_err_no = su_ERR_INVAL;
                  n_pstate_ex_no = 1;
                  break;
               }

               /*n_string_gut(s2p);*/
            }
         }

         /* XXX race, and why not test everywhere, then? */
         if(su_path_is_dir(cp, FAL0)){
            n_err(_("%s: is a directory\n"), n_shexp_quote_cp(cp, FAL0));
            if(a_HARDERR())
               goto jerr;
            n_pstate_err_no = su_ERR_ISDIR;
            n_pstate_ex_no = 1;
            break;
         }

         if((n_pstate_err_no = a_coll_include_file(cp, (c == 'R'), TRU1)
               ) != su_ERR_NONE){
            if(ferror(_coll_fp))
               goto jerr;
            if(a_HARDERR())
               goto jerr;
            n_pstate_ex_no = 1;
            break;
         }
         n_pstate_err_no = su_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      case 'e':
      case 'v':
         /* Edit the current message.  'e' -> use EDITOR, 'v' -> use VISUAL */
         if(cnt != 0 || coap != NULL)
            goto jearg;
jev_go:
         if((n_pstate_err_no = a_coll_edit(c,
                ((c == '|' || ok_blook(editheaders)) ? hp : NIL), cp)
               ) == su_ERR_NONE)
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
         /* Interpolate the named messages */
         if((n_pstate_err_no = a_coll_forward(cp, _coll_fp, hp, c)
               ) == su_ERR_NONE)
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
         if(!(n_psonce & n_PSO_INTERACTIVE)){
            n_pstate_err_no = su_ERR_NOTTY;
            n_pstate_ex_no = 1;
            break;
         }
         do
            grab_headers(mx_GO_INPUT_CTX_COMPOSE, hp, GEXTRA, 0);
         while(check_from_and_sender(hp->h_from, hp->h_sender) == NULL);
         n_pstate_err_no = su_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      case 'h':
         /* Grab a bunch of headers */
         if(cnt != 0)
            goto jearg;
         if(!(n_psonce & n_PSO_INTERACTIVE)){
            n_pstate_err_no = su_ERR_NOTTY;
            n_pstate_ex_no = 1;
            break;
         }
         do
            grab_headers(mx_GO_INPUT_CTX_COMPOSE, hp,
              (GTO | GSUBJECT | GCC | GBCC),
              (ok_blook(bsdcompat) && ok_blook(bsdorder)));
         while(hp->h_to == NULL);
         n_pstate_err_no = su_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      case 'I':
      case 'i':
         /* Insert a variable into the file */
         if(cnt == 0)
            goto jearg;
         cp = n_var_vlook(cp, TRU1);
jIi_putesc:
         n_pstate_err_no = su_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         if(cp == NULL || *cp == '\0'){
            break;
         }
         if(!a_coll_putesc(cp, (c != 'I'), _coll_fp))
            goto jerr;
         if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT) &&
               (!a_coll_putesc(cp, (c != 'I'), n_stdout) ||
                fflush(n_stdout) == EOF))
            goto jerr;
         break;
      /* case 'M': <> 'F' */
      /* case 'm': <> 'f' */
      case 'p':
         /* Print current state of the message without altering anything */
         if(cnt != 0)
            goto jearg;
         if(!a_coll_print(_coll_fp, hp) || ferror(_coll_fp))
            goto jerr; /* XXX pstate_err_no ++ */
         n_pstate_err_no = su_ERR_NONE; /* XXX */
         n_pstate_ex_no = 0; /* XXX */
         break;
      /* case 'Q': <> 'F' */
      case 'q':
      case 'x':
         /* Force a quit, act like an interrupt had happened */
         if(cnt != 0)
            goto jearg;
         /* If we are running a splice hook, assume it quits on its own now,
          * otherwise we (no true out-of-band IPC to signal this state, XXX)
          * have to SIGTERM it in order to stop this wild beast */
         flags |= a_COAP_NOSIGTERM;
         ++_coll_hadintr;
         _collint((c == 'x') ? 0 : SIGINT);
         exit(su_EX_ERR);
         /*NOTREACHED*/
      /* case 'R': <> 'd' */
      /* case 'r': <> 'd' */
      case 's':
         /* Set the Subject list */
         if(cnt == 0)
            goto jearg;
         /* Subject:; take care for Debian #419840 and strip any \r and \n */
         if(su_cs_first_of(hp->h_subject = savestr(cp), "\n\r") != su_UZ_MAX){
            char *xp;

            n_err(_("-s: normalizing away invalid ASCII NL / CR bytes\n"));
            for(xp = hp->h_subject; *xp != '\0'; ++xp)
               if(*xp == '\n' || *xp == '\r')
                  *xp = ' ';
            n_pstate_err_no = su_ERR_INVAL;
            n_pstate_ex_no = 1;
         }else{
            n_pstate_err_no = su_ERR_NONE;
            n_pstate_ex_no = 0;
         }
         break;
      case 't':
         /* Add to the To: list TODO join 'b', 'c' */
         if(cnt == 0)
            goto jearg;
         else{
            struct mx_name *np;
            s8 soe;

            soe = 0;
            if((np = checkaddrs(lextract(cp, GTO | GFULL), EACM_NORMAL, &soe)
                  ) != NULL)
               hp->h_to = cat(hp->h_to, np);
            if(soe == 0){
               n_pstate_err_no = su_ERR_NONE;
               n_pstate_ex_no = 0;
            }else{
               n_pstate_ex_no = 1;
               n_pstate_err_no = (soe < 0) ? su_ERR_PERM : su_ERR_INVAL;
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
         if((cp = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL)
               )) == NIL){
            n_err(_("Write what file!?\n"));
            if(a_HARDERR())
               goto jerr;
            n_pstate_err_no = su_ERR_INVAL;
            n_pstate_ex_no = 1;
            break;
         }
         rewind(_coll_fp);
         if((n_pstate_err_no = a_coll_write(cp, _coll_fp, 1)) == su_ERR_NONE)
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
         mx_tty_addhist(&n_string_cp(s)[1], (mx_GO_INPUT_CTX_COMPOSE |
            (hist & a_HIST_GABBY ? mx_GO_INPUT_HIST_GABBY : mx_GO_INPUT_NONE) |
            (n_pstate_err_no == su_ERR_NONE ? mx_GO_INPUT_NONE
               : mx_GO_INPUT_HIST_GABBY | mx_GO_INPUT_HIST_ERROR)));
      }
      if(c != '\0')
         goto jcont;
   }

jout:
   flags |= a_EVER_LEFT_INPUT_LOOPS;

   /* Do we have *on-compose-splice-shell*, or *on-compose-splice*?
    * TODO Usual f...ed up state of signals and terminal etc. */
   if(coap == NULL && (cp = ok_vlook(on_compose_splice_shell)) != NULL) Jocs:{
      union {int (*ptf)(void); char const *sh;} u;
      char const *cmd;

      /* Reset *escape* and more to their defaults. On change update manual! */
      if(ifs_saved == NULL)
         ifs_saved = savestr(ok_vlook(ifs));
      ok_vclear(ifs);
      escape = n_ESCAPE[0];
      flags &= ~a_CAN_DELAY_INJECT;

      if(coapm != NULL){
         /* XXX Due pipe_open() fflush(NIL) in PTF mode */
         /*if(!n_real_seek(_coll_fp, 0, SEEK_END))
          *  goto jerr;*/
         u.ptf = &a_coll_ocs__mac;
         cmd = (char*)-1;
         a_coll_ocs__macname = cp = coapm;
      }else{
         u.sh = ok_vlook(SHELL);
         cmd = cp;
      }

      i = su_cs_len(cp) +1;
      coap = su_LOFI_ALLOC(VSTRUCT_SIZEOF(struct a_coll_ocs_arg,coa_cmd) + i);
      coap->coa_pipe[0] = coap->coa_pipe[1] = -1;
      coap->coa_stdin = coap->coa_stdout = NIL;
      coap->coa_senderr = checkaddr_err;
      su_mem_copy(coap->coa_cmd, cp, i);

      mx_sigs_all_holdx();
      coap->coa_opipe = safe_signal(SIGPIPE, SIG_IGN);
      coap->coa_oint = safe_signal(SIGINT, SIG_IGN);
      mx_sigs_all_rele();

      if(mx_fs_pipe_cloexec(coap->coa_pipe) &&
            (coap->coa_stdin = mx_fs_fd_open(coap->coa_pipe[0],
                  mx_FS_O_RDONLY)) != NIL &&
            (coap->coa_stdout = mx_fs_pipe_open(cmd, mx_FS_PIPE_WRITE, u.sh,
                  NIL, coap->coa_pipe[1])) != NIL){
         close(S(int,coap->coa_pipe[1]));
         coap->coa_pipe[1] = -1;

         temporary_compose_mode_hook_call(NIL);
         mx_go_splice_hack(coap->coa_cmd, coap->coa_stdin, coap->coa_stdout,
            (n_psonce & ~(n_PSO_INTERACTIVE | n_PSO_TTYANY)),
            &a_coll_ocs__finalize, &coap);
         /* Hook version protocol for ~^: update manual upon change! */
         fputs(mx_DIG_MSG_PLUMBING_VERSION "\n", n_stdout/*coap->coa_stdout*/);
         goto jcont;
      }

      c = su_err_no();
      a_coll_ocs__finalize(coap);
      n_perr(_("Cannot invoke *on-compose-splice(-shell)?*"), c);
      goto jerr;
   }
   if(*checkaddr_err != 0){
      *checkaddr_err = 0;
      goto jerr;
   }
   if(coapm == NIL && (coapm = ok_vlook(on_compose_splice)) != NIL)
      goto Jocs;
   if(coap != NIL && ifs_saved != NIL){
      ok_vset(ifs, ifs_saved);
      ifs_saved = NIL;
   }

   /*
    * Note: the order of the following steps is documented for `~.'.
    * Adjust the manual on change!!
    */

   /* Final chance to edit headers, if not already above; and *asksend* */
   if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
      if(ok_blook(bsdcompat) || ok_blook(askatend)){
         gfield = GNONE;
         if(ok_blook(askcc))
            gfield |= GCC;
         if(ok_blook(askbcc))
            gfield |= GBCC;
         if(gfield != GNONE)
            grab_headers(mx_GO_INPUT_CTX_COMPOSE, hp, gfield, 1);
      }

      if(ok_blook(askattach))
         hp->h_attach = mx_attachments_list_edit(hp->h_attach,
               mx_GO_INPUT_CTX_COMPOSE);

      if(ok_blook(asksend)){
         boole b;

         ifs_saved = coapm = NULL;
         coap = NULL;

         fprintf(n_stdout,
            _("-------\n(Preliminary) Envelope contains:\n")); /* XXX */
         if(!n_puthead(TRU1, hp, n_stdout,
               GIDENT | GREF_IRT  | GSUBJECT | GTO | GCC | GBCC | GBCC_IS_FCC |
               GCOMMA, SEND_TODISP, CONV_NONE, NULL, NULL))
            goto jerr;

jreasksend:
         if(mx_go_input(mx_GO_INPUT_CTX_COMPOSE | mx_GO_INPUT_NL_ESC,
               _("Send this message [yes/no, empty: recompose]? "),
               &linebuf, &linesize, NULL, NULL) < 0){
            if(!mx_go_input_is_eof())
               goto jerr;
            cp = n_1;
         }

         if((b = n_boolify(linebuf, UZ_MAX, TRUM1)) < FAL0)
            goto jreasksend;
         if(b == TRU2)
            goto jcont;
         if(!b)
            goto jerr;
      }
   }

   /* Execute compose-leave */
   if((cp = ok_vlook(on_compose_leave)) != NIL){
      setup_from_and_sender(hp);
      temporary_compose_mode_hook_call(cp);
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

      if((cpq = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
            ) == NIL){
         n_err(_("*signature* expands to invalid file: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         goto jerr;
      }
      cpq = n_shexp_quote_cp(cp = cpq, FAL0);

      if((sigfp = mx_fs_open(cp, mx_FS_O_RDONLY)) == NIL){
         n_err(_("Can't open *signature* %s: %s\n"), cpq, su_err_doc(-1));
         goto jerr;
      }

      if(linebuf == NIL)
         linebuf = su_ALLOC(linesize = mx_LINESIZE);

      c = '\0';
      while((i = fread(linebuf, sizeof *linebuf, linesize,
            UNVOLATILE(FILE*,sigfp))) > 0){
         c = linebuf[i - 1];
         if(i != fwrite(linebuf, sizeof *linebuf, i, _coll_fp))
            goto jerr;
      }

      /* C99 */{
         FILE *x = UNVOLATILE(FILE*,sigfp);
         int e = su_err_no_by_errno(), ise = ferror(x);

         sigfp = NULL;
         mx_fs_close(x);

         if(ise){
            n_err(_("Errors while reading *signature* %s: %s\n"),
               cpq, su_err_doc(e));
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

   if(mp != NIL && ok_blook(quote_as_attachment)){
      struct mx_attachment *ap;

      ap = n_autorec_calloc(1, sizeof *ap);
      if((ap->a_flink = hp->h_attach) != NIL)
         hp->h_attach->a_blink = ap;
      hp->h_attach = ap;
      ap->a_msgno = S(int,P2UZ(mp - message + 1));
      ap->a_content_description =
            ok_vlook(content_description_quote_attachment);
   }

jleave:
   sigprocmask(SIG_BLOCK, &nset, NULL);
   mx_fs_linepool_release(linebuf, linesize);
   mx_DIG_MSG_COMPOSE_GUT(&dmc);
   n_pstate &= ~n_PS_COMPOSE_MODE;
   safe_signal(SIGINT, _coll_saveint);
   safe_signal(SIGHUP, _coll_savehup);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD_OU;
   return _coll_fp;

jerr:
   mx_sigs_all_holdx();

   if(coap != NULL && coap != (struct a_coll_ocs_arg*)-1){
      if(!(flags & a_COAP_NOSIGTERM))
         mx_fs_pipe_signal(coap->coa_stdout, SIGTERM);
      mx_go_splice_hack_remove_after_jump();
      coap = NULL;
   }
   if(ifs_saved != NULL){
      ok_vset(ifs, ifs_saved);
      ifs_saved = NULL;
   }
   if(sigfp != NIL){
      mx_fs_close(UNVOLATILE(FILE*,sigfp));
      sigfp = NULL;
   }
   if(_coll_fp != NIL){
      mx_fs_close(_coll_fp);
      _coll_fp = NIL;
   }

   mx_sigs_all_rele();

   ASSERT(checkaddr_err != NULL);
   /* TODO We don't save in $DEAD upon error because msg not readily composed?
    * TODO This is no good, it should go ZOMBIE / DRAFT / POSTPONED or what! */
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

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_COLLECT
/* s-it-mode */
