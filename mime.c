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
static size_t	_fwrite_td(char *ptr, size_t size, FILE *f, enum tdflags flags,
			char const *prefix, size_t prefixlen);

static int mustquote_body(int c);
static int mustquote_hdr(const char *cp, int wordstart, int wordend);
static int mustquote_inhdrq(int c);
static size_t	delctrl(char *cp, size_t sz);
static int has_highbit(register const char *s);
static int is_this_enc(const char *line, const char *encoding);
static char *ctohex(unsigned char c, char *hex);
static size_t mime_write_toqp(struct str *in, FILE *fo, int (*mustquote)(int));
static void mime_str_toqp(struct str *in, struct str *out,
		int (*mustquote)(int), int inhdr);
static void mime_fromqp(struct str *in, struct str *out, int ishdr);
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
_fwrite_td(char *ptr, size_t size, FILE *f, enum tdflags flags,
	char const *prefix, size_t prefixlen)
{
	/* TODO note: after send/MIME layer rewrite we will have a string pool
	 * TODO so that memory allocation count drops down massively; for now,
	 * TODO v14.0 that is, we pay a lot & heavily depend on the allocator */
	/* TODO well if we get a broken pipe here, and it happens to
	 * TODO happen pretty easy when sleeping in a full pipe buffer,
	 * TODO then the current codebase performs longjump away;
	 * TODO this leaves memory leaks behind ('think up to 3 per,
	 * TODO dep. upon alloca availability).  For this to be fixed
	 * TODO we either need to get rid of the longjmp()s (tm) or
	 * TODO the storage must come from the outside or be tracked
	 * TODO in a carrier struct.  Best both.  But storage reuse
	 * TODO would be a bigbig win besides */
	struct str in, out;

	in.s = ptr;
	in.l = size;
	out.s = NULL;
	out.l = 0;

	if (! (
#ifdef HAVE_ICONV
		((flags & TD_ICONV) && iconvd != (iconv_t)-1) ||
#endif
			(flags & (TD_ISPR|TD_DELCTRL|_TD_BUFCOPY))
			== (TD_DELCTRL|_TD_BUFCOPY)))
		flags &= ~(TD_ICONV|_TD_BUFCOPY);

#ifdef HAVE_ICONV
	if (flags & TD_ICONV) {
		/* TODO leftover data (incomplete multibyte sequences) not
		 * TODO handled, leads to many skipped over data
		 * TODO send/MIME rewrite: don't assume complete input line is
		 * TODO a complete output line, PLUS */
		(void)str_iconv(iconvd, &out, &in, TRU1); /* XXX ERRORS?! */
		in = out;
	} else
#endif
	if (flags & _TD_BUFCOPY) {
		str_dup(&out, &in);
		in = out;
	}

	if (flags & TD_ISPR)
		makeprint(&in, &out);
	else {
		out.s = in.s;
		out.l = in.l;
	}
	if (flags & TD_DELCTRL)
		out.l = delctrl(out.s, out.l);
	size = prefixwrite(out.s, out.l, f, prefix, prefixlen);

	if (out.s != in.s)
		free(out.s);
	if (in.s != ptr)
		free(in.s);
	return size;
}

/*
 * Check if c must be quoted inside a message's body.
 */
static int 
mustquote_body(int c)
{
	/* FIXME use lookup table, this encodes too much, possibly worse */
	/* FIXME encodes \t, does NOT encode \x0D\n as \x0D\x0A[=] */
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
	/* TODO In the future the handling of charsets is likely to change.
	 * TODO If no *sendcharsets* are set then for text parts LC_ALL /
	 * TODO *ttycharset* is simply passed through (ditto attachments unless
	 * TODO specific charsets have been set in per-attachment level), i.e.,
	 * TODO no conversion at all.
	 * TODO If *sendcharsets* is set, then the input is converted to the
	 * TODO desired sendcharset, and once the charset that can handle the
	 * TODO input has been found the MIME classification takes place *on
	 * TODO that converted data* and once.
	 * TODO Until then the MIME classification takes place on the input
	 * TODO data, i.e., before actual charset conversion, though the MIME
	 * TODO classifier may adjust the output character set, though on false
	 * TODO assumptions that may not always work for the desired output
	 * TODO charset (???).
	 * TODO The new approach sounds more sane to me whatsoever.
	 * TODO It has the side effect that iconv() is applied to the text even
	 * TODO if that is 7bit clean, however, *iff* *sendcharsets* is set.
	 * TODO drop *charset* and *do_iconv* parameters, then; need to report
	 * TODO a "classify as binary charset", though.
	 * TODO And note that even the new approach will not allow RFC 2045
	 * TODO compatible base64 \n -> \r\n conversion even if we would make
	 * TODO a difference for _ISTXT, because who knows wether we deal with
	 * TODO multibyte encoded data?  We would need to be multibyte-aware!!
	 * TODO BTW., after the MIME/send layer rewrite we could use a MIME
	 * TODO boundary of "=-=-=" if we would add a B_ in EQ spirit to F_,
	 * TODO and report that state to the outer world */
#define F_		"From "
#define F_SIZEOF	(sizeof(F_) - 1)

	char f_buf[F_SIZEOF], *f_p = f_buf;
	enum {	_CLEAN		= 0,	/* Plain RFC 2822 message */
		_NCTT		= 1<<0,	/* *contenttype == NULL */
		_ISTXT		= 1<<1,	/* *contenttype =~ text/ */
		_HIGHBIT	= 1<<2,	/* Not 7bit clean */
		_LONGLINES	= 1<<3,	/* MIME_LINELEN_LIMIT exceed. */
		_CTRLCHAR	= 1<<4,	/* Control characters seen */
		_HASNUL		= 1<<5,	/* Contains \0 characters */
		_NOTERMNL	= 1<<6,	/* Lacks a final newline */
		_TRAILWS	= 1<<7,	/* Blanks before NL */
		_FROM_		= 1<<8	/* ^From_ seen */
	} ctt = _CLEAN;
	enum conversion convert;
	sl_it curlen;
	int c, lastc;

	assert(ftell(fp) == 0x0l);

	*do_iconv = 0;

	if (*contenttype == NULL)
		ctt = _NCTT;
	else if (ascncasecmp(*contenttype, "text/", 5) == 0)
		ctt = _ISTXT;
	convert = _conversion_by_encoding();

	if (fsize(fp) == 0)
		goto j7bit;

	/* We have to inspect the file content */
	for (curlen = 0, c = EOF;; ++curlen) {
		lastc = c;
		c = getc(fp);

		if (c == '\0') {
			ctt |= _HASNUL;
			break;
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
		if (ctt & (_NCTT|_ISTXT))
			*contenttype = "application/octet-stream";
		/* XXX Set *charset=binary only if not yet set as not to loose
		 * XXX UTF-16 etc. character set information?? */
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
	struct str cin, cout;
	char *p, *q, *op, *upper, *cs, *cbeg, *lastwordend = NULL;
	char const *tcs;
	int convert;
	size_t maxstor, lastoutl = 0;
#ifdef	HAVE_ICONV
	iconv_t fhicd = (iconv_t)-1;
#endif

	tcs = charset_get_lc();
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

			cout.s = NULL;
			cout.l = 0;
			switch (convert) {
				case CONV_FROMB64:
					(void)b64_decode(&cout, &cin, 0, NULL);
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
				char const *iptr;
				char *mptr, *nptr, *uptr;
				size_t inleft, outleft;

			again:	inleft = cout.l;
				outleft = maxstor - out->l;
				mptr = nptr = q;
				uptr = nptr + outleft;
				iptr = cout.s;
				if (iconv_ft(fhicd, &iptr,&inleft,
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
	if (flags & TD_ISPR) {
		makeprint(out, &cout);
		free(out->s);
		*out = cout;
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
	char const *charset7, *charset;
	struct str cin, cout;
	size_t sz = 0, col = 0, wr, charsetlen;
	int quoteany, mustquote, broken,
		maxcol = 65 /* there is the header field's name, too */;

	charset7 = charset_get_7bit();
	charset = _CHARSET();
	wr = strlen(charset7);
	charsetlen = strlen(charset);
	charsetlen = MAX(charsetlen, wr);
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
convhdra(char const *str, size_t len, FILE *fp)
{
#ifdef	HAVE_ICONV
	char *cbuf = NULL;
#endif
	struct str cin;
	size_t ret = 0;

#ifdef HAVE_ICONV
	if (iconvd == (iconv_t)-1) {
#endif
		cin.s = UNCONST(str);
		cin.l = len;
#ifdef HAVE_ICONV
	} else {
		char *op;
		char const *ip;
		size_t osz, isz, cbufsz = (len << 1) - (len >> 2);

jagain:		osz = cbufsz;
		op = cbuf = ac_alloc(cbufsz);
		ip = str;
		isz = len;
		if (iconv_ft(iconvd, &ip, &isz, &op, &osz, 0) == (size_t)-1) {
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
	struct str in, out;
	ssize_t sz;
	int state;
#ifdef HAVE_ICONV
	char mptr[LINESIZE * 6], *nptr;
	char const *iptr;
	size_t inleft, outleft;
#endif

	dflags |= _TD_BUFCOPY;
	in.s = UNCONST(ptr);
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
		iptr = UNCONST(ptr);
		if (iconv_ft(iconvd, &iptr, &inleft, &nptr, &outleft, 0)
				!= (size_t)-1) {
			in.l = sizeof(mptr) - outleft;
			in.s = mptr;
		} else {
			if (errno == EILSEQ || errno == EINVAL) {
				/* xxx report convertion error? */;
			}
			sz = -1;
			goto jleave;
		}
		dflags &= ~_TD_BUFCOPY;
	}
#endif

jconvert:
	out.s = NULL;
	out.l = 0;
	switch (convert) {
	case CONV_FROMQP:
		mime_fromqp(&in, &out, 0);
		sz = _fwrite_td(out.s, out.l, f, dflags, prefix, prefixlen);
		free(out.s);
		break;
	case CONV_TOQP:
		sz = mime_write_toqp(&in, f, mustquote_body);
		break;
	case CONV_8BIT:
		sz = prefixwrite(in.s, in.l, f, prefix, prefixlen);
		break;
	case CONV_FROMB64:
		rest = NULL;
	case CONV_FROMB64_T:
		state = b64_decode(&out, &in, 0, rest);
		if ((sz = out.l) != 0) {
			if (state != OKAY)
				prefix = NULL, prefixlen = 0;
			sz = _fwrite_td(out.s, out.l, f, dflags & ~_TD_BUFCOPY,
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
		sz = _fwrite_td(out.s, out.l, f, dflags & TD_DELCTRL,
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
		sz = _fwrite_td(in.s, in.l, f, dflags, prefix, prefixlen);
	}
jleave:
	return sz;
}
