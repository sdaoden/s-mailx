/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File operations, name word expansion.
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
#define n_FILE fio

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/wait.h>

#ifdef HAVE_WORDEXP
# include <wordexp.h>
#endif


struct fio_stack {
   FILE *s_file;     /* File we were in. */
   void *s_cond;     /* Saved state of conditional stack */
   ui32_t s_pstate;  /* Copy of ::pstate */
};

/* Slots in ::message */
static size_t           _message_space;

/* */
static struct fio_stack _fio_stack[FIO_STACK_SIZE];
static size_t           _fio_stack_size;
static FILE *           _fio_input;

/* Locate the user's mailbox file (where new, unread mail is queued) */
static void       _findmail(char *buf, size_t bufsize, char const *user,
                     bool_t force);

/* Perform shell meta character expansion TODO obsolete (INSECURE!) */
static char *     _globname(char const *name, enum fexp_mode fexpm);

/* line is a buffer with the result of fgets(). Returns the first newline or
 * the last character read */
static size_t     _length_of_line(char const *line, size_t linesize);

/* Read a line, one character at a time */
static char *     _fgetline_byone(char **line, size_t *linesize, size_t *llen,
                     FILE *fp, int appendnl, size_t n SMALLOC_DEBUG_ARGS);

/* Take the data out of the passed ghost file and toss it into a dynamically
 * allocated message structure */
static void       makemessage(void);

static enum okay  get_header(struct message *mp);

/* Workhorse */
static bool_t a_file_lock(int fd, enum n_file_lock_type ft, off_t off, off_t len);

/* `source' and `source_if' (if silent_error: no pipes allowed, then) */
static bool_t     _source_file(char const *file, bool_t silent_error);

static void
_findmail(char *buf, size_t bufsize, char const *user, bool_t force)
{
   char *cp;
   NYD_ENTER;

   if (!strcmp(user, myname) && !force && (cp = ok_vlook(folder)) != NULL) {
      ;
   }

   if (force || (cp = ok_vlook(MAIL)) == NULL)
      snprintf(buf, bufsize, "%s/%s", MAILSPOOL, user); /* TODO */
   else
      n_strscpy(buf, cp, bufsize);
   NYD_LEAVE;
}

static char *
_globname(char const *name, enum fexp_mode fexpm)
{
#ifdef HAVE_WORDEXP
   wordexp_t we;
   char *cp = NULL;
   sigset_t nset;
   int i;
   NYD_ENTER;

   /* Mac OS X Snow Leopard and Linux don't init fields on error, causing
    * SIGSEGV in wordfree(3); so let's just always zero it ourselfs */
   memset(&we, 0, sizeof we);

   /* Some systems (notably Open UNIX 8.0.0) fork a shell for wordexp()
    * and wait, which will fail if our SIGCHLD handler is active */
   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, NULL);
# ifndef WRDE_NOCMD
#  define WRDE_NOCMD 0
# endif
   i = wordexp(name, &we, WRDE_NOCMD);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);

   switch (i) {
   case 0:
      break;
#ifdef WRDE_CMDSUB
   case WRDE_CMDSUB:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Command substitution not allowed\n"), name);
      goto jleave;
#endif
   case WRDE_NOSPACE:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Expansion buffer overflow\n"), name);
      goto jleave;
   case WRDE_BADCHAR:
   case WRDE_SYNTAX:
   default:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("Syntax error in \"%s\"\n"), name);
      goto jleave;
   }

   switch (we.we_wordc) {
   case 1:
      cp = savestr(we.we_wordv[0]);
      break;
   case 0:
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": No match\n"), name);
      break;
   default:
      if (fexpm & FEXP_MULTIOK) {
         size_t j, l;

         for (l = 0, j = 0; j < we.we_wordc; ++j)
            l += strlen(we.we_wordv[j]) + 1;
         ++l;
         cp = salloc(l);
         for (l = 0, j = 0; j < we.we_wordc; ++j) {
            size_t x = strlen(we.we_wordv[j]);
            memcpy(cp + l, we.we_wordv[j], x);
            l += x;
            cp[l++] = ' ';
         }
         cp[l] = '\0';
      } else if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Ambiguous\n"), name);
      break;
   }
jleave:
   wordfree(&we);
   NYD_LEAVE;
   return cp;

#else /* HAVE_WORDEXP */
   struct stat sbuf;
   char xname[PATH_MAX +1], cmdbuf[PATH_MAX +1], /* also used for files */
      *shellp, *cp = NULL;
   int pivec[2], pid, l, waits;
   NYD_ENTER;

   if (pipe(pivec) < 0) {
      n_perr(_("pipe"), 0);
      goto jleave;
   }
   snprintf(cmdbuf, sizeof cmdbuf, "echo %s", name);
   if ((shellp = ok_vlook(SHELL)) == NULL)
      shellp = UNCONST(XSHELL);
   pid = start_command(shellp, NULL, COMMAND_FD_NULL, pivec[1],
         "-c", cmdbuf, NULL, NULL);
   if (pid < 0) {
      close(pivec[0]);
      close(pivec[1]);
      goto jleave;
   }
   close(pivec[1]);

jagain:
   l = read(pivec[0], xname, sizeof xname);
   if (l < 0) {
      if (errno == EINTR)
         goto jagain;
      n_perr(_("read"), 0);
      close(pivec[0]);
      goto jleave;
   }
   close(pivec[0]);
   if (!wait_child(pid, &waits) && WTERMSIG(waits) != SIGPIPE) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Expansion failed\n"), name);
      goto jleave;
   }
   if (l == 0) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": No match\n"), name);
      goto jleave;
   }
   if (l == sizeof xname) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Expansion buffer overflow\n"), name);
      goto jleave;
   }
   xname[l] = 0;
   for (cp = xname + l - 1; *cp == '\n' && cp > xname; --cp)
      ;
   cp[1] = '\0';
   if (!(fexpm & FEXP_MULTIOK) && strchr(xname, ' ') != NULL &&
         stat(xname, &sbuf) < 0) {
      if (!(fexpm & FEXP_SILENT))
         n_err(_("\"%s\": Ambiguous\n"), name);
      cp = NULL;
      goto jleave;
   }
   cp = savestr(xname);
jleave:
   NYD_LEAVE;
   return cp;
#endif /* !HAVE_WORDEXP */
}

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
   int appendnl, size_t n SMALLOC_DEBUG_ARGS)
{
   char *rv;
   int c;
   NYD2_ENTER;

   assert(*linesize == 0 || *line != NULL);
   for (rv = *line;;) {
      if (*linesize <= LINESIZE || n >= *linesize - 128) {
         *linesize += ((rv == NULL) ? LINESIZE + n + 1 : 256);
         *line = rv = (srealloc)(rv, *linesize SMALLOC_DEBUG_ARGSCALL);
      }
      c = getc(fp);
      if (c != EOF) {
         rv[n++] = c;
         rv[n] = '\0';
         if (c == '\n')
            break;
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

static void
makemessage(void)
{
   NYD_ENTER;
   if (msgCount == 0)
      message_append(NULL);
   setdot(message);
   message[msgCount].m_size = 0;
   message[msgCount].m_lines = 0;
   NYD_LEAVE;
}

static enum okay
get_header(struct message *mp)
{
   enum okay rv;
   NYD_ENTER;
   UNUSED(mp);

   switch (mb.mb_type) {
   case MB_FILE:
   case MB_MAILDIR:
      rv = OKAY;
      break;
#ifdef HAVE_POP3
   case MB_POP3:
      rv = pop3_header(mp);
      break;
#endif
   case MB_VOID:
   default:
      rv = STOP;
      break;
   }
   NYD_LEAVE;
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

   rv = (fcntl(fd, F_SETLK, &flp) != -1);
   NYD2_LEAVE;
   return rv;
}

static bool_t
_source_file(char const *file, bool_t silent_error)
{
   char *cp;
   bool_t ispipe;
   FILE *fi;
   NYD_ENTER;

   fi = NULL;

   if ((ispipe = !silent_error)) {
      size_t i = strlen(file);

      while (i > 0 && spacechar(file[i - 1]))
         --i;
      if (i > 0 && file[i - 1] == '|')
         cp = savestrbuf(file, --i);
      else
         ispipe = FAL0;
   }

   if (ispipe) {
      char const *sh;

      if ((sh = ok_vlook(SHELL)) == NULL)
         sh = XSHELL;
      if ((fi = Popen(cp, "r", sh, NULL, COMMAND_FD_NULL)) == NULL) {
         n_perr(cp, 0);
         goto jleave;
      }
   } else if ((cp = fexpand(file, FEXP_LOCAL)) == NULL)
      goto jleave;
   else if ((fi = Fopen(cp, "r")) == NULL) {
      if (!silent_error || (options & OPT_D_V))
         n_perr(cp, 0);
      goto jleave;
   }

   if (temporary_localopts_store != NULL) {
      n_err(_("Before v15 you cannot `source' from within macros, sorry\n"));
      goto jeclose;
   }
   if (_fio_stack_size >= NELEM(_fio_stack)) {
      n_err(_("Too many `source' recursions\n"));
jeclose:
      if (ispipe)
         Pclose(fi, TRU1);
      else
         Fclose(fi);
      fi = NULL;
      goto jleave;
   }

   _fio_stack[_fio_stack_size].s_file = _fio_input;
   _fio_stack[_fio_stack_size].s_cond = condstack_release();
   _fio_stack[_fio_stack_size].s_pstate = pstate;
   ++_fio_stack_size;
   pstate &= ~(PS_LOADING | PS_PIPING);
   pstate |= PS_SOURCING | (ispipe ? PS_PIPING : PS_NONE);
   _fio_input = fi;
jleave:
   NYD_LEAVE;
   return (fi != NULL);
}

FL char *
(fgetline)(char **line, size_t *linesize, size_t *cnt, size_t *llen, FILE *fp,
   int appendnl SMALLOC_DEBUG_ARGS)
{
   size_t i_llen, sz;
   char *rv;
   NYD2_ENTER;

   if (cnt == NULL) {
      /* Without count, we can't determine where the chars returned by fgets()
       * end if there's no newline.  We have to read one character by one */
      rv = _fgetline_byone(line, linesize, llen, fp, appendnl, 0
            SMALLOC_DEBUG_ARGSCALL);
      goto jleave;
   }

   if ((rv = *line) == NULL || *linesize < LINESIZE)
      *line = rv = (srealloc)(rv, *linesize = LINESIZE SMALLOC_DEBUG_ARGSCALL);
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
      *line = rv = (srealloc)(rv, *linesize += 256 SMALLOC_DEBUG_ARGSCALL);
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
   SMALLOC_DEBUG_ARGS)
{
   /* TODO readline_restart(): always *appends* LF just to strip it again;
    * TODO should be configurable just as for fgetline(); ..or whatever.. */
   int rv = -1;
   long sz;
   NYD2_ENTER;

   clearerr(ibuf);

   /* Interrupts will cause trouble if we are inside a stdio call. As this is
    * only relevant if input is from tty, bypass it by read(), then */
   if (fileno(ibuf) == 0 && (options & OPT_TTYIN)) {
      assert(*linesize == 0 || *linebuf != NULL);
      for (;;) {
         if (*linesize <= LINESIZE || n >= *linesize - 128) {
            *linesize += ((*linebuf == NULL) ? LINESIZE + n + 1 : 256);
            *linebuf = (srealloc)(*linebuf, *linesize SMALLOC_DEBUG_ARGSCALL);
         }
jagain:
         sz = read(0, *linebuf + n, *linesize - n - 1);
         if (sz > 0) {
            n += sz;
            (*linebuf)[n] = '\0';
            if (n > 0 && (*linebuf)[n - 1] == '\n')
               break;
         } else {
            if (sz < 0 && errno == EINTR)
               goto jagain;
            if (n > 0) {
               if ((*linebuf)[n - 1] != '\n') {
                  (*linebuf)[n++] = '\n';
                  (*linebuf)[n] = '\0';
               }
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
            SMALLOC_DEBUG_ARGSCALL) == NULL)
         goto jleave;
   }
   if (n > 0 && (*linebuf)[n - 1] == '\n')
      (*linebuf)[--n] = '\0';
   rv = (int)n;
jleave:
   NYD2_LEAVE;
   return rv;
}

FL int
(readline_input)(char const *prompt, bool_t nl_escape, char **linebuf,
   size_t *linesize, char const *string SMALLOC_DEBUG_ARGS)
{
   /* TODO readline: linebuf pool! */
   FILE *ifile = (_fio_input != NULL) ? _fio_input : stdin;
   bool_t doprompt, dotty;
   int n, nold;
   NYD2_ENTER;

   doprompt = (!(pstate & PS_SOURCING) && (options & OPT_INTERACTIVE));
   dotty = (doprompt && !ok_blook(line_editor_disable));
   if (!doprompt)
      prompt = NULL;
   else if (prompt == NULL)
      prompt = getprompt();

   /* Ensure stdout is flushed first anyway */
   if (!dotty && prompt == NULL)
      fflush(stdout);

   for (nold = n = 0;;) {
      if (dotty) {
         assert(ifile == stdin);
         if (string != NULL && (n = (int)strlen(string)) > 0) {
            if (*linesize > 0)
               *linesize += n +1;
            else
               *linesize = (size_t)n + LINESIZE +1;
            *linebuf = (srealloc)(*linebuf, *linesize SMALLOC_DEBUG_ARGSCALL);
            memcpy(*linebuf, string, (size_t)n +1);
         }
         string = NULL;
         /* TODO if nold>0, don't redisplay the entire line!
          * TODO needs complete redesign ... */
         n = (n_tty_readline)(prompt, linebuf, linesize, n
               SMALLOC_DEBUG_ARGSCALL);
      } else {
         if (prompt != NULL) {
            if (*prompt != '\0')
               fputs(prompt, stdout);
            fflush(stdout);
         }
         n = (readline_restart)(ifile, linebuf, linesize, n
               SMALLOC_DEBUG_ARGSCALL);

         if (n > 0 && nold > 0) {
            int i = 0;
            char const *cp = *linebuf + nold;

            while (blankspacechar(*cp) && nold + i < n)
               ++cp, ++i;
            if (i > 0) {
               memmove(*linebuf + nold, cp, n - nold - i);
               n -= i;
               (*linebuf)[n] = '\0';
            }
         }
      }
      if (n <= 0)
         break;

      /* POSIX says:
       * An unquoted <backslash> at the end of a command line shall
       * be discarded and the next line shall continue the command */
      if (!nl_escape || n == 0 || (*linebuf)[n - 1] != '\\')
         break;
      (*linebuf)[nold = --n] = '\0';
      if (prompt != NULL && *prompt != '\0')
         prompt = ".. "; /* XXX PS2 .. */
   }

   if (n >= 0 && (options & OPT_D_VV))
      n_err(_("%s %d bytes <%.*s>\n"),
         ((pstate & PS_LOADING) ? "LOAD"
          : (pstate & PS_SOURCING) ? "SOURCE" : "READ"),
         n, n, *linebuf);
   NYD2_LEAVE;
   return n;
}

FL char *
n_input_cp_addhist(char const *prompt, char const *string, bool_t isgabby)
{
   /* FIXME n_input_cp_addhist(): leaks on sigjmp without linepool */
   size_t linesize = 0;
   char *linebuf = NULL, *rv = NULL;
   int n;
   NYD2_ENTER;

   n = readline_input(prompt, FAL0, &linebuf, &linesize, string);
   if (n > 0 && *(rv = savestrbuf(linebuf, (size_t)n)) != '\0' &&
         (options & OPT_INTERACTIVE))
      n_tty_addhist(rv, isgabby);

   if (linebuf != NULL)
      free(linebuf);
   NYD2_LEAVE;
   return rv;
}

FL void
setptr(FILE *ibuf, off_t offset)
{
   struct message self;
   char *cp, *linebuf = NULL;
   char const *cp2;
   int c, maybe = 1, inhead = 0, selfcnt = 0;
   size_t linesize = 0, filesize, cnt;
   NYD_ENTER;

   memset(&self, 0, sizeof self);
   self.m_flag = MUSED | MNEW | MNEWEST;
   filesize = mailsize - offset;
   offset = ftell(mb.mb_otf);

   for (;;) {
      if (fgetline(&linebuf, &linesize, &filesize, &cnt, ibuf, 0) == NULL) {
         self.m_xsize = self.m_size;
         self.m_xlines = self.m_lines;
         self.m_have = HAVE_HEADER | HAVE_BODY;
         if (selfcnt > 0)
            message_append(&self);
         makemessage();
         if (linebuf)
            free(linebuf);
         break;
      }

#ifdef notdef
      if (linebuf[0] == '\0')
         linebuf[0] = '.';
#endif
      /* XXX Convert CRLF to LF; this should be rethought in that
       * XXX CRLF input should possibly end as CRLF output? */
      if (cnt >= 2 && linebuf[cnt - 1] == '\n' && linebuf[cnt - 2] == '\r')
         linebuf[--cnt - 1] = '\n';
      fwrite(linebuf, sizeof *linebuf, cnt, mb.mb_otf);
      if (ferror(mb.mb_otf)) {
         n_perr(_("/tmp"), 0);
         exit(EXIT_ERR);
      }
      if (linebuf[cnt - 1] == '\n')
         linebuf[cnt - 1] = '\0';
      if (maybe && linebuf[0] == 'F' && is_head(linebuf, cnt, FAL0)) {
         /* TODO char date[FROM_DATEBUF];
          * TODO extract_date_from_from_(linebuf, cnt, date);
          * TODO self.m_time = 10000; */
         self.m_xsize = self.m_size;
         self.m_xlines = self.m_lines;
         self.m_have = HAVE_HEADER | HAVE_BODY;
         if (selfcnt++ > 0)
            message_append(&self);
         msgCount++;
         self.m_flag = MUSED | MNEW | MNEWEST;
         self.m_size = 0;
         self.m_lines = 0;
         self.m_block = mailx_blockof(offset);
         self.m_offset = mailx_offsetof(offset);
         inhead = 1;
      } else if (linebuf[0] == 0) {
         inhead = 0;
      } else if (inhead) {
         for (cp = linebuf, cp2 = "status";; ++cp) {
            if ((c = *cp2++) == 0) {
               while (c = *cp++, whitechar(c))
                  ;
               if (cp[-1] != ':')
                  break;
               while ((c = *cp++) != '\0')
                  if (c == 'R')
                     self.m_flag |= MREAD;
                  else if (c == 'O')
                     self.m_flag &= ~MNEW;
               break;
            }
            if (*cp != c && *cp != upperconv(c))
               break;
         }
         for (cp = linebuf, cp2 = "x-status";; ++cp) {
            if ((c = *cp2++) == 0) {
               while ((c = *cp++, whitechar(c)))
                  ;
               if (cp[-1] != ':')
                  break;
               while ((c = *cp++) != '\0')
                  if (c == 'F')
                     self.m_flag |= MFLAGGED;
                  else if (c == 'A')
                     self.m_flag |= MANSWERED;
                  else if (c == 'T')
                     self.m_flag |= MDRAFTED;
               break;
            }
            if (*cp != c && *cp != upperconv(c))
               break;
         }
      }
      offset += cnt;
      self.m_size += cnt;
      ++self.m_lines;
      maybe = linebuf[0] == 0;
   }
   NYD_LEAVE;
}

FL int
putline(FILE *obuf, char *linebuf, size_t cnt)
{
   int rv = -1;
   NYD_ENTER;

   fwrite(linebuf, sizeof *linebuf, cnt, obuf);
   putc('\n', obuf);
   if (!ferror(obuf))
      rv = (int)(cnt + 1);
   NYD_LEAVE;
   return rv;
}

FL FILE *
setinput(struct mailbox *mp, struct message *m, enum needspec need)
{
   FILE *rv = NULL;
   enum okay ok = STOP;
   NYD_ENTER;

   switch (need) {
   case NEED_HEADER:
      ok = (m->m_have & HAVE_HEADER) ? OKAY : get_header(m);
      break;
   case NEED_BODY:
      ok = (m->m_have & HAVE_BODY) ? OKAY : get_body(m);
      break;
   case NEED_UNSPEC:
      ok = OKAY;
      break;
   }
   if (ok != OKAY)
      goto jleave;

   fflush(mp->mb_otf);
   if (fseek(mp->mb_itf, (long)mailx_positionof(m->m_block, m->m_offset),
         SEEK_SET) == -1) {
      n_perr(_("fseek"), 0);
      n_panic(_("temporary file seek"));
   }
   rv = mp->mb_itf;
jleave:
   NYD_LEAVE;
   return rv;
}

FL void
message_reset(void)
{
   NYD_ENTER;
   if (message != NULL) {
      free(message);
      message = NULL;
   }
   msgCount = 0;
   _message_space = 0;
   NYD_LEAVE;
}

FL void
message_append(struct message *mp)
{
   NYD_ENTER;
   if (UICMP(z, msgCount + 1, >=, _message_space)) {
      /* XXX remove _message_space magics (or use s_Vector) */
      _message_space = (_message_space >= 128 && _message_space <= 1000000)
            ? _message_space << 1 : _message_space + 64;
      message = srealloc(message, _message_space * sizeof *message);
   }
   if (msgCount > 0) {
      if (mp != NULL)
         message[msgCount - 1] = *mp;
      else
         memset(message + msgCount - 1, 0, sizeof *message);
   }
   NYD_LEAVE;
}

FL bool_t
message_match(struct message *mp, struct search_expr const *sep,
   bool_t with_headers)
{
   char **line;
   size_t *linesize, cnt;
   FILE *fp;
   bool_t rv = FAL0;
   NYD_ENTER;

   if ((fp = Ftmp(NULL, "mpmatch", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
      goto j_leave;

   if (sendmp(mp, fp, NULL, NULL, SEND_TOSRCH, NULL) < 0)
      goto jleave;
   fflush_rewind(fp);

   cnt = fsize(fp);
   line = &termios_state.ts_linebuf; /* XXX line pool */
   linesize = &termios_state.ts_linesize; /* XXX line pool */

   if (!with_headers)
      while (fgetline(line, linesize, &cnt, NULL, fp, 0))
         if (**line == '\n')
            break;

   while (fgetline(line, linesize, &cnt, NULL, fp, 0)) {
#ifdef HAVE_REGEX
      if (sep->ss_sexpr == NULL) {
         if (regexec(&sep->ss_regex, *line, 0,NULL, 0) == REG_NOMATCH)
            continue;
      } else
#endif
      if (!substr(*line, sep->ss_sexpr))
         continue;
      rv = TRU1;
      break;
   }

jleave:
   Fclose(fp);
j_leave:
   NYD_LEAVE;
   return rv;
}

FL struct message *
setdot(struct message *mp)
{
   NYD_ENTER;
   if (dot != mp) {
      prevdot = dot;
      pstate &= ~PS_DID_PRINT_DOT;
   }
   dot = mp;
   uncollapse1(dot, 0);
   NYD_LEAVE;
   return dot;
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

FL char *
fexpand(char const *name, enum fexp_mode fexpm)
{
   char cbuf[PATH_MAX +1];
   char const *res;
   struct str s;
   bool_t dyn;
   NYD_ENTER;

   /* The order of evaluation is "%" and "#" expand into constants.
    * "&" can expand into "+".  "+" can expand into shell meta characters.
    * Shell meta characters expand into constants.
    * This way, we make no recursive expansion */
   if ((fexpm & FEXP_NSHORTCUT) || (res = shortcut_expand(name)) == NULL)
      res = UNCONST(name);

   if (fexpm & FEXP_SHELL) {
      dyn = FAL0;
      goto jshell;
   }
jnext:
   dyn = FAL0;
   switch (*res) {
   case '%':
      if (res[1] == ':' && res[2] != '\0') {
         res = &res[2];
         goto jnext;
      }
      _findmail(cbuf, sizeof cbuf, (res[1] != '\0' ? res + 1 : myname),
         (res[1] != '\0' || (options & OPT_u_FLAG)));
      res = cbuf;
      goto jislocal;
   case '#':
      if (res[1] != '\0')
         break;
      if (prevfile[0] == '\0') {
         n_err(_("No previous file\n"));
         res = NULL;
         goto jleave;
      }
      res = prevfile;
      goto jislocal;
   case '&':
      if (res[1] == '\0') {
         if ((res = ok_vlook(MBOX)) == NULL)
            res = UNCONST("~/mbox"); /* XXX no magics (POSIX though) */
         else if (res[0] != '&' || res[1] != '\0')
            goto jnext;
      }
      break;
   }

   if (res[0] == '+' && getfold(cbuf, sizeof cbuf)) {
      size_t i = strlen(cbuf);

      res = str_concat_csvl(&s, cbuf,
            ((i > 0 && cbuf[i - 1] == '/') ? "" : "/"), res + 1, NULL)->s;
      dyn = TRU1;

      if (res[0] == '%' && res[1] == ':') {
         res += 2;
         goto jnext;
      }
   }

   /* Catch the most common shell meta character */
jshell:
   if (res[0] == '~') {
      res = n_shell_expand_tilde(res, NULL);
      dyn = TRU1;
   }
   if (anyof(res, "|&;<>{}()[]*?$`'\"\\"))
      switch (which_protocol(res)) {
      case PROTO_FILE:
      case PROTO_MAILDIR:
         res = (fexpm & FEXP_NSHELL) ? n_shell_expand_var(res, TRU1, NULL)
               : _globname(res, fexpm);
         dyn = TRU1;
         goto jleave;
      default:
         break;
      }
jislocal:
   if (fexpm & FEXP_LOCAL)
      switch (which_protocol(res)) {
      case PROTO_FILE:
      case PROTO_MAILDIR:
         break;
      default:
         n_err(_("Not a local file or directory: \"%s\"\n"), name);
         res = NULL;
         break;
      }
jleave:
   if (res && !dyn)
      res = savestr(res);
   NYD_LEAVE;
   return UNCONST(res);
}

FL char *
fexpand_nshell_quote(char const *name)
{
   size_t i, j;
   char *rv, c;
   NYD_ENTER;

   for (i = j = 0; (c = name[i]) != '\0'; ++i)
      if (c == '\\')
         ++j;

   if (j == 0)
      rv = savestrbuf(name, i);
   else {
      rv = salloc(i + j +1);
      for (i = j = 0; (c = name[i]) != '\0'; ++i) {
         rv[j++] = c;
         if (c == '\\')
            rv[j++] = c;
      }
      rv[j] = '\0';
   }
   NYD_LEAVE;
   return rv;
}

FL bool_t
var_folder_updated(char const *name, char **store)
{
   char rv = TRU1;
   char *folder, *unres = NULL, *res = NULL;
   NYD_ENTER;

   if ((folder = UNCONST(name)) == NULL)
      goto jleave;

   /* Expand the *folder*; skip %: prefix for simplicity of use */
   /* XXX This *only* works because we do NOT
    * XXX update environment variables via the "set" mechanism */
   if (folder[0] == '%' && folder[1] == ':')
      folder += 2;
   if ((folder = fexpand(folder, FEXP_FULL)) == NULL) /* XXX error? */
      goto jleave;

   switch (which_protocol(folder)) {
   case PROTO_POP3:
      n_err(_("*folder* cannot be set to a flat, readonly POP3 account\n"));
      rv = FAL0;
      goto jleave;
   default:
      /* Further expansion desired */
      break;
   }

   /* All non-absolute paths are relative to our home directory */
   if (*folder != '/') {
      size_t l1 = strlen(homedir), l2 = strlen(folder);
      unres = ac_alloc(l1 + l2 + 1 +1);
      memcpy(unres, homedir, l1);
      unres[l1] = '/';
      memcpy(unres + l1 + 1, folder, l2);
      unres[l1 + 1 + l2] = '\0';
      folder = unres;
   }

   /* Since lex.c:_update_mailname() uses realpath(3) if available to
    * avoid that we loose track of our currently open folder in case we
    * chdir away, but still checks the leading path portion against
    * getfold() to be able to abbreviate to the +FOLDER syntax if
    * possible, we need to realpath(3) the folder, too */
#ifdef HAVE_REALPATH
   res = ac_alloc(PATH_MAX +1);
   if (realpath(folder, res) == NULL)
      n_err(_("Can't canonicalize \"%s\"\n"), folder);
   else
      folder = res;
#endif

   *store = sstrdup(folder);

   if (res != NULL)
      ac_free(res);
   if (unres != NULL)
      ac_free(unres);
jleave:
   NYD_LEAVE;
   return rv;
}

FL char const *
getdeadletter(void) /* XXX should that be in auxlily.c? */
{
   char const *cp;
   NYD_ENTER;

   if ((cp = ok_vlook(DEAD)) == NULL || (cp = fexpand(cp, FEXP_LOCAL)) == NULL)
      cp = fexpand("~/dead.letter", FEXP_LOCAL | FEXP_SHELL);
   else if (*cp != '/') {
      size_t sz = strlen(cp) + 2 +1;
      char *buf = ac_alloc(sz);

      snprintf(buf, sz, "~/%s", cp);
      cp = fexpand(buf, FEXP_LOCAL | FEXP_SHELL);
      ac_free(buf);
   }

   if (cp == NULL)
      cp = "dead.letter"; /* XXX magic -> nail.h (POSIX thing though) */
   NYD_LEAVE;
   return cp;
}

FL enum okay
get_body(struct message *mp)
{
   enum okay rv;
   NYD_ENTER;
   UNUSED(mp);

   switch (mb.mb_type) {
   case MB_FILE:
   case MB_MAILDIR:
      rv = OKAY;
      break;
#ifdef HAVE_POP3
   case MB_POP3:
      rv = pop3_body(mp);
      break;
#endif
   case MB_VOID:
   default:
      rv = STOP;
      break;
   }
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_file_lock(int fd, enum n_file_lock_type flt, off_t off, off_t len,
   size_t pollmsecs)
{
   size_t tries;
   bool_t rv;
   NYD_ENTER;

   for (tries = 0; tries <= FILE_LOCK_TRIES; ++tries)
      if ((rv = a_file_lock(fd, flt, off, len)) || pollmsecs == 0)
         break;
      else
         n_msleep(pollmsecs, FAL0);
   NYD_LEAVE;
   return rv;
}

FL void
load(char const *name)
{
   struct str n;
   void *cond;
   FILE *in, *oldin;
   NYD_ENTER;

   if (name == NULL || *name == '\0' || (in = Fopen(name, "r")) == NULL)
      goto jleave;

   oldin = _fio_input;
   _fio_input = in;
   pstate |= PS_IN_LOAD;
   /* commands() may sreset(), copy over file name */
   n.l = strlen(name);
   n.s = ac_alloc(n.l +1);
   memcpy(n.s, name, n.l +1);

   cond = condstack_release();
   if (!commands())
      n_err(_("Stopped loading \"%s\" due to errors "
         "(enable *debug* for trace)\n"), n.s);
   condstack_take(cond);

   ac_free(n.s);
   pstate &= ~PS_IN_LOAD;
   _fio_input = oldin;
   Fclose(in);
jleave:
   NYD_LEAVE;
}

FL int
c_source(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _source_file(*(char**)v, FAL0) ? 0 : 1;
   NYD_LEAVE;
   return rv;
}

FL int
c_source_if(void *v) /* XXX obsolete?, support file tests in `if' etc.! */
{
   int rv;
   NYD_ENTER;

   rv = _source_file(*(char**)v, TRU1) ? 0 : 1;
   rv = 0;
   NYD_LEAVE;
   return rv;
}

FL int
unstack(void)
{
   int rv = 1;
   NYD_ENTER;

   if (_fio_stack_size == 0) {
      n_err(_("`source' stack over-pop\n"));
      pstate &= ~PS_SOURCING;
      goto jleave;
   }

   if (pstate & PS_PIPING)
      Pclose(_fio_input, TRU1);
   else
      Fclose(_fio_input);

   --_fio_stack_size;
   if (!condstack_take(_fio_stack[_fio_stack_size].s_cond))
      n_err(_("Unmatched \"if\"\n"));
   pstate &= ~(PS_LOADING | PS_PIPING);
   pstate |= _fio_stack[_fio_stack_size].s_pstate & (PS_LOADING | PS_PIPING);
   _fio_input = _fio_stack[_fio_stack_size].s_file;
   if (_fio_stack_size == 0) {
      if (pstate & PS_LOADING)
         pstate |= PS_SOURCING;
      else
         pstate &= ~PS_SOURCING;
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
