/*	$Id: mime.c,v 1.11 2000/05/02 02:17:37 gunnar Exp $	*/

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
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
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
static char rcsid[]  = "@(#)$Id: mime.c,v 1.11 2000/05/02 02:17:37 gunnar Exp $";
#endif /* not lint */

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * MIME support functions.
 */

/*
 * You won't guess what these are for.
 */
const static char hextable[16] = "0123456789ABCDEF";
static char *mimetypes_world = "/etc/mime.types";
static char *mimetypes_user = "~/mime.types";

/*
 * Check for the readability of the given files.
 */
int
mime_check_attach(atts)
struct name *atts;
{
	struct name *a;

	for (a = atts; a != NIL; a = a->n_flink) {
		if (access(a->n_name, R_OK) != 0) {
			perror(a->n_name);
			return -1;
		}
	}
	return 0;
}

/*
 * Check if c must be quoted inside a message's body.
 */
static int
mustquote_body(c)
unsigned char c;
{
	if (c != '\n' && (c < 0x20 || c == '=' || c > 126))
		return 1;
	return 0;
}

/*
 * Check if c must be quoted inside a message's header.
 */
static int
mustquote_hdr(c)
unsigned char c;
{
	if (c != '\n'
		&& (c < 0x20 || c == '=' || c == '?' || c == '_' || c > 126))
		return 1;
	return 0;
}

/*
 * Check whether c may be displayed on an iso-8859-x terminal.
 */
int
is_undisplayable(c)
unsigned char c;
{
	if ((c >= 0177 && c < 0240)
		|| (c < 0x20 && c!='\n' && c!='\r' && c!='\b' && c!='\t'))
		return 1;
	return 0;
}

static void
out_of_memory()
{
	panic("Out of memory");
}

void *
smalloc(s)
size_t s;
{
	void *p;

	p = malloc(s);
	if (p == NULL) {
		out_of_memory();
	}
	return p;
}

/*
 * glibc 2.1 has such a function, but others ...
 */
static char *
mime_strcasestr(haystack, needle)
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
	for (p = haystack; (p = strpbrk(p, initial)) != NULL; p++) {
		if (strncasecmp(p, needle, sz) == 0)
			break;
	}
	return p;
}

#ifndef	HAVE_STRCASECMP
/* One of the things I really HATE on some SysVs is that they still
 * do not have these functions in their standard libc.
 * These implementations are quick-drawn and inefficient. Claim
 * your vendor for a better version, and one outside libucb!
 */
int
strcasecmp(s1, s2)
const char *s1, *s2;
{
	int cmp, c1, c2;

	do {
		if (isupper(*s1))
			c1 = tolower(*s1);
		else
			c1 = (unsigned char)*s1;
		if (isupper(*s2))
			c2 = tolower(*s2);
		else
			c2 = (unsigned char)*s2;
		cmp = c1 - c2;
		if (cmp != 0)
			return cmp;
	} while (*s1++ != '\0' && *s2++ != '\0');
	return 0;
}

int
strncasecmp(s1, s2, sz)
const char *s1, *s2;
size_t sz;
{
	int cmp, c1, c2;
	size_t i = 1;

	if (sz <= 0)
		return 0;
	do {
		if (isupper(*s1))
			c1 = tolower(*s1);
		else
			c1 = (unsigned char)*s1;
		if (isupper(*s2))
			c2 = tolower(*s2);
		else
			c2 = (unsigned char)*s2;
		cmp = c1 - c2;
		if (cmp != 0)
			return cmp;
	} while (i++ < sz && *s1++ != '\0' && *s2++ != '\0');
	return 0;
}
#endif	/* silly SysV functions */

/*
 * Get the character set dependant on the conversion.
 */
char *
mime_getcharset(convert)
{
	char *charset;

	if (convert == CONV_7BIT) {
		/*
		 * This variable shall remain undocumented because
		 * only experts should change it.
		 */
		charset = value("charset7");
		if (charset == NULL) {
			charset = "US-ASCII";
		}
	} else {
		charset = value("charset");
		if (charset == NULL) {
			charset = "iso-8859-1";
		}
	}
	return charset;
}

/*
 * Get the mime encoding from a Content-Transfer-Encoding header line.
 */
int
mime_getenc(h)
char *h;
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

/*
 * Get the mime content from a Content-Type header line.
 */
int
mime_getcontent(h)
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

/*
 * Get a mime style parameter from a header line.
 */
char *
mime_getparam(param, h)
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

/*
 * Get the boundary out of a Content-Type: multipart/xyz header field.
 */
char *
mime_getboundary(h)
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

char *
mime_getfilename(h)
char *h;
{
	return mime_getparam("filename=", h);
}

/*
 * Get a line like "text/html html" and look if x matches the extension
 * The line must be terminated by a newline character
 */
static char *
mime_tline(x, l)
char *x, *l;
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
		n = (char*)smalloc(strlen(type) + 1);
		strcpy(n, type);
		return n;
	}
	return NULL;
}

/*
 * Check the given MIME type file for extension ext.
 */
static char *
mime_type(ext, filename)
char *ext, *filename;
{
	FILE *f;
	char line[LINESIZE];
	char *type = NULL;

	f = Fopen(filename, "r");
	if (f == NULL)
		return NULL;
	while (fgets(line, LINESIZE, f)) {
		type = mime_tline(ext, line);
		if (type != NULL)
			break;
	}
	Fclose(f);
	return type;
}

/*
 * Return the Content-Type matching the extension of name.
 */
char *
mime_filecontent(name)
char *name;
{
	char *ext, *content;

	ext = strrchr(name, '.');
	if (ext == NULL)
		return NULL;
	else
		ext++;
	if ((content = mime_type(ext, expand(mimetypes_user))) != NULL)
		return content;
	if ((content = mime_type(ext, mimetypes_world)) != NULL)
		return content;
	return NULL;
}

/*
 * Check file contents.
 */
int
mime_isclean(f)
FILE *f;
{
	int isclean = MIME_7BITTEXT;
	long initial_pos;
	int c;

	initial_pos = ftell(f);
	for (;;) {
		c = getc(f);
		if (c == EOF && (feof(f) || ferror(f))) {
			break;
		}
		if (c & 0200) {
			isclean = MIME_INTERTEXT;
			continue;
		}
		if ((c < 0x20 && (c != ' ' && c != '\n' && c != '\t'))
			|| c == 0177) {
			isclean = MIME_BINARY;
			break;
		}
	}
	clearerr(f);
	fseek(f, initial_pos, SEEK_SET);
	return isclean;
}

/*
 * Convert i to a hexadecimal character string and store it in b.
 * The caller has to ensure that the size of b is sufficient.
 */
char *
itohex(i, b)
unsigned i;
char *b;
{
	char *p, *q, c;
	
	p = b;
	while (i != 0) {
		*p++ = hextable[i % 16];
		i /= 16;
	}
	*p-- = '\0';
	q = b;
	while (p >= q) {
		c = *q;
		*q++ = *p;
		*p-- = c;
	}
	return b;
}

/*
 * Convert c to a hexadecimal character string and store it in hex.
 */
static char *
ctohex(c, hex)
unsigned char c;
char *hex;
{
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

/*
 * Write to a file converting to quoted-printable.
 * The mustquote function determines whether a character must be quoted.
 */
static size_t
mime_write_toqp(in, fo, mustquote)
struct str *in;
FILE *fo;
int (*mustquote)(unsigned char);
{
	char *p, *upper, *h, hex[3];
	int l;
	size_t sz;

	sz = in->l;
	upper = in->s + in->l;
	for (p = in->s, l = 0; p < upper; p++) {
		if (mustquote(*p) 
				|| (*(p + 1) == '\n' &&
					(*p == ' ' || *p == '\t'))) {
			sz += 2;
			fputc('=', fo);
			h = ctohex(*p, hex);
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

/*
 * Write to a stringstruct converting to quoted-printable.
 * The mustquote function determines whether a character must be quoted.
 */
static void
mime_str_toqp(in, out, mustquote)
struct str *in, *out;
int (*mustquote)(unsigned char);
{
	char *p, *q, *upper, *h;

	out->s = (char*)smalloc(in->l * 3 + 1);
	q = out->s;
	out->l = in->l;
	upper = in->s + in->l;
	for (p = in->s; p < upper; p++) {
		if (mustquote(*p) 
				|| (*(p + 1) == '\n' &&
					(*p == ' ' || *p == '\t'))) {
			out->l += 2;
			*q++ = '=';
			h = ctohex(*p, q);
			q += 2;
		} else {
			*q++ = *p;
		}
	}
	*q = '\0';
}

/*
 * Write to a stringstruct converting from quoted-printable.
 */
static void
mime_fromqp(in, out, todisplay, ishdr)
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
			if (todisplay && is_undisplayable(*q))
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

/*
 * Convert header fields from RFC 1522 format
 */
void
mime_fromhdr(in, out, todisplay)
struct str *in, *out;
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
					mime_fromb64(&cin, &cout,
							todisplay, 1);
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

/*
 * Convert header fields to RFC 1522 format and write to the file fo.
 */
static size_t
mime_write_tohdr(in, fo)
struct str *in;
FILE *fo;
{
	char *upper, *wbeg, *wend, *charset;
	struct str cin, cout;
	size_t sz, col = 0, wr, charsetlen;
	int mustquote,
		maxcol = 65 /* there is the header field's name, too */;

	sz = in->l;
	upper = in->s + in->l;
	charset = mime_getcharset(CONV_TOQP);
	charsetlen = strlen(charset);
	for (wbeg = in->s; wbeg < upper; wbeg = wend) {
		while (wbeg < upper && isspace(*wbeg)) {
			fputc(*wbeg++, fo);
			sz++, col++;
		}
		if (wbeg == upper)
			break;
		mustquote = 0;
		for (wend = wbeg; wend < upper && !isspace(*wend); wend++) {
			if (mustquote_hdr(*wend))
				mustquote = 1;
		}
		if (mustquote) {
			for (;;) {
				cin.s = wbeg;
				cin.l = wend - wbeg;
				mime_str_toqp(&cin, &cout, mustquote_hdr);
				if ((wr = cout.l + charsetlen + 7)
						< maxcol - col) {
					fprintf(fo, "=?%s?Q?", charset);
					fwrite(cout.s, sizeof(char),
							cout.l, fo);
					fwrite("?=", sizeof(char), 2, fo);
					sz += wr, col += wr;
					free(cout.s);
					break;
				} else {
					if (col > 0) {
						fprintf(fo, "\n ");
						sz += 2;
						col = 0;
						maxcol = 76;
					} else {
						wend -= 4;
					}
					free(cout.s);
				}
			}
		} else {
			if (wend - wbeg > maxcol - col) {
				fwrite("\n ", sizeof(char), 2, fo);
				sz += 2;
				col = 0;
				maxcol = 76;
			}
			wr = fwrite(wbeg, sizeof(*wbeg), wend - wbeg, fo);
			sz += wr, col += wr;
		}
	}
	return sz;
}

/*
 * fwrite while checking for displayability.
 */
static size_t
fwrite_tty(ptr, size, nmemb, f)
void *ptr;
size_t size, nmemb;
FILE *f;
{
	char *p, *upper;
	size_t sz;

	upper = (char*)ptr + nmemb / size;
	for (p = ptr, sz = 0; p < upper; p++, sz++) {
		if (is_undisplayable(*p)) {
			fputc('?', f);
		} else
			fputc(*p, f);
	}
	return sz;
}

/*
 * fwrite performing the given MIME conversion.
 */
size_t
mime_write(ptr, size, nmemb, f, convert, todisplay)
void *ptr;
size_t size, nmemb;
FILE *f;
{
	struct str in, out;
	size_t sz;
	int is_text = 0;

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
	case CONV_FROMB64_T:
		is_text = 1;
		/* FALL THROUGH */
	case CONV_FROMB64:
		mime_fromb64_b(&in, &out, todisplay, is_text, f);
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
