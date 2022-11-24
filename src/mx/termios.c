/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of termios.h.
 *@ FIXME everywhere: tcsetattr() generates SIGTTOU when we're not in
 *@ FIXME foreground pgrp, and can fail with EINTR!!
 *@ TODO . SIGINT during HANDS_OFF reaches us nonetheless.
 *@ TODO . _HANDS_OFF as well as the stack based approach as such is nonsense.
 *@ TODO   It might work well for this MUA, but in general termios_ctx should
 *@ TODO   be a public struct with a lifetime, and activate/suspend methods.
 *@ TODO   It shall be up to the callers how this is managed, we can emit some
 *@ TODO   state events to let them decide for good.
 *@ TODO   Handling children which take over terminal via HANDS_OFF is bad:
 *@ TODO   instead, we need to have a notion of background and foreground,
 *@ TODO   and ensure the terminal is in normal mode when going backward.
 *@ TODO   What children do is up to them, managing them in stack: impossible
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

#include "mx/sigs.h"
#include "mx/tty.h"

#include "mx/termios.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Problem: VAL_ configuration values are strings, we need numbers */
#define a_TERMIOS_DEFAULT_HEIGHT \
	(VAL_HEIGHT[1] == '\0' ? (VAL_HEIGHT[0] - '0') : (VAL_HEIGHT[2] == '\0' \
		? ((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) \
		: (((VAL_HEIGHT[0] - '0') * 10 + (VAL_HEIGHT[1] - '0')) * 10 + (VAL_HEIGHT[2] - '0'))))
#define a_TERMIOS_DEFAULT_WIDTH \
	(VAL_WIDTH[1] == '\0' ? (VAL_WIDTH[0] - '0') : (VAL_WIDTH[2] == '\0' \
		? ((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) \
		: (((VAL_WIDTH[0] - '0') * 10 + (VAL_WIDTH[1] - '0')) * 10 + (VAL_WIDTH[2] - '0'))))

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
	u8 tiose__pad[1];
	/*s32 tiose_pgrp;*/ /* In HANDS_OFF mode */
	su_64( u8 tiose__pad2[4]; )
	mx_termios_on_state_change tiose_on_state_change;
	up tiose_osc_cookie;
	struct termios tiose_state;
};

struct a_termios_g{
	struct a_termios_env *tiosg_envp;
	/* If outermost == normal state; used as init switch, too */
	struct a_termios_env *tiosg_normal;
	struct a_termios_env *tiosg_pend_free;
	/*s32 tiosg_pgrp;
	 *u8 tiosg__pad[4];*/
	n_sighdl_t tiosg_otstp;
	n_sighdl_t tiosg_ottin;
	n_sighdl_t tiosg_ottou;
	n_sighdl_t tiosg_ocont;
#if a_TERMIOS_SIGWINCH != -1
	n_sighdl_t tiosg_owinch;
#endif
	/* Remaining signals only when in password/raw mode */
	n_sighdl_t tiosg_ohup;
	n_sighdl_t tiosg_oint;
	n_sighdl_t tiosg_oquit;
	n_sighdl_t tiosg_oterm;
	struct a_termios_env tiosg_env_base;
};

static struct a_termios_g a_termios_g;

struct mx_termios_dimension mx_termios_dimen;

/* */
static void a_termios_sig_adjust(boole condome);

/* */
static void a_termios_onsig(int sig);

/* */
SINLINE boole a_termios_norm_query(void);

/* Do the system-dependent dance on getting used to terminal dimension */
static void a_termios_dimen_query(struct mx_termios_dimension *tiosdp);

static void
a_termios_sig_adjust(boole condome){
	NYD2_IN;

	if(condome){
		if((a_termios_g.tiosg_ohup = safe_signal(SIGHUP, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGHUP, &a_termios_onsig);
		if((a_termios_g.tiosg_oint = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN)
			safe_signal(SIGINT, &a_termios_onsig);
		safe_signal(SIGQUIT, &a_termios_onsig);
		safe_signal(SIGTERM, &a_termios_onsig);
	}else{
		if(a_termios_g.tiosg_ohup != SIG_IGN)
			safe_signal(SIGHUP, a_termios_g.tiosg_ohup);
		if(a_termios_g.tiosg_oint != SIG_IGN)
			safe_signal(SIGINT, a_termios_g.tiosg_oint);
		safe_signal(SIGQUIT, a_termios_g.tiosg_oquit);
		safe_signal(SIGTERM, a_termios_g.tiosg_oterm);
	}

	NYD2_OU;
}

SINLINE boole
a_termios_norm_query(void){
	boole rv;
	/*NYD2_IN;*/

	rv = (tcgetattr(fileno(mx_tty_fp), &a_termios_g.tiosg_normal->tiose_state) == 0);
	/* XXX always set ECHO and ICANON in our "normal" canonical state */
	a_termios_g.tiosg_normal->tiose_state.c_lflag |= ECHO | ICANON;

	/*NYD2_OU;*/
	return rv;
}

static void
a_termios_onsig(int sig){
	n_sighdl_t oact, myact;
	sigset_t nset;
	struct a_termios_env *tiosep;
	boole jobsig, dopop;
	NYD; /* Signal handler */

	if(sig == a_TERMIOS_SIGWINCH)
		goto jsigwinch;

#undef a_X
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

	dopop = FAL0;
	tiosep = a_termios_g.tiosg_envp;

	if(!jobsig || sig != SIGCONT){
		if(!tiosep->tiose_suspended){
			tiosep->tiose_suspended = TRU1;

			if(tiosep->tiose_on_state_change != NIL){
				dopop = (*tiosep->tiose_on_state_change)(tiosep->tiose_osc_cookie,
						(mx_TERMIOS_STATE_SUSPEND | mx_TERMIOS_STATE_SIGNAL |
							(jobsig ? mx_TERMIOS_STATE_JOB_SIGNAL : 0)), sig);
				if(dopop)
					a_termios_g.tiosg_envp = tiosep->tiose_prev;
			}

			if(tiosep->tiose_cmd != mx_TERMIOS_CMD_NORMAL)
				(void)tcsetattr(fileno(mx_tty_fp), TCSAFLUSH, &a_termios_g.tiosg_normal->tiose_state);
		}
	}

	/* If we shall pop this level link context in a list for later freeing in a more regular context */
	if(dopop){
		tiosep->tiose_prev = a_termios_g.tiosg_pend_free;
		a_termios_g.tiosg_pend_free = tiosep;
	}

	if(jobsig || (tiosep->tiose_cmd != mx_TERMIOS_CMD_HANDS_OFF &&
			oact != SIG_DFL && oact != SIG_IGN && oact != SIG_ERR)){
		myact = safe_signal(sig, oact);

		sigemptyset(&nset);
		sigaddset(&nset, sig);
		sigprocmask(SIG_UNBLOCK, &nset, NIL);
		n_raise(sig);
		sigprocmask(SIG_BLOCK, &nset, NIL);

		safe_signal(sig, myact);

		/* When we come here we shall continue */
		if(!dopop && tiosep->tiose_suspended){
			tiosep->tiose_suspended = FAL0;

			if(tiosep->tiose_cmd != mx_TERMIOS_CMD_HANDS_OFF){
				/* Requery our notion of what is "normal", so that possible user
				 * adjustments which happened in the meantime are kept */
				a_termios_norm_query();

				if(tiosep->tiose_cmd != mx_TERMIOS_CMD_NORMAL)
					(void)tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state);
			}

			if(tiosep->tiose_on_state_change != NIL)
				(*tiosep->tiose_on_state_change)(tiosep->tiose_osc_cookie,
					(mx_TERMIOS_STATE_RESUME | mx_TERMIOS_STATE_SIGNAL |
						(jobsig ? mx_TERMIOS_STATE_JOB_SIGNAL : 0)), sig);
		}

#if a_TERMIOS_SIGWINCH == -1
		if(sig == SIGCONT)
			goto jsigwinch;
#endif
	}

jleave:
	return;

jsigwinch:
	if(n_psonce & n_PSO_INTERACTIVE){
		a_termios_dimen_query(&mx_termios_dimen);
		if(mx_termios_dimen.tiosd_width > 1 && !(n_psonce & n_PSO_TERMCAP_FULLWIDTH))
			--mx_termios_dimen.tiosd_width;
		n_pstate |= n_PS_SIGWINCH_PEND;
	}
	goto jleave;
}

static void
a_termios_dimen_query(struct mx_termios_dimension *tiosdp){
	struct termios tbuf;
#if defined mx_HAVE_TCGETWINSIZE || defined TIOCGWINSZ
	struct winsize ws;
#elif defined TIOCGSIZE
	struct ttysize ts;
#else
# error One of tcgetwinsize(3), TIOCGWINSZ and TIOCGSIZE
#endif
	/*NYD2_IN;*/

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
		ospeed = ((tcgetattr(fileno(mx_tty_fp), &tbuf) == -1) ? B9600 : cfgetospeed(&tbuf));

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
		 tiosdp->tiosd_width = tiosdp->tiosd_real_width = ws.ws_col
#elif defined TIOCGSIZE
		 tiosdp->tiosd_width = tiosdp->tiosd_real_width = ts.ts_cols
#endif
			))
		tiosdp->tiosd_width = tiosdp->tiosd_real_width = a_TERMIOS_DEFAULT_WIDTH;

	/*NYD2_OU;*/
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
		if(!su_state_has(su_STATE_REPRODUCIBLE) && ((n_psonce & n_PSO_INTERACTIVE) ||
					((n_psonce & n_PSO_TTYANY) && (n_poption & n_PO_BATCH_FLAG)))){
			/* (Also) POSIX: LINES and COLUMNS always override.  Variables ensured to be positive numbers */
			u32 l, c;
			char const *cp;
			boole hadl, hadc;

			ASSERT(mx_termios_dimen.tiosd_height == 0);
			ASSERT(mx_termios_dimen.tiosd_real_height == 0);
			ASSERT(mx_termios_dimen.tiosd_width == 0);
			ASSERT(mx_termios_dimen.tiosd_real_width == 0);
			if(n_psonce & n_PSO_INTERACTIVE){
				/* XXX Yet WINCH after WINCH/CONT, but see POSIX TOSTOP flag */
#if a_TERMIOS_SIGWINCH != -1
				if(safe_signal(SIGWINCH, SIG_IGN) != SIG_IGN)
					a_termios_g.tiosg_owinch = safe_signal(SIGWINCH, &a_termios_onsig);
#endif
			}

			l = c = 0;
			if((hadl = ((cp = ok_vlook(LINES)) != NIL)))
				su_idec_u32_cp(&l, cp, 0, NIL);
			if((hadc = ((cp = ok_vlook(COLUMNS)) != NIL)))
				su_idec_u32_cp(&c, cp, 0, NIL);

			if(l == 0 || c == 0){
				/* In non-interactive mode, stop now, except for the documented case that both are set
				 * but not both have been usable */
				if(!(n_psonce & n_PSO_INTERACTIVE) && !((n_psonce & n_PSO_TTYANY) &&
							(n_poption & n_PO_BATCH_FLAG)) && (!hadl || !hadc))
					goto jtermsize_default;

				a_termios_dimen_query(&mx_termios_dimen);
			}

			if(l != 0)
				mx_termios_dimen.tiosd_real_height = mx_termios_dimen.tiosd_height = l;
			if(c != 0)
				mx_termios_dimen.tiosd_width = mx_termios_dimen.tiosd_real_width = c;
		}else{
jtermsize_default:
			/* $COLUMNS and $LINES defaults as documented in the manual! */
			mx_termios_dimen.tiosd_height = mx_termios_dimen.tiosd_real_height = a_TERMIOS_DEFAULT_HEIGHT;
			mx_termios_dimen.tiosd_width = mx_termios_dimen.tiosd_real_width = a_TERMIOS_DEFAULT_WIDTH;
		}

		/* Note: for this first invocation this will always trigger.  If we have termcap support then
		 * termcap_init() will undo this if FULLWIDTH is set after termcap is initialized.  We have to evaluate
		 * it now since cmds may run pre-termcap ... */
		if(mx_termios_dimen.tiosd_width > 1)
			--mx_termios_dimen.tiosd_width;
		n_pstate |= n_PS_SIGWINCH_PEND;
	}

	NYD_OU;
}

void
mx_termios_on_state_change_set(mx_termios_on_state_change tiossc, up cookie){
	NYD2_IN;
	ASSERT(a_termios_g.tiosg_envp->tiose_prev != NIL); /* Not in base level */

	a_termios_g.tiosg_envp->tiose_on_state_change = tiossc;
	a_termios_g.tiosg_envp->tiose_osc_cookie = cookie;

	NYD2_OU;
}

boole
mx_termios_cmd(u32 tiosc, uz a1){
	/* xxx tcsetattr not correct says manual: would need to requery and check whether all desired changes made it */
	boole rv;
	struct a_termios_env *tiosep_2free, *tiosep;
	NYD_IN;

	tiosep_2free = NIL;
	UNINIT(tiosep, NIL);

	ASSERT_NYD_EXEC((tiosc & mx__TERMIOS_CMD_CTL_MASK) ||
		tiosc == mx_TERMIOS_CMD_RESET ||
		/*(tiosc == mx_TERMIOS_CMD_SET_PGRP && a_termios_g.tiosg_envp->tiose_cmd == mx_TERMIOS_CMD_HANDS_OFF) ||*/
		(a_termios_g.tiosg_envp->tiose_prev != NIL &&
			((tiosc & mx__TERMIOS_CMD_ACT_MASK) == mx_TERMIOS_CMD_RAW ||
			 (tiosc & mx__TERMIOS_CMD_ACT_MASK) == mx_TERMIOS_CMD_RAW_TIMEOUT) &&
			(a_termios_g.tiosg_envp->tiose_cmd == mx_TERMIOS_CMD_RAW ||
			 a_termios_g.tiosg_envp->tiose_cmd == mx_TERMIOS_CMD_RAW_TIMEOUT)),
		rv = FAL0);
	ASSERT_NYD_EXEC(!(tiosc & mx_TERMIOS_CMD_POP) || a_termios_g.tiosg_envp->tiose_prev != NIL, rv = FAL0);
	ASSERT_NYD_EXEC((tiosc & mx__TERMIOS_CMD_ACT_MASK) ||
			(tiosc == mx_TERMIOS_CMD_RESET /*|| tiosc == mx_TERMIOS_CMD_SET_PGRP*/),
		rv = FAL0);

	if(a_termios_g.tiosg_normal == NIL){
		a_termios_g.tiosg_normal = a_termios_g.tiosg_envp;
		a_termios_g.tiosg_normal->tiose_cmd = mx_TERMIOS_CMD_NORMAL;
		/*rv =*/ a_termios_norm_query();
	}

	/* Note: RESET only called with signals blocked in main loop handler */
	if(tiosc == mx_TERMIOS_CMD_RESET){
		boole isfirst;

		if((tiosep = a_termios_g.tiosg_envp)->tiose_prev == NIL){
			rv = TRU1;
			goto jleave;
		}
		rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state) == 0);

		for(isfirst = TRU1;; isfirst = FAL0){
			a_termios_g.tiosg_envp = tiosep->tiose_prev;

			if(isfirst || !tiosep->tiose_suspended){
				if(tiosep->tiose_on_state_change != NIL)
					(*tiosep->tiose_on_state_change)(tiosep->tiose_osc_cookie,
						(isfirst ? mx_TERMIOS_STATE_SUSPEND : 0 | mx_TERMIOS_STATE_POP), 0);
			}

			su_FREE(tiosep);

			if((tiosep = a_termios_g.tiosg_envp)->tiose_prev == NIL)
				break;
		}

		a_termios_sig_adjust(FAL0);
		goto jleave;
#if 0
	}else if(tiosc == mx_TERMIOS_CMD_SET_PGRP){
		if(a_termios_g.tiosg_pgrp == 0)
			a_termios_g.tiosg_pgrp = getpgrp();
		rv = (tcsetpgrp(fileno(mx_tty_fp), S(pid_t,a1)) == 0);
		goto jleave;
#endif
	}else if(a_termios_g.tiosg_envp->tiose_cmd == (tiosc & mx__TERMIOS_CMD_ACT_MASK) &&
			!(tiosc & mx__TERMIOS_CMD_CTL_MASK)){
		rv = TRU1;
		goto jleave;
	}

	if(tiosc & mx_TERMIOS_CMD_PUSH){
		tiosep = su_TCALLOC(struct a_termios_env, 1);
		tiosep->tiose_cmd = (tiosc & mx__TERMIOS_CMD_ACT_MASK);
	}

	mx_sigs_all_holdx();

	if(tiosc & mx_TERMIOS_CMD_PUSH){
		if((tiosep->tiose_prev = a_termios_g.tiosg_envp)->tiose_prev == NIL)
			a_termios_sig_adjust(TRU1);
		else{
			if(!a_termios_g.tiosg_envp->tiose_suspended){
				a_termios_g.tiosg_envp->tiose_suspended = TRU1;
				if(a_termios_g.tiosg_envp->tiose_on_state_change != NIL)
					(*a_termios_g.tiosg_envp->tiose_on_state_change)(
						a_termios_g.tiosg_envp->tiose_osc_cookie,
						mx_TERMIOS_STATE_SUSPEND, 0);
			}
		}

		a_termios_g.tiosg_envp = tiosep;
	}else if(tiosc & mx_TERMIOS_CMD_POP){
		tiosep_2free = tiosep = a_termios_g.tiosg_envp;
		a_termios_g.tiosg_envp = tiosep->tiose_prev;
		tiosep->tiose_prev = NIL;

		if(!tiosep->tiose_suspended && tiosep->tiose_on_state_change != NIL)
			(*tiosep->tiose_on_state_change)(tiosep->tiose_osc_cookie,
				(mx_TERMIOS_STATE_SUSPEND | mx_TERMIOS_STATE_POP), 0);

		if((tiosep = a_termios_g.tiosg_envp)->tiose_prev == NIL)
			a_termios_sig_adjust(FAL0);

		tiosc = tiosep->tiose_cmd | mx_TERMIOS_CMD_POP;
		a1 = tiosep->tiose_a1;
	}else
		tiosep = a_termios_g.tiosg_envp;

	if(tiosep->tiose_prev != NIL)
		su_mem_copy(&tiosep->tiose_state, &a_termios_g.tiosg_normal->tiose_state, sizeof(tiosep->tiose_state));
	else
		ASSERT(tiosep->tiose_cmd == mx_TERMIOS_CMD_NORMAL);

	switch((tiosep->tiose_cmd = (tiosc & mx__TERMIOS_CMD_ACT_MASK))){
	default:
	case mx_TERMIOS_CMD_HANDS_OFF:
		if(!(tiosc & mx_TERMIOS_CMD_PUSH)){
			rv = TRU1;
			break;
		}
		/* FALLTHRU */
	case mx_TERMIOS_CMD_NORMAL:
		rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state) == 0);
		break;
	case mx_TERMIOS_CMD_PASSWORD:
		tiosep->tiose_state.c_iflag &= ~(ISTRIP);
		tiosep->tiose_state.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
		rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state) == 0);
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
		tiosep->tiose_state.c_lflag &= ~(ECHO /*| ECHOE | ECHONL */| ICANON | IEXTEN | ISIG);
		rv = (tcsetattr(fileno(mx_tty_fp), TCSADRAIN, &tiosep->tiose_state) == 0);
		break;
	}

	if(/*!(tiosc & mx__TERMIOS_CMD_CTL_MASK) &&*/ tiosep->tiose_suspended && tiosep->tiose_on_state_change != NIL)
		(*tiosep->tiose_on_state_change)(tiosep->tiose_osc_cookie, mx_TERMIOS_STATE_RESUME, 0);

	/* XXX if(rv)*/{
		tiosep->tiose_suspended = FAL0;
	}

	if(tiosep_2free != NIL)
		tiosep_2free->tiose_prev = a_termios_g.tiosg_pend_free;
	else
		tiosep_2free = a_termios_g.tiosg_pend_free;
	a_termios_g.tiosg_pend_free = NIL;

	mx_sigs_all_rele();

jleave:
	while((tiosep = tiosep_2free) != NIL){
		tiosep_2free = tiosep->tiose_prev;
		su_FREE(tiosep);
	}

	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_TERMIOS
/* s-itt-mode */
