/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Terminal capability interaction.
 *
 * Copyright (c) 2016 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_TERMCAP_H
#define mx_TERMCAP_H

#include <mx/nail.h>

/* Switch indicating necessity of terminal access interface.
 * (As of the time of this writing TERMCAP only with available MLE, but..) */
#if defined mx_HAVE_TERMCAP || defined mx_HAVE_COLOUR || defined mx_HAVE_MLE
# define mx_HAVE_TCAP
#endif
#ifdef mx_HAVE_TCAP

#define mx_HEADER
#include <su/code-in.h>

struct mx_termcap_value;

/* termcap_resume() / termcap_suspend(): flags */
enum mx_termcap_mode{
	/* Base modes */
	mx_TERMCAP_MODE_BASE, /* keypad */
	mx_TERMCAP_MODE_CA = 1u<<0, /* BASE + *termcap-ca-mode* IFF applicable */

	/* Additional flags */

	/* Consumer knows how to handle whatever the terminal throws at her:
	 * - bracketed paste mode is enabled if available, thus _PS[data]_PE may occur */
	mx_TERMCAP_MODE_SMART = 1u<<7,

	/* When destroying we want to turn off anything */
	mx_TERMCAP_MODE_TEARDOWN_MASK = 0xFFu
};

enum mx_termcap_captype{
	mx_TERMCAP_CAPTYPE_NONE,
	/* Internally we share the bitspace, so ensure no value ends up as 0 */
	mx_TERMCAP_CAPTYPE_BOOL,
	mx_TERMCAP_CAPTYPE_NUMERIC,
	mx_TERMCAP_CAPTYPE_STRING,
	mx__TERMCAP_CAPTYPE_MAX1
};

/* Termcap commands; different to queries commands perform actions.
 * Commands are resolved upon init time.
 *
 * Note this is parsed by make-tcap-map.pl, which expects the syntax "CONSTANT, COMMENT" where COMMENT is
 * "Capname/TCap-Code, TYPE[, FLAGS]", and one of Capname and TCap-Code may be the string "-" meaning ENOENT;
 * a | vertical bar or end-of-comment ends processing; see termcap.c.
 * We may use the free-form part after | for the "Variable String" and notes on necessary termcap_cmd() arguments;
 * if those are in [] brackets they are not regular but are only used when the command, i.e., its effect, is somehow
 * simulated / faked by a built-in fallback implementation.
 * Availability of built-in fallback indicated by leading @ (at-sign) */
enum mx_termcap_cmd{
	/* _MODE_BASE: */
	mx_TERMCAP_CMD_ks, /* smkx/ks, STRING | keypad_xmit: -,- */
	mx_TERMCAP_CMD_ke, /* rmkx/ke, STRING | keypad_local: -,- */
	/* _MODE_CA: */
	mx_TERMCAP_CMD_te, /* rmcup/te, STRING | exit_ca_mode: -,- */
	mx_TERMCAP_CMD_ti, /* smcup/ti, STRING | enter_ca_mode: -,- */
	/* _MODE_SMART: */
# ifdef mx_HAVE_KEY_BINDINGS /* for now */
	mx_TERMCAP_CMD_BE, /* BE/-, STRING | enable bracketed paste */
	mx_TERMCAP_CMD_BD, /* BD/-, STRING | disable bracketed paste */
# endif

# ifdef mx_HAVE_MLE
	mx_TERMCAP_CMD_ce, /* el/ce, STRING | @ clr_eol: [start-column],- */
	mx_TERMCAP_CMD_ch, /* hpa/ch, STRING, IDX1 | column_address: column,- */
	mx_TERMCAP_CMD_cr, /* cr/cr, STRING | @ carriage_return: -,- */
	mx_TERMCAP_CMD_le, /* cub1/le, STRING, CNT | @ cursor_left: count,- */
	mx_TERMCAP_CMD_nd, /* cuf1/nd, STRING, CNT | @ cursor_right: count,- */

	mx_TERMCAP_CMD_cl, /* clear/cl, STRING | clear_screen(+home): -,- */
	/* [cl == ho+cd; even though without TERMCAP user could do that, always provide them] */
	mx_TERMCAP_CMD_cd, /* ed/cd, STRING | clr_eos: -,- */
	mx_TERMCAP_CMD_ho, /* home/ho, STRING | cursor_home: -,- */
# endif

	mx__TERMCAP_CMD_MAX1,
	mx__TERMCAP_CMD_MASK = (1u<<24) - 1,

	mx_TERMCAP_CMD_FLAG_CA_MODE = 1u<<29, /* Only perform command when ca-mode is used */
	mx_TERMCAP_CMD_FLAG_FLUSH = 1u<<30 /* I/O should be flushed after command completed */
};

/* Termcap queries; a query is a command that returns a struct n_termcap_value.
 * Queries are resolved once used first, and more often than commands have no termcap(5) equivalence,
 * therefore let us use terminfo(5) names.
 *
 * Note this is parsed by make-tcap-map.pl, which expects the syntax "CONSTANT, COMMENT" where COMMENT is
 * "Capname/TCap-Code, TYPE[, FLAGS]", and one of Capname and TCap-Code may be the string "-" meaning ENOENT;
 * a | vertical bar or end-of-comment ends processing; see termcap.c.
 * We may use the free-form part after | for the "Variable String" and notes.
 * The "xkey | X:" keys are Dickey's xterm extensions, see (our) manual */
enum mx_termcap_query{
	mx_TERMCAP_QUERY_am, /* am/am, BOOL | auto_right_margin */
	mx_TERMCAP_QUERY_sam, /* sam/YE, BOOL | semi_auto_right_margin */
	mx_TERMCAP_QUERY_xenl, /* xenl/xn, BOOL | eat_newline_glitch */
	/* _MODE_SMART: */
# ifdef mx_HAVE_KEY_BINDINGS /* for now */
	mx_TERMCAP_QUERY_PS, /* PS/-, STRING | begin bracketed paste */
	mx_TERMCAP_QUERY_PE, /* PE/-, STRING | end bracketed paste */
# endif

# ifdef mx_HAVE_COLOUR
	mx_TERMCAP_QUERY_colors, /* colors/Co, NUMERIC | max_colors */
# endif

	/* --make-tcap-map--: only KEY_BINDINGS follow.  DO NOT CHANGE THIS LINE!
	 * (Names must be indexable no matter what mx_HAVE_ options: can have only a single contiguous option part) */

	/* Update the `bind' manual on change! */
# ifdef mx_HAVE_KEY_BINDINGS
	mx_TERMCAP_QUERY_key_backspace, /* kbs/kb, STRING */
	mx_TERMCAP_QUERY_key_dc, /* kdch1/kD, STRING | delete-character */
		mx_TERMCAP_QUERY_key_sdc, /* kDC / *4, STRING | ..shifted */
	mx_TERMCAP_QUERY_key_eol, /* kel/kE, STRING | clear-to-end-of-line */
	mx_TERMCAP_QUERY_key_exit, /* kext/@9, STRING */
	mx_TERMCAP_QUERY_key_ic, /* kich1/kI, STRING | insert character */
		mx_TERMCAP_QUERY_key_sic, /* kIC/#3, STRING | ..shifted */
	mx_TERMCAP_QUERY_key_home, /* khome/kh, STRING */
		mx_TERMCAP_QUERY_key_shome, /* kHOM/#2, STRING | ..shifted */
	mx_TERMCAP_QUERY_key_end, /* kend/@7, STRING */
		mx_TERMCAP_QUERY_key_send, /* kEND / *7, STRING | ..shifted */
	mx_TERMCAP_QUERY_key_npage, /* knp/kN, STRING */
	mx_TERMCAP_QUERY_key_ppage, /* kpp/kP, STRING */
	mx_TERMCAP_QUERY_key_left, /* kcub1/kl, STRING */
		mx_TERMCAP_QUERY_key_sleft, /* kLFT/#4, STRING | ..shifted */
		mx_TERMCAP_QUERY_xkey_aleft, /* kLFT3/-, STRING | X: Alt+left */
		mx_TERMCAP_QUERY_xkey_cleft, /* kLFT5/-, STRING | X: Control+left */
	mx_TERMCAP_QUERY_key_right, /* kcuf1/kr, STRING */
		mx_TERMCAP_QUERY_key_sright, /* kRIT/%i, STRING | ..shifted */
		mx_TERMCAP_QUERY_xkey_aright, /* kRIT3/-, STRING | X: Alt+right */
		mx_TERMCAP_QUERY_xkey_cright, /* kRIT5/-, STRING | X: Control+right */
	mx_TERMCAP_QUERY_key_down, /* kcud1/kd, STRING */
		mx_TERMCAP_QUERY_xkey_sdown, /* kDN/-, STRING | ..shifted */
		mx_TERMCAP_QUERY_xkey_adown, /* kDN3/-, STRING | X: Alt+down */
		mx_TERMCAP_QUERY_xkey_cdown, /* kDN5/-, STRING | X: Control+down */
	mx_TERMCAP_QUERY_key_up, /* kcuu1/ku, STRING */
		mx_TERMCAP_QUERY_xkey_sup, /* kUP/-, STRING | ..shifted */
		mx_TERMCAP_QUERY_xkey_aup, /* kUP3/-, STRING | X: Alt+up */
		mx_TERMCAP_QUERY_xkey_cup, /* kUP5/-, STRING | X: Control+up */
	mx_TERMCAP_QUERY_kf0, /* kf0/k0, STRING */
	mx_TERMCAP_QUERY_kf1, /* kf1/k1, STRING */
	mx_TERMCAP_QUERY_kf2, /* kf2/k2, STRING */
	mx_TERMCAP_QUERY_kf3, /* kf3/k3, STRING */
	mx_TERMCAP_QUERY_kf4, /* kf4/k4, STRING */
	mx_TERMCAP_QUERY_kf5, /* kf5/k5, STRING */
	mx_TERMCAP_QUERY_kf6, /* kf6/k6, STRING */
	mx_TERMCAP_QUERY_kf7, /* kf7/k7, STRING */
	mx_TERMCAP_QUERY_kf8, /* kf8/k8, STRING */
	mx_TERMCAP_QUERY_kf9, /* kf9/k9, STRING */
	mx_TERMCAP_QUERY_kf10, /* kf10/k;, STRING */
	mx_TERMCAP_QUERY_kf11, /* kf11/F1, STRING */
	mx_TERMCAP_QUERY_kf12, /* kf12/F2, STRING */
	mx_TERMCAP_QUERY_kf13, /* kf13/F3, STRING */
	mx_TERMCAP_QUERY_kf14, /* kf14/F4, STRING */
	mx_TERMCAP_QUERY_kf15, /* kf15/F5, STRING */
	mx_TERMCAP_QUERY_kf16, /* kf16/F6, STRING */
	mx_TERMCAP_QUERY_kf17, /* kf17/F7, STRING */
	mx_TERMCAP_QUERY_kf18, /* kf18/F8, STRING */
	mx_TERMCAP_QUERY_kf19, /* kf19/F9, STRING */
# endif /* mx_HAVE_KEY_BINDINGS */

	mx__TERMCAP_QUERY_MAX1
};

struct mx_termcap_value{
	enum mx_termcap_captype tv_captype;
	su_64(u8 tv__dummy[4];)
	union mx_termcap_value_data{
		boole tvd_bool;
		u32 tvd_numeric;
		char const *tvd_string;
	} tv_data;
};

/* termcap(3) / xy lifetime handling - only called if n_PSO_INTERACTIVE without n_PO_QUICKRUN_MASK */
EXPORT void mx_termcap_init(void);
EXPORT void mx_termcap_destroy(void);

/* enter_ca_mode / enable keypad / enable bracketed paste mode (all: if possible) */
EXPORT void mx_termcap_resume(BITENUM(u32,mx_termcap_mode) mode);
EXPORT void mx_termcap_suspend(BITENUM(u32,mx_termcap_mode) mode);

/* Command multiplexer, returns FAL0 on I/O error, TRU1 on success and TRUM1 for commands which are not available and
 * have no built-in fallback.
 * \a{cmd} may have \c{_CMD_FLAG_} bits ORd to the command.
 * Will return FAL0 directly unless we have been initialized; returns \TRUM1 if \a{cmd} is not supported and not even
 * an alternative exists.
 * By convention unused argument slots are given as -1 */
EXPORT boole mx_termcap_cmd(BITENUM(u32,mx_termcap_cmd) cmd, sz a1, sz a2);
# define mx_termcap_cmdx(CMD) mx_termcap_cmd(CMD, -1, -1)

/* Query multiplexer.
 * If query is mx__TERMCAP_QUERY_MAX1 then tvp->tv_data.tvd_string must contain the name of the query to look up:
 * this is used to lookup just about *any* (string) capability.
 * Returns TRU1 on success and TRUM1 for queries for which a built-in default is returned; FAL0 is returned on
 * non-availability; for boolean the return value equals the result as such (still tvp is mandatory argument) */
EXPORT boole mx_termcap_query(enum mx_termcap_query query, struct mx_termcap_value *tvp);

# ifdef mx_HAVE_KEY_BINDINGS
/* Get a mx_termcap_query for name or -1 if it is not known, and -2 if type was not _NONE and type does not match. */
EXPORT s32 mx_termcap_query_for_name(char const *name, enum mx_termcap_captype type);

/* Get terminfo name of query */
EXPORT char const *mx_termcap_name_of_query(enum mx_termcap_query query);
# endif

#include <su/code-ou.h>
#endif /* mx_HAVE_TCAP */
#endif /* mx_TERMCAP_H */
/* s-itt-mode */
