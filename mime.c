/*	$Id: mime.c,v 1.19 2000/09/29 04:03:29 gunnar Exp $	*/

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
#if 0
static char rcsid[]  = "@(#)$Id: mime.c,v 1.19 2000/09/29 04:03:29 gunnar Exp $";
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"
#include <errno.h>

/*
 * Mail -- a mail program
 *
 * MIME support functions.
 */

/*
 * You won't guess what these are for.
 */
const static char basetable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static char *mimetypes_world = "/etc/mime.types";
static char *mimetypes_user = "~/mime.types";
char *us_ascii = "us-ascii";

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
		&& (c < 0x20 || c > 126))
		return 1;
	return 0;
}

/*
 * Check if c must be quoted inside a quoting in a message's header.
 */
static int
mustquote_inhdrq(c)
unsigned char c;
{
	if (c != '\n'
		&& (c < 0x20 || c == '=' || c == '?' || c == '_' || c > 126))
		return 1;
	return 0;
}

/*
 * Replace non-printable characters in s with question marks.
 */
void
makeprint(s, l)
char *s;
size_t l;
{
#ifdef	HAVE_SETLOCALE
#ifdef	HAVE_MBSRTOWCS
	int i;
	wchar_t w[LINESIZE], *p;
	char *t;
	size_t wl;

#ifdef	__GLIBC_MINOR__
#if __GLIBC__ <= 2 && __GLIBC_MINOR__ <= 1
	/*
	 * This libc does not have correct utf-8 locales.
	 * Just a strange hack that will disappear one
	 * day, but we need it for testing.
	 */
	if (strcasecmp(gettcharset(), "utf-8") == 0)
		return;
#endif
#endif
	if (*s == '\0' || l == (size_t) 0)
		return;
#ifdef	MB_CUR_MAX
	if (MB_CUR_MAX > 1) {
#endif
		if (l >= LINESIZE)
			return;
		t = s;
		wl = mbsrtowcs(w, (const char **) &t, LINESIZE, NULL);
		if (wl == -1)
			return;
		for (p = w, i = 0; *p && i < wl; p++, i++) {
			if (!iswprint(*p) && *p != '\n' && *p != '\r'
					&& *p != '\b' && *p != '\t')
				*p = '?';
		}
		p = w;
		wcsrtombs(s, (const wchar_t **) &p, l + 1, NULL);
		return;
#ifdef	MB_CUR_MAX
	}
#else	/* !MB_CUR_MAX */
	/*NOTREACHED*/
#endif	/* !MB_CUR_MAX */
#endif	/* HAVE_MBSRTOWCS */
	for (; l > 0 && *s; s++, l--) {
		if (!isprint(*s & 0377) && *s != '\n' && *s != '\r'
				&& *s != '\b' && *s != '\t')
			*s = '?';
	}
#else	/* !HAVE_SETLOCALE */
	/*EMPTY*/ ;
#endif	/* !HAVE_SETLOCALE */
}

/*
 * Check if a name's address part contains invalid characters.
 */
int
mime_name_invalid(name)
char *name;
{
	char *addr, *p;
	int in_quote = 0, err = 0;

	addr = skin(name);

	if (addr == NULL || *addr == '\0')
		return 1;
	for (p = addr; *p != '\0'; p++) {
		if (*p == '\"') {
			in_quote = !in_quote;
		} else if (*p < 040 || ((unsigned char)*p) >= 0177) {
			err++;
		} else if (in_quote) {
			/*EMPTY*/;
		} else if (*p == '(' || *p == ')' || *p == '<' || *p == '>'
				|| *p == ',' || *p == ';' || *p == ':'
				|| *p == '\\' || *p == '[' || *p == ']') {
			err++;
		}
	}
	if (err) {
		fprintf(stderr, "%s contains invalid characters\n", addr);
	}
	return err;
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
 * glibc 2.1 provides such a function, but others ...
 */
char *
strcasestr(haystack, needle)
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

static char defcharset[] = "iso-8859-1";

/*
 * Get the character set dependant on the conversion.
 */
char *
getcharset(convert)
{
	char *charset;

	if (convert == CONV_7BIT) {
		/*
		 * This variable shall remain undocumented because
		 * only experts should change it.
		 */
		charset = value("charset7");
		if (charset == NULL) {
			charset = us_ascii;
		}
	} else {
		charset = value("charset");
		if (charset == NULL) {
			charset = defcharset;
		}
	}
	return charset;
}

/*
 * Get the setting of the terminal's character set.
 */
char *
gettcharset()
{
	char *t;

	t= value("ttycharset");
	if (t == NULL) {
		t = value("charset");
		if (t == NULL)
			t = defcharset;
	}
	return t;
}

#ifdef	HAVE_ICONV
/*
 * Convert a string, upper-casing the characters.
 */
void
strupcpy(dest, src)
char *dest;
const char *src;
{
	do {
		if (islower(*src & 0377))
			*dest++ = toupper(*src);
		else
			*dest++ = *src;
	} while (*src++);
}

/*
 * Strip dashes.
 */
void
stripdash(p)
char *p;
{
	char *q = p;

	do {
		*q = *p;
		if (*p != '-')
			q++;
	} while (*p++);
}

/*
 * An iconv_open wrapper that tries to convert between character set
 * naming conventions.
 */
iconv_t
iconv_open_ft(tocode, fromcode)
const char *tocode, *fromcode;
{
	iconv_t id;
	char *t, *f, *p;

	/*
	 * On Linux systems, this call may succeed.
	 */
	if ((id = iconv_open(tocode, fromcode)) != (iconv_t) -1)
		return id;
	/*
	 * Remove the "iso-" prefixes for Solaris.
	 */
	if (strncasecmp(tocode, "iso-", 4) == 0)
		tocode += 4;
	if (strncasecmp(fromcode, "iso-", 4) == 0)
		fromcode += 4;
	if (*tocode == '\0' || *fromcode == '\0')
		return (iconv_t) -1;
	if ((id = iconv_open(tocode, fromcode)) != (iconv_t) -1)
		return id;
	/*
	 * Solaris prefers upper-case charset names. Don't ask...
	 */
	t = (char*) salloc(strlen(tocode) + 1);
	strupcpy(t, tocode);
	f = (char*) salloc(strlen(fromcode) + 1);
	strupcpy(f, fromcode);
	if ((id = iconv_open(t, f)) != (iconv_t) -1)
		return id;
	/*
	 * Strip dashes for UnixWare.
	 */
	stripdash(t);
	stripdash(f);
	if ((id = iconv_open(t, f)) != (iconv_t) -1)
		return id;
	/*
	 * Add you vendor's sillynesses here.
	 */
	return id;
}

/*
 * Fault-tolerant iconv() function.
 */
size_t
iconv_ft(cd, inb, inbleft, outb, outbleft)
iconv_t cd;
const char **inb;
size_t *inbleft, *outbleft;
char **outb;
{
	size_t sz = 0;

	while ((sz = iconv(cd, inb, inbleft, outb, outbleft))
			== (size_t) -1
			&& (errno == EILSEQ || errno == EINVAL)) {
		if (*inbleft > 0) {
			(*inb)++;
			(*inbleft)--;
		} else {
			**outb = '\0';
			break;
		}
		if (*outbleft > 0) {
			*(*outb)++ = '?';
			(*outbleft)--;
		} else {
			**outb = '\0';
			break;
		}
	}
	return sz;
}
#endif	/* HAVE_ICONV */

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
	if (strncasecmp(p, "message/rfc822", 14) == 0)
		return MIME_822;
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

	p = strcasestr(h, param);
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
	r = (char*)salloc(sz + 1);
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
	return q;
}

char *
mime_getfilename(h)
char *h;
{
	return mime_getparam("filename=", h);
}

/*
 * Get a line like "text/html html" and look if x matches the extension.
 * The line must be terminated by a newline character.
 */
static char *
mime_tline(x, l)
char *x, *l;
{
	char *type, *n;
	int match = 0;

	if ((*l & 0200) || isalpha(*l) == 0)
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
		if ((c < 040 && (c != ' ' && c != '\n' && c != '\t'))
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
 * Print an error because of an invalid character sequence.
 */
static void
invalid_seq(c)
char c;
{
	/*fprintf(stderr, "iconv: cannot convert %c\n", c);*/
	/*EMPTY*/ ;
}

/*
 * Convert i to a baseX character string and store it in b.
 * The caller has to ensure that the size of b is sufficient.
 */
char *
itostr(base, i, b)
unsigned base, i;
char *b;
{
	char *p, *q, c;
	
	p = b;
	while (i != 0) {
		*p++ = basetable[i % base];
		i /= base;
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
	hex[1] = basetable[d];
	if (c > d)
		hex[0] = basetable[(c - d) / 16];
	else
		hex[0] = basetable[0];
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
			if (l >= 69) {
				sz += 2;
				fwrite("=\n", sizeof(char), 2, fo);
				l = 0;
			}
			sz += 2;
			fputc('=', fo);
			h = ctohex(*p, hex);
			fwrite(h, sizeof(char), 2, fo);
			l += 3;
		} else {
			if (*p == '\n')
				l = 0;
			else if (l >= 71) {
				sz += 2;
				fwrite("=\n", sizeof(char), 2, fo);
				l = 0;
			}
			fputc(*p, fo);
			l++;
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
mime_fromqp(in, out, ishdr)
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
			} while ((*p == ' ' || *p == '\t') && p < upper);
			if (p == upper)
				break;
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
mime_fromhdr(in, out, flags)
struct str *in, *out;
{
	char *p, *q, *upper, *cs, *cbeg, *tcs;
	struct str cin, cout;
	int convert;
	size_t maxstor;
#ifdef	HAVE_ICONV
	iconv_t fhicd = (iconv_t) -1;
#endif

	tcs = gettcharset();
	maxstor = 4 * in->l;
	out->s = (char *) smalloc(maxstor + 1);
	out->l = 0;
	upper = in->s + in->l;
	for (p = in->s, q = out->s; p < upper; p++) {
		if (*p == '=' && *(p + 1) == '?') {
			p += 2;
			cbeg = p;
			while (*p++ != '?');	/* strip charset */
			cs = (char *) salloc(p - cbeg);
			memcpy(cs, cbeg, p - cbeg - 1);
			cs[p - cbeg - 1] = '\0';
#ifdef	HAVE_ICONV
			if (fhicd != (iconv_t) -1)
				iconv_close(fhicd);
			if (strcmp(cs, tcs))
				fhicd = iconv_open_ft(tcs, cs);
			else
				fhicd = (iconv_t) -1;
#endif
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
					mime_fromb64(&cin, &cout, 1);
					break;
				case CONV_FROMQP:
					mime_fromqp(&cin, &cout, 1);
					break;
				default: abort();
			}
#ifdef	HAVE_ICONV
			if ((flags & TD_ICONV) && fhicd != (iconv_t) -1) {
				char *iptr, *mptr, *nptr, *uptr;
				size_t inleft, outleft;

				inleft = cout.l;
				outleft = maxstor - out->l;
				mptr = nptr = q;
				uptr = nptr + outleft;
				iptr = cout.s;
				iconv_ft(fhicd, (const char **) &iptr,
							&inleft, &nptr,
							&outleft);
				out->l += uptr - mptr - outleft;
				q += uptr - mptr - outleft;
			} else {
#endif
				memcpy(q, cout.s, cout.l);
				q += cout.l;
				out->l += cout.l;
#ifdef	HAVE_ICONV
			}
#endif
			free(cout.s);
		} else {
			*q++ = *p;
			out->l++;
		}
	}
	*q = '\0';
	if (flags & TD_ISPR)
		makeprint(out->s, out->l);
#ifdef	HAVE_ICONV
	if (fhicd != (iconv_t) -1)
		iconv_close(fhicd);
#endif
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
	size_t sz = 0, col = 0, wr, charsetlen;
	int mustquote,
		maxcol = 65 /* there is the header field's name, too */;

	upper = in->s + in->l;
	charset = getcharset(CONV_TOQP);
	charsetlen = strlen(charset);
	for (wbeg = in->s, mustquote = 0; wbeg < upper; wbeg++)
		if (mustquote_hdr(*wbeg))
			mustquote++;
	if (2 * mustquote > in->l) {
		/*
		 * Print the entire field in base64.
		 */
		for (wbeg = in->s; wbeg < upper; wbeg = wend) {
			wend = upper;
			cin.s = wbeg;
			for (;;) {
				cin.l = wend - wbeg;
				if (cin.l * 4/3 + 7 + charsetlen
						< maxcol - col) {
					fprintf(fo, "=?%s?B?", charset);
					wr = mime_write_tob64(&cin, fo, 1);
					fwrite("?=", sizeof (char), 2, fo);
					wr += 7 + charsetlen;
					sz += wr, col += wr;
					if (wend < upper) {
						fwrite("\n ", sizeof (char),
								2, fo);
						sz += 2;
						col = 0;
						maxcol = 76;
					}
					break;
				} else {
					if (col) {
						fprintf(fo, "\n ");
						sz += 2;
						col = 0;
						maxcol = 76;
					} else {
						wend -= 4;
					}
				}
			}
		}
	} else {
		/*
		 * Print the field word-wise in quoted-printable.
		 */
		for (wbeg = in->s; wbeg < upper; wbeg = wend) {
			while (wbeg < upper && isspace(*wbeg)) {
				fputc(*wbeg++, fo);
				sz++, col++;
			}
			if (wbeg == upper)
				break;
			mustquote = 0;
			for (wend = wbeg; wend < upper && !isspace(*wend);
					wend++) {
				if (mustquote_hdr(*wend))
					mustquote++;
			}
			if (mustquote) {
				for (;;) {
					cin.s = wbeg;
					cin.l = wend - wbeg;
					mime_str_toqp(&cin, &cout,
							mustquote_inhdrq);
					if ((wr = cout.l + charsetlen + 7)
							< maxcol - col) {
						fprintf(fo, "=?%s?Q?",
								charset);
						fwrite(cout.s, sizeof(char),
								cout.l, fo);
						fwrite("?=", sizeof(char),
								2, fo);
						sz += wr, col += wr;
						free(cout.s);
						break;
					} else {
						if (col) {
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
				if (col && wend - wbeg > maxcol - col) {
					fwrite("\n ", sizeof(char), 2, fo);
					sz += 2;
					col = 0;
					maxcol = 76;
				}
				wr = fwrite(wbeg, sizeof(*wbeg),
						wend - wbeg, fo);
				sz += wr, col += wr;
			}
		}
	}
	return sz;
}

/*
 * Write an address to a header field.
 */
static size_t
mime_write_tohdr_a(in, f)
struct str *in;
FILE *f;
{
	char *p, *q, *ip, *op;
	struct str cin;
	size_t sz = 0, isz, osz;
	char cbuf[LINESIZE];
#ifdef	HAVE_ICONV
	iconv_t fhicd = (iconv_t) -1;
#endif

	*(in->s + in->l) = '\0';
	if (strpbrk(in->s, "(<") == NULL)
		return fwrite(in->s, sizeof(char), in->l, f);
	if ((p = strchr(in->s, '<')) != NULL) {
		q = strchr(p, '>');
		if (q++ == NULL)
			return 0;
#ifdef	HAVE_ICONV
		if (fhicd == (iconv_t) -1) {
#endif
			cin.s = in->s;
			cin.l = p - in->s;
#ifdef	HAVE_ICONV
		} else {
			ip = in->s;
			isz = p - in->s;
			op = cbuf;
			osz = sizeof cbuf;
			if (iconv(fhicd, (const char **) &ip, &isz,
				&op, &osz) == (size_t) -1) {
				iconv_close(fhicd);
				return 0;
			}
			cin.s = cbuf;
			cin.l = sizeof cbuf - osz;
		}
#endif
		sz = mime_write_tohdr(&cin, f);
		sz += fwrite(p, sizeof(char), q - p, f);
#ifdef	HAVE_ICONV
		if (fhicd == (iconv_t) -1) {
#endif
			cin.s = q;
			cin.l = in->s + in->l - q;
#ifdef	HAVE_ICONV
		} else {
			ip = q;
			isz = in->s + in->l - q;
			op = cbuf;
			osz = sizeof cbuf;
			if (iconv(fhicd, (const char **) &ip, &isz,
				&op, &osz) == (size_t) -1)
				return 0;
			cin.s = cbuf;
			cin.l = sizeof cbuf - osz;
		}
#endif
		sz += mime_write_tohdr(&cin, f);
	} else if ((p = strchr(in->s, '(')) != NULL) {
		q = in->s;
		do {
			sz += fwrite(q, sizeof(char), p - q, f);
			q = strchr(p, ')');
			if (q++ == NULL) {
#ifdef	HAVE_ICONV
				if (fhicd != (iconv_t) -1)
					iconv_close(fhicd);
#endif
				return 0;
			}
#ifdef	HAVE_ICONV
			if (fhicd == (iconv_t) -1) {
#endif
				cin.s = p;
				cin.l = q - p;
#ifdef	HAVE_ICONV
			} else {
				ip = p;
				isz = q - p;
				op = cbuf;
				osz = sizeof cbuf;
				if (iconv(fhicd, (const char **) &ip, &isz,
					&op, &osz) == (size_t) -1) {
					iconv_close(fhicd);
					return 0;
				}
				cin.s = cbuf;
				cin.l = sizeof cbuf - osz;
			}
#endif
			sz += mime_write_tohdr(&cin, f);
			p = strchr(q, '(');
		} while (p != NULL);
		if (*q != '\0')
			sz += fwrite(q, sizeof(char), in->s + in->l - q, f);
	}
#ifdef	HAVE_ICONV
	if (fhicd != (iconv_t) -1)
		iconv_close(fhicd);
#endif
	return sz;
}

/*
 * fwrite whilst adding prefix, if present.
 */
static size_t
prefixwrite(ptr, size, nmemb, f, prefix, prefixlen)
void *ptr;
size_t size, nmemb, prefixlen;
FILE *f;
char *prefix;
{
	static FILE *lastf;
	static char lastc = '\n';
	size_t rsz, wsz = 0;
	char *p = ptr;

	if (nmemb == 0)
		return 0;
	if (prefix == NULL) {
		lastf = f;
		lastc = ((char *)ptr)[size * nmemb - 1];
		return fwrite(ptr, size, nmemb, f);
	}
	if (f != lastf || lastc == '\n') {
		if (*p == '\n' || *p == '\0')
			wsz += fwrite(prefix, sizeof *prefix, prefixlen, f);
		else {
			fputs(prefix, f);
			wsz += strlen(prefix);
		}
	}
	lastf = f;
	for (rsz = size * nmemb; rsz; rsz--, p++, wsz++) {
		fputc(*p, f);
		if (*p != '\n' || rsz == 1) {
			continue;
		}
		if (p[1] == '\n' || p[1] == '\0')
			wsz += fwrite(prefix, sizeof *prefix, prefixlen, f);
		else {
			fputs(prefix, f);
			wsz += strlen(prefix);
		}
	}
	lastc = p[-1];
	return wsz;
}

/*
 * fwrite while checking for displayability.
 */
static size_t
fwrite_td(ptr, size, nmemb, f, flags, prefix, prefixlen)
void *ptr;
char *prefix;
size_t size, nmemb, prefixlen;
FILE *f;
{
	char *upper;
	size_t sz, csize;
#ifdef	HAVE_ICONV
	char *iptr, *nptr;
	size_t inleft, outleft;
	char mptr[LINESIZE * 4];
#else
	char mptr[LINESIZE];
#endif

	csize = size * nmemb / sizeof (char);
	if (flags == 0 || csize >= sizeof mptr)
		return prefixwrite(ptr, size, nmemb, f, prefix, prefixlen);
#ifdef	HAVE_ICONV
	if ((flags & TD_ICONV) && iconvd != (iconv_t) -1) {
		inleft = csize;
		outleft = sizeof mptr;
		nptr = mptr;
		iptr = ptr;
		iconv_ft(iconvd, (const char **) &iptr, &inleft,
				&nptr, &outleft);
		nmemb = sizeof mptr - outleft;
		size = sizeof(char);
		ptr = mptr;
		csize = size * nmemb / sizeof (char);
	} else
#endif
	{
		memcpy(mptr, ptr, nmemb / size);
	}
	upper = (char*) mptr + csize;
	*upper = '\0';
	if (flags & TD_ISPR)
		makeprint(mptr, upper - (char *) mptr);
	sz = prefixwrite(mptr, sizeof(char), upper - (char *) mptr, f,
			prefix, prefixlen);
	return sz;
}

/*
 * fwrite performing the given MIME conversion.
 */
size_t
mime_write(ptr, size, nmemb, f, convert, dflags, prefix, prefixlen)
void *ptr;
size_t size, nmemb, prefixlen;
char *prefix;
FILE *f;
{
	struct str in, out;
	size_t sz, csize;
	int is_text = 0;
#ifdef	HAVE_ICONV
	char mptr[LINESIZE * 4];
	char *iptr, *nptr;
	size_t inleft, outleft;
#endif

	csize = size * nmemb / sizeof (char);
#ifdef	HAVE_ICONV
	if (csize < sizeof mptr && (dflags & TD_ICONV)
			&& iconvd != (iconv_t) -1
			&& (convert == CONV_TOQP || convert == CONV_TOB64
				|| convert == CONV_TOHDR)) {

		inleft = csize;
		outleft = sizeof mptr;
		nptr = mptr;
		iptr = ptr;
		if (iconv(iconvd, (const char **) &iptr, &inleft,
				&nptr, &outleft) != (size_t) -1) {
			in.l = sizeof mptr - outleft;
			in.s = mptr;
		} else {
			if (errno == EILSEQ || errno == EINVAL)
				invalid_seq(*iptr);
			return 0;
		}
	} else {
#endif
		in.s = ptr;
		in.l = csize;
#ifdef	HAVE_ICONV
	}
#endif
	switch (convert) {
	case CONV_FROMQP:
		mime_fromqp(&in, &out, 0);
		sz = fwrite_td(out.s, sizeof(char), out.l, f, dflags,
				prefix, prefixlen);
		free(out.s);
		break;
	case CONV_TOQP:
		sz = mime_write_toqp(&in, f, mustquote_body);
		break;
	case CONV_FROMB64_T:
		is_text = 1;
		/*FALLTHROUGH*/
	case CONV_FROMB64:
		mime_fromb64_b(&in, &out, is_text, f);
		sz = fwrite_td(out.s, sizeof(char), out.l, f, dflags,
				prefix, prefixlen);
		free(out.s);
		break;
	case CONV_TOB64:
		sz = mime_write_tob64(&in, f, 0);
		break;
	case CONV_FROMHDR:
		mime_fromhdr(&in, &out, TD_ISPR|TD_ICONV);
		sz = fwrite_td(out.s, sizeof(char), out.l, f, TD_NONE,
				prefix, prefixlen);
		free(out.s);
		break;
	case CONV_TOHDR:
		sz = mime_write_tohdr(&in, f);
		break;
	case CONV_TOHDR_A:
		sz = mime_write_tohdr_a(&in, f);
		break;
	default:
		sz = fwrite_td(in.s, sizeof(char), in.l, f, dflags,
				prefix, prefixlen);
	}
	return sz;
}
