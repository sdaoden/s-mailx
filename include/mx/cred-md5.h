/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MD5 message digest related.
 *
 * Copyright (c) 2014 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CRED_MD5_H
#define mx_CRED_MD5_H

#include <mx/nail.h>
#ifdef mx_HAVE_TLS_MD5
#include <openssl/md5.h>

#define mx_HEADER
#include <su/code-in.h>

/* */
#define mx_MD5_DIGEST_SIZE 16u

/* */
#define mx_MD5_TOHEX_SIZE 32u

/* MD5 (RFC 1321) related facilities */
#define mx_md5_t MD5_CTX
#define mx_md5_init MD5_Init
#define mx_md5_update MD5_Update
#define mx_md5_final MD5_Final

/* Store the MD5 checksum as a hexadecimal string in *hex*, *not* terminated,
 * using lowercase ASCII letters as defined in RFC 2195 */
EXPORT char *mx_md5_tohex(char hex[mx_MD5_TOHEX_SIZE], void const *vp);

/* CRAM-MD5 encode the *user* / *pass* / *b64* combo; NULL on overflow error */
EXPORT char *mx_md5_cram_string(struct str const *user, struct str const *pass, char const *b64);

/* RFC 2104: HMAC: Keyed-Hashing for Message Authentication.
 * unsigned char *text: pointer to data stream
 * int text_len: length of data stream
 * unsigned char *key: pointer to authentication key
 * int key_len: length of authentication key
 * caddr_t digest: caller digest to be filled in */
EXPORT void mx_md5_hmac(unsigned char *text, int text_len, unsigned char *key, int key_len, void *digest);

#include <su/code-ou.h>
#endif /* mx_HAVE_TLS_MD5 */
#endif /* mx_CRED_MD5_H */
/* s-itt-mode */
