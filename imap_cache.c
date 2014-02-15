/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ A cache for IMAP.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE(imap_cache)
#ifdef HAVE_IMAP
# include <dirent.h>
# include <fcntl.h>

static char *           encname(struct mailbox *mp, const char *name, int same,
                           const char *box);
static char *           encuid(struct mailbox *mp, unsigned long uid);
static FILE *           clean(struct mailbox *mp, struct cw *cw);
static unsigned long *  builds(long *contentelem);
static void             purge(struct mailbox *mp, struct message *m, long mc,
                           struct cw *cw, const char *name);
static int              longlt(const void *a, const void *b);
static void             remve(unsigned long n);
static FILE *           cache_queue1(struct mailbox *mp, char const *mode,
                           char **xname);
static enum okay        dequeue1(struct mailbox *mp);

static const char infofmt[] = "%c %lu %d %lu %ld";
#define INITSKIP 128L
#define USEBITS(f)  \
   ((f) & (MSAVED|MDELETED|MREAD|MBOXED|MNEW|MFLAGGED|MANSWERED|MDRAFTED))

static const char README1[] = "\
This is a cache directory maintained by " UAGENT "(1).\n\
You should not change any files within.\n\
Nevertheless, the structure is as follows: Each subdirectory of the\n\
current directory represents an IMAP account, and each subdirectory\n\
below that represents a mailbox. Each mailbox directory contains a file\n\
named UIDVALIDITY which describes the validity in relation to the version\n\
on the server. Other files have names corresponding to their IMAP UID.\n";
static const char README2[] = "\n\
The first 128 bytes of these files are used to store message attributes; the\n\
following data is equivalent to compress(1) output. So if you have to save a\n\
message by hand because of an emergency, throw away the first 128 bytes and\n\
decompress the rest, as e.g. 'dd if=MESSAGEFILE skip=1 bs=128 | zcat' does.\n";
static const char README3[] = "\n\
Files named QUEUE contain data that will be sent do the IMAP server next\n\
time a connection is made in online mode.\n";
static const char README4[] = "\n\
You can safely delete any file or directory here, unless it contains a QUEUE\n\
file that is not empty; " UAGENT
   "mailx(1) will download the data again and will also\n\
write new cache entries if configured in this way. If you do not wish to use\n\
the cache anymore, delete the entire directory and unset the 'imap-cache'\n\
variable in " UAGENT "(1).\n";
static const char README5[] = "\n\
For more information about " UAGENT "(1), visit\n\
<http://sdaoden.users.sourceforge.net/code.html>.\n"; /* TODO MAGIC CONSTANT */

static char *
encname(struct mailbox *mp, const char *name, int same, const char *box)
{
   char *cachedir, *eaccount, *ename, *res;
   char const *emailbox;
   int resz;
   NYD_ENTER;

   ename = urlxenc(name);
   if (mp->mb_cache_directory && same && box == NULL) {
      res = salloc(resz = strlen(mp->mb_cache_directory) + strlen(ename) + 2);
      snprintf(res, resz, "%s%s%s", mp->mb_cache_directory,
         (*ename ? "/" : ""), ename);
   } else {
      if ((cachedir = ok_vlook(imap_cache)) == NULL ||
            (cachedir = file_expand(cachedir)) == NULL) {
         res = NULL;
         goto jleave;
      }
      eaccount = urlxenc(mp->mb_imap_account);
      if (box)
         emailbox = urlxenc(box);
      else if (asccasecmp(mp->mb_imap_mailbox, "INBOX"))
         emailbox = urlxenc(mp->mb_imap_mailbox);
      else
         emailbox = "INBOX";
      res = salloc(resz = strlen(cachedir) + strlen(eaccount) +
            strlen(emailbox) + strlen(ename) + 4);
      snprintf(res, resz, "%s/%s/%s%s%s", cachedir, eaccount, emailbox,
            (*ename ? "/" : ""), ename);
   }
jleave:
   NYD_LEAVE;
   return res;
}

static char *
encuid(struct mailbox *mp, unsigned long uid)
{
   char buf[30], *cp;
   NYD_ENTER;

   snprintf(buf, sizeof buf, "%lu", uid);
   cp = encname(mp, buf, 1, NULL);
   NYD_LEAVE;
   return cp;
}

FL enum okay
getcache1(struct mailbox *mp, struct message *m, enum needspec need,
   int setflags)
{
   FILE *fp;
   long n = 0, size = 0, xsize, xtime, xlines = -1, lines = 0;
   int lastc = EOF, i, xflag, inheader = 1;
   char b, iob[32768];
   off_t offset;
   void *zp;
   enum okay rv = STOP;
   NYD_ENTER;

   if (setflags == 0 && ((mp->mb_type != MB_IMAP && mp->mb_type != MB_CACHE) ||
         m->m_uid == 0))
      goto jleave;
   if ((fp = Fopen(encuid(mp, m->m_uid), "r")) == NULL)
      goto jleave;

   (void)fcntl_lock(fileno(fp), F_RDLCK);
   if (fscanf(fp, infofmt, &b, (unsigned long*)&xsize, &xflag,
         (unsigned long*)&xtime, &xlines) < 4)
      goto jfail;
   if (need != NEED_UNSPEC) {
      switch (b) {
      case 'H':
         if (need == NEED_HEADER)
            goto jsuccess;
         goto jfail;
      case 'B':
         if (need == NEED_HEADER || need == NEED_BODY)
            goto jsuccess;
         goto jfail;
      default:
         goto jfail;
      }
   }
jsuccess:
   if (b == 'N')
      goto jflags;
   if (fseek(fp, INITSKIP, SEEK_SET) < 0)
      goto jfail;
   zp = zalloc(fp);
   if (fseek(mp->mb_otf, 0L, SEEK_END) < 0) {
      zfree(zp);
      goto jfail;
   }
   offset = ftell(mp->mb_otf);
   while (inheader && (n = zread(zp, iob, sizeof iob)) > 0) {
      size += n;
      for (i = 0; i < n; i++) {
         if (iob[i] == '\n') {
            lines++;
            if (lastc == '\n')
               inheader = 0;
         }
         lastc = iob[i]&0377;
      }
      fwrite(iob, 1, n, mp->mb_otf);
   }
   if (n > 0 && need == NEED_BODY) {
      while ((n = zread(zp, iob, sizeof iob)) > 0) {
         size += n;
         for (i = 0; i < n; i++)
            if (iob[i] == '\n')
               lines++;
         fwrite(iob, 1, n, mp->mb_otf);
      }
   }
   fflush(mp->mb_otf);
   if (zfree(zp) < 0 || n < 0 || ferror(fp) || ferror(mp->mb_otf))
      goto jfail;

   m->m_size = size;
   m->m_lines = lines;
   m->m_block = mailx_blockof(offset);
   m->m_offset = mailx_offsetof(offset);
jflags:
   if (setflags) {
      m->m_xsize = xsize;
      m->m_time = xtime;
      if (setflags & 2) {
         m->m_flag = xflag | MNOFROM;
         if (b != 'B')
            m->m_flag |= MHIDDEN;
      }
   }
   if (xlines > 0 && m->m_xlines <= 0)
      m->m_xlines = xlines;
   switch (b) {
   case 'B':
      m->m_xsize = xsize;
      if (xflag == MREAD && xlines > 0)
         m->m_flag |= MFULLYCACHED;
      if (need == NEED_BODY) {
         m->m_have |= HAVE_HEADER | HAVE_BODY;
         if (m->m_lines > 0)
            m->m_xlines = m->m_lines;
         break;
      }
      /*FALLTHRU*/
   case 'H':
      m->m_have |= HAVE_HEADER;
      break;
   case 'N':
      break;
   }
   rv = OKAY;
jfail:
   Fclose(fp);
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
getcache(struct mailbox *mp, struct message *m, enum needspec need)
{
   enum okay rv;
   NYD_ENTER;

   rv = getcache1(mp, m, need, 0);
   NYD_LEAVE;
   return rv;
}

FL void
putcache(struct mailbox *mp, struct message *m)
{
   char iob[32768], *name, ob;
   FILE *ibuf, *obuf;
   int c, oflag;
   long n, cnt, oldoffset, osize, otime, olines = -1;
   void *zp;
   NYD_ENTER;

   if ((mp->mb_type != MB_IMAP && mp->mb_type != MB_CACHE) || m->m_uid == 0 ||
         m->m_time == 0 || (m->m_flag & (MTOUCH|MFULLYCACHED)) == MFULLYCACHED)
      goto jleave;
   if (m->m_have & HAVE_BODY)
      c = 'B';
   else if (m->m_have & HAVE_HEADER)
      c = 'H';
   else if (m->m_have == HAVE_NOTHING)
      c = 'N';
   else
      goto jleave;
   if ((oldoffset = ftell(mp->mb_itf)) < 0) /* XXX weird err hdling */
      oldoffset = 0;
   if ((obuf = Fopen(name = encuid(mp, m->m_uid), "r+")) == NULL) {
      if ((obuf = Fopen(name, "w")) == NULL)
         goto jleave;
      (void)fcntl_lock(fileno(obuf), F_WRLCK); /* XXX err hdl */
   } else {
      (void)fcntl_lock(fileno(obuf), F_WRLCK); /* XXX err hdl */
      if (fscanf(obuf, infofmt, &ob, (unsigned long*)&osize, &oflag,
            (unsigned long*)&otime, &olines) >= 4 && ob != '\0' &&
            (ob == 'B' || (ob == 'H' && c != 'B'))) {
         if (m->m_xlines <= 0 && olines > 0)
            m->m_xlines = olines;
         if ((c != 'N' && (size_t)osize != m->m_xsize) ||
               oflag != (int)USEBITS(m->m_flag) || otime != m->m_time ||
               (m->m_xlines > 0 && olines != m->m_xlines)) {
            fflush(obuf);
            rewind(obuf);
            fprintf(obuf, infofmt, ob, (unsigned long)m->m_xsize,
               USEBITS(m->m_flag), (unsigned long)m->m_time, m->m_xlines);
            putc('\n', obuf);
         }
         Fclose(obuf);
         goto jleave;
      }
      fflush(obuf);
      rewind(obuf);
      ftruncate(fileno(obuf), 0);
   }
   if ((ibuf = setinput(mp, m, NEED_UNSPEC)) == NULL) {
      Fclose(obuf);
      goto jleave;
   }
   if (c == 'N')
      goto jdone;
   fseek(obuf, INITSKIP, SEEK_SET);
   zp = zalloc(obuf);
   cnt = m->m_size;
   while (cnt > 0) {
      n = (cnt > (long)sizeof iob) ? (long)sizeof iob : cnt;
      cnt -= n;
      if ((size_t)n != fread(iob, 1, n, ibuf) ||
            n != (long)zwrite(zp, iob, n)) {
         unlink(name);
         zfree(zp);
         goto jout;
      }
   }
   if (zfree(zp) < 0) {
      unlink(name);
      goto jout;
   }
jdone:
   rewind(obuf);
   fprintf(obuf, infofmt, c, (unsigned long)m->m_xsize, USEBITS(m->m_flag),
         (unsigned long)m->m_time, m->m_xlines);
   putc('\n', obuf);
   if (ferror(obuf)) {
      unlink(name);
      goto jout;
   }
   if (c == 'B' && USEBITS(m->m_flag) == MREAD)
      m->m_flag |= MFULLYCACHED;
jout:
   if (Fclose(obuf) != 0) {
      m->m_flag &= ~MFULLYCACHED;
      unlink(name);
   }
   (void)fseek(mp->mb_itf, oldoffset, SEEK_SET);
jleave:
   NYD_LEAVE;
}

FL void
initcache(struct mailbox *mp)
{
   char *name, *uvname;
   FILE *uvfp;
   unsigned long uv;
   struct cw cw;
   NYD_ENTER;

   if (mp->mb_cache_directory != NULL)
      free(mp->mb_cache_directory);
   mp->mb_cache_directory = NULL;
   if ((name = encname(mp, "", 1, NULL)) == NULL)
      goto jleave;
   mp->mb_cache_directory = sstrdup(name);
   if ((uvname = encname(mp, "UIDVALIDITY", 1, NULL)) == NULL)
      goto jleave;
   if (cwget(&cw) == STOP)
      goto jleave;
   if ((uvfp = Fopen(uvname, "r+")) == NULL ||
         (fcntl_lock(fileno(uvfp), F_RDLCK), 0) ||
         fscanf(uvfp, "%lu", &uv) != 1 || uv != mp->mb_uidvalidity) {
      if ((uvfp = clean(mp, &cw)) == NULL)
         goto jout;
   } else {
      fflush(uvfp);
      rewind(uvfp);
   }
   fcntl_lock(fileno(uvfp), F_WRLCK);
   fprintf(uvfp, "%lu\n", mp->mb_uidvalidity);
   if (ferror(uvfp) || Fclose(uvfp) != 0) {
      unlink(uvname);
      mp->mb_uidvalidity = 0;
   }
jout:
   cwrelse(&cw);
jleave:
   NYD_LEAVE;
}

FL void
purgecache(struct mailbox *mp, struct message *m, long mc)
{
   char *name;
   struct cw cw;
   NYD_ENTER;

   if ((name = encname(mp, "", 1, NULL)) == NULL)
      goto jleave;
   if (cwget(&cw) == STOP)
      goto jleave;
   purge(mp, m, mc, &cw, name);
   cwrelse(&cw);
jleave:
   NYD_LEAVE;
}

static FILE *
clean(struct mailbox *mp, struct cw *cw)
{
   char *cachedir, *eaccount, *buf;
   char const *emailbox;
   int bufsz;
   DIR *dirp;
   struct dirent *dp;
   FILE *fp = NULL;
   NYD_ENTER;

   if ((cachedir = ok_vlook(imap_cache)) == NULL ||
         (cachedir = file_expand(cachedir)) == NULL)
      goto jleave;
   eaccount = urlxenc(mp->mb_imap_account);
   if (asccasecmp(mp->mb_imap_mailbox, "INBOX"))
      emailbox = urlxenc(mp->mb_imap_mailbox);
   else
      emailbox = "INBOX";
   buf = salloc(bufsz = strlen(cachedir) + strlen(eaccount) +
         strlen(emailbox) + 40);
   if (makedir(cachedir) != OKAY)
      goto jleave;
   snprintf(buf, bufsz, "%s/README", cachedir);
   if ((fp = Fopen(buf, "wx")) != NULL) {
      fputs(README1, fp);
      fputs(README2, fp);
      fputs(README3, fp);
      fputs(README4, fp);
      fputs(README5, fp);
      Fclose(fp);
   }
   fp = NULL;
   snprintf(buf, bufsz, "%s/%s", cachedir, eaccount);
   if (makedir(buf) != OKAY)
      goto jleave;
   snprintf(buf, bufsz, "%s/%s/%s", cachedir, eaccount, emailbox);
   if (makedir(buf) != OKAY)
      goto jleave;
   if (chdir(buf) < 0)
      goto jleave;
   if ((dirp = opendir(".")) == NULL)
      goto jout;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
         continue;
      unlink(dp->d_name);
   }
   closedir(dirp);
   fp = Fopen("UIDVALIDITY", "w");
jout:
   if (cwret(cw) == STOP) {
      fputs("Fatal: Cannot change back to current directory.\n", stderr);
      abort();
   }
jleave:
   NYD_LEAVE;
   return fp;
}

static unsigned long *
builds(long *contentelem)
{
   unsigned long n, *contents = NULL;
   long contentalloc = 0;
   char *x;
   DIR *dirp;
   struct dirent *dp;
   NYD_ENTER;

   *contentelem = 0;
   if ((dirp = opendir(".")) == NULL)
      goto jleave;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
         continue;
      n = strtoul(dp->d_name, &x, 10);
      if (*x != '\0')
         continue;
      if (*contentelem >= contentalloc - 1)
         contents = srealloc(contents,
               (contentalloc += 200) * sizeof *contents);
      contents[(*contentelem)++] = n;
   }
   closedir(dirp);
   if (*contentelem > 0) {
      contents[*contentelem] = 0;
      qsort(contents, *contentelem, sizeof *contents, longlt);
   }
jleave:
   NYD_LEAVE;
   return contents;
}

static void
purge(struct mailbox *mp, struct message *m, long mc, struct cw *cw,
   const char *name)
{
   unsigned long *contents;
   long i, j, contentelem;
   NYD_ENTER;
   UNUSED(mp);

   if (chdir(name) < 0)
      goto jleave;
   contents = builds(&contentelem);
   if (contents) {
      i = j = 0;
      while (j < contentelem) {
         if (i < mc && m[i].m_uid == contents[j]) {
            i++;
            j++;
         } else if (i < mc && m[i].m_uid < contents[j])
            i++;
         else
            remve(contents[j++]);
      }
   }
   if (cwret(cw) == STOP) {
      fputs("Fatal: Cannot change back to current directory.\n", stderr);
      abort();
   }
   free(contents);
jleave:
   NYD_LEAVE;
}

static int
longlt(const void *a, const void *b)
{
   union {long l; int i;} u;
   NYD_ENTER;

   u.l = *(long const*)a - *(long const*)b;
   u.i = (u.l < 0) ? -1 : ((u.l > 0) ? 1 : 0);
   NYD_LEAVE;
   return u.i;
}

static void
remve(unsigned long n)
{
   char buf[30];
   NYD_ENTER;

   snprintf(buf, sizeof buf, "%lu", n);
   unlink(buf);
   NYD_LEAVE;
}

FL void
delcache(struct mailbox *mp, struct message *m)
{
   char *fn;
   NYD_ENTER;

   fn = encuid(mp, m->m_uid);
   if (fn && unlink(fn) == 0)
      m->m_flag |= MUNLINKED;
   NYD_LEAVE;
}

FL enum okay
cache_setptr(int transparent)
{
   struct cw cw;
   int i, omsgCount = 0;
   char *name;
   unsigned long *contents;
   long contentelem;
   struct message *omessage = NULL;
   enum okay rv = STOP;
   NYD_ENTER;

   if (transparent) {
      omessage = message;
      omsgCount = msgCount;
   }
   free(mb.mb_cache_directory);
   mb.mb_cache_directory = NULL;
   if ((name = encname(&mb, "", 1, NULL)) == NULL)
      goto jleave;
   mb.mb_cache_directory = sstrdup(name);
   if (cwget(&cw) == STOP)
      goto jleave;
   if (chdir(name) < 0)
      goto jleave;
   contents = builds(&contentelem);
   msgCount = contentelem;
   message = scalloc(msgCount + 1, sizeof *message);
   if (cwret(&cw) == STOP) {
      fputs("Fatal: Cannot change back to current directory.\n", stderr);
      abort();
   }
   cwrelse(&cw);
   for (i = 0; i < msgCount; i++) {
      message[i].m_uid = contents[i];
      getcache1(&mb, &message[i], NEED_UNSPEC, 3);
   }
   mb.mb_type = MB_CACHE;
   mb.mb_perm = (options & OPT_R_FLAG) ? 0 : MB_DELE;
   if (transparent)
      transflags(omessage, omsgCount, 1);
   else
      setdot(message);
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
cache_list(struct mailbox *mp, const char *base, int strip, FILE *fp)
{
   char *name, *cachedir, *eaccount;
   DIR *dirp;
   struct dirent *dp;
   const char *cp, *bp, *sp;
   int namesz;
   enum okay rv = STOP;
   NYD_ENTER;

   if ((cachedir = ok_vlook(imap_cache)) == NULL ||
         (cachedir = file_expand(cachedir)) == NULL)
      goto jleave;
   eaccount = urlxenc(mp->mb_imap_account);
   name = salloc(namesz = strlen(cachedir) + strlen(eaccount) + 2);
   snprintf(name, namesz, "%s/%s", cachedir, eaccount);
   if ((dirp = opendir(name)) == NULL)
      goto jleave;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.')
         continue;
      cp = sp = urlxdec(dp->d_name);
      for (bp = base; *bp && *bp == *sp; bp++)
         sp++;
      if (*bp)
         continue;
      cp = strip ? sp : cp;
      fprintf(fp, "%s\n", *cp ? cp : "INBOX");
   }
   closedir(dirp);
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
cache_remove(const char *name)
{
   struct stat st;
   DIR *dirp;
   struct dirent *dp;
   char *path, *dir;
   int pathsize, pathend, n;
   enum okay rv = OKAY;
   NYD_ENTER;

   if ((dir = encname(&mb, "", 0, imap_fileof(name))) == NULL)
      goto jleave;
   pathend = strlen(dir);
   path = smalloc(pathsize = pathend + 30);
   memcpy(path, dir, pathend);
   path[pathend++] = '/';
   path[pathend] = '\0';
   if ((dirp = opendir(path)) == NULL) {
      free(path);
      goto jleave;
   }
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
         continue;
      n = strlen(dp->d_name) + 1;
      if (pathend + n > pathsize)
         path = srealloc(path, pathsize = pathend + n + 30);
      memcpy(path + pathend, dp->d_name, n);
      if (stat(path, &st) < 0 || (st.st_mode & S_IFMT) != S_IFREG)
         continue;
      if (unlink(path) < 0) {
         perror(path);
         closedir(dirp);
         free(path);
         rv = STOP;
         goto jleave;
      }
   }
   closedir(dirp);
   path[pathend] = '\0';
   rmdir(path);   /* no error on failure, might contain submailboxes */
   free(path);
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
cache_rename(const char *old, const char *new)
{
   char *olddir, *newdir;
   enum okay rv = OKAY;
   NYD_ENTER;

   if ((olddir = encname(&mb, "", 0, imap_fileof(old))) == NULL ||
         (newdir = encname(&mb, "",0, imap_fileof(new))) == NULL)
      goto jleave;
   if (rename(olddir, newdir) < 0) {
      perror(olddir);
      rv = STOP;
   }
jleave:
   NYD_LEAVE;
   return rv;
}

FL unsigned long
cached_uidvalidity(struct mailbox *mp)
{
   FILE *uvfp;
   char *uvname;
   unsigned long uv;
   NYD_ENTER;

   if ((uvname = encname(mp, "UIDVALIDITY", 1, NULL)) == NULL) {
      uv = 0;
      goto jleave;
   }
   if ((uvfp = Fopen(uvname, "r")) == NULL ||
         (fcntl_lock(fileno(uvfp), F_RDLCK), 0) ||
         fscanf(uvfp, "%lu", &uv) != 1)
      uv = 0;
   if (uvfp != NULL)
      Fclose(uvfp);
jleave:
   NYD_LEAVE;
   return uv;
}

static FILE *
cache_queue1(struct mailbox *mp, char const *mode, char **xname)
{
   char *name;
   FILE *fp = NULL;
   NYD_ENTER;

   if ((name = encname(mp, "QUEUE", 0, NULL)) == NULL)
      goto jleave;
   if ((fp = Fopen(name, mode)) != NULL)
      fcntl_lock(fileno(fp), F_WRLCK);
   if (xname)
      *xname = name;
jleave:
   NYD_LEAVE;
   return fp;
}

FL FILE *
cache_queue(struct mailbox *mp)
{
   FILE *fp;
   NYD_ENTER;

   fp = cache_queue1(mp, "a", NULL);
   if (fp == NULL)
      fputs("Cannot queue IMAP command. Retry when online.\n", stderr);
   NYD_LEAVE;
   return fp;
}

FL enum okay
cache_dequeue(struct mailbox *mp)
{
   int bufsz;
   char *cachedir, *eaccount, *buf, *oldbox;
   DIR *dirp;
   struct dirent *dp;
   enum okay rv = OKAY;
   NYD_ENTER;

   if ((cachedir = ok_vlook(imap_cache)) == NULL ||
         (cachedir = file_expand(cachedir)) == NULL)
      goto jleave;
   eaccount = urlxenc(mp->mb_imap_account);
   buf = salloc(bufsz = strlen(cachedir) + strlen(eaccount) + 2);
   snprintf(buf, bufsz, "%s/%s", cachedir, eaccount);
   if ((dirp = opendir(buf)) == NULL)
      goto jleave;
   oldbox = mp->mb_imap_mailbox;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.')
         continue;
      mp->mb_imap_mailbox = urlxdec(dp->d_name);
      dequeue1(mp);
   }
   closedir(dirp);
   mp->mb_imap_mailbox = oldbox;
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
dequeue1(struct mailbox *mp)
{
   FILE *fp = NULL, *uvfp = NULL;
   char *qname, *uvname;
   unsigned long uv;
   off_t is_size;
   int is_count;
   enum okay rv = OKAY;
   NYD_ENTER;

   fp = cache_queue1(mp, "r+", &qname);
   if (fp != NULL && fsize(fp) > 0) {
      if (imap_select(mp, &is_size, &is_count, mp->mb_imap_mailbox) != OKAY) {
         fprintf(stderr, "Cannot select \"%s\" for dequeuing.\n",
            mp->mb_imap_mailbox);
         goto jsave;
      }
      if ((uvname = encname(mp, "UIDVALIDITY", 0, NULL)) == NULL ||
            (uvfp = Fopen(uvname, "r")) == NULL ||
            (fcntl_lock(fileno(uvfp), F_RDLCK), 0) ||
            fscanf(uvfp, "%lu", &uv) != 1 || uv != mp->mb_uidvalidity) {
         fprintf(stderr, "Unique identifiers for \"%s\" are out of date. "
            "Cannot commit IMAP commands.\n", mp->mb_imap_mailbox);
jsave:
         fputs("Saving IMAP commands to dead.letter\n", stderr);
         savedeadletter(fp, 0);
         ftruncate(fileno(fp), 0);
         Fclose(fp);
         if (uvfp)
            Fclose(uvfp);
         rv = STOP;
         goto jleave;
      }
      Fclose(uvfp);
      printf("Committing IMAP commands for \"%s\"\n", mp->mb_imap_mailbox);
      imap_dequeue(mp, fp);
   }
   if (fp) {
      Fclose(fp);
      unlink(qname);
   }
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_IMAP */

/* vim:set fenc=utf-8:s-it-mode */
