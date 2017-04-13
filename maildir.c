/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Maildir folder support. FIXME rewrite - why do we chdir(2)??
 *@ FIXME indeed - my S-Postman Python (!) is faster dealing with maildir!!
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 */
/*
 * Copyright (c) 2004
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef n_FILE
#define n_FILE maildir

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <dirent.h>

struct mditem {
   struct message *md_data;
   unsigned       md_hash;
};

static struct mditem *_maildir_table;
static ui32_t        _maildir_prime;
static sigjmp_buf    _maildir_jmp;

static void             __maildircatch(int s);
static void             __maildircatch_hold(int s);

static unsigned a_maildir_hash(char const *cp);

/* Do some cleanup in the tmp/ subdir */
static void             _cleantmp(void);

static int              _maildir_setfile1(char const *name, enum fedit_mode fm,
                           int omsgCount);

/* In combination with the names from mkname(), this comparison function
 * ensures that the order of messages in a maildir folder created by mailx
 * remains always the same. In effect, if a mbox folder is transferred to
 * a maildir folder by 'copy *', the message order wont' change */
static int              mdcmp(void const *a, void const *b);

static int              _maildir_subdir(char const *name, char const *sub,
                           enum fedit_mode fm);

static void             _maildir_append(char const *name, char const *sub,
                           char const *fn);

static void             readin(char const *name, struct message *m);

static void             maildir_update(void);

static void             _maildir_move(struct message *m);

static char *           mkname(time_t t, enum mflag f, char const *pref);

static enum okay        maildir_append1(char const *name, FILE *fp, off_t off1,
                           long size, enum mflag flag);

static enum okay        trycreate(char const *name);

static enum okay        mkmaildir(char const *name);

static struct message * mdlook(char const *name, struct message *data);

static void             mktable(void);

static enum okay        subdir_remove(char const *name, char const *sub);

static void
__maildircatch(int s)
{
   NYD_X; /* Signal handler */
   siglongjmp(_maildir_jmp, s);
}

static void
__maildircatch_hold(int s)
{
   NYD_X; /* Signal handler */
   n_UNUSED(s);
   /* TODO no STDIO in signal handler, no _() tr's -- pre-translate interrupt
    * TODO globally; */
   n_err_sighdl(_("\nImportant operation in progress: "
      "interrupt again to forcefully abort\n"));
   safe_signal(SIGINT, &__maildircatch);
}

static unsigned
a_maildir_hash(char const *cp) /* TODO obsolete that -> n_torek_hash */
{
   unsigned h = 0, g;
   NYD_ENTER;

   cp--;
   while (*++cp) {
      h = (h << 4 & 0xffffffff) + (*cp&0377);
      if ((g = h & 0xf0000000) != 0) {
         h = h ^ g >> 24;
         h = h ^ g;
      }
   }
   NYD_LEAVE;
   return h;
}

static void
_cleantmp(void)
{
   char dep[PATH_MAX];
   struct stat st;
   time_t now;
   DIR *dirp;
   struct dirent *dp;
   NYD_ENTER;

   if ((dirp = opendir("tmp")) == NULL)
      goto jleave;

   now = n_time_epoch();
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.')
         continue;
      sstpcpy(sstpcpy(dep, "tmp/"), dp->d_name);
      if (stat(dep, &st) == -1)
         continue;
      if (st.st_atime + 36*3600 < now)
         unlink(dep);
   }
   closedir(dirp);
jleave:
   NYD_LEAVE;
}

static int
_maildir_setfile1(char const *name, enum fedit_mode fm, int omsgCount)
{
   int i;
   NYD_ENTER;

   if (!(fm & FEDIT_NEWMAIL))
      _cleantmp();

   mb.mb_perm = ((n_poption & n_PO_R_FLAG) || (fm & FEDIT_RDONLY))
         ? 0 : MB_DELE;
   if ((i = _maildir_subdir(name, "cur", fm)) != 0)
      goto jleave;
   if ((i = _maildir_subdir(name, "new", fm)) != 0)
      goto jleave;
   _maildir_append(name, NULL, NULL);

   srelax_hold();
   for (i = ((fm & FEDIT_NEWMAIL) ? omsgCount : 0); i < msgCount; ++i) {
      readin(name, message + i);
      srelax();
   }
   srelax_rele();

   if (fm & FEDIT_NEWMAIL) {
      if (msgCount > omsgCount)
         qsort(&message[omsgCount], msgCount - omsgCount, sizeof *message,
            &mdcmp);
   } else if (msgCount)
      qsort(message, msgCount, sizeof *message, &mdcmp);
   i = msgCount;
jleave:
   NYD_LEAVE;
   return i;
}

static int
mdcmp(void const *a, void const *b)
{
   struct message const *mpa = a, *mpb = b;
   long i;
   NYD_ENTER;

   if ((i = mpa->m_time - mpb->m_time) == 0)
      i = strcmp(mpa->m_maildir_file + 4, mpb->m_maildir_file + 4);
   NYD_LEAVE;
   return i;
}

static int
_maildir_subdir(char const *name, char const *sub, enum fedit_mode fm)
{
   DIR *dirp;
   struct dirent *dp;
   int rv;
   NYD_ENTER;

   if ((dirp = opendir(sub)) == NULL) {
      n_err(_("Cannot open directory %s\n"),
         n_shexp_quote_cp(savecatsep(name, '/', sub), FAL0));
      rv = -1;
      goto jleave;
   }
   if (access(sub, W_OK) == -1)
      mb.mb_perm = 0;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
         continue;
      if (dp->d_name[0] == '.')
         continue;
      if (!(fm & FEDIT_NEWMAIL) || mdlook(dp->d_name, NULL) == NULL)
         _maildir_append(name, sub, dp->d_name);
   }
   closedir(dirp);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static void
_maildir_append(char const *name, char const *sub, char const *fn)
{
   struct message *m;
   size_t sz, i;
   time_t t = 0;
   enum mflag f = MUSED | MNOFROM | MNEWEST;
   char const *cp, *xp;
   NYD_ENTER;
   n_UNUSED(name);

   if (fn != NULL && sub != NULL) {
      if (!strcmp(sub, "new"))
         f |= MNEW;

      /* C99 */{
         si64_t tib;

         (void)/*TODO*/n_idec_si64_cp(&tib, fn, 10, &xp);
         t = (time_t)tib;
      }

      if ((cp = strrchr(xp, ',')) != NULL && PTRCMP(cp, >, xp + 2) &&
            cp[-1] == '2' && cp[-2] == ':') {
         while (*++cp != '\0') {
            switch (*cp) {
            case 'F':
               f |= MFLAGGED;
               break;
            case 'R':
               f |= MANSWERED;
               break;
            case 'S':
               f |= MREAD;
               break;
            case 'T':
               f |= MDELETED;
               break;
            case 'D':
               f |= MDRAFT;
               break;
            }
         }
      }
   }

   /* Ensure room (and a NULLified last entry) */
   ++msgCount;
   message_append(NULL);
   --msgCount;

   if (fn == NULL || sub == NULL)
      goto jleave;

   m = message + msgCount++;
   i = strlen(fn);
   m->m_maildir_file = smalloc((sz = strlen(sub)) + i + 1 +1);
   memcpy(m->m_maildir_file, sub, sz);
   m->m_maildir_file[sz] = '/';
   memcpy(m->m_maildir_file + sz + 1, fn, i +1);
   m->m_time = t;
   m->m_flag = f;
   m->m_maildir_hash = ~a_maildir_hash(fn);
jleave:
   NYD_LEAVE;
   return;
}

static void
readin(char const *name, struct message *m)
{
   char *buf;
   size_t bufsize, buflen, cnt;
   long size = 0, lines = 0;
   off_t offset;
   FILE *fp;
   int emptyline = 0;
   NYD_ENTER;

   if ((fp = Fopen(m->m_maildir_file, "r")) == NULL) {
      n_err(_("Cannot read %s for message %lu\n"),
         n_shexp_quote_cp(savecatsep(name, '/', m->m_maildir_file), FAL0),
         (ul_i)PTR2SIZE(m - message + 1));
      m->m_flag |= MHIDDEN;
      goto jleave;
   }

   cnt = fsize(fp);
   fseek(mb.mb_otf, 0L, SEEK_END);
   offset = ftell(mb.mb_otf);
   buf = smalloc(bufsize = LINESIZE);
   buflen = 0;
   while (fgetline(&buf, &bufsize, &cnt, &buflen, fp, 1) != NULL) {
      /* Since we simply copy over data without doing any transfer
       * encoding reclassification/adjustment we *have* to perform
       * RFC 4155 compliant From_ quoting here */
      if (emptyline && is_head(buf, buflen, FAL0)) {
         putc('>', mb.mb_otf);
         ++size;
      }
      size += fwrite(buf, 1, buflen, mb.mb_otf);/*XXX err hdling*/
      emptyline = (*buf == '\n');
      ++lines;
   }
   free(buf);
   if (!emptyline) {
      putc('\n', mb.mb_otf);
      ++lines;
      ++size;
   }

   Fclose(fp);
   fflush(mb.mb_otf);
   m->m_size = m->m_xsize = size;
   m->m_lines = m->m_xlines = lines;
   m->m_block = mailx_blockof(offset);
   m->m_offset = mailx_offsetof(offset);
   substdate(m);
jleave:
   NYD_LEAVE;
}

static void
maildir_update(void)
{
   struct message *m;
   int dodel, c, gotcha = 0, held = 0, modflags = 0;
   NYD_ENTER;

   if (mb.mb_perm == 0)
      goto jfree;

   if (!(n_pstate & n_PS_EDIT)) {
      holdbits();
      for (m = message, c = 0; PTRCMP(m, <, message + msgCount); ++m) {
         if (m->m_flag & MBOX)
            c++;
      }
      if (c > 0)
         if (makembox() == STOP)
            goto jbypass;
   }
   for (m = message, gotcha = 0, held = 0; PTRCMP(m, <, message + msgCount);
         ++m) {
      if (n_pstate & n_PS_EDIT)
         dodel = m->m_flag & MDELETED;
      else
         dodel = !((m->m_flag & MPRESERVE) || !(m->m_flag & MTOUCH));
      if (dodel) {
         if (unlink(m->m_maildir_file) < 0)
            n_err(_("Cannot delete file %s for message %lu\n"),
               n_shexp_quote_cp(savecatsep(mailname, '/', m->m_maildir_file),
                  FAL0), (ul_i)PTR2SIZE(m - message + 1));
         else
            ++gotcha;
      } else {
         if ((m->m_flag & (MREAD | MSTATUS)) == (MREAD | MSTATUS) ||
               (m->m_flag & (MNEW | MBOXED | MSAVED | MSTATUS | MFLAG |
               MUNFLAG | MANSWER | MUNANSWER | MDRAFT | MUNDRAFT))) {
            _maildir_move(m);
            ++modflags;
         }
         ++held;
      }
   }
jbypass:
   if ((gotcha || modflags) && (n_pstate & n_PS_EDIT)) {
      fprintf(n_stdout, "%s %s\n",
         n_shexp_quote_cp(displayname, FAL0),
         ((ok_blook(bsdcompat) || ok_blook(bsdmsgs))
          ? _("complete") : _("updated.")));
   } else if (held && !(n_pstate & n_PS_EDIT) && mb.mb_perm != 0) {
      if (held == 1)
         fprintf(n_stdout, _("Held 1 message in %s\n"), displayname);
      else
         fprintf(n_stdout, _("Held %d messages in %s\n"), held, displayname);
   }
   fflush(n_stdout);
jfree:
   for (m = message; PTRCMP(m, <, message + msgCount); ++m)
      free(m->m_maildir_file);
   NYD_LEAVE;
}

static void
_maildir_move(struct message *m)
{
   char *fn, *new;
   NYD_ENTER;

   fn = mkname(0, m->m_flag, m->m_maildir_file + 4);
   new = savecat("cur/", fn);
   if (!strcmp(m->m_maildir_file, new))
      goto jleave;
   if (link(m->m_maildir_file, new) == -1) {
      n_err(_("Cannot link %s to %s: message %lu not touched\n"),
         n_shexp_quote_cp(savecatsep(mailname, '/', m->m_maildir_file), FAL0),
         n_shexp_quote_cp(savecatsep(mailname, '/', new), FAL0),
         (ul_i)PTR2SIZE(m - message + 1));
      goto jleave;
   }
   if (unlink(m->m_maildir_file) == -1)
      n_err(_("Cannot unlink %s\n"),
         n_shexp_quote_cp(savecatsep(mailname, '/', m->m_maildir_file), FAL0));
jleave:
   NYD_LEAVE;
}

static char *
mkname(time_t t, enum mflag f, char const *pref)
{
   static unsigned long cnt;
   static pid_t mypid; /* XXX This should possibly be global, somehow */
   static char *node;

   char *cp;
   int size, n, i;
   NYD_ENTER;

   if (pref == NULL) {
      if (mypid == 0)
         mypid = getpid();
      if (node == NULL) {
         cp = n_nodename(FAL0);
         n = size = 0;
         do {
            if (UICMP(32, n, <, size + 8))
               node = srealloc(node, size += 20);
            switch (*cp) {
            case '/':
               node[n++] = '\\', node[n++] = '0',
               node[n++] = '5', node[n++] = '7';
               break;
            case ':':
               node[n++] = '\\', node[n++] = '0',
               node[n++] = '7', node[n++] = '2';
               break;
            default:
               node[n++] = *cp;
            }
         } while (*cp++ != '\0');
      }
      size = 60 + strlen(node);
      cp = salloc(size);
      n = snprintf(cp, size, "%lu.%06lu_%06lu.%s:2,",
            (ul_i)t, (ul_i)mypid, ++cnt, node);
   } else {
      size = (n = strlen(pref)) + 13;
      cp = salloc(size);
      memcpy(cp, pref, n +1);
      for (i = n; i > 3; --i)
         if (cp[i - 1] == ',' && cp[i - 2] == '2' && cp[i - 3] == ':') {
            n = i;
            break;
         }
      if (i <= 3) {
         memcpy(cp + n, ":2,", 3 +1);
         n += 3;
      }
   }
   if (n < size - 7) {
      if (f & MDRAFTED)
         cp[n++] = 'D';
      if (f & MFLAGGED)
         cp[n++] = 'F';
      if (f & MANSWERED)
         cp[n++] = 'R';
      if (f & MREAD)
         cp[n++] = 'S';
      if (f & MDELETED)
         cp[n++] = 'T';
      cp[n] = '\0';
   }
   NYD_LEAVE;
   return cp;
}

static enum okay
maildir_append1(char const *name, FILE *fp, off_t off1, long size,
   enum mflag flag)
{
   char buf[4096], *fn, *tfn, *nfn;
   struct stat st;
   FILE *op;
   time_t now;
   size_t nlen, flen, n;
   enum okay rv = STOP;
   NYD_ENTER;

   nlen = strlen(name);

   /* Create a unique temporary file */
   for (nfn = (char*)0xA /* XXX no magic */;; n_msleep(500, FAL0)) {
      now = n_time_epoch();
      flen = strlen(fn = mkname(now, flag, NULL));
      tfn = salloc(n = nlen + flen + 6);
      snprintf(tfn, n, "%s/tmp/%s", name, fn);

      /* Use "wx" for O_EXCL XXX stat(2) rather redundant; coverity:TOCTOU */
      if ((!stat(tfn, &st) || n_err_no == n_ERR_NOENT) &&
            (op = Fopen(tfn, "wx")) != NULL)
         break;

      nfn = (char*)(PTR2SIZE(nfn) - 1);
      if (nfn == NULL) {
         n_err(_("Can't create an unique file name in %s\n"),
            n_shexp_quote_cp(savecat(name, "/tmp"), FAL0));
         goto jleave;
      }
   }

   if (fseek(fp, off1, SEEK_SET) == -1)
      goto jtmperr;
   while (size > 0) {
      size_t z = UICMP(z, size, >, sizeof buf) ? sizeof buf : (size_t)size;

      if (z != (n = fread(buf, 1, z, fp)) || n != fwrite(buf, 1, n, op)) {
jtmperr:
         n_err(_("Error writing to %s\n"), n_shexp_quote_cp(tfn, FAL0));
         Fclose(op);
         goto jerr;
      }
      size -= n;
   }
   Fclose(op);

   nfn = salloc(n = nlen + flen + 6);
   snprintf(nfn, n, "%s/new/%s", name, fn);
   if (link(tfn, nfn) == -1) {
      n_err(_("Cannot link %s to %s\n"), n_shexp_quote_cp(tfn, FAL0),
         n_shexp_quote_cp(nfn, FAL0));
      goto jerr;
   }
   rv = OKAY;
jerr:
   if (unlink(tfn) == -1)
      n_err(_("Cannot unlink %s\n"), n_shexp_quote_cp(tfn, FAL0));
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
trycreate(char const *name)
{
   struct stat st;
   enum okay rv = STOP;
   NYD_ENTER;

   if (!stat(name, &st)) {
      if (!S_ISDIR(st.st_mode)) {
         n_err(_("%s is not a directory\n"), n_shexp_quote_cp(name, FAL0));
         goto jleave;
      }
   } else if (!n_path_mkdir(name)) {
      n_err(_("Cannot create directory %s\n"), n_shexp_quote_cp(name, FAL0));
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
mkmaildir(char const *name) /* TODO proper cleanup on error; use path[] loop */
{
   char *np;
   size_t sz;
   enum okay rv = STOP;
   NYD_ENTER;

   if (trycreate(name) == OKAY) {
      np = ac_alloc((sz = strlen(name)) + 4 +1);
      memcpy(np, name, sz);
      memcpy(np + sz, "/tmp", 4 +1);
      if (trycreate(np) == OKAY) {
         memcpy(np + sz, "/new", 4 +1);
         if (trycreate(np) == OKAY) {
            memcpy(np + sz, "/cur", 4 +1);
            rv = trycreate(np);
         }
      }
      ac_free(np);
   }
   NYD_LEAVE;
   return rv;
}

static struct message *
mdlook(char const *name, struct message *data)
{
   struct mditem *md;
   ui32_t c, h, n = 0;
   NYD_ENTER;

   if (data && data->m_maildir_hash)
      h = ~data->m_maildir_hash;
   else
      h = a_maildir_hash(name);
   h %= _maildir_prime;
   c = h;
   md = _maildir_table + c;

   while (md->md_data != NULL) {
      if (!strcmp(md->md_data->m_maildir_file + 4, name))
         break;
      c += (n & 1) ? -((n+1)/2) * ((n+1)/2) : ((n+1)/2) * ((n+1)/2);
      n++;
      while (c >= _maildir_prime)
         c -= _maildir_prime;
      md = _maildir_table + c;
   }
   if (data != NULL && md->md_data == NULL)
      md->md_data = data;
   NYD_LEAVE;
   return md->md_data;
}

static void
mktable(void)
{
   struct message *mp;
   size_t i;
   NYD_ENTER;

   _maildir_prime = n_prime_next(msgCount);
   _maildir_table = scalloc(_maildir_prime, sizeof *_maildir_table);
   for (mp = message, i = msgCount; i-- != 0; ++mp)
      mdlook(mp->m_maildir_file + 4, mp);
   NYD_LEAVE;
}

static enum okay
subdir_remove(char const *name, char const *sub)
{
   char *path;
   int pathsize, pathend, namelen, sublen, n;
   DIR *dirp;
   struct dirent *dp;
   enum okay rv = STOP;
   NYD_ENTER;

   namelen = strlen(name);
   sublen = strlen(sub);
   path = smalloc(pathsize = namelen + sublen + 30 +1);
   memcpy(path, name, namelen);
   path[namelen] = '/';
   memcpy(path + namelen + 1, sub, sublen);
   path[namelen + sublen + 1] = '/';
   path[pathend = namelen + sublen + 2] = '\0';

   if ((dirp = opendir(path)) == NULL) {
      n_perr(path, 0);
      goto jleave;
   }
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
         continue;
      if (dp->d_name[0] == '.')
         continue;
      n = strlen(dp->d_name);
      if (UICMP(32, pathend + n +1, >, pathsize))
         path = srealloc(path, pathsize = pathend + n + 30);
      memcpy(path + pathend, dp->d_name, n +1);
      if (unlink(path) == -1) {
         n_perr(path, 0);
         closedir(dirp);
         goto jleave;
      }
   }
   closedir(dirp);

   path[pathend] = '\0';
   if (rmdir(path) == -1) {
      n_perr(path, 0);
      goto jleave;
   }
   rv = OKAY;
jleave:
   free(path);
   NYD_LEAVE;
   return rv;
}

FL int
maildir_setfile(char const * volatile name, enum fedit_mode fm)
{
   sighandler_type volatile saveint;
   struct cw cw;
   int omsgCount;
   int volatile i = -1;
   NYD_ENTER;

   omsgCount = msgCount;
   if (cwget(&cw) == STOP) {
      n_alert(_("Cannot open current directory"));
      goto jleave;
   }

   if (!(fm & FEDIT_NEWMAIL) && !quit(FAL0))
      goto jleave;

   saveint = safe_signal(SIGINT, SIG_IGN);

   if (!(fm & FEDIT_NEWMAIL)) {
      if (fm & FEDIT_SYSBOX)
         n_pstate &= ~n_PS_EDIT;
      else
         n_pstate |= n_PS_EDIT;
      if (mb.mb_itf) {
         fclose(mb.mb_itf);
         mb.mb_itf = NULL;
      }
      if (mb.mb_otf) {
         fclose(mb.mb_otf);
         mb.mb_otf = NULL;
      }
      initbox(name);
      mb.mb_type = MB_MAILDIR;
   }

   if (chdir(name) < 0) {
      n_err(_("Cannot change directory to %s\n"), n_shexp_quote_cp(name, FAL0));
      mb.mb_type = MB_VOID;
      *mailname = '\0';
      msgCount = 0;
      cwrelse(&cw);
      safe_signal(SIGINT, saveint);
      goto jleave;
   }

   _maildir_table = NULL;
   if (sigsetjmp(_maildir_jmp, 1) == 0) {
      if (fm & FEDIT_NEWMAIL)
         mktable();
      if (saveint != SIG_IGN)
         safe_signal(SIGINT, &__maildircatch);
      i = _maildir_setfile1(name, fm, omsgCount);
   }
   if ((fm & FEDIT_NEWMAIL) && _maildir_table != NULL)
      free(_maildir_table);

   safe_signal(SIGINT, saveint);

   if (i < 0) {
      mb.mb_type = MB_VOID;
      *mailname = '\0';
      msgCount = 0;
   }

   if (cwret(&cw) == STOP)
      n_panic(_("Cannot change back to current directory"));
   cwrelse(&cw);

   setmsize(msgCount);
   if ((fm & FEDIT_NEWMAIL) && mb.mb_sorted && msgCount > omsgCount) {
      mb.mb_threaded = 0;
      c_sort((void*)-1);
   }

   if (!(fm & FEDIT_NEWMAIL)) {
      n_pstate &= ~n_PS_SAW_COMMAND;
      n_pstate |= n_PS_SETFILE_OPENED;
   }

   if ((n_poption & n_PO_EXISTONLY) && !(n_poption & n_PO_HEADERLIST)) {
      i = (msgCount == 0);
      goto jleave;
   }

   if (!(fm & FEDIT_NEWMAIL) && (fm & FEDIT_SYSBOX) && msgCount == 0) {
      if (mb.mb_type == MB_MAILDIR /* XXX ?? */ && !ok_blook(emptystart))
         n_err(_("No mail at %s\n"), n_shexp_quote_cp(name, FAL0));
      i = 1;
      goto jleave;
   }

   if ((fm & FEDIT_NEWMAIL) && msgCount > omsgCount)
      newmailinfo(omsgCount);
   i = 0;
jleave:
   NYD_LEAVE;
   return i;
}

FL bool_t
maildir_quit(bool_t hold_sigs_on)
{
   sighandler_type saveint;
   struct cw cw;
   bool_t rv;
   NYD_ENTER;

   if(hold_sigs_on)
      rele_sigs();

   rv = FAL0;

   if (cwget(&cw) == STOP) {
      n_alert(_("Cannot open current directory"));
      goto jleave;
   }

   saveint = safe_signal(SIGINT, SIG_IGN);

   if (chdir(mailname) == -1) {
      n_err(_("Cannot change directory to %s\n"),
         n_shexp_quote_cp(mailname, FAL0));
      cwrelse(&cw);
      safe_signal(SIGINT, saveint);
      goto jleave;
   }

   if (sigsetjmp(_maildir_jmp, 1) == 0) {
      if (saveint != SIG_IGN)
         safe_signal(SIGINT, &__maildircatch_hold);
      maildir_update();
   }

   safe_signal(SIGINT, saveint);

   if (cwret(&cw) == STOP)
      n_panic(_("Cannot change back to current directory"));
   cwrelse(&cw);
   rv = TRU1;
jleave:
   if(hold_sigs_on)
      hold_sigs();
   NYD_LEAVE;
   return rv;
}

FL enum okay
maildir_append(char const *name, FILE *fp, long offset)
{
   char *buf, *bp, *lp;
   size_t bufsize, buflen, cnt;
   off_t off1 = -1, offs;
   long size;
   int flag;
   enum {_NONE = 0, _INHEAD = 1<<0, _NLSEP = 1<<1} state;
   enum okay rv;
   NYD_ENTER;

   if ((rv = mkmaildir(name)) != OKAY)
      goto jleave;

   buf = smalloc(bufsize = LINESIZE); /* TODO line pool; signals */
   buflen = 0;
   cnt = fsize(fp);
   offs = offset /* BSD will move due to O_APPEND! ftell(fp) */;
   size = 0;

   srelax_hold();
   for (flag = MNEW, state = _NLSEP;;) {
      bp = fgetline(&buf, &bufsize, &cnt, &buflen, fp, 1);

      if (bp == NULL ||
            ((state & (_INHEAD | _NLSEP)) == _NLSEP &&
             is_head(buf, buflen, FAL0))) {
         if (off1 != (off_t)-1) {
            if ((rv = maildir_append1(name, fp, off1, size, flag)) == STOP)
               goto jfree;
            srelax();
            if (fseek(fp, offs + buflen, SEEK_SET) == -1) {
               rv = STOP;
               goto jfree;
            }
         }
         off1 = offs + buflen;
         size = 0;
         state = _INHEAD;
         flag = MNEW;

         if (bp == NULL)
            break;
      } else
         size += buflen;
      offs += buflen;

      state &= ~_NLSEP;
      if (buf[0] == '\n') {
         state &= ~_INHEAD;
         state |= _NLSEP;
      } else if (state & _INHEAD) {
         if (!ascncasecmp(buf, "status", 6)) {
            lp = buf + 6;
            while (whitechar(*lp))
               ++lp;
            if (*lp == ':')
               while (*++lp != '\0')
                  switch (*lp) {
                  case 'R':
                     flag |= MREAD;
                     break;
                  case 'O':
                     flag &= ~MNEW;
                     break;
                  }
         } else if (!ascncasecmp(buf, "x-status", 8)) {
            lp = buf + 8;
            while (whitechar(*lp))
               ++lp;
            if (*lp == ':') {
               while (*++lp != '\0')
                  switch (*lp) {
                  case 'F':
                     flag |= MFLAGGED;
                     break;
                  case 'A':
                     flag |= MANSWERED;
                     break;
                  case 'T':
                     flag |= MDRAFTED;
                     break;
                  }
            }
         }
      }
   }
   assert(rv == OKAY);
jfree:
   srelax_rele();
   free(buf);
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
maildir_remove(char const *name)
{
   enum okay rv = STOP;
   NYD_ENTER;

   if (subdir_remove(name, "tmp") == STOP ||
         subdir_remove(name, "new") == STOP ||
         subdir_remove(name, "cur") == STOP)
      goto jleave;
   if (rmdir(name) == -1) {
      n_perr(name, 0);
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
