/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File I/O.
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

#include <fcntl.h>

#ifdef HAVE_WORDEXP
# include <wordexp.h>
#endif

#ifdef HAVE_SOCKETS
# include <sys/socket.h>

# include <netdb.h>

# include <netinet/in.h>

# ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
# endif
#endif

#ifdef HAVE_OPENSSL
# include <openssl/err.h>
# include <openssl/rand.h>
# include <openssl/ssl.h>
# include <openssl/x509v3.h>
# include <openssl/x509.h>
#endif

struct fio_stack {
   FILE           *s_file;    /* File we were in. */
   void           *s_cond;    /* Saved state of conditional stack */
   int            s_loading;  /* Loading .mailrc, etc. */
};

static struct fio_stack _fio_stack[FIO_STACK_SIZE];
static size_t           _fio_stack_size;
static FILE *           _fio_input;

/* Locate the user's mailbox file (where new, unread mail is queued) */
static void       _findmail(char *buf, size_t bufsize, char const *user,
                     bool_t force);

/* Perform shell meta character expansion */
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

/* Append the passed message descriptor onto the message structure */
static void       _fio_append(struct message *mp);

static enum okay  get_header(struct message *mp);

/* Write to socket fd, restarting on EINTR, unless anything is written */
#ifdef HAVE_SOCKETS
static long       xwrite(int fd, char const *data, size_t sz);
#endif

static void
_findmail(char *buf, size_t bufsize, char const *user, bool_t force)
{
   char *cp;
   NYD_ENTER;

   if (!strcmp(user, myname) && !force && (cp = ok_vlook(folder)) != NULL) {
      switch (which_protocol(cp)) {
      case PROTO_IMAP:
         if (strcmp(cp, protbase(cp)))
            goto jcopy;
         snprintf(buf, bufsize, "%s/INBOX", cp);
         goto jleave;
      default:
         break;
      }
   }

   if (force || (cp = ok_vlook(MAIL)) == NULL)
      snprintf(buf, bufsize, "%s/%s", MAILSPOOL, user);
   else
jcopy:
      n_strlcpy(buf, cp, bufsize);
jleave:
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
   i = wordexp(name, &we, 0);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);

   switch (i) {
   case 0:
      break;
   case WRDE_NOSPACE:
      if (!(fexpm & FEXP_SILENT))
         fprintf(stderr, tr(83, "\"%s\": Expansion buffer overflow.\n"), name);
      goto jleave;
   case WRDE_BADCHAR:
   case WRDE_SYNTAX:
   default:
      if (!(fexpm & FEXP_SILENT))
         fprintf(stderr, tr(242, "Syntax error in \"%s\"\n"), name);
      goto jleave;
   }

   switch (we.we_wordc) {
   case 1:
      cp = savestr(we.we_wordv[0]);
      break;
   case 0:
      if (!(fexpm & FEXP_SILENT))
         fprintf(stderr, tr(82, "\"%s\": No match.\n"), name);
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
         fprintf(stderr, tr(84, "\"%s\": Ambiguous.\n"), name);
      break;
   }
jleave:
   wordfree(&we);
   NYD_LEAVE;
   return cp;

#else /* HAVE_WORDEXP */
   struct stat sbuf;
   char xname[PATH_MAX], cmdbuf[PATH_MAX], /* also used for files */
      *shellp, *cp = NULL;
   int pivec[2], pid, l, waits;
   NYD_ENTER;

   if (pipe(pivec) < 0) {
      perror("pipe");
      goto jleave;
   }
   snprintf(cmdbuf, sizeof cmdbuf, "echo %s", name);
   if ((shellp = ok_vlook(SHELL)) == NULL)
      shellp = UNCONST(XSHELL);
   pid = start_command(shellp, 0, -1, pivec[1], "-c", cmdbuf, NULL);
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
      perror("read");
      close(pivec[0]);
      goto jleave;
   }
   close(pivec[0]);
   if (!wait_child(pid, &waits) && WTERMSIG(waits) != SIGPIPE) {
      if (!(fexpm & FEXP_SILENT))
         fprintf(stderr, tr(81, "\"%s\": Expansion failed.\n"), name);
      goto jleave;
   }
   if (l == 0) {
      if (!(fexpm & FEXP_SILENT))
         fprintf(stderr, tr(82, "\"%s\": No match.\n"), name);
      goto jleave;
   }
   if (l == sizeof xname) {
      if (!(fexpm & FEXP_SILENT))
         fprintf(stderr, tr(83, "\"%s\": Expansion buffer overflow.\n"), name);
      goto jleave;
   }
   xname[l] = 0;
   for (cp = &xname[l - 1]; *cp == '\n' && cp > xname; --cp)
      ;
   cp[1] = '\0';
   if (!(fexpm & FEXP_MULTIOK) && strchr(xname, ' ') != NULL &&
         stat(xname, &sbuf) < 0) {
      if (!(fexpm & FEXP_SILENT))
         fprintf(stderr, tr(84, "\"%s\": Ambiguous.\n"), name);
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
   NYD_ENTER;

   /* Last character is always '\0' and was added by fgets() */
   for (--linesize, i = 0; i < linesize; i++)
      if (line[i] == '\n')
         break;
   i = (i < linesize) ? i + 1 : linesize;
   NYD_LEAVE;
   return i;
}

static char *
_fgetline_byone(char **line, size_t *linesize, size_t *llen, FILE *fp,
   int appendnl, size_t n SMALLOC_DEBUG_ARGS)
{
   char *rv;
   int c;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

static void
makemessage(void)
{
   NYD_ENTER;
   if (msgCount == 0)
      _fio_append(NULL);
   setdot(message);
   message[msgCount].m_size = 0;
   message[msgCount].m_lines = 0;
   NYD_LEAVE;
}

static void
_fio_append(struct message *mp)
{
   NYD_ENTER;
   if (msgCount + 1 >= msgspace)
      message = srealloc(message, (msgspace += 64) * sizeof *message);
   if (msgCount > 0)
      message[msgCount - 1] = *mp;
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
#ifdef HAVE_IMAP
   case MB_IMAP:
   case MB_CACHE:
      rv = imap_header(mp);
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

#ifdef HAVE_SOCKETS
static long
xwrite(int fd, char const *data, size_t sz)
{
   long rv = -1, wo;
   size_t wt = 0;
   NYD_ENTER;

   do {
      if ((wo = write(fd, data + wt, sz - wt)) < 0) {
         if (errno == EINTR)
            continue;
         else
            goto jleave;
      }
      wt += wo;
   } while (wt < sz);
   rv = (long)sz;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_SOCKETS */

FL char *
(fgetline)(char **line, size_t *linesize, size_t *cnt, size_t *llen, FILE *fp,
   int appendnl SMALLOC_DEBUG_ARGS)
{
   size_t i_llen, sz;
   char *rv;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

FL int
(readline_restart)(FILE *ibuf, char **linebuf, size_t *linesize, size_t n
   SMALLOC_DEBUG_ARGS)
{
   /* TODO readline_restart(): always *appends* LF just to strip it again;
    * TODO should be configurable just as for fgetline(); ..or whatevr.. */
   int rv = -1;
   long sz;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

FL int
(readline_input)(char const *prompt, bool_t nl_escape, char **linebuf,
   size_t *linesize, char const *string SMALLOC_DEBUG_ARGS)
{
   /* TODO readline: linebuf pool! */
   FILE *ifile = (_fio_input != NULL) ? _fio_input : stdin;
   bool_t doprompt, dotty;
   int n;
   NYD_ENTER;

   doprompt = (!sourcing && (options & OPT_INTERACTIVE));
   dotty = (doprompt && !ok_blook(line_editor_disable));
   if (!doprompt)
      prompt = NULL;
   else if (prompt == NULL)
      prompt = getprompt();

   for (n = 0;;) {
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
         n = (tty_readline)(prompt, linebuf, linesize, n
               SMALLOC_DEBUG_ARGSCALL);
      } else {
         if (prompt != NULL && *prompt != '\0') {
            fputs(prompt, stdout);
            fflush(stdout);
         }
         n = (readline_restart)(ifile, linebuf, linesize, n
               SMALLOC_DEBUG_ARGSCALL);
      }
      if (n <= 0)
         break;
      /* POSIX says:
       * An unquoted <backslash> at the end of a command line shall
       * be discarded and the next line shall continue the command */
      if (nl_escape && (*linebuf)[n - 1] == '\\') {
         (*linebuf)[--n] = '\0';
         if (prompt != NULL && *prompt != '\0')
            prompt = ".. "; /* XXX PS2 .. */
         continue;
      }
      break;
   }
   NYD_LEAVE;
   return n;
}

FL char *
readstr_input(char const *prompt, char const *string)
{
   /* FIXME readstr_input: without linepool leaks on sigjmp */
   size_t linesize = 0;
   char *linebuf = NULL, *rv = NULL;
   int n;
   NYD_ENTER;

   n = readline_input(prompt, FAL0, &linebuf, &linesize, string);
   if (n > 0)
      rv = savestrbuf(linebuf, (size_t)n + 1);

   if (linebuf != NULL)
      free(linebuf);
   NYD_LEAVE;
   return rv;
}

FL void
setptr(FILE *ibuf, off_t offset)
{
   struct message this;
   char *cp, *linebuf = NULL;
   char const *cp2;
   int c, maybe = 1, inhead = 0, thiscnt = 0;
   size_t linesize = 0, filesize, cnt;
   NYD_ENTER;

   memset(&this, 0, sizeof this);
   this.m_flag = MUSED | MNEW | MNEWEST;
   filesize = mailsize - offset;
   offset = ftell(mb.mb_otf);

   for (;;) {
      if (fgetline(&linebuf, &linesize, &filesize, &cnt, ibuf, 0) == NULL) {
         this.m_xsize = this.m_size;
         this.m_xlines = this.m_lines;
         this.m_have = HAVE_HEADER | HAVE_BODY;
         if (thiscnt > 0)
            _fio_append(&this);
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
         perror("/tmp");
         exit(1);
      }
      if (linebuf[cnt - 1] == '\n')
         linebuf[cnt - 1] = '\0';
      if (maybe && linebuf[0] == 'F' && is_head(linebuf, cnt)) {
         /* TODO char date[FROM_DATEBUF];
          * TODO extract_date_from_from_(linebuf, cnt, date);
          * TODO this.m_time = 10000; */
         this.m_xsize = this.m_size;
         this.m_xlines = this.m_lines;
         this.m_have = HAVE_HEADER | HAVE_BODY;
         if (thiscnt++ > 0)
            _fio_append(&this);
         msgCount++;
         this.m_flag = MUSED | MNEW | MNEWEST;
         this.m_size = 0;
         this.m_lines = 0;
         this.m_block = mailx_blockof(offset);
         this.m_offset = mailx_offsetof(offset);
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
                     this.m_flag |= MREAD;
                  else if (c == 'O')
                     this.m_flag &= ~MNEW;
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
                     this.m_flag |= MFLAGGED;
                  else if (c == 'A')
                     this.m_flag |= MANSWERED;
                  else if (c == 'T')
                     this.m_flag |= MDRAFTED;
               break;
            }
            if (*cp != c && *cp != upperconv(c))
               break;
         }
      }
      offset += cnt;
      this.m_size += cnt;
      this.m_lines++;
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
         SEEK_SET) < 0) {
      perror("fseek");
      panic(tr(77, "temporary file seek"));
   }
   rv = mp->mb_itf;
jleave:
   NYD_LEAVE;
   return rv;
}

FL struct message *
setdot(struct message *mp)
{
   NYD_ENTER;
   if (dot != mp) {
      prevdot = dot;
      did_print_dot = FAL0;
   }
   dot = mp;
   uncollapse1(dot, 0);
   NYD_LEAVE;
   return dot;
}

FL int
rm(char const *name)
{
   struct stat sb;
   int rv = -1;
   NYD_ENTER;

   if (stat(name, &sb) < 0)
      ;
   else if (!S_ISREG(sb.st_mode))
      errno = EISDIR;
   else
      rv = unlink(name);
   NYD_LEAVE;
   return rv;
}

FL off_t
fsize(FILE *iob)
{
   struct stat sbuf;
   off_t rv;
   NYD_ENTER;

   rv = (fstat(fileno(iob), &sbuf) < 0) ? 0 : sbuf.st_size;
   NYD_LEAVE;
   return rv;
}

FL char *
fexpand(char const *name, enum fexp_mode fexpm)
{
   char cbuf[PATH_MAX], *res;
   struct str s;
   struct shortcut *sh;
   bool_t dyn;
   NYD_ENTER;

   /* The order of evaluation is "%" and "#" expand into constants.
    * "&" can expand into "+".  "+" can expand into shell meta characters.
    * Shell meta characters expand into constants.
    * This way, we make no recursive expansion */
   res = UNCONST(name);
   if (!(fexpm & FEXP_NSHORTCUT) && (sh = get_shortcut(res)) != NULL)
      res = sh->sh_long;

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
         fprintf(stderr, tr(80, "No previous file\n"));
         res = NULL;
         goto jleave;
      }
      res = prevfile;
      goto jislocal;
   case '&':
      if (res[1] == '\0') {
         if ((res = ok_vlook(MBOX)) == NULL)
            res = UNCONST("~/mbox");
         else if (res[0] != '&' || res[1] != '\0')
            goto jnext;
      }
      break;
   }

   if (res[0] == '@' && which_protocol(mailname) == PROTO_IMAP) {
      res = str_concat_csvl(&s, protbase(mailname), "/", res + 1, NULL)->s;
      dyn = TRU1;
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
   if (res[0] == '~' && (res[1] == '/' || res[1] == '\0')) {
      res = str_concat_csvl(&s, homedir, res + 1, NULL)->s;
      dyn = TRU1;
   }

   if (anyof(res, "|&;<>~{}()[]*?$`'\"\\") &&
         which_protocol(res) == PROTO_FILE) {
      res = _globname(res, fexpm);
      dyn = TRU1;
      goto jleave;
   }

jislocal:
   if (fexpm & FEXP_LOCAL)
      switch (which_protocol(res)) {
      case PROTO_FILE:
      case PROTO_MAILDIR:
         break;
      default:
         fprintf(stderr, tr(280,
            "`%s': only a local file or directory may be used\n"), name);
         res = NULL;
         break;
      }
jleave:
   if (res && !dyn)
      res = savestr(res);
   NYD_LEAVE;
   return res;
}

FL void
demail(void)
{
   NYD_ENTER;
   if (ok_blook(keep) || rm(mailname) < 0) {
      int fd = open(mailname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd >= 0)
         close(fd);
   }
   NYD_LEAVE;
}

FL bool_t
var_folder_updated(char const *name, char **store)
{
   char rv = TRU1;
   char *folder, *unres = NULL, *res = NULL;
   NYD_ENTER;

   if ((folder = UNCONST(name)) == NULL)
      goto jleave;

   /* Expand the *folder*; skip `%:' prefix for simplicity of use */
   /* XXX This *only* works because we do NOT
    * XXX update environment variables via the "set" mechanism */
   if (folder[0] == '%' && folder[1] == ':')
      folder += 2;
   if ((folder = fexpand(folder, FEXP_FULL)) == NULL) /* XXX error? */
      goto jleave;

   switch (which_protocol(folder)) {
   case PROTO_POP3:
      /* Ooops.  This won't work */
      fprintf(stderr, tr(501,
         "`folder' cannot be set to a flat, readonly POP3 account\n"));
      rv = FAL0;
      goto jleave;
   case PROTO_IMAP:
      /* Simply assign what we have, even including `%:' prefix */
      if (folder != name)
         goto jvcopy;
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
   res = ac_alloc(PATH_MAX);
   if (realpath(folder, res) == NULL)
      fprintf(stderr, tr(151, "Can't canonicalize `%s'\n"), folder);
   else
      folder = res;
#endif

jvcopy:
   *store = sstrdup(folder);

   if (res != NULL)
      ac_free(res);
   if (unres != NULL)
      ac_free(unres);
jleave:
   NYD_LEAVE;
   return rv;
}

FL bool_t
getfold(char *name, size_t size)
{
   char const *folder;
   NYD_ENTER;

   if ((folder = ok_vlook(folder)) != NULL)
      n_strlcpy(name, folder, size);
   NYD_LEAVE;
   return (folder != NULL);
}

FL char const *
getdeadletter(void) /* XXX should that be in auxlily.c? */
{
   char const *cp;
   NYD_ENTER;

   if ((cp = ok_vlook(DEAD)) == NULL || (cp = fexpand(cp, FEXP_LOCAL)) == NULL)
      cp = fexpand("~/dead.letter", FEXP_LOCAL | FEXP_SHELL);
   else if (*cp != '/') {
      size_t sz = strlen(cp) + 3;
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
#ifdef HAVE_IMAP
   case MB_IMAP:
   case MB_CACHE:
      rv = imap_body(mp);
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

#ifdef HAVE_SOCKETS
FL int
sclose(struct sock *sp)
{
   int i;
   NYD_ENTER;

   if (sp->s_fd > 0) {
      if (sp->s_onclose != NULL)
         (*sp->s_onclose)();
# ifdef HAVE_OPENSSL
      if (sp->s_use_ssl) {
         void *s_ssl = sp->s_ssl, *s_ctx = sp->s_ctx;
         sp->s_ssl = sp->s_ctx = NULL;
         sp->s_use_ssl = 0;
         assert(s_ssl != NULL);
         while (!SSL_shutdown(s_ssl)) /* XXX */
            ;
         SSL_free(s_ssl);
         SSL_CTX_free(s_ctx);
      }
# endif
      i = close(sp->s_fd);
      sp->s_fd = -1;
      goto jleave;
   }
   sp->s_fd = -1;
   i = 0;
jleave:
   NYD_LEAVE;
   return i;
}

FL enum okay
swrite(struct sock *sp, char const *data)
{
   enum okay rv;
   NYD_ENTER;

   rv = swrite1(sp, data, strlen(data), 0);
   NYD_LEAVE;
   return rv;
}

FL enum okay
swrite1(struct sock *sp, char const *data, int sz, int use_buffer)
{
   enum okay rv = STOP;
   int x;
   NYD_ENTER;

   if (use_buffer > 0) {
      int di;

      if (sp->s_wbuf == NULL) {
         sp->s_wbufsize = 4096;
         sp->s_wbuf = smalloc(sp->s_wbufsize);
         sp->s_wbufpos = 0;
      }
      while (sp->s_wbufpos + sz > sp->s_wbufsize) {
         di = sp->s_wbufsize - sp->s_wbufpos;
         sz -= di;
         if (sp->s_wbufpos > 0) {
            memcpy(&sp->s_wbuf[sp->s_wbufpos], data, di);
            rv = swrite1(sp, sp->s_wbuf, sp->s_wbufsize, -1);
         } else
            rv = swrite1(sp, data, sp->s_wbufsize, -1);
         if (rv != OKAY)
            goto jleave;
         data += di;
         sp->s_wbufpos = 0;
      }
      if (sz == sp->s_wbufsize) {
         rv = swrite1(sp, data, sp->s_wbufsize, -1);
         if (rv != OKAY)
            goto jleave;
      } else if (sz) {
         memcpy(&sp->s_wbuf[sp->s_wbufpos], data, sz);
         sp->s_wbufpos += sz;
      }
      rv = OKAY;
      goto jleave;
   } else if (use_buffer == 0 && sp->s_wbuf != NULL && sp->s_wbufpos > 0) {
      x = sp->s_wbufpos;
      sp->s_wbufpos = 0;
      if ((rv = swrite1(sp, sp->s_wbuf, x, -1)) != OKAY)
         goto jleave;
   }
   if (sz == 0) {
      rv = OKAY;
      goto jleave;
   }

# ifdef HAVE_OPENSSL
   if (sp->s_use_ssl) {
jssl_retry:
      x = SSL_write(sp->s_ssl, data, sz);
      if (x < 0) {
         switch (SSL_get_error(sp->s_ssl, x)) {
         case SSL_ERROR_WANT_READ:
         case SSL_ERROR_WANT_WRITE:
            goto jssl_retry;
         }
      }
   } else
# endif
   {
      x = xwrite(sp->s_fd, data, sz);
   }
   if (x != sz) {
      char o[512];
      snprintf(o, sizeof o, "%s write error",
         (sp->s_desc ? sp->s_desc : "socket"));
# ifdef HAVE_OPENSSL
      sp->s_use_ssl ? ssl_gen_err("%s", o) : perror(o);
# else
      perror(o);
# endif
      if (x < 0)
         sclose(sp);
      rv = STOP;
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
sopen(char const *xserver, struct sock *sp, int use_ssl, char const *uhp,
   char const *portstr)
{
# ifdef HAVE_SO_SNDTIMEO
   struct timeval tv;
# endif
# ifdef HAVE_SO_LINGER
   struct linger li;
# endif
# ifdef HAVE_IPV6
   char  hbuf[NI_MAXHOST];
   struct addrinfo hints, *res0, *res;
# else
   struct sockaddr_in servaddr;
   struct in_addr **pptr;
   struct hostent *hp;
   struct servent *ep;
   unsigned short port = 0;
# endif
   int sockfd;
   char *cp, *server = UNCONST(xserver);
   enum okay rv = STOP;
   NYD_ENTER;
   UNUSED(use_ssl);
   UNUSED(uhp);

   if ((cp = strchr(server, ':')) != NULL) { /* TODO URI parse! IPv6! */
      portstr = &cp[1];
# ifndef HAVE_IPV6
      port = strtol(portstr, NULL, 10);
# endif
      server = salloc(cp - xserver + 1);
      memcpy(server, xserver, cp - xserver);
      server[cp - xserver] = '\0';
   }

   /* Connect timeouts after 30 seconds */
# ifdef HAVE_SO_SNDTIMEO
   tv.tv_sec = 30;
   tv.tv_usec = 0;
# endif

# ifdef HAVE_IPV6
   if (options & OPT_VERBOSE)
      fprintf(stderr, "Resolving host %s . . .", server);
   memset(&hints, 0, sizeof hints);
   hints.ai_socktype = SOCK_STREAM;
   if (getaddrinfo(server, portstr, &hints, &res0) != 0) {
      fprintf(stderr, tr(252, " lookup of `%s' failed.\n"), server);
      goto jleave;
   } else if (options & OPT_VERBOSE)
      fprintf(stderr, tr(500, " done.\n"));

   sockfd = -1;
   for (res = res0; res != NULL && sockfd < 0; res = res->ai_next) {
      if (options & OPT_VERBOSE) {
         if (getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof hbuf,
               NULL, 0, NI_NUMERICHOST) != 0)
            strcpy(hbuf, "unknown host");
         fprintf(stderr, tr(192, "%sConnecting to %s:%s . . ."),
               (res == res0 ? "" : "\n"), hbuf, portstr);
      }
      sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (sockfd >= 0) {
#  ifdef HAVE_SO_SNDTIMEO
         setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#  endif
         if (connect(sockfd, res->ai_addr, res->ai_addrlen) != 0) {
            close(sockfd);
            sockfd = -1;
         }
      }
   }
   if (sockfd < 0) {
      perror(tr(254, " could not connect"));
      freeaddrinfo(res0);
      goto jleave;
   }
   freeaddrinfo(res0);

# else /* HAVE_IPV6 */
   if (port == 0) {
      if (!strcmp(portstr, "smtp"))
         port = htons(25);
      else if (!strcmp(portstr, "smtps"))
         port = htons(465);
#  ifdef HAVE_IMAP
      else if (!strcmp(portstr, "imap"))
         port = htons(143);
      else if (!strcmp(portstr, "imaps"))
         port = htons(993);
#  endif
#  ifdef HAVE_POP3
      else if (!strcmp(portstr, "pop3"))
         port = htons(110);
      else if (!strcmp(portstr, "pop3s"))
         port = htons(995);
#  endif
      else if ((ep = getservbyname(UNCONST(portstr), "tcp")) != NULL)
         port = ep->s_port;
      else {
         fprintf(stderr, tr(251, "Unknown service: %s\n"), portstr);
         rv = STOP;
         goto jleave;
      }
   } else
      port = htons(port);

   if (options & OPT_VERBOSE)
      fprintf(stderr, "Resolving host %s . . .", server);
   if ((hp = gethostbyname(server)) == NULL) {
      fprintf(stderr, tr(252, " lookup of `%s' failed.\n"), server);
      goto jleave;
   } else if (options & OPT_VERBOSE)
      fprintf(stderr, tr(500, " done.\n"));

   pptr = (struct in_addr**)hp->h_addr_list;
   if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror(tr(253, "could not create socket"));
      goto jleave;
   }
   memset(&servaddr, 0, sizeof servaddr);
   servaddr.sin_family = AF_INET;
   servaddr.sin_port = port;
   memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
   if (options & OPT_VERBOSE)
      fprintf(stderr, tr(192, "%sConnecting to %s:%d . . ."),
         "", inet_ntoa(**pptr), ntohs(port));

#  ifdef HAVE_SO_SNDTIMEO
   setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#  endif
   if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof servaddr) != 0) {
      perror(tr(254, " could not connect"));
      goto jleave;
   }
# endif /* !HAVE_IPV6 */
   if (options & OPT_VERBOSE)
      fputs(tr(193, " connected.\n"), stderr);

   /* And the regular timeouts */
# ifdef HAVE_SO_SNDTIMEO
   tv.tv_sec = 42;
   tv.tv_usec = 0;
   setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
   setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
# endif
# ifdef HAVE_SO_LINGER
   li.l_onoff = 1;
   li.l_linger = 42;
   setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
# endif

   memset(sp, 0, sizeof *sp);
   sp->s_fd = sockfd;
# ifdef HAVE_SSL
   if (use_ssl && ssl_open(server, sp, uhp) != OKAY) {
      sclose(sp);
      goto jleave;
   }
# endif
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
(sgetline)(char **line, size_t *linesize, size_t *linelen, struct sock *sp
   SMALLOC_DEBUG_ARGS)
{
   int rv;
   size_t lsize = *linesize;
   char *lp_base = *line, *lp = lp_base;
   NYD_ENTER;

   if (sp->s_rsz < 0) {
      sclose(sp);
      rv = sp->s_rsz;
      goto jleave;
   }

   do {
      if (lp_base == NULL || PTRCMP(lp, >, lp_base + lsize - 128)) {
         size_t diff = PTR2SIZE(lp - lp_base);
         *linesize = (lsize += 256); /* XXX magic */
         *line = lp_base = (srealloc)(lp_base, lsize SMALLOC_DEBUG_ARGSCALL);
         lp = lp_base + diff;
      }

      if (sp->s_rbufptr == NULL ||
            PTRCMP(sp->s_rbufptr, >=, sp->s_rbuf + sp->s_rsz)) {
# ifdef HAVE_OPENSSL
         if (sp->s_use_ssl) {
jssl_retry:
            sp->s_rsz = SSL_read(sp->s_ssl, sp->s_rbuf, sizeof sp->s_rbuf);
            if (sp->s_rsz <= 0) {
               if (sp->s_rsz < 0) {
                  char o[512];
                  switch(SSL_get_error(sp->s_ssl, sp->s_rsz)) {
                  case SSL_ERROR_WANT_READ:
                  case SSL_ERROR_WANT_WRITE:
                     goto jssl_retry;
                  }
                  snprintf(o, sizeof o, "%s",
                     (sp->s_desc ?  sp->s_desc : "socket"));
                  ssl_gen_err("%s", o);
               }
               break;
            }
         } else
# endif
         {
jagain:
            sp->s_rsz = read(sp->s_fd, sp->s_rbuf, sizeof sp->s_rbuf);
            if (sp->s_rsz <= 0) {
               if (sp->s_rsz < 0) {
                  char o[512];
                  if (errno == EINTR)
                     goto jagain;
                  snprintf(o, sizeof o, "%s",
                     (sp->s_desc ?  sp->s_desc : "socket"));
                  perror(o);
               }
               break;
            }
         }
         sp->s_rbufptr = sp->s_rbuf;
      }
   } while ((*lp++ = *sp->s_rbufptr++) != '\n');
   *lp = '\0';
   lsize = PTR2SIZE(lp - lp_base);

   if (linelen)
      *linelen = lsize;
   rv = (int)lsize;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_SOCKETS */

FL void
load(char const *name)
{
   FILE *in, *oldin;
   NYD_ENTER;

   if (name == NULL || (in = Fopen(name, "r")) == NULL)
      goto jleave;
   oldin = _fio_input;
   _fio_input = in;
   loading = TRU1;
   sourcing = TRU1;
   commands();
   loading = FAL0;
   sourcing = FAL0;
   _fio_input = oldin;
   Fclose(in);
jleave:
   NYD_LEAVE;
}

FL int
c_source(void *v)
{
   int rv = 1;
   char **arglist = v, *cp;
   FILE *fi;
   NYD_ENTER;

   if ((cp = fexpand(*arglist, FEXP_LOCAL)) == NULL)
      goto jleave;
   if ((fi = Fopen(cp, "r")) == NULL) {
      perror(cp);
      goto jleave;
   }

   if (_fio_stack_size >= NELEM(_fio_stack)) {
      fprintf(stderr, tr(3, "Too much \"sourcing\" going on.\n"));
      Fclose(fi);
      goto jleave;
   }

   _fio_stack[_fio_stack_size].s_file = _fio_input;
   _fio_stack[_fio_stack_size].s_cond = condstack_release();
   _fio_stack[_fio_stack_size].s_loading = loading;
   ++_fio_stack_size;
   loading = FAL0;
   _fio_input = fi;
   sourcing = TRU1;
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
unstack(void)
{
   int rv = 1;
   NYD_ENTER;

   if (_fio_stack_size == 0) {
      fprintf(stderr, tr(4, "\"Source\" stack over-pop.\n"));
      sourcing = FAL0;
      goto jleave;
   }

   Fclose(_fio_input);

   --_fio_stack_size;
   if (!condstack_take(_fio_stack[_fio_stack_size].s_cond))
      fprintf(stderr, tr(5, "Unmatched \"if\"\n"));
   loading = _fio_stack[_fio_stack_size].s_loading;
   _fio_input = _fio_stack[_fio_stack_size].s_file;
   if (_fio_stack_size == 0)
      sourcing = loading;
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

/* vim:set fenc=utf-8:s-it-mode */
