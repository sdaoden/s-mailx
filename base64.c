/*	$Id: base64.c,v 1.2 2000/03/21 03:12:24 gunnar Exp $	*/

/*
 * These base64 routines are derived from the metamail-2.7 sources which
 * state the following copyright notice:
 *
 * Copyright (c) 1991 Bell Communications Research, Inc. (Bellcore)
 *
 * Permission to use, copy, modify, and distribute this material 
 * for any purpose and without fee is hereby granted, provided 
 * that the above copyright notice and this permission notice 
 * appear in all copies, and that the name of Bellcore not be 
 * used in advertising or publicity pertaining to this 
 * material without the specific, prior written permission 
 * of an authorized representative of Bellcore.  BELLCORE 
 * MAKES NO REPRESENTATIONS ABOUT THE ACCURACY OR SUITABILITY 
 * OF THIS MATERIAL FOR ANY PURPOSE.  IT IS PROVIDED "AS IS", 
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES.
 */

#ifndef lint
static char rcsid[] __attribute__ ((unused)) = "@(#)$Id: base64.c,v 1.2 2000/03/21 03:12:24 gunnar Exp $";
#endif /* not lint */

/*
 * Mail -- a mail program
 *
 * base64 functions
 */

#include "rcv.h"
#include "extern.h"

const static char b64table[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const static char b64index[] = {
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
	-1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
	52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-1,-1,-1,
	-1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
	15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
	-1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
	41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};

#define char64(c)  ((c) < 0 ? -1 : b64index[(c)])

static char *ctob64(c, d, e, pad)
unsigned char c, d, e;
{
	static char b64[4];

	b64[0] = b64table[c >> 2];
	b64[1] = b64table[((c & 0x3) << 4) | ((d & 0xF0) >> 4)];
	if (pad == 2) {
		b64[2] = b64[3] = '=';
	} else if (pad == 1) {
		b64[2] = b64table[((d & 0xF) << 2) | ((e & 0xC0) >> 6)];
		b64[3] = '=';
	} else {
		b64[2] = b64table[((d & 0xF) << 2) | ((e & 0xC0) >> 6)];
		b64[3] = b64table[e & 0x3F];
	}
	return b64;
}

size_t mime_write_tob64(in, fo)
struct str *in;
FILE *fo;
{
	char *p, *h, *upper;
	int l;
	size_t sz;

	sz = 0;
	upper = in->s + in->l;
	for (p = in->s, l = 0; p < upper; p += 3) {
		if (p + 1 >= upper) {
			h = ctob64(*p, 0, 0, 2);
		} else if (p + 2 >= upper) {
			h = ctob64(*p, *(p + 1), 0, 1);
		} else {
			h = ctob64(*p, *(p + 1), *(p + 2), 0);
		}
		fwrite(h, sizeof(char), 4, fo);
		sz += 4, l += 4;
		if (l >= 71) {
			fputc('\n', fo), sz++;
			l = 0;
		}
	}
	if (l != 0) {
		fputc('\n', fo), sz++;
	}
	return sz;
}

void mime_fromb64(in, out, todisplay)
struct str *in, *out;
{
	char *p, *q, *upper, c, d, e, f, g;
	int done = 0;

	out->s = smalloc(in->l * 3 / 4 + 2);
	out->l = 0;
	upper = in->s + in->l;
	for (p = in->s, q = out->s; p < upper; ) {
		while (isspace(c = *p++));
		if (p >= upper) break;
		if (done) continue;
		while (isspace(d = *p++));
		if (p >= upper) break;
		while (isspace(e = *p++));
		if (p >= upper) break;
		while (isspace(f = *p++));
		if (c == '=' || d == '=') {
			done = 1;
			continue;
		}
		c = char64(c);
		d = char64(d);
		g = ((c << 2) | ((d & 0x30) >> 4));
		if (todisplay && isspecial(g))
			g = '?';
		*q++ = g;
		out->l++;
		if (e == '=') {
			done = 1;
		} else {
			e = char64(e);
			g = (((d & 0xF) << 4) | ((e & 0x3C) >> 2));
			if (todisplay && isspecial(g))
				g = '?';
			*q++ = g;
			out->l++;
			if (f == '=') {
				done = 1;
			} else {
				f = char64(f);
				g = (((e & 0x03) << 6) | f);
				if (todisplay && isspecial(g))
					g = '?';
				*q++ = g;
				out->l++;
			}
		}
	}
	return;
}
