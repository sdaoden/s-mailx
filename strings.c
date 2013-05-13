/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auto-reclaimed string allocation and support routines that build on top of
 *@ them.  Strings handed out by those are reclaimed at the top of the command
 *@ loop each time, so they need not be freed.
 *@ And below this series we do collect all other plain string support routines
 *@ in here, including those which use normal heap memory.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#include "rcv.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#ifdef HAVE_WCTYPE_H
# include <wctype.h>
#endif
#ifdef HAVE_WCWIDTH
# include <wchar.h>
#endif

#include "extern.h"
#ifdef USE_MD5
# include "md5.h"
#endif

/*
 * Allocate SBUFFER_SIZE chunks and keep them in a singly linked list, but
 * release all except the first two in sreset(), because other allocations are
 * performed and the underlaying allocator should have the possibility to
 * reorder stuff and possibly even madvise(2), so that S-nail(1) integrates
 * neatly into the system.
 * To relax stuff further, especially in non-interactive, i.e., send mode, do
 * not even allocate the first buffer, but let that be a builtin DATA section
 * one that is rather small, yet sufficient for send mode to *never* even
 * perform a single dynamic allocation (from our stringdope point of view).
 *
 * If allocations larger than SHUGE_CUTLIMIT come in, smalloc() them directly
 * instead and store them in an extra list that is released whenever sreset()
 * is called.
 */

union __align__ {
	char	*cp;
	size_t	sz;
	ul_it	ul;
};
#define SALIGN		(sizeof(union __align__) - 1)

CTA(ISPOW2(SALIGN + 1));

struct b_base {
	struct buffer	*_next;
	char		*_bot;		/* For spreserve() */
	char		*_max;		/* Max usable byte */
	char		*_caster;	/* NULL if full */
};

/* Single instance builtin buffer, DATA */
struct b_bltin {
	struct b_base	b_base;
	char		b_buf[SBUFFER_BUILTIN - sizeof(struct b_base)];
};
#define SBLTIN_SIZE	SIZEOF_FIELD(struct b_bltin, b_buf)

/* Dynamically allocated buffers */
struct b_dyn {
	struct b_base	b_base;
	char		b_buf[SBUFFER_SIZE - sizeof(struct b_base)];
};
#define SDYN_SIZE	SIZEOF_FIELD(struct b_dyn, b_buf)

struct buffer {
	struct b_base	b;
	char		b_buf[VFIELD_SIZE(SALIGN + 1)];
};

struct huge {
	struct huge	*h_prev;
	char		h_buf[VFIELD_SIZE(SALIGN + 1)];
};
#define SHUGE_CALC_SIZE(S) \
	((sizeof(struct huge) - VFIELD_SIZEOF(struct huge, h_buf)) + (S))

static struct b_bltin	_builtin_buf;
static struct buffer	*_buf_head, *_buf_list, *_buf_server;
static struct huge	*_huge_list;

#ifdef HAVE_ASSERTS
size_t	_all_cnt, _all_cycnt, _all_cycnt_max,
	_all_size, _all_cysize, _all_cysize_max, _all_min, _all_max, _all_wast,
	_all_bufcnt, _all_cybufcnt, _all_cybufcnt_max,
	_all_hugecnt, _all_cyhugecnt, _all_cyhugecnt_max,
	_all_resetreqs, _all_resets;
#endif

/*
 * Allocate size more bytes of space and return the address of the
 * first byte to the caller.  An even number of bytes are always
 * allocated so that the space will always be on a word boundary.
 */
void *
salloc(size_t size)
{
#ifdef HAVE_ASSERTS
	size_t orig_size = size;
#endif
	union {struct buffer *b; struct huge *h; char *cp;} u;
	char *x, *y, *z;

	if (size == 0)
		++size;
	size += SALIGN;
	size &= ~SALIGN;

#ifdef HAVE_ASSERTS
	++_all_cnt;
	++_all_cycnt;
	_all_cycnt_max = MAX(_all_cycnt_max, _all_cycnt);
	_all_size += size;
	_all_cysize += size;
	_all_cysize_max = MAX(_all_cysize_max, _all_cysize);
	_all_min = _all_max == 0 ? size : MIN(_all_min, size);
	_all_max = MAX(_all_max, size);
	_all_wast += size - orig_size;
#endif

	if (size > SHUGE_CUTLIMIT)
		goto jhuge;

	if ((u.b = _buf_server) != NULL)
		goto jumpin;
jredo:
	for (u.b = _buf_head; u.b != NULL; u.b = u.b->b._next) {
jumpin:		x = u.b->b._caster;
		if (x == NULL) {
			if (u.b == _buf_server) {
				if (u.b == _buf_head &&
						(u.b = _buf_head->b._next)
						!= NULL) {
					_buf_server = u.b;
					goto jumpin;
				}
				_buf_server = NULL;
				goto jredo;
			}
			continue;
		}
		y = x + size;
		z = u.b->b._max;
		if (y <= z) {
			/*
			 * Alignment is the one thing, the other is what is
			 * usually allocated, and here about 40 bytes seems to
			 * be a good cut to avoid non-usable non-NULL casters
			 */
			u.b->b._caster = (y + 42+16 >= z) ? NULL : y;
			u.cp = x;
			goto jleave;
		}
	}

	if (_buf_head == NULL) {
		struct b_bltin *b = &_builtin_buf;
		b->b_base._max = b->b_buf + sizeof(b->b_buf) - 1;
		_buf_head = (struct buffer*)b;
		u.b = _buf_head;
	} else {
#ifdef HAVE_ASSERTS
		++_all_bufcnt;
		++_all_cybufcnt;
		_all_cybufcnt_max = MAX(_all_cybufcnt_max, _all_cybufcnt);
#endif
		u.b = (struct buffer*)smalloc(sizeof(struct b_dyn));
		u.b->b._max = u.b->b_buf + SDYN_SIZE - 1;
	}
	if (_buf_list != NULL)
		_buf_list->b._next = u.b;
	_buf_server = _buf_list = u.b;
	u.b->b._next = NULL;
	u.b->b._caster = (u.b->b._bot = u.b->b_buf) + size;
	u.cp = u.b->b._bot;
jleave:
	return (u.cp);

jhuge:
#ifdef HAVE_ASSERTS
	++_all_hugecnt;
	++_all_cyhugecnt;
	_all_cyhugecnt_max = MAX(_all_cyhugecnt_max, _all_cyhugecnt);
#endif
	u.h = smalloc(SHUGE_CALC_SIZE(size));
	u.h->h_prev = _huge_list;
	_huge_list = u.h;
	u.cp = u.h->h_buf;
	goto jleave;
}

void *
csalloc(size_t nmemb, size_t size)
{
	void *vp;

	size *= nmemb;
	vp = salloc(size);
	memset(vp, 0, size);
	return (vp);
}

/*
 * Reset the string area to be empty.
 * Called to free all strings allocated since last reset.
 */
void 
sreset(void)
{
	union {struct buffer *b; struct huge *h;} u;

#ifdef HAVE_ASSERTS
	++_all_resetreqs;
#endif
	if (noreset)
		goto jleave;

#ifdef HAVE_ASSERTS
	_all_cycnt = _all_cysize = _all_cyhugecnt = 0;
	_all_cybufcnt = (_buf_head != NULL && _buf_head->b._next != NULL);
	++_all_resets;
#endif

	for (u.h = _huge_list; u.h != NULL;) {
		struct huge *tmp = u.h;
		u.h = u.h->h_prev;
		free(tmp);
	}
	_huge_list = NULL;

	if ((u.b = _buf_head) != NULL) {
		struct buffer *b = u.b;
		b->b._caster = b->b._bot;
#ifdef HAVE_ASSERTS
		memset(b->b._caster, 0377,
			(size_t)(b->b._max - b->b._caster));
#endif
		_buf_server = b;
		if ((u.b = u.b->b._next) != NULL) {
			b = u.b;
			b->b._caster = b->b._bot;
#ifdef HAVE_ASSERTS
			memset(b->b._caster, 0377,
				(size_t)(b->b._max - b->b._caster));
#endif
			for (u.b = u.b->b._next; u.b != NULL;) {
				struct buffer *b2 = u.b->b._next;
				free(u.b);
				u.b = b2;
			}
		}
		_buf_list = b;
		b->b._next = NULL;
	}

#ifdef HAVE_ASSERTS
	smemreset();
#endif
jleave:	;
}

/*
 * Make the string area permanent.
 * Meant to be called in main, after initialization.
 */
void 
spreserve(void)
{
	struct buffer *b;

	for (b = _buf_head; b != NULL; b = b->b._next)
		b->b._bot = b->b._caster;
}

#ifdef HAVE_ASSERTS
int
sstats(void *v)
{
	(void)v;
	printf("String usage statistics (cycle means one sreset() cycle):\n"
		"  Buffer allocs ever/max simultan. : %lu/%lu\n"
		"  Buffer size of builtin(1)/dynamic: %lu/%lu\n"
		"  Overall alloc count/bytes        : %lu/%lu\n"
		"  Alloc bytes min/max/align wastage: %lu/%lu/%lu\n"
		"  Hugealloc count overall/cycle    : %lu/%lu (cutlimit: %lu)\n"
		"  sreset() cycles                  : %lu (%lu performed)\n"
		"  Cycle maximums: alloc count/bytes: %lu/%lu\n",
		(ul_it)_all_bufcnt, (ul_it)_all_cybufcnt_max,
		(ul_it)SBLTIN_SIZE, (ul_it)SDYN_SIZE,
		(ul_it)_all_cnt, (ul_it)_all_size,
		(ul_it)_all_min, (ul_it)_all_max, (ul_it)_all_wast,
		(ul_it)_all_hugecnt, (ul_it)_all_cyhugecnt_max,
			(ul_it)SHUGE_CUTLIMIT,
		(ul_it)_all_resetreqs, (ul_it)_all_resets,
		(ul_it)_all_cycnt_max, (ul_it)_all_cysize_max);
	return (0);
}
#endif

/*
 * Return a pointer to a dynamic copy of the argument.
 */
char *
savestr(const char *str)
{
	size_t size = strlen(str) + 1;
	char *news = salloc(size);
	memcpy(news, str, size);
	return (news);
}

/*
 * Return new string copy of a non-terminated argument.
 */
char *
savestrbuf(const char *sbuf, size_t sbuf_len)
{
	char *news = salloc(sbuf_len + 1);
	memcpy(news, sbuf, sbuf_len);
	news[sbuf_len] = 0;
	return (news);
}

/*
 * Make a copy of new argument incorporating old one.
 */
char *
save2str(const char *str, const char *old)
{
	size_t newsize = strlen(str) + 1, oldsize = old ? strlen(old) + 1 : 0;
	char *news = salloc(newsize + oldsize);
	if (oldsize) {
		memcpy(news, old, oldsize);
		news[oldsize - 1] = ' ';
	}
	memcpy(news + oldsize, str, newsize);
	return (news);
}

char *
savecat(char const *s1, char const *s2)
{
	size_t l1 = strlen(s1), l2 = strlen(s2);
	char *news = salloc(l1 + l2 + 1);
	memcpy(news + 0, s1, l1);
	memcpy(news + l1, s2, l2);
	news[l1 + l2] = '\0';
	return (news);
}

/*
 * Support routines, auto-reclaimed storage
 */

#define	Hexchar(n)	((n)>9 ? (n)-10+'A' : (n)+'0')
#define	hexchar(n)	((n)>9 ? (n)-10+'a' : (n)+'0')

#ifdef USE_MD5
char *
cram_md5_string(char const *user, char const *pass, char const *b64)
{
	struct str in, out;
	char digest[16], *cp;
	size_t lh, lu;

	out.s = NULL;
	in.s = UNCONST(b64);
	in.l = strlen(in.s);
	(void)b64_decode(&out, &in, NULL);
	assert(out.s != NULL);

	hmac_md5((unsigned char*)out.s, out.l, UNCONST(pass), strlen(pass),
		digest);
	free(out.s);
	cp = md5tohex(digest);

	lh = strlen(cp);
	lu = strlen(user);
	in.l = lh + 1 + lu;
	in.s = ac_alloc(lh + lu + 1 + 1);
	memcpy(in.s, user, lu);
	in.s[lu] = ' ';
	memcpy(in.s + lu + 1, cp, lh);
	(void)b64_encode(&out, &in, B64_SALLOC|B64_CRLF);
	ac_free(in.s);
	return out.s;
}
#endif /* USE_MD5 */

char *
i_strdup(char const *src)
{
	size_t sz;
	char *dest;

	sz = strlen(src) + 1;
	dest = salloc(sz);
	i_strcpy(dest, src, sz);
	return (dest);
}

#ifdef USE_MD5
char *
md5tohex(void const *vp)
{
	char const *cp = vp;
	char *hex;
	int i;

	hex = salloc(33);
	for (i = 0; i < 16; i++) {
		hex[2 * i] = hexchar((cp[i] & 0xf0) >> 4);
		hex[2 * i + 1] = hexchar(cp[i] & 0x0f);
	}
	hex[32] = '\0';
	return hex;
}
#endif /* USE_MD5 */

char *
protbase(char const *cp)
{
	char *n = salloc(strlen(cp) + 1), *np = n;

	while (*cp) {
		if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
			*np++ = *cp++;
			*np++ = *cp++;
			*np++ = *cp++;
		} else if (cp[0] == '/')
			break;
		else
			*np++ = *cp++;
	}
	*np = '\0';
	return (n);
}

char *
urlxenc(char const *cp) /* XXX */
{
	char	*n, *np;

	np = n = salloc(strlen(cp) * 3 + 1);
	while (*cp) {
		if (alnumchar(*cp) || *cp == '_' || *cp == '@' ||
				(np > n && (*cp == '.' || *cp == '-' ||
						*cp == ':')))
			*np++ = *cp;
		else {
			*np++ = '%';
			*np++ = Hexchar((*cp&0xf0) >> 4);
			*np++ = Hexchar(*cp&0x0f);
		}
		cp++;
	}
	*np = '\0';
	return n;
}

char *
urlxdec(char const *cp) /* XXX */
{
	char *n, *np;

	np = n = salloc(strlen(cp) + 1);
	while (*cp) {
		if (cp[0] == '%' && cp[1] && cp[2]) {
			*np = (int)(cp[1]>'9'?cp[1]-'A'+10:cp[1]-'0') << 4;
			*np++ |= cp[2]>'9'?cp[2]-'A'+10:cp[2]-'0';
			cp += 3;
		} else
			*np++ = *cp++;
	}
	*np = '\0';
	return (n);
}

struct str *
str_concat_csvl(struct str *self, ...) /* XXX onepass maybe better here */
{
	va_list vl;
	size_t l;
	char const *cs;

	va_start(vl, self);
	for (l = 0; (cs = va_arg(vl, char const*)) != NULL;)
		l += strlen(cs);
	va_end(vl);

	self->l = l;
	self->s = salloc(l + 1);

	va_start(vl, self);
	for (l = 0; (cs = va_arg(vl, char const*)) != NULL;) {
		size_t i = strlen(cs);
		memcpy(self->s + l, cs, i);
		l += i;
	}
	self->s[l] = '\0';
	va_end(vl);
	return (self);
}

/*
 * Routines that are not related to auto-reclaimed storage follow.
 */

uc_it const	class_char[] = {
/*	000 nul	001 soh	002 stx	003 etx	004 eot	005 enq	006 ack	007 bel	*/
	C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,
/*	010 bs 	011 ht 	012 nl 	013 vt 	014 np 	015 cr 	016 so 	017 si 	*/
	C_CNTRL,C_BLANK,C_WHITE,C_SPACE,C_SPACE,C_SPACE,C_CNTRL,C_CNTRL,
/*	020 dle	021 dc1	022 dc2	023 dc3	024 dc4	025 nak	026 syn	027 etb	*/
	C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,
/*	030 can	031 em 	032 sub	033 esc	034 fs 	035 gs 	036 rs 	037 us 	*/
	C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,C_CNTRL,
/*	040 sp 	041  ! 	042  " 	043  # 	044  $ 	045  % 	046  & 	047  ' 	*/
	C_BLANK,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	050  ( 	051  ) 	052  * 	053  + 	054  , 	055  - 	056  . 	057  / 	*/
	C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	060  0 	061  1 	062  2 	063  3 	064  4 	065  5 	066  6 	067  7 	*/
	C_OCTAL,C_OCTAL,C_OCTAL,C_OCTAL,C_OCTAL,C_OCTAL,C_OCTAL,C_OCTAL,
/*	070  8 	071  9 	072  : 	073  ; 	074  < 	075  = 	076  > 	077  ? 	*/
	C_DIGIT,C_DIGIT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	100  @ 	101  A 	102  B 	103  C 	104  D 	105  E 	106  F 	107  G 	*/
	C_PUNCT,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,
/*	110  H 	111  I 	112  J 	113  K 	114  L 	115  M 	116  N 	117  O 	*/
	C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,
/*	120  P 	121  Q 	122  R 	123  S 	124  T 	125  U 	126  V 	127  W 	*/
	C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,C_UPPER,
/*	130  X 	131  Y 	132  Z 	133  [ 	134  \ 	135  ] 	136  ^ 	137  _ 	*/
	C_UPPER,C_UPPER,C_UPPER,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,
/*	140  ` 	141  a 	142  b 	143  c 	144  d 	145  e 	146  f 	147  g 	*/
	C_PUNCT,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,
/*	150  h 	151  i 	152  j 	153  k 	154  l 	155  m 	156  n 	157  o 	*/
	C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,
/*	160  p 	161  q 	162  r 	163  s 	164  t 	165  u 	166  v 	167  w 	*/
	C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,C_LOWER,
/*	170  x 	171  y 	172  z 	173  { 	174  | 	175  } 	176  ~ 	177 del	*/
	C_LOWER,C_LOWER,C_LOWER,C_PUNCT,C_PUNCT,C_PUNCT,C_PUNCT,C_CNTRL
};

int
anyof(char const *s1, char const *s2)
{
	for (; *s1 != '\0'; ++s1)
		if (strchr(s2, *s1))
			break;
	return (*s1 != '\0');
}

char *
strcomma(char **iolist, int ignore_empty)
{
	char *base, *cp;

	for (base = *iolist; base != NULL; base = *iolist) {
		while (*base != '\0' && blankspacechar(*base))
			++base;
		cp = strchr(base, ',');
		if (cp != NULL)
			*iolist = cp + 1;
		else {
			*iolist = NULL;
			cp = base + strlen(base);
		}
		while (cp > base && blankspacechar(cp[-1]))
			--cp;
		*cp = '\0';
		if (*base != '\0' || ! ignore_empty)
			break;
	}
	return (base);
}

void
i_strcpy(char *dest, const char *src, size_t size)
{
	if (size)
		for (;; ++dest, ++src)
			if ((*dest = lowerconv(*src)) == '\0')
				break;
			else if (--size == 0) {
				*dest = '\0';
				break;
			}
}

int
is_prefix(char const *as1, char const *as2)
{
	char c;
	for (; (c = *as1) == *as2 && c != '\0'; ++as1, ++as2)
		if ((c = *as2) == '\0')
			break;
	return (c == '\0');
}

char const *
last_at_before_slash(char const *sp)
{
	char const *cp;
	char c;

	for (cp = sp; (c = *cp) != '\0'; ++cp)
		if (c == '/')
			break;
	while (cp > sp && *--cp != '@')
		;
	return (*cp == '@' ? cp : NULL);
}

void
makelow(char *cp) /* TODO isn't that crap? --> */
{
#if defined HAVE_MBTOWC && defined HAVE_WCTYPE_H
	if (mb_cur_max > 1) {
		char *tp = cp;
		wchar_t wc;
		int len;

		while (*cp) {
			len = mbtowc(&wc, cp, mb_cur_max);
			if (len < 0)
				*tp++ = *cp++;
			else {
				wc = towlower(wc);
				if (wctomb(tp, wc) == len)
					tp += len, cp += len;
				else
					*tp++ = *cp++; /* <-- at least here */
			}
		}
	} else
#endif
	{
		do
			*cp = tolower((uc_it)*cp);
		while (*cp++);
	}
}

int
substr(char const *str, char const *sub)
{
	char const *cp, *backup;

	cp = sub;
	backup = str;
	while (*str && *cp) {
#if defined HAVE_MBTOWC && defined HAVE_WCTYPE_H
		if (mb_cur_max > 1) {
			wchar_t c, c2;
			int sz;

			if ((sz = mbtowc(&c, cp, mb_cur_max)) < 0)
				goto singlebyte;
			cp += sz;
			if ((sz = mbtowc(&c2, str, mb_cur_max)) < 0)
				goto singlebyte;
			str += sz;
			c = towupper(c);
			c2 = towupper(c2);
			if (c != c2) {
				if ((sz = mbtowc(&c, backup, mb_cur_max)) > 0) {
					backup += sz;
					str = backup;
				} else
					str = ++backup;
				cp = sub;
			}
		} else
#endif
		{
			int c, c2;

#if defined HAVE_MBTOWC && defined HAVE_WCTYPE_H
	singlebyte:
#endif
			c = *cp++ & 0377;
			if (islower(c))
				c = toupper(c);
			c2 = *str++ & 0377;
			if (islower(c2))
				c2 = toupper(c2);
			if (c != c2) {
				str = ++backup;
				cp = sub;
			}
		}
	}
	return *cp == '\0';
}

#ifndef HAVE_SNPRINTF
int
snprintf(char *str, size_t size, const char *format, ...) /* XXX DANGER! */
{
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = vsprintf(str, format, ap);
	va_end(ap);
	if (ret < 0)
		ret = strlen(str);
	return ret;
}
#endif

char *
sstpcpy(char *dst, char const *src)
{
	while ((*dst = *src++) != '\0')
		dst++;
	return (dst);
}

char *
(sstrdup)(char const *cp SMALLOC_DEBUG_ARGS)
{
	char *dp = NULL;

	if (cp != NULL) {
		size_t l = strlen(cp) + 1;
		dp = (smalloc)(l SMALLOC_DEBUG_ARGSCALL);
		memcpy(dp, cp, l);
	}
	return dp;
}

int
asccasecmp(char const *s1, char const *s2)
{
	int cmp;

	for (;;) {
		char c1 = *s1++, c2 = *s2++;
		if ((cmp = lowerconv(c1) - lowerconv(c2)) != 0 || c1 == '\0')
			break;
	}
	return cmp;
}

int
ascncasecmp(char const *s1, char const *s2, size_t sz)
{
	int cmp = 0;

	while (sz-- > 0) {
		char c1 = *s1++, c2 = *s2++;
		if ((cmp = lowerconv(c1) - lowerconv(c2)) != 0 || c1 == '\0')
			break;
	}
	return cmp;
}

char const *
asccasestr(char const *haystack, char const *xneedle)
{
	char *needle = NULL, *NEEDLE;
	size_t i, sz;

	sz = strlen(xneedle);
	if (sz == 0)
		goto jleave;

	needle = ac_alloc(sz);
	NEEDLE = ac_alloc(sz);
	for (i = 0; i < sz; i++) {
		needle[i] = lowerconv(xneedle[i]);
		NEEDLE[i] = upperconv(xneedle[i]);
	}

	while (*haystack) {
		if (*haystack == *needle || *haystack == *NEEDLE) {
			for (i = 1; i < sz; i++)
				if (haystack[i] != needle[i] &&
						haystack[i] != NEEDLE[i])
					break;
			if (i == sz)
				goto jleave;
		}
		haystack++;
	}
	haystack = NULL;
jleave:
	if (needle != NULL) {
		ac_free(NEEDLE);
		ac_free(needle);
	}
	return haystack;
}

struct str *
(n_str_dup)(struct str *self, struct str const *t SMALLOC_DEBUG_ARGS)
{
	if (t != NULL && t->l > 0) {
		self->l = t->l;
		self->s = (srealloc)(self->s, t->l + 1 SMALLOC_DEBUG_ARGSCALL);
		memcpy(self->s, t->s, t->l);
	} else
		self->l = 0;
	return self;
}

struct str *
(n_str_add_buf)(struct str *self, char const *buf, size_t buflen
	SMALLOC_DEBUG_ARGS)
{
	if (buflen != 0) {
		size_t sl = self->l;
		self->l = sl + buflen;
		self->s = (srealloc)(self->s, self->l+1 SMALLOC_DEBUG_ARGSCALL);
		memcpy(self->s + sl, buf, buflen);
	}
	return self;
}

#ifdef HAVE_ICONV
static void _ic_toupper(char *dest, char const *src);
static void _ic_stripdash(char *p);

static void
_ic_toupper(char *dest, const char *src)
{
	do
		*dest++ = upperconv(*src);
	while (*src++);
}

static void
_ic_stripdash(char *p)
{
	char *q = p;

	do
		if (*(q = p) != '-')
			q++;
	while (*p++);
}

iconv_t
n_iconv_open(char const *tocode, char const *fromcode)
{
	iconv_t id;
	char *t, *f;

	if ((id = iconv_open(tocode, fromcode)) != (iconv_t)-1)
		return id;

	/*
	 * Remove the "iso-" prefixes for Solaris.
	 */
	if (ascncasecmp(tocode, "iso-", 4) == 0)
		tocode += 4;
	else if (ascncasecmp(tocode, "iso", 3) == 0)
		tocode += 3;
	if (ascncasecmp(fromcode, "iso-", 4) == 0)
		fromcode += 4;
	else if (ascncasecmp(fromcode, "iso", 3) == 0)
		fromcode += 3;
	if (*tocode == '\0' || *fromcode == '\0')
		return (iconv_t) -1;
	if ((id = iconv_open(tocode, fromcode)) != (iconv_t)-1)
		return id;
	/*
	 * Solaris prefers upper-case charset names. Don't ask...
	 */
	t = salloc(strlen(tocode) + 1);
	_ic_toupper(t, tocode);
	f = salloc(strlen(fromcode) + 1);
	_ic_toupper(f, fromcode);
	if ((id = iconv_open(t, f)) != (iconv_t)-1)
		return id;
	/*
	 * Strip dashes for UnixWare.
	 */
	_ic_stripdash(t);
	_ic_stripdash(f);
	if ((id = iconv_open(t, f)) != (iconv_t)-1)
		return id;
	/*
	 * Add your vendor's sillynesses here.
	 */

	/*
	 * If the encoding names are equal at this point, they
	 * are just not understood by iconv(), and we cannot
	 * sensibly use it in any way. We do not perform this
	 * as an optimization above since iconv() can otherwise
	 * be used to check the validity of the input even with
	 * identical encoding names.
	 */
	if (strcmp(t, f) == 0)
		errno = 0;
	return (iconv_t)-1;
}

void
n_iconv_close(iconv_t cd)
{
	iconv_close(cd);
	if (cd == iconvd)
		iconvd = (iconv_t)-1;
}

void
n_iconv_reset(iconv_t cd)
{
	(void)iconv(cd, NULL, NULL, NULL, NULL);
}

/*
 * (2012-09-24: export and use it exclusively to isolate prototype problems
 * (*inb* is 'char const **' except in POSIX) in a single place.
 * GNU libiconv even allows for configuration time const/non-const..
 * In the end it's an ugly guess, but we can't do better since make(1) doesn't
 * support compiler invocations which bail on error, so no -Werror.
 */
/* Citrus project? */
# if defined _ICONV_H_ && defined __ICONV_F_HIDE_INVALID
  /* DragonFly 3.2.1 is special */
#  ifdef __DragonFly__
#   define __INBCAST(S)	(char ** __restrict__)UNCONST(S)
#  else
#   define __INBCAST(S)	(char const **)UNCONST(S)
#  endif
# endif
# ifndef __INBCAST
#  define __INBCAST(S)	(char **)UNCONST(S)
# endif

int
n_iconv_buf(iconv_t cd, char const **inb, size_t *inbleft,/*XXX redo iconv use*/
	char **outb, size_t *outbleft, bool_t skipilseq)
{
	int err = 0;

	for (;;) {
		size_t sz = iconv(cd, __INBCAST(inb), inbleft, outb, outbleft);
		if (sz != (size_t)-1)
			break;
		err = errno;
		if (! skipilseq || err != EILSEQ)
			break;
		if (*inbleft > 0) {
			(*inb)++;
			(*inbleft)--;
		} else if (*outbleft > 0) {
			**outb = '\0';
			break;
		}
		if (*outbleft > 2) {
			(*outb)[0] = '[';
			(*outb)[1] = '?';
			(*outb)[2] = ']';
			(*outb) += 3;
			(*outbleft) -= 3;
		} else {
			err = E2BIG;
			break;
		}
		err = 0;
	}
	return err;
}
# undef __INBCAST

int
n_iconv_str(iconv_t cd, struct str *out, struct str const *in,
	struct str *in_rest_or_null, bool_t skipilseq)
{
	int err = 0;
	char *obb = out->s, *ob;
	char const *ib;
	size_t olb = out->l, ol, il;

	ol = in->l;
	ol = (ol << 1) - (ol >> 4);
	if (olb < ol) {
		olb = ol;
		goto jrealloc;
	}

	for (;;) {
		ib = in->s;
		il = in->l;
		ob = obb;
		ol = olb;
		err = n_iconv_buf(cd, &ib, &il, &ob, &ol, skipilseq);
		if (err == 0 || err != E2BIG)
			break;
		err = 0;
		olb += in->l;
jrealloc:	obb = srealloc(obb, olb);
	}

	if (in_rest_or_null != NULL) {
		in_rest_or_null->s = UNCONST(ib);
		in_rest_or_null->l = il;
	}
	out->s = obb;
	out->l = olb - ol;
	return err;
}
#endif /* HAVE_ICONV */
