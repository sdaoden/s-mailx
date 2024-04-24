/*@ Implementation of imf.h.
 *@ TODO: add a su_imf_parse_msg_id_header(); add quoter function.
 *
 * Copyright (c) 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

#include "su/cs.h"
#include "su/mem.h"
#include "su/mem-bag.h"

/*#define a_IMF_TABLE_DUMP*/ /* Compile in su_imf_table_dump() */
#ifdef a_IMF_TABLE_DUMP
# include <stdio.h> /* TODO SU I/O */
#endif

#include "su/imf.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

#ifdef su_HAVE_MEM_BAG_LOFI
# define a_IMF_ALLOC(MBP,X) su_MEM_BAG_LOFI_ALLOCATE(MBP, X, 1, su_MEM_BAG_ALLOC_MAYFAIL)
#elif defined su_HAVE_MEM_BAG_AUTO
# define a_IMF_ALLOC(MBP,X) su_MEM_BAG_AUTO_ALLOCATE(MBP, X, 1, su_MEM_BAG_ALLOC_MAYFAIL)
#else
# error Needs one of su_HAVE_MEM_BAG_LOFI and su_HAVE_MEM_BAG_AUTO
#endif

/* Internet Message Format ABNF (of relevant fields) from RFC 5234 and RFC 5322 {{{
 *
 * RFC 5234, B.1.  Core Rules:
 *
 * ALPHA  = %x41-5A / %x61-7A; A-Z / a-z
 * BIT    = "0" / "1"
 * CHAR   = %x01-7F; any 7-bit US-ASCII character, excluding NUL
 * CR     = %x0D ; carriage return
 * CRLF   = CR LF; Internet standard newline
 * CTL    = %x00-1F / %x7F; controls
 * DIGIT  = %x30-39; 0-9
 * DQUOTE = %x22; " (Double Quote)
 * HEXDIG = DIGIT / "A" / "B" / "C" / "D" / "E" / "F"
 * HT     = %x09; horizontal tab
 * LF     = %x0A; linefeed
 * [LWSP  = *(WSP / CRLF WSP); Use of this linear-white-space rule
 *              ; permits lines containing only white space that are no longer legal in mail headers
 *              ; and have caused interoperability problems in other contexts.
 *              ; Do not use when defining mail headers and use with caution in other contexts.]
 * OCTET  = %x00-FF; 8 bits of data
 * SP     = %x20
 * VCHAR  = %x21-7E; visible (printing) characters
 * WSP    = SP / HT; white space
 *
 * RFC 5322
 *
 * CFWS           = (1*([FWS] comment) [FWS]) / FWS
 * FWS            = ([*WSP CRLF] 1*WSP) / obs-FWS; Folding white space; NOTE this implementation supports lone LF, too
 * addr-spec      = local-part "@" domain
 * address        = mailbox / group
 * address-list   = (address *("," address)) / obs-addr-list
 * angle-addr     = [CFWS] "<" addr-spec ">" [CFWS] / obs-angle-addr
 * atext          = ALPHA / DIGIT / "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+" / "-" / "/" / "=" / "?" /
 *                      "^" / "_" / "`" / "{" / "|" / "}" / "~"; Printable US-ASCII less specials; used for "atom"s
 * atom           = [CFWS] 1*atext [CFWS]
 * comment        = "(" *([FWS] ccontent) [FWS] ")"
 * ccontent       = ctext / quoted-pair / comment
 * ctext          = %d33-39 / %d42-91 / %d93-126 / obs-ctext; Printable US-ASCII not including "(", ")", or "\"
 * display-name   = phrase
 * domain         = dot-atom / domain-literal / obs-domain
 * domain-literal = [CFWS] "[" *([FWS] dtext) [FWS] "]" [CFWS]; to be used bypassing normal name-resolution
 * dot-atom       = [CFWS] dot-atom-text [CFWS]
 * dot-atom-text  = 1*atext *("." 1*atext)
 * dtext          = %d33-90 / d94-126 / obs-dtext; Printable US-ASCII not including "[", "]", or "\"
 * group          = display-name ":" [group-list] ";" [CFWS]
 * group-list     = mailbox-list / CFWS / obs-group-list
 * local-part     = dot-atom / quoted-string / obs-local-part
 * mailbox        = name-addr / addr-spec
 * mailbox-list   = (mailbox *("," mailbox)) / obs-mbox-list
 * name-addr      = [display-name] angle-addr
 * phrase         = 1*word / obs-phrase
 * qcontent       = qtext / quoted-pair
 * quoted-pair    = ("\" (VCHAR / WSP)) / obs-qp
 * quoted-string  = [CFWS] DQUOTE *([FWS] qcontent) [FWS] DQUOTE [CFWS]
 * qtext          = %d33 / d35-91 / d93-126 / obs-qtext; Printable US-ASCII excluding "\" and the quote character
 * specials       = "(" / ")" / "<" / ">" / "[" / "]" / ":" / ";" / "@" / "\" / "," / "." / DQUOTE
 *                      ; Special characters that do not appear in "atext"
 * unstructured   = (*([FWS] VCHAR) *WSP) / obs-unstruct
 * word           = atom / quoted-string
 *
 * obs-FWS         = 1*WSP *(CRLF 1*WSP)
 * obs-NO-WS-CTL   = %d1-8 / %d11 / %d12 / %d14-31 / %d127; US-ASCII controls except CR, LF, and whitespace
 * obs-addr-list   = *([CFWS] ",") address *("," [address / CFWS])
 * obs-angle-addr  = [CFWS] "<" obs-route addr-spec ">" [CFWS]
 * obs-ctext       = obs-NO-WS-CTL
 * obs-domain      = atom *("." atom)
 * obs-domain-list = *(CFWS / ",") "@" domain  *("," [CFWS] ["@" domain])
 * obs-dtext       = obs-NO-WS-CTL / quoted-pair
 * obs-group-list  = 1*([CFWS] ",") [CFWS]
 * obs-local-part  = word *("." word)
 * obs-mbox-list   = *([CFWS] ",") mailbox *("," [mailbox / CFWS])
 * obs-phrase      = word *(word / "." / CFWS)
 * obs-phrase-list = [phrase / CFWS] *("," [phrase / CFWS])
 * obs-qp          = "\" (%d0 / obs-NO-WS-CTL / LF / CR); NOTE this implementation does not support NUL
 * obs-route       = obs-domain-list ":"
 * obs-unstruct    = *((*LF *CR *(obs-utext *LF *CR)) / FWS)
 * obs-utext       = %d0 / obs-NO-WS-CTL / VCHAR
 *
 * . 4.4.  Obsolete Addressing
 *   - When interpreting addresses, the route portion SHOULD be ignored
 *
 * . Additional notes:
 *   - In order to accommodate that "3.2.2. Folding White Space and Comments" allows
 *     CFWS rather freely, Postel'ize that and allow it practically everywhere.
 *   - obs-qp is supported WITHOUT NUL.
 *   - obs-utext only WITHOUT NUL.
 * }}} */

/* Address context: whenever an address was parsed su_imf_addr is created and this is reset */
struct a_imf_actx{
	struct a_imf_x{
		char const *hd; /* Header field body content data rest */
		BITENUM(u32,su_imf_mode) mse; /* imf_mode, plus current imf_state */
		u32 group_display_name;
		u32 display_name;
		u32 locpar;
		u32 domain;
		u32 comm;
	} ac_;
	char *ac_group_display_name;
	char *ac_display_name;
	char *ac_locpar;
	char *ac_domain;
	char *ac_comm;
	char ac_dat[VFIELD_SIZE(0)]; /* Storage for any text (single chunk struct) */
};

/* Returns TRU1 if at least 1 WSP was skipped over, TRUM1 if *any* ws was seen, ie: it moved */
static boole a_imf_skip_FWS(struct a_imf_actx *acp);

/* Returns TRUM1 if FWS was parsed *outside* of comments */
static boole a_imf_s_CFWS(struct a_imf_actx *acp);

/* ASSERTs "at a_imf_c_DQUOTE()".  Skips to after the closing DQUOTE().
 * Places but leading or trailing FWS++ normalized to single SP in *buf.
 * Returns NIL on invalid content / missing closing quote error (unless relaxed), ptr to after written byte otherwise */
static char *a_imf_s_quoted_string(struct a_imf_actx *acp, char *buf);

/**/
static void a_imf_addr_create(struct a_imf_actx *acp, struct su_mem_bag *membp, struct su_imf_addr ***appp);

/* _s_ and _skip_ series {{{ */
static boole
a_imf_skip_FWS(struct a_imf_actx *acp){ /* {{{ */
	char const *cp;
	boole rv;
	NYD2_IN;

	rv = FAL0;
	cp = acp->ac_.hd;

	/* As documented (adjust on change) do not classify, skip follow-up lines' leading whitespace, IF ANY */
	for(;; ++cp){
		char c;

#if 1
		if(su_imf_c_ANY_WSP((c = *cp))){
			if(su_imf_c_WSP(c))
				rv = TRU1;
		}
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

static boole
a_imf_s_CFWS(struct a_imf_actx *acp){ /* {{{ */
	char const *xcp;
	uz lvl;
	boole rv, any;
	NYD2_IN;

	UNINIT(any, FAL0);
	for(rv = TRU1, lvl = 0;; acp->ac_.hd = xcp){
		char c;

		if(a_imf_skip_FWS(acp) == TRU1){
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
			acp->ac_.comm += S(u32,i);
		}
	}

jleave:
	NYD2_OU;
	return rv;
} /* }}} */

static char *
a_imf_s_quoted_string(struct a_imf_actx *acp, char *buf){ /* {{{ */
	boole any, lastws;
	NYD2_IN;
	ASSERT(su_imf_c_DQUOTE(*acp->ac_.hd));

	for(lastws = any = FAL0, ++acp->ac_.hd;;){
		char c;
		char const *xcp;

		if(a_imf_skip_FWS(acp) == TRU1 && any){
			*buf++ = ' ';
			lastws = TRU1;
		}

		xcp = acp->ac_.hd;

		while(su_imf_c_qtext(c = *acp->ac_.hd)){
			*buf++ = c;
			++acp->ac_.hd;
		}
		any = (xcp != acp->ac_.hd);
		if(any){
			lastws = FAL0;
			continue;
		}

		for(;;){
			if(acp->ac_.hd[0] != '\\' || !su_imf_c_quoted_pair_c2(c = acp->ac_.hd[1]))
				break;
			buf[0] = '\\';
			buf[1] = c;
			buf += 2;
			acp->ac_.hd += 2;
		}
		any = (xcp != acp->ac_.hd);
		if(any){
			lastws = FAL0;
			continue;
		}

		if(su_imf_c_DQUOTE(*acp->ac_.hd)){
			if(lastws)
				--buf;
			++acp->ac_.hd;
		}else{
			acp->ac_.mse |= su_IMF_ERR_DQUOTE;
			buf = NIL;
		}
		break;
	}

	NYD2_OU;
	return buf;
} /* }}} */
/* }}} */

/* misc {{{ */
static void
a_imf_addr_create(struct a_imf_actx *acp, struct su_mem_bag *membp, struct su_imf_addr ***appp){ /* {{{ */
	struct su_imf_addr *ap;
	u32 i;
	NYD2_IN;

	i = VSTRUCT_SIZEOF(struct su_imf_addr,imfa_dat) +
			acp->ac_.group_display_name +1 + acp->ac_.display_name +1 +
			acp->ac_.locpar +1 + acp->ac_.domain +1 + acp->ac_.comm +1;

	ap = a_IMF_ALLOC(membp, i);
	**appp = ap;
	*appp = &ap->imfa_next;

	ap->imfa_next = NIL;
	ap->imfa_mse = acp->ac_.mse & ~su__IMF_MODE_MASK;

	ap->imfa_group_display_name = ap->imfa_dat;
	if((ap->imfa_group_display_name_len = i = acp->ac_.group_display_name) > 0)
		su_mem_copy(ap->imfa_group_display_name, acp->ac_group_display_name, i);
	ap->imfa_group_display_name[i++] = '\0';

	ap->imfa_display_name = &ap->imfa_group_display_name[i];
	if((ap->imfa_display_name_len = i = acp->ac_.display_name) > 0)
		su_mem_copy(ap->imfa_display_name, acp->ac_display_name, i);
	ap->imfa_display_name[i++] = '\0';

	ap->imfa_locpar = &ap->imfa_display_name[i];
	if((ap->imfa_locpar_len = i = acp->ac_.locpar) > 0)
		su_mem_copy(ap->imfa_locpar, acp->ac_locpar, i);
	ap->imfa_locpar[i++] = '\0';

	ap->imfa_domain = &ap->imfa_locpar[i];
	if((ap->imfa_domain_len = i = acp->ac_.domain) > 0)
		su_mem_copy(ap->imfa_domain, acp->ac_domain, i);
	ap->imfa_domain[i++] = '\0';

	ap->imfa_comm = &ap->imfa_domain[i];
	if((ap->imfa_comm_len = i = acp->ac_.comm) > 0)
		su_mem_copy(ap->imfa_comm, acp->ac_comm, i);
	ap->imfa_comm[i++] = '\0';

	NYD2_OU;
} /* }}} */
/* }}} */

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

s32
su_imf_parse_addr_header(struct su_imf_addr **app, char const *header, BITENUM(u32,su_imf_mode) mode, /* {{{ */
		struct su_mem_bag *membp, char const **endptr_or_nil){
	enum{
		a_NONE,
		a_STAGE_DOMAIN = 1u<<0, /* Parsing a domain */
		a_STAGE_ANGLE = 1u<<1, /* Have seen a left angle bracket, parsing addr-spec (or obs-route) */
		a_STAGE_DOT = 1u<<2, /* Have seen a DOT in a display-name, ie, could be a local-part, too! */
		a_STAGE_REST_OR_SEP = 1u<<3, /* We are done, only comments or address-list separators may follow */
		a_STAGE_MASK = a_STAGE_DOMAIN | a_STAGE_ANGLE | a_STAGE_DOT | a_STAGE_REST_OR_SEP,

		a_ANY = 1u<<8,
		a_WS = 1u<<9,
		a_WS_SEEN = 1u<<10, /* To strip FWS seen in local part in non-angle-bracket addresses */
		a_QUOTE = 1u<<11, /* Had seen quote */
		a_ROUTE = 1u<<12, /* With STAGE_ANGLE */
		a_ROUTE_SEEN = 1u<<13,
		a_NEED_SEP = 1u<<14,
		a_MASK = a_ANY | a_WS | a_WS_SEEN | a_QUOTE | a_ROUTE | a_ROUTE_SEEN | a_NEED_SEP
	};

	char *cp;
	u32 f, l;
	struct a_imf_actx *acp;
	s32 rv;
	struct su_imf_addr **app_base;
	NYD_IN;
	ASSERT_EXEC((mode & ~su__IMF_MODE_ADDR_MASK) == 0, mode &= su__IMF_MODE_ADDR_MASK);

	*(app_base = app) = NIL;

	/* C99 */{
		uz i;

		while(su_imf_c_ANY_WSP(*header))
			++header;
		i = su_cs_len(header);
		if(i == 0){
			rv = -su_ERR_NODATA;
			goto j_leave;
		}

		/* S32_MAX is mem-bag stuff, 3 is 2 strings of maximum size plus room for the structure as such.
		 * We may re-quote dat, so reserve two bytes for quotes plus one for NUL */
		if(i >= (S32_MAX - 6/*xxx too much*/*3) / 6){
			rv = -su_ERR_OVERFLOW;
			goto j_leave;
		}else if(i < VSTRUCT_SIZEOF(struct a_imf_actx,ac_dat))
			i = VSTRUCT_SIZEOF(struct a_imf_actx,ac_dat);
		i += 3;

		acp = a_IMF_ALLOC(membp, i * 6);
		if(acp == NIL){
			rv = -su_ERR_NOMEM;
			goto j_leave;
		}

		acp->ac_.hd = header;
		acp->ac_.mse = mode;
		acp->ac_group_display_name = acp->ac_dat;
		acp->ac_display_name = &acp->ac_group_display_name[i];
		++acp->ac_group_display_name; /* (room for double-quote) */
		acp->ac_locpar = &acp->ac_display_name[i];
		++acp->ac_display_name;
		acp->ac_domain = &acp->ac_locpar[i];
		++acp->ac_locpar;
		acp->ac_comm = &acp->ac_domain[i];
	}

	/* address-list = (address *("," address)); parse one */
	f = a_NONE;
jlist_next:
	rv = su_ERR_NONE;
	l = 0;
	cp = acp->ac_display_name;

	STRUCT_ZERO_FROM(struct a_imf_x, &acp->ac_, group_display_name);
	acp->ac_.mse &= su__IMF_MODE_MASK | ((acp->ac_.mse & su_IMF_STATE_GROUP_END) ? 0 : su_IMF_STATE_GROUP);

	for(rv = su_IMF_ERR_CONTENT;;){
		char c;
		char const *xcp;
		u32 i;

		switch(a_imf_s_CFWS(acp)){
		case FAL0:
			rv = (acp->ac_.mse & (su_IMF_ERR_RELAX | su_IMF_ERR_MASK));
			goto jleave;
		case TRUM1:
			if(l > 0)
				f |= a_WS;
			break;
		default:
			break;
		}

		c = *acp->ac_.hd;

		if(!(f & a_STAGE_MASK)){
			/* Early watch for necessary separator */
			if((f & a_NEED_SEP) && (c != ';' && c != ',' && c != '\0'))
				goto jleave;
		}else if(UNLIKELY(f & a_STAGE_REST_OR_SEP)){
jaddr_step_create:
			switch(c){
			case ',':
				f = a_NONE;
jaddr_create:
				++acp->ac_.hd;
				a_imf_addr_create(acp, membp, &app);
				if(mode & su_IMF_MODE_STOP_EARLY)
					goto jlist_done;
				goto jlist_next;
			case ';':
				if(!(acp->ac_.mse & su_IMF_STATE_GROUP))
					goto jleave;
				acp->ac_.mse |= su_IMF_STATE_GROUP_END;
				f &= ~(a_MASK | a_STAGE_MASK);
				f |= a_NEED_SEP;
				goto jaddr_create;
			case '\0':
				if((acp->ac_.mse & su_IMF_STATE_GROUP) && !(acp->ac_.mse & su_IMF_STATE_GROUP_END)){
					if(acp->ac_.mse & su_IMF_MODE_RELAX)
						acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_STATE_GROUP_END |
								su_IMF_ERR_GROUP_OPEN;
					else{
						rv = su_IMF_ERR_GROUP_OPEN;
						goto jleave;
					}
				}
				a_imf_addr_create(acp, membp, &app);
				goto jlist_done;
			default:
				goto jleave;
			}
		}

		if(!(f & a_STAGE_DOMAIN) && su_imf_c_DQUOTE(*acp->ac_.hd)){
			char *ncp;

			f |= a_ANY | a_QUOTE;
			if((f & (a_STAGE_ANGLE | a_STAGE_DOMAIN | a_WS)) == a_WS){
				cp[l++] = ' ';
				f |= a_WS_SEEN;
			}
			f &= ~a_WS;
			f |= a_ANY;

			/* MUST be display-name or local-part; remove quotes, they are "not [a] semantical[ly] part" */
			xcp = ncp = &cp[l];
			ncp = a_imf_s_quoted_string(acp, ncp);
			if(ncp == NIL){
				rv = (acp->ac_.mse & su_IMF_ERR_MASK);
				goto jleave;
			}
			l += P2UZ(ncp - xcp);
			continue;
		}

		xcp = acp->ac_.hd;
		while(su_imf_c_atext(*acp->ac_.hd))
			++acp->ac_.hd;
		if(xcp != acp->ac_.hd){
			/* MUST be display-name or local-part */
jpushany:
			i = S(u32,P2UZ(acp->ac_.hd - xcp));
			if((f & (a_STAGE_ANGLE | a_STAGE_DOMAIN | a_WS)) == a_WS){
				cp[l++] = ' ';
				f |= a_WS_SEEN;
			}
			f &= ~a_WS;
			f |= a_ANY;
			su_mem_copy(&cp[l], xcp, i);
			l += i;
			continue;
		}

		c = *acp->ac_.hd++;

		if(f & a_STAGE_DOMAIN){
			switch(c){
			case '.':
				cp[l++] = c;
				continue;
			case '[':
				if(l != 0)
					goto jleave;
				/* Literal better directly */
				cp[l++] = '[';
				for(;;){
					a_imf_skip_FWS(acp); /* XXX Postel: CFWS()? */
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

				/* Unfortunately not integratable XXX */
				if((f & a_STAGE_ANGLE) && (a_imf_s_CFWS(acp) == FAL0 || *acp->ac_.hd != '>'))
					goto jleave;
				goto jdomain_done;
			case ';':
				if((f & a_STAGE_ANGLE) || !(acp->ac_.mse & su_IMF_STATE_GROUP))
					goto jleave;
				goto jdomain_done;
			case ',':
			case '\0':
				if(f & a_STAGE_ANGLE)
					goto jleave;
				goto jdomain_done;
			case '>':
				if(!(f & a_STAGE_ANGLE))
					goto jleave;
jdomain_done:
				ASSERT(cp == acp->ac_domain);
				acp->ac_.domain = l;
				l = 0;
				f ^= a_STAGE_DOMAIN | a_STAGE_REST_OR_SEP;
				f &= ~(a_MASK | a_STAGE_ANGLE);
				if(c == ';' || c == ',' || c == '\0'){
					--acp->ac_.hd;
					goto jaddr_step_create;
				}
				continue;
			default:
				goto jleave;
			}
		}else if(f & a_STAGE_ANGLE){
			switch(c){
			case '.':
				/* Inside an angle-addr it is dot-atom not atext */
				cp[l++] = c;
				continue;
			case '@':
				if(f & a_ROUTE)
					goto jleave;
				/* If not yet it can be a route  */
				if(!(f & a_ROUTE_SEEN) && l == 0){
					f |= a_ROUTE;
					continue;
				}
				ASSERT(cp == acp->ac_locpar);
				goto jlocpar_copy;
			case ',':
				if(!(f & a_ROUTE))
					goto jleave;
				/* Must be route end or next hop */
				for(;;){
					if(a_imf_s_CFWS(acp) == FAL0)
						goto jleave;
					c = *acp->ac_.hd++;
					if(c == ':')
						goto jroute_end;
					if(c != ',')
						break;
				}
				if(c != '@')
					goto jleave;
				continue;
			case ':':
				if(!(f & a_ROUTE))
					goto jleave;
jroute_end:
				/* Real address to follow */
				f ^= a_ROUTE | a_ROUTE_SEEN;
				l = 0;
				acp->ac_.comm = 0;
				ASSERT(cp == acp->ac_locpar);
				continue;
			case '>':
				if(f & a_ROUTE)
					goto jleave;
				if(acp->ac_.mse & su_IMF_MODE_OK_ADDR_SPEC_NO_DOMAIN){
					/* STAGE_ANGLE stripped by jdomain_done! */
					acp->ac_.mse |= su_IMF_STATE_ADDR_SPEC_NO_DOMAIN;
					ASSERT(cp == acp->ac_locpar);
					goto jlocpar_copy;
				}
				FALLTHRU
			default:
				goto jleave;
			}
		}else if(f & a_STAGE_DOT){
			switch(c){
			case '.':
				/* We do not know what we are doing for now, keep on going */
				goto jpushany;
			case '@':
				/* An AT "verifies" we were indeed a local-part, now parse domain */
jlocpar_copy:
				if(!(f & a_WS_SEEN))
					su_mem_copy(acp->ac_locpar, cp, acp->ac_.locpar = l);
				else{
					for(; l > 0; ++cp, --l)
						if((c = *cp) != ' ')
							acp->ac_locpar[acp->ac_.locpar++] = c;
				}
				cp = acp->ac_domain;
				l = 0;
				if(f & a_QUOTE){
					*--acp->ac_locpar = '"';
					acp->ac_locpar[++acp->ac_.locpar] = '"';
					++acp->ac_.locpar;
				}
				f &= ~(a_STAGE_DOT | a_MASK);
				f |= a_STAGE_DOMAIN;
				if(acp->ac_.mse & su_IMF_STATE_ADDR_SPEC_NO_DOMAIN)
					goto jdomain_done;
				continue;
			case '<':
				/* Otherwise maybe a 'Dr. Z <y@x>' was falsely seen as a local-part?
				 * .. Then this should be an angle-addr *now* for us to deal with it */
				if(acp->ac_.mse & su_IMF_MODE_OK_DISPLAY_NAME_DOT){
					acp->ac_.mse |= su_IMF_STATE_DISPLAY_NAME_DOT;
					acp->ac_.display_name = l;
					cp = acp->ac_locpar;
					l = 0;
					*--acp->ac_display_name = '"';
					acp->ac_display_name[++acp->ac_.display_name] = '"';
					++acp->ac_.display_name;
					f ^= (a_STAGE_ANGLE | a_STAGE_DOT);
					f &= ~a_MASK;
					continue;
				}
				FALLTHRU
			default:
				rv = su_IMF_ERR_DISPLAY_NAME_DOT;
				goto jleave;
			}
		}else{
			switch(c){
			case '<':
				/* This switches to angle-addr parse mode for sure */
				ASSERT(cp == acp->ac_display_name);
				acp->ac_.display_name = l;
				cp = acp->ac_locpar;
				l = 0;
				f |= a_STAGE_ANGLE;
				if(f & a_QUOTE){
					f ^= a_QUOTE;
					*--acp->ac_display_name = '"';
					acp->ac_display_name[++acp->ac_.display_name] = '"';
					++acp->ac_.display_name;
				}
				continue;
			case '.':
				/* Either parse a local-part without knowing; else IMF_MODE_OK_DISPLAY_NAME_DOT */
				f |= a_STAGE_DOT;
				goto jpushany;
			case '@':
				if(!(f & a_ANY))
					goto jleave;
				goto jlocpar_copy;
			case ':':
				/* Group start */
				if(acp->ac_.mse & su_IMF_STATE_GROUP)
					goto jleave;
				acp->ac_.mse |= su_IMF_STATE_GROUP_START | su_IMF_STATE_GROUP;
				if(f & a_ANY){
					su_mem_copy(acp->ac_group_display_name, cp, l);
					if(f & a_QUOTE){
						*--acp->ac_group_display_name = '"';
						acp->ac_group_display_name[++l] = '"';
						++l;
					}
					acp->ac_.group_display_name = l;
				}else{
					ASSERT(!(f & a_QUOTE));
					if(acp->ac_.mse & su_IMF_MODE_RELAX)
						acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY;
					else{
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY;
						goto jleave;
					}
				}
				l = 0;
				acp->ac_.comm = 0;
				f &= ~(a_ANY | a_QUOTE | a_WS);
				continue;
			case ';':
				if((f & a_NEED_SEP) || !(acp->ac_.mse & su_IMF_STATE_GROUP))
					goto jleave;
				f |= a_NEED_SEP;
				acp->ac_.comm = 0;
				if(acp->ac_.mse & su_IMF_STATE_GROUP_START){
					acp->ac_.mse |= su_IMF_STATE_GROUP_END | su_IMF_STATE_GROUP_EMPTY;
					a_imf_addr_create(acp, membp, &app);
					goto jlist_next; /* No STOP_EARLY, not an address */
				}
				acp->ac_.mse &= su__IMF_MODE_MASK;
				if(*app_base != NIL){
					struct su_imf_addr *ap;

					for(ap = *app_base; ap->imfa_next != NIL; ap = ap->imfa_next){
					}
					ASSERT(ap->imfa_mse & su_IMF_STATE_GROUP);
					ap->imfa_mse |= su_IMF_STATE_GROUP_END;
				}
				continue;
			case ',':
				f &= ~a_NEED_SEP;
				FALLTHRU
			case '\0':
				acp->ac_.comm = 0;
				f &= ~a_WS;
				if(c != '\0')
					continue;
				--acp->ac_.hd;
				if((acp->ac_.mse & (su_IMF_MODE_RELAX | su_IMF_STATE_GROUP_START)) ==
						(su_IMF_MODE_RELAX | su_IMF_STATE_GROUP_START)){
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_STATE_GROUP_END |
							su_IMF_STATE_GROUP_EMPTY | su_IMF_ERR_GROUP_OPEN;
					a_imf_addr_create(acp, membp, &app);
					/* No STOP_EARLY, not an address */
				}else if(*app_base == NIL)
					goto jleave;
				goto jlist_done;
			default:
				goto jleave;
			}
		}
	}

jlist_done:
	rv = su_ERR_NONE;
jleave:
	header = acp->ac_.hd;
j_leave:
	if(endptr_or_nil != NIL)
		*endptr_or_nil = header;

	NYD_OU;
	return rv;
} /* }}} */

s32
su_imf_parse_struct_header(struct su_imf_shtok **shtpp, char const *header, BITENUM(u32,su_imf_mode) mode, /* {{{ */
		struct su_mem_bag *membp, char const **endptr_or_nil){
	s32 rv;
	struct a_imf_actx *acp;
	uz i;
	struct su_imf_shtok **shtpp_base, *shtp;
	NYD_IN;
	ASSERT_EXEC((mode & ~su__IMF_MODE_STRUCT_MASK) == 0, mode &= su__IMF_MODE_STRUCT_MASK);
	ASSERT_EXEC((mode & su_IMF_MODE_TOK_SEMICOLON) || !(mode & su_IMF_MODE_TOK_EMPTY),
		mode &= ~su_IMF_MODE_TOK_EMPTY);

	*(shtpp_base = shtpp) = NIL;

	while(su_imf_c_ANY_WSP(*header))
		++header;

	i = su_cs_len(header);
	if(i == 0){
		rv = -su_ERR_NODATA;
		goto j_leave;
	}

	/* S32_MAX is mem-bag stuff, 2 is 1 string of maximum size plus room for the structure as such.
	 * We may re-quote, so reserve two bytes for quotes plus one for NUL */
	if(i >= (S32_MAX - 2/*xxx too much*/*3) / 2){
		rv = -su_ERR_OVERFLOW;
		goto j_leave;
	}else if(i < VSTRUCT_SIZEOF(struct a_imf_actx,ac_dat))
		i = VSTRUCT_SIZEOF(struct a_imf_actx,ac_dat);
	i += 3;
	i <<= 1;

	acp = a_IMF_ALLOC(membp, i);
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

	STRUCT_ZERO_FROM(struct a_imf_x, &acp->ac_, group_display_name);
	acp->ac_.mse &= su__IMF_MODE_STRUCT_MASK;

	/* In order to correlate semicolon to h1 in "(c1) h1 (c2);" without TOK_COMMENT we need to delay STOP_EARLY */
	for(rv = su_IMF_ERR_CONTENT; *acp->ac_.hd != '\0';){
		char const *start;
		boole any;

		any = FAL0;
		start = acp->ac_.hd;

		if(!a_imf_s_CFWS(acp)){
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

		any = (start != acp->ac_.hd);

		for(i = 0;;){
			char c;

			start = acp->ac_.hd;

			if(su_imf_c_DQUOTE(*acp->ac_.hd)){
				/* Remove quotes, they are "not [a] semantical[ly] part" */
				char *cp;

				cp = a_imf_s_quoted_string(acp, &acp->ac_comm[i]);
				if(cp == NIL){
					rv = (acp->ac_.mse & su_IMF_ERR_MASK);
					goto jleave;
				}
				i += P2UZ(cp - &acp->ac_comm[i]);
				if(i > 0)
					acp->ac_.mse |= su_IMF_STATE_GROUP; /* gross hack: misuse bit for "quotes" */
			}

			while(su_imf_c_atext((c = *acp->ac_.hd))){
				acp->ac_comm[i++] = c;
				++acp->ac_.hd;
			}

			if(mode & su_IMF_MODE_OK_DOT_ATEXT){
				while(*acp->ac_.hd == '.'){
					++acp->ac_.hd;
					acp->ac_comm[i++] = '.';
				}
			}

			if(start == acp->ac_.hd){
				if(i == 0)
					break;
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
		}else if(!any)
			goto jleave;
		continue;

jtoken_create:
		i = acp->ac_.comm;
		i = VSTRUCT_SIZEOF(struct su_imf_shtok,imfsht_dat) + i + 2 +1;

		shtp = a_IMF_ALLOC(membp, i);
		*shtpp = shtp;
		shtpp = &shtp->imfsht_next;

		shtp->imfsht_next = NIL;
		shtp->imfsht_mse = (acp->ac_.mse & ~(su__IMF_MODE_STRUCT_MASK | su_IMF_STATE_GROUP));

		/* C99 */{
			char *cp;

			cp = shtp->imfsht_dat;
			if(acp->ac_.mse & su_IMF_STATE_GROUP)
				*cp++ = '"';
			su_mem_copy(cp, acp->ac_comm, acp->ac_.comm);
			cp += acp->ac_.comm;
			if(acp->ac_.mse & su_IMF_STATE_GROUP)
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

/* imf_table_dump {{{ */
#ifdef a_IMF_TABLE_DUMP
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
	return (c >= 1 && c <= 8) || (c == 11 || c == 12) || (c >= 14 && c <= 31) || (c == 127);
}

SINLINE boole a_imf_c_CR(char c) {return (c == 0x0D);}
SINLINE boole a_imf_c_DQUOTE(char c) {return (c == 0x22);}
SINLINE boole a_imf_c_HT(char c) {return (c == 0x09);}
SINLINE boole a_imf_c_LF(char c) {return (c == 0x0A);}
SINLINE boole a_imf_c_SP(char c) {return (c == 0x20);}

void
su_imf_table_dump(void){
	char c;

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
} /* }}} */
#endif /* a_IMF_TABLE_DUMP */

#undef a_IMF_ALLOC

#include "su/code-ou.h"
#endif /* su_HAVE_IMF */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_IMF
/* s-itt-mode */
