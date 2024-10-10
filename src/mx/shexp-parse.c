/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Shell parser; shexp_is_valid_varname()
 *@ TODO - massively undercommented convoluted complicated non-Unicode aware mess
 *@ TODO - should not use auto-memory
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE shexp_parse
#define mx_SOURCE
#define mx_SOURCE_SHEXP_PARSE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <pwd.h>

#ifdef mx_HAVE_FNMATCH
# include <dirent.h>
# include <fnmatch.h>
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/sort.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/utf.h>

#include "mx/cmd.h"
#include "mx/cmd-shortcut.h"
#include "mx/iconv.h"

/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* POSIX says
 *   Utilities volume of POSIX.1-2008 consist solely of uppercase letters, digits, and the <underscore> ('_') from the
 *   characters defined in Portable Character Set and do not begin with a digit.  Other characters may be permitted by
 *   an implementation; applications shall tolerate the presence of such names.
 * We do support the hyphen-minus "-" (except in first and last position.  We support some special parameter names for
 * one-letter(++) variable names; these have counterparts in the code that manages internal variables, and some more
 * special treatment below! */

#define a_SHEXP_ISVARC(C) (su_cs_is_alnum(C) || (C) == '_' || (C) == '-')
/* (Assumed below!) */
#define a_SHEXP_ISVARC_BAD1ST(C) (su_cs_is_digit(C) || (C) == '-')
#define a_SHEXP_ISVARC_BADNST(C) ((C) == '-')

#define a_SHEXP_ISENVVARC(C) (su_cs_is_alnum(C) || (C) == '_')
#define a_SHEXP_ISENVVARC_BAD1ST(C) su_cs_is_digit(C)
#define a_SHEXP_ISENVVARC_BADNST(C) (FAL0)

enum a_shexp_parse_flags{
	a_SHEXP_PARSE_NONE = 0,
	a_SHEXP_PARSE_SKIPQ = 1u<<0, /* Skip rest of this quote (\u0 ..) */
	a_SHEXP_PARSE_SKIPT = 1u<<1, /* Skip entire token (\c@) */
	a_SHEXP_PARSE_SKIPMASK = a_SHEXP_PARSE_SKIPQ | a_SHEXP_PARSE_SKIPT,
	a_SHEXP_PARSE_SURPLUS = 1u<<2, /* Extended sequence interpretation */
	a_SHEXP_PARSE_TOKEN_NEW = 1u<<3, /* e.g., comments are possible */
	a_SHEXP_PARSE_TOKEN_QUOTE_NIL = 1u<<4, /* within " quote, $@ may still "expand-away" (to nothing) */
	a_SHEXP_PARSE_BRACE = 1u<<5, /* Variable substitution: brace enclosed */
	a_SHEXP_PARSE_DIGIT1 = 1u<<6, /* ..first character was digit */
	a_SHEXP_PARSE_NONDIGIT = 1u<<7, /* ..has seen any non-digits */
	a_SHEXP_PARSE_VARSUBST_MASK = su_BITENUM_MASK(5, 7),

	a_SHEXP_PARSE_ROUND_MASK = a_SHEXP_PARSE_SKIPT | S(s32,~su_BITENUM_MASK(0, 7)),
	a_SHEXP_PARSE_COOKIE = 1u<<8, /* Cookie processing in progress */
	a_SHEXP_PARSE_EXPLODE = 1u<<9, /* "$@" / "$*" (latter with !IGN_IFS_SPLIT) explosion to be performed */
	a_SHEXP_PARSE_IFS_SPLIT = 1u<<10, /* $@/$*, unquoted */
	a_SHEXP_PARSE_CHOP_ONE = 1u<<11, /* Remove one more byte from the input after pushing data to output */
	a_SHEXP_PARSE_TMP = 1u<<30
};

enum a_shexp_parse_action{
	a_SHEXP_PARSE_ACTION_GO,
	a_SHEXP_PARSE_ACTION_STOP,
	a_SHEXP_PARSE_ACTION_RESTART,
	a_SHEXP_PARSE_ACTION_RESTART_EMPTY
};

struct a_shexp_parse_ctx{
	BITENUM(u32,n_shexp_parse_flags) spc_flags; /* As given by user */
	BITENUM(u32,a_shexp_parse_flags) spc_state;
	BITENUM(u32,n_shexp_state) spc_res_state; /* As returned to user */
	ZIPENUM(u8,mx_scope) spc_scope;
	char spc_quotec;
	u8 spc__pad[2];
	uz spc_il;
	char const *spc_ib;
	char const *spc_ib_save;
	struct n_string *spc_store;
	struct str *spc_input; /* Only for orig display; normally spc_ib, spc_il */
	/* "$@" or IFS_SPLIT explosion char** storage (if cookie storage passed by user only).
	 * If any !NIL *X has bit 0 set, IFS_SPLIT mode; if terminating NIL has, " quote mode to be re-entered.
	 * GROSS HACK: *X = char**, still void** not void***; i have forgotten why :( */
	void const **spc_cookie;
	char const *spc_ifs;
	char const *spc_ifs_ws;
	char spc_buf[su_IENC_BUFFER_SIZE];
};

/**/
static enum a_shexp_parse_action a_shexp_parse_raw(struct a_shexp_parse_ctx *spcp);
static enum a_shexp_parse_action a_shexp_parse_quote(struct a_shexp_parse_ctx *spcp);

static enum a_shexp_parse_action a_shexp_parse__charbyte(struct a_shexp_parse_ctx *spcp, char c);
static enum a_shexp_parse_action a_shexp_parse__shexp(struct a_shexp_parse_ctx *spcp);

/* Returns new tail if splits occurred and data has to be worked (appended to *slpp, then), NIL otherwise */
static struct n_strlist **a_shexp_parse__ifs_split(struct a_shexp_parse_ctx *spcp, struct n_strlist **slpp,
		char const *cp);

/* Create new spcp->spc_cookie from slp_head upon success; this sets cookie to indicate IFS_SPLIT mode! */
static boole a_shexp_parse__strlist_to_cookie(struct a_shexp_parse_ctx *spcp, struct n_strlist *slp_head);

#define a_SHEXP_ARITH_COOKIE enum mx_scope
#define a_SHEXP_ARITH_ERROR_TRACK
#include "mx/shexp-arith.h" /* $(MX_SRCDIR) */

static enum a_shexp_parse_action
a_shexp_parse_raw(struct a_shexp_parse_ctx *spcp){ /* {{{ */
	/* TODO n_SHEXP_PARSE_META_SEMICOLON++, well, hack: we are not the shell,
	 * TODO we are not a language, and therefore the general *ifs-ws* and normal
	 * TODO whitespace trimming that input lines undergo (in a_go_evaluate())
	 * TODO has already happened, our result will be used *as is*, and therefore
	 * TODO we need to be aware of and remove trailing unquoted WS that would
	 * TODO otherwise remain, after we have seen a semicolon sequencer.
	 * By sheer luck we only need to track this in non-quote-mode */
	u32 last_known_meta_trim_len;
	BITENUM(u32,n_shexp_parse_flags) flags;
	BITENUM(u32,a_shexp_parse_flags) state;
	enum a_shexp_parse_action rv;
	NYD_IN;

	rv = a_SHEXP_PARSE_ACTION_GO;
	flags = spcp->spc_flags;
	state = spcp->spc_state;
	last_known_meta_trim_len = U32_MAX;

	do /*while(spcp->spc_il > 0);*/{
		char c;

		ASSERT(spcp->spc_il > 0);
		--spcp->spc_il, c = *spcp->spc_ib++;

		if(c == '"' || c == '\''){
			state &= ~a_SHEXP_PARSE_TOKEN_NEW;
			last_known_meta_trim_len = U32_MAX;
			if(flags & n_SHEXP_PARSE_IGN_QUOTES)
				goto Jnormal_char;
			/* Empty quotes are pretty common for "" and '', do that quick */
			else if(spcp->spc_il > 0 && *spcp->spc_ib == c){
				ASSERT(!(flags & n_SHEXP_PARSE_QUOTE_AUTO_FIXED));
				--spcp->spc_il, ++spcp->spc_ib;
				spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
				state &= a_SHEXP_PARSE_ROUND_MASK;
				continue;
			}else{
				spcp->spc_quotec = c;
				if(c == '"')
					state |= a_SHEXP_PARSE_SURPLUS | a_SHEXP_PARSE_TOKEN_QUOTE_NIL;
				else
					ASSERT(!(state & a_SHEXP_PARSE_SURPLUS));
				spcp->spc_res_state |= n_SHEXP_STATE_QUOTE;
				break;
			}
		}else if(c == '$'){
			state &= ~a_SHEXP_PARSE_TOKEN_NEW;
			last_known_meta_trim_len = U32_MAX;
			if((flags & n_SHEXP_PARSE_IGN_SUBST_VAR) || spcp->spc_il == 0)
				goto Jnormal_char;

			if(*spcp->spc_ib != '\''){
				spcp->spc_flags = flags;
				spcp->spc_state = state;
				rv = a_shexp_parse__shexp(spcp);
				flags = spcp->spc_flags;
				state = spcp->spc_state;
				if(rv != a_SHEXP_PARSE_ACTION_GO)
					break;
				continue;
			}

			--spcp->spc_il, ++spcp->spc_ib;
			spcp->spc_quotec = '\'';
			state |= a_SHEXP_PARSE_SURPLUS;
			spcp->spc_res_state |= n_SHEXP_STATE_QUOTE;
			break;
		}else if(c == '\\'){
			state &= ~a_SHEXP_PARSE_TOKEN_NEW;
			last_known_meta_trim_len = U32_MAX;
			/* Outside of quotes this just escapes any next character, but a sole <reverse solidus>
			 * at EOS is left unchanged */
			 if(spcp->spc_il > 0){
				--spcp->spc_il, c = *spcp->spc_ib++;
				spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
			 }
		}
		/* A comment may it be if no token has yet started */
		else if(c == '#'){
			if(!(state & a_SHEXP_PARSE_TOKEN_NEW) || (flags & n_SHEXP_PARSE_IGN_COMMENT))
				goto Jnormal_char;
			spcp->spc_ib += spcp->spc_il;
			spcp->spc_il = 0;
			spcp->spc_res_state |= n_SHEXP_STATE_STOP;
			/*last_known_meta_trim_len = U32_MAX;*/
			rv = a_SHEXP_PARSE_ACTION_STOP;
			break;
		}
		/* Metacharacters that separate tokens must be turned on explicitly */
		else if(c == '|'){
			if(!(flags & n_SHEXP_PARSE_META_VERTBAR))
				goto Jnormal_char;
			spcp->spc_res_state |= n_SHEXP_STATE_META_VERTBAR;

			/* The parsed sequence may be _the_ output, so ensure we do not include metacharacter */
			if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP))
				++spcp->spc_il, --spcp->spc_ib;
			/*last_known_meta_trim_len = U32_MAX;*/
			break;
		}else if(c == ';'){
			if(!(flags & n_SHEXP_PARSE_META_SEMICOLON))
				goto Jnormal_char;
			if(!(flags & n_SHEXP_PARSE_DRYRUN) && (spcp->spc_res_state & n_SHEXP_STATE_OUTPUT) &&
					last_known_meta_trim_len != U32_MAX)
				n_string_trunc(spcp->spc_store, last_known_meta_trim_len);

			/* The parsed sequence may be _the_ output, so ensure we do not include metacharacter */
			/*if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP))*/{
				if(!(flags & n_SHEXP_PARSE_META_KEEP))
					state |= a_SHEXP_PARSE_CHOP_ONE;
				++spcp->spc_il, --spcp->spc_ib;
			}
			/*last_known_meta_trim_len = U32_MAX;*/
			spcp->spc_res_state |= n_SHEXP_STATE_META_SEMICOLON | n_SHEXP_STATE_STOP;
			rv = a_SHEXP_PARSE_ACTION_STOP;
			break;
		}else if(c == ','){
			if(!(flags & (n_SHEXP_PARSE_IFS_ADD_COMMA | n_SHEXP_PARSE_IFS_IS_COMMA)))
				goto Jnormal_char;
			/* The parsed sequence may be _the_ output, so ensure we do not include metacharacter */
			if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP)){
				if(!(flags & n_SHEXP_PARSE_META_KEEP))
					state |= a_SHEXP_PARSE_CHOP_ONE;
				++spcp->spc_il, --spcp->spc_ib;
			}
			rv = a_SHEXP_PARSE_ACTION_STOP;
			/*last_known_meta_trim_len = U32_MAX;*/
			break;
		}else Jnormal_char:{
			u8 blnk;

			blnk = su_cs_is_blank(c) ? 1 : 0;
			blnk |= ((flags & (n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_TRIM_IFSSPACE)) &&
					su_cs_find_c(spcp->spc_ifs_ws, c) != NIL) ? 2 : 0;

			if((!(flags & n_SHEXP_PARSE_IFS_VAR) && (blnk & 1)) ||
					((flags & n_SHEXP_PARSE_IFS_VAR) &&
						((blnk & 2) || su_cs_find_c(spcp->spc_ifs, c) != NIL))){
				rv = a_SHEXP_PARSE_ACTION_STOP;

				if(!(flags & n_SHEXP_PARSE_IFS_IS_COMMA)){
					/* The parsed sequence may be _the_ output, so ensure we do
					 * not include the metacharacter, then. */
					if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_META_KEEP)){
						if(!(flags & n_SHEXP_PARSE_META_KEEP))
							state |= a_SHEXP_PARSE_CHOP_ONE;
						++spcp->spc_il, --spcp->spc_ib;
					}
					/*last_known_meta_trim_len = U32_MAX;*/
					break;
				}

				state |= a_SHEXP_PARSE_TOKEN_NEW;
			}else
				state &= ~a_SHEXP_PARSE_TOKEN_NEW;

			if(blnk && spcp->spc_store != NIL){
				if(last_known_meta_trim_len == U32_MAX)
					last_known_meta_trim_len = spcp->spc_store->s_len;
			}else
				last_known_meta_trim_len = U32_MAX;
		}

		if(!(state & a_SHEXP_PARSE_SKIPMASK)){
			spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
			if(su_cs_is_cntrl(c))
				spcp->spc_res_state |= n_SHEXP_STATE_CONTROL;
			if(!(flags & n_SHEXP_PARSE_DRYRUN))
				n_string_push_c(spcp->spc_store, c);
		}

		ASSERT(!(spcp->spc_res_state & n_SHEXP_STATE_STOP));
	}while(spcp->spc_il > 0);

	spcp->spc_flags = flags;
	spcp->spc_state = state;

	NYD_OU;
	return rv;
} /* }}} */

static enum a_shexp_parse_action
a_shexp_parse_quote(struct a_shexp_parse_ctx *spcp){ /* {{{ */
	BITENUM(u32,n_shexp_parse_flags) flags;
	BITENUM(u32,a_shexp_parse_flags) state;
	enum a_shexp_parse_action rv;
	NYD_IN;

	rv = a_SHEXP_PARSE_ACTION_GO;
	flags = spcp->spc_flags;
	state = spcp->spc_state;

	do /*while(spcp->spc_il > 0);*/{
		char c, c2;

		ASSERT(spcp->spc_il > 0);
		--spcp->spc_il, c = *spcp->spc_ib++;

		ASSERT(!(state & a_SHEXP_PARSE_TOKEN_NEW));
		if(c == spcp->spc_quotec){
			if(flags & n_SHEXP_PARSE_QUOTE_AUTO_FIXED)
				goto jquote_normal_char;
			/* Users may need to recognize the presence of empty quotes, yet "$@" may expand NIL */
			if(!(state & a_SHEXP_PARSE_TOKEN_QUOTE_NIL))
				spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
			state &= a_SHEXP_PARSE_ROUND_MASK;
			spcp->spc_quotec = '\0';
			break;
		}else if(c == '\\'){
			if(!(state & a_SHEXP_PARSE_SURPLUS))
				goto jquote_normal_char;
			state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL;

			spcp->spc_ib_save = spcp->spc_ib - 1;
			/* A sole <reverse solidus> at EOS is treated as-is!  This is ok since the "closing
			 * quote" error will occur next, anyway */
			if(spcp->spc_il == 0){
			}else if((c2 = *spcp->spc_ib) == spcp->spc_quotec){
				--spcp->spc_il, ++spcp->spc_ib;
				c = spcp->spc_quotec;
				spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
			}else if(spcp->spc_quotec == '"'){
				/* Double quotes, POSIX says:
				 *   The <backslash> shall retain its special meaning as an escape character
				 *   (see Section 2.2.1) only when followed by one of the following characters
				 *   when considered special: $ ` " \ <newline> */
				switch(c2){
				case '$': FALLTHRU
				case '`': FALLTHRU
				/* case '"': already handled via c2 == spcp->spc_quotec */
				case '\\':
					--spcp->spc_il, ++spcp->spc_ib;
					c = c2;
					spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
					FALLTHRU
				default:
					break;
				}
			}else{
				/* Dollar-single-quote */
				--spcp->spc_il, ++spcp->spc_ib;
				switch(c2){
				case '"':
				/* case '\'': already handled via c2 == spcp->spc_quotec */
				case '\\':
					c = c2;
					spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
					break;

				case 'a': c = '\a'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;
				case 'b': c = '\b'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;
				case 'f': c = '\f'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;
				case 'n': c = '\n'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;
				case 'r': c = '\r'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;
				case 't': c = '\t'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;
				case 'v': c = '\v'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;

				case 'E': FALLTHRU
				case 'e': c = '\033'; spcp->spc_res_state |= n_SHEXP_STATE_CHANGE; break;

				/* Control character */
				case 'c':
					if(spcp->spc_il == 0)
						goto j_dollar_ungetc;
					--spcp->spc_il, c2 = *spcp->spc_ib++;
					/* Careful: \c\ and \c\\ have to be treated alike in POSIX */
					if(c2 == '\\' && spcp->spc_il > 0 && *spcp->spc_ib == '\\')
						--spcp->spc_il, ++spcp->spc_ib;
					spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
					if(state & a_SHEXP_PARSE_SKIPMASK)
						continue;
					/* ASCII C0: 0..1F, 7F <- @.._ (+ a-z -> A-Z), ? */
					c = su_cs_to_upper(c2) ^ 0x40;
					if(S(u8,c) > 0x1F && c != 0x7F){
						if(flags & n_SHEXP_PARSE_LOG)
							n_err(_("Invalid \\c notation: %.*s: %.*s\n"),
								S(int,spcp->spc_input->l), spcp->spc_input->s,
								S(int,P2UZ(spcp->spc_ib - spcp->spc_ib_save)),
								spcp->spc_ib_save);
						spcp->spc_res_state |= n_SHEXP_STATE_ERR_CONTROL;
					}
					/* As an extension, support \c@ EQ printf(1) alike \c */
					if(c == '\0'){
						state |= a_SHEXP_PARSE_SKIPT;
						continue;
					}
					break;
				case '0':
				case '1': case '2': case '3':
				case '4': case '5': case '6': case '7':
				case 'U':
				case 'u':
				case 'X':
				case 'x':
					spcp->spc_flags = flags;
					spcp->spc_state = state;
					rv = a_shexp_parse__charbyte(spcp, c2);
					flags = spcp->spc_flags;
					state = spcp->spc_state;
					if(rv != a_SHEXP_PARSE_ACTION_GO)
						break;
					ASSERT(!(spcp->spc_res_state & n_SHEXP_STATE_STOP));
					continue;

				/* Extension: \$ can be used to enter $xyz multiplexer.
				 * B(ug|ad) effect: if conversion fails, not written "as-is" */
				case '$':
					if(spcp->spc_il == 0)
						goto j_dollar_ungetc;
					goto jvar_expand;

				default:
j_dollar_ungetc:
					/* Follow bash(1) behaviour, print sequence unchanged */
					++spcp->spc_il, --spcp->spc_ib;
					break;
				}
			}
		}else if(c == '$'){
			if(spcp->spc_quotec != '"' || spcp->spc_il == 0)
				goto jquote_normal_char;
jvar_expand:
			spcp->spc_flags = flags;
			spcp->spc_state = state;
			rv = a_shexp_parse__shexp(spcp);
			flags = spcp->spc_flags;
			state = spcp->spc_state;
			if(rv != a_SHEXP_PARSE_ACTION_GO)
				break;
			ASSERT(!(spcp->spc_res_state & n_SHEXP_STATE_STOP));
			continue;
		}else if(c == '`'){ /* TODO sh command */
			state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL; /* xxx ??? */
			if(spcp->spc_quotec != '"' || spcp->spc_il == 0)
				goto jquote_normal_char;
			continue;
		}

jquote_normal_char:
		state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL;
		if(!(state & a_SHEXP_PARSE_SKIPMASK)){
			spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
			if(su_cs_is_cntrl(c))
				spcp->spc_res_state |= n_SHEXP_STATE_CONTROL;
			if(!(flags & n_SHEXP_PARSE_DRYRUN))
				n_string_push_c(spcp->spc_store, c);
		}

		ASSERT(!(spcp->spc_res_state & n_SHEXP_STATE_STOP));
	}while(spcp->spc_il > 0);

	spcp->spc_flags = flags;
	spcp->spc_state = state;

	NYD_OU;
	return rv;
} /* }}} */

static enum a_shexp_parse_action
a_shexp_parse__charbyte(struct a_shexp_parse_ctx *spcp, char c){ /* {{{ */
	uz i;
	char c2;
	BITENUM(u32,a_shexp_parse_flags) state;
	BITENUM(u32,n_shexp_parse_flags) flags;
	enum a_shexp_parse_action rv;
	NYD_IN;

	rv = a_SHEXP_PARSE_ACTION_GO;
	flags = spcp->spc_flags;
	state = spcp->spc_state;

	switch((c2 = c)){
	/* Octal sequence: 1 to 3 octal bytes */
	case '0':
		/* As an extension (dependent on where you look, echo(1), or awk(1)/tr(1) etc.),
		 * allow leading "0" octal indicator */
		if(spcp->spc_il > 0 && (c = *spcp->spc_ib) >= '0' && c <= '7'){
			c2 = c;
			--spcp->spc_il, ++spcp->spc_ib;
		}
		FALLTHRU
	case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
		c2 -= '0';
		if(spcp->spc_il > 0 && (c = *spcp->spc_ib) >= '0' && c <= '7'){
			c2 = (c2 << 3) | (c - '0');
			--spcp->spc_il, ++spcp->spc_ib;
		}
		if(spcp->spc_il > 0 && (c = *spcp->spc_ib) >= '0' && c <= '7'){
			if(!(state & a_SHEXP_PARSE_SKIPMASK) && S(u8,c2) > 0x1F){
				spcp->spc_res_state |= n_SHEXP_STATE_ERR_NUMBER;
				--spcp->spc_il, ++spcp->spc_ib;
				if(flags & n_SHEXP_PARSE_LOG)
					n_err(_("\\0 argument exceeds byte: %.*s: %.*s\n"),
						S(int,spcp->spc_input->l), spcp->spc_input->s,
						S(int,P2UZ(spcp->spc_ib - spcp->spc_ib_save)),
						spcp->spc_ib_save);
				/* Write unchanged */
jerr_ib_save:
				spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
				if(!(flags & n_SHEXP_PARSE_DRYRUN))
					n_string_push_buf(spcp->spc_store, spcp->spc_ib_save,
						P2UZ(spcp->spc_ib - spcp->spc_ib_save));
				break;
			}
			c2 = (c2 << 3) | (c -= '0');
			--spcp->spc_il, ++spcp->spc_ib;
		}

		spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
		if((c = c2) == '\0')
			state |= a_SHEXP_PARSE_SKIPQ;
		if(state & a_SHEXP_PARSE_SKIPMASK)
			break;

jpushc:
		spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
		if(!(flags & n_SHEXP_PARSE_DRYRUN))
			n_string_push_c(spcp->spc_store, c);
		break;

	/* ISO 10646 / Unicode sequence, 8 or 4 hexadecimal bytes */
	case 'U':
		i = 8;
		if(0){
		/* FALLTHRU */
	case 'u':
			i = 4;
		}

		if(spcp->spc_il == 0){
			/* Follow bash(1) behaviour, print sequence unchanged */
			++spcp->spc_il, --spcp->spc_ib;
			break;
		}

		if(0){
			FALLTHRU
	/* Hexadecimal sequence, 1 or 2 hexadecimal bytes */
	case 'X':
	case 'x':
			if(spcp->spc_il == 0){
				/* Follow bash(1) behaviour, print sequence unchanged */
				++spcp->spc_il, --spcp->spc_ib;
				break;
			}
			i = 2;
		}
		/* C99 */{
			static u8 const hexatoi[] = { /* XXX uses ASCII */
				0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
			};
			uz no, j;

			i = MIN(spcp->spc_il, i);
			for(no = j = 0; i-- > 0; --spcp->spc_il, ++spcp->spc_ib, ++j){
				c = *spcp->spc_ib;
				if(su_cs_is_xdigit(c)){
					no <<= 4;
					no += hexatoi[S(u8,(c) - ((c) <= '9' ? 48 : ((c) <= 'F' ? 55 : 87)))];
				}else if(j == 0){
					if(state & a_SHEXP_PARSE_SKIPMASK)
						break;
					c2 = (c2 == 'U' || c2 == 'u') ? 'u' : 'x';
					if(flags & n_SHEXP_PARSE_LOG)
						n_err(_("Invalid \\%c notation: %.*s: %.*s\n"),
							c2, S(int,spcp->spc_input->l), spcp->spc_input->s,
							S(int,P2UZ(spcp->spc_ib - spcp->spc_ib_save)), spcp->spc_ib_save);
					spcp->spc_res_state |= n_SHEXP_STATE_ERR_NUMBER;
					goto jerr_ib_save;
				}else
					break;
			}

			/* Unicode massage */
			if((c2 != 'U' && c2 != 'u') || su_cs_is_ascii(no)){
				if((c = S(char,no)) != '\0')
					goto jpushc;
				state |= a_SHEXP_PARSE_SKIPQ;
			}else if(no == 0)
				state |= a_SHEXP_PARSE_SKIPQ;
			else if(state & a_SHEXP_PARSE_SKIPMASK){
				spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
				break;
			}else{
				if(!(flags & n_SHEXP_PARSE_DRYRUN))
					n_string_reserve(spcp->spc_store, MAX(j, 4));

				if(no > 0x10FFFF){ /* XXX magic; CText */
					if(flags & n_SHEXP_PARSE_LOG)
						n_err(_("\\U argument exceeds 0x10FFFF: %.*s: %.*s\n"),
							S(int,spcp->spc_input->l), spcp->spc_input->s,
							S(int,P2UZ(spcp->spc_ib - spcp->spc_ib_save)), spcp->spc_ib_save);
					spcp->spc_res_state |= n_SHEXP_STATE_ERR_NUMBER;
					/* But normalize the output anyway */
					goto Jerr_uni_norm;
				}

				j = su_utf32_to_8(no, spcp->spc_buf);

				if(n_psonce & n_PSO_UNICODE){
					spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_UNICODE |
							n_SHEXP_STATE_CHANGE;
					if(!(flags & n_SHEXP_PARSE_DRYRUN))
						n_string_push_buf(spcp->spc_store, spcp->spc_buf, j);
					break;
				}
#ifdef mx_HAVE_ICONV
				else{
					char *icp;

					icp = n_iconv_onetime_cp(n_ICONV_NONE, NIL, NIL, spcp->spc_buf);
					if(icp != NIL){
						spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_CHANGE;
						if(!(flags & n_SHEXP_PARSE_DRYRUN))
							n_string_push_cp(spcp->spc_store, icp);
						break;
					}
				}
#endif
				if(!(flags & n_SHEXP_PARSE_DRYRUN)) Jerr_uni_norm:{
					spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_ERR_UNICODE;
					i = snprintf(spcp->spc_buf, sizeof(spcp->spc_buf), "\\%c%0*X",
							(no > 0xFFFFu ? 'U' : 'u'),
							S(int,no > 0xFFFFu ? 8 : 4), S(u32,no));
					n_string_push_buf(spcp->spc_store, spcp->spc_buf, i);
				}
				break;
			}

			spcp->spc_res_state |= n_SHEXP_STATE_CHANGE;
		}
		break;
	}

	spcp->spc_flags = flags;
	spcp->spc_state = state;

	NYD_OU;
	return rv;
} /* }}} */

static enum a_shexp_parse_action
a_shexp_parse__shexp(struct a_shexp_parse_ctx *spcp){ /* {{{ */
	/* $X is a multiplexer for an increasing amount of functionality */
	char const *cp;
	char c2, c;
	BITENUM(u32,a_shexp_parse_flags) state;
	BITENUM(u32,n_shexp_parse_flags) flags;
	enum a_shexp_parse_action rv;
	NYD_IN;

	rv = a_SHEXP_PARSE_ACTION_GO;
	flags = spcp->spc_flags;
	state = spcp->spc_state;

	ASSERT(!(state & a_SHEXP_PARSE_TOKEN_NEW));

	state &= ~a_SHEXP_PARSE_VARSUBST_MASK;
	ASSERT(spcp->spc_il > 0);
	c2 = *spcp->spc_ib;

	/* 1. Arithmetic expression {{{ */
	if(UNLIKELY(c2 == '(')){
		char const *emsg;
		char *xcp;
		s64 res;
		uz i, parens;

		state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL;
		spcp->spc_ib_save = spcp->spc_ib++;
		i = spcp->spc_il--;
		if(i < 4 /*spcp->spc_il < 3*/ || *spcp->spc_ib != '(')
			goto jearith;
		++spcp->spc_ib;
		--spcp->spc_il;

		emsg = NIL; /* -> "Need [recursive] $ expansion" */
		parens = 2;
		while(spcp->spc_il > 0){
			--spcp->spc_il;
			if((c = *spcp->spc_ib++) == '(')
				++parens;
			else if(c == ')'){
				if(--parens == 0)
					goto jearith;
				if(parens == 1 && spcp->spc_il > 0 && *spcp->spc_ib == ')'){
					--spcp->spc_il;
					++spcp->spc_ib;
					parens = 0;
					break;
				}
			}else if(c == '"' || c == '\''){
jebadsub:
				if(flags & n_SHEXP_PARSE_LOG)
					n_err(_("$(( )) expression cannot handle quotes nor recursion: %.*s: %.*s\n"),
						S(int,spcp->spc_input->l), spcp->spc_input->s,
						S(int,i), spcp->spc_ib_save);
				spcp->spc_res_state |= n_SHEXP_STATE_ERR_BADSUB;
				goto jerr_ib_save;
			}else if(c == '$'){
				/* We cannot handle $(( )) recursion, really TODO */
				if(spcp->spc_il > 0 && *spcp->spc_ib == '(')
					goto jebadsub;
				++emsg;
			}

		}
		if(parens > 0){
jearith:
			if(flags & n_SHEXP_PARSE_LOG)
				n_err(_("$(( )) syntax or grouping error: %.*s: %.*s\n"),
					S(int,spcp->spc_input->l), spcp->spc_input->s, S(int,i), spcp->spc_ib_save);
			spcp->spc_res_state |= n_SHEXP_STATE_ERR_BADSUB | n_SHEXP_STATE_ERR_GROUPOPEN;
			goto jerr_ib_save;
		}

		if((flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_IGN_SUBST_ARITH)) ||
				(state & a_SHEXP_PARSE_SKIPMASK)){
			spcp->spc_res_state |= n_SHEXP_STATE_SUBST;
			goto jleave;
		}

		/* Our range-of-interest */
		xcp = UNCONST(char*,&spcp->spc_ib_save[2]);
		i = P2UZ(&spcp->spc_ib[-2] - xcp);

		/* If we have seen variables in the range, we need to expand them XXX very expensively done */
		if(emsg != NIL){
			struct a_xyz{
				void const *cookie;
				struct n_string so;
				struct str si;
			} *xyzp;

			xyzp = su_AUTO_TALLOC(struct a_xyz, 1);
			n_string_creat_auto(&xyzp->so);
			xyzp->cookie = NIL;
			xyzp->si.l = i;
			xyzp->si.s = savestrbuf(xcp, i);

			for(;;){
				BITENUM(u32,n_shexp_state) xyzs;

				xyzs = n_shexp_parse_token(((n_SHEXP_PARSE_LOG | n_SHEXP_PARSE_IGN_EMPTY |
							n_SHEXP_PARSE_IGN_SUBST_IFS_SPLIT) |
						/* XXX SHEXP_PARSE_RECURSIVE_MASK?? */
						(flags & (n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_IFS_IS_COMMA |
							n_SHEXP_PARSE_IGN_COMMENT | n_SHEXP_PARSE_IGN_SUBST_VAR |
							n_SHEXP_PARSE_SCOPE_CAPSULE | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
							n__SHEXP_PARSE_QUOTE_AUTO_MASK | n_SHEXP_PARSE_QUOTE_AUTO_CLOSE |
							n_SHEXP_PARSE_META_MASK | n_SHEXP_PARSE_META_KEEP))),
						spcp->spc_scope, &xyzp->so, &xyzp->si, &xyzp->cookie);
				if(xyzs & n_SHEXP_STATE_ERR_MASK)
					goto jearith;
				if(xyzs & n_SHEXP_STATE_STOP)
					break;
			}

			i = xyzp->so.s_len;
			xcp = n_string_cp(&xyzp->so);
		}

		switch(a_shexp_arith_eval(spcp->spc_scope, &res, xcp, i, &xcp)){
		default:
			cp = su_ienc_s64(spcp->spc_buf, res, 10);
			spcp->spc_res_state |= n_SHEXP_STATE_SUBST;
			goto j_var_push_cp;
#undef a_X
#define a_X(X,N) case CONCAT(a_SHEXP_ARITH_ERR_,X): emsg = N_(N); break
		a_X(NOMEM, "out of memory");
		a_X(SYNTAX, "syntax error");
		a_X(ASSIGN_NO_VAR, "assignment without variable (precedence error?)");
		a_X(DIV_BY_ZERO, "division by zero");
		a_X(EXP_INVALID, "invalid exponent");
		a_X(NO_OP, "syntax error, expected operand");
		a_X(COND_NO_COLON, "syntax error, incomplete ?: condition");
		a_X(COND_PREC_INVALID, "?: condition, invalid precedence (1:v2:v3=3)");
		a_X(NAME_LOOP, "recursive variable name reference");
		a_X(OP_INVALID, "unknown operator");
		}
#undef a_X

		if(flags & n_SHEXP_PARSE_LOG)
			n_err(_("Bad $(()) substitution: %s: %.*s: stop near: %s\n"),
				V_(emsg), S(int,P2UZ(&spcp->spc_ib[-2] - &spcp->spc_ib_save[2])),
				&spcp->spc_ib_save[2], xcp);
		spcp->spc_res_state |= n_SHEXP_STATE_ERR_BADSUB;
		goto jerr_ib_save;
	} /* }}} */
	/* Scan variable name, possibly in braces; expand, possibly explode *@, IFS split etc {{{ */
	else{
		uz i;
		boole rset;
		char const *vp;

		spcp->spc_ib_save = spcp->spc_ib - 1;

		/* Embraced variable name? */
		if(c2 == '{'){
			state |= a_SHEXP_PARSE_BRACE;
			if(spcp->spc_il <= 1)
				goto jebracenoc;
			--spcp->spc_il, ++spcp->spc_ib;
		}

		vp = spcp->spc_ib;
		state &= ~(a_SHEXP_PARSE_EXPLODE | a_SHEXP_PARSE_IFS_SPLIT);

		/* In order to support $^# we need to treat circumflex especially */
		for(rset = FAL0, i = 0; spcp->spc_il > 0; --spcp->spc_il, ++spcp->spc_ib){
			/* We have some special cases regarding special parameters, so ensure
			 * these do not cause failure.  This code has counterparts in code
			 * that manages internal variables! */
			c = *spcp->spc_ib;
			if(!a_SHEXP_ISVARC(c)){
				if(i != 0)
					break;
				switch(c){
				case '^': /* Skip over multiplexer, do not count it for now */
					rset = TRU1;
					continue;
				case '*':
					/* $* is easier for us to handle via normal variable lookup otherwise */
					if(spcp->spc_quotec != '"' && !(flags & n_SHEXP_PARSE_IGN_SUBST_IFS_SPLIT)){
						FALLTHRU
				case '@':
						state |= a_SHEXP_PARSE_EXPLODE;
					}
					FALLTHRU
				case '#': case '?': case '!':
					--spcp->spc_il, ++spcp->spc_ib;
					++i;
					break;
				default:
					break;
				}
				break;
			}else if(a_SHEXP_ISVARC_BAD1ST(c)){
				if(i == 0)
					state |= a_SHEXP_PARSE_DIGIT1;
			}else
				state |= a_SHEXP_PARSE_NONDIGIT;
			++i;
		}
		if(rset)
			++i;

		/* In skip mode, be easy and.. skip over */
		if(state & a_SHEXP_PARSE_SKIPMASK){
			if((state & a_SHEXP_PARSE_BRACE) && spcp->spc_il > 0 && *spcp->spc_ib == '}')
				--spcp->spc_il, ++spcp->spc_ib;
			spcp->spc_res_state |= n_SHEXP_STATE_SUBST;
			goto jleave;
		}

		/* Handle the scan error cases */
		if((state & (a_SHEXP_PARSE_DIGIT1 | a_SHEXP_PARSE_NONDIGIT)
				) == (a_SHEXP_PARSE_DIGIT1 | a_SHEXP_PARSE_NONDIGIT)){
			if(state & a_SHEXP_PARSE_BRACE){
				if(spcp->spc_il > 0 && *spcp->spc_ib == '}')
					--spcp->spc_il, ++spcp->spc_ib;
				else
					spcp->spc_res_state |= n_SHEXP_STATE_ERR_GROUPOPEN;
			}
			if(flags & n_SHEXP_PARSE_LOG)
				n_err(_("Invalid ${} identifier: %.*s: %.*s\n"),
					S(int,spcp->spc_input->l), spcp->spc_input->s,
					S(int,P2UZ(spcp->spc_ib - spcp->spc_ib_save)), spcp->spc_ib_save);
			spcp->spc_res_state |= n_SHEXP_STATE_ERR_IDENTIFIER;
			goto jerr_ib_save;
		}

		/* Empty variable? */
		if(UNLIKELY(i <= ((state & a_SHEXP_PARSE_BRACE) != 0))){
			if(state & a_SHEXP_PARSE_BRACE){
				if(spcp->spc_il == 0 || *spcp->spc_ib != '}'){
jebracenoc:
					if(flags & n_SHEXP_PARSE_LOG)
						n_err(_("No closing brace for ${}: %.*s: %.*s\n"),
							S(int,spcp->spc_input->l), spcp->spc_input->s,
							S(int,P2UZ(spcp->spc_ib - spcp->spc_ib_save)),
								spcp->spc_ib_save);
					spcp->spc_res_state |= n_SHEXP_STATE_ERR_GROUPOPEN;
					goto jerr_ib_save;
				}

				if(i == 0){
					if(flags & n_SHEXP_PARSE_LOG)
						n_err(_("Bad ${} substitution: %.*s: %.*s\n"),
							S(int,spcp->spc_input->l), spcp->spc_input->s,
							S(int,P2UZ(spcp->spc_ib - spcp->spc_ib_save)),
								spcp->spc_ib_save);
					spcp->spc_res_state |= n_SHEXP_STATE_ERR_BADSUB;
					goto jerr_ib_save;
				}
			}
			/* Simply write dollar as-is? */
			else if(!(state & a_SHEXP_PARSE_SKIPMASK)){
				spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
				if(!(flags & n_SHEXP_PARSE_DRYRUN))
					n_string_push_c(spcp->spc_store, '$');
				goto jleave;
			}
		}

		if(state & a_SHEXP_PARSE_BRACE){
			ASSERT(i > 0);
			ASSERT(spcp->spc_il > 0);
			--spcp->spc_il, ++spcp->spc_ib;
		}

		spcp->spc_res_state |= n_SHEXP_STATE_SUBST;

		/* Dry run stops now */
		if(flags & (n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_IGN_SUBST_VAR)){
			state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL; /* xxx not needed */
			goto jleave;
		}

		ASSERT(!(flags & n_SHEXP_PARSE_IGN_SUBST_IFS_SPLIT) || !(state & a_SHEXP_PARSE_IFS_SPLIT));
		/* Not an explosion or $*-to-be-split */
		if(!(state & a_SHEXP_PARSE_EXPLODE) || (state & a_SHEXP_PARSE_IFS_SPLIT)){
			state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL;
			if(spcp->spc_quotec == '\0' && !(flags & n_SHEXP_PARSE_IGN_SUBST_IFS_SPLIT))
				state |= a_SHEXP_PARSE_IFS_SPLIT;
		}
		ASSERT(!(flags & n_SHEXP_PARSE_IGN_SUBST_IFS_SPLIT) || !(state & a_SHEXP_PARSE_IFS_SPLIT));
state&=~a_SHEXP_PARSE_IFS_SPLIT;/* FIXME*/

		/* $@,$*; POSIX 2.5.2, Special Parameters:
		 *	Expands to the positional parameters, starting from one,
		 *	initially producing one field for each positional parameter that is set.
		 * The first exploded cookie will join with the current token in quotes, etc. */
		if(UNLIKELY(state & a_SHEXP_PARSE_EXPLODE) && spcp->spc_cookie != NIL){
			boole xrv;

			ASSERT(!(state & a_SHEXP_PARSE_COOKIE));
			xrv = n_var_vexplode(spcp->spc_cookie, rset);
			if(!xrv){
				*spcp->spc_cookie = NIL;
				goto jleave;
			}

			spcp->spc_input->s = UNCONST(char*,spcp->spc_ib);
			spcp->spc_input->l = spcp->spc_il;

			if(spcp->spc_quotec == '"'){
				char const **xcookie;

				state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL;
				/* NIL terminator needs bit 0 to indicate quote mode */
				xcookie = S(char const**,*spcp->spc_cookie);
				while(*xcookie != NIL)
					++xcookie;
				*xcookie = S(char const*,S(void*,0x1));
			}else if(state & a_SHEXP_PARSE_IFS_SPLIT){
				struct n_strlist *slp_head, **slpp, **xslpp;
				char const **xcookie;
				boole z1, zx, ismulti;

				ASSERT(!(state & a_SHEXP_PARSE_TOKEN_QUOTE_NIL));
				ASSERT(spcp->spc_quotec == '\0');

				xcookie = S(char const**,*spcp->spc_cookie);
				ASSERT(xcookie != NIL && *xcookie != NIL);
				*spcp->spc_cookie = NIL;
				ismulti = (xcookie[1] != NIL);

				/* If $# is 1 and expansion is empty, no field splitting is to be done.
				 * We simply continue processing as if this did not exist */
				if(xrv < FAL0 && !ismulti){
					rv = a_SHEXP_PARSE_ACTION_GO;
					goto jleave;
				}

				z1 = (**xcookie == '\0');
				UNINIT(zx, FAL0);

				slp_head = NIL;
				for(slpp = &slp_head; *xcookie != NIL; ++xcookie){
					zx = (**xcookie == '\0');
					/* Skip an empty trailing field */
					if(!zx || xcookie[1] != NIL){
						xslpp = a_shexp_parse__ifs_split(spcp, slpp, *xcookie);
						if(xslpp != NIL)
							slpp = xslpp;
					}
				}
				if(slp_head == NIL){
					/* Mysteriously this ended up all empty; we must terminate an existing token */
					ASSERT(ismulti);
					rv = (spcp->spc_state & a_SHEXP_PARSE_TOKEN_NEW)
							? a_SHEXP_PARSE_ACTION_RESTART_EMPTY : a_SHEXP_PARSE_ACTION_STOP;
					goto jleave;
				}

				if(!a_shexp_parse__strlist_to_cookie(spcp, slp_head)){
					spcp->spc_res_state |= n_SHEXP_STATE_ERR_BADSUB;
					goto jerr_ib_save;
				}

				if(zx){
					/* Indicate whether last cookie may be joined with rest */
					xcookie = S(char const**,S(void*,R(up,*spcp->spc_cookie) & ~0x1));
					while(*xcookie != NIL)
						++xcookie;
					zx = ((R(up,*xcookie) & 0x1) != 0) | 0x2;
					*xcookie = S(char const*,R(void*,S(up,zx)));
				}

				if(z1 && !(spcp->spc_state & a_SHEXP_PARSE_TOKEN_NEW)){
					if(!ismulti){
					}
					state |= a_SHEXP_PARSE_COOKIE;
					rv = (spcp->spc_quotec == '\0') ? a_SHEXP_PARSE_ACTION_STOP
							: a_SHEXP_PARSE_ACTION_RESTART_EMPTY;
					goto jleave;
				}


				if(!(spcp->spc_state & a_SHEXP_PARSE_TOKEN_NEW)){
					rv = a_SHEXP_PARSE_ACTION_STOP;
					goto jleave;
				}
			}
			/* All empty?  Avoid work */
			else if(xrv < FAL0){
/* FIXME */
				ASSERT(rv == a_SHEXP_PARSE_ACTION_GO);
				*spcp->spc_cookie = NIL;
				goto jleave;
			}

			state |= a_SHEXP_PARSE_COOKIE;
			rv = a_SHEXP_PARSE_ACTION_RESTART_EMPTY;
			goto jleave;
		}
		ASSERT(!(state & a_SHEXP_PARSE_TOKEN_QUOTE_NIL));

		/* We have some common idioms we avoid memory for; xxx Even better would be var_vlook_buf()! */
		if(i == 1){
			switch(*vp){
			case '?': vp = n_qm; state &= ~a_SHEXP_PARSE_IFS_SPLIT; break;
			case '!': vp = n_em; state &= ~a_SHEXP_PARSE_IFS_SPLIT; break;
			case '*': vp = n_star; break;
			case '@': vp = n_at; break;
			case '#': vp = n_ns; state &= ~a_SHEXP_PARSE_IFS_SPLIT; break;
			default: goto j_var_look_buf;
			}
		}else
j_var_look_buf:
			vp = savestrbuf(vp, i);

		/* Check getenv(3) shall no internal variable exist */
		if(UNLIKELY((cp = n_var_vlook(vp, TRU1)) == NIL)){
			if(flags & n_SHEXP_PARSE_SUBST_FLAG_OUTPUT)
				spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
			goto jleave;
		}
		/* An unquoted expansion of $@ and $* undergoes word splitting */
		else if(0&&UNLIKELY(state & a_SHEXP_PARSE_IFS_SPLIT) && spcp->spc_cookie != NIL){
			struct n_strlist *slp_head;

			ASSERT(!(state & a_SHEXP_PARSE_COOKIE));
			ASSERT(spcp->spc_quotec == '\0');

			if(*cp == '\0'){
				rv = a_SHEXP_PARSE_ACTION_GO;
				goto jleave;
			}

			spcp->spc_input->s = UNCONST(char*,spcp->spc_ib);
			spcp->spc_input->l = spcp->spc_il;
			slp_head = NIL;

			if(a_shexp_parse__ifs_split(spcp, &slp_head, cp) != NIL){
				if(slp_head->sl_next == NIL){
					cp = slp_head->sl_dat;
					/*goto j_var_push_cp;*/
				}else{
					char const **xcookie;

					if(!a_shexp_parse__strlist_to_cookie(spcp, slp_head)){
						spcp->spc_res_state |= n_SHEXP_STATE_ERR_BADSUB;
						goto jerr_ib_save;
					}

					/* Indicate whether last cookie may be joined with rest */
					xcookie = S(char const**,S(void*,R(up,*spcp->spc_cookie) & ~0x1));
					while(*xcookie != NIL)
						++xcookie;
					*xcookie = S(char const*,R(void*,
							S(up,((R(up,*xcookie) & 0x1) != 0) | 0x2)));

/* FIXME join to front */
					state |= a_SHEXP_PARSE_COOKIE;
					rv = (spcp->spc_state & a_SHEXP_PARSE_TOKEN_NEW)
							? a_SHEXP_PARSE_ACTION_RESTART_EMPTY
							: a_SHEXP_PARSE_ACTION_STOP;
					goto jleave;
				}
			}else{
				/* Only whitespace */
				spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
				flags &= ~a_SHEXP_PARSE_IFS_SPLIT;
				rv = (spcp->spc_state & a_SHEXP_PARSE_TOKEN_NEW)
						? a_SHEXP_PARSE_ACTION_RESTART_EMPTY
						: a_SHEXP_PARSE_ACTION_STOP;
				goto jleave;
			}
		}

j_var_push_cp:
		spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
		n_string_push_cp(spcp->spc_store, cp);
		for(; (c = *cp) != '\0'; ++cp)
			if(su_cs_is_cntrl(c)){
				spcp->spc_res_state |= n_SHEXP_STATE_CONTROL;
				break;
			}

		flags &= ~a_SHEXP_PARSE_IFS_SPLIT;
		/*goto jleave;*/
	} /* }}} */

jleave:
	spcp->spc_flags = flags;
	spcp->spc_state = state;

	NYD_OU;
	return rv;

	/* Write unchanged */
jerr_ib_save:
	state &= ~a_SHEXP_PARSE_TOKEN_QUOTE_NIL;
	spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT;
	if(!(flags & n_SHEXP_PARSE_DRYRUN))
		n_string_push_buf(spcp->spc_store, spcp->spc_ib_save, P2UZ(spcp->spc_ib - spcp->spc_ib_save));
	goto jleave;
} /* }}} */

static struct n_strlist **
a_shexp_parse__ifs_split(struct a_shexp_parse_ctx *spcp, struct n_strlist **tailp, char const *cp){ /* {{{ */
	struct str trim;
	boole any, dowsp;
	uz i;
	NYD_IN;

	trim.s = UNCONST(char*,cp);
	trim.l = su_cs_len(cp);
	any = FAL0;
	dowsp = (*spcp->spc_ifs == '\0' ? FAL0 : su_cs_is_space(*spcp->spc_ifs) ? TRU1 : TRUM1);

	/* Non-empty, non-WS delimits a field */
	i = trim.l;

	if(dowsp < FAL0){
#if 0
		spcp->spc_res_state |= n_SHEXP_STATE_OUTPUT; /* FIXME  ONLY WITH *ANY* DATA*/
		if(i == 0)
			goto jnode;
#endif
	}else{
		if(dowsp > FAL0 && (spcp->spc_res_state & n_SHEXP_STATE_OUTPUT))
			dowsp = TRUM1;

		if(!(spcp->spc_state & a_SHEXP_PARSE_TOKEN_NEW))
			dowsp = TRUM1;
	}

jouter:
	while(trim.l > 0){
		if(dowsp > FAL0 && n_str_trim_ifs(&trim, n_STR_TRIM_FRONT, FAL0)->l == 0)
			break;

		for(cp = trim.s, i = 1; i <= trim.l; ++cp, ++i){
			char c;

			c = *cp;

			if(*spcp->spc_ifs != '\0' && su_cs_find_c(spcp->spc_ifs, c) != NIL){
				--i;
jnode:
				any = TRU1;
				*tailp = n_STRLIST_AUTO_ALLOC(i);
				(*tailp)->sl_next = NIL;
				su_mem_copy((*tailp)->sl_dat, trim.s, (*tailp)->sl_len = i);
				(*tailp)->sl_dat[i] = '\0';
				tailp = &(*tailp)->sl_next;
				if(i < trim.l)
					++i;
				trim.s += i;
				ASSERT(i <= trim.l);
				trim.l -= i;

				if(dowsp)
					dowsp = TRU1;
				goto jouter;
			}
		}

		if(trim.l > 0){
			i = trim.l;
			goto jnode;
		}
		break;
	}

	NYD_OU;
	return (any ? tailp : NIL);
} /* }}} */

static boole
a_shexp_parse__strlist_to_cookie(struct a_shexp_parse_ctx *spcp, struct n_strlist *slp_head){ /* {{{ */
	char const **rv, **cppx;
	struct n_strlist *slp;
	uz i;
	NYD_IN;

	for(i = 0, slp = slp_head; slp != NIL; ++i, slp = slp->sl_next){
	}

	if(i == 0)
		rv = NIL;
	else{
		++i;
		cppx = rv = su_AUTO_TALLOC(char const*, i);

		/* Cookie needs bit 0 to indicate IFS_SPLIT mode, bit 1 to indicate may-not-join */
		*spcp->spc_cookie = S(char const**,R(void*,R(up,rv) | 0x1));

		for(slp = slp_head; slp != NIL; slp = slp->sl_next)
			*cppx++ = slp->sl_dat;
		*cppx = NIL;
	}

	NYD_OU;
	return (rv != NIL);
} /* }}} */

FL BITENUM(u32,n_shexp_state)
n_shexp_parse_token(BITENUM(u32,n_shexp_parse_flags) flags, enum mx_scope scope,
		struct n_string *store, struct str *input, void const **cookie){ /* {{{ */
	/* TODO shexp_parse_token: WCHAR (+separate in logical units if possible)
	 * TODO This should produce a tree of objects, so that callees can
	 * TODO recognize whether something happened inside single/double etc.
	 * TODO quotes; e.g., to requote "'[a-z]'" to, e.g., "\[a-z]", etc.! */
	struct a_shexp_parse_ctx spc;
	BITENUM(u32,a_shexp_parse_flags) state;
	NYD2_IN;

	ASSERT((flags & n_SHEXP_PARSE_DRYRUN) || store != NIL);
	ASSERT(input != NIL);
	ASSERT(input->l == 0 || input->s != NIL);
	ASSERT(!(flags & n_SHEXP_PARSE_LOG) || !(flags & n_SHEXP_PARSE_LOG_D_V));
	ASSERT(!(flags & n_SHEXP_PARSE_IFS_ADD_COMMA) || !(flags & n_SHEXP_PARSE_IFS_IS_COMMA));
	ASSERT(!(flags & n_SHEXP_PARSE_QUOTE_AUTO_FIXED) || (flags & n__SHEXP_PARSE_QUOTE_AUTO_MASK));

	STRUCT_ZERO_UNTIL(struct a_shexp_parse_ctx, &spc, spc_buf);
	spc.spc_store = store;
	spc.spc_scope = S(u8,scope);

	if((flags & n_SHEXP_PARSE_LOG_D_V) && (n_poption & n_PO_D_V))
		flags |= n_SHEXP_PARSE_LOG;
	if(flags & n_SHEXP_PARSE_QUOTE_AUTO_FIXED)
		flags |= n_SHEXP_PARSE_QUOTE_AUTO_CLOSE;

	if(flags & n_SHEXP_PARSE_SCOPE_CAPSULE){ /* TODO: NOT IMPLEMENTED! */
		scope = mx_SCOPE_LOCAL;
		/* create temporary environment */
	}

	if((flags & n_SHEXP_PARSE_TRUNC) && store != NIL)
		n_string_trunc(store, 0);

	if(flags & (n_SHEXP_PARSE_IFS_VAR | n_SHEXP_PARSE_TRIM_IFSSPACE)){
		spc.spc_ifs = ok_vlook(ifs);
		spc.spc_ifs_ws = ok_vlook(ifs_ws);
	}else
		spc.spc_ifs_ws = spc.spc_ifs = " ";

	state = a_SHEXP_PARSE_NONE;
	spc.spc_ib = input->s;
	if((spc.spc_il = input->l) == UZ_MAX)
		input->l = spc.spc_il = su_cs_len(spc.spc_ib);
	spc.spc_input = input;

	spc.spc_cookie = cookie;
	if(cookie != NIL && *cookie != NIL){
		ASSERT(!(flags & n_SHEXP_PARSE_DRYRUN));
		state |= a_SHEXP_PARSE_COOKIE;
	}

	ASSERT(spc.spc_res_state == n_SHEXP_STATE_NONE);
jrestart_empty:
	spc.spc_res_state &= n_SHEXP_STATE_WS_LEAD;
	state &= a_SHEXP_PARSE_ROUND_MASK;

	/* In cookie mode, the next ARGV entry is the token already, unchanged, since it has been expanded before! */
	if(state & a_SHEXP_PARSE_COOKIE){
		boole lastsep;
		char const * const *xcookie, *cp;
		uz i, nbytes;

		ASSERT(spc.spc_store != NIL);

		i = spc.spc_store->s_len;
		xcookie = S(char const* const*,*spc.spc_cookie);

		lastsep = FAL0;
		if(R(up,xcookie) & 0x1){
			xcookie = S(char const* const*,R(void*,R(up,xcookie) ^ 0x1));
			state |= a_SHEXP_PARSE_IFS_SPLIT;
		}

		if((nbytes = n_string_push_cp(spc.spc_store, *xcookie)->s_len) > 0)
			nbytes -= i;
		spc.spc_res_state |= n_SHEXP_STATE_OUTPUT;

		for(cp = &spc.spc_store->s_dat[i]; nbytes-- > 0;){
			char c;

			c = *cp++;
			if(su_cs_is_cntrl(c)){
				spc.spc_res_state |= n_SHEXP_STATE_CONTROL;
				break;
			}
		}

		lastsep = FAL0;
		if(R(up,*++xcookie) <= 0x3){
			state &= ~(a_SHEXP_PARSE_COOKIE | a_SHEXP_PARSE_EXPLODE | a_SHEXP_PARSE_IFS_SPLIT);
			if(R(up,*xcookie) & 0x2)
				lastsep = TRU1;
			spc.spc_quotec = (R(up,*xcookie) & 0x1) ? '"' : '\0';
			xcookie = NIL;
		}
		*spc.spc_cookie = xcookie;

		/* The last exploded cookie will join with the yielded input token, so simply fall through then */
		if(state & a_SHEXP_PARSE_COOKIE)
			goto jleave_quick;
		if(lastsep && !(spc.spc_res_state & n_SHEXP_STATE_OUTPUT))
			goto jleave;
	}else{
jrestart:
		if(flags & n_SHEXP_PARSE_TRIM_SPACE){
			for(; spc.spc_il > 0; ++spc.spc_ib, --spc.spc_il){
				if(!su_cs_is_space(*spc.spc_ib))
					break;
				spc.spc_res_state |= n_SHEXP_STATE_WS_LEAD;
			}
		}

		if(flags & n_SHEXP_PARSE_TRIM_IFSSPACE){
			for(; spc.spc_il > 0; ++spc.spc_ib, --spc.spc_il){
				if(su_cs_find_c(spc.spc_ifs_ws, *spc.spc_ib) == NIL)
					break;
				spc.spc_res_state |= n_SHEXP_STATE_WS_LEAD;
			}
		}

		spc.spc_input->s = UNCONST(char*,spc.spc_ib);
		spc.spc_input->l = spc.spc_il;
		spc.spc_quotec = '\0';
	}

	if(spc.spc_il == 0){
		spc.spc_res_state |= n_SHEXP_STATE_STOP;
		goto jleave;
	}

	if(spc.spc_store != NIL)
		n_string_reserve(spc.spc_store, MIN(spc.spc_il, 32)); /* XXX */

	switch(flags & n__SHEXP_PARSE_QUOTE_AUTO_MASK){
	case n_SHEXP_PARSE_QUOTE_AUTO_SQ:
		spc.spc_quotec = '\'';
		spc.spc_res_state |= n_SHEXP_STATE_QUOTE;
		break;
	case n_SHEXP_PARSE_QUOTE_AUTO_DQ:
		spc.spc_quotec = '"';
		if(0){
		FALLTHRU
	case n_SHEXP_PARSE_QUOTE_AUTO_DSQ:
			spc.spc_quotec = '\'';
		}
		spc.spc_res_state |= n_SHEXP_STATE_QUOTE;
		state |= a_SHEXP_PARSE_SURPLUS;
		break;
	default:
		if(spc.spc_quotec == '\0')
			state |= a_SHEXP_PARSE_TOKEN_NEW;
		break;
	}

	do{
		enum a_shexp_parse_action rv;

		spc.spc_flags = flags;
		spc.spc_state = state;
		rv = (spc.spc_quotec == '\0') ? a_shexp_parse_raw(&spc) : a_shexp_parse_quote(&spc);
		flags = spc.spc_flags;
		state = spc.spc_state;

		if(rv == a_SHEXP_PARSE_ACTION_STOP){
			if(state & a_SHEXP_PARSE_COOKIE)
				goto jleave_quick;
			break;
		}
		ASSERT(!(spc.spc_res_state & n_SHEXP_STATE_STOP));
		if(rv == a_SHEXP_PARSE_ACTION_RESTART)
			goto jrestart;
		if(rv == a_SHEXP_PARSE_ACTION_RESTART_EMPTY)
			goto jrestart_empty;
		ASSERT(rv == a_SHEXP_PARSE_ACTION_GO);
		if(state & a_SHEXP_PARSE_COOKIE)
			goto jleave_quick;
	}while(spc.spc_il != 0);

	if(spc.spc_quotec != '\0' && !(flags & n_SHEXP_PARSE_QUOTE_AUTO_CLOSE)){
		if(flags & n_SHEXP_PARSE_LOG)
			n_err(_("No closing quote: %.*s\n"), (int)input->l, input->s);
		spc.spc_res_state |= n_SHEXP_STATE_ERR_QUOTEOPEN;
	}

jleave:
	ASSERT(!(state & a_SHEXP_PARSE_COOKIE));
	if((flags & n_SHEXP_PARSE_DRYRUN) && store != NIL){
		uz i;

		i = P2UZ(spc.spc_ib - input->s);
		if(i > 0){
			n_string_push_buf(spc.spc_store, input->s, i);
			spc.spc_res_state |= n_SHEXP_STATE_OUTPUT;
		}
	}

	if(spc.spc_il > 0){
		if(flags & n_SHEXP_PARSE_TRIM_SPACE){
			for(; spc.spc_il > 0; ++spc.spc_ib, --spc.spc_il){
				if(!su_cs_is_space(*spc.spc_ib))
					break;
				spc.spc_res_state |= n_SHEXP_STATE_WS_TRAIL;
				state &= ~a_SHEXP_PARSE_CHOP_ONE;
			}
		}

		if(flags & n_SHEXP_PARSE_TRIM_IFSSPACE){
			for(; spc.spc_il > 0; ++spc.spc_ib, --spc.spc_il){
				if(su_cs_find_c(spc.spc_ifs_ws, *spc.spc_ib) == NIL)
					break;
				spc.spc_res_state |= n_SHEXP_STATE_WS_TRAIL;
				state &= ~a_SHEXP_PARSE_CHOP_ONE;
			}
		}

		if(state & a_SHEXP_PARSE_CHOP_ONE)
			++spc.spc_ib, --spc.spc_il;

		/* At the start of the next token: if this is a comment, simply throw away all the following data! */
		if(spc.spc_il > 0 && *spc.spc_ib == '#' && !(flags & n_SHEXP_PARSE_IGN_COMMENT)){
			spc.spc_ib += spc.spc_il;
			spc.spc_il = 0;
			spc.spc_res_state |= n_SHEXP_STATE_STOP;
		}
	}

	input->l = spc.spc_il;
	input->s = UNCONST(char*,spc.spc_ib);

	if(!(spc.spc_res_state & n_SHEXP_STATE_STOP)){
		if(!(spc.spc_res_state & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_META_MASK)) &&
				(flags & n_SHEXP_PARSE_IGN_EMPTY) && spc.spc_il > 0)
			goto jrestart_empty;
		if(/*!(spc.spc_res_state & n_SHEXP_STATE_OUTPUT) &&*/ spc.spc_il == 0)
			spc.spc_res_state |= n_SHEXP_STATE_STOP;
	}

	if((state & a_SHEXP_PARSE_SKIPT) && !(spc.spc_res_state & n_SHEXP_STATE_STOP) &&
			(flags & n_SHEXP_PARSE_META_MASK))
		goto jrestart;
jleave_quick:
	ASSERT((spc.spc_res_state & n_SHEXP_STATE_OUTPUT) || !(spc.spc_res_state & n_SHEXP_STATE_UNICODE));
	ASSERT((spc.spc_res_state & n_SHEXP_STATE_OUTPUT) || !(spc.spc_res_state & n_SHEXP_STATE_CONTROL));

	if(flags & n_SHEXP_PARSE_SCOPE_CAPSULE){ /* TODO: NOT IMPLEMENTED! */
		/* destroy temporary environment */
	}

	NYD2_OU;
	return spc.spc_res_state;
} /* }}} */

FL char *
n_shexp_parse_token_cp(BITENUM(u32,n_shexp_parse_flags) flags, enum mx_scope scope, char const **cp){
	struct str input;
	struct n_string sou, *soup;
	char *rv;
	BITENUM(u32,n_shexp_state) shs;
	NYD2_IN;

	ASSERT(cp != NIL);

	input.s = UNCONST(char*,*cp);
	input.l = UZ_MAX;
	soup = n_string_creat_auto(&sou);

	shs = n_shexp_parse_token(flags, scope, soup, &input, NIL);
	if(shs & n_SHEXP_STATE_ERR_MASK){
		soup = n_string_assign_cp(soup, *cp);
		*cp = NIL;
	}else
		*cp = input.s;

	rv = n_string_cp(soup);
	/*n_string_gut(n_string_drop_ownership(soup));*/

	NYD2_OU;
	return rv;
}

FL boole
n_shexp_is_valid_varname(char const *name, boole forenviron){
	char lc, c;
	boole rv;
	NYD2_IN;

	rv = FAL0;
	lc = '\0';

	if(!forenviron){
		for(; (c = *name++) != '\0'; lc = c)
			if(!a_SHEXP_ISVARC(c))
				goto jleave;
			else if(lc == '\0' && a_SHEXP_ISVARC_BAD1ST(c))
				goto jleave;
		if(a_SHEXP_ISVARC_BADNST(lc))
			goto jleave;
	}else{
		for(; (c = *name++) != '\0'; lc = c)
			if(!a_SHEXP_ISENVVARC(c))
				goto jleave;
			else if(lc == '\0' && a_SHEXP_ISENVVARC_BAD1ST(c))
				goto jleave;
		if(a_SHEXP_ISENVVARC_BADNST(lc))
			goto jleave;
	}

	rv = TRU1;
jleave:
	NYD2_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_SHEXP_PARSE
/* s-itt-mode */
