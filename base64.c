/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 Steffen "Daode" Nurpmeso.
 */
/* _decode() and b64_encode() are adapted from NetBSDs mailx(1): */
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
 * Base64 functions
 * Defined in section 6.8 of RFC 2045.
 */

#include "rcv.h"
#include "extern.h"

/* Perform b64_decode on sufficiently spaced & multiple-of-4 base *in*put.
 * Return number of useful bytes in *out* or -1 on error */
static ssize_t	_decode(struct str *out, struct str *in);

static ssize_t
_decode(struct str *out, struct str *in)
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

		if (a > 64 || b > 64 || c == BAD || d == BAD)
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
	in->l -= (size_t)((char*)q - in->s);
	in->s = (char*)q;
	return ret;
}

size_t
b64_encode_calc_size(size_t len)
{
	len = (len * 4) / 3;
	len += (((len / B64_ENCODE_INPUT_PER_LINE) + 1) * 3) + 1;
	return len;
}

struct str *
b64_encode(struct str *out, struct str const *in, enum b64flags flags)
{
	static char const b64table[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	uc_it const *p = (uc_it const*)in->s;
	char *b64;
	ssize_t lnlen, i = b64_encode_calc_size(in->l);

	if (flags & B64_BUF) {
		if (i > (ssize_t)out->l) {
			assert(out->l != 0);
			out->l = 0;
			goto jleave;
		}
		b64 = out->s;
	} else
		out->s = b64 = (flags & B64_SALLOC) ? salloc(i)
				: srealloc(out->s, i);

	if (! (flags & (B64_CRLF|B64_LF)))
		flags &= ~B64_MULTILINE;
	lnlen = 0;

	for (i = (ssize_t)in->l; i > 0; p += 3, i -= 3) {
		ui_it a = p[0], b, c;

		b64[0] = b64table[a >> 2];
		switch (i) {
		case 1:
			b64[1] = b64table[((a & 0x3) << 4)];
			b64[2] = b64[3] = '=';
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
jleave:
	out->s[out->l] = '\0';
	return out;
}

struct str *
b64_encode_cp(struct str *out, char const *cp, enum b64flags flags)
{
	struct str in;
	in.s = (char*)cp;
	in.l = strlen(cp);
	return b64_encode(out, &in, flags);
}

struct str *
b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
	enum b64flags flags)
{
	struct str in;
	in.s = (char*)vp;
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
	char *xins, *x, *xtop, *xnl;
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
	if (ret != OKAY || (ssize_t)(len = _decode(out, &work)) < 0)
		goto jerr;
	if (rest == NULL)
		goto jleave;

	/* Strip CRs, detect NLs */
	xnl = NULL;
	for (xins = out->s, x = xins, xtop = x + out->l; x < xtop;
			*xins++ = *x++)
		switch (*x) {
		case '\r':
			ret = STOP;
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
		rest->s = srealloc(rest->s, rest->l + out->l);
		memcpy(rest->s + rest->l, out->s, out->l);
		rest->l += out->l;
		out->l = 0;
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

struct str *
b64_decode_join(struct str *out, struct str *rest)
{
	if (rest->l > 0) {
		if (out->s == NULL) {
			*out = *rest;
			rest->s = NULL;
		} else {
			out->s = srealloc(out->s, out->l + rest->l);
			memcpy(out->s + out->l, rest->s, rest->l);
			out->l += rest->l;
		}
		rest->l = 0;
	}
	if (rest->s != NULL) {
		free(rest->s);
		rest->s = NULL;
	}
	return out;
}
