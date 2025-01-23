/*@ Implementation of cs.h: basic tools, like copy etc.
 *@ TODO ASM optimization hook (like mem tools).
 *
 * Copyright (c) 2017 - 2025 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#define su_FILE su_cs_tools
#define su_SOURCE
#define su_SOURCE_CS_TOOLS

#include "su/code.h"

#include "su/cs.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

sz
su_cs_cmp(char const *cp1, char const *cp2){
	sz rv;
	NYD_IN;
	ASSERT_NYD_EXEC(cp1 != NIL, rv = (cp2 == NIL) ? 0 : -1);
	ASSERT_NYD_EXEC(cp2 != NIL, rv = 1);

	for(;;){
		char c1;

		rv = c1 = *cp1++;
		rv -= *cp2++;
		if(rv != 0 || c1 == '\0')
			break;
	}

	NYD_OU;
	return rv;
}

sz
su_cs_cmp_n(char const *cp1, char const *cp2, uz n){
	sz rv;
	NYD_IN;
	ASSERT_NYD_EXEC(cp1 != NIL, rv = (cp2 == NIL) ? 0 : -1);
	ASSERT_NYD_EXEC(cp2 != NIL, rv = 1);

	for(rv = 0; n != 0; --n){
		char c1;

		rv = c1 = *cp1++;
		rv -= *cp2++;
		if(rv != 0 || c1 == '\0')
			break;
	}

	NYD_OU;
	return rv;
}

char *
su_cs_copy_n(char *dst, char const *src, uz n){
	NYD_IN;
	ASSERT_NYD(n == 0 || dst != NIL);
	ASSERT_NYD_EXEC(src != NIL, *dst = '\0');

	if(LIKELY(n > 0)){
		char *cp;

		cp = dst;
		do if((*cp++ = *src++) == '\0')
			goto jleave;
		while(--n > 0);
		*--cp = '\0';
	}

	dst = NIL;
jleave:
	NYD_OU;
	return dst;
}

uz
su_cs_len(char const *cp){
	char const *cp_base;
	NYD_IN;
	ASSERT_NYD_EXEC(cp != NIL, cp_base = cp);

	for(cp_base = cp; *cp != '\0'; ++cp)
		;

	NYD_OU;
	return P2UZ(cp - cp_base);
}

char *
su_cs_pcopy(char *dst, char const *src){
	NYD_IN;
	ASSERT_NYD(dst != NIL);
	ASSERT_NYD_EXEC(src != NIL, *dst = '\0');

	while((*dst = *src++) != '\0')
		++dst;

	NYD_OU;
	return dst;
}

char *
su_cs_pcopy_n(char *dst, char const *src, uz n){
	NYD_IN;
	ASSERT_NYD(n == 0 || dst != NIL);
	ASSERT_NYD_EXEC(src != NIL, *dst = '\0');

	if(LIKELY(n > 0)){
		do{
			if((*dst = *src++) == '\0')
				goto jleave;
			++dst;
		}while(--n > 0);
		*--dst = '\0';
	}

	dst = NIL;
jleave:
	NYD_OU;
	return dst;
}

char *
su_cs_squeeze(char *cp){
	uz wscnt;
	char *cp_base, *dst, c;
	NYD_IN;
	ASSERT_NYD_EXEC(cp != NIL, cp_base = NIL);

	dst = cp_base = cp;

	for(wscnt = 0; (c = *cp++) != '\0';){
		if(!su_cs_is_space(c))
			wscnt = 0;
		else if(wscnt++ != 0)
			continue;
		else
			c = ' ';
		*dst++ = c;
	}
	*dst = '\0';

	NYD_OU;
	return cp_base;
}

char *
su_cs_trim(char *cp){
	char *cp_base;
	NYD_IN;
	ASSERT_NYD_EXEC(cp != NIL, cp_base = NIL);

	while(su_cs_is_space(*cp))
		++cp;

	cp_base = cp;

	cp += su_cs_len(cp);
	while(cp > cp_base && su_cs_is_space(*--cp))
		*cp = '\0';

	NYD_OU;
	return cp_base;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_TOOLS
/* s-itt-mode */
