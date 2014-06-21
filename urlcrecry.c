/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ URL parsing, credential handling and crypto hooks.
 *
 * Copyright (c) 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

FL bool_t
url_parse(struct url *urlp, enum cproto cproto, char const *data)
{
#if defined HAVE_SMTP && defined HAVE_POP3 && defined HAVE_IMAP
# define __ALLPROTO
#endif
#if defined HAVE_SMTP || defined HAVE_POP3 || defined HAVE_IMAP
# define __ANYPROTO
   char *cp, *x;
#endif
   bool_t rv = FAL0;
   NYD_ENTER;
   UNUSED(data);

   memset(urlp, 0, sizeof *urlp);
   urlp->url_input = data;
   urlp->url_cproto = cproto;

   /* Network protocol */
#define _protox(X,Y)  \
   urlp->url_portno = Y;\
   memcpy(urlp->url_proto, X "://", sizeof(X "://"));\
   urlp->url_proto[sizeof(X) -1] = '\0';\
   urlp->url_proto_len = sizeof(X) -1;\
   urlp->url_proto_xlen = sizeof(X "://") -1
#define __if(X,Y,Z)  \
   if (!ascncasecmp(data, X "://", sizeof(X "://") -1)) {\
      _protox(X, Y);\
      data += sizeof(X "://") -1;\
      do { Z; } while (0);\
      goto juser;\
   }
#define _if(X,Y)     __if(X, Y, (void)0)
#ifdef HAVE_SSL
# define _ifs(X,Y)   __if(X, Y, urlp->url_needs_tls = TRU1)
#else
# define _ifs(X,Y)   goto jeproto;
#endif

   switch (cproto) {
   case CPROTO_SMTP:
#ifdef HAVE_SMTP
      _if ("smtp", 25)
      _if ("submission", 587)
      _ifs ("smtps", 465)
      _protox("smtp", 25);
      break;
#else
      goto jeproto;
#endif
   case CPROTO_POP3:
#ifdef HAVE_POP3
      _if ("pop3", 110)
      _ifs ("pop3s", 995)
      _protox("pop3", 110);
      break;
#else
      goto jeproto;
#endif
   case CPROTO_IMAP:
#ifdef HAVE_IMAP
      _if ("imap", 143)
      _ifs ("imaps", 993)
      _protox("imap", 143);
      break;
#else
      goto jeproto;
#endif
   }

#undef _ifs
#undef _if
#undef __if
#undef _protox

   if (strstr(data, "://") != NULL) {
#if !defined __ALLPROTO || !defined HAVE_SSL
jeproto:
#endif
      fprintf(stderr, tr(574, "URL `proto://' prefix invalid: `%s'\n"),
         urlp->url_input);
      goto jleave;
   }
#ifdef __ANYPROTO

   /* User and password, I */
juser:
   if ((cp = UNCONST(last_at_before_slash(data))) != NULL) {
      size_t l = PTR2SIZE(cp - data);
      char const *d = data;
      char *ub = ac_alloc(l +1);

      urlp->url_had_user = TRU1;
      data = cp + 1;

      /* And also have a password? */
      if ((cp = memchr(d, ':', l)) != NULL) {
         size_t i = PTR2SIZE(cp - d);

         l -= i + 1;
         memcpy(ub, cp + 1, l);
         ub[l] = '\0';
         urlp->url_pass.l = strlen(urlp->url_pass.s = urlxdec(ub));
         urlp->url_pass_enc.l = strlen(
               urlp->url_pass_enc.s = urlxenc(urlp->url_pass.s, FAL0));

         if (strcmp(ub, urlp->url_pass_enc.s)) {
            fprintf(stderr, tr(581,
               "String is not properly URL percent encoded: `%s'\n"), ub);
            goto jleave;
         }
         l = i;
      }

      memcpy(ub, d, l);
      ub[l] = '\0';
      urlp->url_user.l = strlen(urlp->url_user.s = urlxdec(ub));
      urlp->url_user_enc.l = strlen(
            urlp->url_user_enc.s = urlxenc(urlp->url_user.s, FAL0));

      if (urlp->url_user_enc.l != l || memcmp(urlp->url_user_enc.s, ub, l)) {
         fprintf(stderr, tr(581,
            "String is not properly URL percent encoded: `%s'\n"), ub);
         goto jleave;
      }

      ac_free(ub);
   }

   /* Servername and port -- and possible path suffix */
   if ((cp = strchr(data, ':')) != NULL) { /* TODO URL parse, IPv6 support */
      char *eptr;
      long l;

      urlp->url_port = x = savestr(x = cp + 1);
      if ((x = strchr(x, '/')) != NULL)
         *x = '\0';
      l = strtol(urlp->url_port, &eptr, 10);
      if (*eptr != '\0' || l <= 0 || UICMP(32, l, >=, 0xFFFFu)) {
         fprintf(stderr, tr(573, "URL with invalid port number: `%s'\n"),
            urlp->url_input);
         goto jleave;
      }
      urlp->url_portno = (ui16_t)l;
   } else {
      if ((x = strchr(data, '/')) != NULL)
         data = savestrbuf(data, PTR2SIZE(x - data));
      cp = UNCONST(data + strlen(data));
   }

   /* A (non-empty) path may only occur with IMAP */
   if (x != NULL && x[1] != '\0') {
      if (cproto != CPROTO_IMAP) {
         fprintf(stderr, tr(575, "URL protocol doesn't support paths: `%s'\n"),
            urlp->url_input);
         goto jleave;
      }
      urlp->url_path.l = strlen(++x);
      urlp->url_path.s = savestrbuf(x, urlp->url_path.l);
   }

   urlp->url_host.s = savestrbuf(data, urlp->url_host.l = PTR2SIZE(cp - data));
   {  size_t i;
      for (cp = urlp->url_host.s, i = urlp->url_host.l; i != 0; ++cp, --i)
         *cp = lowerconv(*cp);
   }

   /* .url_h_p: HOST:PORT */
   {  size_t i;
      struct str *s = &urlp->url_h_p;

      s->s = salloc(urlp->url_host.l + 1 + sizeof("65536")-1 +1);
      memcpy(s->s, urlp->url_host.s, i = urlp->url_host.l);
      if (urlp->url_port != NULL) {
         size_t j = strlen(urlp->url_port);
         s->s[i++] = ':';
         memcpy(s->s + i, urlp->url_port, j);
         i += j;
      }
      s->s[i] = '\0';
      s->l = i;
   }

   /* User, II
    * If there was no user in the URL, do we have *user-HOST* or *user*? */
   if (!urlp->url_had_user) {
      char const usrstr[] = "user-";
      size_t const usrlen = sizeof(usrstr) -1;

      cp = ac_alloc(usrlen + urlp->url_h_p.l +1);
      memcpy(cp, usrstr, usrlen);
      memcpy(cp + usrlen, urlp->url_h_p.s, urlp->url_h_p.l +1);
      if ((urlp->url_user.s = vok_vlook(cp)) == NULL) {
         cp[usrlen - 1] = '\0';
         if ((urlp->url_user.s = vok_vlook(cp)) == NULL)
            urlp->url_user.s = UNCONST(myname);
      }
      ac_free(cp);

      urlp->url_user.l = strlen(urlp->url_user.s);
      urlp->url_user.s = savestrbuf(urlp->url_user.s, urlp->url_user.l);
      urlp->url_user_enc.l = strlen(
            urlp->url_user_enc.s = urlxenc(urlp->url_user.s, FAL0));

   }

   /* And then there are a lot of prebuild string combinations TODO do lazy */

   /* .url_u_h: .url_user@.url_host
    * For SMTP we apply ridiculously complicated *v15-compat* plus
    * *smtp-hostname* / *hostname* dependent rules */
   {  struct str h, *s;
      size_t i;

      if (cproto == CPROTO_SMTP && ok_blook(v15_compat) &&
            (cp = ok_vlook(smtp_hostname)) != NULL) {
         if (*cp == '\0')
            cp = nodename(1);
         h.s = savestrbuf(cp, h.l = strlen(cp));
      } else
         h = urlp->url_host;

      s = &urlp->url_u_h;
      i = urlp->url_user.l;

      s->s = salloc(i + 1 + h.l +1);
      if (i > 0) {
         memcpy(s->s, urlp->url_user.s, i);
         s->s[i++] = '@';
      }
      memcpy(s->s + i, h.s, h.l +1);
      i += h.l;
      s->l = i;
   }

   /* .url_u_h_p: .url_user@.url_host[:.url_port] */
   {  struct str *s = &urlp->url_u_h_p;
      size_t i = urlp->url_user.l;

      s->s = salloc(i + 1 + urlp->url_h_p.l +1);
      if (i > 0) {
         memcpy(s->s, urlp->url_user.s, i);
         s->s[i++] = '@';
      }
      memcpy(s->s + i, urlp->url_h_p.s, urlp->url_h_p.l +1);
      i += urlp->url_h_p.l;
      s->l = i;
   }

   /* .url_eu_h_p: .url_user_enc@.url_host[:.url_port] */
   {  struct str *s = &urlp->url_eu_h_p;
      size_t i = urlp->url_user_enc.l;

      s->s = salloc(i + 1 + urlp->url_h_p.l +1);
      if (i > 0) {
         memcpy(s->s, urlp->url_user_enc.s, i);
         s->s[i++] = '@';
      }
      memcpy(s->s + i, urlp->url_h_p.s, urlp->url_h_p.l +1);
      i += urlp->url_h_p.l;
      s->l = i;
   }

   /* .url_p_u_h_p: .url_proto://.url_u_h_p */
   {  size_t i;
      char *ud = salloc((i = urlp->url_proto_xlen + urlp->url_u_h_p.l) +1);

      urlp->url_proto[urlp->url_proto_len] = ':';
      memcpy(sstpcpy(ud, urlp->url_proto), urlp->url_u_h_p.s,
         urlp->url_u_h_p.l +1);
      urlp->url_proto[urlp->url_proto_len] = '\0';

      urlp->url_p_u_h_p = ud;
   }

   /* .url_p_eu_h_p, .url_p_eu_h_p_p: .url_proto://.url_eu_h_p[/.url_path] */
   {  size_t i;
      char *ud = salloc((i = urlp->url_proto_xlen + urlp->url_eu_h_p.l) +
            1 + urlp->url_path.l +1);

      urlp->url_proto[urlp->url_proto_len] = ':';
      memcpy(sstpcpy(ud, urlp->url_proto), urlp->url_eu_h_p.s,
         urlp->url_eu_h_p.l +1);
      urlp->url_proto[urlp->url_proto_len] = '\0';

      if (urlp->url_path.l == 0)
         urlp->url_p_eu_h_p = urlp->url_p_eu_h_p_p = ud;
      else {
         urlp->url_p_eu_h_p = savestrbuf(ud, i);
         urlp->url_p_eu_h_p_p = ud;
         ud += i;
         *ud++ = '/';
         memcpy(ud, urlp->url_path.s, urlp->url_path.l +1);
      }
   }

   rv = TRU1;
#endif /* __ANYPROTO */
jleave:
   NYD_LEAVE;
   return rv;
#undef __ANYPROTO
#undef __ALLPROTO
}

FL bool_t
ccred_lookup_old(struct ccred *ccp, enum cproto cproto, char const *addr)
{
   char const *pname, *pxstr, *authdef;
   size_t pxlen, addrlen, i;
   char *vbuf, *s;
   ui8_t authmask;
   enum {NONE=0, WANT_PASS=1<<0, REQ_PASS=1<<1, WANT_USER=1<<2, REQ_USER=1<<3}
      ware = NONE;
   bool_t addr_is_nuser = FAL0; /* XXX v15.0 legacy! v15_compat */
   NYD_ENTER;

   memset(ccp, 0, sizeof *ccp);

   switch (cproto) {
   default:
   case CPROTO_SMTP:
      pname = "SMTP";
      pxstr = "smtp-auth";
      pxlen = sizeof("smtp-auth") -1;
      authmask = AUTHTYPE_NONE | AUTHTYPE_PLAIN | AUTHTYPE_LOGIN |
            AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "none";
      addr_is_nuser = TRU1;
      break;
   case CPROTO_POP3:
      pname = "POP3";
      pxstr = "pop3-auth";
      pxlen = sizeof("pop3-auth") -1;
      authmask = AUTHTYPE_PLAIN;
      authdef = "plain";
      break;
   case CPROTO_IMAP:
      pname = "IMAP";
      pxstr = "imap-auth";
      pxlen = sizeof("imap-auth") -1;
      authmask = AUTHTYPE_LOGIN | AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "login";
      break;
   }

   ccp->cc_cproto = cproto;
   addrlen = strlen(addr);
   vbuf = ac_alloc(pxlen + addrlen + sizeof("-password-")-1 +1);
   memcpy(vbuf, pxstr, pxlen);

   /* Authentication type */
   vbuf[pxlen] = '-';
   memcpy(vbuf + pxlen + 1, addr, addrlen +1);
   if ((s = vok_vlook(vbuf)) == NULL) {
      vbuf[pxlen] = '\0';
      if ((s = vok_vlook(vbuf)) == NULL)
         s = UNCONST(authdef);
   }

   if (!asccasecmp(s, "none")) {
      ccp->cc_auth = "NONE";
      ccp->cc_authtype = AUTHTYPE_NONE;
      /*ware = NONE;*/
   } else if (!asccasecmp(s, "plain")) {
      ccp->cc_auth = "PLAIN";
      ccp->cc_authtype = AUTHTYPE_PLAIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!asccasecmp(s, "login")) {
      ccp->cc_auth = "LOGIN";
      ccp->cc_authtype = AUTHTYPE_LOGIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!asccasecmp(s, "cram-md5")) {
      ccp->cc_auth = "CRAM-MD5";
      ccp->cc_authtype = AUTHTYPE_CRAM_MD5;
      ware = REQ_PASS | REQ_USER;
   } else if (!asccasecmp(s, "gssapi")) {
      ccp->cc_auth = "GSS-API";
      ccp->cc_authtype = AUTHTYPE_GSSAPI;
      ware = REQ_USER;
#if 0 /* TODO SASL (homebrew `auto'matic indeed) */
   } else if (!asccasecmp(cp, "sasl")) {
      ware = REQ_USER | WANT_PASS;
#endif
   } /* no else */

   /* Verify method */
   if (!(ccp->cc_authtype & authmask)) {
      fprintf(stderr, tr(273, "Unsupported %s authentication method: %s\n"),
         pname, s);
      ccp = NULL;
      goto jleave;
   }
#ifndef HAVE_MD5
   if (ccp->cc_authtype == AUTHTYPE_CRAM_MD5) {
      fprintf(stderr, tr(277, "No CRAM-MD5 support compiled in.\n"));
      ccp = NULL;
      goto jleave;
   }
#endif
#ifndef HAVE_GSSAPI
   if (ccp->cc_authtype == AUTHTYPE_GSSAPI) {
      fprintf(stderr, tr(272, "No GSS-API support compiled in.\n"));
      ccp = NULL;
      goto jleave;
   }
#endif

   /* User name */
   if (!(ware & (WANT_USER | REQ_USER)))
      goto jpass;

   if (!addr_is_nuser) {
      if ((s = UNCONST(last_at_before_slash(addr))) != NULL) {
         ccp->cc_user.s = urlxdec(savestrbuf(addr, PTR2SIZE(s - addr)));
         ccp->cc_user.l = strlen(ccp->cc_user.s);
      } else if (ware & REQ_USER)
         goto jgetuser;
      goto jpass;
   }

   memcpy(vbuf + pxlen, "-user-", i = sizeof("-user-") -1);
   i += pxlen;
   memcpy(vbuf + i, addr, addrlen +1);
   if ((s = vok_vlook(vbuf)) == NULL) {
      vbuf[--i] = '\0';
      if ((s = vok_vlook(vbuf)) == NULL && (ware & REQ_USER)) {
         if ((s = getuser(NULL)) == NULL) {
jgetuser:   /* TODO v15.0: today we simply bail, but we should call getuser().
             * TODO even better: introduce `PROTO-user' and `PROTO-pass' and
             * TODO check that first, then! change control flow, grow `vbuf' */
            fprintf(stderr, tr(274,
               "A user is necessary for %s authentication.\n"), pname);
            ccp = NULL;
            goto jleave;
         }
      }
   }
   ccp->cc_user.l = strlen(ccp->cc_user.s = savestr(s));

   /* Password */
jpass:
   if (!(ware & (WANT_PASS | REQ_PASS)))
      goto jleave;

   if (!addr_is_nuser) {
      memcpy(vbuf, "password-", i = sizeof("password-") -1);
   } else {
      memcpy(vbuf + pxlen, "-password-", i = sizeof("-password-") -1);
      i += pxlen;
   }
   memcpy(vbuf + i, addr, addrlen +1);
   if ((s = vok_vlook(vbuf)) == NULL) {
      vbuf[--i] = '\0';
      if ((!addr_is_nuser || (s = vok_vlook(vbuf)) == NULL) &&
            (ware & REQ_PASS)) {
         if ((s = getpassword(NULL)) == NULL) {
            fprintf(stderr, tr(275,
               "A password is necessary for %s authentication.\n"), pname);
            ccp = NULL;
            goto jleave;
         }
      }
   }
   if (s != NULL)
      ccp->cc_pass.l = strlen(ccp->cc_pass.s = savestr(s));

jleave:
   ac_free(vbuf);
   NYD_LEAVE;
   return (ccp != NULL);
}

FL bool_t
ccred_lookup(struct ccred *ccp, struct url *urlp)
{
   char const *pstr, *authdef;
   size_t plen, i;
   char *vbuf, *s;
   ui8_t authmask;
   enum {NONE=0, WANT_PASS=1<<0, REQ_PASS=1<<1, WANT_USER=1<<2, REQ_USER=1<<3}
      ware = NONE;
   NYD_ENTER;

   memset(ccp, 0, sizeof *ccp);
   ccp->cc_user = urlp->url_user;

   switch ((ccp->cc_cproto = urlp->url_cproto)) {
   default:
   case CPROTO_SMTP:
      pstr = "smtp";
      plen = sizeof("smtp") -1;
      authmask = AUTHTYPE_NONE | AUTHTYPE_PLAIN | AUTHTYPE_LOGIN |
            AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "none";
      break;
   case CPROTO_POP3:
      pstr = "pop3";
      plen = sizeof("pop3") -1;
      authmask = AUTHTYPE_PLAIN;
      authdef = "plain";
      break;
   case CPROTO_IMAP:
      pstr = "imap";
      plen = sizeof("imap") -1;
      authmask = AUTHTYPE_LOGIN | AUTHTYPE_CRAM_MD5 | AUTHTYPE_GSSAPI;
      authdef = "login";
      break;
   }

   /* Note: "password-" is longer than "-auth-", .url_u_h_p and .url_h_p */
   vbuf = ac_alloc(plen + sizeof("password-")-1 + urlp->url_u_h_p.l +1);
   memcpy(vbuf, pstr, plen);

   /* Authentication type */
   memcpy(vbuf + plen, "-auth-", i = sizeof("-auth-") -1);
   i += plen;
   /* -USER@HOST, -HOST, '' */
   memcpy(vbuf + i, urlp->url_u_h_p.s, urlp->url_u_h_p.l +1);
   if ((s = vok_vlook(vbuf)) == NULL) {
      memcpy(vbuf + i, urlp->url_h_p.s, urlp->url_h_p.l +1);
      if ((s = vok_vlook(vbuf)) == NULL) {
         vbuf[plen + sizeof("-auth") -1] = '\0';
         if ((s = vok_vlook(vbuf)) == NULL)
            s = UNCONST(authdef);
      }
   }

   if (!asccasecmp(s, "none")) {
      ccp->cc_auth = "NONE";
      ccp->cc_authtype = AUTHTYPE_NONE;
      /*ware = NONE;*/
   } else if (!asccasecmp(s, "plain")) {
      ccp->cc_auth = "PLAIN";
      ccp->cc_authtype = AUTHTYPE_PLAIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!asccasecmp(s, "login")) {
      ccp->cc_auth = "LOGIN";
      ccp->cc_authtype = AUTHTYPE_LOGIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!asccasecmp(s, "cram-md5")) {
      ccp->cc_auth = "CRAM-MD5";
      ccp->cc_authtype = AUTHTYPE_CRAM_MD5;
      ware = REQ_PASS | REQ_USER;
   } else if (!asccasecmp(s, "gssapi")) {
      ccp->cc_auth = "GSS-API";
      ccp->cc_authtype = AUTHTYPE_GSSAPI;
      ware = REQ_USER;
#if 0 /* TODO SASL (homebrew `auto'matic indeed) */
   } else if (!asccasecmp(cp, "sasl")) {
      ware = REQ_USER | WANT_PASS;
#endif
   } /* no else */

   /* Verify method */
   if (!(ccp->cc_authtype & authmask)) {
      fprintf(stderr, tr(273, "Unsupported %s authentication method: %s\n"),
         pstr, s);
      ccp = NULL;
      goto jleave;
   }
#ifndef HAVE_MD5
   if (ccp->cc_authtype == AUTHTYPE_CRAM_MD5) {
      fprintf(stderr, tr(277, "No CRAM-MD5 support compiled in.\n"));
      ccp = NULL;
      goto jleave;
   }
#endif
#ifndef HAVE_GSSAPI
   if (ccp->cc_authtype == AUTHTYPE_GSSAPI) {
      fprintf(stderr, tr(272, "No GSS-API support compiled in.\n"));
      ccp = NULL;
      goto jleave;
   }
#endif

   /* Password */
   if ((ccp->cc_pass = urlp->url_pass).s != NULL)
      goto jleave;

   memcpy(vbuf, "password-", i = sizeof("password-") -1);
   /* -USER@HOST, -HOST, '' */
   memcpy(vbuf + i, urlp->url_u_h_p.s, urlp->url_u_h_p.l +1);
   if ((s = vok_vlook(vbuf)) == NULL) {
      memcpy(vbuf + i, urlp->url_h_p.s, urlp->url_h_p.l +1);
      if ((s = vok_vlook(vbuf)) == NULL) {
         vbuf[--i] = '\0';
         if ((s = vok_vlook(vbuf)) == NULL && (ware & REQ_PASS) &&
               (s = getpassword(NULL)) == NULL) {
            fprintf(stderr, tr(275,
               "A password is necessary for %s authentication.\n"), pstr);
            ccp = NULL;
            goto jleave;
         }
      }
   }
   if (s != NULL)
      ccp->cc_pass.l = strlen(ccp->cc_pass.s = savestr(s));

jleave:
   ac_free(vbuf);
   NYD_LEAVE;
   return (ccp != NULL);
}

#ifdef HAVE_MD5
FL char *
md5tohex(char hex[MD5TOHEX_SIZE], void const *vp)
{
   char const *cp = vp;
   size_t i, j;
   NYD_ENTER;

   for (i = 0; i < MD5TOHEX_SIZE / 2; ++i) {
      j = i << 1;
# define __hex(n) ((n) > 9 ? (n) - 10 + 'a' : (n) + '0')
      hex[j] = __hex((cp[i] & 0xF0) >> 4);
      hex[++j] = __hex(cp[i] & 0x0F);
# undef __hex
   }
   NYD_LEAVE;
   return hex;
}

FL char *
cram_md5_string(struct str const *user, struct str const *pass,
   char const *b64)
{
   struct str in, out;
   char digest[16], *cp;
   NYD_ENTER;

   out.s = NULL;
   in.s = UNCONST(b64);
   in.l = strlen(in.s);
   b64_decode(&out, &in, NULL);
   assert(out.s != NULL);

   hmac_md5((uc_it*)out.s, out.l, (uc_it*)pass->s, pass->l, digest);
   free(out.s);
   cp = md5tohex(salloc(MD5TOHEX_SIZE +1), digest);

   in.l = user->l + MD5TOHEX_SIZE +1;
   in.s = ac_alloc(user->l + 1 + MD5TOHEX_SIZE +1);
   memcpy(in.s, user->s, user->l);
   in.s[user->l] = ' ';
   memcpy(in.s + user->l + 1, cp, MD5TOHEX_SIZE);
   b64_encode(&out, &in, B64_SALLOC | B64_CRLF);
   ac_free(in.s);
   NYD_LEAVE;
   return out.s;
}

FL void
hmac_md5(unsigned char *text, int text_len, unsigned char *key, int key_len,
   void *digest)
{
   /*
    * This code is taken from
    *
    * Network Working Group                                       H. Krawczyk
    * Request for Comments: 2104                                          IBM
    * Category: Informational                                      M. Bellare
    *                                                                    UCSD
    *                                                              R. Canetti
    *                                                                     IBM
    *                                                           February 1997
    *
    *
    *             HMAC: Keyed-Hashing for Message Authentication
    */
   md5_ctx context;
   unsigned char k_ipad[65]; /* inner padding - key XORd with ipad */
   unsigned char k_opad[65]; /* outer padding - key XORd with opad */
   unsigned char tk[16];
   int i;
   NYD_ENTER;

   /* if key is longer than 64 bytes reset it to key=MD5(key) */
   if (key_len > 64) {
      md5_ctx tctx;

      md5_init(&tctx);
      md5_update(&tctx, key, key_len);
      md5_final(tk, &tctx);

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
   memset(k_ipad, 0, sizeof k_ipad);
   memset(k_opad, 0, sizeof k_opad);
   memcpy(k_ipad, key, key_len);
   memcpy(k_opad, key, key_len);

   /* XOR key with ipad and opad values */
   for (i=0; i<64; i++) {
      k_ipad[i] ^= 0x36;
      k_opad[i] ^= 0x5c;
   }

   /* perform inner MD5 */
   md5_init(&context);                    /* init context for 1st pass */
   md5_update(&context, k_ipad, 64);      /* start with inner pad */
   md5_update(&context, text, text_len);  /* then text of datagram */
   md5_final(digest, &context);           /* finish up 1st pass */

   /* perform outer MD5 */
   md5_init(&context);                 /* init context for 2nd pass */
   md5_update(&context, k_opad, 64);   /* start with outer pad */
   md5_update(&context, digest, 16);   /* then results of 1st hash */
   md5_final(digest, &context);        /* finish up 2nd pass */
   NYD_LEAVE;
}
#endif /* HAVE_MD5 */

/* vim:set fenc=utf-8:s-it-mode */
