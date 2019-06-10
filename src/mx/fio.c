/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE fio
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/* TODO fake */
#include "su/code-in.h"

struct a_fio_lpool_ent{
   struct a_fio_lpool_ent *fiole_last;
   char *fiole_dat;
   uz fiole_size;
};

struct a_fio_lpool_ent *a_fio_lpool_free;
struct a_fio_lpool_ent *a_fio_lpool_used;

/* line is a buffer with the result of fgets(). Returns the first newline or
 * the last character read */
static uz     _length_of_line(char const *line, uz linesize);

/* Read a line, one character at a time */
static char *     _fgetline_byone(char **line, uz *linesize, uz *llen,
                     FILE *fp, int appendnl, uz n  su_DBG_LOC_ARGS_DECL);

/* Workhorse */
static boole a_file_lock(int fd, enum n_file_lock_type ft, off_t off,
               off_t len);

static uz
_length_of_line(char const *line, uz linesize)
{
   uz i;
   NYD2_IN;

   /* Last character is always '\0' and was added by fgets() */
   for (--linesize, i = 0; i < linesize; i++)
      if (line[i] == '\n')
         break;
   i = (i < linesize) ? i + 1 : linesize;
   NYD2_OU;
   return i;
}

static char *
_fgetline_byone(char **line, uz *linesize, uz *llen, FILE *fp,
   int appendnl, uz n  su_DBG_LOC_ARGS_DECL)
{
   char *rv;
   int c;
   NYD2_IN;

   ASSERT(*linesize == 0 || *line != NULL);
   n_pstate &= ~n_PS_READLINE_NL;

   for (rv = *line;;) {
      if (*linesize <= LINESIZE || n >= *linesize - 128) {
         *linesize += ((rv == NULL) ? LINESIZE + n + 1 : 256);
         *line = rv = su_MEM_REALLOC_LOCOR(rv, *linesize,
               su_DBG_LOC_ARGS_ORUSE);
      }
      c = getc(fp);
      if (c != EOF) {
         rv[n++] = c;
         rv[n] = '\0';
         if (c == '\n') {
            n_pstate |= n_PS_READLINE_NL;
            break;
         }
      } else {
         if (n > 0) {
            if (appendnl) {
               rv[n++] = '\n';
               rv[n] = '\0';
            }
            break;
         } else {
            rv = NULL;
            goto jleave;
         }
      }
   }
   if (llen)
      *llen = n;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_file_lock(int fd, enum n_file_lock_type flt, off_t off, off_t len)
{
   struct flock flp;
   boole rv;
   NYD2_IN;

   su_mem_set(&flp, 0, sizeof flp);

   switch (flt) {
   default:
   case FLT_READ:    rv = F_RDLCK;  break;
   case FLT_WRITE:   rv = F_WRLCK;  break;
   }
   flp.l_type = rv;
   flp.l_start = off;
   flp.l_whence = SEEK_SET;
   flp.l_len = len;

   if (!(rv = (fcntl(fd, F_SETLK, &flp) != -1)))
      switch (su_err_no()) {
      case su_ERR_BADF:
      case su_ERR_INVAL:
         rv = TRUM1;
         break;
      }
   NYD2_OU;
   return rv;
}

FL void
mx_linepool_aquire(char **dp, uz *sp){
   struct a_fio_lpool_ent *lpep;
   NYD2_IN;

   if((lpep = a_fio_lpool_free) != NIL)
      a_fio_lpool_free = lpep->fiole_last;
   else
      lpep = su_TCALLOC(struct a_fio_lpool_ent, 1);

   lpep->fiole_last = a_fio_lpool_used;
   a_fio_lpool_used = lpep;
   *dp = lpep->fiole_dat;
   lpep->fiole_dat = NIL;
   *sp = lpep->fiole_size;
   NYD2_OU;
}

FL void
mx_linepool_release(char *d, uz s){
   struct a_fio_lpool_ent *lpep;
   NYD2_IN;

   ASSERT(a_fio_lpool_used != NIL);
   lpep = a_fio_lpool_used;
   a_fio_lpool_used = lpep->fiole_last;

   lpep->fiole_last = a_fio_lpool_free;
   a_fio_lpool_free = lpep;
   lpep->fiole_dat = d;
   lpep->fiole_size = s;
   NYD2_OU;
}

FL void mx_linepool_cleanup(void){
   struct a_fio_lpool_ent *lpep, *tmp;
   NYD2_IN;

   lpep = a_fio_lpool_used;
   a_fio_lpool_used = NIL;
jredo:
   while((tmp = lpep) != NIL){
      lpep = lpep->fiole_last;
      if(tmp->fiole_dat != NIL)
         su_FREE(tmp->fiole_dat);
      su_FREE(tmp);
   }

   if((lpep = a_fio_lpool_free) != NIL){
      a_fio_lpool_free = NIL;
      goto jredo;
   }
   NYD2_OU;
}

FL char *
(fgetline)(char **line, uz *linesize, uz *cnt, uz *llen, FILE *fp,
   int appendnl su_DBG_LOC_ARGS_DECL)
{
   uz i_llen, size;
   char *rv;
   NYD2_IN;

   if (cnt == NULL) {
      /* Without count, we can't determine where the chars returned by fgets()
       * end if there's no newline.  We have to read one character by one */
      rv = _fgetline_byone(line, linesize, llen, fp, appendnl, 0
            su_DBG_LOC_ARGS_USE);
      goto jleave;
   }

   n_pstate &= ~n_PS_READLINE_NL;

   if ((rv = *line) == NULL || *linesize < LINESIZE)
      *line = rv = su_MEM_REALLOC_LOCOR(rv, *linesize = LINESIZE,
            su_DBG_LOC_ARGS_ORUSE);
   size = (*linesize <= *cnt) ? *linesize : *cnt + 1;
   if (size <= 1 || fgets(rv, size, fp) == NULL) {
      /* Leave llen untouched; it is used to determine whether the last line
       * was \n-terminated in some callers */
      rv = NULL;
      goto jleave;
   }

   i_llen = _length_of_line(rv, size);
   *cnt -= i_llen;
   while (rv[i_llen - 1] != '\n') {
      *line = rv = su_MEM_REALLOC_LOCOR(rv, *linesize += 256,
            su_DBG_LOC_ARGS_ORUSE);
      size = *linesize - i_llen;
      size = (size <= *cnt) ? size : *cnt + 1;
      if (size <= 1 || fgets(rv + i_llen, size, fp) == NULL) {
         if (appendnl) {
            rv[i_llen++] = '\n';
            rv[i_llen] = '\0';
         }
         break;
      }
      size = _length_of_line(rv + i_llen, size);
      i_llen += size;
      *cnt -= size;
   }
   if (llen)
      *llen = i_llen;
jleave:
   NYD2_OU;
   return rv;
}

FL int
(readline_restart)(FILE *ibuf, char **linebuf, uz *linesize, uz n
   su_DBG_LOC_ARGS_DECL)
{
   /* TODO readline_restart(): always *appends* LF just to strip it again;
    * TODO should be configurable just as for fgetline(); ..or whatever..
    * TODO intwrap */
   int rv = -1;
   long size;
   NYD2_IN;

   clearerr(ibuf);

   /* Interrupts will cause trouble if we are inside a stdio call. As this is
    * only relevant if input is from tty, bypass it by read(), then */
   if ((n_psonce & n_PSO_TTYIN) && fileno(ibuf) == 0) {
      ASSERT(*linesize == 0 || *linebuf != NULL);
      n_pstate &= ~n_PS_READLINE_NL;
      for (;;) {
         if (*linesize <= LINESIZE || n >= *linesize - 128) {
            *linesize += ((*linebuf == NULL) ? LINESIZE + n + 1 : 256);
            *linebuf = su_MEM_REALLOC_LOCOR(*linebuf, *linesize,
                  su_DBG_LOC_ARGS_ORUSE);
         }
jagain:
         size = read(0, *linebuf + n, *linesize - n - 1);
         if (size > 0) {
            n += size;
            (*linebuf)[n] = '\0';
            if ((*linebuf)[n - 1] == '\n') {
               n_pstate |= n_PS_READLINE_NL;
               break;
            }
         } else {
            if (size < 0 && su_err_no() == su_ERR_INTR)
               goto jagain;
            /* TODO eh.  what is this?  that now supposed to be a line?!? */
            if (n > 0) {
               if ((*linebuf)[n - 1] != '\n') {
                  (*linebuf)[n++] = '\n';
                  (*linebuf)[n] = '\0';
               } else
                  n_pstate |= n_PS_READLINE_NL;
               break;
            } else
               goto jleave;
         }
      }
   } else {
      /* Not reading from standard input or standard input not a terminal. We
       * read one char at a time as it is the only way to get lines with
       * embedded NUL characters in standard stdio */
      if (_fgetline_byone(linebuf, linesize, &n, ibuf, 1, n
            su_DBG_LOC_ARGS_USE) == NULL)
         goto jleave;
   }
   if (n > 0 && (*linebuf)[n - 1] == '\n')
      (*linebuf)[--n] = '\0';
   rv = (int)n;
jleave:
   NYD2_OU;
   return rv;
}

FL off_t
fsize(FILE *iob)
{
   struct stat sbuf;
   off_t rv;
   NYD_IN;

   rv = (fstat(fileno(iob), &sbuf) == -1) ? 0 : sbuf.st_size;
   NYD_OU;
   return rv;
}

FL boole
n_file_lock(int fd, enum n_file_lock_type flt, off_t off, off_t len,
   uz pollmsecs)
{
   uz tries;
   boole didmsg, rv;
   NYD_IN;

   if(pollmsecs == UZ_MAX)
      pollmsecs = FILE_LOCK_MILLIS;

   UNINIT(rv, 0);
   for (didmsg = FAL0, tries = 0; tries <= FILE_LOCK_TRIES; ++tries) {
      rv = a_file_lock(fd, flt, off, len);

      if (rv == TRUM1) {
         rv = FAL0;
         break;
      }
      if (rv || pollmsecs == 0)
         break;
      else {
         if(!didmsg){
            n_err(_("Failed to create a file lock, waiting %lu milliseconds "),
               pollmsecs);
            didmsg = TRU1;
         }else
            n_err(".");
         n_msleep(pollmsecs, FAL0);
      }
   }
   if(didmsg)
      n_err(" %s\n", (rv ? _("ok") : _("failure")));
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
