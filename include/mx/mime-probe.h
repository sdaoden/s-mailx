/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Content probing (input character set-, needed-conversion detection).
 *
 * Copyright (c) 2013 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_MIME_PROBE_H
#define mx_MIME_PROBE_H

#include <su/code.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_mime_probe_charset_ctx;
struct mx_mime_probe_ctx;

enum mx_mime_probe_flags{ /* (xxx) split in _INPUT_/_STATE_, or _MODE_ ..?*/
	mx_MIME_PROBE_NONE,
	/* Input */
	mx_MIME_PROBE_CLEAN = mx_MIME_PROBE_NONE, /* Plain RFC 5322 message */
	mx_MIME_PROBE_DEEP_INSPECT = 1u<<0, /*@I Always test all the file */
	mx_MIME_PROBE_CT_NONE = 1u<<1, /*@I *content_type == NIL */
	mx_MIME_PROBE_CT_TXT = 1u<<2, /*@I *content_type =~ text\/ */
	mx_MIME_PROBE_CT_TXT_COK = 1u<<3, /*@I CT_TXT + *mime-allow-text-controls* XXX latter brain damage */
	/* (Only) Output */
	mx_MIME_PROBE_HIGHBIT = 1u<<4, /* Not 7bit clean */
	mx_MIME_PROBE_TTYC5T_UTF8 = 1u<<5, /* *ttycharset-detect* was set and only UTF-8 seen, .. */
	mx_MIME_PROBE_TTYC5T_OTHER = 1u<<6, /* ..set non-empty, use the "other" */
	mx_MIME_PROBE_LONGLINES = 1u<<7, /* MIME_LINELEN_LIMIT exceed. */
	mx_MIME_PROBE_CRLF = 1u<<8, /* \x0D\x0A sequences HACK v15: simply reencode */
	mx_MIME_PROBE_CTRLCHAR = 1u<<9, /* Control characters seen */
	mx_MIME_PROBE_HASNUL = 1u<<10, /* Contains \0 characters */
	mx_MIME_PROBE_NOTERMNL = 1u<<11, /* Lacks a final newline */
	mx_MIME_PROBE_FROM_ = 1u<<12, /* ^From_ seen */
	mx_MIME_PROBE_FROM_1STLINE = 1u<<13, /* From_ line seen */
	/* Seems not to be human graspable textual data; NOT with CT_TXT! XXX not with CT_TXT_COK */
	mx_MIME_PROBE_NO_TXT_4SURE = 1u<<16,

	mx__MIME_PROBE_1STLINE = 1u<<17, /* .. */
	mx__MIME_PROBE_TTYC5T_DETECT = 1u<<18, /* *ttycharset-detect* */
	mx__MIME_PROBE_FREE = 1u<<19,
	mx__MIME_PROBE_MASK = mx__MIME_PROBE_FREE - 1
};

struct mx_mime_probe_charset_ctx{
	boole mpcc_iconv_disable; /* !mx_HAVE_ICONV||*iconv-disable* */
	boole mpcc_cs_7bit_is_ascii; /* Whether .mpcc_cs_7bit is indeed US-ASCII */
	boole mpcc_ttyc5t_is_utf8; /* .mpcc_ttyc5t is indeed UTF-8 */
	u8 mpcc__pad[su_6432(5,1)];
	char const *mpcc_cs_7bit; /* *charset-7bit*: 7-bit clean charset */
	char const *mpcc_ttyc5t; /* *ttycharset*: 8-bit capable (assumed) */
	/* *ttycharset-detect*: UTF-8 auto-detection if set (su_empty or with value, assumed to be 8-bit capable).
	 * With auto-detection input character set results will be:
	 * . If 7-bit clean mpcc_cs_7bit is used as usual (NIL if mpcc_cs_7bit_is_ascii)
	 * . If UTF-8 is detected su_utf8_name is used.
	 * . Otherwise mpcc_ttyc5t is used unless this is set with value */
	char const *mpcc_ttyc5t_detect;
};

struct mx_mime_probe_ctx{
	BITENUM(u32,mx_mime_probe_flags) mpc_mpf; /*@I(O) */
	/*char mpc_lastc;*/
	char mpc_c;
	u8 mpc__dummy[3];
	sz mpc_curlnlen;
	u64 mpc_all_len;
	u64 mpc_all_highbit; /* TODO not yet interpreted */
	u64 mpc_all_bogus;
	char const *mpc_ttyc5t_detect; /* setup() to *ttycharset_detect* */
};

/* Fills in mpccp */
EXPORT struct mx_mime_probe_charset_ctx *mx_mime_probe_charset_ctx_setup(struct mx_mime_probe_charset_ctx *mpccp);

/* Check whether header body/param string needs MIME conversion, set *charset according to mpccp.
 * Return TRU1 for MIME+iconv, and TRUM1 for only MIME.
 * Note: only cares for character set, other MIME necessities are not taken into account! */
#define mx_MIME_PROBE_HEAD_DEFAULT_RES(BOOLE,CPP,MPCCP) \
do{\
	BOOLE = TRUM1 /* XXX mpccp->mpcc_cs_7bit_is_ascii ? TRUM1 : TRU1 */;\
	*(CPP) = (MPCCP)->mpcc_cs_7bit;\
}while(0)
EXPORT boole mx_mime_probe_head_cp(char const **charset, char const *cp, struct mx_mime_probe_charset_ctx const *mpccp);

/* In-depth inspection of raw content: call _round() repeatedly, last time with a 0 length buffer, finally check
 * .mpc_mpf for result.
 * No further call if _round() return includes _NO_TXT_4SURE, as the resulting classification is unambiguous */
EXPORT struct mx_mime_probe_ctx *mx_mime_probe_setup(struct mx_mime_probe_ctx *mpcp,
		BITENUM(u32,mx_mime_probe_flags) mpf);

EXPORT BITENUM(u32,mx_mime_probe_flags) mx_mime_probe_round(struct mx_mime_probe_ctx *mpcp, char const *bp, uz bl);

#include <su/code-ou.h>
#endif /* mx_MIME_PROBE_H */
/* s-itt-mode */
