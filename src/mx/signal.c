/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Signal related stuff as well as NotYetDead functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC (better: drop)
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
#define su_FILE signal

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/*
 * TODO At the beginning of November 2015 -- for v14.9 -- i've tried for one
 * TODO and a half week to convert this codebase to SysV style signal handling,
 * TODO meaning no SA_RESTART and EINTR in a lot of places and error reporting
 * TODO up the chain.   I failed miserably, not only because S/MIME / SSL but
 * TODO also because of general frustration.  Directly after v14.9 i will strip
 * TODO ANYTHING off the codebase (socket stuff etc.) and keep only the very
 * TODO core, doing namespace and type cleanup and convert this core to a clean
 * TODO approach, from which i plan to start this thing anew.
 * TODO For now i introduced the n_sigman, yet another hack, to be used in a
 * TODO few places.
 * TODO The real solution:
 * TODO - No SA_RESTART.  Just like for my C++ library: encapsulate EINTR in
 * TODO   userspace at the systemcall (I/O rewrite: drop stdio), normal
 * TODO   interface auto-restarts, special _intr() series return EINTR.
 * TODO   Do n_sigman_poll()/peek()/whatever whenever desired for the former,
 * TODO   report errors up the call chain, have places where operations can be
 * TODO   "properly" aborted.
 * TODO - We save the initial signal settings upon program startup.
 * TODO - We register our sigman handlers once, at program startup.
 * TODO   Maximally, and most likely only due to lack of atomic CAS, ignore
 * TODO   or block some signals temporarily.  Best if not.
 * TODO   The signal handlers only set a flag.  Block all signals for handler
 * TODO   execution, like this we are safe to "set the first signal was x".
 * TODO - In interactive context, ignore SIGTERM.
 * TODO   I.e., see the POSIX standard for what a shell does.
 * TODO - In non-interactive context, don't know anything about job control!?!
 * TODO - Place child processes in their own process group.  Restore the signal
 * TODO   mask back to the saved original one for them, before exec.
 * TODO - Except for job control related (<-> interactive) ignore any signals
 * TODO   while we are "behind" a child that occupies the terminal.  For those,
 * TODO   perform proper terminal attribute handling.  For childs that don't
 * TODO   occupy the terminal we "are the shell" and should therefore manage
 * TODO   them accordingly, including termination request as necessary.
 * TODO   And if we have a worker in a pipeline, we need to manage it and deal
 * TODO   with it properly, WITHOUT temporary signal overwrites.
 * TODO - No more jumps.
 * TODO - (When sockets will be reintroduced, non-blocking.)
 * TODO - (When SSL is reintroduced, memory BIO.  It MAY be necessary to
 * TODO   temporarily block signals during a few SSL functions?  Read SSL docu!
 * TODO   But i prefer blocking since it's a single syscall, not temporary
 * TODO   SA_RESTART setting, since that has to be done for every signal.)
 */

#ifdef HAVE_NYD
struct nyd_info {
   char const  *ni_file;
   char const  *ni_fun;
   ui32_t      ni_chirp_line;
   ui32_t      ni_level;
};
#endif

/* {hold,rele}_all_sigs() */
static size_t           _alls_depth;
static sigset_t         _alls_nset, _alls_oset;

/* {hold,rele}_sigs() */
static size_t           _hold_sigdepth;
static sigset_t         _hold_nset, _hold_oset;

/* NYD, memory pool debug */
#ifdef HAVE_NYD
static ui32_t           _nyd_curr, _nyd_level;
static struct nyd_info  _nyd_infos[NYD_CALLS_MAX];
#endif

/* */
static void a_signal_dummyhdl(int sig);

/* */
#ifdef HAVE_NYD
static void    _nyd_print(int fd, struct nyd_info *nip);
#endif

static void
a_signal_dummyhdl(int sig){
   n_UNUSED(sig);
}

#ifdef HAVE_NYD
static void
_nyd_print(int fd, struct nyd_info *nip)
{
   char buf[80];
   union {int i; size_t z;} u;

   u.i = snprintf(buf, sizeof buf,
         "%c [%2" PRIu32 "] %.25s (%.40s:%" PRIu32 ")\n",
         "=><"[(nip->ni_chirp_line >> 29) & 0x3], nip->ni_level, nip->ni_fun,
         nip->ni_file, (nip->ni_chirp_line & 0x1FFFFFFFu));
   if (u.i > 0) {
      u.z = u.i;
      if (u.z > sizeof buf)
         u.z = sizeof buf - 1; /* (Skip \0) */
      write(fd, buf, u.z);
   }
}
#endif

FL int
c_sleep(void *v){ /* XXX installs sighdl+ due to outer jumps and SA_RESTART! */
   sigset_t nset, oset;
   struct sigaction nact, oact;
   bool_t ignint;
   uiz_t sec, msec;
   char **argv;
   NYD_IN;

   argv = v;

   if((n_idec_uiz_cp(&sec, argv[0], 0, NULL) &
         (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
         ) != n_IDEC_STATE_CONSUMED)
      goto jesyn;

   if(argv[1] == NULL){
      msec = 0;
      ignint = FAL0;
   }else if((n_idec_uiz_cp(&msec, argv[1], 0, NULL) &
         (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
         ) != n_IDEC_STATE_CONSUMED)
      goto jesyn;
   else
      ignint = (argv[2] != NULL);

   if(UIZ_MAX / n_DATE_MILLISSEC < sec)
      goto jeover;
   sec *= n_DATE_MILLISSEC;

   if(UIZ_MAX - sec < msec)
      goto jeover;
   msec += sec;

   /* XXX This requires a terrible mess of signal handling:
    * - we usually have our SA_RESTART handler, that must be replaced
    * - we most often have a sigsetjmp() to overcome SA_RESTART
    * - TODO for now hold_all_sigs() most often on in robot mode,
    *   TODO therefore we also need sigprocmask(), to block anything
    *   TODO except SIGINT, and to unblock SIGINT, thus! */
   memset(&nact, 0, sizeof nact);
   nact.sa_handler = &a_signal_dummyhdl;
   sigemptyset(&nact.sa_mask);
   sigaddset(&nact.sa_mask, SIGINT);
   sigaction(SIGINT, &nact, &oact);

   sigfillset(&nset);
   sigdelset(&nset, SIGINT);
   sigprocmask(SIG_BLOCK, &nset, &oset);
   sigemptyset(&nset);
   sigaddset(&nset, SIGINT);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);

   n_pstate_err_no = (n_msleep(msec, ignint) > 0) ? n_ERR_INTR : n_ERR_NONE;

   sigprocmask(SIG_SETMASK, &oset, NULL);
   sigaction(SIGINT, &oact, NULL);
jleave:
   NYD_OU;
   return (argv == NULL);
jeover:
   n_err(_("`sleep': argument(s) overflow(s) datatype\n"));
   n_pstate_err_no = n_ERR_OVERFLOW;
   argv = NULL;
   goto jleave;
jesyn:
   n_err(_("Synopsis: sleep: <seconds> [<milliseconds>] [uninterruptible]\n"));
   n_pstate_err_no = n_ERR_INVAL;
   argv = NULL;
   goto jleave;
}


#ifdef HAVE_DEVEL
FL int
c_sigstate(void *vp){ /* TODO remove again */
   struct{
      int val;
      char const name[12];
   } const *hdlp, hdla[] = {
      {SIGINT, "SIGINT"}, {SIGHUP, "SIGHUP"}, {SIGQUIT, "SIGQUIT"},
      {SIGTSTP, "SIGTSTP"}, {SIGTTIN, "SIGTTIN"}, {SIGTTOU, "SIGTTOU"},
      {SIGCHLD, "SIGCHLD"}, {SIGPIPE, "SIGPIPE"}
   };
   char const *cp;
   NYD2_IN;

   if((cp = vp) != NULL && cp[0] != '\0'){
      if(!asccasecmp(&cp[1], "all")){
         if(cp[0] == '+')
            hold_all_sigs();
         else
            rele_all_sigs();
      }else if(!asccasecmp(&cp[1], "hold")){
         if(cp[0] == '+')
            hold_sigs();
         else
            rele_sigs();
      }
   }

   fprintf(n_stdout, "alls_depth %zu, hold_sigdepth %zu\nHandlers:\n",
      _alls_depth, _hold_sigdepth);
   for(hdlp = hdla; hdlp < &hdla[n_NELEM(hdla)]; ++hdlp){
      sighandler_type shp;

      shp = safe_signal(hdlp->val, SIG_IGN);
      safe_signal(hdlp->val, shp);
      fprintf(n_stdout, "  %s: %p (%s)\n", hdlp->name, shp,
         (shp == SIG_ERR ? "ERR" : (shp == SIG_DFL ? "DFL"
            : (shp == SIG_IGN ? "IGN" : "ptf?"))));
   }
   NYD2_OU;
   return OKAY;
}
#endif /* HAVE_DEVEL */

FL void
n_raise(int signo)
{
   NYD2_IN;
   if(n_pid == 0)
      n_pid = getpid();
   kill(n_pid, signo);
   NYD2_OU;
}

FL sighandler_type
safe_signal(int signum, sighandler_type handler)
{
   struct sigaction nact, oact;
   sighandler_type rv;
   NYD2_IN;

   nact.sa_handler = handler;
   sigfillset(&nact.sa_mask);
   nact.sa_flags = SA_RESTART;
   rv = (sigaction(signum, &nact, &oact) != 0) ? SIG_ERR : oact.sa_handler;
   NYD2_OU;
   return rv;
}

FL n_sighdl_t
n_signal(int signo, n_sighdl_t hdl){
   struct sigaction nact, oact;
   NYD2_IN;

   nact.sa_handler = hdl;
   sigfillset(&nact.sa_mask);
   nact.sa_flags = 0;
   hdl = (sigaction(signo, &nact, &oact) != 0) ? SIG_ERR : oact.sa_handler;
   NYD2_OU;
   return hdl;
}

FL void
hold_all_sigs(void)
{
   NYD2_IN;
   if (_alls_depth++ == 0) {
      sigfillset(&_alls_nset);
      sigdelset(&_alls_nset, SIGABRT);
#ifdef SIGBUS
      sigdelset(&_alls_nset, SIGBUS);
#endif
      sigdelset(&_alls_nset, SIGFPE);
      sigdelset(&_alls_nset, SIGILL);
      sigdelset(&_alls_nset, SIGKILL);
      sigdelset(&_alls_nset, SIGSEGV);
      sigdelset(&_alls_nset, SIGSTOP);

      sigdelset(&_alls_nset, SIGCHLD);
      sigprocmask(SIG_BLOCK, &_alls_nset, &_alls_oset);
   }
   NYD2_OU;
}

FL void
rele_all_sigs(void)
{
   NYD2_IN;
   if (--_alls_depth == 0)
      sigprocmask(SIG_SETMASK, &_alls_oset, (sigset_t*)NULL);
   NYD2_OU;
}

FL void
hold_sigs(void)
{
   NYD2_IN;
   if (_hold_sigdepth++ == 0) {
      sigemptyset(&_hold_nset);
      sigaddset(&_hold_nset, SIGHUP);
      sigaddset(&_hold_nset, SIGINT);
      sigaddset(&_hold_nset, SIGQUIT);
      sigprocmask(SIG_BLOCK, &_hold_nset, &_hold_oset);
   }
   NYD2_OU;
}

FL void
rele_sigs(void)
{
   NYD2_IN;
   if (--_hold_sigdepth == 0)
      sigprocmask(SIG_SETMASK, &_hold_oset, NULL);
   NYD2_OU;
}

/* TODO This is temporary gracyness */
static struct n_sigman *n__sigman;
static void n__sigman_hdl(int signo);
static void
n__sigman_hdl(int signo){
   NYD_X; /* Signal handler */
   n__sigman->sm_signo = signo;
   siglongjmp(n__sigman->sm_jump, 1);
}

FL int
n__sigman_enter(struct n_sigman *self, int flags){
   /* TODO no error checking when installing sighdls */
   int rv;
   NYD2_IN;

   if((int)flags >= 0){
      self->sm_flags = (enum n_sigman_flags)flags;
      self->sm_signo = 0;
      self->sm_outer = n__sigman;
      if(flags & n_SIGMAN_HUP)
         self->sm_ohup = safe_signal(SIGHUP, &n__sigman_hdl);
      if(flags & n_SIGMAN_INT)
         self->sm_oint = safe_signal(SIGINT, &n__sigman_hdl);
      if(flags & n_SIGMAN_QUIT)
         self->sm_oquit = safe_signal(SIGQUIT, &n__sigman_hdl);
      if(flags & n_SIGMAN_PIPE)
         self->sm_opipe = safe_signal(SIGPIPE, &n__sigman_hdl);
      n__sigman = self;
      rv = 0;
   }else{
      flags = self->sm_flags;

      /* Just in case of a race (signal while holding and ignoring? really?) */
      if(!(flags & n__SIGMAN_PING)){
         if(flags & n_SIGMAN_HUP)
            safe_signal(SIGHUP, SIG_IGN);
         if(flags & n_SIGMAN_INT)
            safe_signal(SIGINT, SIG_IGN);
         if(flags & n_SIGMAN_QUIT)
            safe_signal(SIGQUIT, SIG_IGN);
         if(flags & n_SIGMAN_PIPE)
            safe_signal(SIGPIPE, SIG_IGN);
      }
      rv = self->sm_signo;
      /* The signal mask has been restored, but of course rele_sigs() has
       * already been called: account for restoration due to jump */
      ++_hold_sigdepth;
   }
   rele_sigs();
   NYD2_OU;
   return rv;
}

FL void
n_sigman_cleanup_ping(struct n_sigman *self){
   ui32_t f;
   NYD2_IN;

   hold_sigs();

   f = self->sm_flags;
   f |= n__SIGMAN_PING;
   self->sm_flags = f;

   if(f & n_SIGMAN_HUP)
      safe_signal(SIGHUP, SIG_IGN);
   if(f & n_SIGMAN_INT)
      safe_signal(SIGINT, SIG_IGN);
   if(f & n_SIGMAN_QUIT)
      safe_signal(SIGQUIT, SIG_IGN);
   if(f & n_SIGMAN_PIPE)
      safe_signal(SIGPIPE, SIG_IGN);

   rele_sigs();
   NYD2_OU;
}

FL void
n_sigman_leave(struct n_sigman *self,
      enum n_sigman_flags reraise_flags){
   ui32_t f;
   int sig;
   NYD2_IN;

   hold_sigs();
   n__sigman = self->sm_outer;

   f = self->sm_flags;
   if(f & n_SIGMAN_HUP)
      safe_signal(SIGHUP, self->sm_ohup);
   if(f & n_SIGMAN_INT)
      safe_signal(SIGINT, self->sm_oint);
   if(f & n_SIGMAN_QUIT)
      safe_signal(SIGQUIT, self->sm_oquit);
   if(f & n_SIGMAN_PIPE)
      safe_signal(SIGPIPE, self->sm_opipe);

   rele_sigs();

   sig = 0;
   switch(self->sm_signo){
   case SIGPIPE:
      if((reraise_flags & n_SIGMAN_PIPE) ||
            ((reraise_flags & n_SIGMAN_NTTYOUT_PIPE) &&
             !(n_psonce & n_PSO_TTYOUT)))
         sig = SIGPIPE;
      break;
   case SIGHUP:
      if(reraise_flags & n_SIGMAN_HUP)
         sig = SIGHUP;
      break;
   case SIGINT:
      if(reraise_flags & n_SIGMAN_INT)
         sig = SIGINT;
      break;
   case SIGQUIT:
      if(reraise_flags & n_SIGMAN_QUIT)
         sig = SIGQUIT;
      break;
   default:
      break;
   }

   NYD2_OU;
   if(sig != 0){
      sigset_t cset;

      sigemptyset(&cset);
      sigaddset(&cset, sig);
      sigprocmask(SIG_UNBLOCK, &cset, NULL);
      n_raise(sig);
   }
}

FL int
n_sigman_peek(void){
   int rv;
   NYD2_IN;
   rv = 0;
   NYD2_OU;
   return rv;
}

FL void
n_sigman_consume(void){
   NYD2_IN;
   NYD2_OU;
}

#ifdef HAVE_NYD
FL void
_nyd_chirp(ui8_t act, char const *file, ui32_t line, char const *fun)
{
   struct nyd_info *nip = _nyd_infos;

   if (_nyd_curr != n_NELEM(_nyd_infos))
      nip += _nyd_curr++;
   else
      _nyd_curr = 1;
   nip->ni_file = file;
   nip->ni_fun = fun;
   nip->ni_chirp_line = ((ui32_t)(act & 0x3) << 29) | (line & 0x1FFFFFFFu);
   nip->ni_level = ((act == 0) ? _nyd_level
         : (act == 1) ? ++_nyd_level : _nyd_level--);
}

FL void
_nyd_oncrash(int signo)
{
   char pathbuf[PATH_MAX], s2ibuf[32], *cp;
   struct sigaction xact;
   sigset_t xset;
   struct nyd_info *nip;
   int fd;
   size_t i, fnl;
   char const *tmpdir;

   n_LCTA(sizeof("./") -1 + sizeof(VAL_UAGENT) -1 + sizeof(".dat") < PATH_MAX,
      "System limits too low for fixed-size buffer operation");

   xact.sa_handler = SIG_DFL;
   sigemptyset(&xact.sa_mask);
   xact.sa_flags = 0;
   sigaction(signo, &xact, NULL);

   i = strlen(tmpdir = ok_vlook(TMPDIR));
   fnl = sizeof(VAL_UAGENT) -1;

   if (i + 1 + fnl + 1 + sizeof(".dat") > sizeof(pathbuf)) {
      (cp = pathbuf)[0] = '.';
      i = 1;
   } else
      memcpy(cp = pathbuf, tmpdir, i);
   cp[i++] = '/'; /* xxx pathsep */
   memcpy(cp += i, VAL_UAGENT, fnl);
   i += fnl;
   memcpy(cp += fnl, ".dat", sizeof(".dat"));
   fnl = i + sizeof(".dat") -1;

   if ((fd = open(pathbuf, O_WRONLY | O_CREAT | O_EXCL, 0666)) == -1)
      fd = STDERR_FILENO;

# undef _X
# define _X(X) (X), sizeof(X) -1
   write(fd, _X("\n\nNYD: program dying due to signal "));

   cp = s2ibuf + sizeof(s2ibuf) -1;
   *cp = '\0';
   i = signo;
   do {
      *--cp = "0123456789"[i % 10];
      i /= 10;
   } while (i != 0);
   write(fd, cp, PTR2SIZE((s2ibuf + sizeof(s2ibuf) -1) - cp));

   write(fd, _X(":\n"));

   if (_nyd_infos[n_NELEM(_nyd_infos) - 1].ni_file != NULL)
      for (i = _nyd_curr, nip = _nyd_infos + i; i < n_NELEM(_nyd_infos); ++i)
         _nyd_print(fd, nip++);
   for (i = 0, nip = _nyd_infos; i < _nyd_curr; ++i)
      _nyd_print(fd, nip++);

   write(fd, _X("----------\nCome up to the lab and see what's on the slab\n"));

   if (fd != STDERR_FILENO) {
      write(STDERR_FILENO, _X("Crash NYD listing written to "));
      write(STDERR_FILENO, pathbuf, fnl);
      write(STDERR_FILENO, _X("\n"));
# undef _X

      close(fd);
   }

   sigemptyset(&xset);
   sigaddset(&xset, signo);
   sigprocmask(SIG_UNBLOCK, &xset, NULL);
   n_raise(signo);
   for (;;)
      _exit(n_EXIT_ERR);
}
#endif /* HAVE_NYD */

/* s-it-mode */
