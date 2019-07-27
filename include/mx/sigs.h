/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Signal handling and commands heavily related with signals.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
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
#ifndef mx_SIGS_H
#define mx_SIGS_H

#include <mx/nail.h>

/* TODO FAKE */
#define mx_HEADER
#include <su/code-in.h>

/* `sleep' */
FL int c_sleep(void *v);

/* */
FL void n_raise(int signo);

/* Provide BSD-like signal() on all systems TODO v15 -> SysV -> n_signal() */
FL n_sighdl_t safe_signal(int signum, n_sighdl_t handler);

/* Provide reproducable non-restartable signal handler installation */
FL n_sighdl_t n_signal(int signo, n_sighdl_t hdl);

/* Block all signals except some fatal trap ones and SIGCHLD.
 * sigadjust starts an optional 0 terminated list of signal adjustments:
 * a positive one will be sigdelset()ted, a negative one will be added.
 * Adjusts the list if already active */
FL void mx_sigs_all_hold(s32 sigadjust, ...);
#define mx_sigs_all_holdx() mx_sigs_all_hold(0)
FL void mx_sigs_all_rele(void);

/* Hold HUP/QUIT/INT */
FL void hold_sigs(void);
FL void rele_sigs(void);

/* Call _ENTER_SWITCH() with the according flags, it'll take care for the rest
 * and also set the jump buffer - it returns 0 if anything went fine and
 * a signal number if a jump occurred, in which case all handlers requested in
 * flags are temporarily SIG_IGN.
 * _cleanup_ping() informs the condome that no jumps etc. shall be performed
 * until _leave() is called in the following -- to be (optionally) called right
 * before the local jump label is reached which is jumped to after a long jump
 * occurred, straight code flow provided, e.g., to avoid destructors to be
 * called twice.  _leave() must always be called last, reraise_flags will be
 * used to decide how signal handling has to continue */
#define n_SIGMAN_ENTER_SWITCH(S,F) do{\
   int __x__;\
   hold_sigs();\
   if(sigsetjmp((S)->sm_jump, 1))\
      __x__ = -1;\
   else\
      __x__ = F;\
   n__sigman_enter(S, __x__);\
}while(0); switch((S)->sm_signo)

FL int n__sigman_enter(struct n_sigman *self, int flags);
FL void n_sigman_cleanup_ping(struct n_sigman *self);
FL void n_sigman_leave(struct n_sigman *self, enum n_sigman_flags flags);

/* Pending signal or 0? */
FL int n_sigman_peek(void);
FL void n_sigman_consume(void);

/* Not-Yet-Dead debug information (handler installation in main.c) */
#if su_DVLOR(1, 0)
FL void mx__nyd_oncrash(int signo);
#endif

#include <su/code-ou.h>
#endif /* mx_SIGS_H */
/* s-it-mode */
