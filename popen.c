/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of pipes, child processes, temporary files, file enwrapping.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
   struct fp   *link;
   int         omode;
   int         pid;
   enum {
      FP_RAW      = 0,
      FP_GZIP     = 1<<0,
      FP_XZ       = 1<<1,
      FP_BZIP2    = 1<<2,
      FP_MAILDIR  = 1<<4,
      FP_HOOK     = 1<<5,
      FP_PIPE     = 1<<6,
      FP_MASK     = (1<<7) - 1,
      /* TODO FP_UNLINK: should be in a separated process so that unlinking
       * TODO the temporary "garbage" is "safe"(r than it is like that) */
      FP_UNLINK   = 1<<9,
      FP_TERMIOS  = 1<<10
   }           flags;
   long        offset;
   FILE        *fp;
   char        *realfile;
   char        *save_cmd;
   struct termios *fp_tios;
   n_sighdl_t fp_osigint;     /* Only if FP_TERMIOS */
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

/* TODO Rather temporary: deal with job control with FD_PASS */
static struct termios a_popen_tios;
static sighandler_type a_popen_otstp, a_popen_ottin, a_popen_ottou;
static volatile int a_popen_hadsig;

static int           scan_mode(char const *mode, int *omode);
static void          register_file(FILE *fp, int omode, int pid,
                        int flags, char const *realfile, long offset,
                        char const *save_cmd, struct termios *tiosp,
                        n_sighdl_t osigint);
static enum okay     _file_save(struct fp *fpp);
static int           _file_load(int flags, int infd, int outfd,
                        char const *load_cmd);
static enum okay     unregister_file(FILE *fp, struct termios **tiosp,
                        n_sighdl_t *osigint);
static int           file_pid(FILE *fp);

/* TODO Rather temporary: deal with job control with FD_PASS */
static void a_popen_jobsigs_up(void);
static void a_popen_jobsigs_down(void);
static void a_popen_jobsig(int sig);

/* Handle SIGCHLD */
static void a_popen_sigchld(int signo);

static struct child *a_popen_child_find(int pid, bool_t create);
static void a_popen_child_del(struct child *cp);

static int
scan_mode(char const *mode, int *omode)
{
   static struct {
      char const  mode[4];
      int         omode;
   } const maps[] = {
      {"r", O_RDONLY},
      {"w", O_WRONLY | O_CREAT | n_O_NOFOLLOW | O_TRUNC},
      {"wx", O_WRONLY | O_CREAT | O_EXCL},
      {"a", O_WRONLY | O_APPEND | O_CREAT | n_O_NOFOLLOW},
      {"a+", O_RDWR | O_APPEND},
      {"r+", O_RDWR},
      {"w+", O_RDWR | O_CREAT | O_EXCL}
   };

   int i;
   NYD2_ENTER;

   for (i = 0; UICMP(z, i, <, n_NELEM(maps)); ++i)
      if (!strcmp(maps[i].mode, mode)) {
         *omode = maps[i].omode;
         i = 0;
         goto jleave;
      }

   n_alert(_("Internal error: bad stdio open mode %s"), mode);
   n_err_no = n_ERR_INVAL;
   *omode = 0; /* (silence CC) */
   i = -1;
jleave:
   NYD2_LEAVE;
   return i;
}

static void
register_file(FILE *fp, int omode, int pid, int flags,
   char const *realfile, long offset, char const *save_cmd,
   struct termios *tiosp, n_sighdl_t osigint)
{
   struct fp *fpp;
   NYD_ENTER;

   assert(!(flags & FP_UNLINK) || realfile != NULL);
   assert(!(flags & FP_TERMIOS) || tiosp != NULL);

   fpp = smalloc(sizeof *fpp);
   fpp->fp = fp;
   fpp->omode = omode;
   fpp->pid = pid;
   fpp->link = fp_head;
   fpp->flags = flags;
   fpp->realfile = (realfile != NULL) ? sstrdup(realfile) : NULL;
   fpp->save_cmd = (save_cmd != NULL) ? sstrdup(save_cmd) : NULL;
   fpp->fp_tios = tiosp;
   fpp->fp_osigint = osigint;
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

   /* Ensure the I/O library doesn't optimize the fseek(3) away! */
   if(!n_real_seek(fpp->fp, fpp->offset, SEEK_SET)){
      outfd = n_err_no;
      n_err(_("Fatal: cannot restore file position and save %s: %s\n"),
         n_shexp_quote_cp(fpp->realfile, FAL0), n_err_to_doc(outfd));
      goto jleave;
   }

   if ((fpp->flags & FP_MASK) == FP_MAILDIR) {
      rv = maildir_append(fpp->realfile, fpp->fp, fpp->offset);
      goto jleave;
   }

   outfd = open(fpp->realfile,
         ((fpp->omode | O_CREAT | (fpp->omode & O_APPEND ? 0 : O_TRUNC) |
            n_O_NOFOLLOW) & ~O_EXCL), 0666);
   if (outfd == -1) {
      outfd = n_err_no;
      n_err(_("Fatal: cannot create %s: %s\n"),
         n_shexp_quote_cp(fpp->realfile, FAL0), n_err_to_doc(outfd));
      goto jleave;
   }

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
      cmd[0] = ok_vlook(SHELL);
      cmd[1] = "-c";
      cmd[2] = fpp->save_cmd;
   }
   if (n_child_run(cmd[0], 0, fileno(fpp->fp), outfd,
         cmd[1], cmd[2], NULL, NULL) >= 0)
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
      cmd[0] = ok_vlook(SHELL);
      cmd[1] = "-c";
      cmd[2] = load_cmd;
      break;
   case FP_MAILDIR:
      rv = 0;
      goto jleave;
   }

   rv = n_child_run(cmd[0], 0, infd, outfd, cmd[1], cmd[2], NULL, NULL);
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
unregister_file(FILE *fp, struct termios **tiosp, n_sighdl_t *osigint)
{
   struct fp **pp, *p;
   enum okay rv = OKAY;
   NYD_ENTER;

   if (tiosp)
      *tiosp = NULL;

   for (pp = &fp_head; (p = *pp) != NULL; pp = &p->link)
      if (p->fp == fp) {
         switch (p->flags & FP_MASK) {
         case FP_RAW:
         case FP_PIPE:
            break;
         default:
            rv = _file_save(p);
            break;
         }
         if ((p->flags & FP_UNLINK) && unlink(p->realfile))
            rv = STOP;

         *pp = p->link;
         if (p->save_cmd != NULL)
            free(p->save_cmd);
         if (p->realfile != NULL)
            free(p->realfile);
         if (p->flags & FP_TERMIOS) {
            if (tiosp != NULL) {
               *tiosp = p->fp_tios;
               *osigint = p->fp_osigint;
            } else
               free(p->fp_tios);
         }
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
a_popen_jobsigs_up(void){
   sigset_t nset, oset;
   NYD2_ENTER;

   sigfillset(&nset);

   sigprocmask(SIG_BLOCK, &nset, &oset);
   a_popen_otstp = safe_signal(SIGTSTP, &a_popen_jobsig);
   a_popen_ottin = safe_signal(SIGTTIN, &a_popen_jobsig);
   a_popen_ottou = safe_signal(SIGTTOU, &a_popen_jobsig);

   /* This assumes oset contains nothing but SIGCHLD, so to say */
   sigdelset(&oset, SIGTSTP);
   sigdelset(&oset, SIGTTIN);
   sigdelset(&oset, SIGTTOU);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD2_LEAVE;
}

static void
a_popen_jobsigs_down(void){
   sigset_t nset, oset;
   NYD2_ENTER;

   sigfillset(&nset);

   sigprocmask(SIG_BLOCK, &nset, &oset);
   safe_signal(SIGTSTP, a_popen_otstp);
   safe_signal(SIGTTIN, a_popen_ottin);
   safe_signal(SIGTTOU, a_popen_ottou);

   sigaddset(&oset, SIGTSTP);
   sigaddset(&oset, SIGTTIN);
   sigaddset(&oset, SIGTTOU);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD2_LEAVE;
}

static void
a_popen_jobsig(int sig){
   sighandler_type oldact;
   sigset_t nset;
   bool_t hadsig;
   NYD_X; /* Signal handler */

   hadsig = (a_popen_hadsig != 0);
   a_popen_hadsig = 1;
   if(!hadsig)
      n_TERMCAP_SUSPEND(TRU1);

   oldact = safe_signal(sig, SIG_DFL);

   sigemptyset(&nset);
   sigaddset(&nset, sig);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);
   n_raise(sig);
   sigprocmask(SIG_BLOCK, &nset, NULL);

   safe_signal(sig, oldact);
}

static void
a_popen_sigchld(int signo){
   pid_t pid;
   int status;
   struct child *cp;
   NYD_X; /* Signal handler */
   n_UNUSED(signo);

   for (;;) {
      pid = waitpid(-1, &status, WNOHANG);
      if (pid <= 0) {
         if (pid == -1 && n_err_no == n_ERR_INTR)
            continue;
         break;
      }

      if ((cp = a_popen_child_find(pid, FAL0)) != NULL) {
         if (cp->free)
            cp->pid = -1; /* XXX Was _delchild(cp);# */
         else {
            cp->done = 1;
            cp->status = status;
         }
      }
   }
}

static struct child *
a_popen_child_find(int pid, bool_t create){
   struct child **cpp, *cp;
   NYD2_ENTER;

   for(cpp = &_popen_child; (cp = *cpp) != NULL && cp->pid != pid;
         cpp = &(*cpp)->link)
      ;

   if(cp == NULL && create)
      (*cpp = cp = scalloc(1, sizeof *cp))->pid = pid;
   NYD2_LEAVE;
   return cp;
}

static void
a_popen_child_del(struct child *cp){
   struct child **cpp;
   NYD2_ENTER;

   cpp = &_popen_child;

   for(;;){
      if(*cpp == cp){
         *cpp = cp->link;
         free(cp);
         break;
      }
      if(*(cpp = &(*cpp)->link) == NULL){
         DBG( n_err("! a_popen_child_del(): implementation error\n"); )
         break;
      }
   }
   NYD2_LEAVE;
}

FL void
n_child_manager_start(void)
{
   struct sigaction nact, oact;
   NYD_ENTER;

   nact.sa_handler = &a_popen_sigchld;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = SA_RESTART
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
   NYD2_ENTER; /* (only for Fopen() and once in go.c) */

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
      register_file(fp, osflags, 0, FP_RAW, NULL, 0L, NULL, NULL,NULL);
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
      register_file(fp, osflags, 0, FP_RAW, NULL, 0L, NULL, NULL,NULL);
   NYD_LEAVE;
   return fp;
}

FL int
Fclose(FILE *fp)
{
   int i = 0;
   NYD_ENTER;

   if (unregister_file(fp, NULL, NULL) == OKAY)
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

   if ((osflags & O_APPEND) && ((p = which_protocol(file)) == PROTO_MAILDIR)) {
      flags |= FP_MAILDIR;
      osflags = O_RDWR | O_APPEND | O_CREAT | n_O_NOFOLLOW;
      infd = -1;
   } else {
      char const *ext;

      if ((ext = strrchr(file, '.')) != NULL) {
         if (!asccasecmp(ext, ".gz"))
            flags |= FP_GZIP;
         else if (!asccasecmp(ext, ".xz")) {
            flags |= FP_XZ;
            osflags &= ~O_APPEND;
            rof &= ~OF_APPEND;
         } else if (!asccasecmp(ext, ".bz2")) {
            flags |= FP_BZIP2;
            osflags &= ~O_APPEND;
            rof &= ~OF_APPEND;
         } else {
#undef _X1
#define _X1 "file-hook-load-"
#undef _X2
#define _X2 "file-hook-save-"
            size_t l = strlen(++ext);
            char *vbuf = ac_alloc(l + n_MAX(sizeof(_X1), sizeof(_X2)));

            memcpy(vbuf, _X1, sizeof(_X1) -1);
            memcpy(vbuf + sizeof(_X1) -1, ext, l);
            vbuf[sizeof(_X1) -1 + l] = '\0';
            cload = n_var_vlook(vbuf, FAL0);
            memcpy(vbuf, _X2, sizeof(_X2) -1);
            memcpy(vbuf + sizeof(_X2) -1, ext, l);
            vbuf[sizeof(_X2) -1 + l] = '\0';
            csave = n_var_vlook(vbuf, FAL0);
#undef _X2
#undef _X1
            ac_free(vbuf);

            if ((csave != NULL) && (cload != NULL)) {
               flags |= FP_HOOK;
               osflags &= ~O_APPEND;
               rof &= ~OF_APPEND;
            } else if ((csave != NULL) | (cload != NULL)) {
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
            (!(osflags & O_CREAT) || n_err_no != n_ERR_NOENT))
         goto jleave;
   }

   /* Note rv is not yet register_file()d, fclose() it in error path! */
   if ((rv = Ftmp(NULL, "zopen", rof)) == NULL) {
      n_perr(_("tmpfile"), 0);
      goto jerr;
   }

   if (flags & FP_MAILDIR)
      ;
   else if (infd >= 0) {
      if (_file_load(flags, infd, fileno(rv), cload) < 0) {
jerr:
         if (rv != NULL)
            fclose(rv);
         rv = NULL;
         if (infd >= 0)
            close(infd);
         goto jleave;
      }
   } else {
      if ((infd = creat(file, 0666)) == -1) {
         fclose(rv);
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

   register_file(rv, osflags, 0, flags, file, offset, csave, NULL,NULL);
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
Ftmp(char **fn, char const *namehint, enum oflags oflags)
{
   /* The 8 is arbitrary but leaves room for a six character suffix (the
    * POSIX minimum path length is 14, though we don't check that XXX).
    * 8 should be more than sufficient given that we use base64url encoding
    * for our random string */
   enum {_RANDCHARS = 8u};

   char *cp_base, *cp;
   size_t maxname, xlen, i;
   char const *tmpdir;
   int osoflags, fd, e;
   bool_t relesigs;
   FILE *fp;
   NYD_ENTER;

   assert(namehint != NULL);
   assert((oflags & OF_WRONLY) || (oflags & OF_RDWR));
   assert(!(oflags & OF_RDONLY));
   assert(!(oflags & OF_REGISTER_UNLINK) || (oflags & OF_REGISTER));

   fp = NULL;
   relesigs = FAL0;
   e = 0;
   tmpdir = ok_vlook(TMPDIR);
   maxname = NAME_MAX;
#ifdef HAVE_PATHCONF
   {  long pc;

      if ((pc = pathconf(tmpdir, _PC_NAME_MAX)) != -1)
         maxname = (size_t)pc;
   }
#endif

   if ((oflags & OF_SUFFIX) && *namehint != '\0') {
      if ((xlen = strlen(namehint)) > maxname - _RANDCHARS) {
         n_err_no = n_ERR_NAMETOOLONG;
         goto jleave;
      }
   } else
      xlen = 0;

   /* Prepare the template string once, then iterate over the random range */
   cp_base =
   cp = smalloc(strlen(tmpdir) + 1 + maxname +1);
   cp = sstpcpy(cp, tmpdir);
   *cp++ = '/';
   {
      char *x = sstpcpy(cp, VAL_UAGENT);
      *x++ = '-';
      if (!(oflags & OF_SUFFIX))
         x = sstpcpy(x, namehint);

      i = PTR2SIZE(x - cp);
      if (i > maxname - xlen - _RANDCHARS) {
         size_t j = maxname - xlen - _RANDCHARS;
         x -= i - j;
         i = j;
      }

      if ((oflags & OF_SUFFIX) && xlen > 0)
         memcpy(x + _RANDCHARS, namehint, xlen);

      x[xlen + _RANDCHARS] = '\0';
      cp = x;
   }

   osoflags = O_CREAT | O_EXCL | _O_CLOEXEC;
   osoflags |= (oflags & OF_WRONLY) ? O_WRONLY : O_RDWR;
   if (oflags & OF_APPEND)
      osoflags |= O_APPEND;

   for (i = 0;; ++i) {
      memcpy(cp, n_random_create_cp(_RANDCHARS), _RANDCHARS);

      hold_all_sigs();
      relesigs = TRU1;

      if ((fd = open(cp_base, osoflags, 0600)) != -1) {
         _CLOEXEC_SET(fd);
         break;
      }
      if (i >= FTMP_OPEN_TRIES) {
         e = n_err_no;
         goto jfree;
      }
      relesigs = FAL0;
      rele_all_sigs();
   }

   if (oflags & OF_REGISTER) {
      char const *osflags = (oflags & OF_RDWR ? "w+" : "w");
      int osflagbits;

      scan_mode(osflags, &osflagbits); /* TODO osoflags&xy ?!!? */
      if ((fp = fdopen(fd, osflags)) != NULL)
         register_file(fp, osflagbits | _O_CLOEXEC, 0,
            (FP_RAW | (oflags & OF_REGISTER_UNLINK ? FP_UNLINK : 0)),
            cp_base, 0L, NULL, NULL,NULL);
   } else
      fp = fdopen(fd, (oflags & OF_RDWR ? "w+" : "w"));

   if (fp == NULL || (oflags & OF_UNLINK)) {
      e = n_err_no;
      unlink(cp_base);
      goto jfree;
   }

   if (fn != NULL)
      *fn = cp_base;
   else
      free(cp_base);
jleave:
   if (relesigs && (fp == NULL || !(oflags & OF_HOLDSIGS)))
      rele_all_sigs();
   if (fp == NULL)
      n_err_no = e;
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
Ftmp_free(char **fn) /* TODO DROP: OF_REGISTER_FREEPATH! */
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
   sigset_t nset;
   char mod[2];
   n_sighdl_t osigint;
   struct termios *tiosp;
   FILE *rv;
   NYD_ENTER;

   /* First clean up child structures */
   /* C99 */{
      struct child **cpp, *cp;

      hold_all_sigs();
      for (cpp = &_popen_child; *cpp != NULL;) {
         if ((*cpp)->pid == -1) {
            cp = *cpp;
            *cpp = cp->link;
            free(cp);
         } else
            cpp = &(*cpp)->link;
      }
      rele_all_sigs();
   }

   rv = NULL;
   tiosp = NULL;
   n_UNINIT(osigint, SIG_ERR);
   mod[0] = '0', mod[1] = '\0';

   if (!pipe_cloexec(p))
      goto jleave;

   if (*mode == 'r') {
      myside = p[READ];
      fd0 = n_CHILD_FD_PASS;
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
      fd1 = n_CHILD_FD_PASS;
      mod[0] = 'w';
   }

   /* In interactive mode both STDIN and STDOUT point to the terminal.  If we
    * pass through the TTY restore terminal attributes after pipe returns.
    * XXX It shouldn't matter which FD we actually use in this case */
   if ((n_psonce & n_PSO_INTERACTIVE) && (fd0 == n_CHILD_FD_PASS ||
         fd1 == n_CHILD_FD_PASS)) {
      osigint = n_signal(SIGINT, SIG_IGN);
      tiosp = smalloc(sizeof *tiosp);
      tcgetattr(STDIN_FILENO, tiosp);
      n_TERMCAP_SUSPEND(TRU1);
   }

   sigemptyset(&nset);

   if (cmd == (char*)-1) {
      if ((pid = n_child_fork()) == -1)
         n_perr(_("fork"), 0);
      else if (pid == 0) {
         union {char const *ccp; int (*ptf)(void); int es;} u;
         n_child_prepare(&nset, fd0, fd1);
         close(p[READ]);
         close(p[WRITE]);
         u.ccp = sh;
         u.es = (*u.ptf)();
         _exit(u.es);
      }
   } else if (sh == NULL) {
      pid = n_child_start(cmd, &nset, fd0, fd1, NULL, NULL, NULL, env_addon);
   } else {
      pid = n_child_start(sh, &nset, fd0, fd1, "-c", cmd, NULL, env_addon);
   }
   if (pid < 0) {
      close(p[READ]);
      close(p[WRITE]);
      goto jleave;
   }
   close(hisside);
   if ((rv = fdopen(myside, mod)) != NULL)
      register_file(rv, 0, pid,
         (tiosp == NULL ? FP_PIPE : FP_PIPE | FP_TERMIOS),
         NULL, 0L, NULL, tiosp, osigint);
   else
      close(myside);
jleave:
   if(rv == NULL && tiosp != NULL){
      n_TERMCAP_RESUME(TRU1);
      tcsetattr(STDIN_FILENO, TCSAFLUSH, tiosp);
      n_free(tiosp);
      n_signal(SIGINT, osigint);
   }
   NYD_LEAVE;
   return rv;
}

FL bool_t
Pclose(FILE *ptr, bool_t dowait)
{
   n_sighdl_t osigint;
   struct termios *tiosp;
   int pid;
   bool_t rv = FAL0;
   NYD_ENTER;

   pid = file_pid(ptr);
   if(pid < 0)
      goto jleave;

   unregister_file(ptr, &tiosp, &osigint);
   fclose(ptr);

   if(dowait){
      hold_all_sigs();
      rv = n_child_wait(pid, NULL);
      if(tiosp != NULL){
         n_TERMCAP_RESUME(TRU1);
         tcsetattr(STDIN_FILENO, TCSAFLUSH, tiosp);
         n_signal(SIGINT, osigint);
      }
      rele_all_sigs();
   }else{
      n_child_free(pid);
      rv = TRU1;
   }

   if(tiosp != NULL)
      free(tiosp);
jleave:
   NYD_LEAVE;
   return rv;
}

FL FILE *
n_pager_open(void)
{
   char const *env_add[2], *pager;
   FILE *rv;
   NYD_ENTER;

   assert(n_psonce & n_PSO_INTERACTIVE);

   pager = n_pager_get(env_add + 0);
   env_add[1] = NULL;

   if ((rv = Popen(pager, "w", NULL, env_add, n_CHILD_FD_PASS)) == NULL)
      n_perr(pager, 0);
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_pager_close(FILE *fp)
{
   sighandler_type sh;
   bool_t rv;
   NYD_ENTER;

   sh = safe_signal(SIGPIPE, SIG_IGN);
   rv = Pclose(fp, TRU1);
   safe_signal(SIGPIPE, sh);
   NYD_LEAVE;
   return rv;
}

FL void
close_all_files(void)
{
   NYD_ENTER;
   while (fp_head != NULL)
      if ((fp_head->flags & FP_MASK) == FP_PIPE)
         Pclose(fp_head->fp, TRU1);
      else
         Fclose(fp_head->fp);
   NYD_LEAVE;
}

FL int
n_child_run(char const *cmd, sigset_t *mask, int infd, int outfd,
   char const *a0, char const *a1, char const *a2, char const **env_addon)
{
   sigset_t nset, oset;
   sighandler_type soldint;
   int rv;
   enum {a_NONE = 0, a_INTIGN = 1<<0, a_TTY = 1<<1} f;
   NYD_ENTER;

   f = a_NONE;
   n_UNINIT(soldint, SIG_ERR);

   /* TODO Of course this is a joke given that during a "p*" the PAGER may
    * TODO be up and running while we play around like this... but i guess
    * TODO this can't be helped at all unless we perform complete and true
    * TODO process group separation and ensure we don't deadlock us out
    * TODO via TTY jobcontrol signal storms (could this really happen?).
    * TODO Or have a builtin pager.  Or query any necessity BEFORE we start
    * TODO any action, and shall we find we need to run programs dump it
    * TODO all into a temporary file which is then passed through to the
    * TODO PAGER.  Ugh.  That still won't help for "needsterminal" anyway */
   if(infd == n_CHILD_FD_PASS || outfd == n_CHILD_FD_PASS){
      soldint = safe_signal(SIGINT, SIG_IGN);
      f = a_INTIGN;

      if(n_psonce & n_PSO_INTERACTIVE){
         f |= a_TTY;
         tcgetattr((n_psonce & n_PSO_TTYIN ? STDIN_FILENO : STDOUT_FILENO),
            &a_popen_tios);
         n_TERMCAP_SUSPEND(FAL0);
         sigfillset(&nset);
         sigdelset(&nset, SIGCHLD);
         sigdelset(&nset, SIGINT);
         /* sigdelset(&nset, SIGPIPE); TODO would need a handler */
         sigprocmask(SIG_BLOCK, &nset, &oset);
         a_popen_hadsig = 0;
         a_popen_jobsigs_up();
      }
   }

   if((rv = n_child_start(cmd, mask, infd, outfd, a0, a1, a2, env_addon)) < 0)
      rv = -1;
   else{
      if(n_child_wait(rv, NULL))
         rv = 0;
      else{
         if(ok_blook(bsdcompat) || ok_blook(bsdmsgs))
            n_err(_("Fatal error in process\n"));
         rv = -1;
      }
   }

   if(f & a_TTY){
      a_popen_jobsigs_down();
      n_TERMCAP_RESUME(a_popen_hadsig ? TRU1 : FAL0);
      tcsetattr(((n_psonce & n_PSO_TTYIN) ? STDIN_FILENO : STDOUT_FILENO),
         ((n_psonce & n_PSO_TTYIN) ? TCSAFLUSH : TCSADRAIN), &a_popen_tios);
      sigprocmask(SIG_SETMASK, &oset, NULL);
   }
   if(f & a_INTIGN){
      if(soldint != SIG_IGN)
         safe_signal(SIGINT, soldint);
   }
   NYD_LEAVE;
   return rv;
}

FL int
n_child_start(char const *cmd, sigset_t *mask, int infd, int outfd,
   char const *a0, char const *a1, char const *a2,
   char const **env_addon)
{
   int rv;
   NYD_ENTER;

   if ((rv = n_child_fork()) == -1) {
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
                  env[ei] = n_UNCONST(ee);
                  env_addon[ai] = NULL;
                  break;
               }
            }
         }

         /* And append the rest */
         for (ei = ei_orig, ai = ai_orig; ai-- > 0;)
            if (env_addon[ai] != NULL)
               env[ei++] = n_UNCONST(env_addon[ai]);

         env[ei] = NULL;
         environ = env;
      }

      i = (int)getrawlist(TRU1, argv, n_NELEM(argv), cmd, strlen(cmd));

      if ((argv[i++] = n_UNCONST(a0)) != NULL &&
            (argv[i++] = n_UNCONST(a1)) != NULL &&
            (argv[i++] = n_UNCONST(a2)) != NULL)
         argv[i] = NULL;
      n_child_prepare(mask, infd, outfd);
      execvp(argv[0], argv);
      perror(argv[0]);
      _exit(n_EXIT_ERR);
   }
   NYD_LEAVE;
   return rv;
}

FL int
n_child_fork(void){
   struct child *cp;
   int pid;
   NYD2_ENTER;

   cp = a_popen_child_find(0, TRU1);

   if((cp->pid = pid = fork()) == -1){
      a_popen_child_del(cp);
      n_perr(_("fork"), 0);
   }
   NYD2_LEAVE;
   return pid;
}

FL void
n_child_prepare(sigset_t *nset, int infd, int outfd)
{
   int i;
   sigset_t fset;
   NYD_ENTER;

   /* All file descriptors other than 0, 1, and 2 are supposed to be cloexec */
   /* TODO WHAT IS WITH STDERR_FILENO DAMN? */
   if ((i = (infd == n_CHILD_FD_NULL)))
      infd = open("/dev/null", O_RDONLY);
   if (infd >= 0) {
      dup2(infd, STDIN_FILENO);
      if (i)
         close(infd);
   }

   if ((i = (outfd == n_CHILD_FD_NULL)))
      outfd = open("/dev/null", O_WRONLY);
   if (outfd >= 0) {
      dup2(outfd, STDOUT_FILENO);
      if (i)
         close(outfd);
   }

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
n_child_free(int pid){
   sigset_t nset, oset;
   struct child *cp;
   NYD2_ENTER;

   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   if((cp = a_popen_child_find(pid, FAL0)) != NULL){
      if(cp->done)
         a_popen_child_del(cp);
      else
         cp->free = TRU1;
   }

   sigprocmask(SIG_SETMASK, &oset, NULL);
   NYD2_LEAVE;
}

FL bool_t
n_child_wait(int pid, int *wait_status){
   sigset_t nset, oset;
   struct child *cp;
   int ws;
   bool_t rv;
   NYD_ENTER;

   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   if((cp = a_popen_child_find(pid, FAL0)) != NULL){
      while(!cp->done)
         sigsuspend(&oset);
      ws = cp->status;
      a_popen_child_del(cp);
   }else
      ws = 0;

   sigprocmask(SIG_SETMASK, &oset, NULL);

   if(wait_status != NULL)
      *wait_status = ws;
   rv = (WIFEXITED(ws) && WEXITSTATUS(ws) == 0);
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
