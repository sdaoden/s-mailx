/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of pipes, child processes, temporary files, file enwrapping.
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
#define n_FILE popen

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/wait.h>

#define READ               0
#define WRITE              1

struct fp {
   FILE        *fp;
   struct fp   *link;
   char        *realfile;
   char        *save_cmd;
   long        offset;
   int         omode;
   int         pipe;
   int         pid;
   enum {
      FP_RAW      = 0,
      FP_GZIP     = 1<<0,
      FP_XZ       = 1<<1,
      FP_BZIP2    = 1<<2,
      FP_IMAP     = 1<<3,
      FP_MAILDIR  = 1<<4,
      FP_HOOK     = 1<<5,
      FP_MASK     = (1<<6) - 1
   }           flags;
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
                        int flags, char const *realfile, long offset,
                        char const *save_cmd);
static enum okay     _file_save(struct fp *fpp);
static int           _file_load(int flags, int infd, int outfd,
                        char const *load_cmd);
static enum okay     unregister_file(FILE *fp);
static int           file_pid(FILE *fp);

/* Handle SIGCHLD */
static void          _sigchld(int signo);

static int           wait_command(int pid);
static struct child *_findchild(int pid, bool_t create);
static void          _delchild(struct child *cp);

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
   NYD2_ENTER;

   for (i = 0; UICMP(z, i, <, NELEM(maps)); ++i)
      if (!strcmp(maps[i].mode, mode)) {
         *omode = maps[i].omode;
         i = 0;
         goto jleave;
      }

   n_alert(_("Internal error: bad stdio open mode %s"), mode);
   errno = EINVAL;
   *omode = 0; /* (silence CC) */
   i = -1;
jleave:
   NYD2_LEAVE;
   return i;
}

static void
register_file(FILE *fp, int omode, int ispipe, int pid, int flags,
   char const *realfile, long offset, char const *save_cmd)
{
   struct fp *fpp;
   NYD_ENTER;

   fpp = smalloc(sizeof *fpp);
   fpp->fp = fp;
   fpp->omode = omode;
   fpp->pipe = ispipe;
   fpp->pid = pid;
   fpp->link = fp_head;
   fpp->flags = flags;
   fpp->realfile = (realfile != NULL) ? sstrdup(realfile) : NULL;
   fpp->save_cmd = (save_cmd != NULL) ? sstrdup(save_cmd) : NULL;
   fpp->offset = offset;
   fp_head = fpp;
   NYD_LEAVE;
}

static enum okay
_file_save(struct fp *fpp)
{
   char const *cmd[3];
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
   if (fseek(fpp->fp, fpp->offset, SEEK_SET) == -1)
      goto jleave;

#ifdef HAVE_IMAP
   if ((fpp->flags & FP_MASK) == FP_IMAP) {
      rv = imap_append(fpp->realfile, fpp->fp);
      goto jleave;
   }
#endif
   if ((fpp->flags & FP_MASK) == FP_MAILDIR) {
      rv = maildir_append(fpp->realfile, fpp->fp);
      goto jleave;
   }

   outfd = open(fpp->realfile, (fpp->omode | O_CREAT) & ~O_EXCL, 0666);
   if (outfd == -1) {
      n_err(_("Fatal: cannot create \"%s\": %s\n"),
         fpp->realfile, strerror(errno));
      goto jleave;
   }
   if (!(fpp->omode & O_APPEND))
      ftruncate(outfd, 0);

   cmd[2] = NULL;
   switch (fpp->flags & FP_MASK) {
   case FP_GZIP:
      cmd[0] = "gzip";  cmd[1] = "-c"; break;
   case FP_BZIP2:
      cmd[0] = "bzip2"; cmd[1] = "-c"; break;
   case FP_XZ:
      cmd[0] = "xz";    cmd[1] = "-c"; break;
   default:
      cmd[0] = "cat";   cmd[1] = NULL; break;
   case FP_HOOK:
      if ((cmd[0] = ok_vlook(SHELL)) == NULL)
         cmd[0] = XSHELL;
      cmd[1] = "-c";
      cmd[2] = fpp->save_cmd;
   }
   if (run_command(cmd[0], 0, fileno(fpp->fp), outfd, cmd[1], cmd[2], NULL)
         >= 0)
      rv = OKAY;
   close(outfd);
jleave:
   NYD_LEAVE;
   return rv;
}

static int
_file_load(int flags, int infd, int outfd, char const *load_cmd)
{
   char const *cmd[3];
   int rv;
   NYD_ENTER;

   cmd[2] = NULL;
   switch (flags & FP_MASK) {
   case FP_GZIP:     cmd[0] = "gzip";  cmd[1] = "-cd"; break;
   case FP_BZIP2:    cmd[0] = "bzip2"; cmd[1] = "-cd"; break;
   case FP_XZ:       cmd[0] = "xz";    cmd[1] = "-cd"; break;
   default:          cmd[0] = "cat";   cmd[1] = NULL;  break;
   case FP_HOOK:
      if ((cmd[0] = ok_vlook(SHELL)) == NULL)
         cmd[0] = XSHELL;
      cmd[1] = "-c";
      cmd[2] = load_cmd;
      break;
   case FP_MAILDIR:
   case FP_IMAP:
      rv = 0;
      goto jleave;
   }

   rv = run_command(cmd[0], 0, infd, outfd, cmd[1], cmd[2], NULL);
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
         if ((p->flags & FP_MASK) != FP_RAW) /* TODO ;} */
            rv = _file_save(p);
         *pp = p->link;
         if (p->save_cmd != NULL)
            free(p->save_cmd);
         if (p->realfile != NULL)
            free(p->realfile);
         free(p);
         goto jleave;
      }
   DBGOR(n_panic, n_alert)(_("Invalid file pointer"));
   rv = STOP;
jleave:
   NYD_LEAVE;
   return rv;
}

static int
file_pid(FILE *fp)
{
   int rv;
   struct fp *p;
   NYD2_ENTER;

   rv = -1;
   for (p = fp_head; p; p = p->link)
      if (p->fp == fp) {
         rv = p->pid;
         break;
      }
   NYD2_LEAVE;
   return rv;
}

static void
_sigchld(int signo)
{
   pid_t pid;
   int status;
   struct child *cp;
   NYD_X; /* Signal handler */
   UNUSED(signo);

   for (;;) {
      pid = waitpid(-1, &status, WNOHANG);
      if (pid <= 0) {
         if (pid == -1 && errno == EINTR)
            continue;
         break;
      }

      if ((cp = _findchild(pid, FAL0)) != NULL) {
         if (cp->free)
            cp->pid = -1; /* XXX Was _delchild(cp);# */
         else {
            cp->done = 1;
            cp->status = status;
         }
      }
   }
}

static int
wait_command(int pid)
{
   int rv = 0;
   NYD_ENTER;

   if (!wait_child(pid, NULL)) {
      if (ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         n_err(_("Fatal error in process\n"));
      rv = -1;
   }
   NYD_LEAVE;
   return rv;
}

static struct child *
_findchild(int pid, bool_t create)
{
   struct child **cpp;
   NYD_ENTER;

   for (cpp = &_popen_child; *cpp != NULL && (*cpp)->pid != pid;
         cpp = &(*cpp)->link)
      ;

   if (*cpp == NULL && create) {
      *cpp = smalloc(sizeof **cpp);
      (*cpp)->pid = pid;
      (*cpp)->done = (*cpp)->free = 0;
      (*cpp)->link = NULL;
   }
   NYD_LEAVE;
   return *cpp;
}

static void
_delchild(struct child *cp)
{
   struct child **cpp;
   NYD_ENTER;

   cpp = &_popen_child;
   for (;;) {
      if (*cpp == cp) {
         *cpp = cp->link;
         free(cp);
         break;
      }
      if (*(cpp = &(*cpp)->link) == NULL) {
         DBG( n_err("! popen.c:_delchild(): implementation error\n"); )
         break;
      }
   }
   NYD_LEAVE;
}

FL void
command_manager_start(void)
{
   struct sigaction nact, oact;
   NYD_ENTER;

   nact.sa_handler = &_sigchld;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = 0
#ifdef SA_RESTART
         | SA_RESTART
#endif
#ifdef SA_NOCLDSTOP
         | SA_NOCLDSTOP
#endif
         ;
   if (sigaction(SIGCHLD, &nact, &oact) != 0)
      n_panic(_("Cannot install signal handler for child process management"));
   NYD_LEAVE;
}

FL FILE *
safe_fopen(char const *file, char const *oflags, int *xflags)
{
   int osflags, fd;
   FILE *fp = NULL;
   NYD2_ENTER; /* (only for Fopen() and once in lex.c) */

   if (scan_mode(oflags, &osflags) < 0)
      goto jleave;
   osflags |= _O_CLOEXEC;
   if (xflags != NULL)
      *xflags = osflags;

   if ((fd = open(file, osflags, 0666)) == -1)
      goto jleave;
   _CLOEXEC_SET(fd);

   fp = fdopen(fd, oflags);
jleave:
   NYD2_LEAVE;
   return fp;
}

FL FILE *
Fopen(char const *file, char const *oflags)
{
   FILE *fp;
   int osflags;
   NYD_ENTER;

   if ((fp = safe_fopen(file, oflags, &osflags)) != NULL)
      register_file(fp, osflags, 0, 0, FP_RAW, NULL, 0L, NULL);
   NYD_LEAVE;
   return fp;
}

FL FILE *
Fdopen(int fd, char const *oflags, bool_t nocloexec)
{
   FILE *fp;
   int osflags;
   NYD_ENTER;

   scan_mode(oflags, &osflags);
   if (!nocloexec)
      osflags |= _O_CLOEXEC; /* Ensured to be set by caller as documented! */

   if ((fp = fdopen(fd, oflags)) != NULL)
      register_file(fp, osflags, 0, 0, FP_RAW, NULL, 0L, NULL);
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
   return (i == 3 ? 0 : EOF);
}

FL FILE *
Zopen(char const *file, char const *oflags) /* FIXME MESS! */
{
   FILE *rv = NULL;
   char const *cload = NULL, *csave = NULL;
   int flags, osflags, mode, infd;
   enum oflags rof;
   long offset;
   enum protocol p;
   NYD_ENTER;

   if (scan_mode(oflags, &osflags) < 0)
      goto jleave;

   flags = 0;
   rof = OF_RDWR | OF_UNLINK;
   if (osflags & O_APPEND)
      rof |= OF_APPEND;
   mode = (osflags == O_RDONLY) ? R_OK : R_OK | W_OK;

   if ((osflags & O_APPEND) && ((p = which_protocol(file)) == PROTO_IMAP ||
         p == PROTO_MAILDIR)) {
      flags |= (p == PROTO_IMAP) ? FP_IMAP : FP_MAILDIR;
      osflags = O_RDWR | O_APPEND | O_CREAT;
      infd = -1;
   } else {
      char const *ext;

      if ((ext = strrchr(file, '.')) != NULL) {
         if (!strcmp(ext, ".gz"))
            flags |= FP_GZIP;
         else if (!strcmp(ext, ".xz"))
            flags |= FP_XZ;
         else if (!strcmp(ext, ".bz2"))
            flags |= FP_BZIP2;
         else {
#undef _X1
#define _X1 "file-hook-load-"
#undef _X2
#define _X2 "file-hook-save-"
            size_t l = strlen(++ext);
            char *vbuf = ac_alloc(l + MAX(sizeof(_X1), sizeof(_X2)));

            memcpy(vbuf, _X1, sizeof(_X1) -1);
            memcpy(vbuf + sizeof(_X1) -1, ext, l);
            vbuf[sizeof(_X1) -1 + l] = '\0';
            cload = vok_vlook(vbuf);
            memcpy(vbuf, _X2, sizeof(_X2) -1);
            memcpy(vbuf + sizeof(_X2) -1, ext, l);
            vbuf[sizeof(_X2) -1 + l] = '\0';
            csave = vok_vlook(vbuf);
#undef _X2
#undef _X1
            ac_free(vbuf);

            if ((csave != NULL) && (cload != NULL))
               flags |= FP_HOOK;
            else if ((csave != NULL) | (cload != NULL)) {
               n_alert(_("Only one of *mailbox-(load|save)-%s* is set!  "
                  "Treating as plain text!"), ext);
               goto jraw;
            } else
               goto jraw;
         }
      } else {
jraw:
         /*flags |= FP_RAW;*/
         rv = Fopen(file, oflags);
         goto jleave;
      }

      if ((infd = open(file, (mode & W_OK) ? O_RDWR : O_RDONLY)) == -1 &&
            (!(osflags & O_CREAT) || errno != ENOENT))
         goto jleave;
   }

   if ((rv = Ftmp(NULL, "zopen", rof, 0600)) == NULL) {
      n_perr(_("tmpfile"), 0);
      goto jerr;
   }

   if (flags & (FP_IMAP | FP_MAILDIR))
      ;
   else if (infd >= 0) {
      if (_file_load(flags, infd, fileno(rv), cload) < 0) {
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

   register_file(rv, osflags, 0, 0, flags, file, offset, csave);
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
#ifdef HAVE_MKOSTEMP
   fd = 0;
   /*if (!(oflags & OF_REGISTER))*/
      /* O_CLOEXEC note: <-> support check included in mk-conf.sh */
      fd |= O_CLOEXEC;
   if (oflags & OF_APPEND)
      fd |= O_APPEND;
   if ((fd = mkostemp(cp_base, fd)) == -1)
      goto jfree;

   if (mode != (S_IRUSR | S_IWUSR) && fchmod(fd, mode) == -1) {
      close(fd);
      goto junlink;
   }
#elif defined HAVE_MKSTEMP
   if ((fd = mkstemp(cp_base)) == -1)
      goto jfree;
   /*if (!(oflags & OF_REGISTER))*/
      (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
   if (oflags & OF_APPEND) { /* XXX include CLOEXEC here, drop above, then */
      int f;

      if ((f = fcntl(fd, F_GETFL)) == -1 ||
            fcntl(fd, F_SETFL, f | O_APPEND) == -1) {
jclose:
         close(fd);
         goto junlink;
      }
   }

   if (mode != (S_IRUSR | S_IWUSR) && fchmod(fd, mode) == -1)
      goto jclose;
#else
   if (mktemp(cp_base) == NULL)
      goto jfree;
   if ((fd = open(cp_base, O_CREAT | O_EXCL | O_RDWR | _O_CLOEXEC |
         (oflags & OF_APPEND ? O_APPEND : 0), mode)) == -1)
      goto junlink;
   /*if (!(oflags & OF_REGISTER))*/
      _CLOEXEC_SET(fd);
#endif

   if (oflags & OF_REGISTER)
      fp = Fdopen(fd, (oflags & OF_RDWR ? "w+" : "w"), FAL0);
   else
      fp = fdopen(fd, (oflags & OF_RDWR ? "w+" : "w"));
   if (fp == NULL || (oflags & OF_UNLINK)) {
junlink:
      unlink(cp_base);
      goto jfree;
   }

   if (fn != NULL)
      *fn = cp_base;
   else
      free(cp_base);
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

#ifdef HAVE_PIPE2
   if (pipe2(fd, O_CLOEXEC) == -1)
      goto jleave;
#else
   if (pipe(fd) == -1)
      goto jleave;
   (void)fcntl(fd[0], F_SETFD, FD_CLOEXEC);
   (void)fcntl(fd[1], F_SETFD, FD_CLOEXEC);
#endif
   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
Popen(char const *cmd, char const *mode, char const *sh,
   char const **env_addon, int newfd1)
{
   int p[2], myside, hisside, fd0, fd1, pid;
   char mod[2] = {'0', '\0'};
   sigset_t nset;
   FILE *rv = NULL;
   NYD_ENTER;

   /* First clean up child structures */
   {  sigset_t oset;
      struct child **cpp, *cp;

      sigfillset(&nset);
      sigprocmask(SIG_BLOCK, &nset, &oset);

      for (cpp = &_popen_child; *cpp != NULL;) {
         if ((*cpp)->pid == -1) {
            cp = *cpp;
            *cpp = cp->link;
            free(cp);
         } else
            cpp = &(*cpp)->link;
      }

      sigprocmask(SIG_SETMASK, &oset, NULL);
   }

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

   if (cmd == (char*)-1) {
      if ((pid = fork_child()) == -1)
         n_perr(_("fork"), 0);
      else if (pid == 0) {
         union {char const *ccp; int (*ptf)(void); int es;} u;
         prepare_child(&nset, fd0, fd1);
         close(p[READ]);
         close(p[WRITE]);
         u.ccp = sh;
         u.es = (*u.ptf)();
         _exit(u.es);
      }
   } else if (sh == NULL) {
      pid = start_command(cmd, &nset, fd0, fd1, NULL, NULL, NULL, env_addon);
   } else {
      pid = start_command(sh, &nset, fd0, fd1, "-c", cmd, NULL, env_addon);
   }
   if (pid < 0) {
      close(p[READ]);
      close(p[WRITE]);
      goto jleave;
   }
   close(hisside);
   if ((rv = fdopen(myside, mod)) != NULL)
      register_file(rv, 0, 1, pid, FP_RAW, NULL, 0L, NULL);
   else
      close(myside);
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
fork_child(void)
{
   struct child *cp;
   int pid;
   NYD_ENTER;

   cp = _findchild(0, TRU1);

   if ((cp->pid = pid = fork()) == -1) {
      _delchild(cp);
      n_perr(_("fork"), 0);
   }
   NYD_LEAVE;
   return pid;
}

FL int
run_command(char const *cmd, sigset_t *mask, int infd, int outfd,
   char const *a0, char const *a1, char const *a2)
{
   int rv;
   NYD_ENTER;

   if ((rv = start_command(cmd, mask, infd, outfd, a0, a1, a2, NULL)) < 0)
      rv = -1;
   else
      rv = wait_command(rv);
   NYD_LEAVE;
   return rv;
}

FL int
start_command(char const *cmd, sigset_t *mask, int infd, int outfd,
   char const *a0, char const *a1, char const *a2,
   char const **env_addon)
{
   int rv;
   NYD_ENTER;

   if ((rv = fork_child()) == -1) {
      n_perr(_("fork"), 0);
      rv = -1;
   } else if (rv == 0) {
      char *argv[128];
      int i;

      if (env_addon != NULL) { /* TODO env_addon; should have struct child */
         extern char **environ;
         size_t ei, ei_orig, ai, ai_orig;
         char **env;

         /* TODO note we don't check the POSIX limit:
          * the total space used to store the environment and the arguments to
          * the process is limited to {ARG_MAX} bytes */
         for (ei = 0; environ[ei] != NULL; ++ei)
            ;
         ei_orig = ei;
         for (ai = 0; env_addon[ai] != NULL; ++ai)
            ;
         ai_orig = ai;
         env = ac_alloc(sizeof(*env) * (ei + ai +1));
         memcpy(env, environ, sizeof(*env) * ei);

         /* Replace all those keys that yet exist */
         while (ai-- > 0) {
            char const *ee, *kvs;
            size_t kl;

            ee = env_addon[ai];
            kvs = strchr(ee, '=');
            assert(kvs != NULL);
            kl = PTR2SIZE(kvs - ee);
            assert(kl > 0);
            for (ei = ei_orig; ei-- > 0;) {
               char const *ekvs = strchr(env[ei], '=');
               if (ekvs != NULL && kl == PTR2SIZE(ekvs - env[ei]) &&
                     !memcmp(ee, env[ei], kl)) {
                  env[ei] = UNCONST(ee);
                  env_addon[ai] = NULL;
                  break;
               }
            }
         }

         /* And append the rest */
         for (ei = ei_orig, ai = ai_orig; ai-- > 0;)
            if (env_addon[ai] != NULL)
               env[ei++] = UNCONST(env_addon[ai]);

         env[ei] = NULL;
         environ = env;
      }

      i = getrawlist(cmd, strlen(cmd), argv, NELEM(argv), 0);

      if ((argv[i++] = UNCONST(a0)) != NULL &&
            (argv[i++] = UNCONST(a1)) != NULL &&
            (argv[i++] = UNCONST(a2)) != NULL)
         argv[i] = NULL;
      prepare_child(mask, infd, outfd);
      execvp(argv[0], argv);
      perror(argv[0]);
      _exit(EXIT_ERR);
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
      dup2(infd, STDIN_FILENO);
   if (outfd >= 0)
      dup2(outfd, STDOUT_FILENO);

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
free_child(int pid)
{
   sigset_t nset, oset;
   struct child *cp;
   NYD_ENTER;

   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   if ((cp = _findchild(pid, FAL0)) != NULL) {
      if (cp->done)
         _delchild(cp);
      else
         cp->free = 1;
   }

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

   cp = _findchild(pid, FAL0);
   if (cp != NULL) {
      while (!cp->done)
         sigsuspend(&oset);
      ws = cp->status;
      _delchild(cp);
   } else
      ws = 0;

   sigprocmask(SIG_SETMASK, &oset, NULL);

   if (wait_status != NULL)
      *wait_status = ws;
   rv = (WIFEXITED(ws) && WEXITSTATUS(ws) == 0);
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
