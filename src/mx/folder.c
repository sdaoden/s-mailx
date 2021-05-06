/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Folder (mailbox) initialization, newmail announcement and related.
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
#define su_FILE folder
#define mx_SOURCE
#define mx_SOURCE_FOLDER

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <pwd.h>

#include <su/cs.h>
#include <su/mem.h>
#include <su/path.h>

#include "mx/cmd-shortcut.h"
#include "mx/compat.h"
#include "mx/dig-msg.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/net-pop3.h"
#include "mx/net-socket.h"
#include "mx/sigs.h"
#include "mx/ui-str.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Update mailname (if name != NIL) and displayname, return whether displayname
 * was large enough to swallow mailname */
static boole  _update_mailname(char const *name);
#ifdef mx_HAVE_C90AMEND1 /* TODO unite __narrow_suffix() into one fun! */
su_SINLINE uz __narrow_suffix(char const *cp, uz cpl, uz maxl);
#endif

/**/
static void a_folder_info(void);

/* Set up the input pointers while copying the mail file into /tmp */
static void a_folder_mbox_setptr(FILE *ibuf, off_t offset, boole iseml,
      boole maybepipe);

#ifdef mx_HAVE_C90AMEND1
su_SINLINE uz
__narrow_suffix(char const *cp, uz cpl, uz maxl)
{
   int err;
   uz i, ok;
   NYD_IN;

   for (err = ok = i = 0; cpl > maxl || err;) {
      int ml = mblen(cp, cpl);
      if (ml < 0) { /* XXX _narrow_suffix(): mblen() error; action? */
         (void)mblen(NULL, 0);
         err = 1;
         ml = 1;
      } else {
         if (!err)
            ok = i;
         err = 0;
         if (ml == 0)
            break;
      }
      cp += ml;
      i += ml;
      cpl -= ml;
   }
   NYD_OU;
   return ok;
}
#endif /* mx_HAVE_C90AMEND1 */

static boole
_update_mailname(char const *name) /* TODO 2MUCH work, cache, prop of Obj! */
{
   char const *foldp;
   char *mailp, *dispp;
   uz i, j, foldlen;
   boole rv;
   NYD_IN;

   /* Do not realpath(3) if it's only an update request */
   if(name != NIL){
#ifdef mx_HAVE_REALPATH
      char const *adjname;
      enum protocol p;

      p = which_protocol(name, TRU1, TRU1, &adjname);

      if(p == n_PROTO_FILE || p == n_PROTO_MAILDIR || p == n_PROTO_EML){
         name = adjname;
         if(realpath(name, mailname) == NIL && su_err_no() != su_ERR_NOENT){
            n_err(_("Cannot canonicalize %s\n"), n_shexp_quote_cp(name, FAL0));
            goto jdocopy;
         }
      }else
jdocopy:
#endif
         su_cs_pcopy_n(mailname, name, sizeof(mailname));
   }

   mailp = mailname;
   dispp = displayname;

   /* Don't display an absolute path but "+FOLDER" if under *folder* */
   if(*(foldp = n_folder_query()) != '\0'){
      foldlen = su_cs_len(foldp);
      if(strncmp(foldp, mailp, foldlen))
         foldlen = 0;
   }else
      foldlen = 0;

   /* We want to see the name of the folder .. on the screen */
   i = su_cs_len(mailp);
   if(i < sizeof(displayname) - 3 -1){
      if(foldlen > 0){
         *dispp++ = '+';
         *dispp++ = '[';
         su_mem_copy(dispp, mailp, foldlen);
         dispp += foldlen;
         mailp += foldlen;
         *dispp++ = ']';
         su_mem_copy(dispp, mailp, i -= foldlen);
         dispp[i] = '\0';
      }else
         su_mem_copy(dispp, mailp, i +1);
      rv = TRU1;
   }else{
      /* Avoid disrupting multibyte sequences (if possible) */
#ifndef mx_HAVE_C90AMEND1
      j = sizeof(displayname) / 3 - 3;
      i -= sizeof(displayname) - (1/* + */ + 3) - j;
#else
      j = mx_field_detect_clip(sizeof(displayname) / 3, mailp, i);
      i = j + __narrow_suffix(mailp + j, i - j,
         sizeof(displayname) - (1/* + */ + 3 + 1) - j);
#endif
      snprintf(dispp, sizeof(displayname), "%s%.*s...%s",
         (foldlen > 0 ? "[+]" : ""), (int)j, mailp, mailp + i);
      rv = FAL0;
   }

   n_PS_ROOT_BLOCK((ok_vset(mailbox_resolved, mailname),
      ok_vset(mailbox_display, displayname)));
   NYD_OU;
   return rv;
}

static void
a_folder_info(void){
   struct message *mp;
   int u, n, d, s, hidden, moved;
   NYD2_IN;

   if(mb.mb_type == MB_VOID){
      fprintf(n_stdout, _("(Currently no active mailbox)"));
      goto jleave;
   }

   s = d = hidden = moved = 0;
   for (mp = message, n = 0, u = 0; PCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MNEW)
         ++n;
      if ((mp->m_flag & MREAD) == 0)
         ++u;
      if ((mp->m_flag & (MDELETED | MSAVED)) == (MDELETED | MSAVED))
         ++moved;
      if ((mp->m_flag & (MDELETED | MSAVED)) == MDELETED)
         ++d;
      if ((mp->m_flag & (MDELETED | MSAVED)) == MSAVED)
         ++s;
      if (mp->m_flag & MHIDDEN)
         ++hidden;
   }

   /* If displayname gets truncated the user effectively has no option to see
    * the full pathname of the mailbox, so print it at least for '? fi' */
   fprintf(n_stdout, "%s: ", n_shexp_quote_cp(
      (_update_mailname(NULL) ? displayname : mailname), FAL0));
   if (msgCount == 1)
      fprintf(n_stdout, _("1 message"));
   else
      fprintf(n_stdout, _("%d messages"), msgCount);
   if (n > 0)
      fprintf(n_stdout, _(" %d new"), n);
   if (u-n > 0)
      fprintf(n_stdout, _(" %d unread"), u);
   if (d > 0)
      fprintf(n_stdout, _(" %d deleted"), d);
   if (s > 0)
      fprintf(n_stdout, _(" %d saved"), s);
   if (moved > 0)
      fprintf(n_stdout, _(" %d moved"), moved);
   if (hidden > 0)
      fprintf(n_stdout, _(" %d hidden"), hidden);
   if (mb.mb_perm == 0)
      fprintf(n_stdout, _(" [Read-only]"));
#ifdef mx_HAVE_IMAP
   if (mb.mb_type == MB_CACHE)
      fprintf(n_stdout, _(" [Disconnected]"));
#endif

jleave:
   putc('\n', n_stdout);
   NYD2_OU;
}

static void
a_folder_mbox_setptr(FILE *ibuf, off_t offset, boole iseml, boole maybepipe){
   enum{
      a_RFC4155 = 1u<<0,
      a_HAD_BAD_FROM_ = 1u<<1,
      a_HAD_DATA = 1u<<2,
      a_HAD_ONE = 1u<<3,
      a_MAYBE = 1u<<4,
      a_CREATE = 1u<<5,
      a_INHEAD = 1u<<6,
      a_COMMIT = 1u<<7,
      a_ISEML = 1u<<16
   };

   struct message self, commit;
   char *linebuf, *cp;
   boole from_;
   u32 f;
   uz filesize, linesize, cnt;
   int msgcnt_base;
   NYD_IN;

   msgcnt_base = msgCount;
   filesize = mailsize - offset;

   su_mem_set(&self, 0, sizeof self);
   self.m_flag = MVALID | MNEW | MNEWEST;

   offset = ftell(mb.mb_otf);
   f = (iseml ? (a_ISEML | a_HAD_DATA)
         : (a_MAYBE | (ok_blook(mbox_rfc4155) ? a_RFC4155 : 0)));

   mx_fs_linepool_aquire(&linebuf, &linesize);
   for(;;){
      /* Ensure space for terminating LF, so do append it */
      if(UNLIKELY(fgetline(&linebuf, &linesize, (maybepipe ? NIL : &filesize),
            &cnt, ibuf, TRU1) == NIL)){
         /* TODO We are not prepared for error here */
         if(f & (a_HAD_DATA | a_HAD_ONE)){
            if(f & a_CREATE){
               commit.m_size += self.m_size;
               commit.m_lines += self.m_lines;
            }else
               commit = self;
            commit.m_xsize = commit.m_size;
            commit.m_xlines = commit.m_lines;
            commit.m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
            if(!(f & (a_ISEML | a_HAD_ONE)))
               commit.m_flag &= ~MVALID;
            if(f & a_ISEML)
               commit.m_flag |= MNOFROM;
            mx_message_append(&commit);
         }
         mx_message_append_nil();

         if(f & a_HAD_BAD_FROM_){
            /*if(!(mb.mb_active & MB_BAD_FROM_))*/{
               mb.mb_active |= MB_BAD_FROM_;
               /* TODO mbox-rfc4155 does NOT fix From_ line! */
               n_err(_("MBOX contains non-conforming From_ line(s)!\n"
                  "  Message boundaries may have been misdetected!\n"
                  "  Setting *mbox-rfc4155* and reopening _may_ "
                     "improve the result.\n"
                  "  Recreating the mailbox will perform MBOXO quoting: "
                     "\"copy * SOME-FILE\".\n"
                  "  (Then unset *mbox-rfc4155* again.)\n"));
            }
         }

         mx_fs_linepool_release(linebuf, linesize);
         break;
      }

      /* Normalize away line endings, we will place (readded) \n */
      if(cnt >= 2 && linebuf[cnt - 2] == '\r')
         linebuf[--cnt] = '\0';
      linebuf[--cnt] = '\0';
      /* We cannot use this ASSERTion since it will trigger for example when
       * the Linux kernel crashes and the log files (which may contain NULs)
       * are sent out via email!  (It has been active for quite some time..) */
      /*ASSERT(linebuf[0] != '\0' || cnt == 0);*/

      /* TODO In v15 this should use a/the flat MIME parser in order to ignore
       * TODO "From " when MIME boundaries are active -- whereas this opens
       * TODO another can of worms, it very likely is better than messing up
       * TODO MIME because of a "From " line!.
       * TODO That is: Mailbox superclass, MBOX:Mailbox, virtual load() which
       * TODO creates collection of MessageHull objects which are able to load
       * TODO their content, and normalize content, correct structural errs */
      if(UNLIKELY(cnt == 0)){
         if(!(f & a_ISEML))
            f |= a_MAYBE;

         if(LIKELY(!(f & a_CREATE)))
            f &= ~a_INHEAD;
         else{
            f |= a_HAD_BAD_FROM_;
            f &= ~(a_CREATE | a_INHEAD | a_COMMIT);
            commit.m_size += self.m_size;
            commit.m_lines += self.m_lines;
            commit.m_flag |= MBADFROM_;
            self = commit;
         }
         goto jputln;
      }

      if(UNLIKELY((f & a_MAYBE) && (from_ = (linebuf[0] == 'F')) &&
            (from_ = is_head(linebuf, cnt, TRU1)) &&
            (from_ == TRU1 || !(f & a_RFC4155)))){
         /* TODO char date[n_FROM_DATEBUF];
          * TODO extract_date_from_from_(linebuf, cnt, date);
          * TODO self.m_time = 10000; */
         self.m_xsize = self.m_size;
         self.m_xlines = self.m_lines;
         self.m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
         commit = self;

         f |= a_CREATE | a_INHEAD;
         if(f & a_ISEML)
            f |= a_COMMIT;
         else if(!(f & a_COMMIT) &&
               (f & (a_HAD_DATA | a_HAD_ONE)) == a_HAD_DATA){
            commit.m_flag &= ~MVALID;
            if(msgcnt_base == msgCount)
               mx_message_append(&commit);
            else
               message[msgCount - 1] = commit;
         }
         f &= ~a_MAYBE;
         f |= a_HAD_DATA;

         if(from_ == TRUM1){
            f |= a_HAD_BAD_FROM_;
            /* TODO MBADFROM_ causes the From_ line to be replaced entirely
             * TODO when the message is newly written via e.g. `copy'.
             * TODO Instead this From_ should be an object which can fix
             * TODO the parts which are missing or are faulty, such that
             * TODO good info can be kept; this is possible after the main
             * TODO message header has been fully parsed.  For now we are stuck
             * TODO and fail for example in a_header_extract_date_from_from_()
             * TODO (which should not exist as such btw) */
            self.m_flag = MVALID | MNEW | MNEWEST | MBADFROM_;
         }else
            self.m_flag = MVALID | MNEW | MNEWEST |
                  (f & a_ISEML ? MNOFROM : 0);
         self.m_size = 0;
         self.m_lines = 0;
         self.m_block = mailx_blockof(offset);
         self.m_offset = mailx_offsetof(offset);
         goto jputln;
      }

      f &= ~a_MAYBE;
      if(LIKELY(!(f & a_INHEAD))){
         if(UNLIKELY(!(f & (a_ISEML | a_HAD_ONE | a_HAD_DATA)))){
            for(cp = &linebuf[cnt]; cp-- != linebuf;)
               if(!su_cs_is_space(*cp)){
                  f |= a_HAD_DATA;
                  self.m_flag |= MNOFROM;
                  break;
               }
         }
         goto jputln;
      }

      if(LIKELY((cp = su_mem_find(linebuf, ':', cnt)) != NULL)){
         u32 mf;
         char *cps, *cpe, c;

         f |= a_HAD_DATA;
         if(f & a_CREATE)
            f |= a_COMMIT;

         mf = self.m_flag;
         for(cps = linebuf; su_cs_is_blank(*cps); ++cps)
            ;
         for(cpe = cp; cpe > cps && (--cpe, su_cs_is_blank(*cpe));)
            ;
         switch(P2UZ(cpe - cps)){
         case 5:
            if(!su_cs_cmp_case_n(cps, "status", 5))
               for(;;){
                  if((c = *++cp) == '\0')
                     break;
                  switch(c){
                  case 'R': mf |= MREAD; break;
                  case 'O': mf &= ~MNEW; break;
                  }
               }
            break;
         case 7:
            if(!su_cs_cmp_case_n(cps, "x-status", 7))
               for(;;){
                  if((c = *++cp) == '\0')
                     break;
                  switch(c){
                  case 'F': mf |= MFLAGGED; break;
                  case 'A': mf |= MANSWERED; break;
                  case 'T': mf |= MDRAFTED; break;
                  }
               }
            break;
         }
         self.m_flag = mf;
      }else if(!su_cs_is_blank(linebuf[0])){
         /* So either this is a false detection (nothing but From_ line
          * yet, and is not a valid MBOX according to POSIX and RFC 5322), or
          * no separating empty line in between header/body!
          * In the latter case, add one! */
         f |= a_HAD_DATA;
         if(!(f & a_CREATE)){
            if(putc('\n', mb.mb_otf) == EOF){
               n_perr(_("/tmp"), 0);
               exit(n_EXIT_ERR); /* TODO no! */
            }
            ++offset;
            ++self.m_size;
            ++self.m_lines;
         }else{
            commit.m_size += self.m_size;
            commit.m_lines += self.m_lines;
            self = commit;
            f &= ~(a_CREATE | a_INHEAD | a_COMMIT);
         }
      }else if(!(f & (a_HAD_ONE | a_HAD_DATA))){
         for(cp = &linebuf[cnt]; cp-- != linebuf;)
            if(!su_cs_is_space(*cp)){
               f |= a_HAD_DATA;
               break;
            }
      }

jputln:
      if(f & a_COMMIT){
         f &= ~(a_CREATE | a_COMMIT);
         if(f & a_HAD_ONE)
            mx_message_append(&commit);
         f |= a_HAD_ONE | a_HAD_DATA;
      }
      linebuf[cnt++] = '\n';
      ASSERT(linebuf[cnt] == '\0');
      fwrite(linebuf, sizeof *linebuf, cnt, mb.mb_otf);

      if(ferror(mb.mb_otf)){
         n_perr(_("/tmp"), 0);
         exit(n_EXIT_ERR); /* TODO no! */
      }
      offset += cnt;
      self.m_size += cnt;
      ++self.m_lines;
   }

   NYD_OU;
}

FL int
setfile(char const *name, enum fedit_mode fm) /* TODO oh my god */
{
   /* TODO This all needs to be converted to URL:: and Mailbox:: */
   static int shudclob;

   enum{
      a_NONE,
      a_DEVNULL = 1u<<0,
      a_STDIN = 1u<<1,

      a_SPECIALS_MASK = a_DEVNULL | a_STDIN
   };

   struct stat stb;
   uz offset;
   char const *who, *orig_name;
   enum protocol proto;
   int rv, omsgCount = 0;
   FILE *ibuf, *lckfp;
   u8 flags;
   NYD_IN;

   n_pstate &= ~n_PS_SETFILE_OPENED;
   flags = a_NONE;
   ibuf = lckfp = NIL;

   if(n_poption & n_PO_R_FLAG)
      fm |= FEDIT_RDONLY;

   /* C99 */{
      enum fexp_mode fexpm;

      if((who = mx_shortcut_expand(name)) != NIL){
         fexpm = FEXP_NSHELL;
         name = who;
      }else
         fexpm = FEXP_SHORTCUT | FEXP_NSHELL;

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

      if((name = fexpand(name, fexpm)) == NIL)
         goto jem1;
   }

   /* For at least substdate() users TODO -> eventloop tick */
   time_current_update(&time_current, FAL0);

   switch((proto = which_protocol(orig_name = name, TRU1, TRU1, &name))){
   case n_PROTO_EML:
      if(!(fm & FEDIT_RDONLY) || (fm & ~(FEDIT_RDONLY | FEDIT_MAIN))){
         n_err(_("Sorry, for now eml:// files only work read-only: %s\n"),
            orig_name);
         goto jem1;
      }
      /* FALLTHRU */
   case PROTO_FILE:
      if(name[1] == '\0' && name[0] == '-'){
         if(fm ^ (FEDIT_RDONLY | FEDIT_MAIN)){
            n_err(_("Standard input \"-\" only works for -f "
               "command line option, read-only\n"));
            goto jem1;
         }
         flags = a_STDIN;
      }else if((n_poption & n_PO_BATCH_FLAG) &&
            !su_cs_cmp(name, su_path_dev_null))
         flags = a_DEVNULL;
      else{
#ifdef mx_HAVE_REALPATH
         do{ /* TODO we need objects, goes away then */
# ifdef mx_HAVE_REALPATH_NULL
            char *cp;

            if((cp = realpath(name, NIL)) != NIL){
               name = savestr(cp);
               free(cp);
            }
# else
            char cbuf[PATH_MAX];

            if(realpath(name, cbuf) != NIL)
               name = savestr(cbuf);
# endif
         }while(0);
#endif
      }
      rv = 1;
      break;
#ifdef mx_HAVE_MAILDIR
   case PROTO_MAILDIR:
      shudclob = 1;
      rv = maildir_setfile(who, name, fm);
      goto jleave;
#endif
#ifdef mx_HAVE_POP3
   case PROTO_POP3:
      shudclob = 1;
      rv = mx_pop3_setfile(who, orig_name, fm);
      goto jleave;
#endif
#ifdef mx_HAVE_IMAP
   case PROTO_IMAP:
      shudclob = 1;
      if((fm & FEDIT_NEWMAIL) && mb.mb_type == MB_CACHE)
         rv = 1;
      else
         rv = imap_setfile(who, orig_name, fm);
      goto jleave;
#endif
   default:
      n_err(_("Cannot handle protocol: %s\n"), orig_name);
      goto jem1;
   }

   if(flags & a_STDIN)
      ibuf = mx_fs_fd_open(fileno(n_stdin), (mx_FS_O_RDONLY |
            mx_FS_O_NOCLOEXEC));
   else if((ibuf = mx_fs_open_any(savecat("file://", name), mx_FS_O_RDONLY,
            NIL)) == NIL){
      int e = su_err_no();

      if ((fm & FEDIT_SYSBOX) && e == su_ERR_NOENT) {
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
            n_perr(name, e);
         /* We must avoid returning -1 and causing program exit */
         rv = 1;
         goto jleave;
      }
      n_perr(name, e);
      goto jem1;
   }

   if (fstat(fileno(ibuf), &stb) == -1) {
      if (fm & FEDIT_NEWMAIL)
         goto jleave;
      n_perr(_("fstat"), 0);
      goto jem1;
   }

   if((flags & a_SPECIALS_MASK) || S_ISREG(stb.st_mode)){
      /* EMPTY */
   }else{
      if(fm & FEDIT_NEWMAIL)
         goto jleave;
      su_err_set_no(S_ISDIR(stb.st_mode) ? su_ERR_ISDIR : su_ERR_INVAL);
      n_perr(name, 0);
      goto jem1;
   }

   if (shudclob && !(fm & FEDIT_NEWMAIL) && !quit(FAL0))
      goto jem2;

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

      if((ibuf = mx_fs_open_any(name, mx_FS_O_RDONLY, NIL)) == NIL ||
            fstat(fileno(ibuf), &stb) == -1 ||
            !S_ISREG(stb.st_mode)){
         n_perr(name, 0);
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
      initbox(name);
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

   if (lckfp == NULL) {
      if (!(fm & FEDIT_NEWMAIL)) {
#ifdef mx_HAVE_UISTRINGS
         char const * const emsg = (n_pstate & n_PS_EDIT)
               ? N_("Unable to lock mailbox, aborting operation")
               : N_("Unable to (dot) lock mailbox, aborting operation");
#endif
         n_perr(V_(emsg), 0);
      }
      rele_sigs();
      if (!(fm & FEDIT_NEWMAIL))
         goto jem1;
      goto jleave;
   }

   mailsize = (flags & a_SPECIALS_MASK) ? 0 : fsize(ibuf);

   /* TODO This is too simple minded?  We should regenerate an index file
    * TODO to be able to truly tell whether *anything* has changed! */
   if ((fm & FEDIT_NEWMAIL) && UCMP(z, mailsize, <=, offset)) {
      rele_sigs();
      goto jleave;
   }
   a_folder_mbox_setptr(ibuf, offset, (proto == n_PROTO_EML),
      ((flags & a_SPECIALS_MASK/* xxx pipe thing only though */) != 0));
   setmsize(msgCount);
   if ((fm & FEDIT_NEWMAIL) && mb.mb_sorted) {
      mb.mb_threaded = 0;
      c_sort((void*)-1);
   }

   mx_fs_close(ibuf);
   ibuf = NIL;
   if(lckfp != NIL && lckfp != R(FILE*,-1)){
      mx_fs_pipe_close(lckfp, FAL0);
      /*lckfp = NIL;*/
   }

   if (!(fm & FEDIT_NEWMAIL)) {
      n_pstate &= ~n_PS_SAW_COMMAND;
      n_pstate |= n_PS_SETFILE_OPENED;
   }

   rele_sigs();

   rv = (msgCount == 0);
   if(rv)
      su_err_set_no(su_ERR_NODATA);

   if(n_poption & n_PO_EXISTONLY)
      goto jleave;

   if(rv){
      if(!(n_pstate & n_PS_EDIT) || (fm & FEDIT_NEWMAIL)){
         if(!(fm & FEDIT_NEWMAIL)){
            if (!ok_blook(emptystart))
               n_err(_("No mail for %s at %s\n"),
                  who, n_shexp_quote_cp(name, FAL0));
         }
         goto jleave;
      }
   }

   if(fm & FEDIT_NEWMAIL)
      newmailinfo(omsgCount);
jleave:
   if(ibuf != NIL){
      mx_fs_close(ibuf);
      if(lckfp != NIL && lckfp != R(FILE*,-1))
         mx_fs_pipe_close(lckfp, FAL0);
   }

   NYD_OU;
   return rv;

jem2:
   if(mb.mb_digmsg != NIL)
      mx_dig_msg_on_mailbox_close(&mb);
   mb.mb_type = MB_VOID;
jem1:
   su_err_set_no(su_ERR_NOTOBACCO);
   rv = -1;
   goto jleave;
}

FL int
newmailinfo(int omsgCount)
{
   int mdot, i;
   NYD_IN;

   for (i = 0; i < omsgCount; ++i)
      message[i].m_flag &= ~MNEWEST;

   if (msgCount > omsgCount) {
      for (i = omsgCount; i < msgCount; ++i)
         message[i].m_flag |= MNEWEST;
      fprintf(n_stdout, _("New mail has arrived.\n"));
      if ((i = msgCount - omsgCount) == 1)
         fprintf(n_stdout, _("Loaded 1 new message.\n"));
      else
         fprintf(n_stdout, _("Loaded %d new messages.\n"), i);
   } else
      fprintf(n_stdout, _("Loaded %d messages.\n"), msgCount);

   temporary_folder_hook_check(TRU1);

   mdot = getmdot(1);

   if(ok_blook(header) && (i = omsgCount + 1) <= msgCount){
#ifdef mx_HAVE_IMAP
      if(mb.mb_type == MB_IMAP)
         imap_getheaders(i, msgCount); /* TODO not here */
#endif
      for(omsgCount = 0; i <= msgCount; ++omsgCount, ++i)
         n_msgvec[omsgCount] = i;
      n_msgvec[omsgCount] = 0;
      print_headers(n_msgvec, FAL0, FAL0);
   }
   NYD_OU;
   return mdot;
}

FL void
setmsize(int size)
{
   NYD_IN;
   if(n_msgvec != NULL)
      n_free(n_msgvec);
   n_msgvec = n_calloc(size +1, sizeof *n_msgvec);
   NYD_OU;
}

FL void
print_header_summary(char const *Larg)
{
   uz i;
   NYD_IN;

   getmdot(0);
#ifdef mx_HAVE_IMAP
      if(mb.mb_type == MB_IMAP)
         imap_getheaders(0, msgCount); /* TODO not here */
#endif
   ASSERT(n_msgvec != NULL);

   if (Larg != NULL) {
      /* Avoid any messages XXX add a make_mua_silent() and use it? */
      if ((n_poption & (n_PO_V | n_PO_EXISTONLY)) == n_PO_EXISTONLY) {
         n_stdout = freopen(su_path_dev_null, "w", stdout);
         n_stderr = freopen(su_path_dev_null, "w", stderr);
      }
      i = (n_getmsglist(n_shexp_quote_cp(Larg, FAL0), n_msgvec, 0, NULL) <= 0);
      if (n_poption & n_PO_EXISTONLY)
         n_exit_status = (int)i;
      else if(i == 0)
         print_headers(n_msgvec, TRU1, FAL0); /* TODO should be iterator! */
   } else {
      i = 0;
      if(!mb.mb_threaded){
         for(; UCMP(z, i, <, msgCount); ++i)
            n_msgvec[i] = i + 1;
      }else{
         struct message *mp;

         for(mp = threadroot; mp; ++i, mp = next_in_thread(mp))
            n_msgvec[i] = (int)P2UZ(mp - message + 1);
      }
      print_headers(n_msgvec, FAL0, TRU1); /* TODO should be iterator! */
   }
   NYD_OU;
}

FL void
n_folder_announce(enum n_announce_flags af){
   int vec[2], mdot;
   NYD_IN;

   mdot = (mb.mb_type == MB_VOID) ? 1 : getmdot(0);
   dot = &message[mdot - 1];

   if(af != n_ANNOUNCE_NONE && ok_blook(header) &&
         ((af & n_ANNOUNCE_MAIN_CALL) ||
          ((af & n_ANNOUNCE_CHANGE) && !ok_blook(posix))))
      af |= n_ANNOUNCE_STATUS | n__ANNOUNCE_HEADER;

   if(af & n_ANNOUNCE_STATUS){
      a_folder_info();
      af |= n__ANNOUNCE_ANY;
   }

   if(af & n__ANNOUNCE_HEADER){
      if(!(af & n_ANNOUNCE_MAIN_CALL) && ok_blook(bsdannounce))
         n_OBSOLETE(_("*bsdannounce* is now default behaviour"));
      vec[0] = mdot;
      vec[1] = 0;
      print_header_group(vec); /* XXX errors? */
      af |= n__ANNOUNCE_ANY;
   }

   if(af & n__ANNOUNCE_ANY)
      fflush(n_stdout);
   NYD_OU;
}

FL int
getmdot(int nmail)
{
   struct message *mp;
   char *cp;
   int mdot;
   enum mflag avoid = MHIDDEN | MDELETED;
   NYD_IN;

   if (!nmail) {
      if (ok_blook(autothread)) {
         n_OBSOLETE(_("please use *autosort=thread* instead of *autothread*"));
         c_thread(NULL);
      } else if ((cp = ok_vlook(autosort)) != NULL) {
         if (mb.mb_sorted != NULL)
            n_free(mb.mb_sorted);
         mb.mb_sorted = su_cs_dup(cp, 0);
         c_sort(NULL);
      }
   }
   if (mb.mb_type == MB_VOID) {
      mdot = 1;
      goto jleave;
   }

   if (nmail)
      for (mp = message; PCMP(mp, <, message + msgCount); ++mp)
         if ((mp->m_flag & (MNEWEST | avoid)) == MNEWEST)
            break;

   if (!nmail || PCMP(mp, >=, message + msgCount)) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if ((mp->m_flag & (MNEW | avoid)) == MNEW)
               break;
      } else {
         for (mp = message; PCMP(mp, <, message + msgCount); ++mp)
            if ((mp->m_flag & (MNEW | avoid)) == MNEW)
               break;
      }
   }

   if ((mb.mb_threaded ? (mp == NULL) : PCMP(mp, >=, message + msgCount))) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if (mp->m_flag & MFLAGGED)
               break;
      } else {
         for (mp = message; PCMP(mp, <, message + msgCount); ++mp)
            if (mp->m_flag & MFLAGGED)
               break;
      }
   }

   if ((mb.mb_threaded ? (mp == NULL) : PCMP(mp, >=, message + msgCount))) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if (!(mp->m_flag & (MREAD | avoid)))
               break;
      } else {
         for (mp = message; PCMP(mp, <, message + msgCount); ++mp)
            if (!(mp->m_flag & (MREAD | avoid)))
               break;
      }
   }

   if (nmail &&
         (mb.mb_threaded ? (mp != NULL) : PCMP(mp, <, message + msgCount)))
      mdot = (int)P2UZ(mp - message + 1);
   else if (ok_blook(showlast)) {
      if (mb.mb_threaded) {
         for (mp = this_in_thread(threadroot, -1); mp;
               mp = prev_in_thread(mp))
            if (!(mp->m_flag & avoid))
               break;
         mdot = (mp != NULL) ? (int)P2UZ(mp - message + 1) : msgCount;
      } else {
         for (mp = message + msgCount - 1; mp >= message; --mp)
            if (!(mp->m_flag & avoid))
               break;
         mdot = (mp >= message) ? (int)P2UZ(mp - message + 1) : msgCount;
      }
   } else if (!nmail &&
         (mb.mb_threaded ? (mp != NULL) : PCMP(mp, <, message + msgCount)))
      mdot = (int)P2UZ(mp - message + 1);
   else if (mb.mb_threaded) {
      for (mp = threadroot; mp; mp = next_in_thread(mp))
         if (!(mp->m_flag & avoid))
            break;
      mdot = (mp != NULL) ? (int)P2UZ(mp - message + 1) : 1;
   } else {
      for (mp = message; PCMP(mp, <, message + msgCount); ++mp)
         if (!(mp->m_flag & avoid))
            break;
      mdot = PCMP(mp, <, message + msgCount)
            ? (int)P2UZ(mp - message + 1) : 1;
   }
jleave:
   NYD_OU;
   return mdot;
}

FL void
initbox(char const *name)
{
   struct mx_fs_tmp_ctx *fstcp;
   boole err;
   NYD_IN;

   if (mb.mb_type != MB_VOID)
      su_cs_pcopy_n(prevfile, mailname, PATH_MAX);

   /* TODO name always NE mailname (but goes away for objects anyway)
    * TODO Well, not true no more except that in parens */
   _update_mailname((name != mailname) ? name : NULL);

   err = FAL0;
   if((mb.mb_otf = mx_fs_tmp_open(NIL, "tmpmbox", (mx_FS_O_WRONLY |
            mx_FS_O_HOLDSIGS | mx_FS_O_NOREGISTER), &fstcp)) == NIL){
      n_perr(_("initbox: temporary mail message file, writer"), 0);
      err = TRU1;
   }else if((mb.mb_itf = mx_fs_open(fstcp->fstc_filename, (mx_FS_O_RDONLY |
            mx_FS_O_NOREGISTER))) == NIL){
      n_perr(_("initbox: temporary mail message file, reader"), 0);
      err = TRU1;
   }
   mx_fs_tmp_release(fstcp);
   if(err)
      exit(n_EXIT_ERR);

   mx_message_reset();
   mb.mb_active = MB_NONE;
   mb.mb_threaded = 0;
#ifdef mx_HAVE_IMAP
   mb.mb_flags = MB_NOFLAGS;
#endif
   if (mb.mb_sorted != NULL) {
      n_free(mb.mb_sorted);
      mb.mb_sorted = NULL;
   }
   dot = prevdot = threadroot = NULL;
   n_pstate &= ~n_PS_DID_PRINT_DOT;
   NYD_OU;
}

FL char const *
n_folder_query(void){
   struct n_string s_b, *s;
   enum protocol proto;
   char *cp;
   char const *rv, *adjcp;
   boole err;
   NYD_IN;

   s = n_string_creat_auto(&s_b);

   /* *folder* is linked with *folder_resolved*: we only use the latter */
   for(err = FAL0;;){
      if((rv = ok_vlook(folder_resolved)) != NIL)
         break;

      /* POSIX says:
       *    If directory does not start with a <slash> ('/'), the contents
       *    of HOME shall be prefixed to it.
       * And:
       *    If folder is unset or set to null, [.] filenames beginning with
       *    '+' shall refer to files in the current directory.
       * We may have the result already.
       * P.S.: that "or set to null" seems to be a POSIX bug, V10 mail and BSD
       * Mail since 1982 work differently, follow suit */
      rv = su_empty;
      err = FAL0;

      if((cp = ok_vlook(folder)) == NIL)
         goto jset;

      /* Expand the *folder*; skip %: prefix for simplicity of use */
      if(cp[0] == '%' && cp[1] == ':')
         cp += 2;
      if((err = (cp = fexpand(cp, FEXP_NSPECIAL | FEXP_NFOLDER | FEXP_NSHELL)
            ) == NIL) /*|| *cp == '\0'*/)
         goto jset;
      else{
         uz i;

         for(i = su_cs_len(cp);;){
            if(--i == 0)
               goto jset;
            if(cp[i] != '/'){
               cp[++i] = '\0';
               break;
            }
         }
      }

      switch((proto = which_protocol(cp, FAL0, FAL0, &adjcp))){
      case PROTO_POP3:
         n_err(_("*folder*: cannot use the POP3 protocol\n"));
         err = TRU1;
         goto jset;
      case PROTO_IMAP:
#ifdef mx_HAVE_IMAP
         rv = cp;
         if(!su_cs_cmp(rv, protbase(rv)))
            rv = savecatsep(rv, '/', n_empty);
#else
         n_err(_("*folder*: IMAP support not compiled in\n"));
         err = TRU1;
#endif
         goto jset;
      default:
         /* Further expansion desired */
         break;
      }

      /* Prefix HOME as necessary */
      if(*adjcp != '/'){ /* XXX path_is_absolute() */
         uz l1, l2;
         char const *home;

         home = ok_vlook(HOME);
         l1 = su_cs_len(home);
         ASSERT(l1 > 0); /* (checked VIP variable) */
         l2 = su_cs_len(cp);

         s = n_string_reserve(s, l1 + 1 + l2 +1);

         if(cp != adjcp){
            uz i;

            s = n_string_push_buf(s, cp, i = P2UZ(adjcp - cp));
            cp += i;
            l2 -= i;
         }

         s = n_string_push_buf(s, home, l1);
         if(l2 > 0){
            s = n_string_push_c(s, '/');
            s = n_string_push_buf(s, cp, l2);
         }
         cp = n_string_cp(s);
         s = n_string_drop_ownership(s);
      }

      /* TODO Since visual mailname is resolved via realpath(3) if available
       * TODO to avoid that we loose track of our currently open folder in case
       * TODO we chdir away, but still checks the leading path portion against
       * TODO folder_query() to be able to abbreviate to the +FOLDER syntax if
       * TODO possible, we need to realpath(3) the folder, too */
#ifndef mx_HAVE_REALPATH
      rv = cp;
#else
      ASSERT(s->s_len == 0 && s->s_dat == NIL);
# ifndef mx_HAVE_REALPATH_NULL
      s = n_string_reserve(s, PATH_MAX +1);
# endif

      if((s->s_dat = realpath(cp, s->s_dat)) != NIL){
# ifdef mx_HAVE_REALPATH_NULL
         n_string_cp(s = n_string_assign_cp(s, cp = s->s_dat));
         free(cp);
# endif
         rv = s->s_dat;
      }else if(su_err_no() == su_ERR_NOENT)
         rv = cp;
      else{
         n_err(_("Cannot canonicalize *folder*: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         err = TRU1;
         rv = su_empty;
      }
      s = n_string_drop_ownership(s);
#endif /* mx_HAVE_REALPATH */

      /* Always append a solidus to our result path upon success */
      if(!err){
         uz i;

         if(rv[(i = su_cs_len(rv)) - 1] != '/'){
            s = n_string_reserve(s, i + 1 +1);
            s = n_string_push_buf(s, rv, i);
            s = n_string_push_c(s, '/');
            rv = n_string_cp(s);
            s = n_string_drop_ownership(s);
         }
      }

jset:
      n_PS_ROOT_BLOCK(ok_vset(folder_resolved, rv));
   }

   if(err){
      n_err(_("*folder* is not resolvable, using CWD\n"));
      ASSERT(rv != NIL && *rv == '\0');
   }

   NYD_OU;
   return rv;
}

FL int
n_folder_mbox_prepare_append(FILE *fout, struct stat *st_or_nil){
   /* TODO n_folder_mbox_prepare_append -> Mailbox->append() */
   struct stat stb;
   char buf[2];
   int rv;
   boole needsep;
   NYD2_IN;

   if(fseek(fout, -2L, SEEK_END) == 0 && fread(buf, sizeof *buf, 2, fout) == 2)
      needsep = (buf[0] != '\n' || buf[1] != '\n');
   else{
      rv = su_err_no();
      if(st_or_nil == NIL){
         st_or_nil = &stb;
         if(fstat(fileno(fout), st_or_nil))
            goto jerrno;
      }

      if(st_or_nil->st_size >= 2)
         goto jleave;
      if(st_or_nil->st_size == 0){
         clearerr(fout);
         rv = su_ERR_NONE;
         goto jleave;
      }

      if(fseek(fout, -1L, SEEK_END))
         goto jerrno;
      if(fread(buf, sizeof *buf, 1, fout) != 1)
         goto jerrno;
      needsep = (buf[0] != '\n');
   }

   rv = su_ERR_NONE;
   if((needsep && (fseek(fout, 0L, SEEK_END) || putc('\n', fout) == EOF)) ||
         fflush(fout) == EOF)
jerrno:
      rv = su_err_no();

jleave:
   NYD2_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_FOLDER
/* s-it-mode */
