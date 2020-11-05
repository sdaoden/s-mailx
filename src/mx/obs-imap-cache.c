/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ A cache for IMAP.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 * SPDX-License-Identifier: BSD-4-Clause
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
#undef su_FILE
#define su_FILE obs_imap_cache
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_IMAP
#include <dirent.h>

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/url.h"

/* TODO fake */
#include "su/code-in.h"

static char *           encname(struct mailbox *mp, const char *name, int same,
                           const char *box);
static char *           encuid(struct mailbox *mp, u64 uid);
static FILE *           clean(struct mailbox *mp, struct cw *cw);
static u64 *         builds(long *contentelem);
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
This is a cache directory maintained by " VAL_UAGENT "(1).\n\
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
decompress the rest, as e.g. \"dd if=FILE skip=1 bs=128 | zcat\" does.\n";
static const char README3[] = "\n\
Files named QUEUE contain data that will be sent do the IMAP server next\n\
time a connection is made in online mode.\n";
static const char README4[] = "\n\
You can safely delete any file or directory here, unless it contains a QUEUE\n\
file that is not empty; " VAL_UAGENT
   " will download the data again and will also\n\
write new cache entries if configured in this way. If you do not wish to use\n\
the cache anymore, delete the entire directory and unset the *imap-cache*\n\
variable in " VAL_UAGENT "(1).\n";

static char *
encname(struct mailbox *mp, const char *name, int same, const char *box)
{
   char *cachedir, *eaccount, *ename, *res;
   int resz;
   NYD2_IN;

   ename = mx_url_xenc(name, TRU1);
   if (mp->mb_cache_directory && same && box == NULL) {
      res = n_autorec_alloc(resz = su_cs_len(mp->mb_cache_directory) +
            su_cs_len(ename) + 2);
      snprintf(res, resz, "%s%s%s", mp->mb_cache_directory,
         (*ename ? "/" : ""), ename);
   } else {
      res = NULL;

      if((cachedir = ok_vlook(imap_cache)) == NIL ||
            (cachedir = fexpand(cachedir, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
               FEXP_NSHELL))) == NIL)
         goto jleave;
      eaccount = mx_url_xenc(mp->mb_imap_account, TRU1);

      if (box != NULL || su_cs_cmp_case(box = mp->mb_imap_mailbox, "INBOX")) {
         boole err;

         box = imap_path_encode(box, &err);
         if(err)
            goto jleave;
         box = mx_url_xenc(box, TRU1);
      } else
         box = "INBOX";

      res = n_autorec_alloc(resz = su_cs_len(cachedir) + su_cs_len(eaccount) +
            su_cs_len(box) + su_cs_len(ename) + 4);
      snprintf(res, resz, "%s/%s/%s%s%s", cachedir, eaccount, box,
            (*ename ? "/" : ""), ename);
   }
jleave:
   NYD2_OU;
   return res;
}

static char *
encuid(struct mailbox *mp, u64 uid)
{
   char buf[64], *cp;
   NYD2_IN;

   snprintf(buf, sizeof buf, "%" PRIu64, uid);
   cp = encname(mp, buf, 1, NULL);
   NYD2_OU;
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
   NYD2_IN;

   if (setflags == 0 && ((mp->mb_type != MB_IMAP && mp->mb_type != MB_CACHE) ||
         m->m_uid == 0))
      goto jleave;
   if((fp = mx_fs_open(encuid(mp, m->m_uid), "r")) == NIL)
      goto jleave;

   mx_file_lock(fileno(fp), mx_FILE_LOCK_TYPE_READ, 0,0, 0);
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
         m->m_content_info |= CI_HAVE_HEADER | CI_HAVE_BODY;
         if (m->m_lines > 0)
            m->m_xlines = m->m_lines;
         break;
      }
      /*FALLTHRU*/
   case 'H':
      m->m_content_info |= CI_HAVE_HEADER;
      break;
   case 'N':
      break;
   }
   rv = OKAY;
jfail:
   mx_fs_close(fp);
jleave:
   NYD2_OU;
   return rv;
}

FL enum okay
getcache(struct mailbox *mp, struct message *m, enum needspec need)
{
   enum okay rv;
   NYD_IN;

   rv = getcache1(mp, m, need, 0);
   NYD_OU;
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
   NYD_IN;

   if ((mp->mb_type != MB_IMAP && mp->mb_type != MB_CACHE) || m->m_uid == 0 ||
         m->m_time == 0 || (m->m_flag & (MTOUCH|MFULLYCACHED)) == MFULLYCACHED)
      goto jleave;
   if (m->m_content_info & CI_HAVE_BODY)
      c = 'B';
   else if (m->m_content_info & CI_HAVE_HEADER)
      c = 'H';
   else if (!(m->m_content_info & CI_HAVE_MASK))
      c = 'N';
   else
      goto jleave;
   if ((oldoffset = ftell(mp->mb_itf)) < 0) /* XXX weird err hdling */
      oldoffset = 0;
   if((obuf = mx_fs_open(name = encuid(mp, m->m_uid), "r+")) == NIL){
      if((obuf = mx_fs_open(name, "w")) == NIL)
         goto jleave;
      mx_file_lock(fileno(obuf), mx_FILE_LOCK_TYPE_WRITE, 0,0, 0); /* XXX err*/
   }else{
      mx_file_lock(fileno(obuf), mx_FILE_LOCK_TYPE_READ, 0,0, 0); /* XXX err */
      if (fscanf(obuf, infofmt, &ob, (unsigned long*)&osize, &oflag,
            (unsigned long*)&otime, &olines) >= 4 && ob != '\0' &&
            (ob == 'B' || (ob == 'H' && c != 'B'))) {
         if (m->m_xlines <= 0 && olines > 0)
            m->m_xlines = olines;
         if ((c != 'N' && (uz)osize != m->m_xsize) ||
               oflag != (int)USEBITS(m->m_flag) || otime != m->m_time ||
               (m->m_xlines > 0 && olines != m->m_xlines)) {
            fflush(obuf);
            rewind(obuf);
            fprintf(obuf, infofmt, ob, (unsigned long)m->m_xsize,
               S(int,USEBITS(m->m_flag)), (unsigned long)m->m_time,
               m->m_xlines);
            putc('\n', obuf);
         }
         mx_fs_close(obuf);
         goto jleave;
      }
      fflush(obuf);
      rewind(obuf);
      ftruncate(fileno(obuf), 0);
   }

   if((ibuf = setinput(mp, m, NEED_UNSPEC)) == NIL){
      mx_fs_close(obuf);
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
      if ((uz)n != fread(iob, 1, n, ibuf) ||
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
   fprintf(obuf, infofmt, c, (unsigned long)m->m_xsize,
      S(int,USEBITS(m->m_flag)), (unsigned long)m->m_time, m->m_xlines);
   putc('\n', obuf);
   if (ferror(obuf)) {
      unlink(name);
      goto jout;
   }
   if (c == 'B' && USEBITS(m->m_flag) == MREAD)
      m->m_flag |= MFULLYCACHED;

jout:
   if(!mx_fs_close(obuf)){
      m->m_flag &= ~MFULLYCACHED;
      unlink(name);
   }
   (void)fseek(mp->mb_itf, oldoffset, SEEK_SET);
jleave:
   NYD_OU;
}

FL void
initcache(struct mailbox *mp)
{
   char *name, *uvname;
   FILE *uvfp;
   u64 uv;
   struct cw cw;
   NYD_IN;

   if (mp->mb_cache_directory != NULL)
      n_free(mp->mb_cache_directory);
   mp->mb_cache_directory = NULL;
   if ((name = encname(mp, "", 1, NULL)) == NULL)
      goto jleave;
   mp->mb_cache_directory = su_cs_dup(name, 0);
   if ((uvname = encname(mp, "UIDVALIDITY", 1, NULL)) == NULL)
      goto jleave;
   if (cwget(&cw) == STOP)
      goto jleave;

   if((uvfp = mx_fs_open(uvname, "r+")) == NIL ||
         (mx_file_lock(fileno(uvfp), mx_FILE_LOCK_TYPE_READ, 0,0, 0), 0) ||
         fscanf(uvfp, "%" PRIu64 , &uv) != 1 || uv != mp->mb_uidvalidity) {
      if ((uvfp = clean(mp, &cw)) == NULL)
         goto jout;
   } else {
      fflush(uvfp);
      rewind(uvfp);
   }

   mx_file_lock(fileno(uvfp), mx_FILE_LOCK_TYPE_WRITE, 0,0, 0);
   fprintf(uvfp, "%" PRIu64 "\n", mp->mb_uidvalidity);

   /* C99 */{
      int x;

      x = ferror(uvfp);

      if(!mx_fs_close(uvfp) || x){
         unlink(uvname);
         mp->mb_uidvalidity = 0;
      }
   }

jout:
   cwrelse(&cw);
jleave:
   NYD_OU;
}

FL void
purgecache(struct mailbox *mp, struct message *m, long mc)
{
   char *name;
   struct cw cw;
   NYD_IN;

   if ((name = encname(mp, "", 1, NULL)) == NULL)
      goto jleave;
   if (cwget(&cw) == STOP)
      goto jleave;
   purge(mp, m, mc, &cw, name);
   cwrelse(&cw);
jleave:
   NYD_OU;
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
   NYD_IN;

   if((cachedir = ok_vlook(imap_cache)) == NIL ||
         (cachedir = fexpand(cachedir, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL))) == NIL)
      goto jleave;
   eaccount = mx_url_xenc(mp->mb_imap_account, TRU1);
   if (su_cs_cmp_case(emailbox = mp->mb_imap_mailbox, "INBOX")) {
      boole err;

      emailbox = imap_path_encode(emailbox, &err);
      if(err)
         goto jleave;
      emailbox = mx_url_xenc(emailbox, TRU1);
   }
   buf = n_autorec_alloc(bufsz = su_cs_len(cachedir) + su_cs_len(eaccount) +
         su_cs_len(emailbox) + 40);
   if (!n_path_mkdir(cachedir))
      goto jleave;
   snprintf(buf, bufsz, "%s/README", cachedir);
   if((fp = mx_fs_open(buf, "wx")) != NIL){
      fputs(README1, fp);
      fputs(README2, fp);
      fputs(README3, fp);
      fputs(README4, fp);
      mx_fs_close(fp);
   }
   fp = NULL;
   snprintf(buf, bufsz, "%s/%s/%s", cachedir, eaccount, emailbox);
   if (!n_path_mkdir(buf))
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
   fp = mx_fs_open("UIDVALIDITY", "w");
jout:
   if (cwret(cw) == STOP) {
      n_err(_("Fatal: Cannot change back to current directory.\n"));
      abort();
   }
jleave:
   NYD_OU;
   return fp;
}

static u64 *
builds(long *contentelem)
{
   u64 n, *contents = NULL;
   long contentalloc = 0;
   char const *x;
   DIR *dirp;
   struct dirent *dp;
   NYD_IN;

   *contentelem = 0;
   if ((dirp = opendir(".")) == NULL)
      goto jleave;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
         continue;

      su_idec_u64_cp(&n, dp->d_name, 10, &x);/* TODO errors? */
      if (*x != '\0')
         continue;
      if (*contentelem >= contentalloc - 1)
         contents = n_realloc(contents,
               (contentalloc += 200) * sizeof *contents);
      contents[(*contentelem)++] = n;
   }
   closedir(dirp);
   if (*contentelem > 0) {
      contents[*contentelem] = 0;
      qsort(contents, *contentelem, sizeof *contents, longlt);
   }
jleave:
   NYD_OU;
   return contents;
}

static void
purge(struct mailbox *mp, struct message *m, long mc, struct cw *cw,
   const char *name)
{
   u64 *contents;
   long i, j, contentelem;
   NYD_IN;
   UNUSED(mp);

   if (chdir(name) < 0)
      goto jleave;
   contents = builds(&contentelem);
   if (contents != NULL) {
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
      n_free(contents);
   }
   if (cwret(cw) == STOP) {
      n_err(_("Fatal: Cannot change back to current directory.\n"));
      abort();
   }
jleave:
   NYD_OU;
}

static int
longlt(const void *a, const void *b)
{
   union {long l; int i;} u;
   NYD_IN;

   u.l = *(long const*)a - *(long const*)b;
   u.i = (u.l < 0) ? -1 : ((u.l > 0) ? 1 : 0);
   NYD_OU;
   return u.i;
}

static void
remve(unsigned long n)
{
   char buf[30];
   NYD_IN;

   snprintf(buf, sizeof buf, "%lu", n);
   unlink(buf);
   NYD_OU;
}

FL void
delcache(struct mailbox *mp, struct message *m)
{
   char *fn;
   NYD_IN;

   fn = encuid(mp, m->m_uid);
   if (fn && unlink(fn) == 0)
      m->m_flag |= MUNLINKED;
   NYD_OU;
}

FL enum okay
cache_setptr(enum fedit_mode fm, int transparent)
{
   struct cw cw;
   int i, omsgCount = 0;
   char *name;
   u64 *contents;
   long contentelem;
   struct message *omessage;
   enum okay rv = STOP;
   NYD_IN;

   omessage = message;
   omsgCount = msgCount;

   if (mb.mb_cache_directory != NULL) {
      n_free(mb.mb_cache_directory);
      mb.mb_cache_directory = NULL;
   }
   if ((name = encname(&mb, "", 1, NULL)) == NULL)
      goto jleave;
   mb.mb_cache_directory = su_cs_dup(name, 0);
   if (cwget(&cw) == STOP)
      goto jleave;
   if (chdir(name) < 0)
      goto jleave;
   contents = builds(&contentelem);
   msgCount = contentelem;
   message = n_calloc(msgCount + 1, sizeof *message);
   if (cwret(&cw) == STOP) {
      n_err(_("Fatal: Cannot change back to current directory.\n"));
      abort();
   }
   cwrelse(&cw);

   srelax_hold();
   for (i = 0; i < msgCount; i++) {
      message[i].m_uid = contents[i];
      getcache1(&mb, &message[i], NEED_UNSPEC, 3);
      srelax();
   }
   srelax_rele();

   if (contents != NULL)
      n_free(contents);
   mb.mb_type = MB_CACHE;
   mb.mb_perm = ((n_poption & n_PO_R_FLAG) || (fm & FEDIT_RDONLY)
         ) ? 0 : MB_DELE;
   if(omessage != NULL){
      if(transparent)
         /* This frees the message */
         transflags(omessage, omsgCount, 1);
      else
         n_free(omessage);
   }
   setdot(message, FAL0);
   rv = OKAY;
jleave:
   NYD_OU;
   return rv;
}

FL enum okay
cache_list(struct mailbox *mp, const char *base, int strip, FILE *fp)
{
   char *name, *cachedir, *eaccount;
   DIR *dirp;
   struct dirent *dp;
   const char *cp, *bp, *cp2;
   int namesz;
   enum okay rv = STOP;
   NYD_IN;

   if((cachedir = ok_vlook(imap_cache)) == NIL ||
         (cachedir = fexpand(cachedir, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL))) == NIL)
      goto jleave;
   eaccount = mx_url_xenc(mp->mb_imap_account, TRU1);
   name = n_autorec_alloc(namesz = su_cs_len(cachedir) +
         su_cs_len(eaccount) + 2);
   snprintf(name, namesz, "%s/%s", cachedir, eaccount);
   if ((dirp = opendir(name)) == NULL)
      goto jleave;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.')
         continue;
      cp = cp2 = imap_path_decode(mx_url_xdec(dp->d_name), NULL);
      for (bp = base; *bp && *bp == *cp2; bp++)
         cp2++;
      if (*bp)
         continue;
      cp = strip ? cp2 : cp;
      fprintf(fp, "%s\n", *cp ? cp : "INBOX");
   }
   closedir(dirp);
   rv = OKAY;
jleave:
   NYD_OU;
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
   NYD_IN;

   if ((dir = encname(&mb, "", 0, imap_fileof(name))) == NULL)
      goto jleave;
   pathend = su_cs_len(dir);
   path = n_alloc(pathsize = pathend + 30);
   su_mem_copy(path, dir, pathend);
   path[pathend++] = '/';
   path[pathend] = '\0';
   if ((dirp = opendir(path)) == NULL) {
      n_free(path);
      goto jleave;
   }
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
         continue;
      n = su_cs_len(dp->d_name) + 1;
      if (pathend + n > pathsize)
         path = n_realloc(path, pathsize = pathend + n + 30);
      su_mem_copy(path + pathend, dp->d_name, n);
      if (stat(path, &st) < 0 || (st.st_mode & S_IFMT) != S_IFREG)
         continue;
      if (unlink(path) < 0) {
         n_perr(path, 0);
         closedir(dirp);
         n_free(path);
         rv = STOP;
         goto jleave;
      }
   }
   closedir(dirp);
   path[pathend] = '\0';
   rmdir(path);   /* no error on failure, might contain submailboxes */
   n_free(path);
jleave:
   NYD_OU;
   return rv;
}

FL enum okay
cache_rename(const char *old, const char *new)
{
   char *olddir, *newdir;
   enum okay rv = OKAY;
   NYD_IN;

   if ((olddir = encname(&mb, "", 0, imap_fileof(old))) == NULL ||
         (newdir = encname(&mb, "",0, imap_fileof(new))) == NULL)
      goto jleave;
   if (rename(olddir, newdir) < 0) {
      n_perr(olddir, 0);
      rv = STOP;
   }
jleave:
   NYD_OU;
   return rv;
}

FL u64
cached_uidvalidity(struct mailbox *mp)
{
   FILE *uvfp;
   char *uvname;
   u64 uv;
   NYD_IN;

   if ((uvname = encname(mp, "UIDVALIDITY", 1, NULL)) == NULL) {
      uv = 0;
      goto jleave;
   }
   if((uvfp = mx_fs_open(uvname, "r")) == NIL ||
         (mx_file_lock(fileno(uvfp), mx_FILE_LOCK_TYPE_READ, 0,0, 0), 0) ||
         fscanf(uvfp, "%" PRIu64, &uv) != 1)
      uv = 0;
   if(uvfp != NIL)
      mx_fs_close(uvfp);
jleave:
   NYD_OU;
   return uv;
}

static FILE *
cache_queue1(struct mailbox *mp, char const *mode, char **xname)
{
   char *name;
   FILE *fp = NULL;
   NYD_IN;

   if ((name = encname(mp, "QUEUE", 0, NULL)) == NULL)
      goto jleave;
   if((fp = mx_fs_open(name, mode)) != NIL)
      mx_file_lock(fileno(fp), mx_FILE_LOCK_TYPE_WRITE, 0,0, 0);
   if (xname)
      *xname = name;
jleave:
   NYD_OU;
   return fp;
}

FL FILE *
cache_queue(struct mailbox *mp)
{
   FILE *fp;
   NYD_IN;

   fp = cache_queue1(mp, "a", NULL);
   if (fp == NULL)
      n_err(_("Cannot queue IMAP command. Retry when online.\n"));
   NYD_OU;
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
   NYD_IN;

   if((cachedir = ok_vlook(imap_cache)) == NIL ||
         (cachedir = fexpand(cachedir, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL))) == NIL)
      goto jleave;
   eaccount = mx_url_xenc(mp->mb_imap_account, TRU1);
   buf = n_autorec_alloc(bufsz = su_cs_len(cachedir) +
         su_cs_len(eaccount) + 2);
   snprintf(buf, bufsz, "%s/%s", cachedir, eaccount);
   if ((dirp = opendir(buf)) == NULL)
      goto jleave;
   oldbox = mp->mb_imap_mailbox;
   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.')
         continue;
      /* FIXME MUST BLOCK SIGNALS IN ORDER TO ENSURE PROPER RESTORE!
       * (but wuuuuh, what a shit!) */
      mp->mb_imap_mailbox = su_cs_dup(
            imap_path_decode(mx_url_xdec(dp->d_name), NULL), 0);
      dequeue1(mp);
      {  char *x = mp->mb_imap_mailbox;
         mp->mb_imap_mailbox = oldbox;
         n_free(x);
      }
   }
   closedir(dirp);
jleave:
   NYD_OU;
   return rv;
}

static enum okay
dequeue1(struct mailbox *mp)
{
   FILE *fp = NULL, *uvfp = NULL;
   char *qname, *uvname;
   u64 uv;
   off_t is_size;
   int is_count;
   enum okay rv = OKAY;
   NYD_IN;

   fp = cache_queue1(mp, "r+", &qname);
   if (fp != NULL && fsize(fp) > 0) {
      if (imap_select(mp, &is_size, &is_count, mp->mb_imap_mailbox, FEDIT_NONE)
            != OKAY) {
         n_err(_("Cannot select \"%s\" for dequeuing.\n"),
            mp->mb_imap_mailbox);
         goto jsave;
      }
      if ((uvname = encname(mp, "UIDVALIDITY", 0, NULL)) == NULL ||
            (uvfp = mx_fs_open(uvname, "r")) == NIL ||
            (mx_file_lock(fileno(uvfp), mx_FILE_LOCK_TYPE_READ, 0,0, 0), 0) ||
            fscanf(uvfp, "%" PRIu64, &uv) != 1 || uv != mp->mb_uidvalidity) {
         n_err(_("Unique identifiers for \"%s\" are out of date. "
            "Cannot commit IMAP commands.\n"), mp->mb_imap_mailbox);
jsave:
         n_err(_("Saving IMAP commands to *DEAD*\n"));
         savedeadletter(fp, 0);
         ftruncate(fileno(fp), 0);
         mx_fs_close(fp);
         if(uvfp != NIL)
            mx_fs_close(uvfp);
         rv = STOP;
         goto jleave;
      }
      mx_fs_close(uvfp);
      printf("Committing IMAP commands for \"%s\"\n", mp->mb_imap_mailbox);
      imap_dequeue(mp, fp);
   }

   if(fp != NIL){
      mx_fs_close(fp);
      unlink(qname);
   }
jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_IMAP */
/* s-it-mode */
