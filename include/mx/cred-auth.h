/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Credential and authentication (method) lookup.
 *
 * Copyright (c) 2014 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CRED_AUTH_H
#define mx_CRED_AUTH_H

#include <mx/nail.h>
#ifdef mx_HAVE_NET

#include <mx/url.h>

#define mx_HEADER
#include <su/code-in.h>

enum mx_cred_authtype{
   mx_CRED_AUTHTYPE_NONE = 1u<<0,
   mx_CRED_AUTHTYPE_PLAIN = 1u<<1, /* POP3: APOP is covered by this */
   mx_CRED_AUTHTYPE_LOGIN = 1u<<2,
   mx_CRED_AUTHTYPE_CRAM_MD5 = 1u<<3,
   mx_CRED_AUTHTYPE_GSSAPI = 1u<<4
};

struct mx_cred_ctx{
   u32 cc_cproto; /* Used enum cproto */
   u32 cc_authtype; /* Desired enum mx_cred_authtype */
   char const *cc_auth; /* Authentication type as string */
   struct str cc_user; /* User (url_xdec()oded) or NIL */
   struct str cc_pass; /* Password (url_xdec()oded) or NIL */
};

/* Zero ccp and lookup credentials for communicating with urlp.
 * Return whether credentials are available and valid (for chosen auth) */
EXPORT boole mx_cred_auth_lookup(struct mx_cred_ctx *ccp, struct mx_url *urlp);
EXPORT boole mx_cred_auth_lookup_old(struct mx_cred_ctx *ccp,
      enum cproto cproto, char const *addr);

#include <su/code-ou.h>
#endif /* mx_HAVE_NET */
#endif /* mx_CRED_AUTH_H */
/* s-it-mode */
