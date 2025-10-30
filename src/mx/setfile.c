/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ setfile() -- TODO oh my god; needs to be converted to URL:: and Mailbox::
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2025 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Copyright (c) 1980, 1993 The Regents of the University of California.
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
#define su_FILE setfile
#define mx_SOURCE
#define mx_SOURCE_SETFILE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <pwd.h>

#include <su/cs.h>
#include <su/mem.h>
#include <su/path.h>

#include "mx/cmd-shortcut.h"
#include "mx/dig-msg.h"
#include "mx/fexpand.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/net-pop3.h"
#include "mx/net-socket.h"
#include "mx/okeys.h"
#include "mx/sigs.h"
#include "mx/time.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

FL int
setfile(char const *name, enum fedit_mode fm)
{
   static int shudclob;

   enum{
      a_NONE,
      a_DEVNULL = 1u<<0,
      a_STDIN = 1u<<1,

      a_SPECIALS_MASK = a_DEVNULL | a_STDIN
   };

   struct su_pathinfo pi;
   uz offset;
   char const *who;
   enum protocol proto;
   int rv, err, omsgCount;
   FILE *ibuf, *lckfp;
   u8 flags;
   NYD_IN;

   n_pstate &= ~n_PS_SETFILE_OPENED;
   flags = a_NONE;
   ibuf = lckfp = NIL;
   rv = -1;
   err = su_ERR_NONE;
   omsgCount = 0;

   if(n_poption & n_PO_R_FLAG)
      fm |= FEDIT_RDONLY;

   /* C99 */{
      enum mx_fexp_mode fexpm;

      if((who = mx_shortcut_expand(name)) != NIL){
         fexpm = mx_FEXP_NVAR;
         name = who;
      }else
         fexpm = mx_FEXP_SHORTCUT | mx_FEXP_NSHELL;

      if(name[0] == '%'){
         char const *cp;

         fm |= FEDIT_SYSBOX; /* TODO fexpand() needs to tell is-valid-user! */
         if(*(who = &name[1]) == ':')
            ++who;
         if((cp = su_cs_rfind_c(who, '/')) != NULL)
            who = &cp[1];
         if(*who == '\0')
            goto jlogname;
      }else{
jlogname:
         if(fm & FEDIT_ACCOUNT){
            if((who = ok_vlook(account)) == NULL)
               who = ACCOUNT_NULL;
            who = savecatsep(_("account"), ' ', who);
         }else
            who = ok_vlook(LOGNAME);
      }

      if(!(fm & FEDIT_SYSBOX)){
         char const *cp;

         if((((cp = ok_vlook(inbox)) != NIL && *cp != '\0') ||
                  (cp = ok_vlook(MAIL)) != NIL) &&
               !su_cs_cmp(cp, name))
            fm |= FEDIT_SYSBOX;
      }

      if((name = mx_fexpand(name, fexpm)) == NIL){
         ASSERT(rv == -1);
         goto jleave;
      }
   }

   /* For at least substdate() users TODO -> eventloop tick */
   mx_time_current_update(NIL, FAL0);

   /* v15-compat: which_protocol(): no auto-completion */
   /* C99 */{
      char const *orig_name;

      orig_name = name;
      proto = which_protocol(name, TRU1, TRU1, &name);

      switch(proto){
      case n_PROTO_EML:
         if(!(fm & FEDIT_RDONLY) || (fm & ~(FEDIT_RDONLY | FEDIT_MAIN))){
            n_err(_("Sorry, for now eml:// files only work read-only: %s\n"),
               orig_name);
            goto jeperm;
         }
         FALLTHRU
      case n_PROTO_SMBOX:
         FALLTHRU
      case n_PROTO_XMBOX:
         FALLTHRU
      case n_PROTO_FILE:
         if(name[1] == '\0' && name[0] == '-'){
            if(!(fm & FEDIT_RDONLY) || (fm & ~(FEDIT_RDONLY | FEDIT_MAIN))){
               n_err(_("Standard input \"-\" requires read-only mode\n"));
               goto jeperm;
            }
            flags = a_STDIN;
         }else if((n_poption & n_PO_BATCH_FLAG) &&
               !su_cs_cmp(name, su_path_null))
            flags = a_DEVNULL;
         else{
#ifdef mx_HAVE_REALPATH
            char cbuf[PATH_MAX];

            if(realpath(name, cbuf) != NIL)
               name = savestr(cbuf);
#endif
         }
         rv = 1;
         break;

#ifdef mx_HAVE_MAILDIR
      case n_PROTO_MAILDIR:
         shudclob = 1;
         rv = maildir_setfile(who, name, fm);
         goto jleave;
#endif
#ifdef mx_HAVE_POP3
      case n_PROTO_POP3:
         shudclob = 1;
         rv = mx_pop3_setfile(who, orig_name, fm);
         goto jleave;
#endif
#ifdef mx_HAVE_IMAP
      case n_PROTO_IMAP:
         shudclob = 1;
         if((fm & FEDIT_NEWMAIL) && mb.mb_type == MB_CACHE)
            rv = 1;
         else
            rv = imap_setfile(who, orig_name, fm);
         goto jleave;
#endif

      default:
         n_err(_("Cannot handle protocol: %s\n"), orig_name);
         err = su_ERR_PROTONOSUPPORT;
         goto jeno;
      }
   }

   if(flags & a_STDIN){
      ibuf = mx_fs_fd_open(fileno(n_stdin), (mx_FS_O_RDONLY |
            mx_FS_O_NOCLOEXEC | mx_FS_O_NOCLOFORK | mx_FS_O_NOCLOSEFD));
      if(ibuf == NIL){
         err = su_err();
         n_perr(n_hy, err);
         goto jeno;
      }
   }else if((ibuf = mx_fs_open_any(savecat("file://", name), mx_FS_O_RDONLY,
            NIL)) == NIL){
      err = su_err();

      if ((fm & FEDIT_SYSBOX) && err == su_ERR_NOENT) {
         if (!(fm & FEDIT_ACCOUNT) && su_cs_cmp(who, ok_vlook(LOGNAME)) &&
               getpwnam(who) == NULL) {
            n_err(_("%s is not a user of this system\n"),
               n_shexp_quote_cp(who, FAL0));
            goto jem2;
         }
         if (!(n_poption & n_PO_QUICKRUN_MASK) && ok_blook(bsdcompat))
            n_err(_("No mail for %s at %s\n"),
               who, n_shexp_quote_cp(name, FAL0));
      }
      if (fm & FEDIT_NEWMAIL)
         goto jleave;

      if(mb.mb_digmsg != NIL)
         mx_dig_msg_on_mailbox_close(&mb);
      mb.mb_type = MB_VOID;

      if (ok_blook(emptystart)) {
         if (!(n_poption & n_PO_QUICKRUN_MASK) && !ok_blook(bsdcompat))
            n_perr(name, err);
         /* We must avoid returning -1 and cause program exit */
         rv = 1;
         goto jleave;
      }
      n_perr(name, err);
      goto jeno;
   }

   if(!su_pathinfo_fstat(&pi, fileno(ibuf))){
      if(fm & FEDIT_NEWMAIL)
         goto jleave;
      err = su_err();
      n_perr(_("fstat"), err);
      goto jeno;
   }

   if((flags & a_SPECIALS_MASK) || su_pathinfo_is_reg(&pi)){
      /* EMPTY */
   }else{
      if(fm & FEDIT_NEWMAIL)
         goto jleave;

      err = su_pathinfo_is_dir(&pi) ? su_ERR_ISDIR : su_ERR_INVAL;
      n_perr(name, err);
      goto jeno;
   }

   if(shudclob && !(fm & FEDIT_NEWMAIL) && !quit(FAL0)){
      err = su_ERR_NOTOBACCO;
      goto jem2;
   }

   hold_sigs();

#ifdef mx_HAVE_NET
   if(!(fm & FEDIT_NEWMAIL) && mb.mb_sock != NIL){
      if(mb.mb_sock->s_fd >= 0)
         mx_socket_close(mb.mb_sock); /* TODO VMAILFS->close() on open thing */
      su_FREE(mb.mb_sock);
      mb.mb_sock = NIL;
   }
#endif

   /* TODO There is no intermediate VOID box we've switched to: name may
    * TODO point to the same box that we just have written, so any updates
    * TODO we won't see!  Reopen again in this case.  RACY! Goes with VOID! */
   /* TODO In addition: in case of compressed/hook boxes we lock a tmp file! */
   /* TODO We may uselessly open twice but quit() doesn't say whether we were
    * TODO modified so we can't tell: Mailbox::is_modified() :-(( */
   if(!(flags & a_SPECIALS_MASK) && /*shudclob && !(fm & FEDIT_NEWMAIL) &&*/
         !su_cs_cmp(name, mailname)){
      name = mailname;
      mx_fs_close(ibuf);

      err = su_ERR_NONE;
      if((ibuf = mx_fs_open_any(name, mx_FS_O_RDONLY, NIL)) == NIL ||
            !su_pathinfo_fstat(&pi, fileno(ibuf)))
         err = su_err();
      else if(!su_pathinfo_is_reg(&pi))
         err = su_ERR_INVAL;
      if(err != su_ERR_NONE){
         n_perr(name, err);
         rele_sigs();
         goto jem2;
      }
   }

   /* Copy the messages into /tmp and set pointers */
   if(!(fm & FEDIT_NEWMAIL)){
      if(flags & a_DEVNULL){
         mb.mb_type = MB_VOID;
         mb.mb_perm = 0;
      }else{
         mb.mb_type = MB_FILE;
         mb.mb_perm = (((fm & FEDIT_RDONLY) || access(name, W_OK) < 0)
               ? 0 : MB_DELE | MB_EDIT);
         if (shudclob) {
            if (mb.mb_itf) {
               fclose(mb.mb_itf);
               mb.mb_itf = NULL;
            }
            if (mb.mb_otf) {
               fclose(mb.mb_otf);
               mb.mb_otf = NULL;
            }
         }
      }
      shudclob = 1;
      if (fm & FEDIT_SYSBOX)
         n_pstate &= ~n_PS_EDIT;
      else
         n_pstate |= n_PS_EDIT;
      n_initbox(name, (mb.mb_perm == 0));
      offset = 0;
   } else {
      fseek(mb.mb_otf, 0L, SEEK_END);
      /* TODO Doing this without holding a lock is.. And not err checking.. */
      fseek(ibuf, mailsize, SEEK_SET);
      offset = mailsize;
      omsgCount = msgCount;
   }

   if(flags & a_SPECIALS_MASK) /* XXX no locking for read-only "-" "file" */
      lckfp = (FILE*)-1;
   else if(!(n_pstate & n_PS_EDIT))
      lckfp = mx_file_dotlock(name, fileno(ibuf), (mx_FILE_LOCK_MODE_TSHARE |
            (fm & FEDIT_NEWMAIL ? 0 : mx_FILE_LOCK_MODE_RETRY)));
   else if(mx_file_lock(fileno(ibuf), (mx_FILE_LOCK_MODE_TSHARE |
         (fm & FEDIT_NEWMAIL ? 0 : mx_FILE_LOCK_MODE_RETRY) |
         mx_FILE_LOCK_MODE_LOG)))
      lckfp = R(FILE*,-1);

   if(lckfp == NIL){
      if(!(fm & FEDIT_NEWMAIL)){
         char const *emsg;

         err = su_err();
         emsg = ((n_pstate & n_PS_EDIT)
               ? N_("Unable to lock mailbox, aborting operation")
               : N_("Unable to (dot) lock mailbox, aborting operation"));
         n_perr(V_(emsg), err);
      }

      rele_sigs();

      if(!(fm & FEDIT_NEWMAIL))
         goto jeno;
      goto jleave;
   }

   mailsize = (flags & a_SPECIALS_MASK) ? 0 : fsize(ibuf);

   /* TODO This is too simple minded?  We should regenerate an index file
    * TODO to be able to truly tell whether *anything* has changed! */
   if ((fm & FEDIT_NEWMAIL) && UCMP(z, mailsize, <=, offset)) {
      rele_sigs();
      fprintf(n_stdout, _("No new mail\n"));
      goto jleave;
   }
   n_folder_mbox_setptr(ibuf, offset, proto,
      ((flags & a_SPECIALS_MASK/* xxx pipe thing only though */) != 0));
   setmsize(msgCount);
   if ((fm & FEDIT_NEWMAIL) && mb.mb_sorted) {
      mb.mb_threaded = 0;
      c_sort((void*)-1);
   }

   mx_fs_close(ibuf);
   ibuf = NIL;
   if(lckfp != NIL && lckfp != R(FILE*,-1)){
      mx_fs_pipe_close(lckfp, TRU1);
      /*lckfp = NIL;*/
   }

   if (!(fm & FEDIT_NEWMAIL)) {
      n_pstate &= ~n_PS_SAW_COMMAND;
      n_pstate |= n_PS_SETFILE_OPENED;
   }

   rele_sigs();

   rv = (msgCount == 0);

   if(n_poption & n_PO_EXISTONLY)
      goto jxleave;

   if(rv && (!(n_pstate & n_PS_EDIT) || (fm & FEDIT_NEWMAIL))){
      if(!(fm & FEDIT_NEWMAIL)){
         if(!ok_blook(emptystart))
            n_err(_("No mail for %s\n"), who);
      }
      goto jxleave;
   }

   if(fm & FEDIT_NEWMAIL)
      newmailinfo(omsgCount);

jxleave:
   if(rv)
      su_err_set(su_ERR_NODATA);

jleave:
   if(ibuf != NIL){
      mx_fs_close(ibuf);
      if(lckfp != NIL && lckfp != R(FILE*,-1))
         mx_fs_pipe_close(lckfp, TRU1);
   }

   NYD_OU;
   return rv;

jeperm:
   err = su_ERR_PERM;
   goto jeno;
jem2:
   if(mb.mb_digmsg != NIL)
      mx_dig_msg_on_mailbox_close(&mb);
   mb.mb_type = MB_VOID;
   /* FALLTHRU */
jeno:
   su_err_set(err);
   rv = -1;
   goto jleave;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_SETFILE
/* s-it-mode */
