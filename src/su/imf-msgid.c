/*@ Implementation of imf.h, parse_msgid_header().
 *
 * Copyright (c) 2025 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_imf_msgid
#define su_SOURCE
#define su_SOURCE_IMF_MSGID

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
su_imf_parse_msgid_header(struct su_imf_msgid **mipp, char const *header, BITENUM(u32,su_imf_mode) mode, /* {{{ */
		struct su_mem_bag *membp, char const **endptr_or_nil){
	enum{
		a_NONE,
		a_STAGE_ANGLE = 1u<<0,
		a_STAGE_ANGLE_FAKE = 1u<<1,
		a_STAGE_RIGHT = 1u<<2,
		a_STAGE_MASK = a_STAGE_ANGLE | a_STAGE_ANGLE_FAKE | a_STAGE_RIGHT,

		a_QUOTE = 1u<<8,
		a_QUOTE_ANYHOW = 1u<<9,
		a_MASK = a_QUOTE | a_QUOTE_ANYHOW,

		a_TMP = 1u<<24
	};

	char *cp;
	u32 f, l;
	struct su__imf_actx *acp;
	s32 rv;
	struct su_imf_msgid **mipp_base, *mip;
	NYD_IN;
	ASSERT_NYD_EXEC(mipp != NIL, rv = -su_ERR_NODATA);
	ASSERT_NYD_EXEC(header != NIL, rv = -su_ERR_NODATA);
	ASSERT_NYD_EXEC(membp != NIL, rv = -su_ERR_NODATA);
	ASSERT_EXEC((mode & ~su__IMF_MODE_MSGID_MASK) == 0, mode &= su__IMF_MODE_MSGID_MASK);
	ASSERT_EXEC(!(mode & su_IMF_MODE_ID_SINGLE) || !(mode & su_IMF_MODE_STOP_EARLY), (void)0);

	/* (behavior EQ with/out ASSERT) */
	if(mode & su_IMF_MODE_ID_SINGLE)
		mode &= ~su_IMF_MODE_STOP_EARLY;

	*(mipp_base = mipp) = NIL;

	/* C99 */{
		uz i;

		while(su_imf_c_ANY_WSP(*header))
			++header;
		i = su_cs_len(header);
		if(i == 0){
			rv = -su_ERR_NODATA;
			goto j_leave;
		}

		/* S32_MAX is mem-bag stuff, 4 is 3 strings of maximum size plus room for the structure as such.
		 * We may re-quote some data, so reserve two bytes for quotes.
		 * Strings must be terminated */
		if(i >= (S32_MAX - 2 - 3*1) / 4){
			rv = -su_ERR_OVERFLOW;
			goto j_leave;
		}else if(i < VSTRUCT_SIZEOF(struct su__imf_actx,ac_dat))
			i = VSTRUCT_SIZEOF(struct su__imf_actx,ac_dat);
		i += +1; /* domain + comment not quoted, so includes +2) */

		acp = su__IMF_ALLOC(membp, i * 4);
		if(acp == NIL){
			rv = -su_ERR_NOMEM;
			goto j_leave;
		}

		acp->ac_.hd = header;
		acp->ac_.mse = mode;
		/* Yes.  This looks hacky.  But dig it */
		acp->ac_group_display_name = acp->ac_display_name = NIL;
		acp->ac_locpar = acp->ac_dat;
		acp->ac_domain = &acp->ac_locpar[i + 2];
		acp->ac_comm = &acp->ac_domain[i];
	}

	/* 1*msg-id: parse one */
	f = a_NONE;
jlist_next:
	rv = su_ERR_NONE;
	l = 0;
	cp = acp->ac_locpar;

	STRUCT_ZERO_FROM(struct su__imf_x, &acp->ac_, locpar);
	acp->ac_.mse &= su__IMF_MODE_MSGID_MASK;

	for(rv = su_IMF_ERR_CONTENT;;){
		char c;
		char const *xcp;
		uz i;

		switch(su__imf_s_CFWS(acp)){
		case FAL0:
			rv = (acp->ac_.mse & (su_IMF_ERR_RELAX | su_IMF_ERR_MASK));
			goto jleave;
		case TRUM1:
			break;
		default:
			break;
		}

		xcp = acp->ac_.hd;

		while(su_imf_c_atext(*acp->ac_.hd))
			++acp->ac_.hd;
		if(xcp != acp->ac_.hd){
			if(!(f & a_STAGE_MASK)){
				ASSERT(l == 0);
				if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
					rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
					goto jleave;
				}
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;

				if((acp->ac_.mse & su_IMF_MODE_ID_SINGLE) && *mipp_base != NIL)
					goto jleave;

				acp->ac_.comm = 0; /* comments only from "inside" */
				f |= a_STAGE_ANGLE_FAKE | a_STAGE_ANGLE;
			}
			i = P2UZ(acp->ac_.hd - xcp);
			su_mem_copy(&cp[l], xcp, i);
			l += S(u32,i);
			continue;
		}

		c = *acp->ac_.hd;
		if(c != '\0')
			++acp->ac_.hd;

		if(f & a_STAGE_RIGHT){
			ASSERT(cp == acp->ac_domain);
			switch(c){
			case '.':
				if(l == 0 || cp[l - 1] == '.'){
					if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
						goto jleave;
					}
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				}
				cp[l++] = c;
				continue;
			case '[':
				/* xxx Accept mixed with RELAX? */
				if(l != 0)
					goto jleave;
				/* Literal better directly */
				cp[l++] = '[';
				for(;;){
					su__imf_skip_FWS(acp); /* XXX Postel: CFWS()? */
					c = *acp->ac_.hd++;
					if(su_imf_c_dtext(c) || su_imf_c_obs_NO_WS_CTL(c))
						cp[l++] = c;
					else if(c == '\\' && su_imf_c_quoted_pair_c2(*acp->ac_.hd)){
						cp[l++] = c;
						cp[l++] = *acp->ac_.hd++;
					}else if(c == ']')
						break;
					else
						goto jleave;
				}
				cp[l++] = ']';
				acp->ac_.mse |= su_IMF_STATE_DOMAIN_LITERAL;

				/* Unfortunately not integratable code flow */
				if(su__imf_s_CFWS(acp) == FAL0)
					goto jleave;
				if(*acp->ac_.hd == '>')
					++acp->ac_.hd;
				else{
					if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
						goto jleave;
					}
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				}
				ASSERT(cp == acp->ac_domain);
				acp->ac_.domain = l;
				goto jid_new;
			case '\0':
				if(!(f & a_STAGE_ANGLE_FAKE))
					goto jid__lj1;
				FALLTHRU
			case '>':
				ASSERT(cp == acp->ac_domain);
				if(l == 0 || cp[l - 1] == '.'){
jid__lj1:
					if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
						goto jleave;
					}
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				}
				ASSERT(cp == acp->ac_domain);
				acp->ac_.domain = l;
				f &= ~a_STAGE_ANGLE_FAKE;
				goto jid_new;
			case '<':
				/* Bogus start of new */
				if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
					rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
					goto jleave;
				}
				--acp->ac_.hd;
				ASSERT(cp == acp->ac_domain);
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				acp->ac_.domain = l;
				f &= ~a_STAGE_ANGLE_FAKE;
				goto jid_new;
			case '@':
				/* Multiple @: RELAX move over to id-left */
				if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
					rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
					goto jleave;
				}
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				if(l > 0){
					acp->ac_locpar[acp->ac_.locpar++] = '@';
					su_mem_copy(&acp->ac_locpar[acp->ac_.locpar], cp, l);
					acp->ac_.locpar += S(u32,l);
					l = 0;
				}
				break;
			default:
				if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
					rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
					goto jleave;
				}
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				cp[l++] = c;
				break;
			}
		}else if(f & a_STAGE_ANGLE){
			switch(c){
			case '"':{
				char *ncp;

				/* temporarily remove quotes, they are "not [a] semantical[ly] part" */
				--acp->ac_.hd;
				xcp = ncp = &cp[l];
				ncp = su__imf_s_quoted_string(acp, ncp);
				if(ncp == NIL){
					rv = (acp->ac_.mse & su_IMF_ERR_MASK);
					goto jleave;
				}
				i = P2UZ(ncp - xcp);
				f |= a_QUOTE_ANYHOW;
				if(i > 0){
					f |= a_QUOTE;
					l += S(u32,i);
				}
				}continue;
			case '.':
				if(l == 0 || cp[l - 1] == '.'){
					if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
						goto jleave;
					}
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				}
				cp[l++] = c;
				continue;
			case '@':
				ASSERT(cp == acp->ac_locpar);
				acp->ac_.locpar = l;
				cp = acp->ac_domain;
				l = 0;
				f |= a_STAGE_RIGHT;
				break;
			case '<':
				/* Bogus start of new */
				--acp->ac_.hd;
				FALLTHRU
			case '>':
				/* No @id-right (see mutt commit 99ba609f09582469360f3a5fd7132ee556cbaeaf) */
				if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
					rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
					goto jleave;
				}
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				acp->ac_.locpar = l;
				goto jid_new;
			case '\0':
				if(f & a_STAGE_ANGLE_FAKE){
					ASSERT(acp->ac_.mse & su_IMF_STATE_RELAX);
					ASSERT(acp->ac_.mse & su_IMF_ERR_CONTENT);
					acp->ac_.locpar = l;
					goto jid_new;
				}
				goto jleave;
			default:
				if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
					rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
					goto jleave;
				}
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				cp[l++] = c;
				break;
			}
		}else if(c == '\0')
			break;
		else{
			if(c != '<'){
				if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
					rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
					goto jleave;
				}
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				f |= a_STAGE_ANGLE_FAKE;
				cp[l++] = c;
			}

			if((acp->ac_.mse & su_IMF_MODE_ID_SINGLE) && *mipp_base != NIL)
				goto jleave;

			acp->ac_.comm = 0; /* comments only from "inside" */
			f |= a_STAGE_ANGLE;
		}
		continue;

		/* */
jid_new:
		/* id-left: catch double-dot, and leading and trailing dot; eg "".@ parsed fine so far */
		f |= a_TMP;
		for(cp = acp->ac_locpar, i = l = acp->ac_.locpar; l-- != 0;){
			if(cp[l] == '.'){
				if(f & a_TMP){
					if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
						goto jleave;
					}
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				}
				f |= a_TMP;
			}else
				f &= ~a_TMP;
		}

		if(acp->ac_.locpar == 0 || acp->ac_.domain == 0)
			f |= a_TMP;

		if(f & a_TMP){
			if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
				rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
				goto jleave;
			}
			acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
		}

		if(f & a_QUOTE){
			*--acp->ac_locpar = '"';
			acp->ac_locpar[++i] = '"';
			acp->ac_.locpar = ++i;
		}

		/**/
		i = su_VSTRUCT_SIZEOF(struct su_imf_msgid,imfmi_dat) +3;
		i += acp->ac_.locpar;
		i += acp->ac_.domain;
		i += acp->ac_.comm;

		mip = su__IMF_ALLOC(membp, i);
		*mipp = mip;
		mipp = &mip->imfmi_next;
		mip->imfmi_next = NIL;
		mip->imfmi_mse = acp->ac_.mse & ~su__IMF_MODE_MSGID_MASK;

		cp = mip->imfmi_dat;
		mip->imfmi_id_left = cp;
		i = mip->imfmi_id_left_len = acp->ac_.locpar;
		if(i > 0){
			su_mem_copy(cp, acp->ac_locpar, i);
			cp += i;
		}
		*cp++ = '\0';

		mip->imfmi_id_right = cp;
		i = mip->imfmi_id_right_len = acp->ac_.domain;
		if(i > 0){
			su_mem_copy(cp, acp->ac_domain, i);
			cp += i;
		}
		*cp++ = '\0';

		mip->imfmi_comm = cp;
		i = mip->imfmi_comm_len = acp->ac_.comm;
		if(i > 0){
			su_mem_copy(cp, acp->ac_comm, i);
			cp += i;
		}
		*cp++ = '\0';

		if(mode & su_IMF_MODE_STOP_EARLY)
			break;

		cp = acp->ac_locpar;
		l = 0;
		f &= ~(a_STAGE_MASK | a_MASK);
		goto jlist_next;
	}
	ASSERT((mode & su_IMF_MODE_STOP_EARLY) || *acp->ac_.hd == '\0');

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
#undef su_SOURCE_IMF_MSGID
/* s-itt-mode */
