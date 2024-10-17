/*@ Implementation of prime.h.
 *
 * Copyright (c) 2001 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_prime
#define su_SOURCE
#define su_SOURCE_PRIME

#include "su/code.h"

#include "su/prime.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

/* Collected & merged from 'GLIB 1's 'gprimes.c' and 'GNU STL's 'hashtable'
 * (around Y2K+1 or so); beyond 0x18000005 added in 2022 */
static u32 const a_prime_lookup[] = {
	0x00000002, 0x00000005, 0x0000000B,
	0x00000017, 0x0000002F, 0x00000061, 0x0000009D,
	0x0000011B, 0x0000022D, 0x00000337, 0x000004D5, 0x00000741, 0x00000AD9,
	0x00001051, 0x00001867, 0x0000249B, 0x000036E9, 0x00005261, 0x00007B8B, 0x0000B947,
	0x000115E7ul, 0x0001A0E1ul, 0x00027149ul, 0x0003A9E5ul, 0x00057EE3ul, 0x00083E39ul, 0x000C5D67ul,
	0x00128C09ul, 0x001BD1FFul, 0x0029BB13ul, 0x003E988Bul, 0x005DE4C1ul, 0x008CD721ul, 0x00D342ABul,
	0x01800013ul, 0x03000005ul, 0x06000017ul, 0x0C000013ul,
	0x18000005ul, 0x30000059ul, 0x600000F5ul, 0xC0000217ul/*, 0xFFFFFFFBul*/
};
CTAV(su_PRIME_LOOKUP_MAX == 0xC0000217ul);

#define a_PRIME_FIRST &a_prime_lookup[0]
#define a_PRIME_MIDDLE &a_prime_lookup[NELEM(a_prime_lookup) / 2]
#define a_PRIME_LAST &a_prime_lookup[NELEM(a_prime_lookup) - 1]

static u64 const a_prime_max = su_U64_C(0xFFFFFFFFFFFFFFFD);

/* */
static boole a_prime_is_pseudo(u64 no);
static boole a_prime_is_real(u64 no);

/* */
static u64 a_prime_calc(u64 no, s64 add, boole pseudo_ok);

static boole
a_prime_is_pseudo(u64 no){
	boole rv;
	NYD_IN;

	switch(no){
	case 2:
	case 3:
	case 5:
	case 7:
		rv = TRU1; break;
	case 0:
	case 1:
	case 4:
	case 6:
		rv = FAL0; break;
	default:
		rv = ((no & 1) && (no % 3) && (no % 5) && (no % 7));
		break;
	}

	NYD_OU;
	return rv;
}

static boole
a_prime_is_real(u64 no){ /* TODO brute force yet (at least Miller-Rabin?) */
	/* no is pseudo! */
	union {uz x; u64 x64; boole rv;} u;
	NYD_IN;

	if(no < 2)
		goto jfal;
	if(no != 2){
		u64 xno;

		for(u.x64 = 3, xno = (no >> 1) + 1; u.x64 < xno; u.x64 += 2)
			if(no % u.x64 == 0)
				goto jfal;
	}

	u.rv = TRU1;
jleave:
	NYD_OU;
	return u.rv;
jfal:
	u.rv = FAL0;
	goto jleave;
}

static u64
a_prime_calc(u64 no, s64 add, boole pseudo_ok){
	NYD_IN;

	/* Primes are all odd (except 2 but that is NEVER evaluated in here) */
	if(!(no & 1)){
		no += S(u64,add);
		add += add;
		goto jdiver;
	}

	add += add;
	for(;;){
		no += S(u64,add);
jdiver:
		if(!a_prime_is_pseudo(no))
			continue;
		if(pseudo_ok || a_prime_is_real(no))
			break;
	}

	NYD_OU;
	return no;
}

boole
su_prime_is_prime(u64 no, boole allowpseudo){
	boole rv;
	NYD_IN;

	rv = a_prime_is_pseudo(no);
	if(rv && !allowpseudo)
		rv = a_prime_is_real(no);

	NYD_OU;
	return rv;
}

u64
su_prime_get_former(u64 no, boole allowpseudo){
	NYD_IN;

	if(no <= 2 + 1)
		no = 2;
	else if(no > a_prime_max)
		no = a_prime_max;
	else
		no = a_prime_calc(no, -1, allowpseudo);

	NYD_OU;
	return no;
}

u64
su_prime_get_next(u64 no, boole allowpseudo){
	NYD_IN;

	if(no < 2)
		no = 2;
	else if(no >= a_prime_max - 2)
		no = a_prime_max;
	else
		no = a_prime_calc(no, +1, allowpseudo);

	NYD_OU;
	return no;
}

u32
su_prime_lookup_former(u32 no){
	u32 const *cursor;
	NYD_IN;

	cursor = ((no < *a_PRIME_MIDDLE) ? a_PRIME_MIDDLE - 1 : a_PRIME_LAST);
	while(*cursor >= no && --cursor > a_PRIME_FIRST)
		;

	NYD_OU;
	return *cursor;
}

u32
su_prime_lookup_next(u32 no){
	u32 const *cursor;
	NYD_IN;

	cursor = ((no > *a_PRIME_MIDDLE) ? a_PRIME_MIDDLE + 1 : a_PRIME_FIRST);
	while(*cursor <= no && ++cursor < a_PRIME_LAST)
		;

	NYD_OU;
	return *cursor;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_PRIME
/* s-itt-mode */
