/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd.h.
 *@ TODO The new cmd_arg_parse() argument list parser is
 *@ TODO too stupid yet, however: it should fully support subcommands, too, so
 *@ TODO that, e.g., "vexpr regex" arguments can be fully prepared by the
 *@ TODO generic parser.  But at least a bit.
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
#define su_FILE cmd
#define mx_SOURCE
#define mx_SOURCE_CMD

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/sort.h>

#include "mx/cmd-charsetalias.h"
#include "mx/cmd-cnd.h"
#include "mx/cmd-commandalias.h"
#include "mx/cmd-csop.h"
#include "mx/cmd-edit.h"
#include "mx/cmd-filetype.h"
#include "mx/cmd-fop.h"
#include "mx/cmd-misc.h"
#include "mx/cmd-mlist.h"
#include "mx/cmd-shortcut.h"
#include "mx/cmd-spam.h"
#include "mx/cmd-vexpr.h"
#include "mx/colour.h"
#include "mx/cred-netrc.h"
#include "mx/dig-msg.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/ignore.h"
#include "mx/mailcap.h"
#include "mx/mime-type.h"
#include "mx/mta-aliases.h"
#include "mx/names.h"
#include "mx/privacy.h"
#include "mx/sigs.h"
#include "mx/termios.h"
#include "mx/tty.h"
#include "mx/url.h"

#include "mx/cmd.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Create a multiline info string about all known additional infos for lcp */
static char const *a_cmd_cmdinfo(struct mx_cmd_desc const *cdp);

/* Print a list of all commands */
static int a_cmd_c_list(void *vp);

/* `help' / `?' command */
static int a_cmd_c_help(void *vp);

static char const a_cmd_prefixes[][8] = {">", "eval", "global", "ignerr", "local", "our", "pp", "u", "vput", "wysh"};

/* List of all commands; but first their cmd_arg_desc instances */
#include "mx/cmd-tab.h" /* $(MX_SRCDIR) */
static struct mx_cmd_desc const a_cmd_ctable[] = {
#include <mx/cmd-tab.h>
};

/* And the indexes */
#include "mx/gen-cmd-tab.h" /* $(MX_SRCDIR) */

static char const *
a_cmd_cmdinfo(struct mx_cmd_desc const *cdp){
	struct n_string rvb, *rv;
	char const *cp;
	NYD2_IN;

	rv = n_string_creat_auto(&rvb);
	rv = n_string_reserve(rv, 80);

	switch(cdp->cd_caflags & mx_CMD_ARG_TYPE_MASK){
	case mx_CMD_ARG_TYPE_MSGLIST:
		cp = N_("message-list");
		break;
	case mx_CMD_ARG_TYPE_NDMLIST:
		cp = N_("message-list (without default)");
		break;
	case mx_CMD_ARG_TYPE_STRING:
	case mx_CMD_ARG_TYPE_RAWDAT:
		cp = N_("string data");
		break;
	case mx_CMD_ARG_TYPE_RAWLIST:
		cp = N_("old-style quoting");
		break;
	case mx_CMD_ARG_TYPE_WYRA:
		cp = N_("`wysh' for sh(1)ell-style quoting");
		break;
	case mx_CMD_ARG_TYPE_WYSH:
		cp = (cdp->cd_mflags_o_minargs == 0 && cdp->cd_mmask_o_maxargs == 0)
				? N_("sh(1)ell-style quoting (takes no arguments)")
				: N_("sh(1)ell-style quoting");
		break;
	default:
	case mx_CMD_ARG_TYPE_ARG:{
		u32 flags;
		uz i, ol;
		struct mx_cmd_arg_desc const *cadp;

		rv = n_string_push_cp(rv, _("argument tokens: "));

		for(cadp = cdp->cd_cadp, ol = i = 0; i < cadp->cad_no; ++i){
			flags = cadp->cad_ent_flags[i][0];

			if(flags & mx__CMD_ARG_DESC_TYPE_LIST_WITH_DFLT_MASK){
			}else{
				if(flags & mx_CMD_ARG_DESC_OPTION){
					++ol;
					rv = n_string_push_c(rv, '[');
				}
				if(i != 0){
					rv = n_string_push_c(rv, ',');
					rv = n_string_push_c(rv, ' ');
				}
				if(flags & mx_CMD_ARG_DESC_GREEDY)
					rv = n_string_push_c(rv, ':');
			}

			switch(flags & mx__CMD_ARG_DESC_TYPE_MASK){
			default:
				rv = n_string_push_cp(rv, _("AUA"));
			case mx_CMD_ARG_DESC_SHEXP:
				rv = n_string_push_cp(rv, _("(shell-)token"));
				break;
			case mx_CMD_ARG_DESC_MSGLIST:
				rv = n_string_push_cp(rv, _("[(shell-)msglist (default dot)]"));
				break;
			case mx_CMD_ARG_DESC_MSGLIST_AND_TARGET:
				rv = n_string_push_cp(rv, _("[[(shell-)msglist (default dot)], (shell-)token]"));
				break;
			case mx_CMD_ARG_DESC_NDMSGLIST:
				rv = n_string_push_cp(rv, _("(shell-)msglist (no default)"));
				break;
			}

			if(flags & mx__CMD_ARG_DESC_TYPE_LIST_WITH_DFLT_MASK){

			}else{
				if(flags & mx_CMD_ARG_DESC_GREEDY)
					rv = n_string_push_c(rv, ':');
			}
		}
		while(ol-- > 0)
			rv = n_string_push_c(rv, ']');
		cp = NIL;
		}break;
	}
	if(cp != NIL)
		rv = n_string_push_cp(rv, V_(cp));

	/* Note: on updates, change the manual! */
	if(cdp->cd_caflags & mx_CMD_ARG_G)
		rv = n_string_push_cp(rv, _(" | `global'"));
	if(cdp->cd_caflags & mx_CMD_ARG_L)
		rv = n_string_push_cp(rv, _(" | `local'"));
	if(cdp->cd_caflags & mx_CMD_ARG_O)
		rv = n_string_push_cp(rv, _(" | `our'"));
	if(cdp->cd_caflags & mx_CMD_ARG_U)
		rv = n_string_push_cp(rv, _(" | `u'"));

	if(cdp->cd_caflags & mx_CMD_ARG_V)
		rv = n_string_push_cp(rv, _(" | `>'"));
	if(cdp->cd_caflags & mx_CMD_ARG_EM)
		rv = n_string_push_cp(rv, _(" | *!*"));


	if(cdp->cd_caflags & (mx_CMD_ARG_A | mx_CMD_ARG_I | mx_CMD_ARG_M | mx_CMD_ARG_X | mx_CMD_ARG_NEEDMAC)){
		rv = n_string_push_cp(rv, _(" | yay:"));
		if(cdp->cd_caflags & mx_CMD_ARG_A)
			rv = n_string_push_cp(rv, _(" active-mailbox"));
		if(cdp->cd_caflags & mx_CMD_ARG_I)
			rv = n_string_push_cp(rv, _(" batch/interactive"));
		if(cdp->cd_caflags & mx_CMD_ARG_M)
			rv = n_string_push_cp(rv, _(" send-mode"));
		if(cdp->cd_caflags & mx_CMD_ARG_NEEDMAC)
			rv = n_string_push_cp(rv, _(" macro/account"));
	}

	if(cdp->cd_caflags & (mx_CMD_ARG_R | mx_CMD_ARG_S)){
		rv = n_string_push_cp(rv, _(" | nay:"));
		if(cdp->cd_caflags & mx_CMD_ARG_R)
			rv = n_string_push_cp(rv, _(" compose-mode"));
		if(cdp->cd_caflags & mx_CMD_ARG_S)
			rv = n_string_push_cp(rv, _(" startup"));
		if(cdp->cd_caflags & mx_CMD_ARG_SC)
			rv = n_string_push_cp(rv, _(" startup (pre -X)"));
		if(cdp->cd_caflags & mx_CMD_ARG_W)
			rv = n_string_push_cp(rv, _(" read-only-mailbox"));
	}

	if(cdp->cd_caflags & mx_CMD_ARG_HGABBY)
		rv = n_string_push_cp(rv, _(" | history:gabby"));
	if(cdp->cd_caflags & mx_CMD_ARG_NO_HISTORY)
		rv = n_string_push_cp(rv, _(" | history:ignored"));

	cp = n_string_cp(rv);
	NYD2_OU;
	return cp;
}

static int
a_cmd_c_list(void *vp){
	FILE *fp;
	struct mx_cmd_desc const *cdp, *cdp_max;
	uz scrwid, i, l;
	NYD_IN;
	UNUSED(vp);

	if((fp = mx_fs_tmp_open(NIL, "list", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)) == NIL)
		fp = n_stdout;

	fprintf(fp, _("Commands are:\n"));

	scrwid = mx_TERMIOS_WIDTH_OF_LISTS();
	i = 0;
	l = 1;

	for(cdp = a_cmd_ctable, cdp_max = &cdp[NELEM(a_cmd_ctable)]; cdp < cdp_max; ++cdp){
		char const *pre, *suf;

		if(cdp->cd_func == NIL)
			pre = "[", suf = "]";
		else
			pre = suf = n_empty;

		if(n_poption & n_PO_D_V){
			fprintf(fp, "%s%s%s\n", pre, cdp->cd_name, suf);
			++l;
			fprintf(fp, " : %s%s\n", ((cdp->cd_caflags & mx_CMD_ARG_OBS) ? "OBSOLETE: " : su_empty),
				V_(cdp->cd_doc));
			++l;
			fprintf(fp, " : %s\n", ((cdp->cd_func != NIL ? a_cmd_cmdinfo(cdp)
				: _("command is not compiled in"))));
			++l;
		}else{
			uz j;

			j = su_cs_len(cdp->cd_name);
			if(*pre != '\0')
				j += 2;

			if((i += j + 2) > scrwid){
				i = j;
				fprintf(fp, "\n");
				++l;
			}
			fprintf(fp, (&cdp[1] != cdp_max ? "%s%s%s, " : "%s%s%s\n"), pre, cdp->cd_name, suf);
		}
	}

	if(fp != n_stdout){
		page_or_print(fp, l);

		mx_fs_close(fp);
	}else
		clearerr(fp);

	NYD_OU;
	return su_EX_OK;
}

static int
a_cmd_c_help(void *vp){
	int rv;
	char const *arg;
	FILE *fp;
	NYD_IN;

	if((fp = mx_fs_tmp_open(NIL, "help", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)) == NIL)
		fp = n_stdout;

	/* Help for a single command? */
	if((arg = *S(char const**,vp)) != NIL){
		struct mx_cmd_desc const *cdp, *cdp_max;
		char const *alias_name, *alias_exp, *aepx;

		/* Aliases take precedence, unless disallowed.
		 * Avoid self-recursion; since a commandalias can shadow a command of
		 * equal name allow one level of expansion to return an equal result:
		 * "commandalias q q;commandalias x q;x" should be "x->q->q->quit" */
		alias_name = NIL;
		if(*arg == '\\')
			++arg;
		else while((aepx = mx_commandalias_exists(arg, &alias_exp)) != NIL &&
				(alias_name == NIL || su_cs_cmp(alias_name, aepx))){
			alias_name = aepx;
			fprintf(fp, "%s -> ", arg);
			arg = alias_exp;
		}

		cdp_max = &(cdp = a_cmd_ctable)[NELEM(a_cmd_ctable)];
		cdp = &cdp[a_CMD_CIDX(*arg)];

		for(; cdp < cdp_max; ++cdp){
			if(cdp->cd_func == NIL || !su_cs_starts_with(cdp->cd_name, arg))
				continue;

			fputs(arg, fp);
			if(su_cs_cmp(arg, cdp->cd_name))
				fprintf(fp, " (%s)", cdp->cd_name);
			fprintf(fp, ": %s%s", ((cdp->cd_caflags & mx_CMD_ARG_OBS) ? "OBSOLETE: " : su_empty),
				V_(cdp->cd_doc));
			if(n_poption & n_PO_D_V)
				fprintf(fp, "\n  : %s", a_cmd_cmdinfo(cdp));
			putc('\n', fp);
			rv = 0;
			goto jleave;
		}

		if(alias_name != NIL){
			fprintf(fp, "%s\n", n_shexp_quote_cp(arg, TRU1));
			rv = 0;
		}else{
			n_err(_("Unknown command: `%s'\n"), arg);
			rv = 1;
		}
	}else{
		/* Very ugly, but take care for compiler supported string lengths :( */
		fputs(_(
			"Commands -- <msglist> denotes message specification tokens, e.g.,\n"
			"1-5, :n, @f@Cow or . (current, the \"dot\"), separated by *ifs*:\n"),
			fp);
		fputs(_(
"\n"
"type <msglist>         type (`print') messages (honour `headerpick' etc.)\n"
"Type <msglist>         like `type' but always show all headers\n"
"next                   goto and type next message\n"
"headers                header summary ... for messages surrounding \"dot\"\n"
"search <msglist>       ... for the given expression list (alias for `from')\n"
"delete <msglist>       delete messages (can be `undelete'd)\n"),
			fp);

		fputs(_(
"\n"
"save <msglist> folder  append messages to folder and mark as saved\n"
"copy <msglist> folder  like `save', but do not mark them (`move' moves)\n"
"write <msglist> file   write message contents to file (prompts for parts)\n"
"Reply <msglist>        reply to message sender(s) only\n"
"reply <msglist>        like `Reply', but address all recipients\n"
"Lreply <msglist>       forced mailing list `reply' (see `mlist')\n"),
			fp);

		fputs(_(
"\n"
"mail <recipients>      compose a mail for the given recipients\n"
"file folder            change to another mailbox\n"
"File folder            like `file', but open readonly\n"
"quit                   quit and apply changes to the current mailbox\n"
"xit or exit            like `quit', but discard changes\n"
"!shell command         shell escape\n"
"list                   show all commands (*verbose*)\n"),
			fp);

		rv = (ferror(fp) != 0);
	}

jleave:
	if(fp != n_stdout){
		page_or_print(fp, 0);

		mx_fs_close(fp);
	}else
		clearerr(fp);

	NYD_OU;
	return rv;
}

char const *
mx_cmd_isolate_name(char const *cmd){
	NYD2_IN;

	while(*cmd != '\0' && su_cs_find_c("\\!~|? \t0123456789&%@$^.:/-+*'\",;(`", *cmd) == NIL)
		++cmd;

	NYD2_OU;
	return cmd;
}

boole
mx_cmd_is_valid_name(char const *cmd){
	/* Mirrors things from go.c */
	uz i;
	NYD2_IN;

	i = 0;
	do if(!su_cs_cmp_case(cmd, a_cmd_prefixes[i])){
		cmd = NIL;
		break;
	}while(++i < NELEM(a_cmd_prefixes));

	NYD2_OU;
	return (cmd != NIL);
}

struct mx_cmd_desc const *
mx_cmd_by_arg_desc(struct mx_cmd_arg_desc const *cac_desc){
	struct mx_cmd_desc const *cdp;
	NYD2_IN;

	for(cdp = &a_cmd_ctable[a_CMD_CIDX(*cac_desc->cad_name)]; cdp < &a_cmd_ctable[NELEM(a_cmd_ctable)]; ++cdp)
		if(cdp->cd_cadp == cac_desc)
			goto jleave;

	/* Should not happen though */
	cdp = NIL;
jleave:
	NYD2_OU;
	return cdp;
}

struct mx_cmd_desc const *
mx_cmd_by_name_firstfit(char const *cmd){ /* TODO v15: by_arg_desc; but go.c */
	struct mx_cmd_desc const *cdp;
	char cc, cC, cx;
	NYD2_IN;

	cC = su_cs_to_upper(cc = *cmd);
	cdp = &a_cmd_ctable[a_CMD_CIDX(cc)];
	cc = su_cs_to_lower(cc);

	for(; cdp < &a_cmd_ctable[NELEM(a_cmd_ctable)]; ++cdp)
		if(cdp->cd_func != NIL && su_cs_starts_with(cdp->cd_name, cmd))
			goto jleave;
		else if((cx = *cdp->cd_name) != cc && cx != cC)
			break;

	cdp = NIL;
jleave:
	NYD2_OU;
	return cdp;
}

char **
mx_cmd_by_name_match_all(struct str const *token){
	uz i;
	struct n_strlist *slp_base, **slpp, *xslp;
	boole isaster;
	char **rv;
	NYD2_IN;

	rv = NIL;

	/* bash: leading \ prevents match for non-paths */
	if(*token->s == '\\')
		goto jleave;

	/* Match all? */
	if(token->l == 1 && *token->s == '*'){
		token = NIL;
		isaster = TRU1;
	}
	/* Only if plain command */
	else if(*mx_cmd_isolate_name(token->s) != '\0')
		goto jleave;
	else
		isaster = FAL0;

	slp_base = NIL;
	slpp = &slp_base;

	i = 0;
	do{
		char const *cp;

		cp = a_cmd_prefixes[i];

		if(isaster || !su_cs_cmp_case_n(cp, token->s, token->l)){
			uz j;

			j = su_cs_len(cp);
			*slpp = xslp = n_STRLIST_AUTO_ALLOC(j);
			xslp->sl_next = NIL;
			xslp->sl_len = j;
			su_mem_copy(xslp->sl_dat, cp, ++j);
			slpp = &xslp->sl_next;
		}
	}while(++i < NELEM(a_cmd_prefixes));

	/* C99 */{
		char cc, cC, cx;
		struct mx_cmd_desc const *cdp;

		cdp = &a_cmd_ctable[0];
		if(!isaster){
			cC = su_cs_to_upper(cc = *token->s);
			cc = su_cs_to_lower(cc);
			cdp += a_CMD_CIDX(cc);
		}else{
			UNINIT(cc, '\0');
			UNINIT(cC, '\0');
		}

		for(; cdp < &a_cmd_ctable[NELEM(a_cmd_ctable)]; ++cdp){
			if(cdp->cd_func != NIL &&
					(isaster || su_cs_starts_with_n(cdp->cd_name, token->s, token->l))){
				i = su_cs_len(cdp->cd_name);
				*slpp = xslp = n_STRLIST_AUTO_ALLOC(i);
				xslp->sl_next = NIL;
				xslp->sl_len = i;
				su_mem_copy(xslp->sl_dat, cdp->cd_name, ++i);
				slpp = &xslp->sl_next;
			}else if(!isaster && (cx = *cdp->cd_name) != cc && cx != cC)
				break;
		}
	}

	mx_commandalias_name_match_all(slpp, token);

	if(slp_base == NIL)
		goto jleave;

	for(i = 0, xslp = slp_base; xslp != NIL; ++i, xslp = xslp->sl_next){
	}

	rv = su_AUTO_TALLOC(char*, i +1);
	for(i = 0, xslp = slp_base; xslp != NIL; ++i, xslp = xslp->sl_next)
		rv[i] = xslp->sl_dat;
	rv[i] = NIL;

	su_sort_shell_vpp(S(void const**,rv), i, su_cs_toolbox.tb_cmp);

jleave:
	NYD2_OU;
	return rv;
}

struct mx_cmd_desc const *
mx_cmd_get_default(void){
	struct mx_cmd_desc const *cdp;
	NYD2_IN;

	cdp = &a_cmd_ctable[a_CMD_DEFAULT_IDX];

	NYD2_OU;
	return cdp;
}

boole
mx_cmd_print_synopsis(struct mx_cmd_desc const *cdp_or_nil, FILE *fp_or_nil){
	char const *name, *doc;
	boole rv;
	NYD2_IN;

	rv = TRU1;

	if(!su_state_has(su_STATE_REPRODUCIBLE)){
		name = (cdp_or_nil != NIL) ? cdp_or_nil->cd_name : su_empty;
		if((doc = mx_cmd_get_brief_doc(cdp_or_nil)) != NIL)
			doc = V_(doc);

		if(*name != '\0'){
			if(fp_or_nil == NIL)
				n_err(_("Synopsis: %s: %s\n"), name, doc);
			else
				rv = (fprintf(fp_or_nil, _("Synopsis: %s: %s\n"), name, doc) >= 0);
		}
	}

	NYD2_OU;
	return rv;
}

boole
mx_cmd_arg_parse(struct mx_cmd_arg_ctx *cacp, enum mx_scope scope, boole skip_aka_dryrun){
	enum {a_NONE, a_STOPLOOP = 1u<<0, a_GREEDYJOIN = 1u<<1, a_REDID = 1u<<2};

	struct mx_cmd_arg ncap, *lcap, *target_argp, **target_argpp, *cap;
	struct n_string shou, *shoup;
	struct str shin_orig, shin;
	BITENUM(u32,n_shexp_state) shs;
	struct mx_cmd_arg_desc const *cadp;
	uz parsed_args, cad_idx;
	void const *cookie;
	u8 f;
	s32 nerr;
	NYD_IN;

	ASSERT(cacp->cac_inlen == 0 || cacp->cac_indat != NIL);
#if DVLDBGOR(1, 0)
	/* C99 */{
		boole opt_seen = FAL0;

		for(cadp = cacp->cac_desc, cad_idx = 0; cad_idx < cadp->cad_no; ++cad_idx){
			ASSERT(cadp->cad_ent_flags[cad_idx][0] & mx__CMD_ARG_DESC_TYPE_MASK);

			/* TODO CMD_ARG_DESC_MSGLIST+ may only be used as the last entry */
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST) ||
				cad_idx + 1 == cadp->cad_no);
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_NDMSGLIST) ||
				cad_idx + 1 == cadp->cad_no);
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
				cad_idx + 1 == cadp->cad_no);
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_RAW) ||
				cad_idx + 1 == cadp->cad_no);

			ASSERT(!opt_seen || (cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));
			if(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION)
				opt_seen = TRU1;
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY) ||
				cad_idx + 1 == cadp->cad_no);

			/* TODO CMD_ARG_DESC_MSGLIST+ can only be CMD_ARG_DESC_GREEDY.
			 * TODO And they may not be CMD_ARG_DESC_OPTION */
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST) ||
				(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY));
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_NDMSGLIST) ||
				(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY));
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
				(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY));

			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST) ||
				!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_NDMSGLIST) ||
				!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));
			ASSERT(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
				!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION));

			ASSERT((cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST_AND_TARGET) ||
				!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_MSGLIST_AND_TARGET_NAME_ADDR_OR_GABBY));
		}
	}
#endif /* DVLDBGOR(1,0) */

	nerr = su_ERR_NONE;
	shin.s = UNCONST(char*,cacp->cac_indat);
	shin.l = (cacp->cac_inlen == UZ_MAX ? su_cs_len(shin.s) : cacp->cac_inlen);
	shin_orig = shin;
	cacp->cac_no = 0;
	cacp->cac_scope = cacp->cac_scope_vput = cacp->cac_scope_pp = mx_SCOPE_NONE;
	cacp->cac_arg = lcap = NIL;
	cacp->cac_vput = NIL;

	f = a_NONE;
	cookie = NIL;
	cad_idx = parsed_args = 0;
	cadp = cacp->cac_desc;

	/* Handle 0 args */
	if(UNLIKELY(cacp->cac_desc->cad_no == 0)){
		shoup = n_string_creat_auto(&shou);
		shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
					(skip_aka_dryrun ? n_SHEXP_PARSE_DRYRUN : 0) |
					n_SHEXP_PARSE_META_SEMICOLON | n_SHEXP_PARSE_TRIM_SPACE),
				scope, shoup, &shin, NIL);
		n_SHEXP_STATE_ERR_ADJUST(shs);
		if((shs & n_SHEXP_STATE_META_SEMICOLON) && shin.l > 0){
			ASSERT(shs & n_SHEXP_STATE_STOP);
			mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, shin.s, shin.l);
			shin.l = 0;
		}

		if(shoup->s_len > 0){
			nerr = su_ERR_NOTSUP;
			goto jerr;
		}
		lcap = R(struct mx_cmd_arg*,-1);
		goto jleave;
	}

	/* TODO We need to test >= 0 in order to deal with MSGLIST arguments, as
	 * TODO those use getmsglist() and that needs to deal with that situation.
	 * TODO In the future that should change; see jloop_break TODO below */
	for(; /*shin.l >= 0 &&*/ cad_idx < cadp->cad_no; ++cad_idx){
jredo:
		STRUCT_ZERO(struct mx_cmd_arg, &ncap);
		ncap.ca_indat = shin.s;
		/* >ca_inline once we know */
		su_mem_copy(&ncap.ca_ent_flags[0], &cadp->cad_ent_flags[cad_idx][0], sizeof ncap.ca_ent_flags);
		target_argpp = NIL;
		f &= ~a_STOPLOOP;

		switch(ncap.ca_ent_flags[0] & mx__CMD_ARG_DESC_TYPE_MASK){
		default:
		case mx_CMD_ARG_DESC_SHEXP:
jshexp_restart:
			if(shin.l == 0) goto jloop_break; /* xxx (required grrr) quickshot */

			shoup = n_string_creat_auto(&shou);

			ncap.ca_arg_flags =
			shs = n_shexp_parse_token((ncap.ca_ent_flags[1] | n_SHEXP_PARSE_LOG |
						(skip_aka_dryrun ? n_SHEXP_PARSE_DRYRUN : 0) |
						n_SHEXP_PARSE_META_SEMICOLON | n_SHEXP_PARSE_TRIM_SPACE),
					scope, shoup, &shin,
					(ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_GREEDY ? &cookie : NIL));

			if((shs & n_SHEXP_STATE_META_SEMICOLON) && shin.l > 0){
				ASSERT(shs & n_SHEXP_STATE_STOP);
				mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, shin.s, shin.l);
				shin.l = 0;
			}

			n_SHEXP_STATE_ERR_ADJUST(shs);
			if(shs & n_SHEXP_STATE_ERR_MASK)
				goto jerr;

			ncap.ca_inlen = P2UZ(shin.s - ncap.ca_indat);
			if(shs & n_SHEXP_STATE_OUTPUT){
				ncap.ca_arg.ca_str.s = n_string_cp(shoup);
				ncap.ca_arg.ca_str.l = shou.s_len;
			}

			if((shs & n_SHEXP_STATE_STOP) &&
					(ncap.ca_ent_flags[0] & (mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP))){
				if(!(shs & n_SHEXP_STATE_OUTPUT)){
					/* We would return FAL0 for bind in "bind;echo huhu" or
					 * "reply # comment", whereas we do not for "bind" or "reply"
					 * due to the "shin.l==0 goto jloop_break;" introductional
					 * quickshot; ensure we succeed */
					if(shs & (n_SHEXP_STATE_STOP | n_SHEXP_STATE_META_SEMICOLON))
						goto jloop_break;
					goto jleave;
				}

				/* Succeed if we had any arg */
				f |= a_STOPLOOP;
			}else if(!(shs & n_SHEXP_STATE_OUTPUT)){
				/* Can happen at least now that eval is a modifier, for "eval $1
				 * echo au" where $1 does not exist and thus expand to nothing.
				 * We simply treat it as if it was not given at all */
				goto jshexp_restart;
			}
			break;

		case mx_CMD_ARG_DESC_MSGLIST_AND_TARGET:
			target_argpp = &target_argp;
			/* FALLTHRU */
		case mx_CMD_ARG_DESC_MSGLIST:
		case mx_CMD_ARG_DESC_NDMSGLIST:
			/* TODO _MSGLIST yet at end and greedy only (fast hack).
			 * TODO And consumes too much memory */
			ASSERT(shin.s[shin.l] == '\0');
			n_pstate_err_no = su_ERR_NONE;
			if(n_getmsglist(scope, skip_aka_dryrun, shin.s,
					(ncap.ca_arg.ca_msglist = su_AUTO_CALLOC_N(sizeof *ncap.ca_arg.ca_msglist, msgCount +1)),
					cacp->cac_msgflag, target_argpp) < 0){
				if(n_pstate_err_no != su_ERR_NONE)
					nerr = n_pstate_err_no;
				goto jerr;
			}

			if(ncap.ca_arg.ca_msglist[0] == 0){
				switch(ncap.ca_ent_flags[0] & mx__CMD_ARG_DESC_TYPE_MASK){
				case mx_CMD_ARG_DESC_MSGLIST_AND_TARGET:
				case mx_CMD_ARG_DESC_MSGLIST:
					if((ncap.ca_arg.ca_msglist[0] = first(cacp->cac_msgflag, cacp->cac_msgmask)) == 0){
						if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) || (n_poption & n_PO_D_V))
							n_err(_("No applicable messages\n"));

						nerr = mx_CMD_ARG_DESC_TO_ERRNO(ncap.ca_ent_flags[0]);
						if(nerr == su_ERR_NONE)
							nerr = su_ERR_NOMSG;
						goto jerr;
					}
					ncap.ca_arg.ca_msglist[1] = 0;
					ASSERT(n_msgmark1 == NIL);
					n_msgmark1 = &message[ncap.ca_arg.ca_msglist[0] - 1];

					/* TODO For the MSGLIST_AND_TARGET case an entirely empty input
					 * TODO results in no _TARGET argument: ensure it is there! */
					if(target_argpp != NIL && (cap = *target_argpp) == NIL){
						cap = su_AUTO_CALLOC(sizeof *cap);
						cap->ca_arg.ca_str.s = UNCONST(char*,su_empty);
						*target_argpp = cap;
					}
					/* FALLTHRU */
				default:
					break;
				}
			}else if((ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_MSGLIST_NEEDS_SINGLE) &&
					ncap.ca_arg.ca_msglist[1] != 0){
				if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) || (n_poption & n_PO_D_V))
					n_err(_("Cannot specify multiple messages at once\n"));
				nerr = su_ERR_NOTSUP;
				goto jerr;
			}
			shin.l = 0;
			f |= a_STOPLOOP; /* XXX Asserted to be last above! */

			if(target_argpp != NIL &&
					(ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_MSGLIST_AND_TARGET_NAME_ADDR_OR_GABBY)){
				struct mx_name *np;

				if((np = n_extract_single((*target_argpp)->ca_arg.ca_str.s, GTO)) == NIL ||
						!(np->n_flags & (mx_NAME_ADDRSPEC_ISNAME | mx_NAME_ADDRSPEC_ISADDR)))
					n_pstate |= n_PS_GABBY_FUZZ;
			}

			break;

		case mx_CMD_ARG_DESC_RAW:
			if(shin.l == 0) goto jloop_break; /* xxx (required grrr) quickshot XXX really, for RAW?? */
			shoup = n_string_creat_auto(&shou);
			shoup = n_string_assign_buf(shoup, shin.s, shin.l);
			shin.s += shin.l;
			ncap.ca_inlen = P2UZ(shin.s - ncap.ca_indat);
			ncap.ca_arg_flags = shs = n_SHEXP_STATE_OUTPUT;
			ncap.ca_arg.ca_str.s = n_string_cp(shoup);
			ncap.ca_arg.ca_str.l = shoup->s_len;
			f |= a_STOPLOOP;
			break;
		}
		++parsed_args;

		if(f & a_GREEDYJOIN){ /* TODO speed this up! */
			char *cp;
			uz i;

			ASSERT((ncap.ca_ent_flags[0] & mx__CMD_ARG_DESC_TYPE_MASK) != mx_CMD_ARG_DESC_MSGLIST);
			ASSERT(lcap != NIL);
			ASSERT(target_argpp == NIL);
			i = lcap->ca_arg.ca_str.l;
			lcap->ca_arg.ca_str.l += 1 + ncap.ca_arg.ca_str.l;
			cp = su_AUTO_ALLOC(lcap->ca_arg.ca_str.l +1);
			su_mem_copy(cp, lcap->ca_arg.ca_str.s, i);
			lcap->ca_arg.ca_str.s = cp;
			cp[i++] = ' ';
			su_mem_copy(&cp[i], ncap.ca_arg.ca_str.s, ncap.ca_arg.ca_str.l +1);
		}else{
			cap = su_AUTO_ALLOC(sizeof *cap);
			su_mem_copy(cap, &ncap, sizeof ncap);
			if(lcap == NIL)
				cacp->cac_arg = cap;
			else
				lcap->ca_next = cap;
			lcap = cap;
			if(++cacp->cac_no == U32_MAX){
				nerr = su_ERR_OVERFLOW;
				goto jerr;
			}

			if(target_argpp != NIL){
				lcap->ca_next = cap = *target_argpp;
				if(cap != NIL){
					lcap = cap;
					if(++cacp->cac_no == U32_MAX){
						nerr = su_ERR_OVERFLOW;
						goto jerr;
					}
				}
			}
		}

		if(f & a_STOPLOOP)
			goto jleave;

		if((shin.l > 0 || cookie != NIL) &&
				(ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_GREEDY)){
			if(!(f & a_GREEDYJOIN) && ((ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_GREEDY_JOIN) &&
					(ncap.ca_ent_flags[0] & mx_CMD_ARG_DESC_SHEXP)))
				f |= a_GREEDYJOIN;
			f |= a_REDID;
			goto jredo;
		}
	}

jloop_break:
	ASSERT(cad_idx < cadp->cad_no || !(f & a_REDID) ||
		((f & a_REDID) && cad_idx + 1 == cadp->cad_no &&
		 (cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_GREEDY)));
	if(!(f & a_REDID) && cad_idx < cadp->cad_no){
		if(!(cadp->cad_ent_flags[cad_idx][0] & mx_CMD_ARG_DESC_OPTION))
			goto jerr;
	}else if(!(f & a_STOPLOOP) && shin.l > 0){
		nerr = su_ERR_2BIG;
		goto jerr;
	}

	lcap = R(struct mx_cmd_arg*,-1);
jleave:
	n_pstate_err_no = nerr;

	NYD_OU;
	return (lcap != NIL);

jerr:
	if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) || (n_poption & n_PO_D_V)){
		if(nerr != su_ERR_NONE)
			n_err(_("%s: %s\n"), cadp->cad_name, su_err_doc(nerr));
		else{
			uz i;

			for(i = 0; (i < cadp->cad_no && !(cadp->cad_ent_flags[i][0] & mx_CMD_ARG_DESC_OPTION)); ++i){
			}

			shoup = n_string_creat_auto(&shou);
			shoup = n_shexp_quote(shoup, &shin_orig, TRU1);
			shin_orig.s = n_string_cp(shoup);
			shin_orig.l = shoup->s_len;

			shoup = n_string_creat_auto(&shou);
			shoup = n_shexp_quote(shoup, &shin, TRU1);
			shin.s = n_string_cp(shoup);
			shin.l = shoup->s_len;

			n_err(_("%s: invalid argument(s), stopped after %" PRIuZ " of %" PRIuZ "%s\n"
					"  Input was %s, Rest is %s\n"),
				cadp->cad_name, parsed_args, i, (i == cadp->cad_no ? su_empty : _(" (or more)")),
				shin_orig.s, shin.s);
		}

		if(!su_state_has(su_STATE_REPRODUCIBLE))
			mx_cmd_print_synopsis(mx_cmd_by_name_firstfit(cadp->cad_name), NIL);
	}

	if(nerr == su_ERR_NONE)
		nerr = su_ERR_INVAL;

	lcap = NIL;
	goto jleave;
}

void *
mx_cmd_arg_save_to_bag(struct mx_cmd_arg_ctx const *cacp, void *vp){
	struct mx_cmd_arg *ncap;
	struct mx_cmd_arg_ctx *ncacp;
	char *buf;
	struct mx_cmd_arg const *cap;
	uz len, i;
	struct su_mem_bag *mbp;
	NYD2_IN;

	mbp = vp;

	/* For simplicity, save it all in once chunk xxx no longer necessary! */
	len = sizeof *cacp;
	for(cap = cacp->cac_arg; cap != NIL; cap = cap->ca_next){
		i = cap->ca_arg.ca_str.l +1;
		i = ALIGN_Z(i);
		len += sizeof(*cap) + i;
	}
	if(cacp->cac_vput != NIL)
		len += su_cs_len(cacp->cac_vput) +1;

	ncacp = su_MEM_BAG_AUTO_ALLOCATE(mbp, sizeof(char), len, su_MEM_BAG_ALLOC_MUSTFAIL);
	*ncacp = *cacp;
	buf = R(char*,&ncacp[1]);

	for(ncap = NIL, cap = cacp->cac_arg; cap != NIL; cap = cap->ca_next){
		vp = buf;
		DVLDBG( STRUCT_ZERO(struct mx_cmd_arg_ctx, vp); )

		if(ncap == NIL)
			ncacp->cac_arg = vp;
		else
			ncap->ca_next = vp;
		ncap = vp;
		ncap->ca_next = NIL;
		ncap->ca_ent_flags[0] = cap->ca_ent_flags[0];
		ncap->ca_ent_flags[1] = cap->ca_ent_flags[1];
		ncap->ca_arg_flags = cap->ca_arg_flags;
		su_mem_copy(ncap->ca_arg.ca_str.s = R(char*,&ncap[1]), cap->ca_arg.ca_str.s,
			(i = (ncap->ca_arg.ca_str.l = cap->ca_arg.ca_str.l) +1));

		i = ALIGN_Z(i);
		buf += sizeof(*ncap) + i;
	}

	if(cacp->cac_vput != NIL){
		ncacp->cac_vput = buf;
		su_mem_copy(buf, cacp->cac_vput, su_cs_len(cacp->cac_vput) +1);
	}else
		ncacp->cac_vput = NIL;

	NYD2_OU;
	return ncacp;
}

int
getrawlist(enum mx_scope scope, boole wysh/* v15-cpmpat */, boole skip_aka_dryrun, char **res_dat, uz res_size,
		char const *line, uz linesize){
	int res_no;
	NYD_IN;

	n_pstate &= ~n_PS_GABBY_FUZZ;

	if(res_size == 0){
		res_no = -1;
		goto jleave;
	}else if(UCMP(z, res_size, >, INT_MAX))
		res_size = INT_MAX;
	else
		--res_size;
	res_no = 0;

	if(!wysh){
		/* And assuming result won't grow input */
		char c2, c, quotec, *cp2, *linebuf;

		linebuf = su_LOFI_ALLOC(linesize);

		for(;;){
			for(; su_cs_is_blank(*line); ++line)
				;
			if(*line == '\0')
				break;

			if(UCMP(z, res_no, >=, res_size)){
				n_err(_("Too many input tokens for result storage\n"));
				res_no = -1;
				break;
			}

			cp2 = linebuf;
			quotec = '\0';

			/* TODO v15: complete switch in order mirror known behaviour */
			while((c = *line++) != '\0'){
				if(quotec != '\0'){
					if(c == quotec){
						quotec = '\0';
						continue;
					}else if(c == '\\'){
						if((c2 = *line++) == quotec)
							c = c2;
						else
							--line;
					}
				}else if(c == '"' || c == '\''){
					quotec = c;
					continue;
				}else if(c == '\\'){
					if((c2 = *line++) != '\0')
						c = c2;
					else
						--line;
				}else if(su_cs_is_blank(c))
					break;
				*cp2++ = c;
			}

			res_dat[res_no++] = savestrbuf(linebuf, P2UZ(cp2 - linebuf));
			if(c == '\0')
				break;
		}

		su_LOFI_FREE(linebuf);
	}else{
		/* sh(1) compat mode.  Prepare shell token-wise */
		struct n_string store;
		struct str input;
		void const *cookie;

		n_string_creat_auto(&store);
		input.s = UNCONST(char*,line);
		input.l = linesize;
		cookie = NIL;

		for(;;){
			if(UCMP(z, res_no, >=, res_size)){
				n_err(_("Too many input tokens for result storage\n"));
				res_no = -1;
				break;
			}

			/* C99 */{
				BITENUM(u32,n_shexp_state) shs;

				shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
						(cookie == NIL ? n_SHEXP_PARSE_TRIM_SPACE : 0) |
						(skip_aka_dryrun ? n_SHEXP_PARSE_DRYRUN : 0) |
						/* TODO not here in old style n_SHEXP_PARSE_IFS_VAR |*/
						n_SHEXP_PARSE_META_SEMICOLON), scope,
						(skip_aka_dryrun ? NIL : &store), &input, &cookie);

				if((shs & n_SHEXP_STATE_META_SEMICOLON) && input.l > 0){
					ASSERT(shs & n_SHEXP_STATE_STOP);
					mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, input.s, input.l);
				}

				n_SHEXP_STATE_ERR_ADJUST(shs);
				if(shs & n_SHEXP_STATE_ERR_MASK){
					res_no = -1;
					break;
				}

				if(shs & n_SHEXP_STATE_OUTPUT){
					res_dat[res_no++] = n_string_cp(&store);
					n_string_drop_ownership(&store);
				}

				if(shs & n_SHEXP_STATE_STOP)
					break;
			}
		}

		n_string_gut(&store);
	}

	if(res_no >= 0)
		res_dat[(uz)res_no] = NIL;
jleave:
	NYD_OU;
	return res_no;
}

boole
mx_cmd_eval(u32 cnt, enum mx_scope scope, struct str *io, char const *prefix_or_nil){
	mx_CMD_ARG_DESC_SUBCLASS_DEF(eval, 1, a_cmd_cad_eval){
		{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_HONOUR_STOP,
		 n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_TRIM_IFSSPACE} /* args */
	}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

	static struct mx_cmd_desc const a_cmd_eval = {
		"eval", R(int(*)(void*),-1), mx_CMD_ARG_TYPE_ARG, 0, 0, mx_CMD_ARG_DESC_SUBCLASS_CAST(&a_cmd_cad_eval), NIL
	};

	struct mx_cmd_arg_ctx cac;
	struct n_string as_b, *asp;
	struct mx_cmd_arg *cap;
	uz i, j;
	boole rv;
	NYD_IN;

	rv = FAL0;

	while(cnt > 0){
		su_STRUCT_ZERO(struct mx_cmd_arg_ctx, &cac);
		cac.cac_desc = a_cmd_eval.cd_cadp;
		cac.cac_indat = io->s;
		cac.cac_inlen = io->l;
		if(!mx_cmd_arg_parse(&cac, scope, FAL0))
			goto jleave;

		for(i = 0, cap = cac.cac_arg; cap != NIL; cap = cap->ca_next)
			i += cap->ca_arg.ca_str.l + 1;
		if(prefix_or_nil != NIL)
			i += (j = su_cs_len(prefix_or_nil) +1);
		else
			j = 0;

		asp = n_string_creat_auto(&as_b);
		asp = n_string_reserve(asp, i);

		for(cap = cac.cac_arg; cap != NIL; cap = cap->ca_next){
			if(asp->s_len > 0)
				asp = n_string_push_c(asp, ' ');
			asp = n_string_push_buf(asp, cap->ca_arg.ca_str.s, cap->ca_arg.ca_str.l);
		}

		if(--cnt == 0 && j > 0){
			asp = n_string_unshift_buf(asp, prefix_or_nil, j);
			asp->s_dat[--j] = ' ';
		}

		io->s = n_string_cp(asp);
		io->l = asp->s_len;
		/* n_string_gut(n_string_drop_ownership(asp)); */
	}

	rv = TRU1;
jleave:
	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD
/* s-itt-mode */
