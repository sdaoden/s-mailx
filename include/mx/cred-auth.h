/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Credential and authentication (method) lookup.
 *
 * Copyright (c) 2014 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#endif

struct mx_cred_ctx;

#ifdef mx_HAVE_NET
enum mx_cred_authtype{
   mx_CRED_AUTHTYPE_NONE,
   mx_CRED_AUTHTYPE_ERROR = mx_CRED_AUTHTYPE_NONE,
   mx_CRED_AUTHTYPE_CRAM_MD5 = 1u<<0,
   mx_CRED_AUTHTYPE_EXTERNAL = 1u<<1,
      /* Note! This de-facto is EXTERNAL, but find_name("EXTERNAL") will not
       * return this one as part of a mask, but only EXTERNAL.  Therefore, if
       * for example a server capability announcement is parsed, callees are
       * responsible to ensure that EXTERNANON is also included! */
      mx_CRED_AUTHTYPE_EXTERNANON = 1u<<2,
   mx_CRED_AUTHTYPE_GSSAPI = 1u<<3,
   mx_CRED_AUTHTYPE_LOGIN = 1u<<4, /* SMTP: SASL, IMAP: IMAP login */
   /* OAUTHBEARER (RFC 7628) and XOAUTH2 (early-shoot usage) share logic */
   mx_CRED_AUTHTYPE_OAUTHBEARER = 1u<<5,
      mx_CRED_AUTHTYPE_XOAUTH2 = 1u<<6,
   mx_CRED_AUTHTYPE_PLAIN = 1u<<7 /* POP3: APOP is covered by this */
};
enum{
   /* These additional bits will be set by auth_authtype_verify_bits() */
   mx_CRED_AUTHTYPE_MULTI = 1u<<8, /* Multiple types are set */
   mx_CRED_AUTHTYPE_NEED_TLS = 1u<<9, /* TLS is requirement for 1+ mech */

   mx_CRED_AUTHTYPE_MECH_MASK = mx_CRED_AUTHTYPE_MULTI - 1,
   mx_CRED_AUTHTYPE_MECH_COUNT = 8,
   mx_CRED_AUTHTYPE_LASTBIT = 9,
   mx_CRED_AUTHTYPE_MASK = (1u<<(mx_CRED_AUTHTYPE_LASTBIT+1)) - 1
};

/* Authentication types per proto, overall (all supported).
 * The _AUTO_ variants can be fully managed without external help (for example
 * GSSAPI needs a ticket, EXTERNAL need a client certificate, etc.) */
enum mx_cred_proto_authtypes{
   mx_CRED_PROTO_AUTHTYPES_IMAP =
         mx_CRED_AUTHTYPE_CRAM_MD5 |
         mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
         mx_CRED_AUTHTYPE_GSSAPI |
         mx_CRED_AUTHTYPE_LOGIN |
         mx_CRED_AUTHTYPE_OAUTHBEARER,

      /*mx_CRED_PROTO_AUTHTYPES_AUTO_IMAP*/
      /*mx_CRED_PROTO_AUTHTYPES_AUTO_NOTLS_IMAP*/

      mx_CRED_PROTO_AUTHTYPES_DEFAULT_IMAP = mx_CRED_AUTHTYPE_LOGIN,

   mx_CRED_PROTO_AUTHTYPES_POP3 =
         mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
         mx_CRED_AUTHTYPE_GSSAPI |
         mx_CRED_AUTHTYPE_OAUTHBEARER |
         mx_CRED_AUTHTYPE_PLAIN,

      mx_CRED_PROTO_AUTHTYPES_DEFAULT_POP3 = mx_CRED_AUTHTYPE_PLAIN,

      /*mx_CRED_PROTO_AUTHTYPES_AUTO_POP3*/
      /*mx_CRED_PROTO_AUTHTYPES_AUTO_NOTLS_SMTP*/

   mx_CRED_PROTO_AUTHTYPES_SMTP =
         mx_CRED_AUTHTYPE_CRAM_MD5 |
         mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
         mx_CRED_AUTHTYPE_GSSAPI |
         mx_CRED_AUTHTYPE_LOGIN |
         mx_CRED_AUTHTYPE_OAUTHBEARER | mx_CRED_AUTHTYPE_XOAUTH2,
         mx_CRED_AUTHTYPE_PLAIN,

      mx_CRED_PROTO_AUTHTYPES_AUTO_SMTP =
            mx_CRED_AUTHTYPE_CRAM_MD5 |
            mx_CRED_AUTHTYPE_LOGIN |
            mx_CRED_AUTHTYPE_PLAIN,

      mx_CRED_PROTO_AUTHTYPES_AUTO_NOTLS_SMTP =
            mx_CRED_AUTHTYPE_CRAM_MD5,

      mx_CRED_PROTO_AUTHTYPES_DEFAULT_SMTP = mx_CRED_PROTO_AUTHTYPES_AUTO_SMTP
};

/* Authentication types per proto, truly available.
 * Ugly, but configure-time evaluation as well as an external symbol: worse! */
enum mx_cred_proto_authtypes_available{
   mx_CRED_PROTO_AUTHTYPES_AVAILABLE_IMAP =
#ifdef mx_HAVE_MD5
         mx_CRED_AUTHTYPE_CRAM_MD5 |
#endif
#ifdef mx_HAVE_TLS
         mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
#endif
#ifdef mx_HAVE_GSSAPI
         mx_CRED_AUTHTYPE_GSSAPI |
#endif
         mx_CRED_AUTHTYPE_LOGIN |
#ifdef mx_HAVE_TLS
         mx_CRED_AUTHTYPE_OAUTHBEARER |
#endif
         mx_CRED_AUTHTYPE_NONE,

   mx_CRED_PROTO_AUTHTYPES_AVAILABLE_POP3 =
#ifdef mx_HAVE_TLS
         mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
#endif
#ifdef mx_HAVE_GSSAPI
         mx_CRED_AUTHTYPE_GSSAPI |
#endif
#ifdef mx_HAVE_TLS
         mx_CRED_AUTHTYPE_OAUTHBEARER |
#endif
         mx_CRED_AUTHTYPE_PLAIN,

   mx_CRED_PROTO_AUTHTYPES_AVAILABLE_SMTP =
#ifdef mx_HAVE_MD5
         mx_CRED_AUTHTYPE_CRAM_MD5 |
#endif
#ifdef mx_HAVE_TLS
         mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
#endif
#ifdef mx_HAVE_GSSAPI
         mx_CRED_AUTHTYPE_GSSAPI |
#endif
         mx_CRED_AUTHTYPE_LOGIN |
#ifdef mx_HAVE_TLS
         mx_CRED_AUTHTYPE_OAUTHBEARER | mx_CRED_AUTHTYPE_XOAUTH2 |
#endif
         mx_CRED_AUTHTYPE_PLAIN |
         mx_CRED_AUTHTYPE_NONE
};

struct mx_cred_ctx{
   /* FIXME cc_auth, cc_authtype and cc_needs_tls are obsolete */
char const *cc_auth; /* v15-compat Authentication type as string */
u32 cc_authtype; /* v15-compat Desired cred_authtype */
boole cc_needs_tls; /* v15-compat .cc_authtype requires TLS transport */
   u8 cc_cproto; /* Used enum cproto */
   u8 cc__dummy[2 + 4];
   u32 cc_config; /* authtype, additional bits, plus protocol internals */
   struct str cc_user; /* User or NIL */
   struct str cc_pass; /* Password or NIL */
};

struct mx_cred_authtype_info{
   u32 cai_type; /* The cred_authtype */
   u16 cai_flags; /* Internal */
   boole cai_pass_cleartxt;
   boole cai_needs_tls;
   char const cai_user_name[12]; /* What we expect in config */
   char const cai_name[12]; /* The real name */
};

struct mx_cred_authtype_verify_ctx{
   enum cproto cavc_proto; /* Input: protocol to test */
   u32 cavc_mechplusbits; /* I/O: (usable) mechanisms (plus additional bits) */
   u32 cavc_cnt; /* Output: number of reauthentication methods */
   u8 cavc__pad[4];
};

/* Zero ccp and lookup credentials for communicating with urlp.
 * It will call protocol specific _config() function if possible: the callee
 * should not use URL except for using it to lookup variable chains.
 * Return whether credentials are available and valid (for chosen auth) */
EXPORT boole mx_cred_auth_lookup(struct mx_cred_ctx *credp,
      struct mx_url *urlp);

/* Find a (usr config, else official) type name.  Return it or NIL,
 * or -1 if name is "allmechs" (only with usr config).
 * (Recall the EXTERNAL/EXTERNANON problem, as above.)
 * The latter finds a type, and returns it or NIL (unsupported) */
EXPORT struct mx_cred_authtype_info const *mx_cred_auth_type_find_name(
      char const *name, boole usr);
EXPORT struct mx_cred_authtype_info const *mx_cred_auth_type_find_type(
      u32 type);

/* Verify all cred_authtype .cavc_mechplusbits for the given .cavc_proto,
 * with bells and whistles.
 * maybe_tls is an indication whether the transport could eventually be
 * secured -- that "whether" should solely be based upon the protocol
 * configuration, not to an actual URL.
 * Return whether any usable mechanism remains in .cavc_mechbits.
 * Returns TRUM1 if the list includes all possible mechanisms. */
EXPORT boole mx_cred_auth_type_verify_bits(
      struct mx_cred_authtype_verify_ctx *cavcp, boole maybe_tls);

/* Once the connection is established, and potentially has been secured,
 * select an actual authentication type from the given mechanisms.
 * mechplusbits should have been adjusted already to be compatible with the
 * server offerings.
 * assume_tls states whether the connection is secured, or not.
 * Returns the "best" match, or U32_MAX upon error aka no authentication
 * mechanism can be used */
EXPORT u32 mx_cred_auth_type_select(enum cproto proto, u32 mechplusbits,
      boole assume_tls);

#include <su/code-ou.h>
#endif /* mx_HAVE_NET */
#endif /* mx_CRED_AUTH_H */
/* s-it-mode */
