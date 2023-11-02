/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Shell quoting.
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
#define su_FILE shexp_quote
#define mx_SOURCE
#define mx_SOURCE_SHEXP_QUOTE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/utf.h>

#include "mx/iconv.h"
#include "mx/ui-str.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_shexp_quote_flags{
	a_SHEXP_QUOTE_NONE,
	a_SHEXP_QUOTE_ROUNDTRIP = 1u<<0, /* Result won't be consumed immediately */

	a_SHEXP_QUOTE_T_REVSOL = 1u<<8, /* Type: by reverse solidus */
	a_SHEXP_QUOTE_T_SINGLE = 1u<<9, /* Type: single-quotes */
	a_SHEXP_QUOTE_T_DOUBLE = 1u<<10, /* Type: double-quotes */
	a_SHEXP_QUOTE_T_DOLLAR = 1u<<11, /* Type: dollar-single-quotes */
	a_SHEXP_QUOTE_T_MASK = a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOUBLE |
			a_SHEXP_QUOTE_T_DOLLAR,

	a_SHEXP_QUOTE__FREESHIFT = 16u
};

struct a_shexp_quote_ctx{
	struct n_string *sqc_store; /* Result storage */
	struct str sqc_input; /* Input data, topmost level */
	u32 sqc_cnt_revso;
	u32 sqc_cnt_single;
	u32 sqc_cnt_double;
	u32 sqc_cnt_dollar;
	enum a_shexp_quote_flags sqc_flags;
	u8 sqc__dummy[4];
};

struct a_shexp_quote_lvl{
	struct a_shexp_quote_lvl *sql_link; /* Outer level */
	struct str sql_dat; /* This level (has to) handle(d) */
	enum a_shexp_quote_flags sql_flags;
	u8 sql__dummy[4];
};

/* Parse an input string and create a sh(1)ell-quoted result */
static void a_shexp_quote(struct a_shexp_quote_ctx *sqcp, struct a_shexp_quote_lvl *sqlp);

static void
a_shexp_quote(struct a_shexp_quote_ctx *sqcp, struct a_shexp_quote_lvl *sqlp){ /* {{{ */
	/* XXX Because of the problems caused by ISO C multibyte interface we cannot
	 * XXX use the recursive implementation because of stateful encodings.
	 * XXX I.e., if a quoted substring cannot be self-contained - the data after
	 * XXX the quote relies on "the former state", then this doesn't make sense.
	 * XXX Therefore this is not fully programmed out but instead only detects
	 * XXX the "most fancy" quoting necessary, and directly does that.
	 * XXX As a result of this, T_REVSOL and T_DOUBLE are not even considered.
	 * XXX Otherwise we rather have to convert to wide first and act on that,
	 * XXX e.g., call visual_info(VISUAL_INFO_WOUT_CREATE) on entire input */
#undef a_SHEXP_QUOTE_RECURSE /* XXX (Needs complete revisit, then) */
#ifdef a_SHEXP_QUOTE_RECURSE
# define jrecurse jrecurse
	struct a_shexp_quote_lvl sql;
#else
# define jrecurse jstep
#endif
	struct mx_visual_info_ctx vic;
	union {struct a_shexp_quote_lvl *head; struct n_string *store;} u;
	u32 flags;
	uz il;
	char const *ib, *ib_base;
	NYD2_IN;

	ib_base = ib = sqlp->sql_dat.s;
	il = sqlp->sql_dat.l;
	flags = sqlp->sql_flags;

	/* Iterate over the entire input, classify characters and type of quotes along the way.  Whenever a quote
	 * change has to be applied, adjust flags for the new situation -, setup sql.* and recurse- */
	while(il > 0){
		char c;

		c = *ib;
		if(su_cs_is_cntrl(c)){
			if(flags & a_SHEXP_QUOTE_T_DOLLAR)
				goto jstep;
			if(c == '\t' && (flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOUBLE)))
				goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
			++sqcp->sqc_cnt_dollar;
#endif
			flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
			goto jrecurse;
		}else if(su_cs_is_space(c) || c == '|' || c == '&' || c == ';' ||
				/* Whereas we do not support those, quote them for the sh(1)ell */
				c == '(' || c == ')' || c == '<' || c == '>' || c == '"' || c == '$'){
			if(flags & a_SHEXP_QUOTE_T_MASK)
				goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
			++sqcp->sqc_cnt_single;
#endif
			flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_SINGLE;
			goto jrecurse;
		}else if(c == '\''){
			if(flags & (a_SHEXP_QUOTE_T_MASK & ~a_SHEXP_QUOTE_T_SINGLE))
				goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
			++sqcp->sqc_cnt_dollar;
#endif
			flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
			goto jrecurse;
		}else if(c == '\\' || (c == '#' && ib == ib_base)){
			if(flags & a_SHEXP_QUOTE_T_MASK)
				goto jstep;
#ifdef a_SHEXP_QUOTE_RECURSE
			++sqcp->sqc_cnt_single;
#endif
			flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_SINGLE;
			goto jrecurse;
		}else if(!su_cs_is_ascii(c)){
			/* Need to keep together multibytes */
#ifdef a_SHEXP_QUOTE_RECURSE
			STRUCT_ZERO(struct mx_visual_info_ctx, &vic);
			vic.vic_indat = ib;
			vic.vic_inlen = il;
			mx_visual_info(&vic, mx_VISUAL_INFO_ONE_CHAR | mx_VISUAL_INFO_SKIP_ERRORS);
#endif
			/* xxx check whether resulting \u would be ASCII */
			if(!(flags & a_SHEXP_QUOTE_ROUNDTRIP) || (flags & a_SHEXP_QUOTE_T_DOLLAR)){
#ifdef a_SHEXP_QUOTE_RECURSE
				ib = vic.vic_oudat;
				il = vic.vic_oulen;
				continue;
#else
				goto jstep;
#endif
			}
#ifdef a_SHEXP_QUOTE_RECURSE
			++sqcp->sqc_cnt_dollar;
#endif
			flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
			goto jrecurse;
		}else
jstep:
			++ib, --il;
	}
	sqlp->sql_flags = flags;

	/* Level made the great and completed processing input.  Reverse the list of levels, detect the "most fancy"
	 * quote type needed along this way */
	/* XXX Due to restriction as above very crude */
	for(flags = 0, il = 0, u.head = NIL; sqlp != NIL;){
		struct a_shexp_quote_lvl *tmp;

		tmp = sqlp->sql_link;
		sqlp->sql_link = u.head;
		u.head = sqlp;
		il += sqlp->sql_dat.l;
		if(sqlp->sql_flags & a_SHEXP_QUOTE_T_MASK)
			il += (sqlp->sql_dat.l >> 1);
		flags |= sqlp->sql_flags;
		sqlp = tmp;
	}
	sqlp = u.head;

	/* Finally work the substrings in the correct order, adjusting quotes along the way as necessary.  Start off
	 * with the "most fancy" quote, so that the user sees an overall boundary she can orientate herself on.  We do
	 * it like that to be able to give the user some "encapsulation experience", to address what strikes me is
	 * a problem of sh(1)ell quoting: different to, e.g., perl(1), where you see at a glance where a string starts
	 * and ends, sh(1) quoting occurs at the "top level", disrupting the visual appearance of "a string" as such */
	u.store = n_string_reserve(sqcp->sqc_store, il);

	if(flags & a_SHEXP_QUOTE_T_DOLLAR){
		u.store = n_string_push_buf(u.store, "$'", sizeof("$'") -1);
		flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOLLAR;
	}else if(flags & a_SHEXP_QUOTE_T_DOUBLE){
		u.store = n_string_push_c(u.store, '"');
		flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_DOUBLE;
	}else if(flags & a_SHEXP_QUOTE_T_SINGLE){
		u.store = n_string_push_c(u.store, '\'');
		flags = (flags & ~a_SHEXP_QUOTE_T_MASK) | a_SHEXP_QUOTE_T_SINGLE;
	}else /*if(flags & a_SHEXP_QUOTE_T_REVSOL)*/
		flags &= ~a_SHEXP_QUOTE_T_MASK;

	/* Work all the levels */
	for(; sqlp != NIL; sqlp = sqlp->sql_link){
		/* As necessary update our mode of quoting */
#ifdef a_SHEXP_QUOTE_RECURSE
		il = 0;

		switch(sqlp->sql_flags & a_SHEXP_QUOTE_T_MASK){
		case a_SHEXP_QUOTE_T_DOLLAR:
			if(!(flags & a_SHEXP_QUOTE_T_DOLLAR))
				il = a_SHEXP_QUOTE_T_DOLLAR;
			break;
		case a_SHEXP_QUOTE_T_DOUBLE:
			if(!(flags & (a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR)))
				il = a_SHEXP_QUOTE_T_DOLLAR;
			break;
		case a_SHEXP_QUOTE_T_SINGLE:
			if(!(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOUBLE |
						a_SHEXP_QUOTE_T_DOLLAR)))
				il = a_SHEXP_QUOTE_T_SINGLE;
			break;
		default:
		case a_SHEXP_QUOTE_T_REVSOL:
			if(!(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOUBLE |
						a_SHEXP_QUOTE_T_DOLLAR)))
				il = a_SHEXP_QUOTE_T_REVSOL;
			break;
		}

		if(il != 0){
			if(flags & (a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOLLAR))
				u.store = n_string_push_c(u.store, '\'');
			else if(flags & a_SHEXP_QUOTE_T_DOUBLE)
				u.store = n_string_push_c(u.store, '"');
			flags &= ~a_SHEXP_QUOTE_T_MASK;

			flags |= (u32)il;
			if(flags & a_SHEXP_QUOTE_T_DOLLAR)
				u.store = n_string_push_buf(u.store, "$'", sizeof("$'") -1);
			else if(flags & a_SHEXP_QUOTE_T_DOUBLE)
				u.store = n_string_push_c(u.store, '"');
			else if(flags & a_SHEXP_QUOTE_T_SINGLE)
				u.store = n_string_push_c(u.store, '\'');
		}
#endif /* a_SHEXP_QUOTE_RECURSE */

		/* Work the level's substring */
		ib = sqlp->sql_dat.s;
		il = sqlp->sql_dat.l;

		while(il > 0){
			char c2, c;

			c = *ib;

			if(su_cs_is_cntrl(c)){
				ASSERT(c == '\t' || (flags & a_SHEXP_QUOTE_T_DOLLAR));
				ASSERT((flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_SINGLE |
					a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR)));
				switch((c2 = c)){
				case 0x07: c = 'a'; break;
				case 0x08: c = 'b'; break;
				case 0x0A: c = 'n'; break;
				case 0x0B: c = 'v'; break;
				case 0x0C: c = 'f'; break;
				case 0x0D: c = 'r'; break;
				case 0x1B: c = 'E'; break;
				default: break;
				case 0x09:
					if(flags & a_SHEXP_QUOTE_T_DOLLAR){
						c = 't';
						break;
					}
					if(flags & a_SHEXP_QUOTE_T_REVSOL)
						u.store = n_string_push_c(u.store, '\\');
					goto jpush;
				}
				u.store = n_string_push_c(u.store, '\\');
				if(c == c2){
					u.store = n_string_push_c(u.store, 'c');
					c ^= 0x40;
				}
				goto jpush;
			}else if(su_cs_is_space(c) || c == '|' || c == '&' || c == ';' ||
					/* Whereas we do not support those, quote them for sh(1)ell */
					c == '(' || c == ')' || c == '<' || c == '>' || c == '"' || c == '$'){
				if(flags & (a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOLLAR))
					goto jpush;
				ASSERT(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_DOUBLE));
				u.store = n_string_push_c(u.store, '\\');
				goto jpush;
			}else if(c == '\''){
				if(flags & a_SHEXP_QUOTE_T_DOUBLE)
					goto jpush;
				ASSERT(!(flags & a_SHEXP_QUOTE_T_SINGLE));
				u.store = n_string_push_c(u.store, '\\');
				goto jpush;
			}else if(c == '\\' || (c == '#' && ib == ib_base)){
				if(flags & a_SHEXP_QUOTE_T_SINGLE)
					goto jpush;
				ASSERT(flags & (a_SHEXP_QUOTE_T_REVSOL | a_SHEXP_QUOTE_T_DOUBLE | a_SHEXP_QUOTE_T_DOLLAR));
				u.store = n_string_push_c(u.store, '\\');
				goto jpush;
			}else if(su_cs_is_ascii(c)){
				/* Shorthand: we can simply push that thing out */
jpush:
				u.store = n_string_push_c(u.store, c);
				++ib, --il;
			}else{
				/* Not an ASCII character, take care not to split up multibyte sequences etc.  For the
				 * sake of compile testing, do not enwrap in mx_HAVE_ALWAYS_UNICODE_LOCALE ||
				 * mx_HAVE_NATCH_CHAR */
				if(n_psonce & n_PSO_UNICODE){
					u32 unic;
					char const *ib2;
					uz il2, il3;

					ib2 = ib;
					il2 = il;
					if((unic = su_utf8_to_32(&ib2, &il2)) != U32_MAX){
						char itoa[32];
						char const *cp;

						il2 = P2UZ(&ib2[0] - &ib[0]);
						if((flags & a_SHEXP_QUOTE_ROUNDTRIP) || unic == 0xFFFDu){
							/* Use padding to make ambiguities impossible */
							il3 = snprintf(itoa, sizeof itoa, "\\%c%0*X",
									(unic > 0xFFFFu ? 'U' : 'u'),
									S(int,unic > 0xFFFFu ? 8 : 4), unic);
							cp = itoa;
						}else{
							il3 = il2;
							cp = &ib[0];
						}
						u.store = n_string_push_buf(u.store, cp, il3);
						ib += il2, il -= il2;
						continue;
					}
				}

				STRUCT_ZERO(struct mx_visual_info_ctx, &vic);
				vic.vic_indat = ib;
				vic.vic_inlen = il;
				mx_visual_info(&vic, mx_VISUAL_INFO_ONE_CHAR | mx_VISUAL_INFO_SKIP_ERRORS);

				/* Work this substring as sensitive as possible */
				il -= vic.vic_oulen;
				if(!(flags & a_SHEXP_QUOTE_ROUNDTRIP))
					u.store = n_string_push_buf(u.store, ib, il);
#ifdef mx_HAVE_ICONV
				else if((vic.vic_indat = n_iconv_onetime_cp(n_ICONV_NONE, "utf-8", ok_vlook(ttycharset),
							savestrbuf(ib, il))) != NIL){
					u32 unic;
					char const *ib2;
					uz il2, il3;

					il2 = su_cs_len(ib2 = vic.vic_indat);
					if((unic = su_utf8_to_32(&ib2, &il2)) != U32_MAX){
						char itoa[32];

						il2 = P2UZ(&ib2[0] - &vic.vic_indat[0]);
						/* Use padding to make ambiguities impossible */
						il3 = snprintf(itoa, sizeof itoa, "\\%c%0*X",
								(unic > 0xFFFFu ? 'U' : 'u'),
								S(int,unic > 0xFFFFu ? 8 : 4), unic);
						u.store = n_string_push_buf(u.store, itoa, il3);
					}else
						goto Jxseq;
				}
#endif
				else
#ifdef mx_HAVE_ICONV
					Jxseq:
#endif
						while(il-- > 0){
					u.store = n_string_push_buf(u.store, "\\xFF", sizeof("\\xFF") -1);
					n_c_to_hex_base16(&u.store->s_dat[u.store->s_len - 2], *ib++);
				}

				ib = vic.vic_oudat;
				il = vic.vic_oulen;
			}
		}
	}

	/* Close an open quote */
	if(flags & (a_SHEXP_QUOTE_T_SINGLE | a_SHEXP_QUOTE_T_DOLLAR))
		u.store = n_string_push_c(u.store, '\'');
	else if(flags & a_SHEXP_QUOTE_T_DOUBLE)
		u.store = n_string_push_c(u.store, '"');
#ifdef a_SHEXP_QUOTE_RECURSE
jleave:
#endif
	NYD2_OU;
	return;

#ifdef a_SHEXP_QUOTE_RECURSE
jrecurse:
	sqlp->sql_dat.l -= il;

	sql.sql_link = sqlp;
	sql.sql_dat.s = UNCONST(char*,ib);
	sql.sql_dat.l = il;
	sql.sql_flags = flags;
	a_shexp_quote(sqcp, &sql);
	goto jleave;
#endif

#undef jrecurse
#undef a_SHEXP_QUOTE_RECURSE
} /* }}} */

FL boole
n_shexp_unquote_one(struct n_string *store, char const *input){
	struct str dat;
	BITENUM(u32,n_shexp_state) shs;
	boole rv;
	NYD_IN;

	dat.s = UNCONST(char*,input);
	dat.l = UZ_MAX;
	shs = n_shexp_parse_token((n_SHEXP_PARSE_TRUNC | n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_LOG |
				n_SHEXP_PARSE_IGN_EMPTY), mx_SCOPE_NONE, store, &dat, NIL);

	if(!(shs & n_SHEXP_STATE_STOP))
		n_err(_("# Only one (shell-quoted) argument is expected: %s\n"), input);

	if((shs & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_STOP | n_SHEXP_STATE_ERR_MASK)
			) != (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_STOP))
		rv = FAL0;
	else if(!(shs & n_SHEXP_STATE_STOP))
		rv = TRUM1;
	else if(shs & n_SHEXP_STATE_OUTPUT)
		rv = TRU1;
	else
		rv = TRU2;

	NYD_OU;
	return rv;
}

FL struct n_string *
n_shexp_quote(struct n_string *store, struct str const *input, boole rndtrip){
	struct a_shexp_quote_lvl sql;
	struct a_shexp_quote_ctx sqc;
	NYD2_IN;

	ASSERT(store != NIL);
	ASSERT(input != NIL);
	ASSERT(input->l == 0 || input->s != NIL);

	STRUCT_ZERO(struct a_shexp_quote_ctx, &sqc);
	sqc.sqc_store = store;
	sqc.sqc_input.s = input->s;
	if((sqc.sqc_input.l = input->l) == UZ_MAX)
		sqc.sqc_input.l = su_cs_len(input->s);
	sqc.sqc_flags = rndtrip ? a_SHEXP_QUOTE_ROUNDTRIP : a_SHEXP_QUOTE_NONE;

	if(sqc.sqc_input.l == 0)
		store = n_string_push_buf(store, "''", sizeof("''") -1);
	else{
		STRUCT_ZERO(struct a_shexp_quote_lvl, &sql);
		sql.sql_dat = sqc.sqc_input;
		sql.sql_flags = sqc.sqc_flags;
		a_shexp_quote(&sqc, &sql);
	}

	NYD2_OU;
	return store;
}

FL char *
n_shexp_quote_cp(char const *cp, boole rndtrip){
	struct n_string store;
	struct str input;
	char *rv;
	NYD2_IN;

	ASSERT(cp != NIL);

	input.s = UNCONST(char*,cp);
	input.l = UZ_MAX;
	rv = n_string_cp(n_shexp_quote(n_string_creat_auto(&store), &input, rndtrip));
	n_string_gut(n_string_drop_ownership(&store));

	NYD2_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_SHEXP_QUOTE
/* s-itt-mode */
