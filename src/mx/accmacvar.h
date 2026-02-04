/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Data structures shared in between accmacvar.c and make-okey-map.pl.
 *
 * Copyright (c) 2012 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

/* See okeys.h:enum okeys for description; notes:
 * - VF_BOOL: according to make-okey-map.pl detected ok_b_* name prefix
 * - VF_EXT_*: extended flags, not part of struct a_amv_var_map.avm_flags */
enum a_amv_var_flags{
	a_AMV_VF_NONE = 0,
	a_AMV_VF_BOOL = 1u<<0,

	a_AMV_VF_CHAIN = 1u<<1,
	a_AMV_VF_VIRT = 1u<<2,
	a_AMV_VF_VIP = 1u<<3,
	a_AMV_VF_RDONLY = 1u<<4,
	a_AMV_VF_NODEL = 1u<<5,
	a_AMV_VF_I3VAL = 1u<<6,
	a_AMV_VF_DEFVAL = 1u<<7,
	a_AMV_VF_IMPORT = 1u<<8,
	a_AMV_VF_ENV = 1u<<9,
	a_AMV_VF_NOLOPTS = 1u<<10,
	a_AMV_VF_NOTEMPTY = 1u<<11,
	/* TODO _VF_NUM, _VF_POSNUM: we also need 64-bit limit numbers! */
	a_AMV_VF_NUM = 1u<<12,
	a_AMV_VF_POSNUM = 1u<<13,
	a_AMV_VF_LOWER = 1u<<14,
	a_AMV_VF_OBSOLETE = 1u<<15,
	a_AMV_VF__MASK = (1u<<(15+1)) - 1,

	/* Indicates the instance is actually a variant of a _VF_CHAIN, it thus uses the a_amv_var_map of the base
	 * variable, but it is not the base itself and therefore care must be taken */
	a_AMV_VF_EXT_CHAIN = 1u<<22,
	a_AMV_VF_EXT_LOCAL = 1u<<23, /* `local' */
	a_AMV_VF_EXT_LINKED = 1u<<24, /* `environ' link'ed */
	a_AMV_VF_EXT_FROZEN = 1u<<25, /* Has been set by -S,.. */
	a_AMV_VF_EXT_FROZEN_UNSET = 1u<<26, /* ..and was used to unset a variable */
	a_AMV_VF_EXT__FROZEN_MASK = a_AMV_VF_EXT_FROZEN | a_AMV_VF_EXT_FROZEN_UNSET,
	a_AMV_VF_EXT__MASK = (1u<<(26+1)) - 1,
	/* All the flags that could be set for customs / `local' variables */
	a_AMV_VF_EXT__CUSTOM_MASK = a_AMV_VF_EXT_LOCAL | a_AMV_VF_EXT_LINKED | a_AMV_VF_EXT_FROZEN,
	a_AMV_VF_EXT__LOCAL_MASK = a_AMV_VF_EXT_LOCAL | a_AMV_VF_EXT_LINKED,
	a_AMV_VF_EXT__TMP_FLAG = 1u<<27
};

/* After inclusion of gen-okeys.h we ASSERT keyoff fits in 16-bit */
struct a_amv_var_map{
	u32 avm_hash;
	u16 avm_keyoff;
	BITENUM(u16,a_amv_var_flags) avm_flags; /* Without extended bits */
};
CTA(a_AMV_VF__MASK <= U16_MAX, "Enumeration excesses storage datatype");

/* XXX Since there is no indicator character used for variable chains, we just
 * XXX cannot do better than using expensive detection.
 * The length of avcmb_prefix is highly hardwired with make-okey-map.pl etc. */
struct a_amv_var_chain_map_bsrch{
	char avcmb_prefix[4];
	u16 avcmb_chain_map_off;
	u16 avcmb_chain_map_eokey; /* Is an enum okeys */
};

/* Use 16-bit for enum okeys; all around here we use 32-bit for it instead, but that owed to faster access (?) */
struct a_amv_var_chain_map{
	u16 avcm_keyoff;
	u16 avcm_okey;
};
/* Not <= because we have _S_MAILX_TEST */
CTA(mx_OKEYS_MAX < U16_MAX, "Enumeration excesses storage datatype");

/* s-itt-mode */
