/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cred-auth.h.
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

#include "mx/cred-auth.h"
#include "su/code-in.h"

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
mx_cred_auth_lookup(struct mx_cred_ctx *ccp, struct mx_url *urlp){
   enum{
      a_NONE,
      a_WANT_PASS = 1u<<0,
      a_REQ_PASS = 1u<<1,
      a_WANT_USER = 1u<<2,
      a_REQ_USER = 1u<<3,
      a_NEED_TLS = 1u<<4,
      a_UNAVAIL = 1u<<7

   };

   static struct authtype{
      char const at_user_name[16];
      char const at_name[13];
      u8 at_ware;
      u16 at_type;
   } const ata[] = {
      {"none", "NONE", a_NONE, mx_CRED_AUTHTYPE_NONE},

      {"plain", "PLAIN", (a_REQ_PASS | a_REQ_USER), mx_CRED_AUTHTYPE_PLAIN},
      {"login", "LOGIN", (a_REQ_PASS | a_REQ_USER), mx_CRED_AUTHTYPE_LOGIN},
      {"oauthbearer\0", "OAUTHBEARER\0", (a_REQ_PASS | a_REQ_USER |a_NEED_TLS),
         mx_CRED_AUTHTYPE_OAUTHBEARER},
      {"external", "EXTERNAL", (a_REQ_USER | a_NEED_TLS),
         mx_CRED_AUTHTYPE_EXTERNAL},
      {"externanon\0", "EXTERNAL", a_NEED_TLS, mx_CRED_AUTHTYPE_EXTERNANON},

      {"cram-md5", "CRAM-MD5", (a_REQ_PASS | a_REQ_USER
#ifndef mx_HAVE_MD5
            | a_UNAVAIL
#endif
         ), mx_CRED_AUTHTYPE_CRAM_MD5},
      {"gssapi", "GSS-API", (a_REQ_USER
#ifndef mx_HAVE_GSSAPI
            | a_UNAVAIL
#endif
         ), mx_CRED_AUTHTYPE_GSSAPI}
   };

   char *s;
   char const *pstr, *authdef;
   u16 authmask;
   enum okeys authokey;
   u32 ware;
   NYD_IN;

   su_mem_set(ccp, 0, sizeof *ccp);
   ccp->cc_user = urlp->url_user;
   ASSERT(urlp->url_user.s != NIL);

   ware = a_NONE;

   switch((ccp->cc_cproto = urlp->url_cproto)){
   case CPROTO_CCRED:
      authokey = R(enum okeys,-1);
      authmask = mx_CRED_AUTHTYPE_PLAIN;
      authdef = "plain";
      pstr = "ccred";
      break;
   default:
   case CPROTO_SMTP:
      authokey = ok_v_smtp_auth;
      authmask = mx_CRED_AUTHTYPE_NONE |
            mx_CRED_AUTHTYPE_PLAIN | mx_CRED_AUTHTYPE_LOGIN |
            mx_CRED_AUTHTYPE_OAUTHBEARER |
            mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
            mx_CRED_AUTHTYPE_CRAM_MD5 |
            mx_CRED_AUTHTYPE_GSSAPI;
      authdef = "plain";
      pstr = "smtp";
      break;
   case CPROTO_POP3:
      authokey = ok_v_pop3_auth;
      authmask = mx_CRED_AUTHTYPE_PLAIN |
            mx_CRED_AUTHTYPE_OAUTHBEARER |
            mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
            mx_CRED_AUTHTYPE_GSSAPI;
      authdef = "plain";
      pstr = "pop3";
      break;
#ifdef mx_HAVE_IMAP
   case CPROTO_IMAP:
      pstr = "imap";
      authokey = ok_v_imap_auth;
      authmask = mx_CRED_AUTHTYPE_LOGIN |
            mx_CRED_AUTHTYPE_OAUTHBEARER |
            mx_CRED_AUTHTYPE_EXTERNAL | mx_CRED_AUTHTYPE_EXTERNANON |
            mx_CRED_AUTHTYPE_CRAM_MD5 |
            mx_CRED_AUTHTYPE_GSSAPI;
      authdef = "login";
      break;
#endif
   }

   /* Authentication type XXX table driven iter */
   if(authokey == R(enum okeys,-1) ||
         (s = xok_VLOOK(authokey, urlp, OXM_ALL)) == NIL)
      s = UNCONST(char*,authdef);

   for(ware = 0; ware < NELEM(ata); ++ware){
      struct authtype const *atp;

      if(!su_cs_cmp_case(s, (atp = &ata[ware])->at_user_name)){
         ccp->cc_auth = atp->at_name;
         ccp->cc_authtype = atp->at_type;
         if((ware = atp->at_ware) & a_NEED_TLS)
            ccp->cc_needs_tls = TRU1;

         if(ware & a_UNAVAIL){
            n_err(_("No %s support compiled in\n"), ccp->cc_auth);
            ccp = NIL;
            goto jleave;
         }
         break;
      }
   }

   /* Verify method */
   if(!(ccp->cc_authtype & authmask)){
      n_err(_("Unsupported %s authentication method: %s\n"), pstr, s);
      ccp = NIL;
      goto jleave;
   }

   if((ware & a_NEED_TLS)
#ifdef mx_HAVE_TLS
         && !(urlp->url_flags & (mx_URL_TLS_REQUIRED | mx_URL_TLS_OPTIONAL))
#endif
   ){
      n_err(_("TLS transport is required for authentication %s: %s\n"),
         ccp->cc_auth, urlp->url_p_u_h_p);
      ccp = NIL;
      goto jleave;
   }

   /* Password */
   ccp->cc_pass = urlp->url_pass;
   if(ccp->cc_pass.s != NIL)
      goto jleave;

   if((s = xok_vlook(password, urlp, OXM_ALL)) != NIL)
      goto js2pass;

#ifdef mx_HAVE_NETRC
   if(xok_blook(netrc_lookup, urlp, OXM_ALL)){
      struct mx_netrc_entry nrce;

      if(mx_netrc_lookup(&nrce, urlp) && nrce.nrce_password != NIL){
         urlp->url_pass.s = savestr(nrce.nrce_password);
         urlp->url_pass.l = su_cs_len(urlp->url_pass.s);
         ccp->cc_pass = urlp->url_pass;
         goto jleave;
      }
   }
#endif

   if(ware & a_REQ_PASS){
      if((s = mx_tty_getpass(savecat(urlp->url_u_h.s,
            _(" requires a password: ")))) != NIL)
js2pass:
         ccp->cc_pass.l = su_cs_len(ccp->cc_pass.s = savestr(s));
      else{
         n_err(_("A password is necessary for %s authentication\n"), pstr);
         ccp = NIL;
      }
   }

jleave:
   if(ccp != NIL && (n_poption & n_PO_D_VV))
      n_err(_("Credentials: host %s, user %s, pass %s\n"),
         urlp->url_h_p.s, n_shexp_quote_cp(ccp->cc_user.s, FAL0),
         n_shexp_quote_cp((ccp->cc_pass.s != NIL ? ccp->cc_pass.s : su_empty),
            FAL0));
   NYD_OU;
   return (ccp != NIL);
}

boole
mx_cred_auth_lookup_old(struct mx_cred_ctx *ccp, enum cproto cproto,
   char const *addr)
{
   char const *pname, *pxstr, *authdef, *s;
   uz pxlen, addrlen, i;
   char *vbuf;
   u8 authmask;
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
      pname = "SMTP";
      pxstr = "smtp-auth";
      pxlen = sizeof("smtp-auth") -1;
      authmask = mx_CRED_AUTHTYPE_NONE |
            mx_CRED_AUTHTYPE_PLAIN | mx_CRED_AUTHTYPE_LOGIN |
            mx_CRED_AUTHTYPE_CRAM_MD5 | mx_CRED_AUTHTYPE_GSSAPI;
      authdef = "none";
      addr_is_nuser = TRU1;
      break;
   case CPROTO_POP3:
      pname = "POP3";
      pxstr = "pop3-auth";
      pxlen = sizeof("pop3-auth") -1;
      authmask = mx_CRED_AUTHTYPE_PLAIN;
      authdef = "plain";
      break;
#ifdef mx_HAVE_IMAP
   case CPROTO_IMAP:
      pname = "IMAP";
      pxstr = "imap-auth";
      pxlen = sizeof("imap-auth") -1;
      authmask = mx_CRED_AUTHTYPE_LOGIN |
            mx_CRED_AUTHTYPE_CRAM_MD5 | mx_CRED_AUTHTYPE_GSSAPI;
      authdef = "login";
      break;
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
      ccp->cc_authtype = mx_CRED_AUTHTYPE_NONE;
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
   n_lofi_free(vbuf);
   if (ccp != NULL && (n_poption & n_PO_D_VV))
      n_err(_("Credentials: host %s, user %s, pass %s\n"),
         addr, (ccp->cc_user.s != NULL ? ccp->cc_user.s : n_empty),
         (ccp->cc_pass.s != NULL ? ccp->cc_pass.s : n_empty));
   NYD_OU;
   return (ccp != NULL);
}

#include "su/code-ou.h"
#endif /* mx_HAVE_NET */
/* s-it-mode */
