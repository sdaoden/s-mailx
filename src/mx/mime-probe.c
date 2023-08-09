/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mime-probe.h.
 *
 * Copyright (c) 2013 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE mime_probe
#define mx_SOURCE
#define mx_SOURCE_MIME_PROBE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/utf.h>

#include "mx/iconv.h"

#include "mx/mime-probe.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct mx_mime_probe_charset_ctx *
mx_mime_probe_charset_ctx_setup(struct mx_mime_probe_charset_ctx *mpccp){
	NYD2_IN;

	/*STRUCT_ZERO(struct mx_mime_probe_charset_ctx, mpccp);*/
	mpccp->mpcc_iconv_disable = TRU1
#ifdef mx_HAVE_ICONV
			&& ok_blook(iconv_disable)
#endif
			;
	/* These are all iconv_norm_name()d! */
	mpccp->mpcc_cs_7bit = ok_vlook(charset_7bit);
	mpccp->mpcc_cs_7bit_is_ascii = n_iconv_name_is_ascii(mpccp->mpcc_cs_7bit);
	mpccp->mpcc_ttyc5t = ok_vlook(ttycharset);
	mpccp->mpcc_ttyc5t_is_utf8 = n_iconv_name_is_utf8(mpccp->mpcc_ttyc5t);
	mpccp->mpcc_ttyc5t_detect = ok_vlook(ttycharset_detect);

	NYD2_OU;
	return mpccp;
}

boole
mx_mime_probe_head_cp(char const **charset, char const *cp, struct mx_mime_probe_charset_ctx const *mpccp){
	boole rv;
	NYD2_IN;

	for(;;){
		char c;

		c = *cp++;
		if(c == '\0'){
			mx_MIME_PROBE_HEAD_DEFAULT_RES(rv, charset, mpccp);
			break;
		}
		if(S(u8,c) & 0x80){
			rv = TRU1;
			if(mpccp->mpcc_ttyc5t_detect != NIL)
				goto jslow;
			*charset = mpccp->mpcc_ttyc5t;
			break;
		}
	}

jleave:
	NYD2_OU;
	return rv;

jslow:/* C99 */{
	uz l;

	l = su_cs_len(--cp);
	for(;;){
		u32 utf;

		if((utf = su_utf8_to_32(&cp, &l)) == U32_MAX){
			*charset = ((*mpccp->mpcc_ttyc5t_detect != '\0')
					? mpccp->mpcc_ttyc5t_detect : mpccp->mpcc_ttyc5t);
			break;
		}else if(utf == '\0'/* XXX ?? */ || l == 0){
			*charset = su_utf8_name_lower;
			break;
		}
	}
	}goto jleave;
}

struct mx_mime_probe_ctx *
mx_mime_probe_setup(struct mx_mime_probe_ctx *mpcp, BITENUM(u32,mx_mime_probe_flags) mpf){
	NYD2_IN;

	STRUCT_ZERO(struct mx_mime_probe_ctx, mpcp);
	/*mpcp->mpc_lastc =*/ mpcp->mpc_c = EOF;
	mpcp->mpc_ttyc5t_detect = ok_vlook(ttycharset_detect);
	mpcp->mpc_mpf = mpf | mx__MIME_PROBE_1STLINE |
			(mpcp->mpc_ttyc5t_detect != NIL ? mx__MIME_PROBE_TTYC5T_DETECT : mx_MIME_PROBE_NONE);

	NYD2_OU;
	return mpcp;
}

BITENUM(u32,mx_mime_probe_flags)
mx_mime_probe_round(struct mx_mime_probe_ctx *mpcp, char const *bp, uz bl){
	/* TODO BTW., after the MIME/send layer rewrite we could use the shorted possible MIME boundary ("=-=-=")
	 * TODO if we would add a B_ in EQ spirit to F_, and report that state (to mime_param_boundary_create()) */
#define a_F_ "From "
#define a_F_SIZEOF (sizeof(a_F_) -1)
	enum {a_ISEOF = mx__MIME_PROBE_FREE};

	char f_buf[a_F_SIZEOF], *f_p = f_buf;
	BITENUM(u32,mx_mime_probe_flags) mpf;
	int c, lastc;
	s64 alllen;
	sz curlnlen;
	NYD2_IN;

	curlnlen = mpcp->mpc_curlnlen;
	alllen = mpcp->mpc_all_len;
	c = mpcp->mpc_c;
	/*lastc = mpcp->mpc_lastc;*/
	mpf = mpcp->mpc_mpf;
	if(bl == 0)
		mpf |= a_ISEOF;

	for(;; ++curlnlen){
		if(bl == 0){
			/* Real EOF, or only current buffer end? */
			if(mpf & a_ISEOF){
				lastc = c;
				c = EOF;
			}else{
				lastc = EOF;
				break;
			}
		}else{
			++alllen;
			lastc = c;
			c = S(uc,*bp++);
		}
		--bl;

		if(UNLIKELY(c == '\n' || c == EOF)){
			mpf &= ~mx__MIME_PROBE_1STLINE;
			if(curlnlen >= MIME_LINELEN_LIMIT)
				mpf |= mx_MIME_PROBE_LONGLINES;
			if(lastc == '\r')
				mpf |= mx_MIME_PROBE_CRLF;
			if(c == EOF)
				break;
			f_p = f_buf;
			curlnlen = -1;
			continue;
		}

		if(UNLIKELY(c == '\0')){
			mpf |= mx_MIME_PROBE_HASNUL;
			if(!(mpf & mx_MIME_PROBE_CT_TXT_COK)){
				mpf |= mx_MIME_PROBE_NO_TXT_4SURE;
				break;
			}
			continue;
		}

		/* A bit hairy is handling of \r=\x0D=CR.
		 * RFC 2045, 6.7:
		 * Control characters other than TAB, or CR and LF as parts of CRLF pairs, must not appear.  \r alone
		 * does not force _CTRLCHAR below since we cannot peek the next character.  Thus right here, inspect
		 * the last seen character for if its \r and set _CTRLCHAR in a delayed fashion */
		 /*else*/ if(lastc == '\r')
			mpf |= mx_MIME_PROBE_CTRLCHAR;

		/* Control character? XXX this is all ASCII here */
		if(c < 0x20 || c == 0x7F){
			/* RFC 2045, 6.7, as above ... */
			if(c != '\t' && c != '\r')
				mpf |= mx_MIME_PROBE_CTRLCHAR;

			/* If there is a escape sequence in reverse solidus notation defined for this in ANSI
			 * X3.159-1989 (ANSI C89), do not treat it as a control for real.
			 * I.e., \a=\x07=BEL, \b=\x08=BS, \t=\x09=HT.
			 * Do not follow libmagic(1) in respect to \v=\x0B=VT.  \f=\x0C=NP; ignore \e=\x1B=ESC */
			if((c >= '\x07' && c <= '\x0D') || c == '\x1B')
				continue;

			/* As a special case, if we are going for displaying data to the user or quoting a message then
			 * simply continue this, in the end, in case we get there, we will decide upon the
			 * all_len/all_bogus ratio whether this is usable plain text or not */
			++mpcp->mpc_all_bogus;
			if(mpf & mx_MIME_PROBE_DEEP_INSPECT)
				continue;

			mpf |= mx_MIME_PROBE_HASNUL; /* Force base64 */
			if(!(mpf & mx_MIME_PROBE_CT_TXT_COK)){
				mpf |= mx_MIME_PROBE_NO_TXT_4SURE;
				break;
			}
		}else if(S(u8,c) & 0x80){
			mpf |= mx_MIME_PROBE_HIGHBIT;
			++mpcp->mpc_all_highbit;
			if(!(mpf & (mx_MIME_PROBE_CT_NONE | mx_MIME_PROBE_CT_TXT))){/* TODO _CT_NONE? */
				mpf |= mx_MIME_PROBE_HASNUL /* =base64 XXX but ugly! */ | mx_MIME_PROBE_NO_TXT_4SURE;
				break;
			}else if(mpf & mx__MIME_PROBE_TTYC5T_DETECT){
				--bp;
				++bl;
				if(su_utf8_to_32(&bp, &bl) != U32_MAX)
					mpf |= mx_MIME_PROBE_TTYC5T_UTF8;
				else{
					mpf &= ~(mx__MIME_PROBE_TTYC5T_DETECT | mx_MIME_PROBE_TTYC5T_UTF8);
					mpf |= mx_MIME_PROBE_TTYC5T_OTHER;
				}
			}
		}else if(!(mpf & mx_MIME_PROBE_FROM_) && UCMP(z, curlnlen, <, a_F_SIZEOF)){
			*f_p++ = S(char,c);
			if(UCMP(z, curlnlen, ==, a_F_SIZEOF - 1) && P2UZ(f_p - f_buf) == a_F_SIZEOF &&
					!su_mem_cmp(f_buf, a_F_, a_F_SIZEOF)){
				mpf |= mx_MIME_PROBE_FROM_;
				if(mpf & mx__MIME_PROBE_1STLINE)
					mpf |= mx_MIME_PROBE_FROM_1STLINE;
			}
		}
	}
	if(c == EOF && lastc != '\n')
		mpf |= mx_MIME_PROBE_NOTERMNL;

	mpf &= mx__MIME_PROBE_MASK;

	mpcp->mpc_curlnlen = curlnlen;
	/*mpcp->mpc_lastc = lastc*/;
	mpcp->mpc_c = c;
	mpcp->mpc_mpf = mpf;
	mpcp->mpc_all_len = alllen;

	NYD2_OU;
	return mpf;

#undef a_F_
#undef a_F_SIZEOF
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MIME_PROBE
/* s-itt-mode */
