/*@ Implementation of code.h: su_state_create().
 *
 * Copyright (c) 2021 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_core_create
#define su_SOURCE
#define su_SOURCE_CORE_CREATE

#include <su/random.h>

#if su_DVLDBGOR(1, 0)
# include <su/mem.h>
#endif

#include "su/internal.h" /* $(SU_SRCDIR) */

#include "su/code.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

s32
su_state_create(BITENUM(u32,su_state_create_flags) create_flags, char const *name_or_nil, uz flags, u32 estate){
	s32 rv;

	if((rv = su_state_create_core(name_or_nil, flags, estate)) != su_ERR_NONE)
		goto jleave;

#undef a_V1
#define a_V1(X) ((X) | su_STATE_CREATE_V1 | su_STATE_CREATE_ALL)

	if(create_flags & a_V1(su_STATE_CREATE_RANDOM)){
		if((rv = su_random_vsp_install(NIL, estate)) != su_ERR_NONE)
			goto jleave;
#if DVLDBGOR(1, 0)
		if(!(create_flags & a_V1(su__STATE_CREATE_RANDOM_MEM_FILLER)))
#endif
			if(!su_random_builtin_seed(TRU1)){
				rv = su_state_err(-su_ERR_CANCELED, estate, _("cannot seed random generator"));
				goto jleave;
			}
	}

	/* (*/
#if DVLDBGOR(1, 0)
	if(create_flags & a_V1(su__STATE_CREATE_RANDOM_MEM_FILLER)){
		u8 mf;

		if((rv = su_random_builtin_generate(&mf, sizeof(mf), estate)) != su_ERR_NONE)
			goto jleave;

		/* The patterns 0 and -1 are more likely to reveal problems */
		switch(mf & 0x3){
		case 0x0: mf = 0x00; break;
		case 0x1: mf = 0xFFu; break;
		default: break;
		}
		su_mem_set_conf(su_MEM_CONF_FILLER_SET, mf);
	}
#endif

#if defined su_HAVE_MD && !defined su_USECASE_MX
	if((create_flags & a_V1(su_STATE_CREATE_MD)) && (rv = su__md_init(estate)) != su_ERR_NONE)
		goto jleave;
#endif

#undef a_V1

jleave:
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CORE_CREATE
/* s-itt-mode */
