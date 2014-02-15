/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of pipes, child processes, temporary files, file enwrapping.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#define READ               0
#define WRITE              1

#ifndef O_CLOEXEC
# define _OUR_CLOEXEC
# define O_CLOEXEC         0
# define _SET_CLOEXEC(FD)  fcntl((FD), F_SETFD, FD_CLOEXEC)
#else
# define _SET_CLOEXEC(FD)
#endif

struct fp {
   FILE        *fp;
   struct fp   *link;
   char        *realfile;
   long        offset;
   int         omode;
   int         pipe;
   int         pid;
   enum {
      FP_RAW      = 0,
      FP_GZIP     = 1<<0,
      FP_BZIP2    = 1<<2,
      FP_IMAP     = 1<<3,
      FP_MAILDIR  = 1<<4,
      FP_MASK     = (1<<5) - 1,
      FP_READONLY = 1<<5
   }           compressed;
};

struct child {
   int         pid;
   char        done;
   char        free;
   int         status;
   struct child *link;
};

static struct fp     *fp_head;
static struct child  *_popen_child;

static int           scan_mode(char const *mode, int *omode);
static void          register_file(FILE *fp, int omode, int ispipe, int pid,
                        int compressed, char const *realfile, long offset);
static enum okay     _compress(struct fp *fpp);
static int           _decompress(int compression, int infd, int outfd);
static enum okay     unregister_file(FILE *fp);
static int           file_pid(FILE *fp);
static int           wait_command(int pid);
static struct child *findchild(int pid);
static void          delchild(struct child *cp);

static int
scan_mode(char const *mode, int *omode)
{
   static struct {
      char const  mode[4];
      int         omode;
   } const maps[] = {
      {"r", O_RDONLY},
      {"w", O_WRONLY | O_CREAT | O_TRUNC},
      {"wx", O_WRONLY | O_CREAT | O_EXCL},
      {"a", O_WRONLY | O_APPEND | O_CREAT},
      {"a+", O_RDWR | O_APPEND},
      {"r+", O_RDWR},
      {"w+", O_RDWR | O_CREAT | O_EXCL}
   };

   int i;
   NYD_ENTER;

   for (i = 0; UICMP(z, i, <, NELEM(maps)); ++i)
      if (!strcmp(maps[i].mode, mode)) {
         *omode = maps[i].omode;
         i = 0;
         goto jleave;
      }

   fprintf(stderr, tr(152, "Internal error: bad stdio open mode %s\n"), mode);
   errno = EINVAL;
   *omode = 0; /* (silence CC) */
   i = -1;
jleave:
   NYD_LEAVE;
   return i;
}

static void
register_file(FILE *fp, int omode, int ispipe, int pid, int compressed,
   char const *realfile, long offset)
{
   struct fp *fpp;
   NYD_ENTER;

   fpp = smalloc(sizeof *fpp);
   fpp->fp = fp;
   fpp->omode = omode;
   fpp->pipe = ispipe;
   fpp->pid = pid;
   fpp->link = fp_head;
   fpp->compressed = compressed;
   fpp->realfile = realfile ? sstrdup(realfile) : NULL;
   fpp->offset = offset;
   fp_head = fpp;
   NYD_LEAVE;
}

static enum okay
_compress(struct fp *fpp)
{
   char const *cmd[2];
   int outfd;
   enum okay rv;
   NYD_ENTER;

   if (fpp->omode == O_RDONLY) {
      rv = OKAY;
      goto jleave;
   }
   rv = STOP;

   fflush(fpp->fp);
   clearerr(fpp->fp);
   if (fseek(fpp->fp, fpp->offset, SEEK_SET) < 0)
      goto jleave;

#ifdef HAVE_IMAP
   if ((fpp->compressed & FP_MASK) == FP_IMAP) {
      rv = imap_append(fpp->realfile, fpp->fp);
      goto jleave;
   }
#endif
   if ((fpp->compressed & FP_MASK) == FP_MAILDIR) {
      rv = maildir_append(fpp->realfile, fpp->fp);
      goto jleave;
   }

   outfd = open(fpp->realfile, (fpp->omode | O_CREAT) & ~O_EXCL, 0666);
   if (outfd < 0) {
      fprintf(stderr, "Fatal: cannot create ");
      perror(fpp->realfile);
      goto jleave;
   }
   if ((fpp->omode & O_APPEND) == 0)
      ftruncate(outfd, 0);
   switch (fpp->compressed & FP_MASK) {
   case FP_GZIP:
      cmd[0] = "gzip";  cmd[1] = "-c"; break;
   case FP_BZIP2:
      cmd[0] = "bzip2"; cmd[1] = "-c"; break;
   default:
      cmd[0] = "cat";   cmd[1] = NULL; break;
   }
   if (run_command(cmd[0], 0, fileno(fpp->fp), outfd, cmd[1], NULL, NULL) >= 0)
      rv = OKAY;
   close(outfd);
jleave:
   NYD_LEAVE;
   return rv;
}

static int
_decompress(int compression, int infd, int outfd)
{
   char const *cmd[2];
   int rv;
   NYD_ENTER;

   switch (compression & FP_MASK) {
   case FP_GZIP:     cmd[0] = "gzip";  cmd[1] = "-cd"; break;
   case FP_BZIP2:    cmd[0] = "bzip2"; cmd[1] = "-cd"; break;
   default:          cmd[0] = "cat";   cmd[1] = NULL;  break;
   case FP_IMAP:
   case FP_MAILDIR:
      rv = 0;
      goto jleave;
   }
   rv = run_command(cmd[0], 0, infd, outfd, cmd[1], NULL, NULL);
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
unregister_file(FILE *fp)
{
   struct fp **pp, *p;
   enum okay rv = OKAY;
   NYD_ENTER;

   for (pp = &fp_head; (p = *pp) != NULL; pp = &p->link)
      if (p->fp == fp) {
         if ((p->compressed & FP_MASK) != FP_RAW) /* TODO ;} */
            rv = _compress(p);
         *pp = p->link;
         free(p);
         goto jleave;
      }
   rv = STOP;
   panic(tr(153, "Invalid file pointer"));
jleave:
   NYD_LEAVE;
   return rv;
}

static int
file_pid(FILE *fp)
{
   int rv;
   struct fp *p;
   NYD_ENTER;

   rv = -1;
   for (p = fp_head; p; p = p->link)
      if (p->fp == fp) {
         rv = p->pid;
         break;
      }
   NYD_LEAVE;
   return rv;
}

static int
wait_command(int pid)
{
   int rv = 0;
   NYD_ENTER;

   if (!wait_child(pid, NULL)) {
      if (ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         fprintf(stderr, tr(154, "Fatal error in process.\n"));
      rv = -1;
   }
   NYD_LEAVE;
   return rv;
}

static struct child *
findchild(int pid)
{
   struct child **cpp;
   NYD_ENTER;

   for (cpp = &_popen_child; *cpp != NULL && (*cpp)->pid != pid;
         cpp = &(*cpp)->link)
      ;
   if (*cpp == NULL) {
      *cpp = smalloc(sizeof **cpp);
      (*cpp)->pid = pid;
      (*cpp)->done = (*cpp)->free = 0;
      (*cpp)->link = NULL;
   }
   NYD_LEAVE;
   return *cpp;
}

static void
delchild(struct child *cp)
{
   struct child **cpp;
   NYD_ENTER;

   for (cpp = &_popen_child; *cpp != cp; cpp = &(*cpp)->link)
      ;
   *cpp = cp->link;
   free(cp);
   NYD_LEAVE;
}

FL FILE *
safe_fopen(char const *file, char const *oflags, int *xflags)
{
   int osflags, fd;
   FILE *fp = NULL;
   NYD_ENTER;

   if (scan_mode(oflags, &osflags) < 0)
      goto jleave;
   osflags |= O_CLOEXEC;
   if (xflags != NULL)
      *xflags = osflags;

   if ((fd = open(file, osflags, 0666)) < 0)
      goto jleave;
   _SET_CLOEXEC(fd);

   fp = fdopen(fd, oflags);
jleave:
   NYD_LEAVE;
   return fp;
}

FL FILE *
Fopen(char const *file, char const *oflags)
{
   FILE *fp;
   int osflags;
   NYD_ENTER;

   if ((fp = safe_fopen(file, oflags, &osflags)) != NULL)
      register_file(fp, osflags, 0, 0, FP_RAW, NULL, 0L);
   NYD_LEAVE;
   return fp;
}

FL FILE *
Fdopen(int fd, char const *oflags)
{
   FILE *fp;
   int osflags;
   NYD_ENTER;

   scan_mode(oflags, &osflags);
   osflags |= O_CLOEXEC;

   if ((fp = fdopen(fd, oflags)) != NULL)
      register_file(fp, osflags, 0, 0, FP_RAW, NULL, 0L);
   NYD_LEAVE;
   return fp;
}

FL int
Fclose(FILE *fp)
{
   int i = 0;
   NYD_ENTER;

   if (unregister_file(fp) == OKAY)
      i |= 1;
   if (fclose(fp) == 0)
      i |= 2;
   NYD_LEAVE;
   return i == 3 ? 0 : EOF;
}

FL FILE *
Zopen(char const *file, char const *oflags, int *compression) /* FIXME MESS! */
{
   FILE *rv = NULL;
   int _compression, osflags, mode, infd;
   enum oflags rof;
   long offset;
   enum protocol p;
   NYD_ENTER;

   if (compression == NULL)
      compression = &_compression;

   if (scan_mode(oflags, &osflags) < 0)
      goto jleave;
   rof = OF_RDWR | OF_UNLINK;
   if (osflags & O_APPEND)
      rof |= OF_APPEND;
   if (osflags == O_RDONLY) {
      mode = R_OK;
      *compression = FP_READONLY;
   } else {
      mode = R_OK | W_OK;
      *compression = 0;
   }

   /* TODO ???? */
   if ((osflags & O_APPEND) && ((p = which_protocol(file)) == PROTO_IMAP ||
         p == PROTO_MAILDIR)) {
      *compression |= (p == PROTO_IMAP) ? FP_IMAP : FP_MAILDIR;
      osflags = O_RDWR | O_APPEND | O_CREAT;
      infd = -1;
   } else {
      char const *ext;

      if ((ext = strrchr(file, '.')) != NULL) {
         if (!strcmp(ext, ".gz"))
            *compression |= FP_GZIP;
         else if (!strcmp(ext, ".bz2"))
            *compression |= FP_BZIP2;
         else
            goto jraw;
      } else {
jraw:
         *compression |= FP_RAW;
         rv = Fopen(file, oflags);
         goto jleave;
      }
      if ((infd = open(file, (mode & W_OK) ? O_RDWR : O_RDONLY)) == -1 &&
            (!(osflags & O_CREAT) || errno != ENOENT))
         goto jleave;
   }

   if ((rv = Ftmp(NULL, "zopen", rof, 0600)) == NULL) {
      perror(tr(167, "tmpfile"));
      goto jerr;
   }
   if (infd >= 0 || (*compression & FP_MASK) == FP_IMAP ||
         (*compression & FP_MASK) == FP_MAILDIR) {
      if (_decompress(*compression, infd, fileno(rv)) < 0) {
jerr:
         if (rv != NULL)
            Fclose(rv);
         rv = NULL;
         if (infd >= 0)
            close(infd);
         goto jleave;
      }
   } else {
      if ((infd = creat(file, 0666)) == -1) {
         Fclose(rv);
         rv = NULL;
         goto jleave;
      }
   }
   if (infd >= 0)
      close(infd);
   fflush(rv);

   if (!(osflags & O_APPEND))
      rewind(rv);
   if ((offset = ftell(rv)) == -1) {
      Fclose(rv);
      rv = NULL;
      goto jleave;
   }
   register_file(rv, osflags, 0, 0, *compression, file, offset);
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
Ftmp(char **fn, char const *prefix, enum oflags oflags, int mode)
{
   FILE *fp = NULL;
   char *cp_base, *cp;
   int fd;
   NYD_ENTER;

   cp_base =
   cp = smalloc(strlen(tempdir) + 1 + sizeof("mail") + strlen(prefix) + 7 +1);
   cp = sstpcpy(cp, tempdir);
   *cp++ = '/';
   cp = sstpcpy(cp, "mail");
   if (*prefix) {
      *cp++ = '-';
      cp = sstpcpy(cp, prefix);
   }
   /* TODO Ftmp(): unroll our own creation loop with atoi(random()) */
   sstpcpy(cp, ".XXXXXX");

   hold_all_sigs();
#ifdef HAVE_MKSTEMP
   if ((fd = mkstemp(cp_base)) == -1)
      goto jfree;
   if (mode != (S_IRUSR | S_IWUSR) && fchmod(fd, mode) == -1)
      goto jclose;
   if (oflags & OF_APPEND) {
      int f;

      if ((f = fcntl(fd, F_GETFL)) == -1 ||
            fcntl(fd, F_SETFL, f | O_APPEND) == -1) {
jclose:
         close(fd);
         goto junlink;
      }
   }
   if (!(oflags & OF_REGISTER))
      fcntl(fd, F_SETFD, FD_CLOEXEC);
#else
   if (mktemp(cp_base) == NULL)
      goto jfree;
   if ((fd = open(cp_base, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC |
         (oflags & OF_APPEND ? O_APPEND : 0), mode)) < 0)
      goto junlink;
#endif

   fp = (*((oflags & OF_REGISTER) ? &Fdopen : &fdopen))(fd,
         (oflags & OF_RDWR ? "w+" : "w"));
   if (fp == NULL || (oflags & OF_UNLINK)) {
junlink:
      unlink(cp_base);
      goto jfree;
   }

   if (fn != NULL)
      *fn = cp_base;
jleave:
   if (fp == NULL || !(oflags & OF_HOLDSIGS))
      rele_all_sigs();
   NYD_LEAVE;
   return fp;
jfree:
   if ((cp = cp_base) != NULL)
      free(cp);
   goto jleave;
}

FL void
Ftmp_release(char **fn)
{
   char *cp;
   NYD_ENTER;

   cp = *fn;
   *fn = NULL;
   if (cp != NULL) {
      unlink(cp);
      rele_all_sigs();
      free(cp);
   }
   NYD_LEAVE;
}

FL void
Ftmp_free(char **fn)
{
   char *cp;
   NYD_ENTER;

   cp = *fn;
   *fn = NULL;
   if (cp != NULL)
      free(cp);
   NYD_LEAVE;
}

FL bool_t
pipe_cloexec(int fd[2])
{
   bool_t rv = FAL0;
   NYD_ENTER;

   if (pipe(fd) < 0)
      goto jleave;
   fcntl(fd[0], F_SETFD, FD_CLOEXEC);
   fcntl(fd[1], F_SETFD, FD_CLOEXEC);
   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
Popen(char const *cmd, char const *mode, char const *sh, int newfd1)
{
   int p[2], myside, hisside, fd0, fd1, pid;
   char mod[2] = { '0', '\0' };
   sigset_t nset;
   FILE *rv = NULL;
   NYD_ENTER;

   if (!pipe_cloexec(p))
      goto jleave;

   if (*mode == 'r') {
      myside = p[READ];
      fd0 = -1;
      hisside = fd1 = p[WRITE];
      mod[0] = *mode;
   } else if (*mode == 'W') {
      myside = p[WRITE];
      hisside = fd0 = p[READ];
      fd1 = newfd1;
      mod[0] = 'w';
   } else {
      myside = p[WRITE];
      hisside = fd0 = p[READ];
      fd1 = -1;
      mod[0] = 'w';
   }
   sigemptyset(&nset);
   if (sh == NULL) {
      pid = start_command(cmd, &nset, fd0, fd1, NULL, NULL, NULL);
   } else {
      pid = start_command(sh, &nset, fd0, fd1, "-c", cmd, NULL);
   }
   if (pid < 0) {
      close(p[READ]);
      close(p[WRITE]);
      goto jleave;
   }
   close(hisside);
   if ((rv = fdopen(myside, mod)) != NULL)
      register_file(rv, 0, 1, pid, FP_RAW, NULL, 0L);
jleave:
   NYD_LEAVE;
   return rv;
}

FL bool_t
Pclose(FILE *ptr, bool_t dowait)
{
   sigset_t nset, oset;
   int pid;
   bool_t rv = FAL0;
   NYD_ENTER;

   pid = file_pid(ptr);
   if (pid < 0)
      goto jleave;
   unregister_file(ptr);
   fclose(ptr);
   if (dowait) {
      sigemptyset(&nset);
      sigaddset(&nset, SIGINT);
      sigaddset(&nset, SIGHUP);
      sigprocmask(SIG_BLOCK, &nset, &oset);
      rv = wait_child(pid, NULL);
      sigprocmask(SIG_SETMASK, &oset, NULL);
   } else {
      free_child(pid);
      rv = TRU1;
   }
jleave:
   NYD_LEAVE;
   return rv;
}

FL void
close_all_files(void)
{
   NYD_ENTER;
   while (fp_head != NULL)
      if (fp_head->pipe)
         Pclose(fp_head->fp, TRU1);
      else
         Fclose(fp_head->fp);
   NYD_LEAVE;
}

FL int
run_command(char const *cmd, sigset_t *mask, int infd, int outfd,
   char const *a0, char const *a1, char const *a2)
{
   int rv;
   NYD_ENTER;

   if ((rv = start_command(cmd, mask, infd, outfd, a0, a1, a2)) < 0)
      rv = -1;
   else
      rv = wait_command(rv);
   NYD_LEAVE;
   return rv;
}

FL int
start_command(char const *cmd, sigset_t *mask, int infd, int outfd,
   char const *a0, char const *a1, char const *a2)
{
   int rv;
   NYD_ENTER;

   if ((rv = fork()) < 0) {
      perror("fork");
      rv = -1;
   } else if (rv == 0) {
      char *argv[100];
      int i = getrawlist(cmd, strlen(cmd), argv, NELEM(argv), 0);

      if ((argv[i++] = UNCONST(a0)) != NULL &&
          (argv[i++] = UNCONST(a1)) != NULL &&
          (argv[i++] = UNCONST(a2)) != NULL)
         argv[i] = NULL;
      prepare_child(mask, infd, outfd);
      execvp(argv[0], argv);
      perror(argv[0]);
      _exit(1);
   }
   NYD_LEAVE;
   return rv;
}

FL void
prepare_child(sigset_t *nset, int infd, int outfd)
{
   int i;
   sigset_t fset;
   NYD_ENTER;

   /* All file descriptors other than 0, 1, and 2 are supposed to be cloexec */
   if (infd >= 0)
      dup2(infd, 0);
   if (outfd >= 0)
      dup2(outfd, 1);

   if (nset) {
      for (i = 1; i < NSIG; ++i)
         if (sigismember(nset, i))
            safe_signal(i, SIG_IGN);
      if (!sigismember(nset, SIGINT))
         safe_signal(SIGINT, SIG_DFL);
   }

   sigemptyset(&fset);
   sigprocmask(SIG_SETMASK, &fset, NULL);
   NYD_LEAVE;
}

FL void
sigchild(int signo)
{
   int pid, status;
   struct child *cp;
   NYD_X; /* Signal handler */
   UNUSED(signo);

jagain:
   while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      cp = findchild(pid);
      if (cp->free)
         delchild(cp);
      else {
         cp->done = 1;
         cp->status = status;
      }
   }
   if (pid == -1 && errno == EINTR)
      goto jagain;
}

FL void
free_child(int pid)
{
   sigset_t nset, oset;
   struct child *cp;
   NYD_ENTER;

   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   cp = findchild(pid);
   if (cp->done)
      delchild(cp);
   else
      cp->free = 1;

   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD_LEAVE;
}

FL bool_t
wait_child(int pid, int *wait_status)
{
   sigset_t nset, oset;
   struct child *cp;
   int ws;
   bool_t rv;
   NYD_ENTER;

   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   cp = findchild(pid);
   while (!cp->done)
      sigsuspend(&oset);
   ws = cp->status;
   delchild(cp);

   sigprocmask(SIG_SETMASK, &oset, NULL);

   if (wait_status != NULL)
      *wait_status = ws;
   rv = (WIFEXITED(ws) && WEXITSTATUS(ws) == 0);
   NYD_LEAVE;
   return rv;
}

#ifdef _OUR_CLOEXEC
# undef O_CLOEXEC
# undef _OUR_CLOEXEC
#endif
#undef _SET_CLOEXEC

/* vim:set fenc=utf-8:s-it-mode */
