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

CTAV(mx_TERMIOS_CMD_NORMAL == 0);

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

struct a_termios_g{
   u8 tiosg_state_cmd;
   u8 tiosg__pad[1];
   boole tiosg_norm_init;
   boole tiosg_norm_need;
   int volatile tiosg_hadsig;
   mx_termios_on_signal tiosg_on_signal;
   n_sighdl_t tiosg_otstp;
   n_sighdl_t tiosg_ottin;
   n_sighdl_t tiosg_ottou;
   n_sighdl_t tiosg_ocont;
#if a_TERMIOS_SIGWINCH != -1
   n_sighdl_t tiosg_owinch;
#endif
   /* Remaining signals only in password/raw mode */
   n_sighdl_t tiosg_ohup;
   n_sighdl_t tiosg_oint;
   n_sighdl_t tiosg_oquit;
   n_sighdl_t tiosg_oterm;
   struct termios tiosg_normal;
   struct termios tiosg_other;
};

static struct a_termios_g a_termios_g;
FILE *a_termios_FIXME;

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
      ASSERT(a_termios_g.tiosg_state_cmd == mx_TERMIOS_CMD_NORMAL);
      a_termios_g.tiosg_ohup = safe_signal(SIGHUP, &a_termios_onsig);
      a_termios_g.tiosg_oint = safe_signal(SIGINT, &a_termios_onsig);
      a_termios_g.tiosg_oquit = safe_signal(SIGQUIT, &a_termios_onsig);
      a_termios_g.tiosg_oterm = safe_signal(SIGTERM, &a_termios_onsig);
   }else{
      ASSERT(a_termios_g.tiosg_state_cmd != mx_TERMIOS_CMD_NORMAL);
      safe_signal(SIGHUP, a_termios_g.tiosg_ohup);
      safe_signal(SIGINT, a_termios_g.tiosg_oint);
      safe_signal(SIGQUIT, a_termios_g.tiosg_oquit);
      safe_signal(SIGTERM, a_termios_g.tiosg_oterm);
   }
   NYD2_OU;
}

static void
a_termios_onsig(int sig){
   n_sighdl_t oldact;
   sigset_t nset;
   boole inredo, hadsig;
   NYD; /* Signal handler */

   inredo = FAL0;

   hadsig = (a_termios_g.tiosg_hadsig != 0);
   a_termios_g.tiosg_hadsig = 1;

   switch(sig){
   case a_TERMIOS_SIGWINCH:
jsigwinch:
      n_pstate |= n_PS_SIGWINCH_PEND;
      a_termios_dimen_query();
      break;
   case SIGTSTP:
   case SIGTTIN:
   case SIGTTOU:
   case SIGCONT:
      if(!a_termios_g.tiosg_norm_init)
         goto jleave;
      if(0){
         /* FALLTHRU*/
   case SIGHUP:
   case SIGINT:
   case SIGQUIT:
   case SIGTERM:
         ASSERT(a_termios_g.tiosg_norm_init);
      }
      if(a_termios_g.tiosg_state_cmd != mx_TERMIOS_CMD_NORMAL){
         if(tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &a_termios_g.tiosg_normal
               ) == 0){
            a_termios_g.tiosg_state_cmd = mx_TERMIOS_CMD_NORMAL;
            a_termios_g.tiosg_norm_need = TRU1;
         }
      }
      break;
   }

   if(!inredo){
      /* Do we need two pre calls? one blocked, one unblocked?*/
      if(a_termios_g.tiosg_on_signal != NIL)
         (*a_termios_g.tiosg_on_signal)(sig, FAL0);

      if(sig != a_TERMIOS_SIGWINCH){
         oldact = safe_signal(sig, SIG_DFL);

         sigemptyset(&nset);
         sigaddset(&nset, sig);
         sigprocmask(SIG_UNBLOCK, &nset, NULL);
         n_raise(sig);
         sigprocmask(SIG_BLOCK, &nset, NULL);

         safe_signal(sig, oldact);
      }

      if(a_termios_g.tiosg_on_signal != NIL)
         (*a_termios_g.tiosg_on_signal)(sig, TRU1);

      if(sig == SIGCONT){
         inredo = TRU1;
         goto jsigwinch;
      }
   }

jleave:;
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

mx_termios_on_signal
mx_termios_set_on_signal(mx_termios_on_signal hdl){
   mx_termios_on_signal rv;
   NYD_IN;

   rv = a_termios_g.tiosg_on_signal;
   a_termios_g.tiosg_on_signal = hdl;
   NYD_OU;
   return rv;
}

boole
mx_termios_cmd(enum mx_termios_cmd cmd, uz a1){
   /* xxx tcsetattr not correct says manual: would need to requery and check
    * whether all desired changes made it instead! */
   enum{
      a_NONE,
      a_SIGS_DO = 1u<<0,
      a_SIGS_BLOCKED = 1u<<1,
      a_SIGS_CONDOME = 1u<<2
   };

   sigset_t nset, oset;
   boole rv;
   u8 f;
   NYD_IN;

   if(!a_termios_g.tiosg_norm_init){
      a_termios_g.tiosg_norm_init = TRU1;
      if(cmd != mx_TERMIOS_CMD_QUERY)
         mx_termios_cmdx(mx_TERMIOS_CMD_QUERY);
   }

   f = a_NONE;

   if(a_termios_g.tiosg_state_cmd != mx_TERMIOS_CMD_NORMAL ||
         cmd > mx_TERMIOS_CMD_QUERY){
      f = a_SIGS_DO | a_SIGS_BLOCKED;
      sigfillset(&nset);
      sigprocmask(SIG_BLOCK, &nset, &oset); /* (delays ASSERT()s..) */
   }

   switch(cmd){
   default:
   case mx_TERMIOS_CMD_QUERY:
      rv = (tcgetattr(fileno(mx_tty_fp), &a_termios_g.tiosg_normal) == 0);
      /* XXX always set ECHO and ICANON in our "normal" canonical state */
      a_termios_g.tiosg_normal.c_lflag |= ECHO | ICANON;
      break;
   case mx_TERMIOS_CMD_NORMAL:
      if(a_termios_g.tiosg_state_cmd == mx_TERMIOS_CMD_NORMAL)
         rv = TRU1;
      else
         rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN,
               &a_termios_g.tiosg_normal) == 0);
      break;
   case mx_TERMIOS_CMD_PASSWORD:
      ASSERT(a_termios_g.tiosg_state_cmd == mx_TERMIOS_CMD_NORMAL);
      ASSERT(f & a_SIGS_DO);

      su_mem_copy(&a_termios_g.tiosg_other, &a_termios_g.tiosg_normal,
         sizeof(a_termios_g.tiosg_normal));
      a_termios_g.tiosg_other.c_iflag &= ~(ISTRIP);
      a_termios_g.tiosg_other.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);

      rv = (tcsetattr(fileno(mx_tty_fp), TCSAFLUSH, &a_termios_g.tiosg_other
            ) == 0);
      f |= a_SIGS_CONDOME;
      break;
   case mx_TERMIOS_CMD_RAW:
   case mx_TERMIOS_CMD_RAW_TIMEOUT:
      ASSERT(a_termios_g.tiosg_state_cmd == mx_TERMIOS_CMD_NORMAL ||
         a_termios_g.tiosg_state_cmd == mx_TERMIOS_CMD_RAW ||
         a_termios_g.tiosg_state_cmd == mx_TERMIOS_CMD_RAW_TIMEOUT);
      ASSERT(f & a_SIGS_DO);

      if(cmd == a_termios_g.tiosg_state_cmd){
         f &= ~a_SIGS_DO;
         rv = TRU1;
         break;
      }

      su_mem_copy(&a_termios_g.tiosg_other, &a_termios_g.tiosg_normal,
         sizeof(a_termios_g.tiosg_normal));
      a1 = MIN(U8_MAX, a1);
      if(cmd == mx_TERMIOS_CMD_RAW){
         a_termios_g.tiosg_other.c_cc[VMIN] = S(u8,a1);
         a_termios_g.tiosg_other.c_cc[VTIME] = 0;
      }else{
         a_termios_g.tiosg_other.c_cc[VMIN] = 0;
         a_termios_g.tiosg_other.c_cc[VTIME] = S(u8,a1);
      }
      a_termios_g.tiosg_other.c_iflag &= ~(ISTRIP | IGNCR | IXON | IXOFF);
      a_termios_g.tiosg_other.c_lflag &= ~(ECHO /*| ECHOE | ECHONL */|
            ICANON | IEXTEN | ISIG);

      rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &a_termios_g.tiosg_other
            ) == 0);
      if(a_termios_g.tiosg_state_cmd != mx_TERMIOS_CMD_NORMAL)
         f &= ~a_SIGS_DO;
      f |= a_SIGS_CONDOME;
      break;
   }

   if(rv){
      if(f & a_SIGS_DO)
         a_termios_sig_adjust((f & a_SIGS_CONDOME) != 0);
      if(cmd != mx_TERMIOS_CMD_QUERY)
         a_termios_g.tiosg_state_cmd = cmd;
   }

   if(f & a_SIGS_BLOCKED)
      sigprocmask(SIG_SETMASK, &oset, NIL);

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
