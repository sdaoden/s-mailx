/*@ C++ injection point of most things which need it.
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
#define su_FILE su_cxx_core
#define su_SOURCE

#include "su/code.h"
su_USECASE_MX_DISABLED

#include <stdarg.h>

#include "su/cs.h"
#include "su/utf.h"

#include "su/code-in.h"
NSPC_USE(su)

// code.h

STA void
log::write(BITENUM_IS(u32,level) lvl, char const *fmt, ...){ // XXX unroll
   va_list va;
   NYD_IN;

   va_start(va, fmt);
   su_log_vwrite(lvl, fmt, &va);
   va_end(va);
   NYD_OU;
}

// cs.h

STA type_toolbox<char*> const * const cs::type_toolbox =
      R(NSPC(su)type_toolbox<char*> const*,&su_cs_toolbox);
STA type_toolbox<char const*> const * const cs::const_type_toolbox =
      R(NSPC(su)type_toolbox<char const*> const*,&su_cs_toolbox);

STA type_toolbox<char*> const * const cs::type_toolbox_case =
      R(NSPC(su)type_toolbox<char*> const*,&su_cs_toolbox_case);
STA type_toolbox<char const*> const * const cs::const_type_toolbox_case =
      R(NSPC(su)type_toolbox<char const*> const*,&su_cs_toolbox_case);

// utf.h

STA char const utf8::replacer[sizeof su_UTF8_REPLACER] = su_UTF8_REPLACER;

#include "su/code-ou.h"
/* s-it-mode */
