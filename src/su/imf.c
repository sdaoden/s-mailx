/*@ Implementation of imf.h DATA, and y-imf.h.
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
#define su_FILE su_imf
#define su_SOURCE
#define su_SOURCE_IMF

#include "su/code.h"

su_EMPTY_FILE()
#ifdef su_HAVE_IMF

/*#define a_IMF_TABLE_DUMP*/ /* Compile in su_imf_table_dump() */
#ifdef a_IMF_TABLE_DUMP
# include <stdio.h> /* TODO SU I/O */
#endif

#include "su/imf.h"
#include "su/y-imf.h" /* $(SU_SRCDIR) */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

#include <su/y-imf.h> /* 2. */

/* Created by table_dump() */
#undef a_X
#define a_X(X) CONCAT(su_IMF_C_, X)
u16 const su__imf_c_tbl[su__IMF_TABLE_SIZE + 1] = { /* {{{ */
	0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(HT) | 0,
	a_X(LF) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(CR) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(NO_WS_CTL) | 0,
	a_X(SP) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(SPECIAL) | a_X(DQUOTE) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(DIGIT) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(CTEXT) | a_X(QTEXT) | a_X(SPECIAL) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(ALPHA) | a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(VCHAR) | a_X(ATEXT) | a_X(CTEXT) | a_X(DTEXT) | a_X(QTEXT) | 0,
	a_X(NO_WS_CTL) | 0,
}; /* }}} */
#undef a_X

/* _s_ and _skip_ series {{{ */
boole
su__imf_skip_FWS(struct su__imf_actx *acp){ /* {{{ */
	char const *cp;
	boole rv;
	NYD2_IN;

	rv = FAL0;
	cp = acp->ac_.hd;

	/* As documented (adjust on change) do not classify, skip follow-up lines' leading whitespace, IF ANY */
	for(;; ++cp){
		char c;

#if 1
		if(su_imf_c_ANY_WSP((c = *cp)))
			rv |= su_imf_c_WSP(c);
#else
		if(su_imf_c_WSP((c = *cp))
			rv = TRU1;
		else if(su_imf_c_LF(c)){
		}else if(su_imf_c2_CRLF(c, cp[1]))
			++cp;
#endif
		else
			break;
	}

	if(cp != acp->ac_.hd){
		acp->ac_.hd = cp;
		if(!rv)
			rv = TRUM1;
	}

	NYD2_OU;
	return rv;
} /* }}} */

boole
su__imf_s_CFWS(struct su__imf_actx *acp){ /* {{{ */
	char const *xcp;
	uz lvl;
	boole rv, any;
	NYD2_IN;

	UNINIT(any, FAL0);
	for(rv = TRU1, lvl = 0;; acp->ac_.hd = xcp){
		char c;

		if(su__imf_skip_FWS(acp) == TRU1){
			if(lvl == 0)
				rv = TRUM1;
			else if(any < FAL0)
				any = TRU2;
		}

		c = *(xcp = acp->ac_.hd);
		++xcp;

		if(UNLIKELY(lvl == 0)){
			if(c != '(')
				break;
			any = (acp->ac_.comm > 0);
			++lvl;
			continue;
		}else switch(c){
		case ')':
			--lvl;
			continue;
		case '(':
			++lvl;
			continue;
		case '\0':
jerr:
			if(acp->ac_.mse & su_IMF_MODE_RELAX)
				acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_COMMENT;
			else{
				acp->ac_.mse |= su_IMF_ERR_RELAX | su_IMF_ERR_COMMENT;
				rv = FAL0;
			}
			goto jleave;
		}

		for(;;){
			if(su_imf_c_ctext(c) || /* obs-ctext */su_imf_c_obs_NO_WS_CTL(c)){
			}else if(c == '\\' && su_imf_c_quoted_pair_c2(*xcp))
				++xcp;
			else
				break;
			c = *xcp++;
		}

		if(UNLIKELY(--xcp == acp->ac_.hd))
			goto jerr;
		else{
			uz i;

			i = P2UZ(xcp - acp->ac_.hd);
			if(any > FAL0)
				acp->ac_comm[acp->ac_.comm++] = ' ';
			any = TRUM1;
			su_mem_copy(&acp->ac_comm[acp->ac_.comm], acp->ac_.hd, i);
			acp->ac_.comm += i;
		}
	}

jleave:
	NYD2_OU;
	return rv;
} /* }}} */

char *
su__imf_s_quoted_string(struct su__imf_actx *acp, char *buf){ /* {{{ */
	boole any;
	NYD2_IN;
	ASSERT(su_imf_c_DQUOTE(*acp->ac_.hd));

	for(any = FAL0, ++acp->ac_.hd;;){
		char c;
		char const *xcp;

		xcp = acp->ac_.hd;

		while(su_imf_c_ANY_WSP((c = *xcp))){
			++xcp;
			if(su_imf_c_WSP(c))
				*buf++ = c;
		}

		acp->ac_.hd = xcp;
		xcp = acp->ac_.hd;

		while(su_imf_c_qtext(c = *acp->ac_.hd)){
			*buf++ = c;
			++acp->ac_.hd;
		}
		any = (xcp != acp->ac_.hd);
		if(any)
			continue;

		for(;;){
			if(acp->ac_.hd[0] != '\\' || !su_imf_c_quoted_pair_c2(c = acp->ac_.hd[1]))
				break;
			buf[0] = '\\';
			buf[1] = c;
			buf += 2;
			acp->ac_.hd += 2;
		}
		any = (xcp != acp->ac_.hd);
		if(any)
			continue;

		if(su_imf_c_DQUOTE(*acp->ac_.hd))
			++acp->ac_.hd;
		else{
			acp->ac_.mse |= su_IMF_ERR_DQUOTE;
			buf = NIL;
		}
		break;
	}

	NYD2_OU;
	return buf;
} /* }}} */
/* }}} */

/* imf_table_dump {{{ */
#ifdef a_IMF_TABLE_DUMP
SINLINE boole a_imf_c_CR(char c) {return (c == 0x0D);}
SINLINE boole a_imf_c_DQUOTE(char c) {return (c == 0x22);}
SINLINE boole a_imf_c_HT(char c) {return (c == 0x09);}
SINLINE boole a_imf_c_LF(char c) {return (c == 0x0A);}
SINLINE boole a_imf_c_SP(char c) {return (c == 0x20);}

SINLINE boole a_imf_c_ALPHA(char c) {return ((c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A));}
SINLINE boole a_imf_c_DIGIT(char c) {return (c >= 0x30 && c <= 0x39);}
SINLINE boole a_imf_c_VCHAR(char c) {return (c >= 0x21 && c <= 0x7E);}
SINLINE boole a_imf_c_atext(char c){
	return (a_imf_c_ALPHA(c) || a_imf_c_DIGIT(c) ||
			c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' ||
			c == '+' || c == '-' || c == '/' || c == '=' || c == '?' || c == '^' || c == '_' || c == '`' ||
			c == '{' || c == '|' || c == '}' || c == '~');
}
SINLINE boole a_imf_c_ctext(char c) {return ((c >= 33 && c <= 39) || (c >= 42 && c <= 91) || (c >= 93 && c <= 126));}
SINLINE boole a_imf_c_dtext(char c) {return ((c >= 33 && c <= 90) || (c >= 94 && c <= 126));}
SINLINE boole a_imf_c_qtext(char c) {return (c == 33 || (c >= 35 && c <= 91) || (c >= 93 && c <= 126));}
SINLINE boole a_imf_c_special(char c){
	return (c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == ':' || c == ';' ||
			c == '@' || c == '\\' || c == ',' || c == '.' || a_imf_c_DQUOTE(c));
}
SINLINE boole a_imf_c_obs_NO_WS_CTL(char c){
	return ((c >= 1 && c <= 8) || c == 11 || c == 12 || (c >= 14 && c <= 31) || c == 127);
}

void
su_imf_table_dump(void){
	char c;

	fputs("#undef a_X\n", stdout);
	fputs("#define a_X(X) CONCAT(su_IMF_C_, X)\n", stdout);
	fputs("u16 const su__imf_c_tbl[su__IMF_TABLE_SIZE + 1] = { /* {{{ */\n", stdout);

	for(c = '\0';;){
		fputc('\t', stdout);
		if(a_imf_c_ALPHA(c))
			fputs("a_X(ALPHA) | ", stdout);
		if(a_imf_c_DIGIT(c))
			fputs("a_X(DIGIT) | ", stdout);
		if(a_imf_c_VCHAR(c))
			fputs("a_X(VCHAR) | ", stdout);
		if(a_imf_c_atext(c))
			fputs("a_X(ATEXT) | ", stdout);
		if(a_imf_c_ctext(c))
			fputs("a_X(CTEXT) | ", stdout);
		if(a_imf_c_dtext(c))
			fputs("a_X(DTEXT) | ", stdout);
		if(a_imf_c_qtext(c))
			fputs("a_X(QTEXT) | ", stdout);
		if(a_imf_c_special(c))
			fputs("a_X(SPECIAL) | ", stdout);
		if(a_imf_c_obs_NO_WS_CTL(c))
			fputs("a_X(NO_WS_CTL) | ", stdout);

		if(a_imf_c_CR(c))
			fputs("a_X(CR) | ", stdout);
		if(a_imf_c_DQUOTE(c))
			fputs("a_X(DQUOTE) | ", stdout);
		if(a_imf_c_HT(c))
			fputs("a_X(HT) | ", stdout);
		if(a_imf_c_LF(c))
			fputs("a_X(LF) | ", stdout);
		if(a_imf_c_SP(c))
			fputs("a_X(SP) | ", stdout);

		fputs("0,\n", stdout);

		if(c++ == S8_MAX)
			break;
	}

	fputs("}; /* }}} */\n", stdout);
}
#endif /* }}} a_IMF_TABLE_DUMP */

#include <su/y-imf.h> /* 3. */

#include "su/code-ou.h"
#endif /* su_HAVE_IMF */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_IMF
/* s-itt-mode */
