/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ This defines the command array, and is included by ./cmd.c.
 *@ It is included twice, the first part defines new-style cmd_arg context
 *@ objects, the second part the actual command array.
 *@ The script mk/make-cmd-tab.sh generates ./gen-cmd-tab.h based on the
 *@ content of the latter, which must be kept in alphabetical order (almost).
 *@ Entries in column 0 are missorted due to POSIX command abbreviation
 *@ requirements (or are obsolete).  Upper- and lowercase sorting: often not.
 *@ The default command needs a --MKTAB-DFL-- comment suffix to command name.
 *@ The parsed content must be within --MKTAB-START-- and --MKTAB-END--.
 *
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
/* Not indented for dual inclusion */
#ifndef mx_CMD_TAB_H
# define mx_CMD_TAB_H

#ifdef mx_HAVE_KEY_BINDINGS
# define a_CMD_CAD_BIND mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_bind)
mx_CMD_ARG_DESC_SUBCLASS_DEF(bind, 3, a_cmd_cad_bind){
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* context */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_DRYRUN}, /* subcommand / key sequence */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_GREEDY_JOIN |
			mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_TRIM_IFSSPACE} /* expansion */
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;
#else
# define a_CMD_CAD_BIND NIL
#endif

mx_CMD_ARG_DESC_SUBCLASS_DEF(call, 2, a_cmd_cad_call){
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* macro name */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_TRIM_IFSSPACE} /* args */
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

#ifdef mx_HAVE_TLS
# define a_CMD_CAD_CERTSAVE mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_certsave)
mx_CMD_ARG_DESC_SUBCLASS_DEF(certsave, 1, a_cmd_cad_certsave){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;
#else
# define a_CMD_CAD_CERTSAVE NIL
#endif

mx_CMD_ARG_DESC_SUBCLASS_DEF(Copy, 1, a_cmd_cad_Copy){
	{mx_CMD_ARG_DESC_MSGLIST | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(copy, 1, a_cmd_cad_copy){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(Decrypt, 1, a_cmd_cad_Decrypt){
	{mx_CMD_ARG_DESC_MSGLIST | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(decrypt, 1, a_cmd_cad_decrypt){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

/* Superset of the one in mx_dig_msg_circumflex() */
mx_CMD_ARG_DESC_SUBCLASS_DEF(digmsg, 6, a_cmd_cad_digmsg){ /* XXX 4 OR 5 */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* subcommand (/ msgno/-) */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* msgno/- (/ first part of user cmd) */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg1 */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg2 */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg3 */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP | mx_CMD_ARG_DESC_GREEDY |
			mx_CMD_ARG_DESC_GREEDY_JOIN,
		n_SHEXP_PARSE_TRIM_IFSSPACE} /* arg4 */
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

/* elif: uses if */

mx_CMD_ARG_DESC_SUBCLASS_DEF(Forward, 1, a_cmd_cad_Forward){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY /*| mx_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE*/,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(forward, 1, a_cmd_cad_forward){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY /*| mx_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE*/,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(if, 1, a_cmd_cad_if){
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(Move, 1, a_cmd_cad_Move){
	{mx_CMD_ARG_DESC_MSGLIST | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(move, 1, a_cmd_cad_move){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(pdot, 1, a_cmd_cad_pdot){
	{mx_CMD_ARG_DESC_MSGLIST | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(pipe, 1, a_cmd_cad_pipe){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(readctl, 2, a_cmd_cad_readctl){
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* subcommand */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_GREEDY_JOIN |
			mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_TRIM_IFSSPACE} /* var names */
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(Resend, 1, a_cmd_cad_Resend){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(resend, 1, a_cmd_cad_resend){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(Save, 1, a_cmd_cad_Save){
	{mx_CMD_ARG_DESC_MSGLIST | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(save, 1, a_cmd_cad_save){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(setdot, 1, a_cmd_cad_setdot){
	{mx_CMD_ARG_DESC_MSGLIST | mx_CMD_ARG_DESC_GREEDY |
			mx_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

#ifdef mx_HAVE_KEY_BINDINGS
# define a_CMD_CAD_UNBIND mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_unbind)
mx_CMD_ARG_DESC_SUBCLASS_DEF(unbind, 2, a_cmd_cad_unbind){
	{mx_CMD_ARG_DESC_SHEXP, n_SHEXP_PARSE_TRIM_IFSSPACE}, /* context */
	/* key sequence or * */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_HONOUR_STOP, n_SHEXP_PARSE_DRYRUN}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;
#else
# define a_CMD_CAD_UNBIND NIL
#endif

mx_CMD_ARG_DESC_SUBCLASS_DEF(vpospar, 2, a_cmd_cad_vpospar){
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_TRIM_IFSSPACE}, /* subcommand */
	{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_HONOUR_STOP,
		n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_TRIM_IFSSPACE} /* args */
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

mx_CMD_ARG_DESC_SUBCLASS_DEF(write, 1, a_cmd_cad_write){
	{mx_CMD_ARG_DESC_MSGLIST_AND_TARGET | mx_CMD_ARG_DESC_GREEDY,
		n_SHEXP_PARSE_TRIM_IFSSPACE}
}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

#else /* ifndef mx_CMD_TAB_H */

#undef MAC
#define MAC (n_MAXARGC - 1)

/* Some shorter aliases to be able to define a command in two lines */
#define TMSGLST mx_CMD_ARG_TYPE_MSGLIST
#define TNDMLST mx_CMD_ARG_TYPE_NDMLIST
#define TRAWDAT mx_CMD_ARG_TYPE_RAWDAT
# define TSTRING mx_CMD_ARG_TYPE_STRING
#define TWYSH mx_CMD_ARG_TYPE_WYSH
# define TRAWLST mx_CMD_ARG_TYPE_RAWLIST
# define TWYRA mx_CMD_ARG_TYPE_WYRA
#define TARG mx_CMD_ARG_TYPE_ARG

#define A mx_CMD_ARG_A
#define F mx_CMD_ARG_F
#define G mx_CMD_ARG_G
#define HG mx_CMD_ARG_HGABBY
#define I mx_CMD_ARG_I
#define L mx_CMD_ARG_L
#define LNMAC mx_CMD_ARG_L_NOMAC
#define M mx_CMD_ARG_M
#define O mx_CMD_ARG_O
#define P mx_CMD_ARG_P
#undef R
#define R mx_CMD_ARG_R
#define SC mx_CMD_ARG_SC
#undef S
#define S mx_CMD_ARG_S
#define T mx_CMD_ARG_T
#define V mx_CMD_ARG_V
#define W mx_CMD_ARG_W
#define X mx_CMD_ARG_X
#define NOHOOK mx_CMD_ARG_NO_HOOK
#define NMAC mx_CMD_ARG_NEEDMAC
#define NOHIST mx_CMD_ARG_NO_HISTORY
#define EM mx_CMD_ARG_EM

	/* --MKTAB-START-- */

	{"!", &c_shell, (M | V | X | EM | TRAWDAT), 0, 0, NIL,
	 N_("Execute <shell-command>")},
	{"=", &c_pdot, (A | HG | M | V | X | EM | SC | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_pdot),
	 N_("Show message number of [<msglist>] (or the \"dot\")")},
	{":", su_R(int(*)(void*),-1)/*ok!*/, (HG | L | M | X | TWYSH), 0, MAC, NIL,
	 N_("No-effect: does nothing but expanding arguments")},
	{"?", &a_cmd_c_help, (HG | M | X | TWYSH), 0, 1, NIL,
	 N_("Show help [for the given command]")},
	{"|", &c_pipe, (A | M | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_pipe),
	 N_("Pipe [<msglist>] to [<command>], honour `headerpick'")},

{"alias", &c_alias, (M | TWYSH), 0, MAC, NIL,
	 N_("(a) Show all (or [-] <alias>), or append to <alias> :<data>:")},
	{"account", &c_account, (M | NOHOOK | TWYSH), 0, MAC, NIL,
	 N_("Create <account {>, select <account>, or list all accounts")},
	{"addrcodec", &c_addrcodec, (HG | M | V | X | EM | TRAWDAT), 0, 0, NIL,
	 N_("Mail address <[+[+[+]]]e[ncode]|d[ecode]|s[kin[l[ist]]]> <rest-of-line>")},
	{"alternates", &c_alternates, (M | V | TWYSH), 0, MAC, NIL,
	 N_("(alt) Show or define an alternate <address-list> (for *from* etc)")},
	{"answered", &c_answered, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Mark the given <msglist> as answered")},

	{"bind",
#ifdef mx_HAVE_KEY_BINDINGS
	 &c_bind,
#else
	 NIL,
#endif
	 (M | TARG), 0, MAC, a_CMD_CAD_BIND,
	 N_("For [<context> (base)], [<show>] or bind <key[:,key:]> [:<expansion>:]")},

{"copy", &c_copy, (A | M | EM | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_copy),
	 N_("(c) Copy [<msglist>], do not mark them for deletion")},
	{"cache",
#ifdef mx_HAVE_IMAP
	 &c_cache,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Read specified <message list> into the IMAP cache")},
	{"call", &c_call, (L | LNMAC | M | X | EM | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_call),
	 N_("Call macro <name> [:<arg>:]")},
	{"call_if", &c_call_if, (L | LNMAC | M | X | EM | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_call),
	 N_("Call macro <name> like `call', but be silent if non-existing")},
	{"cd", &c_chdir, (M | X | TWYSH), 0, 1, NIL,
	 N_("Change working directory to the specified, or the login directory")},
	{"certsave",
#ifdef mx_HAVE_TLS
	 &c_certsave,
#else
	 NIL,
#endif
	 (A | M | SC | TARG), 0, MMNDEL, a_CMD_CAD_CERTSAVE,
	 N_("Save S/MIME certificates of [<msglist>] to <file>")},
{"chdir", &c_chdir, (M | TWYSH), 0, 1, NIL,
	 N_("(ch) Change CWD to the specified, or the login directory")},
	{"charsetalias", &c_charsetalias, (M | TWYSH), 0, MAC, NIL,
	 N_("Define [:<charset> <charset-alias>:]s, or list mappings")},
	{"colour",
#ifdef mx_HAVE_COLOUR
	 &c_colour,
#else
	 NIL,
#endif
	 (M | TWYSH), 0, 4, NIL,
	 N_("Show colour settings [of <type> (1,8,256,all/*)], or define one")},
	{"commandalias", &c_commandalias, (M | X | TWYSH), 0, MAC, NIL,
	 N_("Print/create command <alias> [<command>], or list all such")},
	{"connect",
#ifdef mx_HAVE_IMAP
	 &c_connect,
#else
	 NIL,
#endif
	 (A | M | S | TSTRING), 0, 0, NIL,
	 N_("If disconnected, connect to IMAP `folder'")},
	{"Copy", &c_Copy, (A | M | EM | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_Copy),
	 N_("(C) Like `copy', but derive `folder' from first sender")},
	{"collapse", &c_collapse, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Collapse thread view [of <msglist>] in \"sort thread\" view")},
	{"csop",
#ifdef mx_HAVE_CMD_CSOP
	 &c_csop,
#else
	 NIL,
#endif
	 (HG | M | V | X | EM | TWYSH), 2, MAC, NIL,
	 N_("C-style byte string <operation>s on given :<argument>:")},
	{"cwd", &c_cwd, (M | V | X | TWYSH), 0, 0, NIL,
	 N_("Print current working directory (CWD)")},

{"delete", &c_delete, (A | M | W | P | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(d) Delete <msglist>")},
{"discard", &c_ignore, (M | TWYRA), 0, MAC, NIL,
	 N_("(di) Add <headers> to \"headerpick type ignore\", or show that list")},
	{"Decrypt", &c_Decrypt, (A | M | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_Decrypt),
	 N_("Like `decrypt', but derive `folder' from first sender")},
	{"decrypt", &c_decrypt, (A | M | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_decrypt),
	 N_("Like `copy', but decrypt first, if encrypted")},
	{"define", &c_define, (M | X | TWYSH), 0, 2, NIL,
	 N_("Define a <macro {>, or list [<macro>]/all existing ones")},
	{"digmsg", &c_digmsg, (HG | M | X | EM | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_digmsg),
	 N_("<create|remove> <-|msgno> [<->] | <-|msgno> <cmd>: message access")},
	{"disconnect",
#ifdef mx_HAVE_IMAP
	 &c_disconnect,
#else
	 NIL,
#endif
	 (A | M | S | TNDMLST), 0, 0, NIL,
	 N_("If connected, disconnect from IMAP `folder'")},
	{"dotmove", &c_dotmove, (A | M | SC | TSTRING), 1, 1, NIL,
	 N_("Move the dot up <-> or down <+> by one")},
	{"dp", &c_deltype, (A | M | W | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Delete [<msglist>]/\"dot\", then perform `next'")},
	{"dt", &c_deltype, (A | M | W | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Delete [<msglist>]/\"dot\", then perform `next'")},
	{"draft", &c_draft, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Mark <msglist> as draft")},

{"edit", &c_edit, (HG | A | I | M | S | TMSGLST), 0, MMNORM, NIL,
	 N_("(e) Edit <msglist> with $EDITOR")},
	{"echo", &c_echo, (HG | M | V | X | EM | TWYSH), 0, MAC, NIL,
	 N_("(ec) Echo arguments, and a trailing newline, to standard output")},
	{"echoerr", &c_echoerr, (HG | M | X | EM | TWYSH), 0, MAC, NIL,
	 N_("Echo arguments, and a trailing newline, to standard error")},
	{"echon", &c_echon, (HG | M | V | X | EM | TWYSH), 0, MAC, NIL,
	 N_("Echo arguments to standard output, without a trailing newline")},
	{"echoerrn", &c_echoerrn, (HG | M | X | EM | TWYSH), 0, MAC, NIL,
	 N_("Echo arguments, without a trailing newline, to standard error")},
{"else", &c_else, (HG | F | M | X | EM | TWYSH), 0, 0, NIL,
	 N_("(el) Part of the if/elif/else/endif conditional expression")},
	{"elif", &c_elif, (HG | F | M | X | EM | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_if),
	 N_("Part of the if/elif/else/endif conditional expression")},
	{"endif", &c_endif, (HG | F | M | X | EM | TWYSH), 0, 0, NIL,
	 N_("(en) Part of the if/elif/else/endif conditional expression")},
	{"environ", &c_environ, (HG | L | M | V | X | EM | TWYSH), 2, MAC, NIL,
	 N_("<link|unlink|set|unset> environment :<variable>:, or [vput] lookup <var>")},
	{"errors",
#ifdef mx_HAVE_ERRORS
	 &c_errors,
#else
	 NIL,
#endif
	 (I | M | NOHIST | TWYSH), 0, 1, NIL,
	 N_("Either [<show>] or <clear> the error message ring")},
	{"exit", &c_exit, (M | X | TWYSH), 0, 1, NIL,
	 N_("(ex) Immediately return [<status>] to the shell (no \"at-exit\" actions)")},

{"Followup", &c_Followup, (A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(F) Like `Reply', but derive `folder' from first sender")},
{"from", &c_from, (A | M | SC | TMSGLST), 0, MMNORM, NIL,
	 N_("(f) Type (matching) headers of <msglist> (a search specification)")},
	{"File", &c_File, (M | T | NOHOOK | S | TWYRA), 0, 1, NIL,
	 N_("Open a new `folder' readonly, or show the current one")},
	{"file", &c_file, (M | T | NOHOOK | S | TWYRA), 0, 1, NIL,
	 N_("(fi) Open a new <folder> or show the current one")},
	{"filetype", &c_filetype, (M | TWYSH), 0, MAC, NIL,
	 N_("Create [:<extension> <load-cmd> <save-cmd>:] or list `folder' handlers")},
	{"flag", &c_flag, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("(Un)Flag <msglist> (for special attention)")},
{"followup", &c_followup, (A | I | L | LNMAC | R | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(fo) Like `reply', but derive `folder' from first sender")},
	{"Folder", &c_File, (M | T | NOHOOK | S | TWYRA), 0, 1, NIL,
	 N_("Open a new `folder' readonly, or show the current folder")},
	{"folder", &c_file, (M | T | NOHOOK | S | TWYRA), 0, 1, NIL,
	 N_("(fold) Open a new <folder>, or show the current one")},
	{"folders", &c_folders, (M | T | SC | TWYRA), 0, 1, NIL,
	 N_("List mailboxes below the given or the global *folder*")},
	{"fop",
#ifdef mx_HAVE_CMD_FOP
	 &c_fop,
#else
	 NIL,
#endif
	 (HG | M | V | X | EM | TWYSH), 1, MAC, NIL,
	 N_("<Operation>s on file-/paths with [:<arguments>:]")},
	{"Forward", &c_Forward, (A | I | L | LNMAC | R | SC | EM | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_Forward),
	 N_("Like `forward', but derive `folder' from <address>")},
	{"forward", &c_forward, (A | I | L | LNMAC | R | SC | EM | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_forward),
	 N_("Forward <message> to <address>")},
{"followupall", &c_followupall, (O | A | I | L | LNMAC | R | SC | TMSGLST), 0, MMNDEL, NIL,
 N_("Like `reply', but derive `folder' from first sender")},
{"followupsender", &c_followupsender, (O | A | I | L | LNMAC | R | SC | TMSGLST), 0, MMNDEL, NIL,
 N_("Like `Followup', but always reply to sender only")},
{"fwddiscard", &c_fwdignore, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `headerpick'")},
{"fwdignore", &c_fwdignore, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `headerpick'")},
{"fwdretain", &c_fwdretain, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `headerpick'")},

{"group", &c_alias, (M | TWYSH), 0, MAC, NIL,
	 N_("(g) Show all (or <alias>), or append to <alias> :<data>:")},
{"ghost", &c_commandalias, (O | M | X | TWYRA), 0, MAC, NIL,
 N_("Please use `commandalias'")},

{"headers", &c_headers, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(h) Type a page of headers (with the first of <msglist> if given)")},
	{"headerpick", &c_headerpick, (M | TWYSH), 0, MAC, NIL,
	 N_("[<ctx> [<type> [<headers>]]] | <create|remove> <ctx> | assign <ctx> <ctx>")},
	{"help", &a_cmd_c_help, (HG | M | X | TWYSH), 0, 1, NIL,
	 N_("(hel) Show help [for the given command]")},
	{"history",
#ifdef mx_HAVE_HISTORY
	 &c_history,
#else
	 NIL,
#endif
	 (I | M | NOHIST | SC | TWYSH), 0, MAC, NIL,
	 N_("<[show]|load|save|clear>, <delete> :<NO>:, or re-eval entry <NO>")},
	{"hold", &c_preserve, (A | M | W | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(ho) In primary `folder', hold <msglist>, do not move it to *MBOX*")},
	{"if", &c_if, (HG | F | M | X | EM | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_if),
	 N_("(i) Part of the if/elif/else/endif conditional expression")},
	{"ignore", &c_ignore, (M | TWYRA), 0, MAC, NIL,
	 N_("(ig) Add <headers> to \"headerpick type ignore\", or show that list")},
	{"imap",
#ifdef mx_HAVE_IMAP
	 &c_imap_imap,
#else
	 NIL,
#endif
	 (A | M | S | TSTRING), 0, MAC, NIL,
	 N_("Send command strings directly to the IMAP server")},
	{"imapcodec",
#ifdef mx_HAVE_IMAP
	 &c_imapcodec,
#else
	 NIL,
#endif
	 (HG | M | V | X | TRAWDAT), 0, 0, NIL,
	 N_("IMAP `folder' name <e[ncode]|d[ecode]> <rest-of-line>")},

	{"Lfollowup", &c_Lfollowup, (A | I | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mailing-list followup to the given <msglist>")},
	{"list", &a_cmd_c_list, (M | TWYSH), 0, 0, NIL,
	 N_("(l) List all commands (in lookup order)")},
{"localopts", &c_localopts, (O | M | X | NMAC | NOHIST | TWYSH), 1, 2, NIL,
 N_("Please use the \"local\" command modifier instead")},
	{"Lreply", &c_Lreply, (A | I | L | LNMAC | R | SC | EM | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mailing-list reply to the given <msglist>")},

	{"Mail", &c_Sendmail, (I | L | LNMAC | R | EM | SC | TSTRING), 0, 0, NIL,
	 N_("Like `mail', but derive `folder' from first recipient")},
	{"mail", &c_sendmail, (I | L | LNMAC | R | EM | SC | TSTRING), 0, 0, NIL,
	 N_("(m) Compose mail; recipients may be given as arguments")},
	{"mailcap",
#ifdef mx_HAVE_MAILCAP
	 &c_mailcap,
#else
	 NIL,
#endif
	 (M | TWYSH), 0, 1, NIL,
	 N_("[<show>], <load> or <clear> the $MAILCAPS cache")},
	{"mbox", &c_mboxit, (A | M | W | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(mb) Indicate that <msglist> is to be stored in *MBOX*")},
	{"mimetype", &c_mimetype, (M | TWYSH), 0, MAC, NIL,
	 N_("(Load and) show all known MIME types, or define some")},
	{"mimeview", &c_mimeview, (A | I | EM | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Show non-text parts of the given <message>")},
	{"mlist", &c_mlist, (M | TWYSH), 0, MAC, NIL,
	 N_("Show all known mailing lists or define some")},
	{"mlsubscribe", &c_mlsubscribe, (M | TWYSH), 0, MAC, NIL,
	 N_("Show all mailing list subscriptions or define some")},
	{"More", &c_More, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Invoke $PAGER [on <msglist>]")},
	{"more", &c_more, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Invoke $PAGER [on <msglist>]")},
	{"Move", &c_Move, (A | M | EM | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_Move),
	 N_("Like `move', but derive `folder' from first sender")},
	{"move", &c_move, (A | M | EM | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_move),
	 N_("Like `copy', but mark messages for deletion")},
	{"mtaaliases",
#ifdef mx_HAVE_MTA_ALIASES
	 &c_mtaaliases,
#else
	 NIL,
#endif
	 (M | TWYSH), 0, 2, NIL,
	 N_("[<show>], <load>, <clear> *mta-aliases* cache, or show [-] <MTA alias>")},
{"Mv", &c_Move, (O | A | M | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_Move),
 N_("Please use `Move'")},
{"mv", &c_move, (O | A | M | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_move),
 N_("Please use `move'")},

{"next"/*--MKTAB-DFL--*/, &c_next, (A | M | SC | TNDMLST), 0, MMNDEL, NIL,
	 N_("(n) Go to and `type' [<msglist>]/\"next\" message")},
	{"netrc",
#ifdef mx_HAVE_NETRC
	 &c_netrc,
#else
	 NIL,
#endif
	 (M | TWYSH), 0, 2, NIL,
	 N_("[<show>], <load>, <clear> cache, or <lookup> [USR@]HOST")},
	{"New", &c_unread, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mark <msglist> as not being read")},
	{"new", &c_unread, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mark <msglist> as not being read")},
	{"newmail", &c_newmail, (A | M | T | NOHOOK | SC | TWYSH), 0, 0, NIL,
	 N_("Check for new mail in current `folder'")},

	{"noop", &c_noop, (A | M | SC | TWYSH), 0, 0, NIL,
	 N_("NOOP command if current `folder' is accessed via network")},

{"Print", &c_Type, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(P) Like `print', but bypass `headerpick'")},
{"print", &c_type, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(p) Type [all members of <msglist>], honour `headerpick'")},
	{"Page", &c_More, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Invoke $PAGER [on <msglist>]")},
	{"page", &c_more, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Invoke $PAGER [on <msglist>]")},
	{"Pipe", &c_Pipe, (A | M | SC | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_pipe),
	 N_("Like `pipe', but do not honour `headerpick'")},
	{"pipe", &c_pipe, (A | M | SC | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_pipe),
	 N_("(pi) Pipe [<msglist>] to [<command>], honour `headerpick'")},
	{"preserve", &c_preserve, (A | M | W | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(pre) In primary `folder', hold <msglist>, do not move it to *MBOX*")},

	{"quit", &c_quit, (M | TWYSH), 0, 1, NIL,
	 N_("(q) Exit session with [<status>], perform \"at-exit\" actions")},

{"reply", &c_reply, (A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(r) Reply to originator and recipients of <msglist>")},
	{"read", &c_read, (HG | L | M | X | EM | TWYSH), 1, MAC, NIL,
	 N_("Read a line into <variable>(s), split at $ifs")},
	{"readsh", &c_readsh, (HG | L | M | X | EM | TWYSH), 1, MAC, NIL,
	 N_("Read a line input into <variable>(s), split at shell tokens")},
	{"readall", &c_readall, (HG | L | M | X | EM | TWYSH), 1, 1, NIL,
	 N_("Read anything from standard input until EOF into <variable>")},
	{"readctl", &c_readctl, (HG | M | X | EM | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_readctl),
	 N_("[<show>] or <create|set|remove> <spec> read channels")},
	{"remove", &c_remove, (M | SC | TWYSH), 1, MAC, NIL,
	 N_("Remove the named `folder's")},
	{"rename", &c_rename, (M | SC | TWYSH), 2, 2, NIL,
	 N_("Rename <existing-folder> to <new-folder>")},
	{"Reply", &c_Reply, (A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(R) Reply to originator, exclusively")},
	{"Resend", &c_Resend, (A | L | LNMAC | R | EM | SC | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_Resend),
	 N_("Like `resend', but do not add Resent-* header lines")},
	{"resend", &c_resend, (A | L | LNMAC | R | EM | SC | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_resend),
	 N_("Resend <msglist> to <user>, add Resent-* header lines")},
	{"Respond", &c_Reply, (A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Reply to originator, exclusively")},
	{"respond", &c_reply, (A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Reply to originators and recipients of <msglist>")},
	{"retain", &c_retain, (M | TWYRA), 0, MAC, NIL,
	 N_("(ret) Add <headers> to \"headerpick type retain\", or show that list")},
	{"return", &c_return, (M | X | EM | NMAC |TWYSH), 0, 2, NIL,
	 N_("Return control from macro [<return value> [<exit status>]]")},
{"replyall", &c_replyall, (O | A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
 N_("Please use `reply'")},
{"replysender", &c_replysender, (O | A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
 N_("Please use `Reply'")},
{"respondall", &c_replyall, (O | A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
 N_("Please use `reply'")},
{"respondsender", &c_replysender, (O | A | I | L | LNMAC | R | EM | SC | TMSGLST), 0, MMNDEL, NIL,
 N_("Please use `Reply'")},

	{"Save", &c_Save, (A | EM | M | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_Save),
	 N_("(S) Like `save', but derive `folder' from first sender")},
	{"save", &c_save, (A | EM | M | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_save),
	 N_("(s) Append [[<msglist>] to <file>] or (\"dot\" to) $MBOX")},
{"set", &c_set, (HG | L | M | X | TWYRA), 0, MAC, NIL,
	 N_("(se) Print all variables, or set (a) <variable>(s)")},
	{"search", &c_from, (A | M | SC | TMSGLST), 0, MMNORM, NIL,
	 N_("Search for <msglist>, type matching headers")},
	{"Seen", &c_seen, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mark <msglist> as seen")},
	{"seen", &c_seen, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mark <msglist> as seen")},
	{"setdot", &c_setdot, (A | HG | EM | M | SC | TARG), 0, MMNDEL, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_setdot),
	 N_("Set \"dot\" to the given <msg>")},
{"shell", &c_dosh, (I | EM | M | S | TWYSH), 0, 0, NIL,
	 N_("(sh) Invoke an interactive $SHELL")},
	{"shcodec", &c_shcodec, (HG | M | V | X | EM | TRAWDAT), 0, 0, NIL,
	 N_("Shell quoting: <[+]e[ncode]|d[ecode]> <rest-of-line>")},
	{"shift", &c_shift, (M | X | TWYSH), 0, 1, NIL,
	 N_("In a `call'ed macro, shift positional parameters")},
	{"shortcut", &c_shortcut, (M | TWYSH), 0, MAC, NIL,
	 N_("Define [:<shortcut> plus <expansion>:], or list shortcuts")},
	{"Show", &c_show, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Like `Type', but show raw message content of <msglist>")},
	{"show", &c_show, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Like `type', but show raw message content of <msglist>")},
	{"size", &c_messize, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(si) Show size in bytes for <msglist>")},
	{"sleep", &c_sleep, (M | X | EM | NOHIST | TWYSH), 1, 3, NIL,
	 N_("Sleep for <seconds> [<milliseconds>|empty [non-interruptible]]")},
{"source", &c_source, (M | TWYSH), 1, 1, NIL,
	 N_("(so) Read commands from <file>")},
	{"sort", &c_sort, (A | M | SC | TWYSH), 0, 1, NIL,
	 N_("Change sorting: date,from,size,spam,status,subject,thread,to")},
	{"source_if", &c_source_if, (M | TWYSH), 1, 1, NIL,
	 N_("Like `source', but ignore non-existence")},
	{"spamclear",
#ifdef mx_HAVE_SPAM
	 &c_spam_clear,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Clear spam flag [for members of <msglist>]")},
	{"spamforget",
#ifdef mx_HAVE_SPAM
	 &c_spam_forget,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Force spam detector to unlearn [members of <msglist>]")},
	{"spamham",
#ifdef mx_HAVE_SPAM
	 &c_spam_ham,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Teach spam detector [members of <msglist> are] ham")},
	{"spamrate",
#ifdef mx_HAVE_SPAM
	 &c_spam_rate,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Rate [<msglist>] via spam detector")},
	{"spamset",
#ifdef mx_HAVE_SPAM
	 &c_spam_set,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Set spam flag [for members of <msglist>]")},
	{"spamspam",
#ifdef mx_HAVE_SPAM
	 &c_spam_spam,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Teach spam detector [members of <msglist> are] spam")},
{"saveignore", &c_saveignore, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `headerpick'")},
{"savediscard", &c_saveignore, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `headerpick'")},
{"saveretain", &c_saveretain, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `headerpick'")},

{"Type", &c_Type, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(T) Like `type', but bypass `headerpick'")},
{"type", &c_type, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(t) Type [all members of <msglist>], honour `headerpick'")},
	{"tls",
#ifdef mx_HAVE_TLS
	 &c_tls,
#else
	 NIL,
#endif
	 (HG | V | EM | M | SC | TWYSH), 1, MAC, NIL,
	 N_("TLS information and management: <command> [<:argument:>]")},
	{"Top", &c_Top, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Like `top', but bypass `headerpick'")},
	{"top", &c_top, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Type first *toplines* [of all members of <msglist>]")},
	{"touch", &c_stouch, (A | M | W | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("(tou) Mark <msglist> for saving in *mbox*, if not saved or deleted otherwise")},
{"thread", &c_thread, (O | A | SC | TMSGLST), 0, 0, NIL,
 N_("Please use \"sort thread\"")},

{"undelete", &c_undelete, (A | M | P | SC | TMSGLST), MDELETED, MMNDEL, NIL,
	 N_("(u) Un`delete' <msglist>")},
{"unalias", &c_unalias, (M | TWYSH), 1, MAC, NIL,
	 N_("(una) Un`alias' <name-list> (* for all)")},
	{"unaccount", &c_unaccount, (M | TWYSH), 1, MAC, NIL,
	 N_("Delete all given <accounts> (* for all)")},
	{"unalternates", &c_unalternates, (M | TWYSH), 1, MAC, NIL,
	 N_("Delete alternate <address-list> (* for all)")},
	{"unanswered", &c_unanswered, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Un`answered' <msglist>")},
	{"unbind",
#ifdef mx_HAVE_KEY_BINDINGS
	 &c_unbind,
#else
	 NIL,
#endif
	 (M | TARG), 2, 2, a_CMD_CAD_UNBIND,
	 N_("Un`bind' <context> <key[:,key:]> (* for all)")},
	{"uncharsetalias", &c_uncharsetalias, (M | TWYSH), 1, MAC, NIL,
	 N_("Delete <charset-mapping-list> (* for all)")},
	{"uncollapse", &c_uncollapse, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Uncollapse thread view [of <msglist>] in \"sort thread\" view")},
	{"uncolour",
#ifdef mx_HAVE_COLOUR
	 &c_uncolour,
#else
	 NIL,
#endif
	 (M | TWYSH), 2, 3, NIL,
	 N_("Un`colour' <type> <mapping> (* for all) [<precondition>]")},
	{"uncommandalias", &c_uncommandalias, (M | X | TWYSH), 1, MAC, NIL,
	 N_("Delete <command-alias-list> (* for all)")},
	{"undefine", &c_undefine, (M | X | TWYSH), 1, MAC, NIL,
	 N_("Un`define' all given <macros> (* for all)")},
	{"undraft", &c_undraft, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Un`draft' <msglist>")},
	{"unfiletype", &c_unfiletype, (M | TWYSH), 1, MAC, NIL,
	 N_("Delete `folder' handler for [:<extension>:] (* for all)")},
	{"unflag", &c_unflag, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("(Un)Flag <msglist> (for special attention)")},
	{"ungroup", &c_unalias, (M | TWYSH), 1, MAC, NIL,
	 N_("Un`alias' <name-list> (* for all)")},
	{"unheaderpick", &c_unheaderpick, (M | TWYSH), 3, MAC, NIL,
	 N_("Header deselection: <ctx> <type> <headers>")},
	{"unignore", &c_unignore, (M | TWYRA), 0, MAC, NIL,
	 N_("Un`ignore' <headers>")},
	{"unmlist", &c_unmlist, (M | TWYSH), 1, MAC, NIL,
	 N_("Un`mlist' <name-list> (* for all)")},
	{"unmlsubscribe", &c_unmlsubscribe, (M | TWYSH), 1, MAC, NIL,
	 N_("Un`mlsubscribe' <name-list> (* for all)")},
	{"unmimetype", &c_unmimetype, (M | TWYSH), 1, MAC, NIL,
	 N_("Delete <type>s (reset, * for all; former reinitializes)")},
	{"Unread", &c_unread, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mark <msglist> as not being read")},
	{"unread", &c_unread, (A | M | SC | TMSGLST), 0, MMNDEL, NIL,
	 N_("Mark <msglist> as not being read")},
	{"unretain", &c_unretain, (M | TWYRA), 0, MAC, NIL,
	 N_("Un`retain' <headers>")},
	{"unset", &c_unset, (HG | L | M | X | TWYSH), 1, MAC, NIL,
	 N_("(uns) Unset <option-list>")},
	{"unshortcut", &c_unshortcut, (M | TWYSH), 1, MAC, NIL,
	 N_("Delete <shortcut-list> (* for all)")},
	{"unsort", &c_unsort, (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("The same as \"sort none\"")},
	{"urlcodec", &c_urlcodec, (HG | M | V | X | EM | TRAWDAT), 0, 0, NIL,
	 N_("URL percent <[path]e[ncode]|[path]d[ecode]> <rest-of-line>")},
{"unfwdignore", &c_unfwdignore, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `unheaderpick'")},
{"unfwdretain", &c_unfwdretain, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `unheaderpick'")},
{"unghost", &c_uncommandalias, (O | M | X | TWYRA), 1, MAC, NIL,
 N_("Please use `uncommandalias'")},
{"unsaveignore", &c_unsaveignore, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `unheaderpick'")},
{"unsaveretain", &c_unsaveretain, (O | M | TRAWLST), 0, MAC, NIL,
 N_("Please use `unheaderpick'")},
{"unthread", &c_unsort, (O | A | SC | TMSGLST), 0, 0, NIL,
 N_("Please use \"sort none\" (disable sorted mode)")},

{"visual", &c_visual, (A | I | M | S | TMSGLST), 0, MMNORM, NIL,
	 N_("(v) Edit <msglist> with $VISUAL")},
	{"varshow", &c_varshow, (HG | M | X | TWYSH), 0, MAC, NIL,
	 N_("Show (*verbose*) informations about all/the given <variables>")},
	{"verify",
#ifdef mx_HAVE_XTLS
	 &c_verify,
#else
	 NIL,
#endif
	 (A | M | SC | TMSGLST), 0, 0, NIL,
	 N_("Verify <msglist>")},
	{"version", &c_version, (M | V | X | NOHIST | TWYSH), 0, 0, NIL,
	 N_("Show the version and feature set of the program")},
	{"vexpr",
#ifdef mx_HAVE_CMD_VEXPR
	 &c_vexpr,
#else
	 NIL,
#endif
	 (HG | M | V | X | EM | TWYSH), 1, MAC, NIL,
	 N_("Evaluate [according to] <operator> [any :<argument>:]")},
	{"vpospar", &c_vpospar, (G | HG | M | V | X | EM | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_vpospar),
	 N_("Positional parameters: <clear>, <quote>, or <set> from :<arg>:")},

	{"write", &c_write, (A | M | SC | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_write),
	 N_("(w) Write (append) [[<msglist>] to <file>]")},

{"xit", &c_exit, (M | X | TWYSH), 0, 1, NIL,
	 N_("(x) Immediately return [<status>] to the shell without saving")},
	{"xcall", &c_xcall, (L | LNMAC | M | X | EM | TARG), 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_call),
	 N_("Replace currently executing macro with macro <name> [:<arg>:]")},

	{"Z", &c_Scroll, (A | M | SC | TWYSH), 0, 1, NIL,
	 N_("Like `z', but continues to the next flagged message")},
	{"z", &c_scroll, (A | M | SC | TWYSH), 0, 1, NIL,
	 N_("Scroll header display as indicated by the argument (0,-,+,$)")},

	/* --MKTAB-END-- */
#undef DS

#undef MAC

#undef TMSGLST
#undef TNDMLST
#undef TRAWDAT
# undef TSTRING
#undef TWYSH
# undef TRAWLST
# undef TWYRA
#undef TARG

#undef A
#undef F
#undef G
#undef HG
#undef I
#undef L
#undef LNMAC
#undef M
#undef O
#undef P
#undef R
#define R su_R
#undef SC
#undef S
#define S su_S
#undef T
#undef V
#undef W
#undef X
#undef NMAC
#undef NOHIST
#undef EM

#endif /* mx_CMD_TAB_H */
/* s-itt-mode */
