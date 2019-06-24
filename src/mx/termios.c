/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of termios.h.
 *@ FIXME everywhere: tcsetattr() generates SIGTTOU when we're not in
 *@ FIXME foreground pgrp, and can fail with EINTR!!
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE termios
#define mx_SOURCE
#define mx_SOURCE_TERMIOS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/ioctl.h>

#include <termios.h>

#include <su/icodec.h>
#include <su/mem.h>

#include "mx/tty.h"

#include "mx/termios.h"
#include "su/code-in.h"

/* Problem: VAL_ configuration values are strings, we need numbers */
#define a_TERMIOS_DEFAULT_HEIGHT \
   (VAL_HEIGHT[1] == '\0' ? (VAL_HEIGHT[0] - '0') \
   : (VAL_HEIGHT[2] == '\0' \
      ? ((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) \
      : (((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) * 10 + \
         (VAL_HEIGHT[2] - '0'))))
#define a_TERMIOS_DEFAULT_WIDTH \
   (VAL_WIDTH[1] == '\0' ? (VAL_WIDTH[0] - '0') \
   : (VAL_WIDTH[2] == '\0' \
      ? ((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) \
      : (((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) * 10 + \
         (VAL_WIDTH[2] - '0'))))

#ifdef SIGWINCH
# define a_TERMIOS_SIGWINCH SIGWINCH
#else
# define a_TERMIOS_SIGWINCH -1
#endif

struct a_termios_env{
   struct a_termios_env *tiose_prev;
   u8 tiose_cmd;
   u8 tiose_a1;
   boole tiose_suspended;
   u8 tiose__pad[su_6432(5,1)];
   mx_termios_on_state_change tiose_on_state_change;
   struct termios tiose_state;
};

struct a_termios_g{
   struct a_termios_env *tiosg_envp;
   /* If outermost == normal state; used as init switch, too */
   struct a_termios_env *tiosg_normal;
   n_sighdl_t tiosg_otstp;
   n_sighdl_t tiosg_ottin;
   n_sighdl_t tiosg_ottou;
   n_sighdl_t tiosg_ocont;
#if a_TERMIOS_SIGWINCH != -1
   n_sighdl_t tiosg_owinch;
#endif
   /* Remaining signals only whenin password/raw mode */
   n_sighdl_t tiosg_ohup;
   n_sighdl_t tiosg_oint;
   n_sighdl_t tiosg_oquit;
   n_sighdl_t tiosg_oterm;
   struct a_termios_env tiosg_env_base;
};

static struct a_termios_g a_termios_g;

struct mx_termios_dimension mx_termios_dimen;

static void a_termios_sig_adjust(boole condome);

/* */
static void a_termios_onsig(int sig);

/* Do the system-dependent dance on getting used to terminal dimension */
static void a_termios_dimen_query(void);
static void a_termios__dimit(struct mx_termios_dimension *tiosdp);

static void
a_termios_sig_adjust(boole condome){
   NYD2_IN;

   if(condome){
      a_termios_g.tiosg_ohup = safe_signal(SIGHUP, &a_termios_onsig);
      a_termios_g.tiosg_oint = safe_signal(SIGINT, &a_termios_onsig);
      a_termios_g.tiosg_oquit = safe_signal(SIGQUIT, &a_termios_onsig);
      a_termios_g.tiosg_oterm = safe_signal(SIGTERM, &a_termios_onsig);
   }else{
      safe_signal(SIGHUP, a_termios_g.tiosg_ohup);
      safe_signal(SIGINT, a_termios_g.tiosg_oint);
      safe_signal(SIGQUIT, a_termios_g.tiosg_oquit);
      safe_signal(SIGTERM, a_termios_g.tiosg_oterm);
   }
   NYD2_OU;
}

static void
a_termios_onsig(int sig){
   n_sighdl_t oact, myact;
   sigset_t nset;
   boole jobsig;
   struct a_termios_env *tiosep;
   NYD; /* Signal handler */

   if(sig == a_TERMIOS_SIGWINCH)
      goto jsigwinch;

#define a_X(N,X,Y) \
   case SIG ## N: oact = a_termios_g.tiosg_o ## X; jobsig = Y; break;

   switch(sig){
   default:
   a_X(TSTP, tstp, TRU1)
   a_X(TTIN, ttin, TRU1)
   a_X(TTOU, ttou, TRU1)
   a_X(CONT, cont, TRU1)
   a_X(HUP, hup, FAL0)
   a_X(INT, int, FAL0)
   a_X(QUIT, quit, FAL0)
   a_X(TERM, term, FAL0)
   }

#undef a_X

   tiosep = a_termios_g.tiosg_envp;

   if(tiosep->tiose_cmd != mx_TERMIOS_CMD_HANDS_OFF &&
         (!jobsig || sig != SIGCONT)){
      if(!tiosep->tiose_suspended){
         tiosep->tiose_suspended = TRU1;

         if(tiosep->tiose_on_state_change != NIL)
            (*tiosep->tiose_on_state_change)((mx_TERMIOS_STATE_SUSPEND |
               (jobsig ? 0 : mx_TERMIOS_STATE_SIGNAL)), sig);

         if(tiosep->tiose_cmd != mx_TERMIOS_CMD_NORMAL &&
               tiosep->tiose_cmd != mx_TERMIOS_CMD_HANDS_OFF)
            tcsetattr(fileno(mx_tty_fp), TCSADRAIN,
               &a_termios_g.tiosg_normal->tiose_state);
      }
   }

   if(jobsig || (tiosep->tiose_cmd != mx_TERMIOS_CMD_HANDS_OFF &&
            oact != SIG_DFL && oact != SIG_IGN && oact != SIG_ERR)){
      myact = safe_signal(sig, oact);

      sigemptyset(&nset);
      sigaddset(&nset, sig);
      sigprocmask(SIG_UNBLOCK, &nset, NIL);
      n_raise(sig);
      sigprocmask(SIG_BLOCK, &nset, NIL);
/* FIXME REQUERY NORMAL STATE if !HANDS_OFF*/

      safe_signal(sig, myact);
   }

   if(tiosep->tiose_cmd != mx_TERMIOS_CMD_HANDS_OFF &&
         (!jobsig || sig == SIGCONT) && tiosep->tiose_suspended){
      tiosep->tiose_suspended = FAL0;

      tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state);

      if(tiosep->tiose_on_state_change != NIL)
         (*tiosep->tiose_on_state_change)(mx_TERMIOS_STATE_RESUME |
            mx_TERMIOS_STATE_SIGNAL, sig);
   }

   if(sig == SIGCONT)
      goto jsigwinch;
jleave:
   return;

jsigwinch:
   n_pstate |= n_PS_SIGWINCH_PEND;
   a_termios_dimen_query();
   goto jleave;
}

static void
a_termios_dimen_query(void){
   NYD_IN;
   a_termios__dimit(&mx_termios_dimen);
   /* Note: for the first invocation this will always trigger.
    * If we have termcap support then termcap_init() will undo this if
    * FULLWIDTH is set after termcap is initialized.
    * We have to evaluate it now since cmds may run pre-termcap ... */
/*#ifdef mx_HAVE_TERMCAP*/
   if(mx_termios_dimen.tiosd_width > 1 &&
         !(n_psonce & n_PSO_TERMCAP_FULLWIDTH))
      --mx_termios_dimen.tiosd_width;
/*#endif*/
   NYD_OU;
}

static void
a_termios__dimit(struct mx_termios_dimension *tiosdp){
   struct termios tbuf;
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
   struct winsize ws;
#elif defined TIOCGSIZE
   struct ttysize ts;
#else
# error One of TCGETWINSIZE, TIOCGWINSZ and TIOCGSIZE
#endif
   NYD2_IN;

#ifdef mx_HAVE_TCGETWINSIZE
   if(tcgetwinsize(fileno(mx_tty_fp), &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGWINSZ
   if(ioctl(fileno(mx_tty_fp), TIOCGWINSZ, &ws) == -1)
      ws.ws_col = ws.ws_row = 0;
#elif defined TIOCGSIZE
   if(ioctl(fileno(mx_tty_fp), TIOCGSIZE, &ws) == -1)
      ts.ts_lines = ts.ts_cols = 0;
#endif

#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
   if(ws.ws_row != 0)
      tiosdp->tiosd_height = tiosdp->tiosd_real_height = ws.ws_row;
#elif defined TIOCGSIZE
   if(ts.ts_lines != 0)
      tiosdp->tiosd_height = tiosdp->tiosd_real_height = ts.ts_lines;
#endif
   else{
      speed_t ospeed;

      /* We use the following algorithm for the fallback height:
       * If baud rate < 1200, use  9
       * If baud rate = 1200, use 14
       * If baud rate > 1200, use VAL_HEIGHT */
      ospeed = ((tcgetattr(fileno(mx_tty_fp), &tbuf) == -1)
            ? B9600 : cfgetospeed(&tbuf));

      if(ospeed < B1200)
         tiosdp->tiosd_height = 9;
      else if(ospeed == B1200)
         tiosdp->tiosd_height = 14;
      else
         tiosdp->tiosd_height = a_TERMIOS_DEFAULT_HEIGHT;

      tiosdp->tiosd_real_height = a_TERMIOS_DEFAULT_HEIGHT;
   }

   if(0 == (
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
       tiosdp->tiosd_width = ws.ws_col
#elif defined TIOCGSIZE
       tiosdp->tiosd_width = ts.ts_cols
#endif
         ))
      tiosdp->tiosd_width = a_TERMIOS_DEFAULT_WIDTH;

   NYD2_OU;
}

void
mx_termios_controller_setup(enum mx_termios_setup what){
   sigset_t nset, oset;
   NYD_IN;

   if(what == mx_TERMIOS_SETUP_STARTUP){
      a_termios_g.tiosg_envp = &a_termios_g.tiosg_env_base;

      sigfillset(&nset);
      sigprocmask(SIG_BLOCK, &nset, &oset);

      a_termios_g.tiosg_otstp = safe_signal(SIGTSTP, &a_termios_onsig);
      a_termios_g.tiosg_ottin = safe_signal(SIGTTIN, &a_termios_onsig);
      a_termios_g.tiosg_ottou = safe_signal(SIGTTOU, &a_termios_onsig);
      a_termios_g.tiosg_ocont = safe_signal(SIGCONT, &a_termios_onsig);

      /* This assumes oset contains nothing but SIGCHLD, so to say */
      sigdelset(&oset, SIGTSTP);
      sigdelset(&oset, SIGTTIN);
      sigdelset(&oset, SIGTTOU);
      sigdelset(&oset, SIGCONT);
      sigprocmask(SIG_SETMASK, &oset, NIL);
   }else{
      /* Semantics are a bit hairy and cast in stone in the manual, and
       * scattered all over the place, at least $COLUMNS, $LINES, -# */
      n_pstate |= n_PS_SIGWINCH_PEND;

      if(!su_state_has(su_STATE_REPRODUCIBLE) &&
            ((n_psonce & n_PSO_INTERACTIVE) ||
               ((n_psonce & n_PSO_TTYANY) && (n_poption & n_PO_BATCH_FLAG)))){
         /* (Also) POSIX: LINES and COLUMNS always override.  These variables
          * are ensured to be positive numbers, so no checking */
         u32 l, c;
         char const *cp;
         boole hadl, hadc;

         ASSERT(mx_termios_dimen.tiosd_height == 0);
         ASSERT(mx_termios_dimen.tiosd_real_height == 0);
         ASSERT(mx_termios_dimen.tiosd_width == 0);
         if(n_psonce & n_PSO_INTERACTIVE){
            /* XXX Yet WINCH after WINCH/CONT, but see POSIX TOSTOP flag */
#if a_TERMIOS_SIGWINCH != -1
            if(safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
               a_termios_g.tiosg_owinch = safe_signal(SIGWINCH,
                     &a_termios_onsig);
#endif
         }

         l = c = 0;
         if((hadl = ((cp = ok_vlook(LINES)) != NIL)))
            su_idec_u32_cp(&l, cp, 0, NIL);
         if((hadc = ((cp = ok_vlook(COLUMNS)) != NIL)))
            su_idec_u32_cp(&c, cp, 0, NIL);

         if(l == 0 || c == 0){
            /* In non-interactive mode, stop now, except for the documented case
             * that both are set but not both have been usable */
            if(!(n_psonce & n_PSO_INTERACTIVE) && (!hadl || !hadc))
               goto jtermsize_default;

            a_termios_dimen_query();
         }

         if(l != 0)
            mx_termios_dimen.tiosd_real_height =
                  mx_termios_dimen.tiosd_height = l;
         if(c != 0)
            mx_termios_dimen.tiosd_width = c;
      }else{
jtermsize_default:
         /* $COLUMNS and $LINES defaults as documented in the manual! */
         mx_termios_dimen.tiosd_height =
               mx_termios_dimen.tiosd_real_height = a_TERMIOS_DEFAULT_HEIGHT;
         mx_termios_dimen.tiosd_width = a_TERMIOS_DEFAULT_WIDTH;
      }
   }

   NYD_OU;
}

mx_termios_on_state_change
mx_termios_on_state_change_set(mx_termios_on_state_change tiossc){
   mx_termios_on_state_change rv;
   NYD_IN;
   ASSERT(a_termios_g.tiosg_envp->tiose_prev != NIL &&
      a_termios_g.tiosg_envp->tiose_cmd != mx_TERMIOS_CMD_HANDS_OFF);

   rv = a_termios_g.tiosg_envp->tiose_on_state_change;
   a_termios_g.tiosg_envp->tiose_on_state_change = tiossc;
   NYD_OU;
   return rv;
}

boole
mx_termios_cmd(u32 tiosc, uz a1){
   /* xxx tcsetattr not correct says manual: would need to requery and check
    * whether all desired changes made it instead! */
   sigset_t nset, oset;
   boole rv;
   struct a_termios_env *tiosep, *tiosep_2free;
   NYD_IN;

   ASSERT_NYD_EXEC((tiosc & mx__TERMIOS_CMD_CTL_MASK) ||
       (tiosc & mx__TERMIOS_CMD_ACT_MASK) == mx_TERMIOS_CMD_RESET ||
      (a_termios_g.tiosg_envp->tiose_prev != NIL &&
       ((tiosc & mx__TERMIOS_CMD_ACT_MASK) == mx_TERMIOS_CMD_RAW ||
        (tiosc & mx__TERMIOS_CMD_ACT_MASK) == mx_TERMIOS_CMD_RAW_TIMEOUT) &&
       (a_termios_g.tiosg_envp->tiose_cmd == mx_TERMIOS_CMD_RAW ||
        a_termios_g.tiosg_envp->tiose_cmd == mx_TERMIOS_CMD_RAW_TIMEOUT)),
      rv = FAL0);
   ASSERT_NYD_EXEC(!(tiosc & mx_TERMIOS_CMD_POP) ||
      a_termios_g.tiosg_envp->tiose_prev != NIL, rv = FAL0);

   if(a_termios_g.tiosg_normal == NIL){
      a_termios_g.tiosg_normal = a_termios_g.tiosg_envp;
      a_termios_g.tiosg_normal->tiose_cmd = mx_TERMIOS_CMD_NORMAL;
      rv = (tcgetattr(fileno(mx_tty_fp),
            &a_termios_g.tiosg_normal->tiose_state) == 0);
      /* XXX always set ECHO and ICANON in our "normal" canonical state */
      a_termios_g.tiosg_normal->tiose_state.c_lflag |= ECHO | ICANON;
   }

   /* Note: RESET only called with signals blocked in main loop handler */
   if(tiosc == mx_TERMIOS_CMD_RESET){
      if((tiosep = a_termios_g.tiosg_envp)->tiose_prev == NIL){
         rv = TRU1;
         goto jleave;
      }
      a_termios_g.tiosg_envp = tiosep->tiose_prev;

      if(!tiosep->tiose_suspended && tiosep->tiose_on_state_change != NIL)
         (*tiosep->tiose_on_state_change)((mx_TERMIOS_STATE_SUSPEND |
            mx_TERMIOS_STATE_POP), 0);

      for(;;){
         su_FREE(tiosep);
         if((tiosep = a_termios_g.tiosg_envp)->tiose_prev == NIL)
            break;
         a_termios_g.tiosg_envp = tiosep->tiose_prev;
      }

      rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state
            ) == 0);

      a_termios_sig_adjust(FAL0);
      goto jleave;
   }else if(a_termios_g.tiosg_envp->tiose_cmd ==
            (tiosc & mx__TERMIOS_CMD_ACT_MASK) &&
         !(tiosc & mx__TERMIOS_CMD_CTL_MASK)){
      rv = TRU1;
      goto jleave;
   }

   if(tiosc & mx_TERMIOS_CMD_PUSH){
      tiosep = su_TCALLOC(struct a_termios_env, 1);
      tiosep->tiose_cmd = (tiosc & mx__TERMIOS_CMD_ACT_MASK);
   }

   sigfillset(&nset);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   tiosep_2free = NIL;
   if(tiosc & mx_TERMIOS_CMD_PUSH){
      if((tiosep->tiose_prev = a_termios_g.tiosg_envp)->tiose_prev == NIL)
         a_termios_sig_adjust(TRU1);
      else if(!a_termios_g.tiosg_envp->tiose_suspended &&
            a_termios_g.tiosg_envp->tiose_on_state_change != NIL){
         tiosep->tiose_suspended = TRU1;
         (*a_termios_g.tiosg_envp->tiose_on_state_change)(
            mx_TERMIOS_STATE_SUSPEND, 0);
      }

      a_termios_g.tiosg_envp = tiosep;
   }else if(tiosc & mx_TERMIOS_CMD_POP){
      tiosep_2free = tiosep = a_termios_g.tiosg_envp;
      ASSERT(tiosep->tiose_prev != NIL);
      a_termios_g.tiosg_envp = tiosep->tiose_prev;

      if(!tiosep->tiose_suspended && tiosep->tiose_on_state_change != NIL)
         (*tiosep->tiose_on_state_change)((mx_TERMIOS_STATE_SUSPEND |
            mx_TERMIOS_STATE_POP), 0);

      if((tiosep = a_termios_g.tiosg_envp)->tiose_prev == NIL)
         a_termios_sig_adjust(FAL0);
      tiosc = tiosep->tiose_cmd | mx_TERMIOS_CMD_POP;
      a1 = tiosep->tiose_a1;
   }else
      tiosep = a_termios_g.tiosg_envp;

   if(tiosep->tiose_prev != NIL)
      su_mem_copy(&tiosep->tiose_state, &a_termios_g.tiosg_normal->tiose_state,
         sizeof(tiosep->tiose_state));
   else
      ASSERT(tiosep->tiose_cmd == mx_TERMIOS_CMD_NORMAL);

   switch((tiosep->tiose_cmd = (tiosc & mx__TERMIOS_CMD_ACT_MASK))){
   default:
   case mx_TERMIOS_CMD_HANDS_OFF:
   case mx_TERMIOS_CMD_NORMAL:
      rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state
            ) == 0);
      break;
   case mx_TERMIOS_CMD_PASSWORD:
      tiosep->tiose_state.c_iflag &= ~(ISTRIP);
      tiosep->tiose_state.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
      rv = (tcsetattr(fileno(mx_tty_fp), TCSAFLUSH, &tiosep->tiose_state
            ) == 0);
      break;
   case mx_TERMIOS_CMD_RAW:
   case mx_TERMIOS_CMD_RAW_TIMEOUT:
      a1 = MIN(U8_MAX, a1);
      tiosep->tiose_a1 = S(u8,a1);
      if((tiosc & mx__TERMIOS_CMD_ACT_MASK) == mx_TERMIOS_CMD_RAW){
         tiosep->tiose_state.c_cc[VMIN] = S(u8,a1);
         tiosep->tiose_state.c_cc[VTIME] = 0;
      }else{
         tiosep->tiose_state.c_cc[VMIN] = 0;
         tiosep->tiose_state.c_cc[VTIME] = S(u8,a1);
      }
      tiosep->tiose_state.c_iflag &= ~(ISTRIP | IGNCR | IXON | IXOFF);
      tiosep->tiose_state.c_lflag &= ~(ECHO /*| ECHOE | ECHONL */|
            ICANON | IEXTEN | ISIG);
      rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state
            ) == 0);
      break;
   }

   if(!(tiosc & mx__TERMIOS_CMD_CTL_MASK) && tiosep->tiose_suspended &&
         tiosep->tiose_on_state_change != NIL)
      (*tiosep->tiose_on_state_change)(mx_TERMIOS_STATE_RESUME, 0);

   /* XXX if(rv)*/
      tiosep->tiose_suspended = FAL0;

   sigprocmask(SIG_SETMASK, &oset, NIL);

   if(tiosep_2free != NIL)
      su_FREE(tiosep_2free);
jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
