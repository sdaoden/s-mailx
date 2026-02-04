/*@ Implementation of cs.h: sep[arator based substring] division.
 *
 * Copyright (c) 2017 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_cs_sep
#define su_SOURCE
#define su_SOURCE_CS_SEP

#include "su/code.h"

#include "su/cs.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

char *
su_cs_sep_c(char **iolist, char sep, boole ignore_empty){
	char *base, c, *cp;
	NYD_IN;
	ASSERT_NYD_EXEC(iolist != NIL, base = NIL);

	for(base = *iolist; base != NIL; base = *iolist){
		/* Skip WS */
		while((c = *base) != '\0' && su_cs_is_space(c))
			++base;

		if((cp = su_cs_find_c(base, sep)) != NIL)
			*iolist = &cp[1];
		else{
			*iolist = NIL;
			cp = &base[su_cs_len(base)];
		}

		/* Chop WS */
		while(cp > base && su_cs_is_space(cp[-1]))
			--cp;
		*cp = '\0';

		if(*base != '\0' || !ignore_empty)
			break;
	}

	NYD_OU;
	return base;
}

char *
su_cs_sep_escable_c(char **iolist, char sep, boole ignore_empty){
	char *cp, c, *base;
	boole isesc, anyesc;
	NYD_IN;
	ASSERT_NYD_EXEC(iolist != NIL, base = NIL);

	for(base = *iolist; base != NIL; base = *iolist){
		/* Skip WS */
		while((c = *base) != '\0' && su_cs_is_space(c))
			++base;

		/* Do not recognize escaped sep characters, keep track of whether we
		 * have seen any such tuple along the way */
		for(isesc = anyesc = FAL0, cp = base;; ++cp){
			if(UNLIKELY((c = *cp) == '\0')){
				*iolist = NIL;
				break;
			}else if(!isesc){
				if(c == sep){
					*iolist = &cp[1];
					break;
				}
				isesc = (c == '\\');
			}else{
				isesc = FAL0;
				anyesc |= (c == sep);
			}
		}

		/* Chop WS */
		while(cp > base && su_cs_is_space(cp[-1]))
			--cp;
		*cp = '\0';

		/* Need to strip reverse solidus escaping sep's? */
		if(*base != '\0' && anyesc){
			char *ins;

			for(ins = cp = base;; ++ins)
				if((c = *cp) == '\\' && cp[1] == sep){
					*ins = sep;
					cp += 2;
				}else if((*ins = c) == '\0')
					break;
				else
					++cp;
		}

		if(*base != '\0' || !ignore_empty)
			break;
	}

	NYD_OU;
	return base;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CS_SEP
/* s-itt-mode */
