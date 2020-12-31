/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cred-oauthbearer.h.
 *@ XXX No format strings (or fmt-codec).
 *
 * Copyright (c) 2019 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cred_oauthbearer
#define mx_SOURCE
#define mx_SOURCE_CRED_OAUTHBEARER

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_NET
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/cred-auth.h"
#include "mx/mime-enc.h"
#include "mx/url.h"

#include "mx/cred-oauthbearer.h"
#include "su/code-in.h"

boole
mx_oauthbearer_create_icr(struct str *res, uz pre_len,
      struct mx_url const *urlp, struct mx_cred_ctx const *ccp,
      boole is_xoauth2){
   union {uz u; int s; struct str *xres; boole rv;} j;
   char *cp;
   NYD_IN;

   su_mem_set(res, 0, sizeof *res);
   cp = NIL;

   /* Calculate required storage */
#define a_MAX \
   (2 + sizeof("T18446744073709551615 AUTHENTICATE OAUTHBEARER " \
      "n,a=,\001host=\001port=65535\001auth=Bearer \001\001" NETNL))

   j.u = UZ_MAX - a_MAX;
   if(pre_len >= j.u ||
         ccp->cc_user.l >= (j.u -= pre_len) ||
         ccp->cc_pass.l >= (j.u -= ccp->cc_user.l) ||
         (!is_xoauth2 && urlp->url_host.l >= (j.u -= ccp->cc_pass.l))){
jerr_cred:
      n_err(_("Credentials overflow buffer sizes\n"));
      goto jleave;
   }

   j.u = ccp->cc_user.l + ccp->cc_pass.l;
   if(!is_xoauth2)
      j.u += urlp->url_host.l;

   if((j.u = mx_b64_enc_calc_size(j.u + a_MAX)) == UZ_MAX)
      goto jerr_cred;
#undef a_MAX

   res->s = su_LOFI_ALLOC(j.u);
   cp = su_LOFI_ALLOC(j.u);

   /* Credential string */
   if(!is_xoauth2)
      j.s = snprintf(cp, j.u,
            "n,a=%s,\001host=%s\001port=%hu\001auth=Bearer %s\001\001",
            ccp->cc_user.s, urlp->url_host.s, urlp->url_portno,
            ccp->cc_pass.s);
   else
      j.s = snprintf(cp, j.u, "user=%s\001auth=Bearer %s\001\001",
            ccp->cc_user.s, ccp->cc_pass.s);
   if(j.s < 0)
      goto jerr_io;

   res->s += pre_len;
   j.xres = mx_b64_enc_buf(res, cp, j.s, mx_B64_BUF);
   res->s -= pre_len;
   res->l += pre_len;

   if(j.xres == NIL){
jerr_io:
      su_LOFI_FREE(res->s);
      res->s = NIL;
      goto jleave;
   }

   su_mem_copy(&res->s[res->l], NETNL, sizeof(NETNL));
   res->l += sizeof(NETNL);

jleave:
   if(cp != NIL)
      su_LOFI_FREE(cp);

   if(!(j.rv = (res->s != NIL)))
      res->l = 0;

   NYD_OU;
   return j.rv;
}

#include <su/code-ou.h>
#endif /* mx_HAVE_NET */
/* s-it-mode */
