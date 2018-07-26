/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE fio

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* line is a buffer with the result of fgets(). Returns the first newline or
 * the last character read */
static size_t     _length_of_line(char const *line, size_t linesize);

/* Read a line, one character at a time */
static char *     _fgetline_byone(char **line, size_t *linesize, size_t *llen,
                     FILE *fp, int appendnl, size_t n n_MEMORY_DEBUG_ARGS);

/* Workhorse */
static bool_t a_file_lock(int fd, enum n_file_lock_type ft, off_t off,
               off_t len);

static size_t
_length_of_line(char const *line, size_t linesize)
{
   size_t i;
   NYD2_ENTER;

   /* Last character is always '\0' and was added by fgets() */
   for (--linesize, i = 0; i < linesize; i++)
      if (line[i] == '\n')
         break;
   i = (i < linesize) ? i + 1 : linesize;
   NYD2_LEAVE;
   return i;
}

static char *
_fgetline_byone(char **line, size_t *linesize, size_t *llen, FILE *fp,
   int appendnl, size_t n n_MEMORY_DEBUG_ARGS)
{
   char *rv;
   int c;
   NYD2_ENTER;

   assert(*linesize == 0 || *line != NULL);
   n_pstate &= ~n_PS_READLINE_NL;

   for (rv = *line;;) {
      if (*linesize <= LINESIZE || n >= *linesize - 128) {
         *linesize += ((rv == NULL) ? LINESIZE + n + 1 : 256);
         *line = rv = (n_realloc)(rv, *linesize n_MEMORY_DEBUG_ARGSCALL);
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
   NYD2_LEAVE;
   return rv;
}

static bool_t
a_file_lock(int fd, enum n_file_lock_type flt, off_t off, off_t len)
{
   struct flock flp;
   bool_t rv;
   NYD2_ENTER;

   memset(&flp, 0, sizeof flp);

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
      switch (n_err_no) {
      case n_ERR_BADF:
      case n_ERR_INVAL:
         rv = TRUM1;
         break;
      }
   NYD2_LEAVE;
   return rv;
}

FL char *
(fgetline)(char **line, size_t *linesize, size_t *cnt, size_t *llen, FILE *fp,
   int appendnl n_MEMORY_DEBUG_ARGS)
{
   size_t i_llen, sz;
   char *rv;
   NYD2_ENTER;

   if (cnt == NULL) {
      /* Without count, we can't determine where the chars returned by fgets()
       * end if there's no newline.  We have to read one character by one */
      rv = _fgetline_byone(line, linesize, llen, fp, appendnl, 0
            n_MEMORY_DEBUG_ARGSCALL);
      goto jleave;
   }

   n_pstate &= ~n_PS_READLINE_NL;

   if ((rv = *line) == NULL || *linesize < LINESIZE)
      *line = rv = (n_realloc)(rv, *linesize = LINESIZE
            n_MEMORY_DEBUG_ARGSCALL);
   sz = (*linesize <= *cnt) ? *linesize : *cnt + 1;
   if (sz <= 1 || fgets(rv, sz, fp) == NULL) {
      /* Leave llen untouched; it is used to determine whether the last line
       * was \n-terminated in some callers */
      rv = NULL;
      goto jleave;
   }

   i_llen = _length_of_line(rv, sz);
   *cnt -= i_llen;
   while (rv[i_llen - 1] != '\n') {
      *line = rv = (n_realloc)(rv, *linesize += 256 n_MEMORY_DEBUG_ARGSCALL);
      sz = *linesize - i_llen;
      sz = (sz <= *cnt) ? sz : *cnt + 1;
      if (sz <= 1 || fgets(rv + i_llen, sz, fp) == NULL) {
         if (appendnl) {
            rv[i_llen++] = '\n';
            rv[i_llen] = '\0';
         }
         break;
      }
      sz = _length_of_line(rv + i_llen, sz);
      i_llen += sz;
      *cnt -= sz;
   }
   if (llen)
      *llen = i_llen;
jleave:
   NYD2_LEAVE;
   return rv;
}

FL int
(readline_restart)(FILE *ibuf, char **linebuf, size_t *linesize, size_t n
   n_MEMORY_DEBUG_ARGS)
{
   /* TODO readline_restart(): always *appends* LF just to strip it again;
    * TODO should be configurable just as for fgetline(); ..or whatever..
    * TODO intwrap */
   int rv = -1;
   long sz;
   NYD2_ENTER;

   clearerr(ibuf);

   /* Interrupts will cause trouble if we are inside a stdio call. As this is
    * only relevant if input is from tty, bypass it by read(), then */
   if ((n_psonce & n_PSO_TTYIN) && fileno(ibuf) == 0) {
      assert(*linesize == 0 || *linebuf != NULL);
      n_pstate &= ~n_PS_READLINE_NL;
      for (;;) {
         if (*linesize <= LINESIZE || n >= *linesize - 128) {
            *linesize += ((*linebuf == NULL) ? LINESIZE + n + 1 : 256);
            *linebuf = (n_realloc)(*linebuf, *linesize n_MEMORY_DEBUG_ARGSCALL);
         }
jagain:
         sz = read(0, *linebuf + n, *linesize - n - 1);
         if (sz > 0) {
            n += sz;
            (*linebuf)[n] = '\0';
            if ((*linebuf)[n - 1] == '\n') {
               n_pstate |= n_PS_READLINE_NL;
               break;
            }
         } else {
            if (sz < 0 && n_err_no == n_ERR_INTR)
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
            n_MEMORY_DEBUG_ARGSCALL) == NULL)
         goto jleave;
   }
   if (n > 0 && (*linebuf)[n - 1] == '\n')
      (*linebuf)[--n] = '\0';
   rv = (int)n;
jleave:
   NYD2_LEAVE;
   return rv;
}

FL off_t
fsize(FILE *iob)
{
   struct stat sbuf;
   off_t rv;
   NYD_ENTER;

   rv = (fstat(fileno(iob), &sbuf) == -1) ? 0 : sbuf.st_size;
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_file_lock(int fd, enum n_file_lock_type flt, off_t off, off_t len,
   size_t pollmsecs)
{
   size_t tries;
   bool_t didmsg, rv;
   NYD_ENTER;

   if(pollmsecs == UIZ_MAX)
      pollmsecs = FILE_LOCK_MILLIS;

   n_UNINIT(rv, 0);
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
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
