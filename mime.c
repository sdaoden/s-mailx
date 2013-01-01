/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 Steffen "Daode" Nurpmeso.
 */
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

#include "rcv.h"

#include <ctype.h>
#include <errno.h>
#ifdef HAVE_WCTYPE_H
# include <wctype.h>
#endif

#include "extern.h"

/*
 * Mail -- a mail program
 *
 * MIME support functions.
 */

struct mtnode {
	struct mtnode	*mt_next;
	size_t		mt_mtlen;	/* Length of MIME type string */
	char		mt_line[VFIELD_SIZE(8)];
};

static char const	basetable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ",
			*const _mt_sources[] = {
		MIME_TYPES_USR, MIME_TYPES_SYS, NULL
	},
			*const _mt_bltin[] = {
#include "mime_types.h"
		NULL
	};

struct mtnode	*_mt_list;

/* Initialize MIME type list */
static void		_mt_init(void);
static void		__mt_add_line(char const *line, struct mtnode **tail);

static int mustquote_body(int c);
static int mustquote_hdr(const char *cp, int wordstart, int wordend);
static int mustquote_inhdrq(int c);
static size_t	delctrl(char *cp, size_t sz);
static char const	*getcharset(int isclean);
static int has_highbit(register const char *s);
static int is_this_enc(const char *line, const char *encoding);
static enum mimeclean mime_isclean(FILE *f);
static enum conversion gettextconversion(void);
static char *ctohex(unsigned char c, char *hex);
static size_t mime_write_toqp(struct str *in, FILE *fo, int (*mustquote)(int));
static void mime_str_toqp(struct str *in, struct str *out,
		int (*mustquote)(int), int inhdr);
static void mime_fromqp(struct str *in, struct str *out, int ishdr);
static size_t mime_write_tohdr(struct str *in, FILE *fo);
static size_t convhdra(char *str, size_t len, FILE *fp);
static size_t mime_write_tohdr_a(struct str *in, FILE *f);
static void addstr(char **buf, size_t *sz, size_t *pos,
		char const *str, size_t len);
static void addconv(char **buf, size_t *sz, size_t *pos,
		char const *str, size_t len);
static size_t fwrite_td(void *ptr, size_t size, size_t nmemb, FILE *f,
		enum tdflags flags, char *prefix, size_t prefixlen);

static void
_mt_init(void)
{
	struct mtnode *tail = NULL;
	char *line = NULL;
	size_t linesize = 0;
	char const *const*srcs;
	FILE *fp;

	for (srcs = _mt_sources; *srcs != NULL; ++srcs) {
		char const *fn = file_expand(*srcs);
		if (fn == NULL)
			continue;
		if ((fp = Fopen(fn, "r")) == NULL) {
			/*fprintf(stderr, tr(176, "Cannot open %s\n"), fn);*/
			continue;
		}
		while (fgetline(&line, &linesize, NULL, NULL, fp, 0))
			__mt_add_line(line, &tail);
		Fclose(fp);
	}
	if (line != NULL)
		free(line);

	for (srcs = _mt_bltin; *srcs != NULL; ++srcs)
		__mt_add_line(*srcs, &tail);
}

static void
__mt_add_line(char const *line, struct mtnode **tail) /* XXX diag? dups!*/
{
	char const *type;
	size_t tlen, elen;
	struct mtnode *mtn;

	if (! alphachar(*line))
		goto jleave;

	type = line;
	while (blankchar(*line) == 0 && *line != '\0')
		++line;
	if (*line == '\0')
		goto jleave;
	tlen = (size_t)(line - type);

	while (blankchar(*line) != 0 && *line != '\0')
		++line;
	if (*line == '\0')
		goto jleave;

	elen = strlen(line);
	if (line[elen - 1] == '\n' && line[--elen] == '\0')
		goto jleave;

	mtn = smalloc(sizeof(struct mtnode) +
			VFIELD_SIZEOF(struct mtnode, mt_line) + tlen + 1 +
			elen + 1);
	if (*tail != NULL)
		(*tail)->mt_next = mtn;
	else
		_mt_list = mtn;
	*tail = mtn;
	mtn->mt_next = NULL;
	mtn->mt_mtlen = tlen;
	memcpy(mtn->mt_line, type, tlen);
	mtn->mt_line[tlen] = '\0';
	++tlen;
	memcpy(mtn->mt_line + tlen, line, elen);
	tlen += elen;
	mtn->mt_line[tlen] = '\0';
jleave:	;
}

/*
 * Check if c must be quoted inside a message's body.
 */
static int 
mustquote_body(int c)
{
	if (c != '\n' && (c < 040 || c == '=' || c >= 0177))
		return 1;
	return 0;
}

/*
 * Check if c must be quoted inside a message's header.
 */
static int 
mustquote_hdr(const char *cp, int wordstart, int wordend)
{
	int	c = *cp & 0377;

	if (c != '\n' && (c < 040 || c >= 0177))
		return 1;
	if (wordstart && cp[0] == '=' && cp[1] == '?')
		return 1;
	if (cp[0] == '?' && cp[1] == '=' &&
			(wordend || cp[2] == '\0' || whitechar(cp[2]&0377)))
		return 1;
	return 0;
}

/*
 * Check if c must be quoted inside a quoting in a message's header.
 */
static int 
mustquote_inhdrq(int c)
{
	if (c != '\n'
		&& (c <= 040 || c == '=' || c == '?' || c == '_' || c >= 0177))
		return 1;
	return 0;
}

static size_t
delctrl(char *cp, size_t sz)
{
	size_t	x = 0, y = 0;

	while (x < sz) {
		if (!cntrlchar(cp[x]&0377))
			cp[y++] = cp[x];
		x++;
	}
	return y;
}

/*
 * Get the character set dependant on the conversion.
 */
static char const *
getcharset(int isclean)
{
	char const *charset = charset7;

	if (isclean & (MIME_CTRLCHAR|MIME_HASNUL))
		charset = NULL;
	else if (isclean & MIME_HIGHBIT) {
		charset = (wantcharset && wantcharset != (char *)-1) ?
			wantcharset : value("charset");
		if (charset == NULL)
			charset = defcharset;
	}
	return (charset);
}

/*
 * Get the setting of the terminal's character set.
 */
char const *
gettcharset(void)
{
	char const *t;

	if ((t = value("ttycharset")) == NULL)
		if ((t = value("charset")) == NULL)
			t = defcharset;
	return (t);
}

static int 
has_highbit(const char *s)
{
	if (s) {
		do
			if (*s & 0200)
				return 1;
		while (*s++ != '\0');
	}
	return 0;
}

static int
name_highbit(struct name *np)
{
	while (np) {
		if (has_highbit(np->n_name) || has_highbit(np->n_fullname))
			return 1;
		np = np->n_flink;
	}
	return 0;
}

char const *
need_hdrconv(struct header *hp, enum gfield w)
{
	char const *ret = NULL;

	if (w & GIDENT) {
		if (hp->h_from != NULL) {
			if (name_highbit(hp->h_from))
				goto jneeds;
		} else if (has_highbit(myaddrs(NULL)))
			goto jneeds;
		if (hp->h_organization) {
			if (has_highbit(hp->h_organization))
				goto jneeds;
		} else if (has_highbit(value("ORGANIZATION")))
			goto jneeds;
		if (hp->h_replyto) {
			if (name_highbit(hp->h_replyto))
				goto jneeds;
		} else if (has_highbit(value("replyto")))
			goto jneeds;
		if (hp->h_sender) {
			if (name_highbit(hp->h_sender))
				goto jneeds;
		} else if (has_highbit(value("sender")))
			goto jneeds;
	}
	if (w & GTO && name_highbit(hp->h_to))
		goto jneeds;
	if (w & GCC && name_highbit(hp->h_cc))
		goto jneeds;
	if (w & GBCC && name_highbit(hp->h_bcc))
		goto jneeds;
	if (w & GSUBJECT && has_highbit(hp->h_subject))
jneeds:		ret = getcharset(MIME_HIGHBIT);
	return (ret);
}


static int
is_this_enc(const char *line, const char *encoding)
{
	int quoted = 0, c;

	if (*line == '"')
		quoted = 1, line++;
	while (*line && *encoding)
		if (c = *line++, lowerconv(c) != *encoding++)
			return 0;
	if (quoted && *line == '"')
		return 1;
	if (*line == '\0' || whitechar(*line & 0377))
		return 1;
	return 0;
}

/*
 * Get the mime encoding from a Content-Transfer-Encoding header field.
 */
enum mimeenc 
mime_getenc(char *p)
{
	if (is_this_enc(p, "7bit"))
		return MIME_7B;
	if (is_this_enc(p, "8bit"))
		return MIME_8B;
	if (is_this_enc(p, "base64"))
		return MIME_B64;
	if (is_this_enc(p, "binary"))
		return MIME_BIN;
	if (is_this_enc(p, "quoted-printable"))
		return MIME_QP;
	return MIME_NONE;
}

/*
 * Get a mime style parameter from a header line.
 */
char *
mime_getparam(char *param, char *h)
{
	char *p = h, *q, *r;
	int c;
	size_t sz;

	sz = strlen(param);
	if (!whitechar(*p & 0377)) {
		c = '\0';
		while (*p && (*p != ';' || c == '\\')) {
			c = c == '\\' ? '\0' : *p;
			p++;
		}
		if (*p++ == '\0')
			return NULL;
	}
	for (;;) {
		while (whitechar(*p & 0377))
			p++;
		if (ascncasecmp(p, param, sz) == 0) {
			p += sz;
			while (whitechar(*p & 0377))
				p++;
			if (*p++ == '=')
				break;
		}
		c = '\0';
		while (*p && (*p != ';' || c == '\\')) {
			if (*p == '"' && c != '\\') {
				p++;
				while (*p && (*p != '"' || c == '\\')) {
					c = c == '\\' ? '\0' : *p;
					p++;
				}
				p++;
			} else {
				c = c == '\\' ? '\0' : *p;
				p++;
			}
		}
		if (*p++ == '\0')
			return NULL;
	}
	while (whitechar(*p & 0377))
		p++;
	q = p;
	c = '\0';
	if (*p == '"') {
		p++;
		if ((q = strchr(p, '"')) == NULL)
			return NULL;
	} else {
		q = p;
		while (*q && !whitechar(*q & 0377) && *q != ';')
			q++;
	}
	sz = q - p;
	r = salloc(q - p + 1);
	memcpy(r, p, sz);
	*(r + sz) = '\0';
	return r;
}

char *
mime_get_boundary(char *h, size_t *len)
{
	char *q = NULL, *p;
	size_t sz;

	if ((p = mime_getparam("boundary", h)) != NULL) {
		sz = strlen(p);
		if (len != NULL)
			*len = sz + 2;
		q = salloc(sz + 3);
		q[0] = q[1] = '-';
		memcpy(q + 2, p, sz);
		*(q + sz + 2) = '\0';
	}
	return (q);
}


/*
 * Check file contents.
 */
static enum mimeclean
mime_isclean(FILE *f)
{
	long initial_pos;
	unsigned curlen = 1, maxlen = 0, limit = 950;
	enum mimeclean isclean = 0;
	char	*cp;
	int c = EOF, lastc;

	initial_pos = ftell(f);
	do {
		lastc = c;
		c = getc(f);
		curlen++;
		if (c == '\n' || c == EOF) {
			/*
			 * RFC 821 imposes a maximum line length of 1000
			 * characters including the terminating CRLF
			 * sequence. The configurable limit must not
			 * exceed that including a safety zone.
			 */
			if (curlen > maxlen)
				maxlen = curlen;
			curlen = 1;
		} else if (c & 0200) {
			isclean |= MIME_HIGHBIT;
		} else if (c == '\0') {
			isclean |= MIME_HASNUL;
			break;
		} else if ((c < 040 && (c != '\t' && c != '\f')) || c == 0177) {
			isclean |= MIME_CTRLCHAR;
		}
	} while (c != EOF);
	if (lastc != '\n')
		isclean |= MIME_NOTERMNL;
	clearerr(f);
	fseek(f, initial_pos, SEEK_SET);
	if ((cp = value("maximum-unencoded-line-length")) != NULL)
		limit = (unsigned)atoi(cp);
	if (limit > 950)
		limit = 950;
	if (maxlen > limit)
		isclean |= MIME_LONGLINES;
	return isclean;
}

/*
 * Get the conversion that matches the encoding specified in the environment.
 */
static enum conversion
gettextconversion(void)
{
	char *p;
	int convert;

	if ((p = value("encoding")) == NULL)
		return CONV_8BIT;
	if (strcmp(p, "quoted-printable") == 0)
		convert = CONV_TOQP;
	else if (strcmp(p, "8bit") == 0)
		convert = CONV_8BIT;
	else {
		fprintf(stderr, tr(177,
			"Warning: invalid encoding %s, using 8bit\n"), p);
		convert = CONV_8BIT;
	}
	return convert;
}

/*TODO Dobson: be037047c, contenttype==NULL||"text"==NULL control flow! */
int
get_mime_convert(FILE *fp, char **contenttype, char const **charset,
		enum mimeclean *isclean, int dosign)
{
	int convert;

	*isclean = mime_isclean(fp);
	if (*isclean & MIME_HASNUL ||
			(*contenttype &&
			ascncasecmp(*contenttype, "text/", 5))) {
		convert = CONV_TOB64;
		if (*contenttype == NULL ||
				ascncasecmp(*contenttype, "text/", 5) == 0)
			*contenttype = "application/octet-stream";
		*charset = NULL;
	} else if (*isclean & (MIME_LONGLINES|MIME_CTRLCHAR|MIME_NOTERMNL) ||
			dosign)
		convert = CONV_TOQP;
	else if (*isclean & MIME_HIGHBIT)
		convert = gettextconversion();
	else
		convert = CONV_7BIT;
	if (*contenttype == NULL ||
			ascncasecmp(*contenttype, "text/", 5) == 0) {
		*charset = getcharset(*isclean);
		if (wantcharset == (char *)-1) {
			*contenttype = "application/octet-stream";
			*charset = NULL;
		} if (*isclean & MIME_CTRLCHAR) {
			convert = CONV_TOB64;
			/*
			 * RFC 2046 forbids control characters other than
			 * ^I or ^L in text/plain bodies. However, some
			 * obscure character sets actually contain these
			 * characters, so the content type can be set.
			 */
			if ((*contenttype = value("contenttype-cntrl")) == NULL)
				*contenttype = "application/octet-stream";
		} else if (*contenttype == NULL)
			*contenttype = "text/plain";
	}
	return convert;
}

enum mimecontent
mime_classify_content_of_part(struct mimepart const *mip)
{
	enum mimecontent mc = MIME_UNKNOWN;
	char const *ct = mip->m_ct_type_plain;

	if (asccasecmp(ct, "application/octet-stream") == 0 &&
			mip->m_filename != NULL &&
			value("mime-counter-evidence")) {
		ct = mime_classify_content_type_by_fileext(mip->m_filename);
		if (ct == NULL)
			/* TODO how about let *mime-counter-evidence* have
			 * TODO a value, and if set, saving the attachment in
			 * TODO a temporary file that mime_classify_file() can
			 * TODO examine, and using MIME_TEXT if that gives us
			 * TODO something that seems to be human readable?! */
			goto jleave;
	}
	if (strchr(ct, '/') == NULL) /* For compatibility with non-MIME */
		mc = MIME_TEXT;
	else if (asccasecmp(ct, "text/plain") == 0)
		mc = MIME_TEXT_PLAIN;
	else if (asccasecmp(ct, "text/html") == 0)
		mc = MIME_TEXT_HTML;
	else if (ascncasecmp(ct, "text/", 5) == 0)
		mc = MIME_TEXT;
	else if (asccasecmp(ct, "message/rfc822") == 0)
		mc = MIME_822;
	else if (ascncasecmp(ct, "message/", 8) == 0)
		mc = MIME_MESSAGE;
	else if (asccasecmp(ct, "multipart/alternative") == 0)
		mc = MIME_ALTERNATIVE;
	else if (asccasecmp(ct, "multipart/digest") == 0)
		mc = MIME_DIGEST;
	else if (ascncasecmp(ct, "multipart/", 10) == 0)
		mc = MIME_MULTI;
	else if (asccasecmp(ct, "application/x-pkcs7-mime") == 0 ||
			asccasecmp(ct, "application/pkcs7-mime") == 0)
		mc = MIME_PKCS7;
jleave:
	return (mc);
}

char *
mime_classify_content_type_by_fileext(char const *name)
{
	char *content = NULL;
	struct mtnode *mtn;
	size_t nlen;

	if ((name = strrchr(name, '.')) == NULL || *++name == '\0')
		goto jleave;

	if (_mt_list == NULL)
		_mt_init();

	nlen = strlen(name);
	for (mtn = _mt_list; mtn != NULL; mtn = mtn->mt_next) {
		char const *ext = mtn->mt_line + mtn->mt_mtlen + 1,
			*cp = ext;
		do {
			while (! whitechar(*cp) && *cp != '\0')
				++cp;
			/* Better to do case-insensitive comparison on
			 * extension, since the RFC doesn't specify case of
			 * attribute values? */
			if (nlen == (size_t)(cp - ext) &&
					ascncasecmp(name, ext, nlen) == 0) {
				content = savestrbuf(mtn->mt_line,
						mtn->mt_mtlen);
				goto jleave;
			}
			while (whitechar(*cp) && *cp != '\0')
				++cp;
			ext = cp;
		} while (*ext != '\0');
	}
jleave:
	return (content);
}

int
cmimetypes(void *v)
{
	char **argv = v;
	struct mtnode *mtn;

	if (*argv == NULL)
		goto jlist;
	if (argv[1] != NULL)
		goto jerr;
	if (asccasecmp(*argv, "show") == 0)
		goto jlist;
	if (asccasecmp(*argv, "clear") == 0)
		goto jclear;
jerr:
	fprintf(stderr, "Synopsis: mimetypes: %s\n", tr(418,
		"Either <show> (default) or <clear> the mime.types cache"));
	v = NULL;
jleave:
	return (v == NULL ? STOP : OKAY);

jlist:	{
	FILE *fp;
	char *cp;
	size_t l;

	if (_mt_list == NULL)
		_mt_init();

	if ((fp = Ftemp(&cp, "Ra", "w+", 0600, 1)) == NULL) {
		perror("tmpfile");
		v = NULL;
		goto jleave;
	}
	rm(cp);
	Ftfree(&cp);

	for (l = 0, mtn = _mt_list; mtn != NULL; ++l, mtn = mtn->mt_next)
		fprintf(fp, "%s\t%s\n", mtn->mt_line,
			mtn->mt_line + mtn->mt_mtlen + 1);

	page_or_print(fp, l);
	Fclose(fp);
	}
	goto jleave;

jclear:
	while ((mtn = _mt_list) != NULL) {
		_mt_list = mtn->mt_next;
		free(mtn);
	}
	goto jleave;
}

/*
 * Convert c to a hexadecimal character string and store it in hex.
 */
static char *
ctohex(unsigned char c, char *hex)
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
mime_write_toqp(struct str *in, FILE *fo, int (*mustquote)(int))
{
	char *p, *upper, *h, hex[3];
	int l;
	size_t sz;

	sz = in->l;
	upper = in->s + in->l;
	for (p = in->s, l = 0; p < upper; p++) {
		if (mustquote(*p) ||
				(p < upper - 1 && p[1] == '\n' &&
					blankchar(*p)) ||
				(p < upper - 4 && l == 0 &&
					*p == 'F' && p[1] == 'r' &&
					p[2] == 'o' && p[3] == 'm') ||
				(*p == '.' && l == 0 && p < upper - 1 &&
					p[1] == '\n')) {
			if (l >= 69) {
				sz += 2;
				fwrite("=\n", sizeof (char), 2, fo);
				l = 0;
			}
			sz += 2;
			putc('=', fo);
			h = ctohex((unsigned char)*p, hex);
			fwrite(h, sizeof *h, 2, fo);
			l += 3;
		} else {
			if (*p == '\n')
				l = 0;
			else if (l >= 71) {
				sz += 2;
				fwrite("=\n", sizeof (char), 2, fo);
				l = 0;
			}
			putc(*p, fo);
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
mime_str_toqp(struct str *in, struct str *out, int (*mustquote)(int), int inhdr)
{
	char *p, *q, *upper;

	out->s = smalloc(in->l * 3 + 1);
	q = out->s;
	out->l = in->l;
	upper = in->s + in->l;
	for (p = in->s; p < upper; p++) {
		if (mustquote(*p&0377) || (p+1 < upper && *(p + 1) == '\n' &&
				blankchar(*p & 0377))) {
			if (inhdr && *p == ' ') {
				*q++ = '_';
			} else {
				out->l += 2;
				*q++ = '=';
				ctohex((unsigned char)*p, q);
				q += 2;
			}
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
mime_fromqp(struct str *in, struct str *out, int ishdr)
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
			} while (blankchar(*p & 0377) && p < upper);
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
		} else if (ishdr && *p == '_')
			*q++ = ' ';
		else
			*q++ = *p;
	}
	return;
}

#define	mime_fromhdr_inc(inc) { \
		size_t diff = q - out->s; \
		out->s = srealloc(out->s, (maxstor += inc) + 1); \
		q = &(out->s)[diff]; \
	}
/*
 * Convert header fields from RFC 1522 format TODO no error handling at all
 */
void 
mime_fromhdr(struct str const *in, struct str *out, enum tdflags flags)
{
	struct str crest, cin, cout;
	char *p, *q, *op, *upper, *cs, *cbeg, *lastwordend = NULL;
	char const *tcs;
	int convert;
	size_t maxstor, lastoutl = 0;
#ifdef	HAVE_ICONV
	iconv_t fhicd = (iconv_t)-1;
#endif

	crest.s = NULL;
	crest.l = 0;
	tcs = gettcharset();
	maxstor = in->l;
	out->s = smalloc(maxstor + 1);
	out->l = 0;
	upper = in->s + in->l;
	for (p = in->s, q = out->s; p < upper; p++) {
		op = p;
		if (*p == '=' && *(p + 1) == '?') {
			p += 2;
			cbeg = p;
			while (p < upper && *p != '?')
				p++;	/* strip charset */
			if (p >= upper)
				goto notmime;
			cs = salloc(++p - cbeg);
			memcpy(cs, cbeg, p - cbeg - 1);
			cs[p - cbeg - 1] = '\0';
#ifdef	HAVE_ICONV
			if (fhicd != (iconv_t)-1)
				iconv_close(fhicd);
			if (strcmp(cs, tcs))
				fhicd = iconv_open_ft(tcs, cs);
			else
				fhicd = (iconv_t)-1;
#endif
			switch (*p) {
			case 'B': case 'b':
				convert = CONV_FROMB64;
				break;
			case 'Q': case 'q':
				convert = CONV_FROMQP;
				break;
			default:	/* invalid, ignore */
				goto notmime;
			}
			if (*++p != '?')
				goto notmime;
			cin.s = ++p;
			cin.l = 1;
			for (;;) {
				if (p == upper)
					goto fromhdr_end;
				if (*p++ == '?' && *p == '=')
					break;
				cin.l++;
			}
			cin.l--;
			switch (convert) {
				case CONV_FROMB64:
					crest.l = 0;
					cout.s = NULL;
					(void)b64_decode(&cout, &cin, 0,
						&crest);
					b64_decode_join(&cout, &crest);
					break;
				case CONV_FROMQP:
					mime_fromqp(&cin, &cout, 1);
					break;
				default:
					break;
			}
			if (lastwordend) {
				q = lastwordend;
				out->l = lastoutl;
			}
#ifdef	HAVE_ICONV
			if ((flags & TD_ICONV) && fhicd != (iconv_t)-1) {
				char *iptr, *mptr, *nptr, *uptr;
				size_t inleft, outleft;

			again:	inleft = cout.l;
				outleft = maxstor - out->l;
				mptr = nptr = q;
				uptr = nptr + outleft;
				iptr = cout.s;
				if (iconv_ft(fhicd, (char const**)&iptr,&inleft,
					&nptr, &outleft, 1) == (size_t)-1 &&
						errno == E2BIG) {
					iconv_ft(fhicd, NULL, NULL, NULL, NULL,
						0);
					mime_fromhdr_inc(inleft);
					goto again;
				}
				/*
				 * For state-dependent encodings,
				 * reset the state here, assuming
				 * that states are restricted to
				 * single encoded-word parts.
				 */
				while (iconv_ft(fhicd, NULL, NULL,
					&nptr, &outleft, 0) == (size_t)-1 &&
						errno == E2BIG)
					mime_fromhdr_inc(16);
				out->l += uptr - mptr - outleft;
				q += uptr - mptr - outleft;
			} else {
#endif
				while (cout.l > maxstor - out->l)
					mime_fromhdr_inc(cout.l -
							(maxstor - out->l));
				memcpy(q, cout.s, cout.l);
				q += cout.l;
				out->l += cout.l;
#ifdef	HAVE_ICONV
			}
#endif
			free(cout.s);
			lastwordend = q;
			lastoutl = out->l;
		} else {
notmime:
			p = op;
			while (out->l >= maxstor)
				mime_fromhdr_inc(16);
			*q++ = *p;
			out->l++;
			if (!blankchar(*p&0377))
				lastwordend = NULL;
		}
	}
fromhdr_end:
	*q = '\0';
	if (crest.s != NULL)
		free(crest.s);
	if (flags & TD_ISPR) {
		makeprint(out, &crest);
		free(out->s);
		*out = crest;
	}
	if (flags & TD_DELCTRL)
		out->l = delctrl(out->s, out->l);
#ifdef	HAVE_ICONV
	if (fhicd != (iconv_t)-1)
		iconv_close(fhicd);
#endif
	return;
}

/*
 * Convert header fields to RFC 1522 format and write to the file fo.
 */
static size_t
mime_write_tohdr(struct str *in, FILE *fo)
{
	char buf[B64_LINESIZE],
		*upper, *wbeg, *wend, *lastwordend = NULL, *lastspc, b;
	char const *charset;
	struct str cin, cout;
	size_t sz = 0, col = 0, wr, charsetlen;
	int quoteany, mustquote, broken,
		maxcol = 65 /* there is the header field's name, too */;

	charset = getcharset(MIME_HIGHBIT);
	charsetlen = strlen(charset);
	charsetlen = smax(charsetlen, sizeof(CHARSET7) - 1);
	upper = in->s + in->l;

	b = 0;
	for (wbeg = in->s, quoteany = 0; wbeg < upper; ++wbeg) {
		b |= *wbeg;
		if (mustquote_hdr(wbeg, wbeg == in->s, wbeg == &upper[-1]))
			quoteany++;
	}

	if (2u * quoteany > in->l) {
		/*
		 * Print the entire field in base64.
		 */
		for (wbeg = in->s; wbeg < upper; wbeg = wend) {
			wend = upper;
			cin.s = wbeg;
			for (;;) { /* TODO optimize the -=4 case (...) */
				cin.l = wend - wbeg;
				if (cin.l * 4/3 + 7 + charsetlen
						< maxcol - col) {
					cout.s = buf;
					cout.l = sizeof buf;
					wr = fprintf(fo, "=?%s?B?%s?=",
						b&0200 ? charset : charset7,
						b64_encode(&cout, &cin, B64_BUF
							)->s);
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
					} else
						wend -= 4;
				}
			}
		}
	} else {
		/*
		 * Print the field word-wise in quoted-printable.
		 */
		broken = 0;
		for (wbeg = in->s; wbeg < upper; wbeg = wend) {
			lastspc = NULL;
			while (wbeg < upper && whitechar(*wbeg & 0377)) {
				lastspc = lastspc ? lastspc : wbeg;
				wbeg++;
				col++;
				broken = 0;
			}
			if (wbeg == upper) {
				if (lastspc)
					while (lastspc < wbeg) {
						putc(*lastspc&0377, fo);
							lastspc++,
							sz++;
						}
				break;
			}
			mustquote = 0;
			b = 0;
			for (wend = wbeg;
				wend < upper && !whitechar(*wend & 0377);
					wend++) {
				b |= *wend;
				if (mustquote_hdr(wend, wend == wbeg,
							wbeg == &upper[-1]))
					mustquote++;
			}
			if (mustquote || broken ||
					((wend - wbeg) >= 74 && quoteany)) {
				for (;;) {
					cin.s = lastwordend ? lastwordend :
						wbeg;
					cin.l = wend - cin.s;
					mime_str_toqp(&cin, &cout,
							mustquote_inhdrq, 1);
					if ((wr = cout.l + charsetlen + 7)
							< maxcol - col) {
						if (lastspc)
							while (lastspc < wbeg) {
								putc(*lastspc
									&0377,
									fo);
								lastspc++,
								sz++;
							}
						fprintf(fo, "=?%s?Q?", b&0200 ?
							charset : charset7);
						fwrite(cout.s, sizeof *cout.s,
								cout.l, fo);
						fwrite("?=", 1, 2, fo);
						sz += wr, col += wr;
						free(cout.s);
						break;
					} else {
						broken = 1;
						if (col) {
							putc('\n', fo);
							sz++;
							col = 0;
							maxcol = 76;
							if (lastspc == NULL) {
								putc(' ', fo);
								sz++;
								maxcol--;
							} else
								maxcol -= wbeg -
									lastspc;
						} else {
							wend -= 4;
						}
						free(cout.s);
					}
				}
				lastwordend = wend;
			} else {
				if (col &&
				    (size_t)(wend - wbeg) > maxcol - col) {
					putc('\n', fo);
					sz++;
					col = 0;
					maxcol = 76;
					if (lastspc == NULL) {
						putc(' ', fo);
						sz++;
						maxcol--;
					} else
						maxcol -= wbeg - lastspc;
				}
				if (lastspc)
					while (lastspc < wbeg) {
						putc(*lastspc&0377, fo);
						lastspc++, sz++;
					}
				wr = fwrite(wbeg, sizeof *wbeg,
						wend - wbeg, fo);
				sz += wr, col += wr;
				lastwordend = NULL;
			}
		}
	}
	return sz;
}

/*
 * Write len characters of the passed string to the passed file, 
 * doing charset and header conversion.
 */
static size_t
convhdra(char *str, size_t len, FILE *fp)
{
#ifdef	HAVE_ICONV
	char *cbuf = NULL;
#endif
	struct str cin;
	size_t ret = 0;

#ifdef HAVE_ICONV
	if (iconvd == (iconv_t)-1) {
#endif
		cin.s = str;
		cin.l = len;
#ifdef HAVE_ICONV
	} else {
		char *op, *ip;
		size_t osz, isz, cbufsz = (len << 1) - (len >> 2);

jagain:		osz = cbufsz;
		op = cbuf = ac_alloc(cbufsz);
		ip = str;
		isz = len;
		if (iconv_ft(iconvd, (char const**)&ip, &isz, &op, &osz, 0)
				== (size_t)-1) {
			ac_free(cbuf);
			if (errno != E2BIG)
				goto jleave;
			cbufsz += isz;
			goto jagain;
		}
		cin.s = cbuf;
		cin.l = cbufsz - osz;
	}
#endif
	ret = mime_write_tohdr(&cin, fp);
#ifdef HAVE_ICONV
	if (cbuf != NULL)
		ac_free(cbuf);
jleave:
#endif
	return (ret);
}

/*
 * Write an address to a header field.
 */
static size_t
mime_write_tohdr_a(struct str *in, FILE *f)
{
	char	*cp, *lastcp;
	size_t	sz = 0;

	in->s[in->l] = '\0';
	lastcp = in->s;
	if ((cp = routeaddr(in->s)) != NULL && cp > lastcp) {
		sz += convhdra(lastcp, cp - lastcp, f);
		lastcp = cp;
	} else
		cp = in->s;
	for ( ; *cp; cp++) {
		switch (*cp) {
		case '(':
			sz += fwrite(lastcp, 1, cp - lastcp + 1, f);
			lastcp = ++cp;
			cp = (char*)skip_comment(cp);
			if (--cp > lastcp)
				sz += convhdra(lastcp, cp - lastcp, f);
			lastcp = cp;
			break;
		case '"':
			while (*cp) {
				if (*++cp == '"')
					break;
				if (*cp == '\\' && cp[1])
					cp++;
			}
			break;
		}
	}
	if (cp > lastcp)
		sz += fwrite(lastcp, 1, cp - lastcp, f);
	return sz;
}

static void
addstr(char **buf, size_t *sz, size_t *pos, char const *str, size_t len)
{
	*buf = srealloc(*buf, *sz += len);
	memcpy(&(*buf)[*pos], str, len);
	*pos += len;
}

static void
addconv(char **buf, size_t *sz, size_t *pos, char const *str, size_t len)
{
	struct str in, out;

	in.s = (char*)str;
	in.l = len;
	mime_fromhdr(&in, &out, TD_ISPR|TD_ICONV);
	addstr(buf, sz, pos, out.s, out.l);
	free(out.s);
}

/*
 * Interpret MIME strings in parts of an address field.
 */
char *
mime_fromaddr(char const *name)
{
	char const *cp, *lastcp;
	char *res = NULL;
	size_t ressz = 1, rescur = 0;

	if (name == NULL)
		return (res);
	if (*name == '\0')
		return savestr(name);
	if ((cp = routeaddr(name)) != NULL && cp > name) {
		addconv(&res, &ressz, &rescur, name, cp - name);
		lastcp = cp;
	} else
		cp = lastcp = name;
	for ( ; *cp; cp++) {
		switch (*cp) {
		case '(':
			addstr(&res, &ressz, &rescur, lastcp, cp - lastcp + 1);
			lastcp = ++cp;
			cp = (char*)skip_comment(cp);
			if (--cp > lastcp)
				addconv(&res, &ressz, &rescur, lastcp,
						cp - lastcp);
			lastcp = cp;
			break;
		case '"':
			while (*cp) {
				if (*++cp == '"')
					break;
				if (*cp == '\\' && cp[1])
					cp++;
			}
			break;
		}
	}
	if (cp > lastcp)
		addstr(&res, &ressz, &rescur, lastcp, cp - lastcp);
	res[rescur] = '\0';
	{	char *x = res;
		res = savestr(res);
		free(x);
	}
	return (res);
}

/*
 * fwrite whilst adding prefix, if present.
 */
size_t
prefixwrite(void *ptr, size_t size, size_t nmemb, FILE *f,
		char *prefix, size_t prefixlen)
{
	static FILE *lastf;
	static char lastc = '\n';
	size_t lpref, i, qfold = 0, lnlen = 0, rsz = size * nmemb, wsz = 0;
	char *p, *maxp, c;

	if (rsz == 0)
		return 0;

	if (prefixlen == 0)
		return fwrite(ptr, 1, rsz, f);

	if ((p = value("quote-fold")) != NULL) {
		qfold = (size_t)strtol(p, NULL, 10);
		if (qfold < prefixlen + 4)
			qfold = prefixlen + 4;
		--qfold; /* The newline escape */
	}

	if (f != lastf || lastc == '\n') {
		wsz += fwrite(prefix, sizeof *prefix, prefixlen, f);
		lnlen = prefixlen;
	}
	lastf = f;

	p = ptr;
	maxp = p + rsz;

	if (! qfold) {
		for (;;) {
			c = *p++;
			putc(c, f);
			wsz++;
			if (p == maxp)
				break;
			if (c != '\n')
				continue;
			wsz += fwrite(prefix, sizeof *prefix, prefixlen, f);
		}
	} else {
		for (;;) {
			/*
			 * After writing a real newline followed by our prefix,
			 * compress the quoted prefixes
			 */
			for (lpref = 0; p != maxp;) {
				/* (c: keep cc happy) */
				for (c = i = 0; p + i < maxp;) {
					c = p[i++];
					if (blankspacechar(c))
						continue;
					if (! ISQUOTE(c))
						goto jquoteok;
					break;
				}
				p += i;
				++lpref;
				putc(c, f);
				++wsz;
			}
jquoteok:		lnlen += lpref;

jsoftnl:		/*
			 * Search forward until either *quote-fold* or NL.
			 * In the former case try to break at whitespace,
			 * but only if that lies in the 2nd half of the data
			 */
			for (c = rsz = i = 0; p + i < maxp;) {
				c = p[i++];
				if (c == '\n')
					break;
				if (spacechar(c))
					rsz = i;
				if (lnlen + i >= qfold) {
					c = 0;
					if (rsz > qfold >> 1)
						i = rsz;
					break;
				}
			}

			if (i > 0) {
				wsz += fwrite(p, sizeof *p, i, f);
				p += i;
			}
			if (p >= maxp)
				break;
	
			if (c != '\n') {
				putc('\\', f);
				putc('\n', f);
				wsz += 2;
			}

			wsz += fwrite(prefix, sizeof *prefix, prefixlen, f);
			lnlen = prefixlen;
			if (c == '\n')
				continue;

			if ((i = lpref)) {
				for (; i > 0; ++wsz, ++lnlen, --i)
					(void)putc('.', f);
				(void)putc(' ', f);
				++wsz;
				++lnlen;
			}
			goto jsoftnl;
		}
	}

	lastc = p[-1];
	return (wsz);
}

/*
 * fwrite while checking for displayability.
 */
static size_t
fwrite_td(void *ptr, size_t size, size_t nmemb, FILE *f, enum tdflags flags,
		char *prefix, size_t prefixlen)
{
	char *upper;
	size_t sz, csize;
#ifdef	HAVE_ICONV
	char *iptr, *nptr;
	size_t inleft, outleft;
#endif
	char *mptr, *xmptr, *mlptr = NULL;
	size_t mptrsz;

	csize = size * nmemb;
	mptrsz = csize;
	mptr = xmptr = ac_alloc(mptrsz + 1);
#ifdef	HAVE_ICONV
	if ((flags & TD_ICONV) && iconvd != (iconv_t)-1) {
	again:	inleft = csize;
		outleft = mptrsz;
		nptr = mptr;
		iptr = ptr;
		if (iconv_ft(iconvd, (char const **)&iptr, &inleft,
				&nptr, &outleft, 1) == (size_t)-1 &&
				errno == E2BIG) {
			iconv_ft(iconvd, NULL, NULL, NULL, NULL, 0);
			ac_free(xmptr);
			mptrsz += inleft;
			mptr = xmptr = ac_alloc(mptrsz + 1);
			goto again;
		}
		nmemb = mptrsz - outleft;
		size = sizeof (char);
		ptr = mptr;
		csize = size * nmemb;
	} else
#endif
	{
		memcpy(mptr, ptr, csize);
	}
	upper = mptr + csize;
	*upper = '\0';
	if (flags & TD_ISPR) {
		struct str	in, out;
		in.s = mptr;
		in.l = csize;
		makeprint(&in, &out);
		/* TODO well if we get a broken pipe here, and it happens to
		 * TODO happen pretty easy when sleeping in a full pipe buffer,
		 * TODO then the current codebase performs longjump away;
		 * TODO this leaves memory leaks behind ('think up to 3 per,
		 * TODO dep. upon alloca availability).  For this to be fixed
		 * TODO we either need to get rid of the longjmp()s (tm) or
		 * TODO the storage must come from the outside or be tracked
		 * TODO in a carrier struct.  Best both.  But storage reuse
		 * TODO would be a bigbig win besides */
		mptr = mlptr = out.s;
		csize = out.l;
	}
	if (flags & TD_DELCTRL)
		csize = delctrl(mptr, csize);
	sz = prefixwrite(mptr, sizeof *mptr, csize, f, prefix, prefixlen);
	ac_free(xmptr);
	if (mlptr != NULL)
		free(mlptr);
	return sz;
}

/*
 * fwrite performing the given MIME conversion.
 */
ssize_t
mime_write(void const *ptr, size_t size, FILE *f,
	enum conversion convert, enum tdflags dflags,
	char *prefix, size_t prefixlen, struct str *rest)
{
	struct str in, out;
	ssize_t sz;
	int state;
#ifdef HAVE_ICONV
	char mptr[LINESIZE * 6];
	char *iptr, *nptr;
	size_t inleft, outleft;
#endif

	in.s = (char*)ptr;
	in.l = size;
	if ((sz = size) == 0) {
		if (rest != NULL && rest->l != 0)
			goto jconvert;
		goto jleave;
	}

#ifdef HAVE_ICONV
	if ((dflags & TD_ICONV) && size < sizeof(mptr) && iconvd != (iconv_t)-1
			&& (convert == CONV_TOQP || convert == CONV_8BIT ||
				convert == CONV_TOB64 ||
				convert == CONV_TOHDR)) {
		inleft = size;
		outleft = sizeof mptr;
		nptr = mptr;
		iptr = (char*)ptr;
		if (iconv_ft(iconvd, (char const **)&iptr, &inleft,
				&nptr, &outleft, 0) != (size_t)-1) {
			in.l = sizeof(mptr) - outleft;
			in.s = mptr;
		} else {
			if (errno == EILSEQ || errno == EINVAL) {
				/* xxx report convertion error? */;
			}
			sz = -1;
			goto jleave;
		}
	}
#endif

jconvert:
	out.s = NULL;
	out.l = 0;
	switch (convert) {
	case CONV_FROMQP:
		mime_fromqp(&in, &out, 0);
		sz = fwrite_td(out.s, sizeof *out.s, out.l, f, dflags,
				prefix, prefixlen);
		free(out.s);
		break;
	case CONV_TOQP:
		sz = mime_write_toqp(&in, f, mustquote_body);
		break;
	case CONV_8BIT:
		sz = prefixwrite(in.s, sizeof *in.s, in.l, f,
				prefix, prefixlen);
		break;
	case CONV_FROMB64_T:
	case CONV_FROMB64:
		state = b64_decode(&out, &in, 0, rest);
		if ((sz = out.l) != 0) {
			if (state != OKAY)
				prefix = NULL, prefixlen = 0;
			sz = fwrite_td(out.s, sizeof *out.s, out.l, f, dflags,
				prefix, prefixlen);
		}
		if (out.s != NULL)
			free(out.s);
		if (state != OKAY)
			sz = -1;
		break;
	case CONV_TOB64:
		(void)b64_encode(&out, &in, B64_LF|B64_MULTILINE);
		sz = fwrite(out.s, sizeof *out.s, out.l, f);
		if (sz != (ssize_t)out.l)
			sz = -1;
		free(out.s);
		break;
	case CONV_FROMHDR:
		mime_fromhdr(&in, &out, TD_ISPR|TD_ICONV);
		sz = fwrite_td(out.s, sizeof *out.s, out.l, f,
				dflags&TD_DELCTRL, prefix, prefixlen);
		free(out.s);
		break;
	case CONV_TOHDR:
		sz = mime_write_tohdr(&in, f);
		break;
	case CONV_TOHDR_A:
		sz = mime_write_tohdr_a(&in, f);
		break;
	default:
		sz = fwrite_td(in.s, sizeof *in.s, in.l, f, dflags,
				prefix, prefixlen);
	}
jleave:
	return sz;
}
