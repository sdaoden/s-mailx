/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cred-auth.h.
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
#undef su_FILE
#define su_FILE cred_auth
#define mx_SOURCE
#define mx_SOURCE_CRED_AUTH

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_NET
#include <su/cs.h>
#include <su/mem.h>

#include "mx/cred-netrc.h"
#include "mx/tty.h"
#include "mx/url.h"

#ifdef mx_HAVE_SMTP
# include "mx/net-smtp.h"
#endif

#include "mx/cred-auth.h"
#include "su/code-in.h"

enum a_credauth_flags{
   a_CREDAUTH_NONE,
   a_CREDAUTH_AUTO = 1u<<0,
   a_CREDAUTH_AUTO_NOTLS = 1u<<1,
   a_CREDAUTH_NEED_TLS = 1u<<2,
   a_CREDAUTH_PASS_CLEARTXT = 1u<<3,
   a_CREDAUTH_PASS_REQ = 1u<<4,
   a_CREDAUTH_USER_REQ = 1u<<5,
   a_CREDAUTH_UNAVAIL = 1u<<15
};

/* Bitmasks of what really is available */
enum a_credauth_proto_realtypes{
   a_CREDAUTH_PROTO_REALTYPES_IMAP =
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
         mx_CRED_AUTHTYPE_OAUTHBEARER
#endif
         mx_CRED_AUTHTYPE_NONE,

   a_CREDAUTH_PROTO_REALTYPES_POP3 =
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

   a_CREDAUTH_PROTO_REALTYPES_SMTP =
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
         mx_CRED_AUTHTYPE_PLAIN |
#ifdef mx_HAVE_TLS
         mx_CRED_AUTHTYPE_XOAUTH2 |
#endif
         mx_CRED_AUTHTYPE_NONE
};

static struct mx_cred_authtype_info const
      a_credauth_info[mx_CRED_AUTHTYPE_MECH_COUNT] = {
   {mx_CRED_AUTHTYPE_CRAM_MD5,
      (a_CREDAUTH_AUTO | a_CREDAUTH_AUTO_NOTLS | a_CREDAUTH_PASS_REQ |
         a_CREDAUTH_USER_REQ
#ifndef mx_HAVE_MD5
         | a_CREDAUTH_UNAVAIL
#endif
      ), FAL0, FAL0, "CRAM-MD5", "CRAM-MD5"},

   {mx_CRED_AUTHTYPE_EXTERNAL,
      (a_CREDAUTH_NEED_TLS | a_CREDAUTH_USER_REQ
#ifndef mx_HAVE_TLS
         | a_CREDAUTH_UNAVAIL
#endif
      ), FAL0, TRU1, "EXTERNAL", "EXTERNAL"},

   {mx_CRED_AUTHTYPE_EXTERNANON,
      (a_CREDAUTH_NEED_TLS
#ifndef mx_HAVE_TLS
         | a_CREDAUTH_UNAVAIL
#endif
      ), FAL0, TRU1, "EXTERNANON", "EXTERNAL"},

   {mx_CRED_AUTHTYPE_GSSAPI,
      (a_CREDAUTH_USER_REQ
#ifndef mx_HAVE_GSSAPI
         | a_CREDAUTH_UNAVAIL
#endif
      ), FAL0, FAL0, "GSSAPI", "GSSAPI"},

   {mx_CRED_AUTHTYPE_LOGIN,
      (a_CREDAUTH_AUTO | a_CREDAUTH_PASS_CLEARTXT | a_CREDAUTH_PASS_REQ |
         a_CREDAUTH_USER_REQ),
      TRU1, FAL0, "LOGIN", "LOGIN"},

   {mx_CRED_AUTHTYPE_OAUTHBEARER,
      (a_CREDAUTH_NEED_TLS | a_CREDAUTH_PASS_CLEARTXT | a_CREDAUTH_PASS_REQ |
         a_CREDAUTH_USER_REQ
#ifndef mx_HAVE_TLS
         | a_CREDAUTH_UNAVAIL
#endif
      ), TRU1, TRU1, "OAUTHBEARER\0", "OAUTHBEARER\0"},

   {mx_CRED_AUTHTYPE_PLAIN,
      (a_CREDAUTH_AUTO | a_CREDAUTH_PASS_CLEARTXT | a_CREDAUTH_PASS_REQ |
         a_CREDAUTH_USER_REQ),
      TRU1, FAL0, "PLAIN", "PLAIN"},

   {mx_CRED_AUTHTYPE_XOAUTH2,
      (a_CREDAUTH_NEED_TLS | a_CREDAUTH_PASS_CLEARTXT | a_CREDAUTH_PASS_REQ |
         a_CREDAUTH_USER_REQ
#ifndef mx_HAVE_TLS
         | a_CREDAUTH_UNAVAIL
#endif
      ), TRU1, TRU1, "XOAUTH2", "XOAUTH2"}
};

/* Automatic selection order arrays for with TLS / without TLS */
CTAV(CPROTO_NONE == 0);
CTAV(CPROTO_IMAP == 1);
CTAV(CPROTO_POP3 == 2);
CTAV(CPROTO_SMTP == 3);
CTAV(mx_CRED_AUTHTYPE_MECH_COUNT == 8);
CTAV(mx_CRED_AUTHTYPE_NONE == 0);

static u8 const a_credauth_select[CPROTO_SMTP][mx_CRED_AUTHTYPE_MECH_COUNT] = {
   { /* IMAP */
#ifdef mx_HAVE_TLS
      mx_CRED_AUTHTYPE_EXTERNANON, mx_CRED_AUTHTYPE_EXTERNAL,
#endif
#ifdef mx_HAVE_GSSAPI
      mx_CRED_AUTHTYPE_GSSAPI,
#endif
      mx_CRED_AUTHTYPE_LOGIN,
      /*mx_CRED_AUTHTYPE_XOAUTH2,*/ mx_CRED_AUTHTYPE_OAUTHBEARER,
#ifdef mx_HAVE_MD5
      mx_CRED_AUTHTYPE_CRAM_MD5,
#endif
      mx_CRED_AUTHTYPE_NONE,
   },
   { /* POP3 */
#ifdef mx_HAVE_TLS
      mx_CRED_AUTHTYPE_EXTERNANON, mx_CRED_AUTHTYPE_EXTERNAL,
#endif
#ifdef mx_HAVE_GSSAPI
      mx_CRED_AUTHTYPE_GSSAPI,
#endif
      /*mx_CRED_AUTHTYPE_XOAUTH2,*/ mx_CRED_AUTHTYPE_OAUTHBEARER,
      mx_CRED_AUTHTYPE_PLAIN,
      mx_CRED_AUTHTYPE_NONE,
   },
   { /* SMTP */
#ifdef mx_HAVE_TLS
      mx_CRED_AUTHTYPE_EXTERNANON, mx_CRED_AUTHTYPE_EXTERNAL,
#endif
#ifdef mx_HAVE_GSSAPI
      mx_CRED_AUTHTYPE_GSSAPI,
#endif
      mx_CRED_AUTHTYPE_PLAIN,
      mx_CRED_AUTHTYPE_XOAUTH2, mx_CRED_AUTHTYPE_OAUTHBEARER,
#ifdef mx_HAVE_MD5
      mx_CRED_AUTHTYPE_CRAM_MD5,
#endif
      mx_CRED_AUTHTYPE_LOGIN,
#if !defined mx_HAVE_TLS || !defined mx_HAVE_GSSAPI || !defined mx_HAVE_MD5
      mx_CRED_AUTHTYPE_NONE,
#endif
   }
};

static u8 const a_credauth_select_notls[CPROTO_SMTP]
      [mx_CRED_AUTHTYPE_MECH_COUNT] = {
   { /* IMAP */
#ifdef mx_HAVE_GSSAPI
      mx_CRED_AUTHTYPE_GSSAPI,
#endif
#ifdef mx_HAVE_MD5
      mx_CRED_AUTHTYPE_CRAM_MD5,
#endif
      mx_CRED_AUTHTYPE_NONE,
   },
   { /* POP3 */
#ifdef mx_HAVE_GSSAPI
      mx_CRED_AUTHTYPE_GSSAPI,
#endif
      mx_CRED_AUTHTYPE_NONE,
   },
   { /* SMTP */
#ifdef mx_HAVE_GSSAPI
      mx_CRED_AUTHTYPE_GSSAPI,
#endif
#ifdef mx_HAVE_MD5
      mx_CRED_AUTHTYPE_CRAM_MD5,
#endif
      mx_CRED_AUTHTYPE_NONE,
   }
};

/* temporary (we'll have file://..) */
static char *a_credauth_last_at_before_slash(char const *cp);

static char *
a_credauth_last_at_before_slash(char const *cp){
   char const *xcp;
   char c;
   NYD2_IN;

   for(xcp = cp; (c = *xcp) != '\0'; ++xcp)
      if(c == '/')
         break;
   while(xcp > cp && *--xcp != '@')
      ;
   if(*xcp != '@')
      xcp = NIL;
   NYD2_OU;
   return UNCONST(char*,xcp);
}

boole
mx_cred_auth_lookup(struct mx_cred_ctx *credp, struct mx_url *urlp){
   char *s;
   char const *pstr, *authdef;
   u32 ware, authokey, authmask;
   boole issetup;
   NYD_IN;

   su_mem_set(credp, 0, sizeof *credp);
   credp->cc_user = urlp->url_user;
   ASSERT(urlp->url_user.s != NIL);

   issetup = FAL0;
   ware = a_CREDAUTH_NONE;

   switch((credp->cc_cproto = urlp->url_cproto)){
   case CPROTO_CCRED:
      pstr = "pseudo-host/ccred";
      ware = a_CREDAUTH_PASS_REQ;
      goto jpass;
   default:
   case CPROTO_SMTP:
#ifdef mx_HAVE_SMTP
      pstr = "smtp";
      /* C99 */{
      boole spc; /* v15-compat */

      if((spc = mx_smtp_parse_config(credp, urlp))){
         if(spc == TRUM1){
            authokey = ok_v_smtp_auth;
            authmask = mx_CRED_PROTO_AUTHTYPES_SMTP;
            authdef = "plain";
            break;
         }
         issetup = TRU1;
         ware = credp->cc_config;
         break;
      }
      }
#else
      credp = NIL;
      goto jleave;
#endif
   case CPROTO_POP3:
#ifdef mx_HAVE_POP3
      authokey = ok_v_pop3_auth;
      authmask = mx_CRED_PROTO_AUTHTYPES_POP3;
      authdef = "plain";
      pstr = "pop3";
      break;
#else
      credp = NIL;
      goto jleave;
#endif
   case CPROTO_IMAP:
#ifdef mx_HAVE_IMAP
      pstr = "imap";
      authokey = ok_v_imap_auth;
      authmask = mx_CRED_PROTO_AUTHTYPES_IMAP;
      authdef = "login";
      break;
#else
      credp = NIL;
      goto jleave;
#endif
   }

   if(issetup){
#ifdef mx_HAVE_TLS
      if((ware & mx_CRED_AUTHTYPE_NEED_TLS) &&
            !(urlp->url_flags & (mx_URL_TLS_REQUIRED | mx_URL_TLS_OPTIONAL))){
         n_err(_("TLS transport is required for %s\n"), urlp->url_p_u_h_p);
         credp = NIL;
         goto jleave;
      }
#endif
   }else{
      /* Authentication type */
      if(authokey == U32_MAX ||
            (s = xok_VLOOK(S(enum okeys,authokey), urlp, OXM_ALL)) == NIL)
         s = UNCONST(char*,authdef);

      for(ware = 0; ware < NELEM(a_credauth_info); ++ware){
         struct mx_cred_authtype_info const *caip;

         caip = &a_credauth_info[ware];
         if(!su_cs_cmp_case(s, caip->cai_user_name)){
            credp->cc_auth = caip->cai_name;
            credp->cc_authtype = caip->cai_type;
            if((ware = caip->cai_flags) & a_CREDAUTH_NEED_TLS)
               credp->cc_needs_tls = TRU1;

            if(ware & a_CREDAUTH_UNAVAIL){
               n_err(_("No %s support compiled in\n"), credp->cc_auth);
               credp = NIL;
               goto jleave;
            }
            break;
         }
      }

      /* Verify method */
      if(!(credp->cc_authtype & authmask)){
         n_err(_("Unsupported %s authentication method: %s\n"), pstr, s);
         credp = NIL;
         goto jleave;
      }

      if((ware & a_CREDAUTH_NEED_TLS)
#ifdef mx_HAVE_TLS
            && !(urlp->url_flags & (mx_URL_TLS_REQUIRED | mx_URL_TLS_OPTIONAL))
#endif
      ){
         n_err(_("TLS transport is required for authentication %s: %s\n"),
            credp->cc_auth, urlp->url_p_u_h_p);
         credp = NIL;
         goto jleave;
      }
   }

jpass:
   /* Password */
   if((credp->cc_pass = urlp->url_pass).s != NIL)
      goto jleave;

   if((s = xok_vlook(password, urlp, OXM_ALL)) != NIL)
      goto js2pass;

#ifdef mx_HAVE_NETRC
   if(xok_blook(netrc_lookup, urlp, OXM_ALL)){
      struct mx_netrc_entry nrce;

      if(mx_netrc_lookup(&nrce, urlp) && nrce.nrce_password != NIL){
         urlp->url_pass.s = savestr(nrce.nrce_password);
         urlp->url_pass.l = su_cs_len(urlp->url_pass.s);
         credp->cc_pass = urlp->url_pass;
         goto jleave;
      }
   }
#endif

   if(ware & a_CREDAUTH_PASS_REQ){
      if((s = mx_tty_getpass(savecat(urlp->url_u_h.s,
            _(" requires a password: ")))) != NIL)
js2pass:
         credp->cc_pass.l = su_cs_len(credp->cc_pass.s = savestr(s));
      else{
         n_err(_("%s authentication requires a password for %s\n"),
            pstr, urlp->url_u_h.s);
         credp = NIL;
      }
   }

jleave:
   if(credp != NIL && (n_poption & n_PO_D_VV))
      n_err(_("Credentials: host %s, user %s, pass %s\n"),
         urlp->url_h_p.s, n_shexp_quote_cp(credp->cc_user.s, FAL0),
         n_shexp_quote_cp(((n_poption & n_PO_D_VVV)
            ? (credp->cc_pass.s != NIL ? credp->cc_pass.s : su_empty)
            : _("3x *verbose* for password")), FAL0));

   NYD_OU;
   return (credp != NIL);
}

boole
mx_cred_auth_lookup_old(struct mx_cred_ctx *ccp, enum cproto cproto,
   char const *addr)
{
   char const *pname, *pxstr, *authdef, *s;
   uz pxlen, addrlen, i;
   char *vbuf = NULL;
   u32 authmask;
   enum {NONE=0, WANT_PASS=1<<0, REQ_PASS=1<<1, WANT_USER=1<<2, REQ_USER=1<<3}
      ware = NONE;
   boole addr_is_nuser = FAL0; /* XXX v15.0 legacy! v15_compat */
   NYD_IN;

   n_OBSOLETE(_("Use of old-style credentials, which will vanish in v15!\n"
      "  Please read the manual section "
         "\"On URL syntax and credential lookup\""));

   su_mem_set(ccp, 0, sizeof *ccp);

   switch (cproto) {
   default:
   case CPROTO_SMTP:
#ifdef mx_HAVE_SMTP
      pname = "SMTP";
      pxstr = "smtp-auth";
      pxlen = sizeof("smtp-auth") -1;
      authmask = mx_CRED_AUTHTYPE_PLAIN | mx_CRED_AUTHTYPE_LOGIN |
            mx_CRED_AUTHTYPE_CRAM_MD5 | mx_CRED_AUTHTYPE_GSSAPI;
      authdef = "none";
      addr_is_nuser = TRU1;
      break;
#else
      ccp = NULL;
      goto jleave;
#endif
   case CPROTO_POP3:
#ifdef mx_HAVE_POP3
      pname = "POP3";
      pxstr = "pop3-auth";
      pxlen = sizeof("pop3-auth") -1;
      authmask = mx_CRED_AUTHTYPE_PLAIN;
      authdef = "plain";
      break;
#else
      ccp = NULL;
      goto jleave;
#endif
   case CPROTO_IMAP:
#ifdef mx_HAVE_IMAP
      pname = "IMAP";
      pxstr = "imap-auth";
      pxlen = sizeof("imap-auth") -1;
      authmask = mx_CRED_AUTHTYPE_LOGIN |
            mx_CRED_AUTHTYPE_CRAM_MD5 | mx_CRED_AUTHTYPE_GSSAPI;
      authdef = "login";
      break;
#else
      ccp = NULL;
      goto jleave;
#endif
   }

   ccp->cc_cproto = cproto;
   addrlen = su_cs_len(addr);
   vbuf = n_lofi_alloc(pxlen + addrlen + sizeof("-password-")-1 +1);
   su_mem_copy(vbuf, pxstr, pxlen);

   /* Authentication type */
   vbuf[pxlen] = '-';
   su_mem_copy(vbuf + pxlen + 1, addr, addrlen +1);
   if ((s = n_var_vlook(vbuf, FAL0)) == NULL) {
      vbuf[pxlen] = '\0';
      if ((s = n_var_vlook(vbuf, FAL0)) == NULL)
         s = n_UNCONST(authdef);
   }

   if (!su_cs_cmp_case(s, "none")) {
      ccp->cc_auth = "NONE";
      ccp->cc_authtype = 0;
      /*ware = NONE;*/
   } else if (!su_cs_cmp_case(s, "plain")) {
      ccp->cc_auth = "PLAIN";
      ccp->cc_authtype = mx_CRED_AUTHTYPE_PLAIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "login")) {
      ccp->cc_auth = "LOGIN";
      ccp->cc_authtype = mx_CRED_AUTHTYPE_LOGIN;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "cram-md5")) {
      ccp->cc_auth = "CRAM-MD5";
      ccp->cc_authtype = mx_CRED_AUTHTYPE_CRAM_MD5;
      ware = REQ_PASS | REQ_USER;
   } else if (!su_cs_cmp_case(s, "gssapi")) {
      ccp->cc_auth = "GSS-API";
      ccp->cc_authtype = mx_CRED_AUTHTYPE_GSSAPI;
      ware = REQ_USER;
   } /* no else */

   /* Verify method */
   if (!(ccp->cc_authtype & authmask)) {
      n_err(_("Unsupported %s authentication method: %s\n"), pname, s);
      ccp = NULL;
      goto jleave;
   }
# ifndef mx_HAVE_MD5
   if (ccp->cc_authtype == mx_CRED_AUTHTYPE_CRAM_MD5) {
      n_err(_("No CRAM-MD5 support compiled in\n"));
      ccp = NULL;
      goto jleave;
   }
# endif
# ifndef mx_HAVE_GSSAPI
   if (ccp->cc_authtype == mx_CRED_AUTHTYPE_GSSAPI) {
      n_err(_("No GSS-API support compiled in\n"));
      ccp = NULL;
      goto jleave;
   }
# endif

   /* User name */
   if (!(ware & (WANT_USER | REQ_USER)))
      goto jpass;

   if(!addr_is_nuser){
      if((s = a_credauth_last_at_before_slash(addr)) != NIL){
         char *cp;

         cp = savestrbuf(addr, P2UZ(s - addr));

         if((ccp->cc_user.s = mx_url_xdec(cp)) == NIL){
            n_err(_("String is not properly URL percent encoded: %s\n"), cp);
            ccp = NULL;
            goto jleave;
         }
         ccp->cc_user.l = su_cs_len(ccp->cc_user.s);
      } else if (ware & REQ_USER)
         goto jgetuser;
      goto jpass;
   }

   su_mem_copy(vbuf + pxlen, "-user-", i = sizeof("-user-") -1);
   i += pxlen;
   su_mem_copy(vbuf + i, addr, addrlen +1);
   if ((s = n_var_vlook(vbuf, FAL0)) == NULL) {
      vbuf[--i] = '\0';
      if ((s = n_var_vlook(vbuf, FAL0)) == NULL && (ware & REQ_USER)) {
         if((s = mx_tty_getuser(NIL)) == NIL){
jgetuser:   /* TODO v15.0: today we simply bail, but we should call getuser().
             * TODO even better: introduce "PROTO-user" and "PROTO-pass" and
             * TODO check that first, then! change control flow, grow vbuf */
            n_err(_("A user is necessary for %s authentication\n"), pname);
            ccp = NULL;
            goto jleave;
         }
      }
   }
   ccp->cc_user.l = su_cs_len(ccp->cc_user.s = savestr(s));

   /* Password */
jpass:
   if (!(ware & (WANT_PASS | REQ_PASS)))
      goto jleave;

   if (!addr_is_nuser) {
      su_mem_copy(vbuf, "password-", i = sizeof("password-") -1);
   } else {
      su_mem_copy(vbuf + pxlen, "-password-", i = sizeof("-password-") -1);
      i += pxlen;
   }
   su_mem_copy(vbuf + i, addr, addrlen +1);
   if ((s = n_var_vlook(vbuf, FAL0)) == NULL) {
      vbuf[--i] = '\0';
      if ((!addr_is_nuser || (s = n_var_vlook(vbuf, FAL0)) == NULL) &&
            (ware & REQ_PASS)){
         if((s = mx_tty_getpass(savecat(pname, _(" requires a password: ")))
               ) == NIL){
            n_err(_("A password is necessary for %s authentication\n"),
               pname);
            ccp = NIL;
            goto jleave;
         }
      }
   }
   if (s != NULL)
      ccp->cc_pass.l = su_cs_len(ccp->cc_pass.s = savestr(s));

jleave:
   if(vbuf != NULL)
      n_lofi_free(vbuf);
   if (ccp != NULL && (n_poption & n_PO_D_VV))
      n_err(_("Credentials: host %s, user %s, pass %s\n"),
         addr, (ccp->cc_user.s != NULL ? ccp->cc_user.s : n_empty),
         (ccp->cc_pass.s != NULL ? ccp->cc_pass.s : n_empty));
   NYD_OU;
   return (ccp != NULL);
}

struct mx_cred_authtype_info const *
mx_cred_auth_type_find_name(char const *name, boole usr){
   struct mx_cred_authtype_info const *caip, *caipmax;
   NYD_IN;
   ASSERT_NYD_EXEC(name != NIL, caip = NIL);

   caipmax = &(caip = a_credauth_info)[NELEM(a_credauth_info)];
   for(;;){
      if(!su_cs_cmp_case(name, (usr ? caip->cai_user_name : caip->cai_name))){
         if(caip->cai_flags & a_CREDAUTH_UNAVAIL){
            if(usr || (n_poption & n_PO_D_V))
               n_err(_("Authentication method not available: %s\n"),
                  n_shexp_quote_cp(name, FAL0));
            caip = NIL;
         }
         break;
      }

      if(++caip == caipmax){
         if(usr){
            if(!su_cs_cmp_case(name, "allmechs")){
               caip = R(struct mx_cred_authtype_info*,-1);
               break;
            }
            n_err(_("Unknown authentication method: %s\n"),
               n_shexp_quote_cp(name, FAL0));
         }
         caip = NIL;
         break;
      }
   }

   NYD_OU;
   return caip;
}

struct mx_cred_authtype_info const *
mx_cred_auth_type_find_type(u32 type){
   struct mx_cred_authtype_info const *caip, *caipmax;
   NYD_IN;

   caipmax = &(caip = a_credauth_info)[NELEM(a_credauth_info)];
   for(;;){
      if(type == caip->cai_type)
         break;
      if(++caip == caipmax){
         caip = NIL;
         break;
      }
   }
   NYD_OU;
   return caip;
}

boole
mx_cred_auth_type_verify_bits(struct mx_cred_authtype_verify_ctx *cavcp,
      boole maybe_tls){
   char const *pstr;
   u32 bits, atmask, atrealmask, i;
   boole rv;
   NYD_IN;

   rv = TRU1;
   bits = cavcp->cavc_mechplusbits;

   switch(cavcp->cavc_proto){
   case CPROTO_IMAP:
      bits &= (atmask = mx_CRED_PROTO_AUTHTYPES_IMAP);
      atrealmask = a_CREDAUTH_PROTO_REALTYPES_IMAP;
      pstr = "IMAP";
      break;
   case CPROTO_POP3:
      bits &= (atmask = mx_CRED_PROTO_AUTHTYPES_POP3);
      atrealmask = a_CREDAUTH_PROTO_REALTYPES_POP3;
      pstr = "POP3";
      break;
   case CPROTO_SMTP:
      bits &= (atmask = mx_CRED_PROTO_AUTHTYPES_SMTP);
      atrealmask = a_CREDAUTH_PROTO_REALTYPES_SMTP;
      pstr = "SMTP";
      break;
   default:
      bits = 0;
      goto jleave;
   }

   if(bits == 0)
      goto jleave;

   for(cavcp->cavc_cnt = i = 0; i < NELEM(a_credauth_info); ++i){
      struct mx_cred_authtype_info const *caip;

      caip = &a_credauth_info[i];
      if(bits & caip->cai_type){
         if(!(caip->cai_flags & a_CREDAUTH_UNAVAIL)){
            if(caip->cai_flags & a_CREDAUTH_NEED_TLS){
               if(!(maybe_tls)){
                  if(n_poption & n_PO_D_V)
                     n_err(_("%s: %s (%s) needs TLS, connection will not "
                           "use it: disabled\n"),
                        pstr, caip->cai_user_name, caip->cai_name);
                  goto jxor;
               }
               bits |= mx_CRED_AUTHTYPE_NEED_TLS;
            }
            if(++cavcp->cavc_cnt > 1)
               bits |= mx_CRED_AUTHTYPE_MULTI;
         }else{
            n_err(_("%s: no %s (%s) support available\n"),
               pstr, caip->cai_user_name, caip->cai_name);
jxor:
            if((bits ^= caip->cai_type) == mx_CRED_AUTHTYPE_NONE)
               break;
         }
      }
   }

   if((bits & atmask) == atrealmask)
      rv = TRUM1;
   else if(bits == mx_CRED_AUTHTYPE_NONE){
      n_err(_("%s: no usable authentication types (remain)\n"), pstr);
      rv = FAL0;
   }

jleave:
   cavcp->cavc_mechplusbits = bits;
   NYD_OU;
   return rv;
}

u32
mx_cred_auth_type_select(enum cproto proto, u32 mechplusbits,
      boole assume_tls){
   u8 const (*mp)[mx_CRED_AUTHTYPE_MECH_COUNT];
   char const *pstr;
   u32 rv, i;
   NYD_IN;

   switch(proto){
   case CPROTO_IMAP:
      rv = mx_CRED_PROTO_AUTHTYPES_IMAP;
      pstr = "IMAP";
      break;
   case CPROTO_POP3:
      rv = mx_CRED_PROTO_AUTHTYPES_POP3;
      pstr = "POP3";
      break;
   case CPROTO_SMTP:
      /* SMTP supports authentication-less mode */
      rv = mx_CRED_PROTO_AUTHTYPES_SMTP;
      if((mechplusbits & rv) == mx_CRED_AUTHTYPE_NONE){
         rv = mx_CRED_AUTHTYPE_NONE;
         goto jleave;
      }
      pstr = "SMTP";
      break;
   default:
      rv = U32_MAX;
      goto jleave;
   }

   /* (proto-1 CTA assert at array defs) */
   mp = &(assume_tls ? a_credauth_select : a_credauth_select_notls
         )[S(u32,proto) - 1];

   for(i = mx_CRED_AUTHTYPE_NONE;;)
      if(mechplusbits & (rv = (*mp)[i])){
         if(n_poption & n_PO_D_V)
            n_err(_("%s: authentication: selecting %s\n"),
               pstr, mx_cred_auth_type_find_type(rv)->cai_name);
         break;
      }else if(rv == mx_CRED_AUTHTYPE_NONE ||
            ++i == mx_CRED_AUTHTYPE_MECH_COUNT){
         n_err(_("%s: no authentication type can actually be used\n"), pstr);
         rv = U32_MAX;
         break;
      }

jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_NET */
/* s-it-mode */
