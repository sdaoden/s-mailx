/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Folder (mailbox) initialization, newmail announcement and related.
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
#define n_FILE folder

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <pwd.h>

/* Update mailname (if name != NULL) and displayname, return wether displayname
 * was large enough to swallow mailname */
static bool_t  _update_mailname(char const *name);
#ifdef HAVE_C90AMEND1 /* TODO unite __narrow_suffix() into one fun! */
SINLINE size_t __narrow_suffix(char const *cp, size_t cpl, size_t maxl);
#endif

#ifdef HAVE_C90AMEND1
SINLINE size_t
__narrow_suffix(char const *cp, size_t cpl, size_t maxl)
{
   int err;
   size_t i, ok;
   NYD_ENTER;

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
   NYD_LEAVE;
   return ok;
}
#endif /* HAVE_C90AMEND1 */

static bool_t
_update_mailname(char const *name)
{
   char tbuf[PATH_MAX], *mailp, *dispp;
   size_t i, j;
   bool_t rv = TRU1;
   NYD_ENTER;

   /* Don't realpath(3) if it's only an update request */
   if (name != NULL) {
#ifdef HAVE_REALPATH
      enum protocol p = which_protocol(name);
      if (p == PROTO_FILE || p == PROTO_MAILDIR) {
         if (realpath(name, mailname) == NULL) {
            n_err(_("Can't canonicalize \"%s\"\n"), name);
            rv = FAL0;
            goto jdocopy;
         }
      } else
jdocopy:
#endif
         n_strscpy(mailname, name, sizeof(mailname));
   }

   mailp = mailname;
   dispp = displayname;

   /* Don't display an absolute path but "+FOLDER" if under *folder* */
   if (getfold(tbuf, sizeof tbuf)) {
      i = strlen(tbuf);
      if (i < sizeof(tbuf) -1)
         tbuf[i++] = '/';
      if (!strncmp(tbuf, mailp, i)) {
         mailp += i;
         *dispp++ = '+';
      }
   }

   /* We want to see the name of the folder .. on the screen */
   i = strlen(mailp);
   if (i < sizeof(displayname) -1)
      memcpy(dispp, mailp, i +1);
   else {
      rv = FAL0;
      /* Avoid disrupting multibyte sequences (if possible) */
#ifndef HAVE_C90AMEND1
      j = sizeof(displayname) / 3 - 1;
      i -= sizeof(displayname) - (1/* + */ + 3) - j;
#else
      j = field_detect_clip(sizeof(displayname) / 3, mailp, i);
      i = j + __narrow_suffix(mailp + j, i - j,
         sizeof(displayname) - (1/* + */ + 3 + 1) - j);
#endif
      snprintf(dispp, sizeof(displayname), "%.*s...%s",
         (int)j, mailp, mailp + i);
   }
   NYD_LEAVE;
   return rv;
}

FL int
setfile(char const *name, enum fedit_mode fm) /* TODO oh my god */
{
   /* Note we don't 'userid(myname) != getuid()', preliminary steps are usually
    * necessary to make a mailbox accessible by a different user, and if that
    * has happened, let's just let the usual file perms decide */
   static int shudclob;

   struct stat stb;
   size_t offset;
   char const *who;
   int rv, omsgCount = 0;
   FILE *ibuf = NULL, *lckfp = NULL;
   bool_t isdevnull = FAL0;
   NYD_ENTER;

   pstate &= ~PS_SETFILE_OPENED;

   if (name[0] == '%' || ((who = shortcut_expand(name)) != NULL && *who == '%'))
      fm |= FEDIT_SYSBOX; /* TODO fexpand() needs to tell is-valid-user! */

   who = (name[1] != '\0') ? name + 1 : myname;

   if ((name = expand(name)) == NULL)
      goto jem1;

   switch (which_protocol(name)) {
   case PROTO_FILE:
      if (temporary_protocol_ext != NULL)
         name = savecat(name, temporary_protocol_ext);
      isdevnull = ((options & OPT_BATCH_FLAG) && !strcmp(name, "/dev/null"));
#ifdef HAVE_REALPATH
      do { /* TODO we need objects, goes away then */
         char ebuf[PATH_MAX];
         if (realpath(name, ebuf) != NULL)
            name = savestr(ebuf);
      } while (0);
#endif
      rv = 1;
      break;
   case PROTO_MAILDIR:
      shudclob = 1;
      rv = maildir_setfile(name, fm);
      goto jleave;
#ifdef HAVE_POP3
   case PROTO_POP3:
      shudclob = 1;
      rv = pop3_setfile(name, fm);
      goto jleave;
#endif
   default:
      n_err(_("Cannot handle protocol: %s\n"), name);
      goto jem1;
   }

   if ((ibuf = Zopen(name, "r")) == NULL) {
      if ((fm & FEDIT_SYSBOX) && errno == ENOENT) {
         if (strcmp(who, myname) && getpwnam(who) == NULL) {
            n_err(_("\"%s\" is not a user of this system\n"), who);
            goto jem2;
         }
         if (!(options & OPT_QUICKRUN_MASK) && ok_blook(bsdcompat))
            n_err(_("No mail for %s\n"), who);
         if (fm & FEDIT_NEWMAIL)
            goto jleave;
         if (ok_blook(emptystart)) {
            if (!(options & OPT_QUICKRUN_MASK) && !ok_blook(bsdcompat))
               n_perr(name, 0);
            /* We must avoid returning -1 and causing program exit */
            mb.mb_type = MB_VOID;
            rv = 1;
            goto jleave;
         }
         goto jem2;
      } else if (fm & FEDIT_NEWMAIL)
         goto jleave;
      n_perr(name, 0);
      goto jem1;
   }

   if (fstat(fileno(ibuf), &stb) == -1) {
      if (fm & FEDIT_NEWMAIL)
         goto jleave;
      n_perr(_("fstat"), 0);
      goto jem1;
   }

   if (S_ISREG(stb.st_mode) || isdevnull) {
      /* EMPTY */
   } else {
      if (fm & FEDIT_NEWMAIL)
         goto jleave;
      errno = S_ISDIR(stb.st_mode) ? EISDIR : EINVAL;
      n_perr(name, 0);
      goto jem1;
   }

   /* Looks like all will be well.  We must now relinquish our hold on the
    * current set of stuff.  Must hold signals while we are reading the new
    * file, else we will ruin the message[] data structure */

   hold_sigs(); /* TODO note on this one in quit.c:quit() */
   if (shudclob && !(fm & FEDIT_NEWMAIL))
      quit();
#ifdef HAVE_SOCKETS
   if (!(fm & FEDIT_NEWMAIL) && mb.mb_sock.s_fd >= 0)
      sclose(&mb.mb_sock); /* TODO sorry? VMAILFS->close(), thank you */
#endif

   /* TODO There is no intermediate VOID box we've switched to: name may
    * TODO point to the same box that we just have written, so any updates
    * TODO we won't see!  Reopen again in this case.  RACY! Goes with VOID! */
   /* TODO In addition: in case of compressed/hook boxes we lock a temporary! */
   /* TODO We may uselessly open twice but quit() doesn't say wether we were
    * TODO modified so we can't tell: Mailbox::is_modified() :-(( */
   if (/*shudclob && !(fm & FEDIT_NEWMAIL) &&*/ !strcmp(name, mailname)) {
      name = mailname;
      Fclose(ibuf);

      if ((ibuf = Zopen(name, "r")) == NULL ||
            fstat(fileno(ibuf), &stb) == -1 ||
            (!S_ISREG(stb.st_mode) && !isdevnull)) {
         n_perr(name, 0);
         rele_sigs();
         goto jem2;
      }
   }

   /* Copy the messages into /tmp and set pointers */
   if (!(fm & FEDIT_NEWMAIL)) {
      mb.mb_type = MB_FILE;
      mb.mb_perm = (((options & OPT_R_FLAG) || (fm & FEDIT_RDONLY) ||
            access(name, W_OK) < 0) ? 0 : MB_DELE | MB_EDIT);
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
      shudclob = 1;
      if (fm & FEDIT_SYSBOX)
         pstate &= ~PS_EDIT;
      else
         pstate |= PS_EDIT;
      initbox(name);
      offset = 0;
   } else {
      fseek(mb.mb_otf, 0L, SEEK_END);
      /* TODO Doing this without holding a lock is.. And not err checking.. */
      fseek(ibuf, mailsize, SEEK_SET);
      offset = mailsize;
      omsgCount = msgCount;
   }

   if (isdevnull)
      lckfp = (FILE*)-1;
   else if (!(pstate & PS_EDIT))
      lckfp = dot_lock(name, fileno(ibuf), FLT_READ, offset,0,
            (fm & FEDIT_NEWMAIL ? 0 : 1));
   else if (file_lock(fileno(ibuf), FLT_READ, offset,0,
         (fm & FEDIT_NEWMAIL ? 0 : 1)))
      lckfp = (FILE*)-1;

   if (lckfp == NULL) {
      if (!(fm & FEDIT_NEWMAIL)) {
         char const *emsg = (pstate & PS_EDIT)
               ? N_("Unable to lock mailbox, aborting operation")
               : N_("Unable to (dot) lock mailbox, aborting operation");
         n_perr(V_(emsg), 0);
      }
      rele_sigs();
      if (!(fm & FEDIT_NEWMAIL))
         goto jem1;
      goto jleave;
   }

   mailsize = fsize(ibuf);

   /* TODO This is too simple minded?  We should regenerate an index file
    * TODO to be able to truly tell wether *anything* has changed! */
   if ((fm & FEDIT_NEWMAIL) && UICMP(z, mailsize, <=, offset)) {
      rele_sigs();
      goto jleave;
   }
   setptr(ibuf, offset);
   setmsize(msgCount);
   if ((fm & FEDIT_NEWMAIL) && mb.mb_sorted) {
      mb.mb_threaded = 0;
      c_sort((void*)-1);
   }

   Fclose(ibuf);
   ibuf = NULL;
   if (lckfp != NULL && lckfp != (FILE*)-1) {
      Pclose(lckfp, FAL0);
      /*lckfp = NULL;*/
   }
   rele_sigs();

   if (!(fm & FEDIT_NEWMAIL)) {
      pstate &= ~PS_SAW_COMMAND;
      pstate |= PS_SETFILE_OPENED;
   }

   if ((options & OPT_EXISTONLY) && !(options & OPT_HEADERLIST)) {
      rv = (msgCount == 0);
      goto jleave;
   }

   if ((!(pstate & PS_EDIT) || (fm & FEDIT_NEWMAIL)) && msgCount == 0) {
      if (!(fm & FEDIT_NEWMAIL)) {
         if (!ok_blook(emptystart))
            n_err(_("No mail for %s\n"), who);
      }
      goto jleave;
   }

   if (fm & FEDIT_NEWMAIL)
      newmailinfo(omsgCount);

   rv = 0;
jleave:
   if (ibuf != NULL) {
      Fclose(ibuf);
      if (lckfp != NULL && lckfp != (FILE*)-1)
         Pclose(lckfp, FAL0);
   }
   NYD_LEAVE;
   return rv;
jem2:
   mb.mb_type = MB_VOID;
jem1:
   rv = -1;
   goto jleave;
}

FL int
newmailinfo(int omsgCount)
{
   int mdot, i;
   NYD_ENTER;

   for (i = 0; i < omsgCount; ++i)
      message[i].m_flag &= ~MNEWEST;

   if (msgCount > omsgCount) {
      for (i = omsgCount; i < msgCount; ++i)
         message[i].m_flag |= MNEWEST;
      printf(_("New mail has arrived.\n"));
      if ((i = msgCount - omsgCount) == 1)
         printf(_("Loaded 1 new message.\n"));
      else
         printf(_("Loaded %d new messages.\n"), i);
   } else
      printf(_("Loaded %d messages.\n"), msgCount);

   check_folder_hook(TRU1);

   mdot = getmdot(1);

   if (ok_blook(header))
      print_headers(omsgCount + 1, msgCount, FAL0);
   NYD_LEAVE;
   return mdot;
}

FL void
setmsize(int sz)
{
   NYD_ENTER;
   if (n_msgvec != NULL)
      free(n_msgvec);
   n_msgvec = scalloc(sz + 1, sizeof *n_msgvec);
   NYD_LEAVE;
}

FL void
print_header_summary(char const *Larg)
{
   size_t bot, top, i, j;
   NYD_ENTER;

   if (Larg != NULL) {
      /* Avoid any messages XXX add a make_mua_silent() and use it? */
      if ((options & (OPT_VERB | OPT_EXISTONLY)) == OPT_EXISTONLY) {
         freopen("/dev/null", "w", stdout);
         freopen("/dev/null", "w", stderr);
      }
      assert(n_msgvec != NULL);
      i = (getmsglist(/*TODO make arg const */UNCONST(Larg), n_msgvec, 0) <= 0);
      if (options & OPT_EXISTONLY) {
         exit_status = (int)i;
         goto jleave;
      }
      if (i)
         goto jleave;
      for (bot = msgCount, top = 0, i = 0; (j = n_msgvec[i]) != 0; ++i) {
         if (bot > j)
            bot = j;
         if (top < j)
            top = j;
      }
   } else
      bot = 1, top = msgCount;
   print_headers(bot, top, (Larg != NULL)); /* TODO should take iterator!! */
jleave:
   NYD_LEAVE;
}

FL void
announce(int printheaders)
{
   int vec[2], mdot;
   NYD_ENTER;

   mdot = newfileinfo();
   vec[0] = mdot;
   vec[1] = 0;
   dot = message + mdot - 1;
   if (printheaders && msgCount > 0 && ok_blook(header)) {
      print_header_group(vec); /* XXX errors? */
   }
   NYD_LEAVE;
}

FL int
newfileinfo(void)
{
   struct message *mp;
   int u, n, mdot, d, s, hidden, moved;
   NYD_ENTER;

   if (mb.mb_type == MB_VOID) {
      mdot = 1;
      goto jleave;
   }

   mdot = getmdot(0);
   s = d = hidden = moved =0;
   for (mp = message, n = 0, u = 0; PTRCMP(mp, <, message + msgCount); ++mp) {
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
   printf(_("\"%s\": "),
      (_update_mailname(NULL) ? displayname : mailname));
   if (msgCount == 1)
      printf(_("1 message"));
   else
      printf(_("%d messages"), msgCount);
   if (n > 0)
      printf(_(" %d new"), n);
   if (u-n > 0)
      printf(_(" %d unread"), u);
   if (d > 0)
      printf(_(" %d deleted"), d);
   if (s > 0)
      printf(_(" %d saved"), s);
   if (moved > 0)
      printf(_(" %d moved"), moved);
   if (hidden > 0)
      printf(_(" %d hidden"), hidden);
   else if (mb.mb_perm == 0)
      printf(_(" [Read only]"));
   printf("\n");
jleave:
   NYD_LEAVE;
   return mdot;
}

FL int
getmdot(int nmail)
{
   struct message *mp;
   char *cp;
   int mdot;
   enum mflag avoid = MHIDDEN | MDELETED;
   NYD_ENTER;

   if (!nmail) {
      if (ok_blook(autothread)) {
         OBSOLETE(_("please use *autosort=thread* instead of *autothread*"));
         c_thread(NULL);
      } else if ((cp = ok_vlook(autosort)) != NULL) {
         if (mb.mb_sorted != NULL)
            free(mb.mb_sorted);
         mb.mb_sorted = sstrdup(cp);
         c_sort(NULL);
      }
   }
   if (mb.mb_type == MB_VOID) {
      mdot = 1;
      goto jleave;
   }

   if (nmail)
      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if ((mp->m_flag & (MNEWEST | avoid)) == MNEWEST)
            break;

   if (!nmail || PTRCMP(mp, >=, message + msgCount)) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if ((mp->m_flag & (MNEW | avoid)) == MNEW)
               break;
      } else {
         for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
            if ((mp->m_flag & (MNEW | avoid)) == MNEW)
               break;
      }
   }

   if ((mb.mb_threaded ? (mp == NULL) : PTRCMP(mp, >=, message + msgCount))) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if (mp->m_flag & MFLAGGED)
               break;
      } else {
         for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
            if (mp->m_flag & MFLAGGED)
               break;
      }
   }

   if ((mb.mb_threaded ? (mp == NULL) : PTRCMP(mp, >=, message + msgCount))) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if (!(mp->m_flag & (MREAD | avoid)))
               break;
      } else {
         for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
            if (!(mp->m_flag & (MREAD | avoid)))
               break;
      }
   }

   if (nmail &&
         (mb.mb_threaded ? (mp != NULL) : PTRCMP(mp, <, message + msgCount)))
      mdot = (int)PTR2SIZE(mp - message + 1);
   else if (ok_blook(showlast)) {
      if (mb.mb_threaded) {
         for (mp = this_in_thread(threadroot, -1); mp;
               mp = prev_in_thread(mp))
            if (!(mp->m_flag & avoid))
               break;
         mdot = (mp != NULL) ? (int)PTR2SIZE(mp - message + 1) : msgCount;
      } else {
         for (mp = message + msgCount - 1; mp >= message; --mp)
            if (!(mp->m_flag & avoid))
               break;
         mdot = (mp >= message) ? (int)PTR2SIZE(mp - message + 1) : msgCount;
      }
   } else if (!nmail &&
         (mb.mb_threaded ? (mp != NULL) : PTRCMP(mp, <, message + msgCount)))
      mdot = (int)PTR2SIZE(mp - message + 1);
   else if (mb.mb_threaded) {
      for (mp = threadroot; mp; mp = next_in_thread(mp))
         if (!(mp->m_flag & avoid))
            break;
      mdot = (mp != NULL) ? (int)PTR2SIZE(mp - message + 1) : 1;
   } else {
      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if (!(mp->m_flag & avoid))
            break;
      mdot = PTRCMP(mp, <, message + msgCount)
            ? (int)PTR2SIZE(mp - message + 1) : 1;
   }
jleave:
   NYD_LEAVE;
   return mdot;
}

FL void
initbox(char const *name)
{
   char *tempMesg;
   NYD_ENTER;

   if (mb.mb_type != MB_VOID)
      n_strscpy(prevfile, mailname, PATH_MAX);

   /* TODO name always NE mailname (but goes away for objects anyway)
    * TODO Well, not true no more except that in parens */
   _update_mailname((name != mailname) ? name : NULL);

   if ((mb.mb_otf = Ftmp(&tempMesg, "tmpmbox", OF_WRONLY | OF_HOLDSIGS)) ==
         NULL) {
      n_perr(_("temporary mail message file"), 0);
      exit(EXIT_ERR);
   }
   if ((mb.mb_itf = safe_fopen(tempMesg, "r", NULL)) == NULL) {
      n_perr(_("temporary mail message file"), 0);
      exit(EXIT_ERR);
   }
   Ftmp_release(&tempMesg);

   message_reset();
   mb.mb_threaded = 0;
   if (mb.mb_sorted != NULL) {
      free(mb.mb_sorted);
      mb.mb_sorted = NULL;
   }
   dot = prevdot = threadroot = NULL;
   pstate &= ~PS_DID_PRINT_DOT;
   NYD_LEAVE;
}

FL bool_t
getfold(char *name, size_t size)
{
   char const *folder;
   NYD_ENTER;

   if ((folder = ok_vlook(folder)) != NULL)
      n_strscpy(name, folder, size);
   NYD_LEAVE;
   return (folder != NULL);
}

/* s-it-mode */
