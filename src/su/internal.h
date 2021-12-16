/*@ SU internal only interface.
 *@ Data instantiations all in core-code.c.
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
#ifndef su__INTERNAL_H
#define su__INTERNAL_H

/*#include <su/code.h>*/

#ifdef su_USECASE_SU
# include <su/mutex.h>
#endif

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

#if DVLOR(1, 0)
enum{
   su__NYD_ACTION_MASK = 0x3,
   su__NYD_ACTION_SHIFT = 29,
   su__NYD_ACTION_SHIFT_MASK = (1u << su__NYD_ACTION_SHIFT) - 1
};

struct su__nyd_info{
   char const *ni_file;
   char const *ni_fun;
   u32 ni_chirp_line;
   u32 ni_level;
};

struct su__nyd_control{
   u32 nc_level;
   u16 nc_curr;
   boole nc_skip;
   u8 nc__pad[1];
   struct su__nyd_info nc_infos[su_NYD_ENTRIES];
};

MCTA(su_NYD_ENTRIES <= U16_MAX, "Data type excess")
#endif /* DVLOR(1,0) */

struct su__state_on_gut{
   struct su__state_on_gut *sog_last;
   su_state_on_gut_fun sog_cb;
};

#if defined su_USECASE_SU && !su_ATOMIC_IS_REAL
extern struct su_mutex su__atomic_cas_mtx;
extern struct su_mutex su__atomic_xchg_mtx;
#endif

extern struct su__state_on_gut *su__state_on_gut;
extern struct su__state_on_gut *su__state_on_gut_final;

#ifdef su_HAVE_MD
extern s32 su__md_init(u32 estate);
#endif

C_DECL_END
#include <su/code-ou.h>
#endif /* !su__INTERNAL_H */
/* s-it-mode */
