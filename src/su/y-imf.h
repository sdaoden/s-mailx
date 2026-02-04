/*@ imf.h: internally shared interface.
 *
 * Copyright (c) 2024 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef a_IMF_H
# define a_IMF_H 1

# include "su/cs.h"
# include "su/mem.h"
# include "su/mem-bag.h"

#elif a_IMF_H == 1
# undef a_IMF_H
# define a_IMF_H 2

# ifdef su_HAVE_MEM_BAG_LOFI
#  define su__IMF_ALLOC(MBP,X) su_MEM_BAG_LOFI_ALLOCATE(MBP, X, 1, su_MEM_BAG_ALLOC_MAYFAIL)
# elif defined su_HAVE_MEM_BAG_AUTO
#  define su__IMF_ALLOC(MBP,X) su_MEM_BAG_AUTO_ALLOCATE(MBP, X, 1, su_MEM_BAG_ALLOC_MAYFAIL)
# else
#  error Needs one of su_HAVE_MEM_BAG_LOFI and su_HAVE_MEM_BAG_AUTO
# endif

/* Address context: whenever an address was parsed su_imf_addr is created and this is reset */
struct su__imf_actx{
	struct su__imf_x{
		char const *hd; /* Header field body content data rest */
		BITENUM(u32,su_imf_mode) mse; /* imf_mode, plus current imf_state */
		u32 group_display_name;
		u32 display_name;
		u32 locpar;
		u32 domain;
		u32 comm;
	} ac_;
	char *ac_group_display_name;
	char *ac_display_name;
	char *ac_locpar;
	char *ac_domain;
	char *ac_comm;
	char ac_dat[VFIELD_SIZE(0)]; /* Storage for any text (single chunk struct) */
};

/* Returns TRU1 if at least 1 WSP was skipped over, TRUM1 if *any* ws was seen, ie: it moved */
boole su__imf_skip_FWS(struct su__imf_actx *acp);

/* Returns TRUM1 if FWS was parsed *outside* of comments */
boole su__imf_s_CFWS(struct su__imf_actx *acp);

/* ASSERTs "at imf_c_DQUOTE()".  Skips to after the closing DQUOTE().
 * Returns NIL on invalid content / missing closing quote error (unless relaxed), ptr to after written byte otherwise */
char *su__imf_s_quoted_string(struct su__imf_actx *acp, char *buf);

#elif a_IMF_H == 2
# undef a_IMF_H
# define a_IMF_H 3

/*# undef su__IMF_ALLOC*/

#else
/* Do not bail to support MX OPT_AMALGAMATION=y! # error . */
#endif /* a_IMF_H */
/* s-itt-mode */
