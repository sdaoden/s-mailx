/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ UniformResourceLocator.
 *
 * Copyright (c) 2014 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_URL_H
#define mx_URL_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_url;

#ifdef mx_HAVE_NET
enum mx_url_flags{
   mx_URL_TLS_REQUIRED = 1u<<0, /* Whether protocol always uses SSL/TLS.. */
   mx_URL_TLS_OPTIONAL = 1u<<1, /* ..may later upgrade to SSL/TLS */
   mx_URL_TLS_MASK = mx_URL_TLS_REQUIRED | mx_URL_TLS_OPTIONAL,
   mx_URL_HAD_USER = 1u<<2, /* Whether .url_user was part of the URL */
   mx_URL_HOST_IS_NAME = 1u<<3 /* .url_host not numeric address */
};
#endif

struct mx_url{ /* XXX not _ctx: later object */
   char const *url_input; /* Input as given (really) */
   u32 url_flags;
   u16 url_portno; /* atoi .url_port or default, host endian */
   u8 url_cproto; /* enum cproto as given */
   u8 url_proto_len; /* Length of .url_proto (to first '\0') */
#ifdef mx_HAVE_NET
   char url_proto[16]; /* Communication protocol as 'xy\0://\0' */
   char const *url_port; /* Port (if given) or NIL */
   struct str url_user; /* User, exactly as given / looked up */
   struct str url_user_enc; /* User, url_xenc()oded */
   struct str url_pass; /* Pass (url_xdec()oded) or NULL */
   /* TODO we don't know whether .url_host is a name or an address.  Us
    * TODO Net::IPAddress::fromString() to check that, then set
    * TODO URL_HOST_IS_NAME solely based on THAT!  Until then,
    * TODO URL_HOST_IS_NAME ONLY set if n_URL_TLS_MASK+mx_HAVE_GETADDRINFO */
   struct str url_host; /* Service hostname TODO we don't know */
   struct str url_path; /* Path suffix or NULL */
   /* TODO: url_get_component(url *, enum COMPONENT, str *store) */
   struct str url_h_p; /* .url_host[:.url_port] */
   /* .url_user@.url_host
    * Note: for CPROTO_SMTP this may resolve HOST via *smtp-hostname* (->
    * *hostname*)!  (And may later be overwritten according to *from*!) */
   struct str url_u_h;
   struct str url_u_h_p; /* .url_user@.url_host[:.url_port] */
   struct str url_eu_h_p; /* .url_user_enc@.url_host[:.url_port] */
   char const *url_p_u_h_p; /* .url_proto://.url_u_h_p */
   char const *url_p_eu_h_p; /* .url_proto://.url_eu_h_p */
   char const *url_p_eu_h_p_p; /* .url_proto://.url_eu_h_p[/.url_path] */
#endif /* mx_HAVE_NET */
};

/* URL en- and decoding according to (enough of) RFC 3986 (RFC 1738).
 * These return a newly autorec_alloc()ated result, or NIL on length excess */
EXPORT char *mx_url_xenc(char const *cp, boole ispath  su_DBG_LOC_ARGS_DECL);
EXPORT char *mx_url_xdec(char const *cp  su_DBG_LOC_ARGS_DECL);

#ifdef su_HAVE_DBG_LOC_ARGS
# define mx_url_xenc(CP,P) mx_url_xenc(CP, P  su_DBG_LOC_ARGS_INJ)
# define mx_url_xdec(CP) mx_url_xdec(CP  su_DBG_LOC_ARGS_INJ)
#endif

/* `urlcodec' */
EXPORT int c_urlcodec(void *vp);

/* Parse a RFC 6058 'mailto' URI to a single to: (TODO yes, for now hacky).
 * Return NIL or something that can be converted to a struct mx_name */
EXPORT char *mx_url_mailto_to_address(char const *mailtop);

/* Return port for proto, or NIL if unknown.
 * Upon success *port_or_nil and *issnd_or_nil will be updated, if set; the
 * latter states whether protocol is of a sending type (SMTP, file etc.).
 * For file:// and test:// this returns su_empty, in the former case
 * *port_or_nil is 0 and in the latter U16_MAX */
EXPORT char const *mx_url_servbyname(char const *proto, u16 *port_or_nil,
      boole *issnd_or_nil);

/* Parse data, which must meet the criteria of the protocol cproto, and fill
 * in the URL structure urlp (URL rather according to RFC 3986) */
#ifdef mx_HAVE_NET
EXPORT boole mx_url_parse(struct mx_url *urlp, enum cproto cproto,
      char const *data);
#endif /* mx_HAVE_NET */

#include <su/code-ou.h>
#endif /* mx_URL_H */
/* s-it-mode */
