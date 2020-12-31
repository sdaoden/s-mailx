/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of privacy.h.
 *
 * Copyright (c) 2015 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE privacy
#define mx_SOURCE
#define mx_SOURCE_PRIVACY

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#if defined mx_HAVE_XTLS
#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/privacy.h"
#include "su/code-in.h"

boole
mx_privacy_sign_is_desired(void){
   boole rv;
   NYD_IN;

   rv = ok_blook(smime_sign); /* TODO USER@HOST <-> *from* +++!!! */
#ifndef mx_HAVE_TLS
   if(rv){
      n_err(_("No TLS support compiled in\n"));
      rv = FAL0;
   }
#endif

   NYD_OU;
   return rv;
}

FILE *
mx_privacy_sign(FILE *ip, char const *addr){
   FILE *rv;
   NYD_IN;
   UNUSED(ip);

   if(addr == NIL){
      n_err(_("No *from* address for signing specified\n"));
      rv = NIL;
      goto jleave;
   }

#ifdef mx_HAVE_TLS
   rv = smime_sign(ip, addr);
#else
   n_err(_("No TLS support compiled in\n"));
   rv = NIL;
#endif

jleave:
   NYD_OU;
   return rv;
}

boole
mx_privacy_verify(struct message *mp, int nr){
   NYD_IN;

   UNUSED(mp);
   UNUSED(nr);

   NYD_OU;
   return FAL0;
}

FILE *
mx_privacy_encrypt_try(FILE *ip, char const *to){
   char const k[] = "smime-encrypt-";
   uz nl;
   char const *cp;
   char *vs;
   FILE *rv;
   NYD_IN;

   nl = su_cs_len(to);
   vs = su_LOFI_ALLOC(sizeof(k)-1 + nl +1);
   su_mem_copy(vs, k, sizeof(k) -1);
   su_mem_copy(&vs[sizeof(k) -1], to, nl +1);

   if((cp = n_var_vlook(vs, FAL0)) != NIL){
#ifdef mx_HAVE_TLS
      rv = smime_encrypt(ip, cp, to);
      goto jleave;
#else
      n_err(_("No TLS support compiled in\n"));
#endif
   }

   rv = R(FILE*,-1);
jleave:
   su_LOFI_FREE(vs);

   NYD_OU;
   return rv;
}

boole
mx_privacy_encrypt_is_forced(void){
   boole rv;
   NYD_IN;

   rv = ok_blook(smime_force_encryption);
   NYD_OU;
   return rv;
}

struct message *
mx_privacy_decrypt(struct message *mp, char const *to, char const *cc,
      boole is_a_verify_call){
   NYD_IN;

   UNUSED(mp);
   UNUSED(to);
   UNUSED(cc);
   UNUSED(is_a_verify_call);

   NYD_OU;
   return NIL;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_XTLS */
/* s-it-mode */
