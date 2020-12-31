/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of sigs.h.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE sigs
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <stdarg.h>

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>

#include "mx/cmd.h"

/* TODO fake */
#include "mx/sigs.h"
#include "su/code-in.h"

/*
 * TODO At the beginning of November 2015 -- for v14.9 -- i've tried for one
 * TODO and a half week to convert this codebase to SysV style signal handling,
 * TODO meaning no SA_RESTART and EINTR in a lot of places and error reporting
 * TODO up the chain.   I failed miserably, not only because S/MIME / SSL but
 * TODO also because of general frustration.  Directly after v14.10 i'll strip
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
 * TODO   perform proper terminal attribute handling.  For children that do not
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

/* signal_all_ */
static uz volatile a_sigs_all_depth;
static sigset_t a_sigs_all_nset, a_sigs_all_oset;

/* {hold,rele}_sigs() */
static uz           _hold_sigdepth;
static sigset_t         _hold_nset, _hold_oset;

/* */
static void a_sigs_dummyhdl(int sig);

static void
a_sigs_dummyhdl(int sig){
   UNUSED(sig);
}

int
c_sleep(void *v){ /* XXX installs sighdl+ due to outer jumps and SA_RESTART! */
   sigset_t nset, oset;
   struct sigaction nact, oact;
   boole ignint;
   uz sec, msec;
   char **argv;
   NYD_IN;

   argv = v;

   if((su_idec_uz_cp(&sec, argv[0], 0, NULL) &
         (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED)
      goto jesyn;

   if(argv[1] == NULL){
      msec = 0;
      ignint = FAL0;
   }else if((su_idec_uz_cp(&msec, argv[1], 0, NULL) &
         (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED)
      goto jesyn;
   else
      ignint = (argv[2] != NULL);

   if(UZ_MAX / n_DATE_MILLISSEC < sec)
      goto jeover;
   sec *= n_DATE_MILLISSEC;

   if(UZ_MAX - sec < msec)
      goto jeover;
   msec += sec;

   /* XXX This requires a terrible mess of signal handling:
    * - we usually have our SA_RESTART handler, that must be replaced
    * - we most often have a sigsetjmp() to overcome SA_RESTART
    * - TODO for now signal_all_hold() most often on in robot mode,
    *   TODO therefore we also need sigprocmask(), to block anything
    *   TODO except SIGINT, and to unblock SIGINT, thus! */
   su_mem_set(&nact, 0, sizeof nact);
   nact.sa_handler = &a_sigs_dummyhdl;
   sigemptyset(&nact.sa_mask);
   sigaddset(&nact.sa_mask, SIGINT);
   sigaction(SIGINT, &nact, &oact);

   sigfillset(&nset);
   sigdelset(&nset, SIGINT);
   sigprocmask(SIG_BLOCK, &nset, &oset);
   sigemptyset(&nset);
   sigaddset(&nset, SIGINT);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);

   n_pstate_err_no = (n_msleep(msec, ignint) > 0) ? su_ERR_INTR : su_ERR_NONE;

   sigprocmask(SIG_SETMASK, &oset, NULL);
   sigaction(SIGINT, &oact, NULL);
jleave:
   NYD_OU;
   return (argv == NULL);
jeover:
   n_err(_("sleep: argument(s) overflow(s) datatype\n"));
   n_pstate_err_no = su_ERR_OVERFLOW;
   argv = NULL;
   goto jleave;
jesyn:
   mx_cmd_print_synopsis(mx_cmd_firstfit("sleep"), NIL);
   n_pstate_err_no = su_ERR_INVAL;
   argv = NULL;
   goto jleave;
}

void
n_raise(int signo)
{
   NYD2_IN;
   if(n_pid == 0)
      n_pid = getpid();
   kill(n_pid, signo);
   NYD2_OU;
}

n_sighdl_t
safe_signal(int signum, n_sighdl_t handler)
{
   struct sigaction nact, oact;
   n_sighdl_t rv;
   NYD2_IN;

   nact.sa_handler = handler;
   sigfillset(&nact.sa_mask);
   nact.sa_flags = SA_RESTART;
   rv = (sigaction(signum, &nact, &oact) != 0) ? SIG_ERR : oact.sa_handler;
   NYD2_OU;
   return rv;
}

n_sighdl_t
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

void
mx_sigs_all_hold(s32 sigadjust, ...){
   sigset_t unbl, *ossp;
   boole nounbl, anyunbl;

   sigemptyset(&unbl);

   if((nounbl = (a_sigs_all_depth++ == 0))){
      sigfillset(&a_sigs_all_nset);
      sigdelset(&a_sigs_all_nset, SIGABRT);
#ifdef SIGBUS
      sigdelset(&a_sigs_all_nset, SIGBUS);
#endif
      sigdelset(&a_sigs_all_nset, SIGFPE);
      sigdelset(&a_sigs_all_nset, SIGILL);
      sigdelset(&a_sigs_all_nset, SIGSEGV);

      sigdelset(&a_sigs_all_nset, SIGCHLD);

      sigdelset(&a_sigs_all_nset, SIGKILL);
      sigdelset(&a_sigs_all_nset, SIGSTOP);

      ossp = &a_sigs_all_oset;
   }else
      ossp = NIL;

   anyunbl = FAL0;
   if(sigadjust != 0){
      va_list val;

      va_start(val, sigadjust);

      do if(sigadjust > 0)
         sigaddset(&a_sigs_all_nset, sigadjust);
      else{
         sigadjust = -sigadjust;
         sigdelset(&a_sigs_all_nset, sigadjust);
         sigaddset(&unbl, sigadjust);
         anyunbl = TRU1;
      }while((sigadjust = va_arg(val, s32)) != 0);

      va_end(val);
   }

   sigprocmask(SIG_BLOCK, &a_sigs_all_nset, ossp);
   if(!nounbl && anyunbl)
      sigprocmask(SIG_UNBLOCK, &unbl, NIL);
}

void
mx_sigs_all_rele(void){
   if(--a_sigs_all_depth == 0)
      sigprocmask(SIG_SETMASK, &a_sigs_all_oset, NIL);
}

void
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

void
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
   NYD; /* Signal handler */
   n__sigman->sm_signo = signo;
   siglongjmp(n__sigman->sm_jump, 1);
}

int
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

void
n_sigman_cleanup_ping(struct n_sigman *self){
   u32 f;
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

void
n_sigman_leave(struct n_sigman *self,
      enum n_sigman_flags reraise_flags){
   u32 f;
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

int
n_sigman_peek(void){
   int rv;
   NYD2_IN;
   rv = 0;
   NYD2_OU;
   return rv;
}

void
n_sigman_consume(void){
   NYD2_IN;
   NYD2_OU;
}

#if su_DVLOR(1, 0)
static void a_sigs_nyd__dump(su_up cookie, char const *buf, su_uz blen);
static void
a_sigs_nyd__dump(su_up cookie, char const *buf, su_uz blen){
   write((int)cookie, buf, blen);
}

void
mx__nyd_oncrash(int signo){
   char pathbuf[PATH_MAX], s2ibuf[32], *cp;
   int fd;
   uz i, fnl;
   char const *tmpdir;
   u32 poption_save;

   su_nyd_set_disabled(TRU1);

   LCTA(sizeof("./") -1 + sizeof(VAL_UAGENT) -1 + sizeof(".dat") < PATH_MAX,
      "System limits too low for fixed-size buffer operation");

   poption_save = n_poption; /* XXX sigh */
   n_poption &= ~n_PO_D_V;

      i = su_cs_len(tmpdir = ok_vlook(TMPDIR));

   n_poption = poption_save;

   fnl = sizeof(VAL_UAGENT) -1;

   if(i + 1 + fnl + 1 + sizeof(".dat") > sizeof(pathbuf)){
      (cp = pathbuf)[0] = '.';
      i = 1;
   }else
      su_mem_copy(cp = pathbuf, tmpdir, i);
   cp[i++] = '/'; /* xxx pathsep */
   su_mem_copy(cp += i, VAL_UAGENT, fnl);
   i += fnl;
   su_mem_copy(cp += fnl, ".dat", sizeof(".dat"));
   fnl = i + sizeof(".dat") -1;

   if((fd = open(pathbuf, O_WRONLY | O_CREAT | O_EXCL, 0666)) == -1)
      fd = STDERR_FILENO;

# undef _X
# define _X(X) X, sizeof(X) -1
   if(signo == SIGUSR2)
      write(fd, _X("\n\nNYD: program chirp list"));
   else
      write(fd, _X("\n\nNYD: program dying due to signal "));

   if(signo != SIGUSR2){
      cp = &s2ibuf[sizeof(s2ibuf) -1];
      *cp = '\0';
      i = signo;
      do{
         *--cp = "0123456789"[i % 10];
         i /= 10;
      }while(i != 0);
      write(fd, cp, P2UZ(&s2ibuf[sizeof(s2ibuf) -1] - cp));
   }

   write(fd, _X(":\n"));

   su_nyd_dump(&a_sigs_nyd__dump, S(uz,S(u32,fd)));

   write(fd, _X("-----\nCome up to the lab and see what's on the slab\n"));

   if(fd != STDERR_FILENO){
      write(STDERR_FILENO, _X("Crash NYD listing written to "));
      write(STDERR_FILENO, pathbuf, fnl);
      write(STDERR_FILENO, _X("\n"));
# undef _X

      close(fd);
   }

   if(signo != SIGUSR2){
      struct sigaction xact;
      sigset_t xset;

      xact.sa_handler = SIG_DFL;
      sigemptyset(&xact.sa_mask);
      xact.sa_flags = 0;
      sigaction(signo, &xact, NIL);

      sigemptyset(&xset);
      sigaddset(&xset, signo);
      sigprocmask(SIG_UNBLOCK, &xset, NIL);

      n_raise(signo);

      for(;;)
         _exit(n_EXIT_ERR);
   }

   su_nyd_set_disabled(FAL0);
}
#endif /* DVLOR(1,0) */

#include "su/code-ou.h"
/* s-it-mode */
