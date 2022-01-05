/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Signal handling and commands heavily related with signals.
 *
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef mx_SIGS_H
#define mx_SIGS_H

#include <mx/nail.h>

/* TODO FAKE */
#define mx_HEADER
#include <su/code-in.h>

enum n_sigman_flags{
   n_SIGMAN_NONE = 0,
   n_SIGMAN_HUP = 1<<0,
   n_SIGMAN_INT = 1<<1,
   n_SIGMAN_QUIT = 1<<2,
   n_SIGMAN_TERM = 1<<3,
   n_SIGMAN_PIPE = 1<<4,

   n_SIGMAN_IGN_HUP = 1<<5,
   n_SIGMAN_IGN_INT = 1<<6,
   n_SIGMAN_IGN_QUIT = 1<<7,
   n_SIGMAN_IGN_TERM = 1<<8,

   n_SIGMAN_ALL = 0xFF,
   /* Mostly for _leave() reraise flags */
   n_SIGMAN_VIPSIGS = n_SIGMAN_HUP | n_SIGMAN_INT | n_SIGMAN_QUIT |
         n_SIGMAN_TERM,
   n_SIGMAN_NTTYOUT_PIPE = 1<<16,
   n_SIGMAN_VIPSIGS_NTTYOUT = n_SIGMAN_HUP | n_SIGMAN_INT | n_SIGMAN_QUIT |
         n_SIGMAN_TERM | n_SIGMAN_NTTYOUT_PIPE,

   n__SIGMAN_PING = 1<<17
};

typedef void (*n_sighdl_t)(int);

/* This is somewhat temporary for pre v15 */
struct n_sigman{
   u32 sm_flags; /* enum n_sigman_flags */
   int sm_signo;
   struct n_sigman *sm_outer;
   n_sighdl_t sm_ohup;
   n_sighdl_t sm_oint;
   n_sighdl_t sm_oquit;
   n_sighdl_t sm_oterm;
   n_sighdl_t sm_opipe;
   sigjmp_buf sm_jump;
};

/* `sleep' */
EXPORT int c_sleep(void *v);

/* */
EXPORT void n_raise(int signo);

/* Provide BSD-like signal() on all systems TODO v15 -> SysV -> n_signal() */
EXPORT n_sighdl_t safe_signal(int signum, n_sighdl_t handler);

/* Provide reproducible non-restartable signal handler installation */
EXPORT n_sighdl_t n_signal(int signo, n_sighdl_t hdl);

/* Block all signals except some fatal trap ones and SIGCHLD.
 * sigadjust starts an optional 0 terminated list of signal adjustments:
 * a positive one will be sigdelset()ted, a negative one will be added.
 * Adjusts the list if already active */
EXPORT void mx_sigs_all_hold(s32 sigadjust, ...);
#define mx_sigs_all_holdx() mx_sigs_all_hold(0)
EXPORT void mx_sigs_all_rele(void);

/* Hold HUP/QUIT/INT */
EXPORT void hold_sigs(void);
EXPORT void rele_sigs(void);

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

EXPORT int n__sigman_enter(struct n_sigman *self, int flags);
EXPORT void n_sigman_cleanup_ping(struct n_sigman *self);
EXPORT void n_sigman_leave(struct n_sigman *self, enum n_sigman_flags flags);

/* Pending signal or 0? */
EXPORT int n_sigman_peek(void);
EXPORT void n_sigman_consume(void);

/* Not-Yet-Dead debug information (handler installation in main.c).
 * Does not crash for SIGUSR2 */
DVL( EXPORT void mx__nyd_oncrash(int signo); )

#include <su/code-ou.h>
#endif /* mx_SIGS_H */
/* s-it-mode */
