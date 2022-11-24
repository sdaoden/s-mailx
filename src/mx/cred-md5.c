/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cred-md5.h.
 *
 * Copyright (c) 2014 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cred_md5
#define mx_SOURCE
#define mx_SOURCE_CRED_MD5

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_MD5
#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/mime-enc.h"

#include "mx/cred-md5.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

# ifndef mx_XTLS_HAVE_MD5
#  include "mx/y-cred-md5.h" /* $(MX_SRCDIR) */
# endif

char *
mx_md5_tohex(char hex[mx_MD5_TOHEX_SIZE], void const *vp){
	uz i, j;
	char const *cp;
	NYD_IN;

	cp = vp;

	for(i = 0; i < mx_MD5_TOHEX_SIZE / 2; ++i){
		j = i << 1;
#define a_HEX(n) ((n) > 9 ? (n) - 10 + 'a' : (n) + '0')
		hex[j] = a_HEX((cp[i] & 0xF0) >> 4);
		hex[++j] = a_HEX(cp[i] & 0x0F);
#undef a_HEX
	}

	NYD_OU;
	return hex;
}

char *
mx_md5_cram_string(struct str const *user, struct str const *pass, char const *b64){
	struct str in, out;
	char digest[16], *cp;
	NYD_IN;

	out.s = NIL;
	if(user->l >= UZ_MAX - 1 - mx_MD5_TOHEX_SIZE - 1)
		goto jleave;
	if(pass->l >= INT_MAX)
		goto jleave;

	in.s = UNCONST(char*,b64);
	in.l = su_cs_len(in.s);
	if(!mx_b64_dec(&out, &in))
		goto jleave;
	if(out.l >= INT_MAX){
		su_FREE(out.s);
		out.s = NIL;
		goto jleave;
	}

	mx_md5_hmac(S(uc*,out.s), out.l, S(uc*,pass->s), pass->l, digest);
	su_FREE(out.s);
	cp = mx_md5_tohex(su_AUTO_ALLOC(mx_MD5_TOHEX_SIZE +1), digest);

	in.l = user->l + mx_MD5_TOHEX_SIZE +1;
	in.s = su_LOFI_ALLOC(user->l + 1 + mx_MD5_TOHEX_SIZE +1);
	su_mem_copy(in.s, user->s, user->l);
	in.s[user->l] = ' ';
	su_mem_copy(&in.s[user->l + 1], cp, mx_MD5_TOHEX_SIZE);
	if(mx_b64_enc(&out, &in, mx_B64_AUTO_ALLOC | mx_B64_CRLF) == NIL)
		out.s = NIL;
	su_LOFI_FREE(in.s);

jleave:
	NYD_OU;
	return out.s;
}

void
mx_md5_hmac(uc *text, int text_len, uc *key, int key_len, void *digest){
	/*
	 * This code is taken from
	 *
	 * Network Working Group	H. Krawczyk
	 * Request for Comments: 2104	IBM
	 * Category: Informational	M. Bellare
	 *				UCSD
	 *				R. Canetti
	 *				IBM
	 *				February 1997
	 *
	 *
	 * HMAC: Keyed-Hashing for Message Authentication
	 */
	mx_md5_t context;
	uc k_ipad[65]; /* inner padding - key XORd with ipad */
	uc k_opad[65]; /* outer padding - key XORd with opad */
	uc tk[16];
	int i;
	NYD_IN;

	/* if key is longer than 64 bytes reset it to key=MD5(key) */
	if(key_len > 64){
		mx_md5_t tctx;

		mx_md5_init(&tctx);
		mx_md5_update(&tctx, key, key_len);
		mx_md5_final(tk, &tctx);

		key = tk;
		key_len = 16;
	}

	/* the HMAC_MD5 transform looks like:
	 *
	 * MD5(K XOR opad, MD5(K XOR ipad, text))
	 *
	 * where K is an n byte key
	 * ipad is the byte 0x36 repeated 64 times
	 * opad is the byte 0x5c repeated 64 times
	 * and text is the data being protected */

	/* start out by storing key in pads */
	su_mem_set(k_ipad, 0, sizeof k_ipad);
	su_mem_set(k_opad, 0, sizeof k_opad);
	su_mem_copy(k_ipad, key, key_len);
	su_mem_copy(k_opad, key, key_len);

	/* XOR key with ipad and opad values */
	for(i=0; i<64; i++){
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	/* perform inner MD5 */
	mx_md5_init(&context); /* init context for 1st pass */
	mx_md5_update(&context, k_ipad, 64); /* start with inner pad */
	mx_md5_update(&context, text, text_len); /* then text of datagram */
	mx_md5_final(digest, &context); /* finish up 1st pass */

	/* perform outer MD5 */
	mx_md5_init(&context); /* init context for 2nd pass */
	mx_md5_update(&context, k_opad, 64); /* start with outer pad */
	mx_md5_update(&context, digest, 16); /* then results of 1st hash */
	mx_md5_final(digest, &context); /* finish up 2nd pass */

	NYD_OU;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_MD5 */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CRED_MD5
/* s-itt-mode */
