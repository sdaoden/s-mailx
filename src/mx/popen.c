/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of pipes, child processes, temporary files, file enwrapping.
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
#undef su_FILE
#define su_FILE popen
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>

#define READ               0
#define WRITE              1

struct fp {
   struct fp   *link;
   int         omode;
   int         pid;
   enum {
      FP_RAW,
FP_IMAP = 1u<<3,
      FP_MAILDIR = 1u<<4,
      FP_HOOK = 1u<<5,
      FP_PIPE = 1u<<6,
      FP_MASK = (1u<<7) - 1,
      /* TODO FP_UNLINK: should be in a separated process so that unlinking
       * TODO the temporary "garbage" is "safe"(r than it is like that) */
      FP_UNLINK = 1u<<9,
      FP_TERMIOS = 1u<<10
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

static int a_popen_scan_mode(char const *mode, int *omode);
static void          register_file(FILE *fp, int omode, int pid,
                        int flags, char const *realfile, long offset,
                        char const *save_cmd, struct termios *tiosp,
                        n_sighdl_t osigint);
static enum okay _file_save(struct fp *fpp);
static int a_popen_file_load(int flags, int infd, int outfd,
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
a_popen_scan_mode(char const *mode, int *omode){
   static struct{
      char const mode[4];
      int omode;
   } const maps[] = {
      {"r", O_RDONLY},
      {"w", O_WRONLY | O_CREAT | n_O_NOXY_BITS | O_TRUNC},
      {"wx", O_WRONLY | O_CREAT | O_EXCL},
      {"a", O_WRONLY | O_APPEND | O_CREAT | n_O_NOXY_BITS},
      {"a+", O_RDWR | O_APPEND | O_CREAT | n_O_NOXY_BITS},
      {"r+", O_RDWR},
      {"w+", O_RDWR | O_CREAT | O_EXCL}
   };
   int i;
   n_NYD2_IN;

   for(i = 0; UICMP(z, i, <, n_NELEM(maps)); ++i)
      if(!su_cs_cmp(maps[i].mode, mode)){
         *omode = maps[i].omode;
         i = 0;
         goto jleave;
      }

   su_DBG( n_alert(_("Internal error: bad stdio open mode %s"), mode); )
   su_err_set_no(su_ERR_INVAL);
   *omode = 0; /* (silence CC) */
   i = -1;
jleave:
   n_NYD2_OU;
   return i;
}

static void
register_file(FILE *fp, int omode, int pid, int flags,
   char const *realfile, long offset, char const *save_cmd,
   struct termios *tiosp, n_sighdl_t osigint)
{
   struct fp *fpp;
   n_NYD_IN;

   assert(!(flags & FP_UNLINK) || realfile != NULL);
   assert(!(flags & FP_TERMIOS) || tiosp != NULL);

   fpp = n_alloc(sizeof *fpp);
   fpp->fp = fp;
   fpp->omode = omode;
   fpp->pid = pid;
   fpp->link = fp_head;
   fpp->flags = flags;
   fpp->realfile = (realfile != NULL) ? su_cs_dup(realfile) : NULL;
   fpp->save_cmd = (save_cmd != NULL) ? su_cs_dup(save_cmd) : NULL;
   fpp->fp_tios = tiosp;
   fpp->fp_osigint = osigint;
   fpp->offset = offset;
   fp_head = fpp;
   n_NYD_OU;
}

static enum okay
_file_save(struct fp *fpp)
{
   char const *cmd[3];
   int outfd;
   enum okay rv;
   n_NYD_IN;

   if (fpp->omode == O_RDONLY) {
      rv = OKAY;
      goto jleave;
   }
   rv = STOP;

   fflush(fpp->fp);
   clearerr(fpp->fp);

   /* Ensure the I/O library doesn't optimize the fseek(3) away! */
   if(!n_real_seek(fpp->fp, fpp->offset, SEEK_SET)){
      outfd = su_err_no();
      n_err(_("Fatal: cannot restore file position and save %s: %s\n"),
         n_shexp_quote_cp(fpp->realfile, FAL0), su_err_doc(outfd));
      goto jleave;
   }

#ifdef mx_HAVE_IMAP
   if ((fpp->flags & FP_MASK) == FP_IMAP) {
      rv = imap_append(fpp->realfile, fpp->fp, fpp->offset);
      goto jleave;
   }
#endif

#ifdef mx_HAVE_MAILDIR
   if ((fpp->flags & FP_MASK) == FP_MAILDIR) {
      rv = maildir_append(fpp->realfile, fpp->fp, fpp->offset);
      goto jleave;
   }
#endif

   outfd = open(fpp->realfile,
         ((fpp->omode | O_CREAT | (fpp->omode & O_APPEND ? 0 : O_TRUNC) |
            n_O_NOXY_BITS) & ~O_EXCL), 0666);
   if (outfd == -1) {
      outfd = su_err_no();
      n_err(_("Fatal: cannot create %s: %s\n"),
         n_shexp_quote_cp(fpp->realfile, FAL0), su_err_doc(outfd));
      goto jleave;
   }

   cmd[2] = NULL;
   switch(fpp->flags & FP_MASK){
   case FP_HOOK:
      if(n_poption & n_PO_D_V)
         n_err(_("Using `filetype' handler %s to save %s\n"),
            n_shexp_quote_cp(fpp->save_cmd, FAL0),
            n_shexp_quote_cp(fpp->realfile, FAL0));
      cmd[0] = ok_vlook(SHELL);
      cmd[1] = "-c";
      cmd[2] = fpp->save_cmd;
      break;
   default:
      cmd[0] = "cat";
      cmd[1] = NULL;
      break;
   }
   if (n_child_run(cmd[0], 0, fileno(fpp->fp), outfd,
         cmd[1], cmd[2], NULL, NULL, NULL) >= 0)
      rv = OKAY;

   close(outfd);
jleave:
   n_NYD_OU;
   return rv;
}

static int
a_popen_file_load(int flags, int infd, int outfd, char const *load_cmd){
   char const *cmd[3];
   int rv;
   n_NYD2_IN;

   cmd[2] = NULL;
   switch(flags & FP_MASK){
   case FP_IMAP:
   case FP_MAILDIR:
      rv = 0;
      goto jleave;
   case FP_HOOK:
      cmd[0] = ok_vlook(SHELL);
      cmd[1] = "-c";
      cmd[2] = load_cmd;
      break;
   default:
      cmd[0] = "cat";
      cmd[1] = NULL;
      break;
   }

   rv = n_child_run(cmd[0], 0, infd, outfd, cmd[1], cmd[2], NULL, NULL, NULL);
jleave:
   n_NYD2_OU;
   return rv;
}

static enum okay
unregister_file(FILE *fp, struct termios **tiosp, n_sighdl_t *osigint)
{
   struct fp **pp, *p;
   enum okay rv = OKAY;
   n_NYD_IN;

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
            n_free(p->save_cmd);
         if (p->realfile != NULL)
            n_free(p->realfile);
         if (p->flags & FP_TERMIOS) {
            if (tiosp != NULL) {
               *tiosp = p->fp_tios;
               *osigint = p->fp_osigint;
            } else
               n_free(p->fp_tios);
         }
         n_free(p);
         goto jleave;
      }
   su_DBGOR(n_panic, n_alert)(_("Invalid file pointer"));
   rv = STOP;
jleave:
   n_NYD_OU;
   return rv;
}

static int
file_pid(FILE *fp)
{
   int rv;
   struct fp *p;
   n_NYD2_IN;

   rv = -1;
   for (p = fp_head; p; p = p->link)
      if (p->fp == fp) {
         rv = p->pid;
         break;
      }
   n_NYD2_OU;
   return rv;
}

static void
a_popen_jobsigs_up(void){
   sigset_t nset, oset;
   n_NYD2_IN;

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
   n_NYD2_OU;
}

static void
a_popen_jobsigs_down(void){
   sigset_t nset, oset;
   n_NYD2_IN;

   sigfillset(&nset);

   sigprocmask(SIG_BLOCK, &nset, &oset);
   safe_signal(SIGTSTP, a_popen_otstp);
   safe_signal(SIGTTIN, a_popen_ottin);
   safe_signal(SIGTTOU, a_popen_ottou);

   sigaddset(&oset, SIGTSTP);
   sigaddset(&oset, SIGTTIN);
   sigaddset(&oset, SIGTTOU);
   sigprocmask(SIG_SETMASK, &oset, NULL);
   n_NYD2_OU;
}

static void
a_popen_jobsig(int sig){
   sighandler_type oldact;
   sigset_t nset;
   bool_t hadsig;
   n_NYD_X; /* Signal handler */

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
   n_NYD_X; /* Signal handler */
   n_UNUSED(signo);

   for (;;) {
      pid = waitpid(-1, &status, WNOHANG);
      if (pid <= 0) {
         if (pid == -1 && su_err_no() == su_ERR_INTR)
            continue;
         break;
      }

      if ((cp = a_popen_child_find(pid, FAL0)) != NULL) {
         cp->done = 1;
         if (cp->free)
            cp->pid = -1; /* XXX Was _delchild(cp);# */
         else {
            cp->status = status;
         }
      }
   }
}

static struct child *
a_popen_child_find(int pid, bool_t create){
   struct child **cpp, *cp;
   n_NYD2_IN;

   for(cpp = &_popen_child; (cp = *cpp) != NULL && cp->pid != pid;
         cpp = &(*cpp)->link)
      ;

   if(cp == NULL && create)
      (*cpp = cp = n_calloc(1, sizeof *cp))->pid = pid;
   n_NYD2_OU;
   return cp;
}

static void
a_popen_child_del(struct child *cp){
   struct child **cpp;
   n_NYD2_IN;

   cpp = &_popen_child;

   for(;;){
      if(*cpp == cp){
         *cpp = cp->link;
         n_free(cp);
         break;
      }
      if(*(cpp = &(*cpp)->link) == NULL){
         su_DBG( n_err("! a_popen_child_del(): implementation error\n"); )
         break;
      }
   }
   n_NYD2_OU;
}

FL void
n_child_manager_start(void)
{
   struct sigaction nact, oact;
   n_NYD_IN;

   nact.sa_handler = &a_popen_sigchld;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = SA_RESTART
#ifdef SA_NOCLDSTOP
         | SA_NOCLDSTOP
#endif
         ;
   if (sigaction(SIGCHLD, &nact, &oact) != 0)
      n_panic(_("Cannot install signal handler for child process management"));
   n_NYD_OU;
}

FL FILE *
safe_fopen(char const *file, char const *oflags, int *xflags)
{
   int osflags, fd;
   FILE *fp = NULL;
   n_NYD2_IN; /* (only for Fopen() and once in go.c) */

   if (a_popen_scan_mode(oflags, &osflags) < 0)
      goto jleave;
   osflags |= _O_CLOEXEC;
   if (xflags != NULL)
      *xflags = osflags;

   if ((fd = open(file, osflags, 0666)) == -1)
      goto jleave;
   _CLOEXEC_SET(fd);

   fp = fdopen(fd, oflags);
jleave:
   n_NYD2_OU;
   return fp;
}

FL FILE *
Fopen(char const *file, char const *oflags)
{
   FILE *fp;
   int osflags;
   n_NYD_IN;

   if ((fp = safe_fopen(file, oflags, &osflags)) != NULL)
      register_file(fp, osflags, 0, FP_RAW, NULL, 0L, NULL, NULL,NULL);
   n_NYD_OU;
   return fp;
}

FL FILE *
Fdopen(int fd, char const *oflags, bool_t nocloexec)
{
   FILE *fp;
   int osflags;
   n_NYD_IN;

   a_popen_scan_mode(oflags, &osflags);
   if (!nocloexec)
      osflags |= _O_CLOEXEC; /* Ensured to be set by caller as documented! */

   if ((fp = fdopen(fd, oflags)) != NULL)
      register_file(fp, osflags, 0, FP_RAW, NULL, 0L, NULL, NULL,NULL);
   n_NYD_OU;
   return fp;
}

FL int
Fclose(FILE *fp)
{
   int i = 0;
   n_NYD_IN;

   if (unregister_file(fp, NULL, NULL) == OKAY)
      i |= 1;
   if (fclose(fp) == 0)
      i |= 2;
   n_NYD_OU;
   return (i == 3 ? 0 : EOF);
}

FL FILE *
n_fopen_any(char const *file, char const *oflags, /* TODO should take flags */
      enum n_fopen_state *fs_or_null){ /* TODO as bits, return state */
   /* TODO Support file locking upon open time */
   long offset;
   enum protocol p;
   enum oflags rof;
   int osflags, flags, omode, infd;
   char const *cload, *csave;
   enum n_fopen_state fs;
   FILE *rv;
   n_NYD_IN;

   rv = NULL;
   fs = n_FOPEN_STATE_NONE;
   cload = csave = NULL;

   if(a_popen_scan_mode(oflags, &osflags) < 0)
      goto jleave;

   flags = 0;
   rof = OF_RDWR | OF_UNLINK;
   if(osflags & O_APPEND)
      rof |= OF_APPEND;
   omode = (osflags == O_RDONLY) ? R_OK : R_OK | W_OK;

   /* We don't want to find mbox.bz2 when doing "copy * mbox", but only for
    * "file mbox", so don't try hooks when writing */
   p = which_protocol(csave = file, TRU1, ((omode & W_OK) == 0), &file);
   fs = (enum n_fopen_state)p;
   switch(p){
   default:
      goto jleave;
   case n_PROTO_IMAP:
#ifdef mx_HAVE_IMAP
      file = csave;
      flags |= FP_IMAP;
      osflags = O_RDWR | O_APPEND | O_CREAT | n_O_NOXY_BITS;
      infd = -1;
      break;
#else
      su_err_set_no(su_ERR_OPNOTSUPP);
      goto jleave;
#endif
   case n_PROTO_MAILDIR:
#ifdef mx_HAVE_MAILDIR
      if(fs_or_null != NULL && !access(file, F_OK))
         fs |= n_FOPEN_STATE_EXISTS;
      flags |= FP_MAILDIR;
      osflags = O_RDWR | O_APPEND | O_CREAT | n_O_NOXY_BITS;
      infd = -1;
      break;
#else
      su_err_set_no(su_ERR_OPNOTSUPP);
      goto jleave;
#endif
   case n_PROTO_FILE:{
      struct n_file_type ft;

      if(!(osflags & O_EXCL) && fs_or_null != NULL && !access(file, F_OK))
         fs |= n_FOPEN_STATE_EXISTS;

      if(n_filetype_exists(&ft, file)){
         flags |= FP_HOOK;
         cload = ft.ft_load_dat;
         csave = ft.ft_save_dat;
         /* Cause truncation for compressor/hook output files */
         osflags &= ~O_APPEND;
         rof &= ~OF_APPEND;
         if((infd = open(file, (omode & W_OK ? O_RDWR : O_RDONLY))) != -1){
            fs |= n_FOPEN_STATE_EXISTS;
            if(n_poption & n_PO_D_V)
               n_err(_("Using `filetype' handler %s to load %s\n"),
                  n_shexp_quote_cp(cload, FAL0), n_shexp_quote_cp(file, FAL0));
         }else if(!(osflags & O_CREAT) || su_err_no() != su_ERR_NOENT)
            goto jleave;
      }else{
         /*flags |= FP_RAW;*/
         rv = Fopen(file, oflags);
         if((osflags & O_EXCL) && rv == NULL)
            fs |= n_FOPEN_STATE_EXISTS;
         goto jleave;
      }
      }break;
   }

   /* Note rv is not yet register_file()d, fclose() it in error path! */
   if((rv = Ftmp(NULL, "fopenany", rof)) == NULL){
      n_perr(_("tmpfile"), 0);
      goto jerr;
   }

   if(flags & (FP_IMAP | FP_MAILDIR))
      ;
   else if(infd >= 0){
      if(a_popen_file_load(flags, infd, fileno(rv), cload) < 0){
jerr:
         if(rv != NULL)
            fclose(rv);
         rv = NULL;
         if(infd >= 0)
            close(infd);
         goto jleave;
      }
   }else{
      if((infd = creat(file, 0666)) == -1){
         fclose(rv);
         rv = NULL;
         goto jleave;
      }
   }

   if(infd >= 0)
      close(infd);
   fflush(rv);

   if(!(osflags & O_APPEND))
      rewind(rv);
   if((offset = ftell(rv)) == -1){
      Fclose(rv);
      rv = NULL;
      goto jleave;
   }

   register_file(rv, osflags, 0, flags, file, offset, csave, NULL,NULL);
jleave:
   if(fs_or_null != NULL)
      *fs_or_null = fs;
   n_NYD_OU;
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
   n_NYD_IN;

   assert(namehint != NULL);
   assert((oflags & OF_WRONLY) || (oflags & OF_RDWR));
   assert(!(oflags & OF_RDONLY));
   assert(!(oflags & OF_REGISTER_UNLINK) || (oflags & OF_REGISTER));

   fp = NULL;
   relesigs = FAL0;
   e = 0;
   tmpdir = ok_vlook(TMPDIR);
   maxname = NAME_MAX;
#ifdef mx_HAVE_PATHCONF
   {  long pc;

      if ((pc = pathconf(tmpdir, _PC_NAME_MAX)) != -1)
         maxname = (size_t)pc;
   }
#endif

   if ((oflags & OF_SUFFIX) && *namehint != '\0') {
      if ((xlen = su_cs_len(namehint)) > maxname - _RANDCHARS) {
         su_err_set_no(su_ERR_NAMETOOLONG);
         goto jleave;
      }
   } else
      xlen = 0;

   /* Prepare the template string once, then iterate over the random range */
   cp_base =
   cp = n_lofi_alloc(su_cs_len(tmpdir) + 1 + maxname +1);
   cp = su_cs_pcopy(cp, tmpdir);
   *cp++ = '/';
   {
      char *x = su_cs_pcopy(cp, VAL_UAGENT);
      *x++ = '-';
      if (!(oflags & OF_SUFFIX))
         x = su_cs_pcopy(x, namehint);

      i = PTR2SIZE(x - cp);
      if (i > maxname - xlen - _RANDCHARS) {
         size_t j = maxname - xlen - _RANDCHARS;
         x -= i - j;
      }

      if ((oflags & OF_SUFFIX) && xlen > 0)
         su_mem_copy(x + _RANDCHARS, namehint, xlen);

      x[xlen + _RANDCHARS] = '\0';
      cp = x;
   }

   osoflags = O_CREAT | O_EXCL | _O_CLOEXEC;
   osoflags |= (oflags & OF_WRONLY) ? O_WRONLY : O_RDWR;
   if (oflags & OF_APPEND)
      osoflags |= O_APPEND;

   for(relesigs = TRU1, i = 0;; ++i){
      su_mem_copy(cp, n_random_create_cp(_RANDCHARS, NULL), _RANDCHARS);

      hold_all_sigs();

      if((fd = open(cp_base, osoflags, 0600)) != -1){
         _CLOEXEC_SET(fd);
         break;
      }
      if(i >= FTMP_OPEN_TRIES){
         e = su_err_no();
         goto jfree;
      }
      rele_all_sigs();
   }

   if (oflags & OF_REGISTER) {
      char const *osflags = (oflags & OF_RDWR ? "w+" : "w");
      int osflagbits;

      a_popen_scan_mode(osflags, &osflagbits); /* TODO osoflags&xy ?!!? */
      if ((fp = fdopen(fd, osflags)) != NULL)
         register_file(fp, osflagbits | _O_CLOEXEC, 0,
            (FP_RAW | (oflags & OF_REGISTER_UNLINK ? FP_UNLINK : 0)),
            cp_base, 0L, NULL, NULL,NULL);
   } else
      fp = fdopen(fd, (oflags & OF_RDWR ? "w+" : "w"));

   if (fp == NULL || (oflags & OF_UNLINK)) {
      e = su_err_no();
      unlink(cp_base);
      goto jfree;
   }else if(fp != NULL){
      /* We will succeed and keep the file around for further usage, likely
       * another stream will be opened for pure reading purposes (this is true
       * at the time of this writing.  A restrictive umask(2) settings may have
       * turned the path inaccessible, so ensure it may be read at least!
       * TODO once ok_vlook() can return an integer, look up *umask* first! */
      (void)fchmod(fd, S_IWUSR | S_IRUSR);
   }

   if(fn != NULL){
      i = su_cs_len(cp_base) +1;
      cp = (oflags & OF_FN_AUTOREC) ? n_autorec_alloc(i) : n_alloc(i);
      su_mem_copy(cp, cp_base, i);
      *fn = cp;
   }
   n_lofi_free(cp_base);
jleave:
   if (relesigs && (fp == NULL || !(oflags & OF_HOLDSIGS)))
      rele_all_sigs();
   if (fp == NULL)
      su_err_set_no(e);
   n_NYD_OU;
   return fp;
jfree:
   if((cp = cp_base) != NULL)
      n_lofi_free(cp);
   goto jleave;
}

FL void
Ftmp_release(char **fn)
{
   char *cp;
   n_NYD_IN;

   cp = *fn;
   *fn = NULL;
   if (cp != NULL) {
      unlink(cp);
      rele_all_sigs();
      n_free(cp);
   }
   n_NYD_OU;
}

FL void
Ftmp_free(char **fn) /* TODO DROP: OF_REGISTER_FREEPATH! */
{
   char *cp;
   n_NYD_IN;

   cp = *fn;
   *fn = NULL;
   if (cp != NULL)
      n_free(cp);
   n_NYD_OU;
}

FL bool_t
pipe_cloexec(int fd[2]){
   bool_t rv;
   n_NYD_IN;

   rv = FAL0;

#ifdef mx_HAVE_PIPE2
   if(pipe2(fd, O_CLOEXEC) != -1)
      rv = TRU1;
#else
   if(pipe(fd) != -1){
      n_fd_cloexec_set(fd[0]);
      n_fd_cloexec_set(fd[1]);
      rv = TRU1;
   }
#endif
   n_NYD_OU;
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
   n_NYD_IN;

   /* First clean up child structures */
   /* C99 */{
      struct child **cpp, *cp;

      hold_all_sigs();
      for (cpp = &_popen_child; *cpp != NULL;) {
         if ((*cpp)->pid == -1) {
            cp = *cpp;
            *cpp = cp->link;
            n_free(cp);
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
      tiosp = n_alloc(sizeof *tiosp);
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
         /* TODO should close all other open FDs except stds and reset memory */
         /* Standard I/O drives me insane!  All we need is a sync operation
          * that causes n_stdin to forget about any read buffer it may have.
          * We cannot use fflush(3), this works with Musl and Solaris, but not
          * with GlibC.  (For at least pipes.)  We cannot use fdreopen(),
          * because this function does not exist!  Luckily (!!!) we only use
          * n_stdin not stdin in our child, otherwise all bets were off!
          * TODO (Unless we would fiddle around with FILE* directly:
          * TODO #ifdef __GLIBC__
          * TODO   n_stdin->_IO_read_ptr = n_stdin->_IO_read_end;
          * TODO #elif *BSD*
          * TODO   n_stdin->_r = 0;
          * TODO #elif n_OS_SOLARIS || n_OS_SUNOS
          * TODO   n_stdin->_cnt = 0;
          * TODO #endif
          * TODO ) which should have additional config test for sure! */
         n_stdin = fdopen(STDIN_FILENO, "r");
         /*n_stdout = fdopen(STDOUT_FILENO, "w");*/
         /*n_stderr = fdopen(STDERR_FILENO, "w");*/
         u.ccp = sh;
         u.es = (*u.ptf)();
         /*fflush(NULL);*/
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
   n_NYD_OU;
   return rv;
}

FL bool_t
Pclose(FILE *ptr, bool_t dowait)
{
   n_sighdl_t osigint;
   struct termios *tiosp;
   int pid;
   bool_t rv = FAL0;
   n_NYD_IN;

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
      n_free(tiosp);
jleave:
   n_NYD_OU;
   return rv;
}

VL int
n_psignal(FILE *fp, int sig){
   int rv;
   n_NYD2_IN;

   if((rv = file_pid(fp)) >= 0){
      struct child *cp;

      if((cp = a_popen_child_find(rv, FAL0)) != NULL){
         if((rv = kill(rv, sig)) != 0)
            rv = su_err_no();
      }else
         rv = -1;
   }
   n_NYD2_OU;
   return rv;
}

FL FILE *
n_pager_open(void)
{
   char const *env_add[2], *pager;
   FILE *rv;
   n_NYD_IN;

   assert(n_psonce & n_PSO_INTERACTIVE);

   pager = n_pager_get(env_add + 0);
   env_add[1] = NULL;

   if ((rv = Popen(pager, "w", NULL, env_add, n_CHILD_FD_PASS)) == NULL)
      n_perr(pager, 0);
   n_NYD_OU;
   return rv;
}

FL bool_t
n_pager_close(FILE *fp)
{
   sighandler_type sh;
   bool_t rv;
   n_NYD_IN;

   sh = safe_signal(SIGPIPE, SIG_IGN);
   rv = Pclose(fp, TRU1);
   safe_signal(SIGPIPE, sh);
   n_NYD_OU;
   return rv;
}

FL void
close_all_files(void)
{
   n_NYD_IN;
   while (fp_head != NULL)
      if ((fp_head->flags & FP_MASK) == FP_PIPE)
         Pclose(fp_head->fp, TRU1);
      else
         Fclose(fp_head->fp);
   n_NYD_OU;
}

/* TODO The entire n_child_ series should be replaced with an object, but
 * TODO at least have carrier arguments.  We anyway need a command manager
 * TODO that keeps track and knows how to handle job control ++++! */

FL int
n_child_run(char const *cmd, sigset_t *mask_or_null, int infd, int outfd,
   char const *a0_or_null, char const *a1_or_null, char const *a2_or_null,
   char const **env_addon_or_null, int *wait_status_or_null)
{
   sigset_t nset, oset;
   sighandler_type soldint;
   int rv, e;
   enum {a_NONE = 0, a_INTIGN = 1<<0, a_TTY = 1<<1} f;
   n_NYD_IN;

   f = a_NONE;
   n_UNINIT(soldint, SIG_ERR);

   /* TODO Of course this is a joke given that during a "p*" the PAGER may
    * TODO be up and running while we play around like this... but i guess
    * TODO this can't be helped at all unless we perform complete and true
    * TODO process group separation and ensure we don't deadlock us out
    * TODO via TTY jobcontrol signal storms (could this really happen?).
    * TODO Or have a built-in pager.  Or query any necessity BEFORE we start
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

   if((rv = n_child_start(cmd, mask_or_null, infd, outfd, a0_or_null,
         a1_or_null, a2_or_null, env_addon_or_null)) < 0){
      e = su_err_no();
      rv = -1;
   }else{
      int ws;

      e = 0;
      if(n_child_wait(rv, &ws))
         rv = 0;
      else if(wait_status_or_null == NULL || !WIFEXITED(ws)){
         if(ok_blook(bsdcompat) || ok_blook(bsdmsgs))
            n_err(_("Fatal error in process\n"));
         e = su_ERR_CHILD;
         rv = -1;
      }
      if(wait_status_or_null != NULL)
         *wait_status_or_null = ws;
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

   if(e != 0)
      su_err_set_no(e);
   n_NYD_OU;
   return rv;
}

FL int
n_child_start(char const *cmd, sigset_t *mask_or_null, int infd, int outfd,
   char const *a0_or_null, char const *a1_or_null, char const *a2_or_null,
   char const **env_addon_or_null)
{
   int rv, e;
   n_NYD_IN;

   if ((rv = n_child_fork()) == -1) {
      e = su_err_no();
      n_perr(_("fork"), 0);
      su_err_set_no(e);
      rv = -1;
   } else if (rv == 0) {
      char *argv[128];
      int i;

      if (env_addon_or_null != NULL) {
         extern char **environ;
         size_t ei, ei_orig, ai, ai_orig;
         char **env;

         /* TODO note we don't check the POSIX limit:
          * the total space used to store the environment and the arguments to
          * the process is limited to {ARG_MAX} bytes */
         for (ei = 0; environ[ei] != NULL; ++ei)
            ;
         ei_orig = ei;
         for (ai = 0; env_addon_or_null[ai] != NULL; ++ai)
            ;
         ai_orig = ai;
         env = n_lofi_alloc(sizeof(*env) * (ei + ai +1));
         su_mem_copy(env, environ, sizeof(*env) * ei);

         /* Replace all those keys that yet exist */
         while (ai-- > 0) {
            char const *ee, *kvs;
            size_t kl;

            ee = env_addon_or_null[ai];
            kvs = su_cs_find_c(ee, '=');
            assert(kvs != NULL);
            kl = PTR2SIZE(kvs - ee);
            assert(kl > 0);
            for (ei = ei_orig; ei-- > 0;) {
               char const *ekvs = su_cs_find_c(env[ei], '=');
               if (ekvs != NULL && kl == PTR2SIZE(ekvs - env[ei]) &&
                     !su_mem_cmp(ee, env[ei], kl)) {
                  env[ei] = n_UNCONST(ee);
                  env_addon_or_null[ai] = NULL;
                  break;
               }
            }
         }

         /* And append the rest */
         for (ei = ei_orig, ai = ai_orig; ai-- > 0;)
            if (env_addon_or_null[ai] != NULL)
               env[ei++] = n_UNCONST(env_addon_or_null[ai]);

         env[ei] = NULL;
         environ = env;
      }

      i = (int)getrawlist(TRU1, argv, n_NELEM(argv), cmd, su_cs_len(cmd));
      if(i >= 0){
         if ((argv[i++] = n_UNCONST(a0_or_null)) != NULL &&
               (argv[i++] = n_UNCONST(a1_or_null)) != NULL &&
               (argv[i++] = n_UNCONST(a2_or_null)) != NULL)
            argv[i] = NULL;
         n_child_prepare(mask_or_null, infd, outfd);
         execvp(argv[0], argv);
         perror(argv[0]);
      }
      _exit(n_EXIT_ERR);
   }
   n_NYD_OU;
   return rv;
}

FL int
n_child_fork(void){
   /* Strictly speaking we should do so in the waitpid(2) case too, but since
    * we explicitly waitpid(2) on the pid if just the structure exists, which
    * n_child_wait() does in the parent, all is fine */
#if n_SIGSUSPEND_NOT_WAITPID
   sigset_t nset, oset;
#endif
   struct child *cp;
   int pid;
   n_NYD2_IN;

#if n_SIGSUSPEND_NOT_WAITPID
   sigfillset(&nset);
   sigprocmask(SIG_BLOCK, &nset, &oset);
#endif

   cp = a_popen_child_find(0, TRU1);

   if((cp->pid = pid = fork()) == -1){
      a_popen_child_del(cp);
      n_perr(_("fork"), 0);
   }

#if n_SIGSUSPEND_NOT_WAITPID
   sigprocmask(SIG_SETMASK, &oset, NULL);
#endif
   n_NYD2_OU;
   return pid;
}

FL void
n_child_prepare(sigset_t *nset_or_null, int infd, int outfd)
{
   int i;
   sigset_t fset;
   n_NYD_IN;

   /* All file descriptors other than 0, 1, and 2 are supposed to be cloexec */
   /* TODO WHAT IS WITH STDERR_FILENO DAMN? */
   if ((i = (infd == n_CHILD_FD_NULL)))
      infd = open(n_path_devnull, O_RDONLY);
   if (infd >= 0) {
      dup2(infd, STDIN_FILENO);
      if (i)
         close(infd);
   }

   if ((i = (outfd == n_CHILD_FD_NULL)))
      outfd = open(n_path_devnull, O_WRONLY);
   if (outfd >= 0) {
      dup2(outfd, STDOUT_FILENO);
      if (i)
         close(outfd);
   }

   if (nset_or_null != NULL) {
      for (i = 1; i < NSIG; ++i)
         if (sigismember(nset_or_null, i))
            safe_signal(i, SIG_IGN);
      if (!sigismember(nset_or_null, SIGINT))
         safe_signal(SIGINT, SIG_DFL);
   }

   sigemptyset(&fset);
   sigprocmask(SIG_SETMASK, &fset, NULL);
   n_NYD_OU;
}

FL void
n_child_free(int pid){
   sigset_t nset, oset;
   struct child *cp;
   n_NYD2_IN;

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
   n_NYD2_OU;
}

FL bool_t
n_child_wait(int pid, int *wait_status_or_null){
#if !n_SIGSUSPEND_NOT_WAITPID
   sigset_t oset;
#endif
   sigset_t nset;
   struct child *cp;
   int ws;
   bool_t rv;
   n_NYD_IN;

#if !n_SIGSUSPEND_NOT_WAITPID
   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);
#endif

   if((cp = a_popen_child_find(pid, FAL0)) != NULL){
#if n_SIGSUSPEND_NOT_WAITPID
      sigfillset(&nset);
      sigdelset(&nset, SIGCHLD);
      while(!cp->done)
         sigsuspend(&nset); /* TODO we should allow more than SIGCHLD!! */
      ws = cp->status;
#else
      if(!cp->done)
         waitpid(pid, &ws, 0);
      else
         ws = cp->status;
#endif
      a_popen_child_del(cp);
   }else
      ws = 0;

#if !n_SIGSUSPEND_NOT_WAITPID
   sigprocmask(SIG_SETMASK, &oset, NULL);
#endif

   if(wait_status_or_null != NULL)
      *wait_status_or_null = ws;
   rv = (WIFEXITED(ws) && WEXITSTATUS(ws) == 0);
   n_NYD_OU;
   return rv;
}

/* s-it-mode */
