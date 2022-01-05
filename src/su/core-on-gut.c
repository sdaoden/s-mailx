/*@ Implementation of code.h: su_state_on_gut_*().
 *@ XXX (Mis)Uses su__GLCK_STATE for list protection!?
 *
 * Copyright (c) 2021 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_core_on_gut
#define su_SOURCE
#define su_SOURCE_CORE_ON_GUT

#include "su/mem.h"

#include "su/internal.h" /* $(SU_SRCDIR) */

#include "su/code.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#ifdef su__STATE_ON_GUT_FUN
static struct su__state_on_gut a_corgut_mgr;
#endif

#ifdef su__STATE_ON_GUT_FUN
static void a_corgut_fun(BITENUM_IS(u32,su_state_gut_flags) flags);
#endif

#ifdef su__STATE_ON_GUT_FUN
static void
a_corgut_fun(BITENUM_IS(u32,su_state_gut_flags) flags){
   NYD_IN;

# if DVLOR(1, 0)
   if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
      struct su__state_on_gut **sogpp, *tmp;

      sogpp = &su__state_on_gut;
jredo:
      while((tmp = *sogpp) != NIL){
         *sogpp = tmp->sog_last;
         if(tmp == &a_corgut_mgr)
            break;
         su_FREE(tmp);
      }
      if(*(sogpp = &su__state_on_gut_final) != NIL)
         goto jredo;

      su_CC_MEM_ZERO(&a_corgut_mgr, sizeof(a_corgut_mgr));

      if(flags & su_STATE_GUT_MEM_TRACE){
         enum su_log_level ol;

         su_mem_set_conf(su_MEM_CONF_LINGER_FREE_RELEASE, 0);

         ol = su_log_get_level();
         su_log_set_level(su_LOG_INFO);
         su_mem_trace(TRU1);
         su_log_set_level(ol);
      }
   }
# endif /* DVLOR(1,0) */

   NYD_OU;
}
#endif /* su__STATE_ON_GUT_FUN */

s32
su_state_on_gut_install(su_state_on_gut_fun hdl, boole is_final, u32 estate){
   struct su__state_on_gut *sogp, **sogpp;
   s32 rv;
   NYD_IN;
   ASSERT_NYD_EXEC(hdl != NIL, rv = -su_ERR_FAULT);

   estate &= su_STATE_ERR_MASK;

   if((sogp = su_TALLOCF(struct su__state_on_gut, 1, estate)) != NIL){
      sogpp = is_final ? &su__state_on_gut_final : &su__state_on_gut;

      su__glck(su__GLCK_STATE);

#ifdef su__STATE_ON_GUT_FUN
      if(a_corgut_mgr.sog_cb == NIL){
         a_corgut_mgr.sog_cb = &a_corgut_fun;
         su__state_on_gut_final = &a_corgut_mgr;
      }
#endif

      sogp->sog_last = *sogpp;
      *sogpp = sogp;
      sogp->sog_cb = hdl;

      su__gnlck(su__GLCK_STATE);

      rv = su_STATE_NONE;
   }else
      rv = su_STATE_ERR_NOMEM;

   NYD_OU;
   return rv;
}

boole
su_state_on_gut_uninstall(su_state_on_gut_fun hdl, boole is_final){
   struct su__state_on_gut *sogp, **sogpp;
   NYD_IN;
   ASSERT_NYD_EXEC(hdl != NIL, sogp = NIL);

   sogpp = is_final ? &su__state_on_gut_final : &su__state_on_gut;

   su__glck(su__GLCK_STATE);

   while((sogp = *sogpp) != NIL){
      if(sogp->sog_cb == hdl){
         *sogpp = sogp->sog_last;
         break;
      }else
         sogpp = &sogp->sog_last;
   }

   su__gnlck(su__GLCK_STATE);

   if(sogp != NIL){
#ifdef su__STATE_ON_GUT_FUN
      ASSERT(sogp != &a_corgut_mgr);
#endif
      su_FREE(sogp);
   }

   NYD_OU;
   return (sogp != NIL);
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CORE_ON_GUT
/* s-it-mode */
