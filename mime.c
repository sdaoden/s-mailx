/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012, 2013 Steffen "Daode" Nurpmeso.
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

#define _CHARSET()	((_cs_iter != NULL) ? _cs_iter : charset_get_8bit())

struct mtnode {
	struct mtnode	*mt_next;
	size_t		mt_mtlen;	/* Length of MIME type string */
	char		mt_line[VFIELD_SIZE(8)];
};

static char const	basetable[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ",
			*const _mt_sources[] = {
		/* XXX Order fixed due to *mimetypes-load-control* handling! */
		MIME_TYPES_USR, MIME_TYPES_SYS, NULL
	},
			*const _mt_bltin[] = {
#include "mime_types.h"
		NULL
	};

struct mtnode	*_mt_list;
char		*_cs_iter_base, *_cs_iter;

/* Initialize MIME type list */
static void	_mt_init(void);
static void	__mt_add_line(char const *line, struct mtnode **tail);

/* Get the conversion that matches *encoding* */
static enum conversion _conversion_by_encoding(void);

/* fwrite(3) while checking for displayability */
static size_t	_fwrite_td(struct str const *input, FILE *f, enum tdflags flags,
			struct str *rest, char const *prefix, size_t prefixlen);

static size_t	delctrl(char *cp, size_t sz);
static int has_highbit(register const char *s);
static int is_this_enc(const char *line, const char *encoding);
static size_t mime_write_tohdr(struct str *in, FILE *fo);
static size_t convhdra(char const *str, size_t len, FILE *fp);
static size_t mime_write_tohdr_a(struct str *in, FILE *f);
static void addstr(char **buf, size_t *sz, size_t *pos,
		char const *str, size_t len);
static void addconv(char **buf, size_t *sz, size_t *pos,
		char const *str, size_t len);

static void
_mt_init(void)
{
	struct mtnode *tail = NULL;
	char *line = NULL;
	size_t linesize = 0;
	ui_it idx, idx_ok;
	char const *ccp, *const*srcs;
	FILE *fp;

	if ((ccp = value("mimetypes-load-control")) == NULL)
		idx_ok = (ui_it)-1;
	else for (idx_ok = 0; *ccp != '\0'; ++ccp)
		switch (*ccp) {
		case 'S':
		case 's':
			idx_ok |= 1 << 1;
			break;
		case 'U':
		case 'u':
			idx_ok |= 1 << 0;
			break;
		default:
			/* XXX bad *mimetypes-load-control*; log error? */
			break;
		}

	for (idx = 1, srcs = _mt_sources; *srcs != NULL; idx <<= 1, ++srcs) {
		if ((idx & idx_ok) == 0 || (ccp = file_expand(*srcs)) == NULL)
			continue;
		if ((fp = Fopen(ccp, "r")) == NULL) {
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

static enum conversion
_conversion_by_encoding(void)
{
	char const *cp;
	enum conversion ret;

	if ((cp = value("encoding")) == NULL)
		ret = MIME_DEFAULT_ENCODING;
	else if (strcmp(cp, "quoted-printable") == 0)
		ret = CONV_TOQP;
	else if (strcmp(cp, "8bit") == 0)
		ret = CONV_8BIT;
	else if (strcmp(cp, "base64") == 0)
		ret = CONV_TOB64;
	else {
		fprintf(stderr, tr(177,
			"Warning: invalid encoding %s, using base64\n"), cp);
		ret = CONV_TOB64;
	}
	return (ret);
}

static size_t
_fwrite_td(struct str const *input, FILE *f, enum tdflags flags,
	struct str *rest, char const *prefix, size_t prefixlen)
{
	/* TODO note: after send/MIME layer rewrite we will have a string pool
	 * TODO so that memory allocation count drops down massively; for now,
	 * TODO v14.* that is, we pay a lot & heavily depend on the allocator */
	/* TODO well if we get a broken pipe here, and it happens to
	 * TODO happen pretty easy when sleeping in a full pipe buffer,
	 * TODO then the current codebase performs longjump away;
	 * TODO this leaves memory leaks behind ('think up to 3 per,
	 * TODO dep. upon alloca availability).  For this to be fixed
	 * TODO we either need to get rid of the longjmp()s (tm) or
	 * TODO the storage must come from the outside or be tracked
	 * TODO in a carrier struct.  Best both.  But storage reuse
	 * TODO would be a bigbig win besides */
	/* *input* _may_ point to non-modifyable buffer; but even then it only
	 * needs to be dup'ed away if we have to transform the content */
	struct str in, out;

	in = *input;
	out.s = NULL;
	out.l = 0;

#ifdef HAVE_ICONV
	if ((flags & TD_ICONV) && iconvd != (iconv_t)-1) {
		char *buf = NULL;

		if (rest != NULL && rest->l > 0) {
			in.l = rest->l + input->l;
			in.s = buf = smalloc(in.l + 1);
			memcpy(in.s, rest->s, rest->l);
			memcpy(in.s + rest->l, input->s, input->l);
			rest->l = 0;

		}
		if (n_iconv_str(iconvd, &out, &in, &in, TRU1) != 0 &&
				rest != NULL && in.l > 0) {
			/* Incomplete multibyte at EOF is special */
			if (flags & TD_EOF) {
				out.s = srealloc(out.s, out.l + 4);
				out.s[out.l++] = '[';
				out.s[out.l++] = '?'; /* TODO 0xFFFD !!! */
				out.s[out.l++] = ']';
			} else
				(void)n_str_add(rest, &in);
		}
		in = out;
		out.s = NULL;
		flags &= ~_TD_BUFCOPY;

		if (buf != NULL)
			free(buf);
	}
#endif

	if (flags & TD_ISPR)
		makeprint(&in, &out);
	else if (flags & _TD_BUFCOPY)
		n_str_dup(&out, &in);
	else
		out = in;
	if (flags & TD_DELCTRL)
		out.l = delctrl(out.s, out.l);

	out.l = prefixwrite(out.s, out.l, f, prefix, prefixlen);

	if (out.s != in.s)
		free(out.s);
	if (in.s != input->s)
		free(in.s);
	return out.l;
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
charset_get_7bit(void)
{
	char const *t;

	if ((t = value("charset-7bit")) == NULL)
		t = CHARSET_7BIT;
	return (t);
}

char const *
charset_get_8bit(void)
{
	char const *t;

	if ((t = value(CHARSET_8BIT_VAR)) == NULL)
		t = CHARSET_8BIT;
	return (t);
}

char const *
charset_get_lc(void)
{
	char const *t;

	if ((t = value("ttycharset")) == NULL)
		t = CHARSET_8BIT;
	return (t);
}

void
charset_iter_reset(char const *a_charset_to_try_first)
{
	char const *sarr[3];
	size_t sarrl[3], len;
	char *cp;

	sarr[0] = a_charset_to_try_first;
#ifdef HAVE_ICONV
	if ((sarr[1] = value("sendcharsets")) == NULL &&
			value("sendcharsets-else-ttycharset"))
		sarr[1] = charset_get_lc();
#endif
	sarr[2] = charset_get_8bit();

	sarrl[2] = len = strlen(sarr[2]);
#ifdef HAVE_ICONV
	if ((cp = UNCONST(sarr[1])) != NULL)
		len += (sarrl[1] = strlen(cp));
	else
		sarrl[1] = 0;
	if ((cp = UNCONST(sarr[0])) != NULL)
		len += (sarrl[0] = strlen(cp));
	else
		sarrl[0] = 0;
#endif

	_cs_iter_base = cp = salloc(len + 1);

#ifdef HAVE_ICONV
	if ((len = sarrl[0]) != 0) {
		memcpy(cp, sarr[0], len);
		cp[len] = ',';
		cp += ++len;
	}
	if ((len = sarrl[1]) != 0) {
		memcpy(cp, sarr[1], len);
		cp[len] = ',';
		cp += ++len;
	}
#endif
	len = sarrl[2];
	memcpy(cp, sarr[2], len);
	cp[len] = '\0';
	_cs_iter = NULL;
}

char const *
charset_iter_next(void)
{
	return (_cs_iter = strcomma(&_cs_iter_base, 1));
}

char const *
charset_iter_current(void)
{
	return _cs_iter;
}

void
charset_iter_recurse(char *outer_storage[2]) /* TODO LEGACY FUN, REMOVE */
{
	outer_storage[0] = _cs_iter_base;
	outer_storage[1] = _cs_iter;
}

void
charset_iter_restore(char *outer_storage[2]) /* TODO LEGACY FUN, REMOVE */
{
	_cs_iter_base = outer_storage[0];
	_cs_iter = outer_storage[1];
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
jneeds:		ret = _CHARSET();
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
mime_getparam(char const *param, char *h)
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

char *
mime_create_boundary(void)
{
	char *bp;

	bp = salloc(48);
	snprintf(bp, 48, "=_%011lu=-%s=_",
		(ul_it)time_current.tc_time, getrandstring(47 - (11 + 6)));
	return bp;
}

int
mime_classify_file(FILE *fp, char const **contenttype, char const **charset,
	int *do_iconv)
{
	/* TODO classify once only PLEASE PLEASE PLEASE */
	/* TODO BTW., after the MIME/send layer rewrite we could use a MIME
	 * TODO boundary of "=-=-=" if we would add a B_ in EQ spirit to F_,
	 * TODO and report that state to the outer world */
#define F_		"From "
#define F_SIZEOF	(sizeof(F_) - 1)

	char f_buf[F_SIZEOF], *f_p = f_buf;
	enum {	_CLEAN		= 0,	/* Plain RFC 2822 message */
		_NCTT		= 1<<0,	/* *contenttype == NULL */
		_ISTXT		= 1<<1,	/* *contenttype =~ text/ */
		_ISTXTCOK	= 1<<2,	/* _ISTXT+*mime-allow-text-controls* */
		_HIGHBIT	= 1<<3,	/* Not 7bit clean */
		_LONGLINES	= 1<<4,	/* MIME_LINELEN_LIMIT exceed. */
		_CTRLCHAR	= 1<<5,	/* Control characters seen */
		_HASNUL		= 1<<6,	/* Contains \0 characters */
		_NOTERMNL	= 1<<7,	/* Lacks a final newline */
		_TRAILWS	= 1<<8,	/* Blanks before NL */
		_FROM_		= 1<<9	/* ^From_ seen */
	} ctt = _CLEAN;
	enum conversion convert;
	sl_it curlen;
	int c, lastc;

	assert(ftell(fp) == 0x0l);

	*do_iconv = 0;

	if (*contenttype == NULL)
		ctt = _NCTT;
	else if (ascncasecmp(*contenttype, "text/", 5) == 0)
		ctt = value("mime-allow-text-controls")
			? _ISTXT | _ISTXTCOK : _ISTXT;
	convert = _conversion_by_encoding();

	if (fsize(fp) == 0)
		goto j7bit;

	/* We have to inspect the file content */
	for (curlen = 0, c = EOF;; ++curlen) {
		lastc = c;
		c = getc(fp);

		if (c == '\0') {
			ctt |= _HASNUL;
			if ((ctt & _ISTXTCOK) == 0)
				break;
			continue;
		}
		if (c == '\n' || c == EOF) {
			if (curlen >= MIME_LINELEN_LIMIT)
				ctt |= _LONGLINES;
			if (c == EOF)
				break;
			if (blankchar(lastc))
				ctt |= _TRAILWS;
			f_p = f_buf;
			curlen = -1;
			continue;
		}
		/*
		 * A bit hairy is handling of \r=\x0D=CR.
		 * RFC 2045, 6.7: Control characters other than TAB, or
		 * CR and LF as parts of CRLF pairs, must not appear.
		 * \r alone does not force _CTRLCHAR below since we cannot peek
		 * the next character.
		 * Thus right here, inspect the last seen character for if its
		 * \r and set _CTRLCHAR in a delayed fashion
		 */
		/*else*/ if (lastc == '\r')
			ctt |= _CTRLCHAR;

		/* Control character? */
		if (c < 0x20 || c == 0x7F) {
			/* RFC 2045, 6.7, as above ... */
			if (c != '\t' && c != '\r')
				ctt |= _CTRLCHAR;
			/*
			 * If there is a escape sequence in backslash notation
			 * defined for this in ANSI X3.159-1989 (ANSI C89),
			 * don't treat it as a control for real.
			 * I.e., \a=\x07=BEL, \b=\x08=BS, \t=\x09=HT.
			 * Don't follow libmagic(1) in respect to \v=\x0B=VT.
			 * \f=\x0C=NP; do ignore \e=\x1B=ESC.
			 */
			if ((c >= '\x07' && c <= '\x0D') || c == '\x1B')
				continue;
			ctt |= _HASNUL; /* Force base64 */
			if ((ctt & _ISTXTCOK) == 0)
				break;
		} else if (c & 0x80) {
			ctt |= _HIGHBIT;
			/* TODO count chars with HIGHBIT? libmagic?
			 * TODO try encode part - base64 if bails? */
			if ((ctt & (_NCTT|_ISTXT)) == 0) { /* TODO _NCTT?? */
				ctt |= _HASNUL; /* Force base64 */
				break;
			}
		} else if ((ctt & _FROM_) == 0 && curlen < (sl_it)F_SIZEOF) {
			*f_p++ = (char)c;
			if (curlen == (sl_it)(F_SIZEOF - 1) &&
					(size_t)(f_p - f_buf) == F_SIZEOF &&
					memcmp(f_buf, F_, F_SIZEOF) == 0)
				ctt |= _FROM_;
		}
	}
	if (lastc != '\n')
		ctt |= _NOTERMNL;
	rewind(fp);

	if (ctt & _HASNUL) {
		convert = CONV_TOB64;
		/* Don't overwrite a text content-type to allow UTF-16 and
		 * such, but only on request;
		 * Otherwise enforce what file(1)/libmagic(3) would suggest */
		if (ctt & _ISTXTCOK)
			goto jcharset;
		if (ctt & (_NCTT|_ISTXT))
			*contenttype = "application/octet-stream";
		if (*charset == NULL)
			*charset = "binary";
		goto jleave;
	}

	if (ctt & (_LONGLINES|_CTRLCHAR|_NOTERMNL|_TRAILWS|_FROM_)) {
		convert = CONV_TOQP;
		goto jstepi;
	}
	if (ctt & _HIGHBIT) {
jstepi:		if (ctt & (_NCTT|_ISTXT))
			*do_iconv = (ctt & _HIGHBIT) != 0;
	} else
j7bit:		convert = CONV_7BIT;
	if (ctt & _NCTT)
		*contenttype = "text/plain";

	/* Not an attachment with specified charset? */
jcharset:
	if (*charset == NULL)
		*charset = (ctt & _HIGHBIT) ? _CHARSET() : charset_get_7bit();
jleave:
	return (convert);
#undef F_
#undef F_SIZEOF
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
 * Convert header fields from RFC 1522 format
 * TODO mime_fromhdr(): NO error handling, fat; REWRITE **ASAP**
 */
void 
mime_fromhdr(struct str const *in, struct str *out, enum tdflags flags)
{
	/* TODO mime_fromhdr(): is called with strings that contain newlines;
	 * TODO this is the usual newline problem all around the codebase;
	 * TODO i.e., if we strip it, then the display misses it ;} */
	struct str cin, cout;
	char *p, *op, *upper, *cs, *cbeg;
	char const *tcs;
	int convert;
	size_t lastoutl = (size_t)-1;
#ifdef HAVE_ICONV
	iconv_t fhicd = (iconv_t)-1;
#endif

	out->l = 0;
	if (in->l == 0) {
		*(out->s = smalloc(1)) = '\0';
		goto jleave;
	}
	out->s = NULL;

	tcs = charset_get_lc();
	p = in->s;
	upper = p + in->l;

	while (p < upper) {
		op = p;
		if (*p == '=' && *(p + 1) == '?') {
			p += 2;
			cbeg = p;
			while (p < upper && *p != '?')
				p++;	/* strip charset */
			if (p >= upper)
				goto jnotmime;
			cs = salloc(++p - cbeg);
			memcpy(cs, cbeg, p - cbeg - 1);
			cs[p - cbeg - 1] = '\0';
#ifdef HAVE_ICONV
			if (fhicd != (iconv_t)-1)
				n_iconv_close(fhicd);
			if (asccasecmp(cs, tcs) != 0)
				fhicd = n_iconv_open(tcs, cs);
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
				goto jnotmime;
			}
			if (*++p != '?')
				goto jnotmime;
			cin.s = ++p;
			cin.l = 1;
			for (;;) {
				if (p + 1 >= upper)
					goto jnotmime;
				if (*p++ == '?' && *p == '=')
					break;
				cin.l++;
			}
			++p;
			cin.l--;

			cout.s = NULL;
			cout.l = 0;
			if (convert == CONV_FROMB64) {
				/* XXX Take care for, and strip LF from
				 * XXX [Invalid Base64 encoding ignored] */
				if (b64_decode(&cout, &cin, NULL) == STOP &&
						cout.s[cout.l - 1] == '\n')
					--cout.l;
			} else
				(void)qp_decode(&cout, &cin, NULL);
			if (lastoutl != (size_t)-1)
				out->l = lastoutl;
#ifdef HAVE_ICONV
			if ((flags & TD_ICONV) && fhicd != (iconv_t)-1) {
				cin.s = NULL, cin.l = 0; /* XXX string pool ! */
				convert = n_iconv_str(fhicd, &cin, &cout,
						NULL, TRU1);
				out = n_str_add(out, &cin);
				if (convert) /* EINVAL at EOS */
					out = n_str_add_buf(out, "[?]", 3);
				free(cin.s);
			} else {
#endif
				out = n_str_add(out, &cout);
#ifdef HAVE_ICONV
			}
#endif
			lastoutl = out->l;
			free(cout.s);
		} else {
jnotmime:
			p = op;
			convert = 1;
			while ((op = p + convert) < upper &&
					(op[0] != '=' || op[1] != '?'))
				++convert;
			out = n_str_add_buf(out, p, convert);
			p += convert;
			if (! blankchar(p[-1]))
				lastoutl = (size_t)-1;
		}
	}
	out->s[out->l] = '\0';

	if (flags & TD_ISPR) {
		makeprint(out, &cout);
		free(out->s);
		*out = cout;
	}
	if (flags & TD_DELCTRL)
		out->l = delctrl(out->s, out->l);
#ifdef HAVE_ICONV
	if (fhicd != (iconv_t)-1)
		n_iconv_close(fhicd);
#endif
jleave:
	return;
}

/*
 * Convert header fields to RFC 1522 format and write to the file fo.
 */
static size_t
mime_write_tohdr(struct str *in, FILE *fo) /* TODO rewrite - FAST! */
{
	struct str cin, cout;
	char buf[B64_LINESIZE];
	char const *charset7, *charset, *upper, *wbeg, *wend, *lastspc,
		*lastwordend = NULL;
	size_t sz = 0, col = 0, quoteany, wr, charsetlen,
		maxcol = 65 /* there is the header field's name, too */;
	bool_t highbit, mustquote, broken;

	charset7 = charset_get_7bit();
	charset = _CHARSET();
	wr = strlen(charset7);
	charsetlen = strlen(charset);
	charsetlen = MAX(charsetlen, wr);
	upper = in->s + in->l;

	/* xxx note this results in too much hits since =/? force quoting even
	 * xxx if they don't form =? etc. */
	quoteany = mime_cte_mustquote(in->s, in->l, TRU1);

	highbit = FAL0;
	if (quoteany != 0)
		for (wbeg = in->s; wbeg < upper; ++wbeg)
			if ((uc_it)*wbeg & 0x80)
				highbit = TRU1;

	if (quoteany << 1 > in->l) {
		/*
		 * Print the entire field in base64.
		 */
		for (wbeg = in->s; wbeg < upper; wbeg = wend) {
			wend = upper;
			cin.s = UNCONST(wbeg);
			for (;;) {
				cin.l = wend - wbeg;
				if (cin.l * 4/3 + 7 + charsetlen
						< maxcol - col) {
					cout.s = buf;
					cout.l = sizeof buf;
					wr = fprintf(fo, "=?%s?B?%s?=",
						highbit ? charset : charset7,
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
		broken = FAL0;
		for (wbeg = in->s; wbeg < upper; wbeg = wend) {
			lastspc = NULL;
			while (wbeg < upper && whitechar(*wbeg)) {
				lastspc = lastspc ? lastspc : wbeg;
				wbeg++;
				col++;
				broken = FAL0;
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

			if (lastspc != NULL)
				broken = FAL0;
			highbit = FAL0;
			for (wend = wbeg; wend < upper && ! whitechar(*wend);
					++wend)
				if ((uc_it)*wend & 0x80)
					highbit = TRU1;
			mustquote = (mime_cte_mustquote(wbeg,
					(size_t)(wend - wbeg), TRU1) != 0);

			if (mustquote || broken ||
					((wend - wbeg) >= 76-5 && quoteany)) {
				for (cout.s = NULL;;) {
					cin.s = UNCONST(lastwordend ?
							lastwordend : wbeg);
					cin.l = wend - cin.s;
					(void)qp_encode(&cout, &cin, QP_ISHEAD);
					wr = cout.l + charsetlen + 7;
jqp_retest:
					if (col <= maxcol &&
							wr <= maxcol - col) {
						if (lastspc) {
							/* TODO because we inc-
							 * TODO luded the WS in
							 * TODO the encoded str,
							 * TODO put SP only??
							 * TODO RFC: "any
							 * 'linear-white-space'
							 * that separates
							 * a pair of adjacent
							 * 'encoded-word's is
							 * ignored" */
							putc(' ', fo);
							++sz;
							++col;
						}
						fprintf(fo, "=?%s?Q?%.*s?=",
							highbit ? charset
							: charset7,
							(int)cout.l, cout.s);
						sz += wr, col += wr;
						break;
					} else if (col > 1) {
						/* TODO assuming SP separator,
						 * TODO ignore *lastspc* !?? */
						broken = TRU1;
						if (lastspc != NULL) {
							putc('\n', fo);
							++sz;
							col = 0;
						} else {
							fputs("\n ",
								fo);
							sz += 2;
							col = 1;
						}
						maxcol = 76;
						goto jqp_retest;
					} else {
						for (;;) { /* XXX */
							wend -= 4;
							assert(wend > wbeg);
							if (wr - 4 < maxcol)
								break;
							wr -= 4;
						}
					}
				}
				if (cout.s != NULL)
					free(cout.s);
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
convhdra(char const *str, size_t len, FILE *fp)
{
#ifdef HAVE_ICONV
	struct str ciconv;
#endif
	struct str cin;
	size_t ret = 0;

	cin.s = UNCONST(str);
	cin.l = len;
#ifdef HAVE_ICONV
	ciconv.s = NULL;
	if (iconvd != (iconv_t)-1) {
		ciconv.l = 0;
		if (n_iconv_str(iconvd, &ciconv, &cin, NULL, FAL0) != 0)
			goto jleave;
		cin = ciconv;
	}
#endif
	ret = mime_write_tohdr(&cin, fp);
#ifdef HAVE_ICONV
jleave:
	if (ciconv.s != NULL)
		free(ciconv.s);
#endif
	return ret;
}

/*
 * Write an address to a header field.
 */
static size_t
mime_write_tohdr_a(struct str *in, FILE *f)
{
	char const *cp, *lastcp;
	size_t sz = 0;

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
			cp = skip_comment(cp);
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

	in.s = UNCONST(str);
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
			cp = skip_comment(cp);
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

size_t
prefixwrite(char const *ptr, size_t size, FILE *f,
	char const *prefix, size_t prefixlen)
{
	static FILE *lastf;		/* TODO NO STATIC COOKIES */
	static char lastc = '\n';	/* TODO PLEASE PLEASE PLEASE! */
	size_t lpref, i, qfold = 0, lnlen = 0, rsz = size, wsz = 0;
	char const *p, *maxp;
	char c;

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
				for (++i; i--;)
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

ssize_t
mime_write(char const *ptr, size_t size, FILE *f,
	enum conversion convert, enum tdflags dflags,
	char const *prefix, size_t prefixlen, struct str *rest)
{
	/* TODO note: after send/MIME layer rewrite we will have a string pool
	 * TODO so that memory allocation count drops down massively; for now,
	 * TODO v14.0 that is, we pay a lot & heavily depend on the allocator */
	struct str in, out;
	ssize_t sz;
	int state;

	in.s = UNCONST(ptr);
	in.l = size;
	out.s = NULL;
	out.l = 0;

	dflags |= _TD_BUFCOPY;
	if ((sz = size) == 0) {
		if (rest != NULL && rest->l != 0)
			goto jconvert;
		goto jleave;
	}

#ifdef HAVE_ICONV
	if ((dflags & TD_ICONV) && iconvd != (iconv_t)-1 &&
			(convert == CONV_TOQP || convert == CONV_8BIT ||
			convert == CONV_TOB64 || convert == CONV_TOHDR)) {
		if (n_iconv_str(iconvd, &out, &in, NULL, FAL0) != 0) {
			/* XXX report conversion error? */;
			sz = -1;
			goto jleave;
		}
		in = out;
		out.s = NULL;
		dflags &= ~_TD_BUFCOPY;
	}
#endif

jconvert:
	switch (convert) {
	case CONV_FROMQP:
		state = qp_decode(&out, &in, rest);
		goto jqpb64_dec;
	case CONV_TOQP:
		(void)qp_encode(&out, &in, QP_NONE);
		goto jqpb64_enc;
	case CONV_8BIT:
		sz = prefixwrite(in.s, in.l, f, prefix, prefixlen);
		break;
	case CONV_FROMB64:
		rest = NULL;
	case CONV_FROMB64_T:
		state = b64_decode(&out, &in, rest);
jqpb64_dec:
		if ((sz = out.l) != 0) {
			if (state != OKAY)
				prefix = NULL, prefixlen = 0;
			sz = _fwrite_td(&out, f, dflags & ~_TD_BUFCOPY,
				rest, prefix, prefixlen);
		}
		if (state != OKAY)
			sz = -1;
		break;
	case CONV_TOB64:
		(void)b64_encode(&out, &in, B64_LF|B64_MULTILINE);
jqpb64_enc:
		sz = fwrite(out.s, sizeof *out.s, out.l, f);
		if (sz != (ssize_t)out.l)
			sz = -1;
		break;
	case CONV_FROMHDR:
		mime_fromhdr(&in, &out,
			TD_ISPR|TD_ICONV | (dflags & TD_DELCTRL));
		sz = prefixwrite(out.s, out.l, f, prefix, prefixlen);
		break;
	case CONV_TOHDR:
		sz = mime_write_tohdr(&in, f);
		break;
	case CONV_TOHDR_A:
		sz = mime_write_tohdr_a(&in, f);
		break;
	default:
		sz = _fwrite_td(&in, f, dflags, NULL, prefix, prefixlen);
		break;
	}
jleave:
	if (out.s != NULL)
		free(out.s);
	if (in.s != ptr)
		free(in.s);
	return sz;
}
