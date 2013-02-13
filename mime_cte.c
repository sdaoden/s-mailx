/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012, 2013 Steffen "Daode" Nurpmeso.
 */
/* QP quoting idea, _b64_decode(), b64_encode() taken from NetBSDs mailx(1): */
/*	$NetBSD: mime_codecs.c,v 1.9 2009/04/10 13:08:25 christos Exp $	*/
/*
 * Copyright (c) 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Anon Ymous.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Mail -- a mail program
 *
 * Content-Transfer-Encodings as defined in RFC 2045:
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8
 */

#include "rcv.h"
#include "extern.h"

enum _qact {
	 N =   0,	/* Do not quote */
	 Q =   1,	/* Must quote */
	SP =   2,	/* sp */
	XF =   3,	/* Special character 'F' - maybe quoted */
	XD =   4,	/* Special character '.' - maybe quoted */
	US = '_',	/* In header, special character ' ' quoted as '_' */
	QM = '?',	/* In header, special character ? not always quoted */
	EQ =   Q,	/* '=' must be quoted */
	TB =  SP,	/* Treat '\t' as a space */
	NL =   N,	/* Don't quote '\n' (NL) */
	CR =   Q	/* Always quote a '\r' (CR) */
};

/* Lookup tables to decide wether a character must be encoded or not.
 * Email header differences according to RFC 2047, section 4.2:
 * - also quote SP (as the underscore _), TAB, ?, _, CR, LF
 * - don't care about the special ^F[rom] and ^.$ */
static uc_it const	_qtab_body[] = {
		 Q, Q, Q, Q,  Q, Q, Q, Q,  Q,TB,NL, Q,  Q,CR, Q, Q,
		 Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,
		SP, N, N, N,  N, N, N, N,  N, N, N, N,  N, N,XD, N,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N,EQ, N, N,

		 N, N, N, N,  N, N,XF, N,  N, N, N, N,  N, N, N, N,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, Q,
	},
			_qtab_head[] = {
		 Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,
		 Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,  Q, Q, Q, Q,
		US, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N,EQ, N,QM,

		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, Q,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, N,
		 N, N, N, N,  N, N, N, N,  N, N, N, N,  N, N, N, Q,
	};

/* Check wether **s* must be quoted according to *ishead*, else body rules;
 * *sol* indicates wether we are at the first character of a line/field */
SINLINE enum _qact	_mustquote(char const *s, char const *e, bool_t sol,
				bool_t ishead);

/* Convert c to/from a hexadecimal character string */
SINLINE char *		_qp_ctohex(char *store, char c);
SINLINE si_it		_qp_cfromhex(char const *hex);
/* Perform b64_decode on sufficiently spaced & multiple-of-4 base *in*put.
 * Return number of useful bytes in *out* or -1 on error */
static ssize_t		_b64_decode(struct str *out, struct str *in);

SINLINE enum _qact
_mustquote(char const *s, char const *e, bool_t sol, bool_t ishead)
{
	uc_it const *qtab = ishead ? _qtab_head : _qtab_body;
	enum _qact a = ((uc_it)*s > 0x7F) ? Q : qtab[(uc_it)*s], r;

	if ((r = a) == N || (r = a) == Q)
		goto jleave;
	r = Q;

	/* Special header fields */
	if (ishead) {
		/* ' ' -> '_' */
		if (a == US) {
			r = US;
			goto jleave;
		}
		/* Treat '?' only special if part of '=?' and '?=' (still to
		 * much quoting since it's '=?CHARSET?CTE?stuff?=', and
		 * especially the trailing ?= should be hard too match ,) */
		if (a == QM && ((! sol && s[-1] == '=') ||
				(s < e && s[1] == '=')))
			goto jleave;
		goto jnquote;
	}

	/* Body-only */

	if (a == SP) {
		/* WS only if trailing white space */
		if (s + 1 == e || s[1] == '\n')
			goto jleave;
		goto jnquote;
	}

	/* Rest are special begin-of-line cases */
	if (! sol)
		goto jnquote;

	/* ^From */
	if (a == XF) {
		if (s + 4 < e && s[1] == 'r' && s[2] == 'o' && s[3] == 'm')
			goto jleave;
		goto jnquote;
	}
	/* ^.$ */
	if (a == XD && (s + 1 == e || s[1] == '\n'))
		goto jleave;
jnquote:
	r = N;
jleave:
	return r;
}

SINLINE char *
_qp_ctohex(char *store, char c)
{
	static char const hexmap[] = "0123456789ABCDEF";

	store[2] = '\0';
	store[1] = hexmap[(uc_it)c & 0x0F];
	c = ((uc_it)c >> 4) & 0x0F;
	store[0] = hexmap[(uc_it)c];
	return store;
}

SINLINE si_it
_qp_cfromhex(char const *hex)
{
	static uc_it const atoi16[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF
	};
	uc_it i1, i2;
	si_it r;

	if ((i1 = (uc_it)hex[0] - '0') >= ARRAY_COUNT(atoi16) ||
			(i2 = (uc_it)hex[1] - '0') >= ARRAY_COUNT(atoi16))
		goto jerr;
	i1 = atoi16[i1];
	i2 = atoi16[i2];
	if ((i1 | i2) & 0xF0)
		goto jerr;
	r = i1;
	r <<= 4;
	r += i2;
jleave:
	return r;
jerr:
	r = -1;
	goto jleave;
}

static ssize_t
_b64_decode(struct str *out, struct str *in)
{
	static signed char const b64index[] = {
		-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
		-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
		-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
		52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-2,-1,-1,
		-1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
		15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
		-1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
		41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
	};
#define EQU		(ui_it)-2
#define BAD		(ui_it)-1
#define uchar64(c)	((c) >= sizeof(b64index) ? BAD : (ui_it)b64index[(c)])

	ssize_t ret = -1;
	uc_it *p = (uc_it*)out->s;
	uc_it const *q = (uc_it const*)in->s, *end;

	out->l = 0;

	for (end = q + in->l; q + 4 <= end; q += 4) {
		ui_it a = uchar64(q[0]), b = uchar64(q[1]), c = uchar64(q[2]),
			d = uchar64(q[3]);

		if (a >= EQU || b >= EQU || c == BAD || d == BAD)
			goto jleave;

		*p++ = ((a << 2) | ((b & 0x30) >> 4));
		if (c == EQU)	{ /* got '=' */
			if (d != EQU)
				goto jleave;
			break;
		}
		*p++ = (((b & 0x0f) << 4) | ((c & 0x3c) >> 2));
		if (d == EQU) /* got '=' */
			break;
		*p++ = (((c & 0x03) << 6) | d);
	}
#undef uchar64
#undef EQU
#undef BAD

	ret = (size_t)((char*)p - out->s);
	out->l = (size_t)ret;
jleave:
	in->l -= (size_t)((char*)UNCONST(q) - in->s);
	in->s = UNCONST(q);
	return ret;
}

size_t
mime_cte_mustquote(char const *ln, size_t lnlen, bool_t ishead)
{
	size_t ret;
	bool_t sol;

	for (ret = 0, sol = TRU1; lnlen > 0; sol = FAL0, ++ln, --lnlen)
		ret += (_mustquote(ln, ln + lnlen, sol, ishead) != N);
	return ret;
}

size_t
qp_encode_calc_size(size_t len)
{
	/* Worst case: 'CRLF' -> '=0D=0A=' */
	len = len * 3 + (len >> 1) + 1;
	return len;
}

struct str *
qp_encode_cp(struct str *out, char const *cp, enum qpflags flags)
{
	struct str in;
	in.s = UNCONST(cp);
	in.l = strlen(cp);
	return qp_encode(out, &in, flags);
}

struct str *
qp_encode_buf(struct str *out, void const *vp, size_t vp_len,
	enum qpflags flags)
{
	struct str in;
	in.s = UNCONST(vp);
	in.l = vp_len;
	return qp_encode(out, &in, flags);
}

struct str *
qp_encode(struct str *out, struct str const *in, enum qpflags flags)
{
	bool_t sol = (flags & QP_ISHEAD ? FAL0 : TRU1), seenx;
	ssize_t lnlen;
	char *qp;
	char const *is, *ie;

	if ((flags & QP_BUF) == 0) {
		lnlen = qp_encode_calc_size(in->l);
		out->s = (flags & QP_SALLOC) ? salloc(lnlen)
				: srealloc(out->s, lnlen);
	}
	qp = out->s;
	is = in->s;
	ie = is + in->l;

	/* QP_ISHEAD? */
	if (! sol) {
		for (seenx = FAL0, sol = TRU1; is < ie; sol = FAL0, ++qp) {
			enum _qact mq = _mustquote(is, ie, sol, TRU1);
			char c = *is++;

			if (mq == N) {
				/* We convert into a single *encoded-word*,
				 * that'll end up in =?C?Q??=; quote '?' from
				 * the moment when we're inside there on */
				if (seenx && c == '?')
					goto jheadq;
				*qp = c;
			} else if (mq == US)
				*qp = US;
			else {
				seenx = TRU1;
jheadq:
				*qp++ = '=';
				qp = _qp_ctohex(qp, c) + 1;
			}
		}
		goto jleave;
	}

	/* The body needs to take care for soft line breaks etc. */
	for (lnlen = 0, seenx = FAL0; is < ie; sol = FAL0) {
		enum _qact mq = _mustquote(is, ie, sol, FAL0);
		char c = *is++;

		if (mq == N && (c != '\n' || ! seenx)) {
			*qp++ = c;
			if (++lnlen < QP_LINESIZE - 1 -1)
				continue;
jsoftnl:
			qp[0] = '=';
			qp[1] = '\n';
			qp += 2;
			lnlen = 0;
			continue;
		}

		if (lnlen >= QP_LINESIZE - 3 - 1 -1) {
			qp[0] = '=';
			qp[1] = '\n';
			qp += 2;
			lnlen = 0;
		}
		*qp++ = '=';
		qp = _qp_ctohex(qp, c);
		qp += 2;
		lnlen += 3;
		if (c != '\n' || ! seenx)
			seenx = (c == '\r');
		else {
			seenx = FAL0;
			goto jsoftnl;
		}
	}

	/* Lines that don't end with [CR]LF need an escaped [CR]LF */
	if (in->l > 0 && *--is != '\n') {
		qp[0] = '=';
		qp[1] = '\n';
		qp += 2;
	}
jleave:
	out->l = (size_t)(qp - out->s);
	out->s[out->l] = '\0';
	return out;
}

int
qp_decode(struct str *out, struct str const *in, struct str *rest)
{
	int ret = STOP;
	char *os, *oc;
	char const *is, *ie;

	if (rest != NULL && rest->l != 0) {
		if (out->s != NULL)
			free(out->s);
		out->s = rest->s;
		out->l = rest->l;
		rest->s = NULL;
		rest->l = 0;
	}

	oc = os =
	out->s = srealloc(out->s, out->l + in->l + 3);
	oc += out->l;
	is = in->s;
	ie = is + in->l;

	/* Decoding encoded-word (RFC 2049) in a header field? */
	if (rest == NULL) {
		while (is < ie) {
			si_it c = *is++;
			if (c == '=') {
				if (is + 1 >= ie) {
					++is;
					goto jehead;
				}
				c = _qp_cfromhex(is);
				is += 2;
				if (c >= 0)
					*oc++ = (char)c;
				else {
					/* Illegal according to RFC 2045,
					 * section 6.7.  Almost follow it */
jehead:
					oc[0] = '['; oc[1] = '?'; oc[2] = ']';
					oc += 3;
				}
			} else
				*oc++ = (c == '_') ? ' ' : (char)c;
		}
		goto jleave; /* XXX QP decode, header: errors not reported */
	}

	/* Decoding a complete message/mimepart body line */
	while (is < ie) {
		si_it c = *is++;
		if (c != '=') {
			*oc++ = (char)c;
			continue;
		}

		/*
		 * RFC 2045, 6.7:
		 *   Therefore, when decoding a Quoted-Printable body, any
		 *   trailing white space on a line must be deleted, as it will
		 *   necessarily have been added by intermediate transport
		 *   agents.
		 */
		for (; is < ie && blankchar(*is); ++is)
			;
		if (is + 1 >= ie) {
			/* Soft line break? */
			if (*is == '\n')
				goto jsoftnl;
			++is;
			goto jebody;
		}

		/* Not a soft line break? */
		if (*is != '\n') {
			c = _qp_cfromhex(is);
			is += 2;
			if (c >= 0)
				*oc++ = (char)c;
			else {
				/* Illegal according to RFC 2045, section 6.7.
				 * Rather follow it and include the = and the
				 * follow char */
jebody:
				oc[0] = '['; oc[1] = '?'; oc[2] = ']';
				oc += 3;
			}
			continue;
		}

		/* CRLF line endings are encoded as QP, followed by a soft line
		 * break, so check for this special case, and simply forget we
		 * have seen one, so as not to end up with the entire DOS file
		 * in a contiguous buffer */
jsoftnl:
		if (oc > os && oc[-1] == '\n') {
#if 0			/* TODO qp_decode() we do not normalize CRLF
			 * TODO to LF because for that we would need
			 * TODO to know if we are about to write to
			 * TODO the display or do save the file!
			 * TODO 'hope the MIME/send layer rewrite will
			 * TODO offer the possibility to DTRT */
			if (oc - 1 > os && oc[-2] == '\r') {
				--oc;
				oc[-1] = '\n';
			}
#endif
			break;
		}
		out->l = (size_t)(oc - os);
		rest->s = srealloc(rest->s, rest->l + out->l);
		memcpy(rest->s + rest->l, out->s, out->l);
		rest->l += out->l;
		oc = os;
		break;
	}
	/* XXX RFC: QP decode should check no trailing WS on line */
jleave:
	out->l = (size_t)(oc - os);
	ret = OKAY;
	return ret;
}

size_t
b64_encode_calc_size(size_t len)
{
	len = (len * 4) / 3;
	len += (((len / B64_ENCODE_INPUT_PER_LINE) + 1) * 3);
#if 0 /* TODO B64_ISTEXT */
	if (flags & B64_ISTEXT)
		len += 3;
#endif /* TODO B64_ISTEXT */
	++len;
	return len;
}

struct str *
b64_encode(struct str *out, struct str const *in, enum b64flags flags)
{
	/*
	 * TODO b64_encode() does not yet handle B64_ISTEXT.
	 * TODO That is ok since this flag is yet unused because the MIME
	 * TODO classifier doesn't differentiate in between base64 for text
	 * TODO and binary (i.e., if we decide base64 is needed its binary).
	 * TODO This means that the RFC 2045-enforced \n->\r\n conversion is
	 * TODO NOT performed.
	 */
	static char const b64table[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	uc_it const *p = (uc_it const*)in->s;
	ssize_t i = b64_encode_calc_size(in->l), lnlen;
	char *b64;

	if ((flags & B64_BUF) == 0)
		out->s = (flags & B64_SALLOC) ? salloc(i) : srealloc(out->s, i);
	b64 = out->s;

	if (! (flags & (B64_CRLF|B64_LF)))
		flags &= ~B64_MULTILINE;

	for (lnlen = 0, i = (ssize_t)in->l; i > 0; p += 3, i -= 3) {
		ui_it a = p[0], b, c;

		b64[0] = b64table[a >> 2];
		switch (i) {
		case 1:
			b64[1] = b64table[((a & 0x3) << 4)];
			b64[2] =
			b64[3] = '=';
			break;
		case 2:
			b = p[1];
			b64[1] = b64table[((a & 0x3) << 4) | ((b & 0xf0) >> 4)];
			b64[2] = b64table[((b & 0xf) << 2)];
			b64[3] = '=';
			break;
		default:
			b = p[1];
			c = p[2];
			b64[1] = b64table[((a & 0x3) << 4) | ((b & 0xf0) >> 4)];
			b64[2] = b64table[((b & 0xf) << 2) | ((c & 0xc0) >> 6)];
			b64[3] = b64table[c & 0x3f];
			break;
		}

		b64 += 4;
		if (! (flags & B64_MULTILINE))
			continue;
		lnlen += 4;
		if (lnlen < B64_LINESIZE - 1)
			continue;

		lnlen = 0;
		if (flags & B64_CRLF)
			*b64++ = '\r';
		if (flags & (B64_CRLF|B64_LF))
			*b64++ = '\n';
	}

	if ((flags & (B64_CRLF|B64_LF)) != 0 &&
			((flags & B64_MULTILINE) == 0 || lnlen != 0)) {
		if (flags & B64_CRLF)
			*b64++ = '\r';
		if (flags & (B64_CRLF|B64_LF))
			*b64++ = '\n';
	}
	out->l = (size_t)(b64 - out->s);
	out->s[out->l] = '\0';
	return out;
}

struct str *
b64_encode_cp(struct str *out, char const *cp, enum b64flags flags)
{
	struct str in;
	in.s = UNCONST(cp);
	in.l = strlen(cp);
	return b64_encode(out, &in, flags);
}

struct str *
b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
	enum b64flags flags)
{
	struct str in;
	in.s = UNCONST(vp);
	in.l = vp_len;
	return b64_encode(out, &in, flags);
}

size_t
b64_decode_prepare(struct str *work, struct str const *in)
{
	char *cp = in->s;
	size_t cp_len = in->l;

	while (cp_len > 0 && whitechar(*cp))
		++cp, --cp_len;
	work->s = cp;

	for (cp += cp_len; cp_len > 0; --cp_len) {
		char c = *--cp;
		if (! whitechar(c))
			break;
	}
	work->l = cp_len;

	if (cp_len > 16)
		cp_len = ((cp_len * 3) >> 2) + (cp_len >> 3);
	return cp_len;
}

int
b64_decode(struct str *out, struct str const *in, size_t len, struct str *rest)
{
	struct str work;
	char *x /* TODO B64_ISTEXT *xins, *xtop, *xnl */;
	int ret = STOP;

	if (len == 0)
		len = b64_decode_prepare(&work, in);
	else
		work = *in;

	/* Ignore an empty input, as may happen for an empty final line */
	if (work.l == 0)
		goto jempty;
	if (work.l >= 4 && (work.l & 3) == 0) {
		out->s = srealloc(out->s, len);
		ret = OKAY;
	}
	if (ret != OKAY || (ssize_t)(len = _b64_decode(out, &work)) < 0)
		goto jerr;
	/* Done for binary content */
	if (rest == NULL)
		goto jleave;

#if 0 /* TODO B64_ISTEXT */
	/* TODO Strip CRs, detect NLs
	 * TODO B64_ISTEXT - this will not work for multibyte encodings, and
	 * TODO also is simple minded in that RFC 2045 enforces \n->\r\n
	 * TODO conversion but does not give any hint on the original line
	 * TODO ending; i.e., this code may modify the original data!
	 * TODO anyway: if we do CONV_FROMB64_T, b64_decode() the *entire* part
	 * TODO into a temporary file, then iconv that file.
	 * TODO stripping the CRLF->LF is left as an excercise ;) */
	xnl = NULL;
	for (xins = out->s, x = xins, xtop = x + out->l; x < xtop;
			*xins++ = *x++)
		switch (*x) {
		case '\r':
			ret = STOP; /* TODO what about a trailing FINAL CR?? */
			break;
		case '\n':
			if (ret != OKAY)
				--xins;
			xnl = xins;
			/* FALLTHRU */
		default:
			ret = OKAY;
			break;
		}
	ret = OKAY;
	out->l = (ssize_t)(xins - out->s);

	if (xnl == NULL) {
#endif
		/* TODO B64_ISTEXT: send.c:sendpart() resized this on entry
		 * TODO to be of sufficient size
		 * rest->s = srealloc(rest->s, rest->l + out->l); */
		memcpy(rest->s + rest->l, out->s, out->l);
		rest->l += out->l;
		out->l = 0;
#if 0 /* TODO B64_ISTEXT */
	} else {
		work.l = (size_t)(xins - xnl);
		if (work.l) {
			work.s = ac_alloc(work.l);
			len = (size_t)(xnl - out->s);
			memcpy(work.s, out->s + len, work.l);
			out->l = len;
		}
		if (rest->l) {
			rest->s = srealloc(rest->s, rest->l + out->l);
			memcpy(rest->s + rest->l, out->s, out->l);
			x = out->s;
			out->s = rest->s;
			rest->s = x;
			out->l += rest->l;
			rest->l = 0;
		}
		if (work.l) {
			rest->s = srealloc(rest->s, work.l);
			memcpy(rest->s, work.s, work.l);
			rest->l = work.l;
			ac_free(work.s);
		}
	}
#endif

jleave:
	return ret;

jempty: out->l = 0;
	if (rest != NULL && rest->l != 0) {
		if (out->s != NULL)
			free(out->s);
		out->s = rest->s;
		out->l = rest->l;
		rest->s = NULL;
		rest->l = 0;
	}
	ret = OKAY;
	goto jleave;

jerr:	{
	char const *err = tr(15, "[Invalid Base64 encoding ignored]\n");
	len = strlen(err);
	x = out->s = srealloc(out->s, len + 2);
	if (rest != NULL && rest->l)
		*x++ = '\n';
	memcpy(x, err, len);
	x += len;
	*x = '\0';
	out->l = (size_t)(x - out->s);
	if (rest != NULL)
		rest->l = 0;
	ret = STOP;
	goto jleave;
	}
}
