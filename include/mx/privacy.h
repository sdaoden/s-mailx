/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Generic message signing, verification, en- and decryption layer.
 *
 * Copyright (c) 2015 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_PRIVACY_H
#define mx_PRIVACY_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

#if defined mx_HAVE_XTLS
# define mx_HAVE_PRIVACY

/* Shall we sign an outgoing message? */
EXPORT boole mx_privacy_sign_is_desired(void);

/* */
EXPORT FILE *mx_privacy_sign(FILE *ip, char const *addr);

/*  */
EXPORT boole mx_privacy_verify(struct message *mp, int nr);

/* Try to encrypt a message for user, return -1 if we should send unencrypted,
 * NIL on failure or encrypted message */
EXPORT FILE *mx_privacy_encrypt_try(FILE *ip, char const *to);

/* Shall we produce errors when some messages cannot be sent encrypted? */
EXPORT boole mx_privacy_encrypt_is_forced(void);

/*  */
EXPORT struct message *mx_privacy_decrypt(struct message *mp, char const *to,
      char const *cc, boole is_a_verify_call);

#else /* mx_HAVE_XTLS */
# define mx_privacy_sign_is_desired() (FAL0)
# define mx_privacy_encrypt_try(FP,TO) su_R(FILE*,-1)
# define mx_privacy_encrypt_is_forced() (FAL0)
#endif

#include <su/code-ou.h>
#endif /* mx_PRIVACY_H */
/* s-it-mode */
