/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 Steffen "Daode" Nurpmeso.
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
 * String allocation routines and support routines that build on top of them.
 * Strings handed out here are reclaimed at the top of the command
 * loop each time, so they need not be freed.
 */

#include "rcv.h"

#include <stdarg.h>

#include "extern.h"

/*
 * STRINGSIZE is the minimum size of a chunk we smalloc(), and we have NSPACE
 * chunks each of which shifts the size by 1 (so take care for 32 bit overflow)
 */
#define STRINGSIZE	((unsigned) 4096)
#define NSPACE		19

static struct strings {
	char	*s_topFree;		/* Beginning of this area */
	char	*s_nextFree;		/* Next alloctable place here */
	unsigned s_nleft;		/* Number of bytes left here */
} stringdope[NSPACE];

#ifdef HAVE_ASSERTS
size_t	_all_cnt, _all_cycnt, _all_cycnt_max,
	_all_size, _all_cysize, _all_cysize_max,
	_all_min, _all_max, _all_resetreqs, _all_resets;
#endif

/*
 * Allocate size more bytes of space and return the address of the
 * first byte to the caller.  An even number of bytes are always
 * allocated so that the space will always be on a word boundary.
 * The string spaces are of exponentially increasing size, to satisfy
 * the occasional user with enormous string size requests.
 */
void *
salloc(size_t size)
{
	char *t;
	unsigned int s;
	struct strings *sp;
	int string_index;

	s = size;
	s += (sizeof (char *) - 1);
	s &= ~(sizeof (char *) - 1);
#ifdef HAVE_ASSERTS
	++_all_cnt;
	++_all_cycnt;
	_all_cycnt_max = smax(_all_cycnt_max, _all_cycnt);
	_all_size += size;
	_all_cysize += size;
	_all_cysize_max = smax(_all_cysize_max, _all_cysize);
	_all_min = _all_max == 0 ? size : smin(_all_min, size);
	_all_max = smax(_all_max, size);
#endif

	string_index = 0;
	for (sp = &stringdope[0]; sp < &stringdope[NSPACE]; sp++) {
		if (sp->s_topFree == NULL && (STRINGSIZE << string_index) >= s)
			break;
		if (sp->s_nleft >= s)
			break;
		string_index++;
	}
	if (sp >= &stringdope[NSPACE])
		panic(tr(195, "String too large"));
	if (sp->s_topFree == NULL) {
		string_index = sp - &stringdope[0];
		sp->s_topFree = smalloc(STRINGSIZE << string_index);
		sp->s_nextFree = sp->s_topFree;
		sp->s_nleft = STRINGSIZE << string_index;
	}
	sp->s_nleft -= s;
	t = sp->s_nextFree;
	sp->s_nextFree += s;
	return(t);
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
 * Called to free all strings allocated
 * since last reset.
 */
void 
sreset(void)
{
	struct strings *sp;
	int string_index;

#ifdef HAVE_ASSERTS
	_all_cycnt = _all_cysize = 0;
	++_all_resetreqs;
#endif
	if (noreset)
		return;
#ifdef HAVE_ASSERTS
	++_all_resets;
#endif
	string_index = 0;
	for (sp = &stringdope[0]; sp < &stringdope[NSPACE]; sp++) {
		if (sp->s_topFree == NULL)
			continue;
		sp->s_nextFree = sp->s_topFree;
		sp->s_nleft = STRINGSIZE << string_index;
		string_index++;
	}
}

/*
 * Make the string area permanent.
 * Meant to be called in main, after initialization.
 */
void 
spreserve(void)
{
	struct strings *sp;

	for (sp = &stringdope[0]; sp < &stringdope[NSPACE]; sp++)
		sp->s_topFree = NULL;
}

#ifdef HAVE_ASSERTS
int
sstats(void *v)
{
	struct strings *sp;
	(void)v;

	printf("String usage statistics:\n"
		"  Count/Bytes overall: %lu/%lu\n"
		"  Bytes min/max      : %lu/%lu\n"
		"  Cycle cnt/bytes max: %lu/%lu\n"
		"  sreset() requests  : %lu (%lu performed)\n"
		"  Slot overview:\n",
		(unsigned long)_all_cnt, (unsigned long)_all_size,
		(unsigned long)_all_min, (unsigned long)_all_max,
		(unsigned long)_all_cycnt_max, (unsigned long)_all_cysize_max,
		(unsigned long)_all_resetreqs, (unsigned long)_all_resets);

	for (sp = &stringdope[0]; sp < &stringdope[NSPACE]; sp++) {
		unsigned int i = (unsigned int)(sp - stringdope);
		if (sp->s_nextFree == NULL)
			continue;
		printf("    %2d: buf=%p caster=%p size=%u free=%u\n",
			i + 1, sp->s_topFree, sp->s_nextFree, STRINGSIZE << i,
			sp->s_nleft);
	}
	return (0);
}
#endif /* HAVE_ASSERTS */

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
	int newsize = strlen(str) + 1, oldsize = old ? strlen(old) + 1 : 0;
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
