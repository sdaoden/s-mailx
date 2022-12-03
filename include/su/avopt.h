/*@ Command line option parser.
 *
 * Copyright (c) 2001 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_AVOPT_H
#define su_AVOPT_H

/*!
 * \file
 * \ingroup AVOPT
 * \brief \r{AVOPT}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_avopt;

/* avopt {{{ */
/*!
 * \defgroup AVOPT Command line argument parser
 * \ingroup MISC
 * \brief Command line argument option parser (\r{su/avopt.h})
 *
 * \list{\li{
 * Any printable ASCII (7-bit) character except hyphen-minus \c{-} may be used
 * as a short option.
 * }\li{
 * This implementation always differentiates in between
 * \r{su_AVOPT_STATE_ERR_OPT} and \r{su_AVOPT_STATE_ERR_ARG} errors.
 * A leading colon \c{:} in the short option definition string is thus treated
 * as a normal argument (at any other position it would be mistreated as an
 * argument indicator for the preceding option).
 *
 * For example \c{::ab:c} defines an option \c{:} that takes an argument,
 * an option \c{a} that does not, \c{b} which does, and \c{c} which does not.
 * A user could use it for example like \c{-cabHello -: World}
 * or \c{-a -c -b Hello -:World}.
 * }\li{
 * Long options are supported.
 * They may consist of any printable ASCII (7-bit) character except
 * colon \c{:} (indicating the option has an argument),
 * equal-sign \c{=} (separates an option from its argument),
 * and semicolon \c{;} (definition separator).
 *
 * Long options need to be identified, either via a short option equivalence
 * mapping, or a unique identifier, a (negative) 32-bit decimal value.
 * The identifier is placed after a semicolon \c{;} separator,
 * after the possible \c{:} which indicates an argument requirement,
 * it is what is returned by \r{su_avopt_parse()} and
 * \r{su_avopt_parse_line()}, and what is stored in
 * \r{su_avopt::avo_current_opt}.
 *
 * For example \c{name:;n} defines an option \c{name} that takes an argument,
 * and maps to the short option \c{n}, whether that is itself defined or not.
 * (However, debug enabled code will check that an existing \c{n} would also
 * take an argument.)
 * A user could use it like \c{--name=Jonah} or \c{--name Jonah}, and it would
 * always appear as if \c{-n Jonah} (aka \c{-nJonah}) had been used.
 * And \c{long-help;-99} defines an option \c{long-help} without argument and
 * the identifier \c{-99}.
 * }\li{
 * Documentation strings can optionally be appended to long option definitions
 * after another \c{;} separator that follows the identifier.
 * These documentation strings may be dumped via \r{su_avopt_dump_doc()}.
 *
 * For example \c{account:;a;Activate the given account name} defines an option
 * \c{account} that takes an argument, maps to the short option \c{a}, and has
 * a documentation string, whereas \c{follow-symlinks;-3;Follow symbolic links}
 * defines an option \c{follow-symlinks} with identifier \c{-3} and has the
 * documentation string \c{Follow symbolic links}.
 * }\li{
 * When parsing an argument vector via \r{su_avopt_parse()} an option (not
 * argument) double hyphen-minus \c{--} stops argument processing, anything
 * that follows is no longer recognized as an option.
 * The function still returns \r{su_AVOPT_STATE_DONE} then, but
 * \r{su_avopt::avo_current_opt} contains \r{su_AVOPT_STATE_STOP} in this case.
 *
 * If \c{--} is not given processing is stopped at the first non-option or if
 * the argument list is exhausted, whatever comes first.
 *
 * An option (not argument) single hyphen-minus \c{-} is also not recognized as
 * an option, but also stops processing.
 * The difference is that \c{--} is consumed from the argument list, whereas
 * \c{-} is not and remains in \r{su_avopt::avo_argv}.
 * }\li{
 * Error messages are not logged.
 * Instead, the format strings \r{su_avopt_fmt_err_arg} and
 * \r{su_avopt_fmt_err_opt} can be used with an argument of
 * \r{su_avopt::avo_current_err_opt} whenever an error is generated.
 * }\li{
 * There is a \r{su_avopt_parse_line()} interface that realizes a simple
 * configuration file syntax which mirrors long command line options to
 * per-line directives.
 *
 * For example, the long option \c{account} which can be specified as
 * \c{--account myself} or \c{--account=myself} on the command line is then
 * expected on a line on its own as \c{account myself} or \c{account=myself}.
 * }\li{
 * With \r{su_HAVE_DEBUG} and/or \r{su_HAVE_DEVEL} content of short and long
 * option strings are checked for notational errors.
 * If long options are used which map to existing short options, it is verified
 * that the "takes argument" state is identical.
 * }}
 *
 * \cb{
 *	#define N_(X) X
 *	static char const a_sopts[] = "::A:h#";
 *	static char const * const a_lopts[] = {
 *		"resource-files:;:;" N_("control loading of resource files"),
 *		"account:;A;" N_("execute an `account' command"),
 *		"batch-mode;#;" N_("more confined non-interactive setup"),
 *		"long-help;-99;" N_("this listing"),
 *		NIL
 *	};
 *
 *	struct su_avopt avo;
 *	char const *emsg;
 *	s32 i;
 *
 *	su_avopt_setup(&avo,
 *		(argc != 0 ? --argc : argc), su_C(char const*const*,++argv),
 *		a_sopts, a_lopts);
 *
 *	while((i = su_avopt_parse(&avo)) != su_AVOPT_STATE_DONE){
 *		switch(i){
 *		case 'A':
 *			"account_name" = avo.avo_current_arg;
 *			break;
 *		case 'h':
 *		case -99:
 *			a_main_usage(n_stdout);
 *			if(i != 'h'){
 *				fprintf(n_stdout, "\nLong options:\n");
 *				su_avopt_dump_doc(&avo, &a_main_dump_doc, su_S(su_up,n_stdout));
 *			}
 *			 exit(0);
 *		case '#':
 *			n_var_setup_batch_mode();
 *			break;
 *		case su_AVOPT_STATE_ERR_ARG:
 *			emsg = su_avopt_fmt_err_arg;
 *			if(0){
 *				// FALLTHRU
 *		case su_AVOPT_STATE_ERR_OPT:
 *				emsg = su_avopt_fmt_err_opt;
 *			}
 *			fprintf(stderr, emsg, avo.avo_current_err_opt);
 *			exit(1);
 *		}
 *	}
 *
 *	// avo.avo_current_opt is either su_AVOPT_STATE_DONE or
 *	// su_AVOPT_STATE_STOP in case of -- termination
 *	argc = avo.avo_argc;
 *	argv = su_C(char**,avo.avo_argv);
 * }
 * @{
 */

/*! \remarks{The values of these constants are ASCII control characters.} */
enum su_avopt_state{
	su_AVOPT_STATE_DONE = '\0', /*!< Argument processing is done. */
	/*! Argument processing stopped due to \c{--} terminator.
	 * This is never returned in favour of \r{su_AVOPT_STATE_DONE} by \r{su_avopt_parse()}, but can only be found in
	 * \r{su_avopt::avo_current_opt}.} */
	su_AVOPT_STATE_STOP = '\001',
	su_AVOPT_STATE_ERR_ARG = '\002', /*!< \_ */
	su_AVOPT_STATE_ERR_OPT = '\003' /*!< \_ */
};

/*! \remarks{Most fields make sense only after \r{su_avopt_parse()} or
 * \r{su_avopt_parse_line()} have been called (at least once).} */
struct su_avopt{
	char const *avo_opts_short; /*!< Short options as given. */
	char const * const *avo_opts_long; /*!< Long options as given. */
	char const * const *avo_argv; /*!< Remaining entries. */
	u32 avo_argc; /*!< Remaining count. */
	/*! In the positive number range this denotes a short option 7-bit character or a \r{su_avopt_state} constant,
	 * negative values are reserved for long option identifiers. */
	s32 avo_current_opt;
	char const *avo_current_arg; /*!< Current argument or \NIL. */
	/*! The current option that lead to an error as a \NUL terminated string.
	 * In case of long options only this may include the argument, too,
	 * that is to say the entire argument vector entry which caused failure.
	 * Only useful if any of \r{su_AVOPT_STATE_ERR_ARG} and \r{su_AVOPT_STATE_ERR_OPT} has occurred,
	 * and the only member useful for users after an error. */
	char const *avo_current_err_opt;
	char const *avo_curr;
	u8 avo_flags;
	char avo__buf[2  +1 su_64(+ 4)];
};

/*! \_ */
EXPORT_DATA char const su_avopt_fmt_err_arg[];

/*! \_ */
EXPORT_DATA char const su_avopt_fmt_err_opt[];

/*! One of \a{opts_short_or_nil} and \a{opts_long_or_nil} must be given.
 * The latter must be a \NIL terminated array.
 * \a{argv} can be \NIL if \a{argc} is 0.
 * \r{su_DVLDBGOR()} enabled code inspects the option strings and invalidates on error, meaning that
 * \r{su_avopt_parse()} and \r{su_avopt_parse_line()} may return \r{su_AVOPT_STATE_DONE} unexpectedly. */
EXPORT struct su_avopt *su_avopt_setup(struct su_avopt *self, u32 argc, char const * const *argv,
		char const *opts_short_or_nil, char const * const *opts_long_or_nil);

/*! Parse the setup argument vector.
 * Can be called repeatedly until \r{su_AVOPT_STATE_DONE} is returned. */
EXPORT s32 su_avopt_parse(struct su_avopt *self);

/*! Identical to \r{su_avopt_parse()}, but parse the given string \a{cp} instead of the setup argument vector.
 * \a{cp} should be whitespace trimmed, and is expected to consist of a long option without the leading double
 * hyphen-minus, and an optional argument in the usual notation.
 * \r{su_avopt_setup()} should have been given long options.
 * The fields \r{su_avopt::avo_argc} and \r{su_avopt::avo_argv} are not used.
 * \remarks{One may not intermix calls to \r{su_avopt_parse()} and this function, because \r{su_avopt_parse()}
 * implements a state machine that this function consciously (changes but) ignores.} */
EXPORT s32 su_avopt_parse_line(struct su_avopt *self, char const *cp);

/*! If there are long options, query them all in order and dump them via the given pointer to function \a{ptf}.
 * If there is a short option equivalence, \a{sopt} is not the empty string.
 * Options will be preceded with \c{-} and \c{--}, respectively.
 * Stops when \a{ptf} returns \FAL0, otherwise returns \TRU1.
 *
 * \remarks{The long option string is copied over to a stack buffer of 128 bytes (\a{lopt}), any excess is cut off.}
 *
 * \remarks{The documentation string \a{doc} always points to a byte in the corresponding long option string
 * passed in by the user.} */
EXPORT boole su_avopt_dump_doc(struct su_avopt const *self,
		boole (*ptf)(up cookie, boole has_arg, char const *sopt, char const *lopt, char const *doc), up cookie);
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class avopt;

/* avopt {{{ */
/*!
 * \ingroup AVOPT
 * C++ variant of \r{AVOPT} (\r{su/avopt.h})
 *
 * \remarks{Debug assertions are performed in the C base only.}
 */
class EXPORT avopt : private su_avopt{
	su_CLASS_NO_COPY(avopt);
public:
	/*! \copydoc{su_avopt_state} */
	enum state{
		state_done = su_AVOPT_STATE_DONE, /*!< \copydoc{su_AVOPT_STATE_DONE} */
		state_stop = su_AVOPT_STATE_STOP, /*!< \copydoc{su_AVOPT_STATE_STOP} */
		state_err_arg = su_AVOPT_STATE_ERR_ARG, /*!< \copydoc{su_AVOPT_STATE_ERR_ARG} */
		state_err_opt = su_AVOPT_STATE_ERR_OPT /*!< \copydoc{su_AVOPT_STATE_ERR_OPT} */
	};

	/*! \copydoc{su_avopt_fmt_err_arg} */
	static char const * const fmt_err_arg;

	/*! \copydoc{su_avopt_fmt_err_opt} */
	static char const * const fmt_err_opt;

	/*! \NOOP; \r{setup()} is real constructor. */
	avopt(void) {DBGX( STRUCT_ZERO(su_avopt, this); )}

	/*! \copydoc{su_avopt_setup()} */
	avopt(u32 argc, char const * const *argv, char const *opts_short, char const * const *opts_long=NIL){
		su_avopt_setup(this, argc, argv, opts_short, opts_long);
	}

	/*! \_ */
	~avopt(void) {}

	/*! \copydoc{su_avopt_setup()} */
	avopt &setup(u32 argc, char const * const *argv, char const *opts_short, char const * const *opts_long=NIL){
		SELFTHIS_RET(su_avopt_setup(this, argc, argv, opts_short, opts_long));
	}

	/*! \copydoc{su_avopt_parse()} */
	s32 parse(void) {return su_avopt_parse(this);}

	/*! \copydoc{su_avopt_parse_line()} */
	s32 parse_line(char const *cp) {return su_avopt_parse_line(this, cp);}

	/*! \copydoc{su_avopt::avo_opts_short} */
	char const *opts_short(void) const {return avo_opts_short;}

	/*! \copydoc{su_avopt::avo_opts_long} */
	char const * const *opts_long(void) const {return avo_opts_long;}

	/*! \copydoc{su_avopt::avo_argv} */
	char const * const *argv(void) const {return avo_argv;}

	/*! \copydoc{su_avopt::avo_argc} */
	u32 argc(void) const {return avo_argc;}

	/*! \copydoc{su_avopt::avo_current_opt} */
	s32 current_opt(void) const {return avo_current_opt;}

	/*! \copydoc{su_avopt::avo_current_arg} */
	char const *current_arg(void) const {return avo_current_arg;}

	/*! \copydoc{su_avopt::avo_current_err_opt} */
	char const *current_err_opt(void) const {return avo_current_err_opt;}

	/*! \copydoc{su_avopt_dump_doc()} */
	boole dump_doc(boole (*ptf)(up cookie, boole has_arg, char const *sopt, char const *lopt, char const *doc), up cookie=0) const{
		return su_avopt_dump_doc(this, ptf, cookie);
	}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_AVOPT_H */
/* s-itt-mode */
