/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012, 2013 Steffen "Daode" Nurpmeso.
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

/*
 * Mail -- a mail program
 *
 * Auto-reclaimed string allocation and support routines that build on top of
 * them.  Strings handed out by those are reclaimed at the top of the command
 * loop each time, so they need not be freed.
 * And below this series we do collect all other plain string support routines
 * in here, including those which use normal heap memory.
 */

#include "rcv.h"

#include <stdarg.h>

#include "extern.h"

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
