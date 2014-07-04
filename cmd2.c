/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ More user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/wait.h>

/* Save/copy the indicated messages at the end of the passed file name.
 * If mark is true, mark the message "saved" */
static int     save1(char *str, int domark, char const *cmd,
                  struct ignoretab *ignore, int convert, int sender_record,
                  int domove);

/* Snarf the file from the end of the command line and return a pointer to it.
 * If there is no file attached, return the mbox file.  Put a null in front of
 * the file name so that the message list processing won't see it, unless the
 * file name is the only thing on the line, in which case, return 0 in the
 * reference flag variable */
static char *  snarf(char *linebuf, bool_t *flag, bool_t usembox);

/* Delete the indicated messages.  Set dot to some nice place afterwards */
static int     delm(int *msgvec);

static int     ignore1(char **list, struct ignoretab *tab, char const *which);

/* Print out all currently retained fields */
static int     igshow(struct ignoretab *tab, char const *which);

/* Compare two names for sorting ignored field list */
static int     igcomp(void const *l, void const *r);

/*  */
static int     _unignore(char **list, struct ignoretab *tab, char const *which);
static void    __unign_all(struct ignoretab *tab);
static void    __unign_one(struct ignoretab *tab, char const *name);

static int
save1(char *str, int domark, char const *cmd, struct ignoretab *ignoret,
   int convert, int sender_record, int domove)
{
   off_t mstats[2], tstats[2];
   struct stat st;
   int newfile = 0, compressed = 0, last = 0, *msgvec, *ip;
   struct message *mp;
   char *file = NULL, *cp, *cq;
   char const *disp = "";
   FILE *obuf;
   enum protocol prot;
   bool_t success = FAL0, f;
   NYD_ENTER;

   msgvec = salloc((msgCount + 2) * sizeof *msgvec);
   if (sender_record) {
      for (cp = str; *cp != '\0' && blankchar(*cp); ++cp)
         ;
      f = (*cp != '\0');
   } else {
      if ((file = snarf(str, &f, convert != SEND_TOFILE)) == NULL)
         goto jleave;
   }

   if (!f) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (inhook) {
            success = TRU1;
            goto jleave;
         }
         printf(_("No messages to %s.\n"), cmd);
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;
   if (*msgvec == 0) {
      if (inhook) {
         success = TRU1;
         goto jleave;
      }
      printf("No applicable messages.\n");
      goto jleave;
   }

   if (sender_record) {
      if ((cp = nameof(message + *msgvec - 1, 0)) == NULL) {
         printf(_("Cannot determine message sender to %s.\n"), cmd);
         goto jleave;
      }

      for (cq = cp; *cq != '\0' && *cq != '@'; cq++)
         ;
      *cq = '\0';
      if (ok_blook(outfolder)) {
         size_t sz = strlen(cp) +1;
         file = salloc(sz + 1);
         file[0] = '+';
         memcpy(file + 1, cp, sz);
      } else
         file = cp;
   }

   if ((file = expand(file)) == NULL)
      goto jleave;
   prot = which_protocol(file);
   if (prot != PROTO_IMAP) {
      if (access(file, 0) >= 0) {
         newfile = 0;
         disp = _("[Appended]");
      } else {
         newfile = 1;
         disp = _("[New file]");
      }
   }

   obuf = ((convert == SEND_TOFILE) ? Fopen(file, "a+")
         : Zopen(file, "a+", &compressed));
   if (obuf == NULL) {
      obuf = ((convert == SEND_TOFILE) ? Fopen(file, "wx")
            : Zopen(file, "wx", &compressed));
      if (obuf == NULL) {
         perror(file);
         goto jleave;
      }
   } else {
      if (compressed) {
         newfile = 0;
         disp = _("[Appended]");
      }
      if (!newfile && fstat(fileno(obuf), &st) && S_ISREG(st.st_mode) &&
            fseek(obuf, -2L, SEEK_END) == 0) {
         char buf[2];
         int prependnl = 0;

         switch (fread(buf, sizeof *buf, 2, obuf)) {
         case 2:
            if (buf[1] != '\n') {
               prependnl = 1;
               break;
            }
            /* FALLTHRU */
         case 1:
            if (buf[0] != '\n')
               prependnl = 1;
            break;
         default:
            if (ferror(obuf)) {
               perror(file);
               goto jleave;
            }
            prependnl = 0;
         }
         fflush(obuf);
         if (prependnl) {
            putc('\n', obuf);
            fflush(obuf);
         }
      }
   }

   success = TRU1;
   tstats[0] = tstats[1] = 0;
   imap_created_mailbox = 0;
   srelax_hold();
   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip) {
      mp = message + *ip - 1;
      if (prot == PROTO_IMAP && ignoret[0].i_count == 0 &&
            ignoret[1].i_count == 0
#ifdef HAVE_IMAP /* TODO revisit */
            && imap_thisaccount(file)
#endif
      ) {
#ifdef HAVE_IMAP
         if (imap_copy(mp, *ip, file) == STOP)
#endif
            goto jferr;
#ifdef HAVE_IMAP
         mstats[0] = -1;
         mstats[1] = mp->m_xsize;
#endif
      } else if (sendmp(mp, obuf, ignoret, NULL, convert, mstats) < 0) {
         perror(file);
         goto jferr;
      }
      srelax();
      touch(mp);
      if (domark)
         mp->m_flag |= MSAVED;
      if (domove) {
         mp->m_flag |= MDELETED | MSAVED;
         last = *ip;
      }
      tstats[0] += mstats[0];
      tstats[1] += mstats[1];
   }
   fflush(obuf);
   if (ferror(obuf)) {
      perror(file);
jferr:
      success = FAL0;
   }
   if (Fclose(obuf) != 0)
      success = FAL0;
   srelax_rele();

   if (success) {
      if (prot == PROTO_IMAP || prot == PROTO_MAILDIR) {
         disp = (
#ifdef HAVE_IMAP
            ((prot == PROTO_IMAP) && disconnected(file)) ? "[Queued]" :
#endif
            (imap_created_mailbox ? "[New file]" : "[Appended]"));
      }
      printf("\"%s\" %s ", file, disp);
      if (tstats[0] >= 0)
         printf("%lu", (ul_it)tstats[0]);
      else
         printf(_("binary"));
      printf("/%lu\n", (ul_it)tstats[1]);
   } else if (domark) {
      newfile = ~MSAVED;
      goto jiterand;
   } else if (domove) {
      newfile = ~(MSAVED | MDELETED);
jiterand:
      for (ip = msgvec; *ip != 0 &&
            UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount); ++ip) {
         mp = message + *ip - 1;
         mp->m_flag &= newfile;
      }
   }

   if (domove && last && success) {
      setdot(message + last - 1);
      last = first(0, MDELETED);
      setdot(message + (last != 0 ? last - 1 : 0));
   }
jleave:
   NYD_LEAVE;
   return (success == FAL0);
}

static char *
snarf(char *linebuf, bool_t *flag, bool_t usembox)
{
   char *cp;
   NYD_ENTER;

   if ((cp = laststring(linebuf, flag, 0)) == NULL) {
      if (usembox) {
         *flag = FAL0;
         cp = expand("&");
      } else
         fprintf(stderr, _("No file specified.\n"));
   }
   NYD_LEAVE;
   return cp;
}

static int
delm(int *msgvec)
{
   struct message *mp;
   int rv = -1, *ip, last;
   NYD_ENTER;

   last = 0;
   for (ip = msgvec; *ip != 0; ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      mp->m_flag |= MDELETED | MTOUCH;
      mp->m_flag &= ~(MPRESERVE | MSAVED | MBOX);
      last = *ip;
   }
   if (last != 0) {
      setdot(message + last - 1);
      last = first(0, MDELETED);
      if (last != 0) {
         setdot(message + last - 1);
         rv = 0;
      } else {
         setdot(message);
      }
   }
   NYD_LEAVE;
   return rv;
}

static int
ignore1(char **list, struct ignoretab *tab, char const *which)
{
   int h;
   struct ignore *igp;
   char **ap;
   NYD_ENTER;

   if (*list == NULL) {
      h = igshow(tab, which);
      goto jleave;
   }

   for (ap = list; *ap != 0; ++ap) {
      char *field;
      size_t sz;

      sz = strlen(*ap) +1;
      field = ac_alloc(sz);
      i_strcpy(field, *ap, sz);
      if (member(field, tab))
         goto jnext;

      h = hash(field);
      igp = scalloc(1, sizeof *igp);
      sz = strlen(field) +1;
      igp->i_field = smalloc(sz);
      memcpy(igp->i_field, field, sz);
      igp->i_link = tab->i_head[h];
      tab->i_head[h] = igp;
      ++tab->i_count;
jnext:
      ac_free(field);
   }
   h = 0;
jleave:
   NYD_LEAVE;
   return h;
}

static int
igshow(struct ignoretab *tab, char const *which)
{
   int h;
   struct ignore *igp;
   char **ap, **ring;
   NYD_ENTER;

   if (tab->i_count == 0) {
      printf(_("No fields currently being %s.\n"), which);
      goto jleave;
   }

   ring = salloc((tab->i_count + 1) * sizeof *ring);
   ap = ring;
   for (h = 0; h < HSHSIZE; ++h)
      for (igp = tab->i_head[h]; igp != 0; igp = igp->i_link)
         *ap++ = igp->i_field;
   *ap = 0;

   qsort(ring, tab->i_count, sizeof *ring, igcomp);

   for (ap = ring; *ap != NULL; ++ap)
      printf("%s\n", *ap);
jleave:
   NYD_LEAVE;
   return 0;
}

static int
igcomp(void const *l, void const *r)
{
   int rv;
   NYD_ENTER;

   rv = strcmp(*(char**)UNCONST(l), *(char**)UNCONST(r));
   NYD_LEAVE;
   return rv;
}

static int
_unignore(char **list, struct ignoretab *tab, char const *which)
{
   char *field;
   NYD_ENTER;

   if (tab->i_count == 0)
      printf(_("No fields currently being %s.\n"), which);
   else
      while ((field = *list++) != NULL)
         if (field[0] == '*' && field[1] == '\0') {
            __unign_all(tab);
            break;
         } else
            __unign_one(tab, field);
   NYD_LEAVE;
   return 0;
}

static void
__unign_all(struct ignoretab *tab)
{
   size_t i;
   struct ignore *n, *x;
   NYD_ENTER;

   for (i = 0; i < NELEM(tab->i_head); ++i)
      for (n = tab->i_head[i]; n != NULL; n = x) {
         x = n->i_link;
         free(n->i_field);
         free(n);
      }
   memset(tab, 0, sizeof *tab);
   NYD_LEAVE;
}

static void
__unign_one(struct ignoretab *tab, char const *name)
{
   struct ignore *ip, *iq;
   int h;
   NYD_ENTER;

   h = hash(name);
   for (iq = NULL, ip = tab->i_head[h]; ip != NULL; ip = ip->i_link) {
      if (!asccasecmp(ip->i_field, name)) {
         free(ip->i_field);
         if (iq != NULL)
            iq->i_link = ip->i_link;
         else
            tab->i_head[h] = ip->i_link;
         free(ip);
         --tab->i_count;
         break;
      }
      iq = ip;
   }
   NYD_LEAVE;
}

FL int
c_next(void *v)
{
   int list[2], *ip, *ip2, mdot, *msgvec = v, rv = 1;
   struct message *mp;
   NYD_ENTER;

   if (*msgvec != 0) {
      /* If some messages were supplied, find the first applicable one
       * following dot using wrap around */
      mdot = (int)PTR2SIZE(dot - message + 1);

      /* Find first message in supplied message list which follows dot */
      for (ip = msgvec; *ip != 0; ++ip) {
         if ((mb.mb_threaded ? message[*ip - 1].m_threadpos > dot->m_threadpos
               : *ip > mdot))
            break;
      }
      if (*ip == 0)
         ip = msgvec;
      ip2 = ip;
      do {
         mp = message + *ip2 - 1;
         if (!(mp->m_flag & MMNDEL)) {
            setdot(mp);
            goto jhitit;
         }
         if (*ip2 != 0)
            ++ip2;
         if (*ip2 == 0)
            ip2 = msgvec;
      } while (ip2 != ip);
      printf(_("No messages applicable\n"));
      goto jleave;
   }

   /* If this is the first command, select message 1.  Note that this must
    * exist for us to get here at all */
   if (!sawcom) {
      if (msgCount == 0)
         goto jateof;
      goto jhitit;
   }

   /* Just find the next good message after dot, no wraparound */
   if (mb.mb_threaded == 0) {
      for (mp = dot + did_print_dot; PTRCMP(mp, <, message + msgCount); ++mp)
         if (!(mp->m_flag & MMNORM))
            break;
   } else {
      mp = dot;
      if (did_print_dot)
         mp = next_in_thread(mp);
      while (mp && (mp->m_flag & MMNORM))
         mp = next_in_thread(mp);
   }
   if (mp == NULL || PTRCMP(mp, >=, message + msgCount)) {
jateof:
      printf(_("At EOF\n"));
      rv = 0;
      goto jleave;
   }
   setdot(mp);

   /* Print dot */
jhitit:
   list[0] = (int)PTR2SIZE(dot - message + 1);
   list[1] = 0;
   rv = c_type(list);
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_save(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 1, "save", saveignore, SEND_MBOX, 0, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Save(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 1, "save", saveignore, SEND_MBOX, 1, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_copy(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "copy", saveignore, SEND_MBOX, 0, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Copy(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "copy", saveignore, SEND_MBOX, 1, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_move(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "move", saveignore, SEND_MBOX, 0, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Move(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "move", saveignore, SEND_MBOX, 1, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_decrypt(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "decrypt", saveignore, SEND_DECRYPT, 0, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Decrypt(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = save1(str, 0, "decrypt", saveignore, SEND_DECRYPT, 1, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_write(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   if (str == NULL || *str == '\0')
      str = savestr("/dev/null");
   rv = save1(str, 0, "write", allignore, SEND_TOFILE, 0, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_delete(void *v)
{
   int *msgvec = v;
   NYD_ENTER;

   delm(msgvec);
   NYD_LEAVE;
   return 0;
}

FL int
c_deltype(void *v)
{
   int list[2], rv = 0, *msgvec = v, lastdot;
   NYD_ENTER;

   lastdot = (int)PTR2SIZE(dot - message + 1);
   if (delm(msgvec) >= 0) {
      list[0] = (int)PTR2SIZE(dot - message + 1);
      if (list[0] > lastdot) {
         touch(dot);
         list[1] = 0;
         rv = c_type(list);
         goto jleave;
      }
      printf(_("At EOF\n"));
   } else
      printf(_("No more messages\n"));
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_undelete(void *v)
{
   int *msgvec = v, *ip;
   struct message *mp;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      if (mp->m_flag & (MDELETED | MSAVED))
         mp->m_flag &= ~(MDELETED | MSAVED);
      else
         mp->m_flag &= ~MDELETED;
#ifdef HAVE_IMAP
      if (mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE)
         imap_undelete(mp, *ip);
#endif
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_retfield(void *v)
{
   char **list = v;
   int rv;
   NYD_ENTER;

   rv = ignore1(list, ignore + 1, "retained");
   NYD_LEAVE;
   return rv;
}

FL int
c_igfield(void *v)
{
   char **list = v;
   int rv;
   NYD_ENTER;

   rv = ignore1(list, ignore, "ignored");
   NYD_LEAVE;
   return rv;
}

FL int
c_saveretfield(void *v)
{
   char **list = v;
   int rv;
   NYD_ENTER;

   rv = ignore1(list, saveignore + 1, "retained");
   NYD_LEAVE;
   return rv;
}

FL int
c_saveigfield(void *v)
{
   char **list = v;
   int rv;
   NYD_ENTER;

   rv = ignore1(list, saveignore, "ignored");
   NYD_LEAVE;
   return rv;
}

FL int
c_fwdretfield(void *v)
{
   char **list = v;
   int rv;
   NYD_ENTER;

   rv = ignore1(list, fwdignore + 1, "retained");
   NYD_LEAVE;
   return rv;
}

FL int
c_fwdigfield(void *v)
{
   char **list = v;
   int rv;
   NYD_ENTER;

   rv = ignore1(list, fwdignore, "ignored");
   NYD_LEAVE;
   return rv;
}

FL int
c_unignore(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unignore((char**)v, ignore, "ignored");
   NYD_LEAVE;
   return rv;
}

FL int
c_unretain(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unignore((char**)v, ignore + 1, "retained");
   NYD_LEAVE;
   return rv;
}

FL int
c_unsaveignore(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unignore((char**)v, saveignore, "ignored");
   NYD_LEAVE;
   return rv;
}

FL int
c_unsaveretain(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unignore((char**)v, saveignore + 1, "retained");
   NYD_LEAVE;
   return rv;
}

FL int
c_unfwdignore(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unignore((char**)v, fwdignore, "ignored");
   NYD_LEAVE;
   return rv;
}

FL int
c_unfwdretain(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _unignore((char**)v, fwdignore + 1, "retained");
   NYD_LEAVE;
   return rv;
}

/* vim:set fenc=utf-8:s-it-mode */
