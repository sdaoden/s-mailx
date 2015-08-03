/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File I/O, including resource file loading etc.
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

#ifdef HAVE_SOCKETS
# include <sys/socket.h>

# include <netdb.h>

# include <netinet/in.h>

# ifdef HAVE_ARPA_INET_H
#  include <arpa/inet.h>
# endif
#endif

#ifdef HAVE_WORDEXP
# include <wordexp.h>
#endif

#ifdef HAVE_OPENSSL
# include <openssl/err.h>
# include <openssl/rand.h>
# include <openssl/ssl.h>
# include <openssl/x509v3.h>
# include <openssl/x509.h>
#endif

#ifdef HAVE_DOTLOCK
# include "dotlock.h"
#endif

struct fio_stack {
   FILE  *s_file;    /* File we were in. */
   void  *s_cond;    /* Saved state of conditional stack */
   int   s_loading;  /* Loading .mailrc, etc. */
};

struct shvar_stack {
   struct shvar_stack *shs_next;
   char const  *shs_value; /* Remaining value to expand */
   size_t      shs_len;    /* gth of .shs_dat this level */
   char const  *shs_dat;   /* Result data of this level */
};

/* Slots in ::message */
static size_t           _message_space;

/* XXX Our Popen() main() takes void, temporary global data store */
#ifdef HAVE_DOTLOCK
static enum file_lock_type _dotlock_flt;
static int              _dotlock_fd;
struct dotlock_info *   _dotlock_dip;
#endif

/* */
static struct fio_stack _fio_stack[FIO_STACK_SIZE];
static size_t           _fio_stack_size;
static FILE *           _fio_input;

/* Locate the user's mailbox file (where new, unread mail is queued) */
static void       _findmail(char *buf, size_t bufsize, char const *user,
                     bool_t force);

/* Perform shell variable expansion */
static char *     _shvar_exp(char const *name);
static char *     __shvar_exp(struct shvar_stack *shsp);

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
static bool_t     _file_lock(int fd, enum file_lock_type ft,
                     off_t off, off_t len);

/* main() of fork(2)ed dot file locker */
#ifdef HAVE_DOTLOCK
static int        _dotlock_main(void);
#endif

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
_shvar_exp(char const *name)
{
   struct shvar_stack top;
   char *rv;
   NYD2_ENTER;

   memset(&top, 0, sizeof top);
   top.shs_value = name;
   rv = __shvar_exp(&top);

   NYD2_LEAVE;
   return rv;
}

static char *
__shvar_exp(struct shvar_stack *shsp)
{
   struct shvar_stack next, *np, *tmp;
   char const *vp;
   char lc, c, *cp, *rv;
   size_t i;
   NYD2_ENTER;

   if (*(vp = shsp->shs_value) != '$') {
      union {bool_t hadbs; char c;} u = {FAL0};

      shsp->shs_dat = vp;
      for (lc = '\0', i = 0; ((c = *vp) != '\0'); ++i, ++vp) {
         if (c == '$' && lc != '\\')
            break;
         lc = (lc == '\\') ? (u.hadbs = TRU1, '\0') : c;
      }
      shsp->shs_len = i;

      if (u.hadbs) {
         shsp->shs_dat = cp = savestrbuf(shsp->shs_dat, i);

         for (lc = '\0', rv = cp; (u.c = *cp++) != '\0';) {
            if (u.c != '\\' || lc == '\\')
               *rv++ = u.c;
            lc = (lc == '\\') ? '\0' : u.c;
         }
         *rv = '\0';

         shsp->shs_len = PTR2SIZE(rv - shsp->shs_dat);
      }
   } else {
      if ((lc = (*++vp == '{')))
         ++vp;

      shsp->shs_dat = vp;
      for (i = 0; (c = *vp) != '\0'; ++i, ++vp)
         if (!alnumchar(c) && c != '_')
            break;

      if (lc) {
         if (c != '}') {
            n_err(_("Variable name misses closing \"}\": \"%s\"\n"),
               shsp->shs_value);
            shsp->shs_len = strlen(shsp->shs_value);
            shsp->shs_dat = shsp->shs_value;
            goto junroll;
         }
         c = *++vp;
      }

      shsp->shs_len = i;
      if ((cp = vok_vlook(savestrbuf(shsp->shs_dat, i))) != NULL)
         shsp->shs_len = strlen(shsp->shs_dat = cp);
   }
   if (c != '\0')
      goto jrecurse;

   /* That level made the great and completed encoding.  Build result */
junroll:
   for (i = 0, np = shsp, shsp = NULL; np != NULL;) {
      i += np->shs_len;
      tmp = np->shs_next;
      np->shs_next = shsp;
      shsp = np;
      np = tmp;
   }

   cp = rv = salloc(i +1);
   while (shsp != NULL) {
      np = shsp;
      shsp = shsp->shs_next;
      memcpy(cp, np->shs_dat, np->shs_len);
      cp += np->shs_len;
   }
   *cp = '\0';

jleave:
   NYD2_LEAVE;
   return rv;
jrecurse:
   memset(&next, 0, sizeof next);
   next.shs_next = shsp;
   next.shs_value = vp;
   rv = __shvar_exp(&next);
   goto jleave;
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
   char xname[PATH_MAX], cmdbuf[PATH_MAX], /* also used for files */
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
   pid = start_command(shellp, NULL, -1, pivec[1], "-c", cmdbuf, NULL, NULL);
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

static bool_t
_file_lock(int fd, enum file_lock_type flt, off_t off, off_t len)
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

#ifdef HAVE_DOTLOCK
static int
_dotlock_main(void)
{
   struct dotlock_info di;
   struct stat stb, fdstb;
   char name[NAME_MAX];
   enum dotlock_state dls;
   char const *cp;
   int fd;
   enum file_lock_type flt;
   NYD_ENTER;

   /* Get the arguments "passed to us" */
   flt = _dotlock_flt;
   fd = _dotlock_fd;
   UNUSED(fd);
   di = *_dotlock_dip;

   /* chdir(2)? */
jislink:
   dls = DLS_CANT_CHDIR | DLS_ABANDON;

   if ((cp = strrchr(di.di_file_name, '/')) != NULL) {
      char const *fname = cp + 1;

      while (PTRCMP(cp - 1, >, di.di_file_name) && cp[-1] == '/')
         --cp;
      cp = savestrbuf(di.di_file_name, PTR2SIZE(cp - di.di_file_name));
      if (chdir(cp))
         goto jmsg;

      di.di_file_name = fname;
   }

   /* So we're here, but then again the file can be a symbolic link!
    * This is however only true if we do not have realpath(3) available since
    * that'll have resolved the path already otherwise; nonetheless, let
    * readlink(2) be a precondition for dotlocking and keep this code */
   if (lstat(cp = di.di_file_name, &stb) == -1)
      goto jmsg;
   if (S_ISLNK(stb.st_mode)) {
      /* Use salloc() and hope we stay in builtin buffer.. */
      char *x;
      size_t i;
      ssize_t sr;

      for (x = NULL, i = PATH_MAX;; i += PATH_MAX) {
         x = salloc(i +1);
         sr = readlink(cp, x, i);
         if (sr <= 0) {
            dls = DLS_FISHY | DLS_ABANDON;
            goto jmsg;
         }
         if (UICMP(z, sr, <, i)) {
            x[sr] = '\0';
            i = (size_t)sr;
            break;
         }
      }
      di.di_file_name = x;
      goto jislink;
   }

   dls = DLS_FISHY | DLS_ABANDON;

   /* Bail out if the file has changed its identity in the meanwhile */
   if (fstat(fd, &fdstb) == -1 ||
         fdstb.st_dev != stb.st_dev || fdstb.st_ino != stb.st_ino ||
         fdstb.st_uid != stb.st_uid || fdstb.st_gid != stb.st_gid ||
         fdstb.st_mode != stb.st_mode)
      goto jmsg;

   /* Be aware, even if the error is false! */
   {
      int i = snprintf(name, sizeof name, "%s.lock", di.di_file_name);
      if (i < 0 || UICMP(z, NAME_MAX, <=, (uiz_t)i + 1 +1)) {
         dls = DLS_NAMETOOLONG | DLS_ABANDON;
         goto jmsg;
      }
# ifdef HAVE_PATHCONF
      else {
         /* fd is a file, not portable to use for _PC_NAME_MAX */
         long pc;

         if ((pc = pathconf(".", _PC_NAME_MAX)) == -1 || pc <= (long)i + 1 +1) {
            dls = DLS_NAMETOOLONG | DLS_ABANDON;
            goto jmsg;
         }
      }
# endif
      di.di_lock_name = name;
   }

   /* Ignore SIGPIPE, the child reacts upon EPIPE instead */
   safe_signal(SIGPIPE, SIG_IGN);

   /* We are in the directory of the mailbox for which we have to create
    * a dotlock file for.  We don't know wether we have realpath(3) available,
    * and manually resolving the path is due especially given that S-nail
    * supports the special "%:" syntax to warp any file into a "system
    * mailbox"; there may also be multiple system mailbox directories...
    * So what we do is that we fstat(2) the mailbox and check its UID and
    * GID against that of our own process: if any of those mismatch we must
    * either assume a directory we are not allowed to write in, or that we run
    * via -u/$USER/%USER as someone else, in which case we favour our
    * privilege-separated dotlock process */
   if (stb.st_uid != user_id || stb.st_gid != group_id || access(".", W_OK)) {
      char itoabuf[64];
      char const *args[13];

      snprintf(itoabuf, sizeof itoabuf, "%" PRIuZ, di.di_pollmsecs);
      args[ 0] = PRIVSEP;
      args[ 1] = (flt == FLT_READ ? "rdotlock" : "wdotlock");
      args[ 2] = "mailbox";   args[ 3] = di.di_file_name;
      args[ 4] = "name";      args[ 5] = di.di_lock_name;
      args[ 6] = "hostname";  args[ 7] = di.di_hostname;
      args[ 8] = "randstr";   args[ 9] = di.di_randstr;
      args[10] = "pollmsecs"; args[11] = itoabuf;
      args[12] = NULL;
      execv(LIBEXECDIR "/" UAGENT "-privsep", UNCONST(args));

      dls = DLS_NOEXEC;
      write(STDOUT_FILENO, &dls, sizeof dls);
      /* But fall through and try it with normal privileges! */
   }

   /* So let's try and call it ourselfs!  Note that we don't block signals just
    * like our privsep child does, the user will anyway be able to remove his
    * file again, and if we're in -u/$USER mode then we are allowed to access
    * the user's box: shall we leave behind a stale dotlock then at least we
    * start a friendly human conversation.  Since we cannot handle SIGKILL and
    * SIGSTOP malicious things could happen whatever we do */
   safe_signal(SIGHUP, SIG_IGN);
   safe_signal(SIGINT, SIG_IGN);
   safe_signal(SIGQUIT, SIG_IGN);
   safe_signal(SIGTERM, SIG_IGN);

   NYD;
   dls = _dotlock_create(&di);
   NYD;

   /* Finally: notify our parent about the actual lock state.. */
jmsg:
   write(STDOUT_FILENO, &dls, sizeof dls);
   close(STDOUT_FILENO);

   /* ..then eventually wait until we shall remove the lock again, which will
    * be notified via the read returning */
   if (dls == DLS_NONE) {
      read(STDIN_FILENO, &dls, sizeof dls);

      unlink(name);
   }
   NYD_LEAVE;
   return EXIT_OK;
}
#endif /* HAVE_DOTLOCK */

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
         n = (tty_readline)(prompt, linebuf, linesize, n
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
readstr_input(char const *prompt, char const *string)
{
   /* FIXME readstr_input: leaks on sigjmp without linepool */
   size_t linesize = 0;
   char *linebuf = NULL, *rv = NULL;
   int n;
   NYD2_ENTER;

   n = readline_input(prompt, FAL0, &linebuf, &linesize, string);
   if (n > 0)
      rv = savestrbuf(linebuf, (size_t)n + 1);

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
         SEEK_SET) < 0) {
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

   if ((fp = Ftmp(NULL, "mpmatch", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL)
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
   char cbuf[PATH_MAX];
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
   if (anyof(res, "|&;<>{}()[]*?$`'\"\\"))
      switch (which_protocol(res)) {
      case PROTO_FILE:
      case PROTO_MAILDIR:
         res = (fexpm & FEXP_NSHELL) ? _shvar_exp(res) : _globname(res, fexpm);
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
   res = ac_alloc(PATH_MAX +1);
   if (realpath(folder, res) == NULL)
      n_err(_("Can't canonicalize \"%s\"\n"), folder);
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

FL bool_t
file_lock(int fd, enum file_lock_type flt, off_t off, off_t len,
   size_t pollmsecs)
{
   size_t tries;
   bool_t rv;
   NYD_ENTER;

   for (tries = 0; tries <= FILE_LOCK_TRIES; ++tries)
      if ((rv = _file_lock(fd, flt, off, len)) || pollmsecs == 0)
         break;
      else
         sleep(1); /* TODO pollmsecs -> use finer grain */
   NYD_LEAVE;
   return rv;
}

FL FILE *
dot_lock(char const *fname, int fd, enum file_lock_type flt,
   off_t off, off_t len, size_t pollmsecs)
{
#undef _DOMSG
#ifdef HAVE_DOTLOCK
# define _DOMSG() n_err(_("Creating dotlock for \"%s\" "), fname)
#else
# define _DOMSG() n_err(_("Trying to lock file \"%s\" "), fname)
#endif

#ifdef HAVE_DOTLOCK
   int cpipe[2];
   struct dotlock_info di;
   enum dotlock_state dls;
   char const *emsg = NULL;
#endif
   int UNINIT(serrno, 0);
   union {size_t tries; int (*ptf)(void); char const *sh; ssize_t r;} u;
   bool_t flocked, didmsg = FAL0;
   FILE *rv = NULL;
   NYD_ENTER;

   if (options & OPT_D_VV) {
      _DOMSG();
      didmsg = TRUM1;
   }

   flocked = FAL0;
   for (u.tries = 0; !_file_lock(fd, flt, off, len);)
      switch ((serrno = errno)) {
      case EACCES:
      case EAGAIN:
      case ENOLCK:
         if (pollmsecs > 0 && ++u.tries < FILE_LOCK_TRIES) {
            if (!didmsg)
               _DOMSG();
            n_err(".");
            didmsg = TRUM1;
            sleep(1); /* TODO pollmsecs -> use finer grain */
            continue;
         }
         /* FALLTHRU */
      default:
         goto jleave;
      }
   flocked = TRU1;

#ifndef HAVE_DOTLOCK
jleave:
   if (didmsg == TRUM1)
      n_err("\n");
   if (flocked)
      rv = (FILE*)-1;
   else
      errno = serrno;
   NYD_LEAVE;
   return rv;

#else
   /* Create control-pipe for our dot file locker process, which will remove
    * the lock and terminate once the pipe is closed, for whatever reason */
   if (pipe_cloexec(cpipe) == -1) {
      serrno = errno;
      emsg = N_("  Can't create file lock control pipe\n");
      goto jemsg;
   }

   /* And the locker process itself; it'll be a (rather cheap) thread only
    * unless the lock has to be placed in the system spool and we have our
    * privilege-separated dotlock program available, in which case that will be
    * executed and do "it" with changed group-id */
   di.di_file_name = fname;
   di.di_pollmsecs = pollmsecs;
   /* Initialize some more stuff; query the two strings in the parent in order
    * to cache the result of the former and anyway minimalize child page-ins.
    * Especially uname(3) may hang for multiple seconds when it is called the
    * first time! */
   di.di_hostname = nodename(FAL0);
   di.di_randstr = getrandstring(16);
   _dotlock_flt = flt;
   _dotlock_fd = fd;
   _dotlock_dip = &di;

   u.ptf = &_dotlock_main;
   rv = Popen((char*)-1, "W", u.sh, NULL, cpipe[1]);
   serrno = errno;

   close(cpipe[1]);
   if (rv == NULL) {
      close(cpipe[0]);
      emsg = N_("  Can't create file lock process\n");
      goto jemsg;
   }

   /* Let's check wether we were able to create the dotlock file */
   for (;;) {
      u.r = read(cpipe[0], &dls, sizeof dls);
      if (UICMP(z, u.r, !=, sizeof dls)) {
         serrno = (u.r != -1) ? EAGAIN : errno;
         dls = DLS_DUNNO | DLS_ABANDON;
      } else
         serrno = 0;

      if (dls & DLS_ABANDON)
         close(cpipe[0]);

      switch (dls & ~DLS_ABANDON) {
      case DLS_NONE:
         goto jleave;
      case DLS_CANT_CHDIR:
         if (options & OPT_D_V)
            emsg = N_("  Can't change to directory! Please check permissions\n");
         serrno = EACCES;
         break;
      case DLS_NAMETOOLONG:
         emsg = N_("Resulting dotlock filename would be too long\n");
         serrno = EACCES;
         break;
      case DLS_NOPERM:
         if (options & OPT_D_V)
            emsg = N_("  Can't create a lock file! Please check permissions\n"
                  "  (Maybe setting *dotlock-ignore-error* variable helps.)\n");
         serrno = EACCES;
         break;
      case DLS_NOEXEC:
         if (options & OPT_D_V)
            emsg = N_("  Can't find privilege-separated file lock program\n");
         serrno = ENOENT;
         break;
      case DLS_PRIVFAILED:
         emsg = N_("  Privilege-separated file lock program can't change "
               "privileges\n");
         serrno = EPERM;
         break;
      case DLS_EXIST:
         emsg = N_("  It seems there is a stale dotlock file?\n"
               "  Please remove the lock file manually, then retry\n");
         serrno = EEXIST;
         break;
      case DLS_FISHY:
         emsg = N_("  Fishy!  Is someone trying to \"steal\" foreign files?\n"
               "  Please check the mailbox file etc. manually, then retry\n");
         serrno = EAGAIN; /* ? Hack to ignore *dotlock-ignore-error* xxx */
         break;
      default:
      case DLS_DUNNO:
         emsg = N_("  Unspecified dotlock file control process error.\n"
               "  Like broken I/O pipe; this one is unlikely to happen\n");
         if (serrno != EAGAIN)
            serrno = EINVAL;
         break;
      case DLS_PING:
         if (!didmsg)
            _DOMSG();
         n_err(".");
         didmsg = TRUM1;
         continue;
      }

      if (emsg != NULL) {
         if (!didmsg) {
            _DOMSG();
            didmsg = TRUM1;
         }
         if (didmsg == TRUM1)
            n_err("\n");
         didmsg = TRU1;
         n_err(V_(emsg));
         emsg = NULL;
      }

      if (dls & DLS_ABANDON) {
         Pclose(rv, FAL0);
         rv = NULL;
         break;
      }
   }

jleave:
   if (didmsg == TRUM1)
      n_err("\n");
   if (rv == NULL) {
      if (flocked && serrno != EAGAIN && serrno != EEXIST &&
            ok_blook(dotlock_ignore_error))
         rv = (FILE*)-1;
      else
         errno = serrno;
   }
   NYD_LEAVE;
   return rv;
jemsg:
   if (!didmsg)
      _DOMSG();
   n_err("\n");
   didmsg = TRU1;
   n_err(V_(emsg));
   goto jleave;
#endif /* HAVE_DOTLOCK */
#undef _DOMSG
}

#ifdef HAVE_SOCKETS
FL int
sclose(struct sock *sp)
{
   int i;
   NYD_ENTER;

   i = sp->s_fd;
   sp->s_fd = -1;
   /* TODO NOTE: we MUST NOT close the descriptor 0 here...
    * TODO of course this should be handled in a VMAILFS->open() .s_fd=-1,
    * TODO but unfortunately it isn't yet */
   if (i <= 0)
      i = 0;
   else {
      if (sp->s_onclose != NULL)
         (*sp->s_onclose)();
      if (sp->s_wbuf != NULL)
         free(sp->s_wbuf);
# ifdef HAVE_OPENSSL
      if (sp->s_use_ssl) {
         void *s_ssl = sp->s_ssl;

         sp->s_ssl = NULL;
         sp->s_use_ssl = 0;
         while (!SSL_shutdown(s_ssl)) /* XXX proper error handling;signals! */
            ;
         SSL_free(s_ssl);
      }
# endif
      i = close(i);
   }
   NYD_LEAVE;
   return i;
}

FL enum okay
swrite(struct sock *sp, char const *data)
{
   enum okay rv;
   NYD2_ENTER;

   rv = swrite1(sp, data, strlen(data), 0);
   NYD2_LEAVE;
   return rv;
}

FL enum okay
swrite1(struct sock *sp, char const *data, int sz, int use_buffer)
{
   enum okay rv = STOP;
   int x;
   NYD2_ENTER;

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
            memcpy(sp->s_wbuf + sp->s_wbufpos, data, di);
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
         memcpy(sp->s_wbuf+ sp->s_wbufpos, data, sz);
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
      if (sp->s_use_ssl)
         ssl_gen_err("%s", o);
      else
# endif
         n_perr(o, 0);
      if (x < 0)
         sclose(sp);
      rv = STOP;
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD2_LEAVE;
   return rv;
}

# if defined HAVE_GETADDRINFO || defined HAVE_SSL
static sigjmp_buf __sopen_actjmp; /* TODO someday, we won't need it no more */
static int        __sopen_sig; /* TODO someday, we won't need it no more */
static void
__sopen_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __sopen_sig = sig;
   siglongjmp(__sopen_actjmp, 1);
}
# endif

FL bool_t
sopen(struct sock *sp, struct url *urlp) /* TODO sighandling; refactor */
{
# ifdef HAVE_SO_SNDTIMEO
   struct timeval tv;
# endif
# ifdef HAVE_SO_LINGER
   struct linger li;
# endif
# ifdef HAVE_GETADDRINFO
   char hbuf[NI_MAXHOST];
   struct addrinfo hints, *res0, *res;
   int volatile errval = 0 /* pacify CC */;
# else
   struct sockaddr_in servaddr;
   struct in_addr **pptr;
   struct hostent *hp;
   struct servent *ep;
# endif
# if defined HAVE_GETADDRINFO || defined HAVE_SSL
   sighandler_type volatile ohup, oint;
   char const * volatile serv;
   int volatile sofd = -1;
# else
   char const *serv;
   int sofd = -1; /* TODO may leak without getaddrinfo(3) support (pre v15.0) */
# endif
   NYD_ENTER;

   /* Connect timeouts after 30 seconds XXX configurable */
# ifdef HAVE_SO_SNDTIMEO
   tv.tv_sec = 30;
   tv.tv_usec = 0;
# endif
   serv = (urlp->url_port != NULL) ? urlp->url_port : urlp->url_proto;

   if (options & OPT_VERB)
      n_err(_("Resolving host \"%s:%s\" ... "),
         urlp->url_host.s, serv);

# ifdef HAVE_GETADDRINFO
   res0 = NULL;

   hold_sigs();
   __sopen_sig = 0;
   ohup = safe_signal(SIGHUP, &__sopen_onsig);
   oint = safe_signal(SIGINT, &__sopen_onsig);
   if (sigsetjmp(__sopen_actjmp, 0)) {
      n_err("%s\n",
         (__sopen_sig == SIGHUP ? _("Hangup") : _("Interrupted")));
      if (sofd >= 0) {
         close(sofd);
         sofd = -1;
      }
      goto jjumped;
   }
   rele_sigs();

   for (;;) {
      memset(&hints, 0, sizeof hints);
      hints.ai_socktype = SOCK_STREAM;
      if ((errval = getaddrinfo(urlp->url_host.s, serv, &hints, &res0)) == 0)
         break;

      if (options & OPT_VERB)
         n_err(_("failed\n"));
      n_err(_("Lookup of \"%s:%s\" failed: %s\n"),
         urlp->url_host.s, serv, gai_strerror(errval));

      /* Error seems to depend on how "smart" the /etc/service code is: is it
       * "able" to state wether the service as such is NONAME or does it only
       * check for the given ai_socktype.. */
      if (errval == EAI_NONAME || errval == EAI_SERVICE) {
         if (serv == urlp->url_proto &&
               (serv = url_servbyname(urlp, NULL)) != NULL) {
            n_err(_("  Trying standard protocol port \"%s\"\n"
               "  If that succeeds consider including the port in the URL!\n"),
               serv);
            continue;
         }
         if (serv != urlp->url_port)
            n_err(_("  Including a port number in the URL may "
               "circumvent this problem\n"));
      }
      assert(sofd == -1);
      errval = 0;
      goto jjumped;
   }
   if (options & OPT_VERB)
      n_err(_("done\n"));

   for (res = res0; res != NULL && sofd < 0; res = res->ai_next) {
      if (options & OPT_VERB) {
         if (getnameinfo(res->ai_addr, res->ai_addrlen, hbuf, sizeof hbuf,
               NULL, 0, NI_NUMERICHOST))
            memcpy(hbuf, "unknown host", sizeof("unknown host"));
         n_err(_("%sConnecting to \"%s:%s\" ..."),
               (res == res0 ? "" : "\n"), hbuf, serv);
      }

      sofd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
      if (sofd >= 0) {
#  ifdef HAVE_SO_SNDTIMEO
         (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#  endif
         if (connect(sofd, res->ai_addr, res->ai_addrlen)) {
            errval = errno;
            close(sofd);
            sofd = -1;
         }
      }
   }

jjumped:
   if (res0 != NULL) {
      freeaddrinfo(res0);
      res0 = NULL;
   }

   hold_sigs();
   safe_signal(SIGINT, oint);
   safe_signal(SIGHUP, ohup);
   rele_sigs();

   if (sofd < 0) {
      if (errval != 0) {
         errno = errval;
         n_perr(_("Could not connect"), 0);
      }
      goto jleave;
   }

# else /* HAVE_GETADDRINFO */
   if (serv == urlp->url_proto) {
      if ((ep = getservbyname(UNCONST(serv), "tcp")) != NULL)
         urlp->url_portno = ep->s_port;
      else {
         if (options & OPT_VERB)
            n_err(_("failed\n"));
         if ((serv = url_servbyname(urlp, &urlp->url_portno)) != NULL)
            n_err(_("  Unknown service: \"%s\"\n"
               "  Trying standard protocol port \"%s\"\n"
               "  If that succeeds consider including the port in the URL!\n"),
               urlp->url_proto, serv);
         else {
            n_err(_("  Unknown service: \"%s\"\n"
               "  Including a port in the URL may circumvent this problem\n"),
               urlp->url_proto);
            goto jleave;
         }
      }
   }

   if ((hp = gethostbyname(urlp->url_host.s)) == NULL) {
      char const *emsg;

      if (options & OPT_VERB)
         n_err(_("failed\n"));
      switch (h_errno) {
      case HOST_NOT_FOUND: emsg = N_("host not found"); break;
      default:
      case TRY_AGAIN:      emsg = N_("(maybe) try again later"); break;
      case NO_RECOVERY:    emsg = N_("non-recoverable server error"); break;
      case NO_DATA:        emsg = N_("valid name without IP address"); break;
      }
      n_err(_("Lookup of \"%s:%s\" failed: %s\n"),
         urlp->url_host.s, serv, V_(emsg));
      goto jleave;
   } else if (options & OPT_VERB)
      n_err(_("done\n"));

   pptr = (struct in_addr**)hp->h_addr_list;
   if ((sofd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      n_perr(_("could not create socket"), 0);
      goto jleave;
   }

   memset(&servaddr, 0, sizeof servaddr);
   servaddr.sin_family = AF_INET;
   servaddr.sin_port = htons(urlp->url_portno);
   memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
   if (options & OPT_VERB)
      n_err(_("%sConnecting to \"%s:%d\" ... "),
         "", inet_ntoa(**pptr), (int)urlp->url_portno);
#  ifdef HAVE_SO_SNDTIMEO
   (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#  endif
   if (connect(sofd, (struct sockaddr*)&servaddr, sizeof servaddr)) {
      n_perr(_("could not connect"), 0);
      close(sofd);
      sofd = -1;
      goto jleave;
   }
# endif /* !HAVE_GETADDRINFO */

   if (options & OPT_VERB)
      n_err(_("connected.\n"));

   /* And the regular timeouts XXX configurable */
# ifdef HAVE_SO_SNDTIMEO
   tv.tv_sec = 42;
   tv.tv_usec = 0;
   (void)setsockopt(sofd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
   (void)setsockopt(sofd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
# endif
# ifdef HAVE_SO_LINGER
   li.l_onoff = 1;
   li.l_linger = 42;
   (void)setsockopt(sofd, SOL_SOCKET, SO_LINGER, &li, sizeof li);
# endif

   memset(sp, 0, sizeof *sp);
   sp->s_fd = sofd;

   /* SSL/TLS upgrade? */
# ifdef HAVE_SSL
   if (urlp->url_needs_tls) {
      hold_sigs();
      ohup = safe_signal(SIGHUP, &__sopen_onsig);
      oint = safe_signal(SIGINT, &__sopen_onsig);
      if (sigsetjmp(__sopen_actjmp, 0)) {
         n_err(_("%s during SSL/TLS handshake\n"),
            (__sopen_sig == SIGHUP ? _("Hangup") : _("Interrupted")));
         goto jsclose;
      }
      rele_sigs();

      if (ssl_open(urlp, sp) != OKAY) {
jsclose:
         sclose(sp);
         sofd = -1;
      }

      hold_sigs();
      safe_signal(SIGINT, oint);
      safe_signal(SIGHUP, ohup);
      rele_sigs();
   }
# endif /* HAVE_SSL */

jleave:
   /* May need to bounce the signal to the lex.c trampoline (or wherever) */
# if defined HAVE_GETADDRINFO || defined HAVE_SSL
   if (__sopen_sig != 0) {
      sigset_t cset;
      sigemptyset(&cset);
      sigaddset(&cset, __sopen_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NULL);
      n_raise(__sopen_sig);
   }
#endif
   NYD_LEAVE;
   return (sofd >= 0);
}

FL int
(sgetline)(char **line, size_t *linesize, size_t *linelen, struct sock *sp
   SMALLOC_DEBUG_ARGS)
{
   int rv;
   size_t lsize;
   char *lp_base, *lp;
   NYD2_ENTER;

   lsize = *linesize;
   lp_base = *line;
   lp = lp_base;

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
                  n_perr(o, 0);
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
   NYD2_LEAVE;
   return rv;
}
#endif /* HAVE_SOCKETS */

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
   int rv = 1;
   char **arglist = v, *cp;
   FILE *fi;
   NYD_ENTER;

   if ((cp = fexpand(*arglist, FEXP_LOCAL)) == NULL)
      goto jleave;
   if ((fi = Fopen(cp, "r")) == NULL) {
      n_perr(cp, 0);
      goto jleave;
   }

   if (temporary_localopts_store != NULL) {
      n_err(_("Before v15 you cannot `source' from within macros, sorry\n"));
      Fclose(fi);
      goto jleave;
   }
   if (_fio_stack_size >= NELEM(_fio_stack)) {
      n_err(_("Too many `source' recursions\n"));
      Fclose(fi);
      goto jleave;
   }

   _fio_stack[_fio_stack_size].s_file = _fio_input;
   _fio_stack[_fio_stack_size].s_cond = condstack_release();
   _fio_stack[_fio_stack_size].s_loading = !!(pstate & PS_LOADING);
   ++_fio_stack_size;
   pstate &= ~PS_LOADING;
   pstate |= PS_SOURCING;
   _fio_input = fi;
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
      n_err(_("`source' stack over-pop\n"));
      pstate &= ~PS_SOURCING;
      goto jleave;
   }

   Fclose(_fio_input);

   --_fio_stack_size;
   if (!condstack_take(_fio_stack[_fio_stack_size].s_cond))
      n_err(_("Unmatched \"if\"\n"));
   if (_fio_stack[_fio_stack_size].s_loading)
      pstate |= PS_LOADING;
   else
      pstate &= ~PS_LOADING;
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
