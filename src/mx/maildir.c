/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Maildir folder support. FIXME rewrite - why do we chdir(2)??
 *@ FIXME Simply truncating paths isn't really it.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE maildir
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_MAILDIR

#include <dirent.h>

#include <su/cs.h>
#include <su/icodec.h>
#include <su/prime.h>

/* a_maildir_tbl should be a hash-indexed array of trees! */
static struct message **a_maildir_tbl, **a_maildir_tbl_top;
static ui32_t a_maildir_tbl_prime, a_maildir_tbl_maxdist;
static sigjmp_buf    _maildir_jmp;

static void             __maildircatch(int s);
static void             __maildircatch_hold(int s);

/* Do some cleanup in the tmp/ subdir */
static void             _cleantmp(void);

static int              _maildir_setfile1(char const *name, enum fedit_mode fm,
                           int omsgCount);

static int a_maildir_cmp(void const *a, void const *b);

static int              _maildir_subdir(char const *name, char const *sub,
                           enum fedit_mode fm);

static void             _maildir_append(char const *name, char const *sub,
                           char const *fn);

static void             readin(char const *name, struct message *m);

static void             maildir_update(void);

static void             _maildir_move(struct n_timespec const *tsp,
                           struct message *m);

static char *           mkname(struct n_timespec const *tsp, enum mflag f,
                           char const *pref);

static enum okay        maildir_append1(struct n_timespec const *tsp,
                           char const *name, FILE *fp, off_t off1,
                           long size, enum mflag flag);

static enum okay        trycreate(char const *name);

static enum okay        mkmaildir(char const *name);

static struct message * mdlook(char const *name, struct message *data);

static void             mktable(void);

static enum okay        subdir_remove(char const *name, char const *sub);

static void
__maildircatch(int s)
{
   n_NYD_X; /* Signal handler */
   siglongjmp(_maildir_jmp, s);
}

static void
__maildircatch_hold(int s)
{
   n_NYD_X; /* Signal handler */
   n_UNUSED(s);
   /* TODO no STDIO in signal handler, no _() tr's -- pre-translate interrupt
    * TODO globally; */
   n_err_sighdl(_("\nImportant operation in progress: "
      "interrupt again to forcefully abort\n"));
   safe_signal(SIGINT, &__maildircatch);
}

static void
_cleantmp(void)
{
   struct stat st;
   struct n_string s, *sp;
   si64_t now;
   DIR *dirp;
   struct dirent *dp;
   n_NYD_IN;

   if ((dirp = opendir("tmp")) == NULL)
      goto jleave;

   now = n_time_now(FAL0)->ts_sec;
   sp = n_string_creat_auto(&s);

   while ((dp = readdir(dirp)) != NULL) {
      if (dp->d_name[0] == '.')
         continue;

      sp = n_string_trunc(sp, 0);
      sp = n_string_push_buf(sp, "tmp/", sizeof("tmp/") -1);
      sp = n_string_push_cp(sp, dp->d_name);
      if (stat(n_string_cp(sp), &st) == -1)
         continue;
      if (st.st_atime + 36*3600 < now)
         unlink(sp->s_dat);
   }
   closedir(dirp);
jleave:
   n_NYD_OU;
}

static int
_maildir_setfile1(char const *name, enum fedit_mode fm, int omsgCount)
{
   int i;
   n_NYD_IN;

   if (!(fm & FEDIT_NEWMAIL))
      _cleantmp();

   mb.mb_perm = ((n_poption & n_PO_R_FLAG) || (fm & FEDIT_RDONLY))
         ? 0 : MB_DELE;
   if ((i = _maildir_subdir(name, "cur", fm)) != 0)
      goto jleave;
   if ((i = _maildir_subdir(name, "new", fm)) != 0)
      goto jleave;
   _maildir_append(name, NULL, NULL);

   n_autorec_relax_create();
   for (i = ((fm & FEDIT_NEWMAIL) ? omsgCount : 0); i < msgCount; ++i) {
      readin(name, message + i);
      n_autorec_relax_unroll();
   }
   n_autorec_relax_gut();

   if (fm & FEDIT_NEWMAIL) {
      if (msgCount > omsgCount)
         qsort(&message[omsgCount], msgCount - omsgCount, sizeof *message,
            &a_maildir_cmp);
   } else if (msgCount)
      qsort(message, msgCount, sizeof *message, &a_maildir_cmp);
   i = msgCount;
jleave:
   n_NYD_OU;
   return i;
}

static int
a_maildir_cmp(void const *xa, void const *xb){
   char const *cpa, *cpa_pid, *cpb, *cpb_pid;
   union {struct message const *mp; char const *cp;} a, b;
   si64_t at, bt;
   int rv;
   n_NYD2_IN;

   a.mp = xa;
   b.mp = xb;

   /* We could have parsed the time somewhen in the past, do a quick shot */
   at = (si64_t)a.mp->m_time;
   bt = (si64_t)b.mp->m_time;
   if(at != 0 && bt != 0 && (at -= bt) != 0)
      goto jret;

   /* Otherwise we need to parse the name */
   a.cp = &a.mp->m_maildir_file[4];
   b.cp = &b.mp->m_maildir_file[4];

   /* Interpret time stored in name, and use it for comparison */
   if(((su_idec_s64_cp(&at, a.cp, 10, &cpa)
            ) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE || *cpa != '.' ||
         a.cp == cpa)
      goto jm1; /* Fishy */
   if(((su_idec_s64_cp(&bt, b.cp, 10, &cpb)
            ) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE || *cpb != '.' ||
         b.cp == cpb)
      goto j1; /* Fishy */

   if((at -= bt) != 0)
      goto jret;

   /* If the seconds part does not work, go deeper.
    * We use de-facto standard "maildir — E-mail directory" from the Courier
    * mail server, also used by, e.g., Dovecot: sec.MusecPpid.hostname:2,flags.
    * However, a different name convention exists which uses
    * sec.pid_counter.hostname:2,flags.
    * First go for usec/counter, then pid */

   /* A: exact "standard"? */
   cpa_pid = NULL;
   a.cp = ++cpa;
   if((rv = *a.cp) == 'M')
      ;
   /* Known compat? */
   else if(su_cs_is_digit(rv)){
      cpa_pid = a.cp++;
      while((rv = *a.cp) != '\0' && rv != '_')
         ++a.cp;
      if(rv == '\0')
         goto jm1; /* Fishy */
   }
   /* This is compatible to what dovecot does, it surely does not do so
    * for nothing, but i have no idea, but am too stupid to ask */
   else for(;; rv = *++a.cp){
      if(rv == 'M')
         break;
      if(rv == '\0' || rv == '.' || rv == n_MAILDIR_SEPARATOR)
         goto jm1; /* Fishy */
   }
   ++a.cp;
   if(((su_idec_s64_cp(&at, a.cp, 10, &cpa)
            ) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
      goto jm1; /* Fishy */

   /* B: as above */
   cpb_pid = NULL;
   b.cp = ++cpb;
   if((rv = *b.cp) == 'M')
      ;
   else if(su_cs_is_digit(rv)){
      cpb_pid = b.cp++;
      while((rv = *b.cp) != '\0' && rv != '_')
         ++b.cp;
      if(rv == '\0')
         goto j1;
   }else for(;; rv = *++b.cp){
      if(rv == 'M')
         break;
      if(rv == '\0' || rv == '.' || rv == n_MAILDIR_SEPARATOR)
         goto jm1;
   }
   ++b.cp;
   if(((su_idec_s64_cp(&bt, b.cp, 10, &cpb)
            ) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
      goto j1;

   if((at -= bt) != 0)
      goto jret;

   /* So this gets hairy: sort by PID, then hostname */
   if(cpa_pid != NULL){
      a.cp = cpa_pid;
      xa = cpa;
   }else{
      a.cp = cpa;
      if(*a.cp++ != 'P')
         goto jm1; /* Fishy */
   }
   if(((su_idec_s64_cp(&at, a.cp, 10, &cpa)
            ) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
      goto jm1; /* Fishy */

   if(cpb_pid != NULL){
      b.cp = cpb_pid;
      xb = cpb;
   }else{
      b.cp = cpb;
      if(*b.cp++ != 'P')
         goto j1; /* Fishy */
   }
   if(((su_idec_s64_cp(&bt, b.cp, 10, &cpb)
            ) & su_IDEC_STATE_EMASK) != su_IDEC_STATE_EBASE)
      goto jm1; /* Fishy */

   if((at -= bt) != 0)
      goto jret;

   /* Hostname */
   a.cp = (cpa_pid != NULL) ? xa : cpa;
   b.cp = (cpb_pid != NULL) ? xb : cpb;
   for(;; ++a.cp, ++b.cp){
      char ac, bc;

      ac = *a.cp;
      at = (ac != '\0' && ac != n_MAILDIR_SEPARATOR);
      bc = *b.cp;
      bt = (bc != '\0' && bc != n_MAILDIR_SEPARATOR);
      if((at -= bt) != 0)
         break;
      at = ac;
      if((at -= bc) != 0)
         break;
      if(ac == '\0')
         break;
   }

jret:
   rv = (at == 0 ? 0 : (at < 0 ? -1 : 1));
jleave:
   n_NYD2_OU;
   return rv;
jm1:
   rv = -1;
   goto jleave;
j1:
   rv = 1;
   goto jleave;
}

static int
_maildir_subdir(char const *name, char const *sub, enum fedit_mode fm)
{
   DIR *dirp;
   struct dirent *dp;
   int rv;
   n_NYD_IN;

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
   n_NYD_OU;
   return rv;
}

static void
_maildir_append(char const *name, char const *sub, char const *fn)
{
   struct message *m;
   time_t t = 0;
   enum mflag f = MUSED | MNOFROM | MNEWEST;
   char const *cp, *xp;
   n_NYD_IN;
   n_UNUSED(name);

   if (fn != NULL && sub != NULL) {
      if (!su_cs_cmp(sub, "new"))
         f |= MNEW;

      /* C99 */{
         si64_t tib;

         (void)/*TODO*/su_idec_s64_cp(&tib, fn, 10, &xp);
         t = (time_t)tib;
      }

      if ((cp = su_cs_rfind_c(xp, ',')) != NULL && PTRCMP(cp, >, xp + 2) &&
            cp[-1] == '2' && cp[-2] == n_MAILDIR_SEPARATOR) {
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

   m = &message[msgCount++];
   /* C99 */{
      char *tmp;
      size_t sz, i;

      i = su_cs_len(fn) +1;
      sz = su_cs_len(sub);
      m->m_maildir_file = tmp = n_alloc(sz + 1 + i);
      su_mem_copy(tmp, sub, sz);
      tmp[sz++] = '/';
      su_mem_copy(&tmp[sz], fn, i);
   }
   m->m_time = t;
   m->m_flag = f;
   m->m_maildir_hash = su_cs_hash(fn);
jleave:
   n_NYD_OU;
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
   n_NYD_IN;

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
   buf = n_alloc(bufsize = LINESIZE);
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
   n_free(buf);
   if (!emptyline) {
      /* TODO we need \n\n for mbox format.
       * TODO That is to say we do it wrong here in order to get it right
       * TODO when send.c stuff or with MBOX handling, even though THIS
       * TODO line is solely a property of the MBOX database format! */
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
   n_NYD_OU;
}

static void
maildir_update(void)
{
   struct message *m;
   struct n_timespec const *tsp;
   int dodel, c, gotcha = 0, held = 0, modflags = 0;
   n_NYD_IN;

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

   tsp = n_time_now(TRU1); /* TODO FAL0, eventloop update! */

   n_autorec_relax_create();
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
            _maildir_move(tsp, m);
            n_autorec_relax_unroll();
            ++modflags;
         }
         ++held;
      }
   }
   n_autorec_relax_gut();

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
      n_free(n_UNCONST(m->m_maildir_file));
   n_NYD_OU;
}

static void
_maildir_move(struct n_timespec const *tsp, struct message *m)
{
   char *fn, *newfn;
   n_NYD_IN;

   fn = mkname(tsp, m->m_flag, m->m_maildir_file + 4);
   newfn = savecat("cur/", fn);
   if (!su_cs_cmp(m->m_maildir_file, newfn))
      goto jleave;
   if (link(m->m_maildir_file, newfn) == -1) {
      n_err(_("Cannot link %s to %s: message %lu not touched\n"),
         n_shexp_quote_cp(savecatsep(mailname, '/', m->m_maildir_file), FAL0),
         n_shexp_quote_cp(savecatsep(mailname, '/', newfn), FAL0),
         (ul_i)PTR2SIZE(m - message + 1));
      goto jleave;
   }
   if (unlink(m->m_maildir_file) == -1)
      n_err(_("Cannot unlink %s\n"),
         n_shexp_quote_cp(savecatsep(mailname, '/', m->m_maildir_file), FAL0));
jleave:
   n_NYD_OU;
}

static char *
mkname(struct n_timespec const *tsp, enum mflag f, char const *pref)
{
   static char *node;
   static struct n_timespec ts;

   char *cp;
   int size, n, i;
   n_NYD_IN;

   if (pref == NULL) {
      si64_t s;

      if(n_pid == 0)
         n_pid = getpid();

      if (node == NULL) {
         cp = n_nodename(FAL0);
         n = size = 0;
         do {
            if (UICMP(32, n, <, size + 8))
               node = n_realloc(node, size += 20);
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

      /* Problem: Courier spec uses microseconds, not nanoseconds */
      if((s = tsp->ts_sec) > ts.ts_sec){
         ts.ts_sec = s;
         ts.ts_nsec = tsp->ts_nsec / (n_DATE_NANOSSEC / n_DATE_MICROSSEC);
      }else{
         s = tsp->ts_nsec / (n_DATE_NANOSSEC / n_DATE_MICROSSEC);
         if(s <= ts.ts_nsec)
            s = ts.ts_nsec + 1;
         if(s < n_DATE_MICROSSEC)
            ts.ts_nsec = s;
         else{
            ++ts.ts_sec;
            ts.ts_nsec = 0;
         }
      }

      /* Create a name according to Courier spec */
      size = 60 + su_cs_len(node);
      cp = n_autorec_alloc(size);
      n = snprintf(cp, size, "%" PRId64 ".M%" PRIdZ "P%ld.%s:2,",
            ts.ts_sec, ts.ts_nsec, (long)n_pid, node);
   } else {
      size = (n = su_cs_len(pref)) + 13;
      cp = n_autorec_alloc(size);
      su_mem_copy(cp, pref, n +1);
      for (i = n; i > 3; --i)
         if (cp[i - 1] == ',' && cp[i - 2] == '2' &&
               cp[i - 3] == n_MAILDIR_SEPARATOR) {
            n = i;
            break;
         }
      if (i <= 3) {
         su_mem_copy(cp + n, ":2,", 3 +1);
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
   n_NYD_OU;
   return cp;
}

static enum okay
maildir_append1(struct n_timespec const *tsp, char const *name, FILE *fp,
   off_t off1, long size, enum mflag flag)
{
   char buf[4096], *fn, *tfn, *nfn;
   struct stat st;
   FILE *op;
   size_t nlen, flen, n;
   enum okay rv = STOP;
   n_NYD_IN;

   nlen = su_cs_len(name);

   /* Create a unique temporary file */
   for (nfn = (char*)0xA /* XXX no magic */;; n_msleep(500, FAL0)) {
      flen = su_cs_len(fn = mkname(tsp, flag, NULL));
      tfn = n_autorec_alloc(n = nlen + flen + 6);
      snprintf(tfn, n, "%s/tmp/%s", name, fn);

      /* Use "wx" for O_EXCL XXX stat(2) rather redundant; coverity:TOCTOU */
      if ((!stat(tfn, &st) || su_err_no() == su_ERR_NOENT) &&
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

   nfn = n_autorec_alloc(n = nlen + flen + 6);
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
   n_NYD_OU;
   return rv;
}

static enum okay
trycreate(char const *name)
{
   struct stat st;
   enum okay rv = STOP;
   n_NYD_IN;

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
   n_NYD_OU;
   return rv;
}

static enum okay
mkmaildir(char const *name) /* TODO proper cleanup on error; use path[] loop */
{
   char *np;
   size_t sz;
   enum okay rv = STOP;
   n_NYD_IN;

   if (trycreate(name) == OKAY) {
      np = n_lofi_alloc((sz = su_cs_len(name)) + 4 +1);
      su_mem_copy(np, name, sz);
      su_mem_copy(np + sz, "/tmp", 4 +1);
      if (trycreate(np) == OKAY) {
         su_mem_copy(np + sz, "/new", 4 +1);
         if (trycreate(np) == OKAY) {
            su_mem_copy(np + sz, "/cur", 4 +1);
            rv = trycreate(np);
         }
      }
      n_lofi_free(np);
   }
   n_NYD_OU;
   return rv;
}

static struct message *
mdlook(char const *name, struct message *data)
{
   struct message **mpp, *mp;
   ui32_t h, i;
   n_NYD_IN;

   if(data != NULL)
      i = data->m_maildir_hash;
   else
      i = su_cs_hash(name);
   h = i;
   mpp = &a_maildir_tbl[i %= a_maildir_tbl_prime];

   for(i = 0;;){
      if((mp = *mpp) == NULL){
         if(n_UNLIKELY(data != NULL)){
            *mpp = mp = data;
            if(i > a_maildir_tbl_maxdist)
               a_maildir_tbl_maxdist = i;
         }
         break;
      }else if(mp->m_maildir_hash == h &&
            !su_cs_cmp(&mp->m_maildir_file[4], name))
         break;

      if(n_UNLIKELY(mpp++ == a_maildir_tbl_top))
         mpp = a_maildir_tbl;
      if(++i > a_maildir_tbl_maxdist && n_UNLIKELY(data == NULL)){
         mp = NULL;
         break;
      }
   }
   n_NYD_OU;
   return mp;
}

static void
mktable(void)
{
   struct message *mp;
   size_t i;
   n_NYD_IN;

   i = a_maildir_tbl_prime = msgCount;
   i <<= 1;
   do
      a_maildir_tbl_prime = su_prime_lookup_next(a_maildir_tbl_prime);
   while(a_maildir_tbl_prime < i);
   a_maildir_tbl = n_calloc(a_maildir_tbl_prime, sizeof *a_maildir_tbl);
   a_maildir_tbl_top = &a_maildir_tbl[a_maildir_tbl_prime - 1];
   a_maildir_tbl_maxdist = 0;
   for(mp = message, i = msgCount; i-- != 0; ++mp)
      mdlook(&mp->m_maildir_file[4], mp);
   n_NYD_OU;
}

static enum okay
subdir_remove(char const *name, char const *sub)
{
   char *path;
   int pathsize, pathend, namelen, sublen, n;
   DIR *dirp;
   struct dirent *dp;
   enum okay rv = STOP;
   n_NYD_IN;

   namelen = su_cs_len(name);
   sublen = su_cs_len(sub);
   path = n_alloc(pathsize = namelen + sublen + 30 +1);
   su_mem_copy(path, name, namelen);
   path[namelen] = '/';
   su_mem_copy(path + namelen + 1, sub, sublen);
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
      n = su_cs_len(dp->d_name);
      if (UICMP(32, pathend + n +1, >, pathsize))
         path = n_realloc(path, pathsize = pathend + n + 30);
      su_mem_copy(path + pathend, dp->d_name, n +1);
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
   n_free(path);
   n_NYD_OU;
   return rv;
}

FL int
maildir_setfile(char const *who, char const * volatile name,
   enum fedit_mode fm)
{
   sighandler_type volatile saveint;
   struct cw cw;
   char const *emsg;
   int omsgCount;
   int volatile i = -1;
   n_NYD_IN;

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

   if(!n_is_dir(name, FAL0)){
      emsg = N_("Not a maildir: %s\n");
      goto jerr;
   }else if(chdir(name) < 0){
      emsg = N_("Cannot enter maildir://%s\n");
jerr:
      n_err(V_(emsg), n_shexp_quote_cp(name, FAL0));
      mb.mb_type = MB_VOID;
      *mailname = '\0';
      msgCount = 0;
      cwrelse(&cw);
      safe_signal(SIGINT, saveint);
      goto jleave;
   }

   a_maildir_tbl = NULL;
   if (sigsetjmp(_maildir_jmp, 1) == 0) {
      if (fm & FEDIT_NEWMAIL)
         mktable();
      if (saveint != SIG_IGN)
         safe_signal(SIGINT, &__maildircatch);
      i = _maildir_setfile1(name, fm, omsgCount);
   }
   if ((fm & FEDIT_NEWMAIL) && a_maildir_tbl != NULL)
      n_free(a_maildir_tbl);

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
         n_err(_("No mail for %s at %s\n"), who, n_shexp_quote_cp(name, FAL0));
      i = 1;
      goto jleave;
   }

   if ((fm & FEDIT_NEWMAIL) && msgCount > omsgCount)
      newmailinfo(omsgCount);
   i = 0;
jleave:
   n_NYD_OU;
   return i;
}

FL bool_t
maildir_quit(bool_t hold_sigs_on)
{
   sighandler_type saveint;
   struct cw cw;
   bool_t rv;
   n_NYD_IN;

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
   n_NYD_OU;
   return rv;
}

FL enum okay
maildir_append(char const *name, FILE *fp, long offset)
{
   struct n_timespec const *tsp;
   char *buf, *bp, *lp;
   size_t bufsize, buflen, cnt;
   off_t off1 = -1, offs;
   long size;
   int flag;
   enum {_NONE = 0, _INHEAD = 1<<0, _NLSEP = 1<<1} state;
   enum okay rv;
   n_NYD_IN;

   if ((rv = mkmaildir(name)) != OKAY)
      goto jleave;

   buf = n_alloc(bufsize = LINESIZE); /* TODO line pool; signals */
   buflen = 0;
   cnt = fsize(fp);
   offs = offset /* BSD will move due to O_APPEND! ftell(fp) */;
   size = 0;
   tsp = n_time_now(TRU1); /* TODO -> eventloop */

   n_autorec_relax_create();
   for (flag = MNEW, state = _NLSEP;;) {
      bp = fgetline(&buf, &bufsize, &cnt, &buflen, fp, 1);

      if (bp == NULL ||
            ((state & (_INHEAD | _NLSEP)) == _NLSEP &&
             is_head(buf, buflen, FAL0))) {
         if (off1 != (off_t)-1) {
            if ((rv = maildir_append1(tsp, name, fp, off1, size, flag)) == STOP)
               goto jfree;
            n_autorec_relax_unroll();
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
         if (!su_cs_cmp_case_n(buf, "status", 6)) {
            lp = buf + 6;
            while (su_cs_is_white(*lp))
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
         } else if (!su_cs_cmp_case_n(buf, "x-status", 8)) {
            lp = buf + 8;
            while (su_cs_is_white(*lp))
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
   n_autorec_relax_gut();
   n_free(buf);
jleave:
   n_NYD_OU;
   return rv;
}

FL enum okay
maildir_remove(char const *name)
{
   enum okay rv = STOP;
   n_NYD_IN;

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
   n_NYD_OU;
   return rv;
}
#endif /* mx_HAVE_MAILDIR */

/* s-it-mode */
