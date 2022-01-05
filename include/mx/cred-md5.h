/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MD5 message digest related.
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
/* MD5.H - header file for MD5C.C from RFC 1321 is */
/* Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
rights reserved.

License to copy and use this software is granted provided that it
is identified as the "RSA Data Security, Inc. MD5 Message-Digest
Algorithm" in all material mentioning or referencing this software
or this function.

License is also granted to make and use derivative works provided
that such works are identified as "derived from the RSA Data
Security, Inc. MD5 Message-Digest Algorithm" in all material
mentioning or referencing the derived work.

RSA Data Security, Inc. makes no representations concerning either
the merchantability of this software or the suitability of this
software for any particular purpose. It is provided "as is"
without express or implied warranty of any kind.

These notices must be retained in any copies of any part of this
documentation and/or software.
 */
#ifndef mx_CRED_MD5_H
#define mx_CRED_MD5_H

#include <mx/nail.h>
#ifdef mx_HAVE_MD5
#ifdef mx_XTLS_HAVE_MD5
# include <openssl/md5.h>
#endif

#define mx_HEADER
#include <su/code-in.h>

/* */
#define mx_MD5_DIGEST_SIZE 16u

/* */
#define mx_MD5_TOHEX_SIZE 32u

/* MD5 (RFC 1321) related facilities */
#ifdef mx_XTLS_HAVE_MD5
# define mx_md5_t MD5_CTX
# define mx_md5_init MD5_Init
# define mx_md5_update MD5_Update
# define mx_md5_final MD5_Final
#else
/* RFC 1321, MD5.H: */
/*
 * This version of MD5 has been changed such that any unsigned type with
 * at least 32 bits is acceptable. This is important e.g. for Cray vector
 * machines which provide only 64-bit integers.
 */
typedef unsigned long mx_md5_type;

typedef struct{
   mx_md5_type state[4]; /* state (ABCD) */
   mx_md5_type count[2]; /* number of bits, modulo 2^64 (lsb first) */
   unsigned char buffer[64]; /* input buffer */
} mx_md5_t;

EXPORT void mx_md5_init(mx_md5_t *);
EXPORT void mx_md5_update(mx_md5_t *, unsigned char *, unsigned int);
EXPORT void mx_md5_final(unsigned char[mx_MD5_DIGEST_SIZE], mx_md5_t *);
#endif /* mx_XTLS_HAVE_MD5 */

/* Store the MD5 checksum as a hexadecimal string in *hex*, *not* terminated,
 * using lowercase ASCII letters as defined in RFC 2195 */
EXPORT char *mx_md5_tohex(char hex[mx_MD5_TOHEX_SIZE], void const *vp);

/* CRAM-MD5 encode the *user* / *pass* / *b64* combo; NULL on overflow error */
EXPORT char *mx_md5_cram_string(struct str const *user, struct str const *pass,
      char const *b64);

/* RFC 2104: HMAC: Keyed-Hashing for Message Authentication.
 * unsigned char *text: pointer to data stream
 * int text_len       : length of data stream
 * unsigned char *key : pointer to authentication key
 * int key_len        : length of authentication key
 * caddr_t digest     : caller digest to be filled in */
EXPORT void mx_md5_hmac(unsigned char *text, int text_len, unsigned char *key,
      int key_len, void *digest);

#include <su/code-ou.h>
#endif /* mx_HAVE_MD5 */
#endif /* mx_CRED_MD5_H */
/* s-it-mode */
