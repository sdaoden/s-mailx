/*	$Id: mime.c,v 1.3 2000/03/24 23:01:39 gunnar Exp $	*/

/*
 * Copyright (c) 2000
 *	Gunnar Ritter.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Gunnar Ritter
 *	and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static char copyright[]  =
"@(#) Copyright (c) 2000 Gunnar Ritter. All rights reserved.\n";
static char rcsid[]  = "@(#)$Id: mime.c,v 1.3 2000/03/24 23:01:39 gunnar Exp $";
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * MIME support functions
 */

const static char hextable[16] = "0123456789ABCDEF";
char *defcharset = "iso-8859-1";
static char *mimetypes_world = "/etc/mime.types";
static char *mimetypes_user = "~/mime.types";

int mustquote_body(c)
unsigned char c;
{
	if (c != '\n' && (c < 0x20 || c == '=' || c > 126))
		return 1;
	return 0;
}

int mustquote_hdr(c)
unsigned char c;
{
	if (c != '\n'
		&& (c < 0x20 || c == '=' || c == '?' || c == '_' || c > 126))
		return 1;
	return 0;
}

int isspecial(c)
unsigned char c;
{
	if ((c >= 0177 && c < 0240)
		|| (c < 0x20 && c!='\n' && c!='\r' && c!='\b' && c!='\t'))
		return 1;
	return 0;
}

static void out_of_memory()
{
	panic("Out of memory");
}

void *smalloc(s)
size_t s;
{
	void *p;

	p = malloc(s);
	if (p == NULL) {
		out_of_memory;
	}
	return p;
}

char *mime_strcasestr(haystack, needle)
/* glibc 2.1 has such a function, but others ... */
char *haystack, *needle;
{
	char *p, initial[3];
	size_t sz;

	sz = strlen(needle);
	if (sz == 0)
		return NULL;
	initial[0] = *needle;
	if (islower(*needle)) {
		initial[1] = toupper(*needle);
	} else if (isupper(*needle)) {
		initial[1] = tolower(*needle);
	} else {
		initial[1] = '\0';
	}
	initial[2] = '\0';
	for (p = haystack; p = strpbrk(p, initial); p++) {
		if (strncasecmp(p, needle, sz) == 0)
			break;
	}
	return p;
}

int mime_getenc(h)
char *h;
/* Get the mime encoding from a Content-Transfer-Encoding header line */
{
	char *p;

	p = strchr(h, ':');
	if (p == NULL)
		return MIME_NONE;
	p++;
	while (*p && *p == ' ')
		p++;
	if (strncasecmp(p, "7bit", 4) == 0)
		return MIME_7B;
	if (strncasecmp(p, "8bit", 4) == 0)
		return MIME_8B;
	if (strncasecmp(p, "base64", 6) == 0)
		return MIME_B64;
	if (strncasecmp(p, "binary", 6) == 0)
		return MIME_BIN;
	if (strncasecmp(p, "quoted-printable", 16) == 0)
		return MIME_QP;
	return MIME_NONE;
}

int mime_getcontent(h)
char *h;
{
	char *p;

	p = strchr(h, ':');
	if (p == NULL)
		return 1;
	p++;
	while (*p && *p == ' ')
		p++;
	if (strchr(p, '/') == NULL)	/* for compatibility with non-MIME */
		return MIME_TEXT;
	if (strncasecmp(p, "text/", 5) == 0)
		return MIME_TEXT;
	if (strncasecmp(p, "message/", 8) == 0)
		return MIME_MESSAGE;
	if (strncasecmp(p, "multipart/", 10) == 0)
		return MIME_MULTI;
	return MIME_UNKNOWN;
}

char *mime_getparam(param, h)
char *param, *h;
{
	char *p, *q, *r;
	size_t sz;

	p = mime_strcasestr(h, param);
	if (p == NULL)
		return NULL;
	p += strlen(param);
	while (isspace(*p)) p++;
	if (*p == '\"') {
		p++;
		q = strchr(p, '\"');
		if (q == NULL)
			return NULL;
		sz = q - p;
	} else {
		q = p;
		while (isspace(*q) == 0 && *q != ';') q++;
		sz = q - p;
	}
	r = (char*)smalloc(sz + 1);
	memcpy(r, p, sz);
	*(r + sz) = '\0';
	return r;
}

char *mime_getboundary(h)
char *h;
{
	char *p, *q;
	size_t sz;

	p = mime_getparam("boundary=", h);
	if (p == NULL)
		return NULL;
	sz = strlen(p);
	q = (char*)smalloc(sz + 3);
	memcpy(q, "--", 2);
	memcpy(q + 2, p, sz);
	*(q + sz + 2) = '\0';
	free(p);
	return q;
}

char *mime_getfilename(h)
char *h;
{
	return mime_getparam("filename=", h);
}

static char *mime_tline(x, l)
char *x, *l;
/* Get a line like "text/html html" and look if x matches the extension
 * The line must be terminated by a newline character
 */
{
	char *type, *n;
	int match = 0;

	if (*l & 0200 || isalpha(*l) == 0)
		return NULL;
	type = l;
	while (isspace(*l) == 0 && *l != '\0') l++;
	if (*l == '\0') return NULL;
	*l++ = '\0';
	while (isspace(*l) != 0 && *l != '\0') l++;
	if (*l == '\0') return NULL;
	while (*l != '\0') {
		n = l;
		while (isspace(*l) == 0 && *l != '\0') l++;
		if (*l == '\0') return NULL;
		*l++ = '\0';
		if (strcmp(x, n) == 0) {
			match = 1;
			break;
		}
		while (isspace(*l) != 0 && *l != '\0') l++;
	}
	if (match != 0) {
		n = (char*)malloc(strlen(type) + 1);
		strcpy(n, type);
		return n;
	}
	return NULL;
}

static char *mime_type(ext, filename)
char *ext, *filename;
{
	FILE *f;
	char line[LINESIZE];
	char *type = NULL;

	f = fopen(filename, "r");
	if (f == NULL)
		return NULL;
	while (fgets(line, LINESIZE, f)) {
		type = mime_tline(ext, line);
		if (type != NULL)
			break;
	}
	fclose(f);
	return type;
}

char *mime_filecontent(name)
char *name;
/* Return the Content-Type matching the extension of name */
{
	char *ext, *content;

	ext = strrchr(name, '.');
	if (ext == NULL)
		return NULL;
	else
		ext++;
	if (content = mime_type(ext, expand(mimetypes_user)))
		return content;
	if (content = mime_type(ext, mimetypes_world))
		return content;
	return NULL;
}

int mime_isclean(f)
FILE *f;
/* Check file contents. Return:
 * 0 file is 7bit
 * 1 file is international text
 * 2 file is binary
 */
{
	int isclean;
	long initial_pos;
	char c;

	isclean = 0;
	initial_pos = ftell(f);
	for (;;) {
		c = getc(f);
		if (c == EOF && (feof(f) || ferror(f))) {
			break;
		}
		if (c & 0200) {
			isclean = 1;
			continue;
		}
		if ((c < 0x20 && (c != ' ' && c != '\n' && c != '\t'))
			|| c == 0177) {
			isclean = 2;
			break;
		}
	}
	clearerr(f);
	fseek(f, initial_pos, SEEK_SET);
	return isclean;
}

char *
itohex(i, b)
unsigned i;
char *b;
{
	char *p, c;
	
	p = b;
	while (i != 0) {
		*p++ = hextable[i % 16];
		i /= 16;
	}
	*p-- = '\0';
	while (p >= b) {
		c = *b;
		*b++ = *p;
		*p-- = c;
	}
	return b;
}

char *ctohex(c)
unsigned char c;
{
	static char hex[3];
	unsigned char d;

	hex[2] = '\0';
	d = c % 16;
	hex[1] = hextable[d];
	if (c > d)
		hex[0] = hextable[(c - d) / 16];
	else
		hex[0] = hextable[0];
	return hex;
}

static size_t mime_write_toqp(in, fo, mustquote)
struct str *in;
FILE *fo;
int (*mustquote)(unsigned char);
{
	char *p, *upper, *h;
	int l;
	size_t sz;

	sz = in->l;
	upper = in->s + in->l;
	for (p = in->s, l = 0; p < upper; p++) {
		if (mustquote(*p)) {
			sz += 2;
			fputc('=', fo);
			h = ctohex(*p);
			fwrite(h, sizeof(char), 2, fo);
			l += 3;
		} else {
			if (*p == '\n') l = 0;
			fputc(*p, fo);
			l++;
		}
		if (l >= 71) {
			sz += 2;
			fwrite("=\n", sizeof(char), 2, fo);
			l = 0;
		}
	}
	return sz;
}

static void mime_fromqp(in, out, todisplay, ishdr)
struct str *in, *out;
{
	char *p, *q, *upper;
	char quote[4];

	out->l = in->l;
	out->s = smalloc(out->l + 1);
	upper = in->s + in->l;
	for (p = in->s, q = out->s; p < upper; p++) {
		if (*p == '=') {
			do {
				p++;
				out->l--;
			} while (*p == ' ' || *p == '\t');
			if (*p == '\n') {
				out->l--;
				continue;
			}
			if (p + 1 >= upper)
				break;
			quote[0] = *p++;
			quote[1] = *p;
			quote[2] = '\0';
			*q = (char)strtol(quote, NULL, 16);
			if (todisplay && isspecial(*q))
				*q = '?';
			q++;
			out->l--;
		} else if (ishdr && *p == '_') {
			*q++ = ' ';
		} else {
			*q++ = *p;
		}
	}
	return;
}

void mime_fromhdr(in, out, todisplay)
struct str *in, *out;
/* convert from RFC1522 format */
{
	char *p, *q, *upper;
	struct str cin, cout;
	int convert;

	out->s = (char*)smalloc(in->l + 1);
	out->l = 0;
	upper = in->s + in->l;
	for (p = in->s, q = out->s; p < upper; p++) {
		if (*p == '=' && *(p + 1) == '?') {
			p += 2;
			while (*p++ != '?');	/* strip charset */
			switch (*p) {
			case 'B': case 'b':
				convert = CONV_FROMB64;
				break;
			case 'Q': case 'q':
				convert = CONV_FROMQP;
				break;
			default:	/* invalid, ignore */
				continue;
			}
			if (*++p != '?')
				continue;
			cin.s = ++p;
			cin.l = 1;
			for (;;) {
				if (*p++ == '?' && *p == '=')
					break;
				cin.l++;
			}
			cin.l--;
			switch (convert) {
				case CONV_FROMB64:
					mime_fromb64(&cin, &cout, todisplay);
					break;
				case CONV_FROMQP:
					mime_fromqp(&cin, &cout,
						todisplay, 1);
					break;
				default: abort();
			}
			memcpy(q, cout.s, cout.l);
			q += cout.l;
			out->l += cout.l;
			free(cout.s);
		} else {
			*q++ = *p;
			out->l++;
		}
	}
	*q = '\0';
	return;
}

static size_t mime_write_tohdr(in, fo)
struct str *in;
FILE *fo;
{
	char *p, *upper, *charset;
	struct str cin;
	size_t sz;

	sz = in->l;
	upper = in->s + in->l;
	for (p = in->s; p < upper; p++) {
		if (mustquote_hdr(*p) && *p != ' ') {
			charset = value("charset");
			if (charset == NULL) {
				charset = defcharset;
				assign("charset", defcharset);
			}
			fprintf(fo, "=?%s?Q?", charset);
			cin.s = p;
			cin.l = upper - p;
			sz += mime_write_toqp(&cin, fo, mustquote_hdr) - cin.l;
			fwrite("?=", sizeof(char), 2, fo);
			sz += strlen(charset) + 7;
			break;
		} else {
			fputc(*p, fo);
		}
	}
	return sz;
}

size_t fwrite_tty(ptr, size, nmemb, f)
void *ptr;
size_t size, nmemb;
FILE *f;
{
	char *p, *upper;
	size_t sz;

	upper = (char*)ptr + nmemb / size;
	for (p = ptr, sz = 0; p < upper; p++, sz++) {
		if (isspecial(*p)) {
			fputc('?', f);
		} else
			fputc(*p, f);
	}
	return sz;
}

size_t mime_write(ptr, size, nmemb, f, convert, todisplay)
void *ptr;
size_t size, nmemb;
FILE *f;
/* fwrite with MIME conversions */
{
	struct str in, out;
	size_t sz;
	char quote[4];

	in.s = ptr;
	in.l = size * nmemb / sizeof(char);
	switch (convert) {
	case CONV_FROMQP:
		mime_fromqp(&in, &out, todisplay, 0);
		sz = fwrite(out.s, sizeof(char), out.l, f);
		free(out.s);
		break;
	case CONV_TOQP:
		sz = mime_write_toqp(&in, f, mustquote_body);
		break;
	case CONV_FROMB64:
		mime_fromb64(&in, &out, todisplay);
		sz = fwrite(out.s, sizeof(char), out.l, f);
		free(out.s);
		break;
	case CONV_TOB64:
		sz = mime_write_tob64(&in, f);
		break;
	case CONV_FROMHDR:
		mime_fromhdr(&in, &out, todisplay);
		sz = fwrite(out.s, sizeof(char), out.l, f);
		free(out.s);
		break;
	case CONV_TOHDR:
		sz = mime_write_tohdr(&in, f);
		break;
	default:
		if (todisplay == 0) {
			sz = fwrite(in.s, sizeof(char), in.l, f);
		} else {
			sz = fwrite_tty(in.s, sizeof(char), in.l, f);
		}
	}
	return sz;
}
