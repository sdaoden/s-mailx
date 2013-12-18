/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MD5 algorithm implementation.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Derived from RFC 1321:
 */
/* MD5.H - header file for MD5C.C
 */

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

#if defined HAVE_MD5 && !defined _NAIL_MD5_H
# define _NAIL_MD5_H
# ifdef HAVE_OPENSSL_MD5
#  include <openssl/md5.h>

#  define md5_ctx	MD5_CTX
#  define md5_init	MD5_Init
#  define md5_update	MD5_Update
#  define md5_final	MD5_Final
# else
/*
 * This version of MD5 has been changed such that any unsigned type with
 * at least 32 bits is acceptable. This is important e.g. for Cray vector
 * machines which provide only 64-bit integers.
 */
typedef	unsigned long	md5_type;

typedef struct {
	md5_type state[4];	/* state (ABCD) */
	md5_type count[2];	/* number of bits, modulo 2^64 (lsb first) */
	unsigned char	buffer[64];	/* input buffer */
} md5_ctx;

FL void	md5_init(md5_ctx *);
FL void	md5_update(md5_ctx *, unsigned char *, unsigned int);
FL void	md5_final(unsigned char[16], md5_ctx *);
# endif /* !HAVE_OPENSSL_MD5 */

FL void	hmac_md5(unsigned char *, int, unsigned char *, int, void *);

#endif /* HAVE_MD5 */
