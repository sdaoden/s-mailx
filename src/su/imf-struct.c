/*@ Implementation of imf.h, parse_struct_header().
 *
 * Copyright (c) 2024 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_imf_struct
#define su_SOURCE
#define su_SOURCE_IMF_STRUCT

#include "su/code.h"

su_EMPTY_FILE()
#ifdef su_HAVE_IMF

#include "su/imf.h"
#include "su/y-imf.h" /* $(SU_SRCDIR) */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

#include <su/y-imf.h> /* 2. */

s32
su_imf_parse_struct_header(struct su_imf_shtok **shtpp, char const *header, BITENUM(u32,su_imf_mode) mode, /* {{{ */
		struct su_mem_bag *membp, char const **endptr_or_nil){
	s32 rv;
	struct su__imf_actx *acp;
	uz i;
	struct su_imf_shtok **shtpp_base, *shtp;
	NYD_IN;
	ASSERT_NYD_EXEC(shtpp != NIL, rv = -su_ERR_NODATA);
	ASSERT_NYD_EXEC(header != NIL, rv = -su_ERR_NODATA);
	ASSERT_NYD_EXEC(membp != NIL, rv = -su_ERR_NODATA);
	ASSERT_EXEC((mode & ~su__IMF_MODE_STRUCT_MASK) == 0, mode &= su__IMF_MODE_STRUCT_MASK);

	*(shtpp_base = shtpp) = NIL;

	while(su_imf_c_ANY_WSP(*header))
		++header;

	i = su_cs_len(header);
	if(i == 0){
		rv = -su_ERR_NODATA;
		goto j_leave;
	}

	/* S32_MAX is mem-bag stuff, 2 is 1 string of maximum size plus room for the structure as such.
	 * We may re-quote data, so reserve two bytes for quotes, and one for NUL */
	if(i >= (S32_MAX - 1*2 - 1*1) / 2){
		rv = -su_ERR_OVERFLOW;
		goto j_leave;
	}else if(i < VSTRUCT_SIZEOF(struct su__imf_actx,ac_dat))
		i = VSTRUCT_SIZEOF(struct su__imf_actx,ac_dat);
	i += 2 +1;

	acp = su__IMF_ALLOC(membp, i << 1);
	if(acp == NIL){
		rv = -su_ERR_NOMEM;
		goto j_leave;
	}

	acp->ac_.hd = header;
	acp->ac_.mse = mode;
	/* Yes.  This looks hacky.  But dig it */
	acp->ac_group_display_name = acp->ac_display_name = acp->ac_domain = acp->ac_locpar = NIL;
	acp->ac_comm = acp->ac_dat;

	/* Simply: split tokens after CFWS (or, optionally, semicolon) */
	shtp = NIL;
jtoken_next:
	rv = su_ERR_NONE;

	STRUCT_ZERO_FROM(struct su__imf_x, &acp->ac_, comm);
	acp->ac_.mse &= su__IMF_MODE_STRUCT_MASK;

	/* In order to correlate semicolon to h1 in "(c1) h1 (c2);" without TOK_COMMENT we need to delay STOP_EARLY */
	for(rv = su_IMF_ERR_CONTENT; *acp->ac_.hd != '\0';){
		enum{
			a_NONE,
			a_ANY = 1u<<0,
			a_ANY_EMPTY_QUOTE = 1u<<1,
			a_QUOTE = 1u<<2,
			a_QUOTE_EMPTY = 1u<<3
		};

		u32 f;
		char const *start;

		start = acp->ac_.hd;
		f = a_NONE;

		if(!su__imf_s_CFWS(acp)){
			rv = (acp->ac_.mse & (su_IMF_ERR_RELAX | su_IMF_ERR_MASK));
			goto jleave;
		}
		if(acp->ac_.comm > 0){
			if(mode & su_IMF_MODE_TOK_COMMENT){
				if((mode & su_IMF_MODE_STOP_EARLY) && shtp != NIL)
					goto jdone;
				acp->ac_.mse |= su_IMF_STATE_COMMENT;
				goto jtoken_create;
			}
			acp->ac_.comm = 0;
		}

		if(start != acp->ac_.hd)
			f |= a_ANY;

		for(i = 0;;){
			char c;

			start = acp->ac_.hd;
			f &= ~a_QUOTE_EMPTY;

			if(su_imf_c_DQUOTE(*acp->ac_.hd)){
				/* Remove quotes, they are "not [a] semantical[ly] part" */
				uz j;
				char *cp;

				cp = su__imf_s_quoted_string(acp, &acp->ac_comm[i]);
				if(cp == NIL){
					rv = (acp->ac_.mse & su_IMF_ERR_MASK);
					goto jleave;
				}
				j = P2UZ(cp - &acp->ac_comm[i]);
				f |= (j == 0) ? a_ANY_EMPTY_QUOTE | a_QUOTE_EMPTY : a_QUOTE;
				i += j;
			}

			while(su_imf_c_atext((c = *acp->ac_.hd))){
				acp->ac_comm[i++] = c;
				++acp->ac_.hd;
			}

			if(mode & su_IMF_MODE_DOT_ATEXT){
				while(*acp->ac_.hd == '.'){
					++acp->ac_.hd;
					acp->ac_comm[i++] = '.';
				}
			}

			if(start == acp->ac_.hd){
				if(i == 0){
					if(!(f & a_QUOTE_EMPTY) || !(mode & su_IMF_MODE_TOK_EMPTY))
						break;
					f |= a_QUOTE;
				}
				if((mode & su_IMF_MODE_STOP_EARLY) && shtp != NIL)
					goto jdone;
				acp->ac_.comm = S(u32,i);
				goto jtoken_create;
			}
		}

		if(*acp->ac_.hd == ';'){
			if(!(mode & su_IMF_MODE_TOK_SEMICOLON))
				goto jleave;
			++acp->ac_.hd;
			if(shtp != NIL){
				boole yet;

				yet = ((shtp->imfsht_mse & su_IMF_STATE_SEMICOLON) != 0);
				if(!yet)
					shtp->imfsht_mse |= su_IMF_STATE_SEMICOLON;
				if(mode & su_IMF_MODE_STOP_EARLY)
					goto jdone;
				if(!yet)
					continue;
			}else
				ASSERT(acp->ac_.comm == 0);

			if(mode & su_IMF_MODE_TOK_EMPTY){
				acp->ac_.mse |= su_IMF_STATE_SEMICOLON;
				goto jtoken_create;
			}
		}else if(f & a_ANY_EMPTY_QUOTE){
			if(shtp != NIL && (mode & su_IMF_MODE_STOP_EARLY))
				goto jdone;
			if(mode & su_IMF_MODE_TOK_EMPTY)
				goto jtoken_create;
		}else if(!(f & a_ANY))
			goto jleave;
		continue;

jtoken_create:
		i = acp->ac_.comm;
		i = VSTRUCT_SIZEOF(struct su_imf_shtok,imfsht_dat) + i + 2 +1;

		shtp = su__IMF_ALLOC(membp, i);
		*shtpp = shtp;
		shtpp = &shtp->imfsht_next;

		shtp->imfsht_next = NIL;
		shtp->imfsht_mse = (acp->ac_.mse & ~su__IMF_MODE_STRUCT_MASK);

		/* C99 */{
			char *cp;

			cp = shtp->imfsht_dat;
			if(f & a_QUOTE)
				*cp++ = '"';
			su_mem_copy(cp, acp->ac_comm, acp->ac_.comm);
			cp += acp->ac_.comm;
			if(f & a_QUOTE)
				*cp++ = '"';
			*cp = '\0';

			shtp->imfsht_len = S(u32,P2UZ(cp - shtp->imfsht_dat));
		}
		goto jtoken_next;
	}
	ASSERT(*acp->ac_.hd == '\0');

jdone:
	rv = su_ERR_NONE;
jleave:
	header = acp->ac_.hd;
j_leave:
	if(endptr_or_nil != NIL)
		*endptr_or_nil = header;

	NYD_OU;
	return rv;
} /* }}} */

#include <su/y-imf.h> /* 3. */

#include "su/code-ou.h"
#endif /* su_HAVE_IMF */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_IMF_STRUCT
/* s-itt-mode */
