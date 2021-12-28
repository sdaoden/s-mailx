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
#define su_FILE su_cxx_core
#define su_SOURCE
#define su_SOURCE_CXX_CORE_CODE

#include "su/code.h"

su_USECASE_MX_DISABLED

#include <stdarg.h>

#include "su/avopt.h"
#include "su/cs.h"
#include "su/path.h"
#include "su/utf.h"

#ifdef su_HAVE_MD
# include "su/md.h"
# include "su/mem.h"
# include "su/thread.h"
#endif

/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"
NSPC_USE(su)

// avopt.h

STA char const * const avopt::fmt_err_arg = su_avopt_fmt_err_arg;
STA char const * const avopt::fmt_err_opt = su_avopt_fmt_err_opt;

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

// path.h

STA char const path::dev_null[sizeof su_PATH_DEV_NULL] = su_PATH_DEV_NULL;

// utf.h

STA char const utf8::replacer[sizeof su_UTF8_REPLACER] = su_UTF8_REPLACER;

//
// Conditionalized
//

// md.h
#ifdef su_HAVE_MD
namespace{
   class a_md;

   static void a_md_marshal_del(void *self);
   static up a_md_marshal_prop(void const *self, su_md_prop prop);
   static s32 a_md_marshal_setup(void *self, void const *key, uz key_len,
         uz digest_size);
   static void a_md_marshal_update(void *self, void const *dat, uz dat_len);
   static void a_md_marshal_end(void *self, void *store);

   static su_md_vtbl const a_md_marshal_vtbl = {
      FIN(mdvtbl_new) NIL,
      FIN(mdvtbl_del) &a_md_marshal_del,
      FIN(mdvtbl_property) &a_md_marshal_prop,
      FIN(mdvtbl_setup) &a_md_marshal_setup,
      FIN(mdvtbl_update) &a_md_marshal_update,
      FIN(mdvtbl_end) &a_md_marshal_end
   };

   static void
   a_md_marshal_del(void *self){
      su_DEL(S(NSPC(su)md*,self));
   }

   static up
   a_md_marshal_prop(void const *self, su_md_prop prop){
      return S(NSPC(su)md const*,self)->property(S(md::prop,prop));
   }

   static s32
   a_md_marshal_setup(void *self, void const *key, uz key_len, uz digest_size){
      return S(NSPC(su)md*,self)->setup(key, key_len, digest_size);
   }

   static void
   a_md_marshal_update(void *self, void const *dat, uz dat_len){
      S(NSPC(su)md*,self)->update(dat, dat_len);
   }

   static void
   a_md_marshal_end(void *self, void *store){
      S(NSPC(su)md*,self)->end(store);
   }

   class a_md : public md{
      struct su_md *m_md;
   public:
      a_md(struct su_md *mdp) : m_md(mdp) {}

      OVRX ~a_md(void) {su_md_del(m_md);}

      OVRX up property(prop prop) const{
         up rv;
         NYD_IN;

         rv = su_md_property(m_md, S(su_md_prop,prop));

         NYD_OU;
         return rv;
      }

      OVRX s32 setup(void const *key, uz key_len, uz digest_size){
         s32 rv;
         NYD_IN;
         ASSERT_NYD_EXEC(key_len == 0 || key != NIL, rv = err::efault);

         rv = su_md_setup(m_md, key, key_len, digest_size);

         NYD_OU;
         return rv;
      }

      OVRX void update(void const *dat, uz dat_len){
         NYD_IN;
         ASSERT_NYD(dat_len == 0 || dat != NIL);

         if(dat_len > 0)
            su_md_update(m_md, dat, dat_len);

         NYD_OU;
      }

      OVRX void end(void *store){
         NYD_IN;
         ASSERT_NYD(store != NIL);

         su_md_end(m_md, store);

         NYD_OU;
      }
   };
}

STA md *
md::new_by_algo(algo algo, u32 estate){
   md *rv;
   struct su_md *mdp;
   NYD_IN;

   estate &= state::err_mask;

   mdp = su_md_new_by_algo(S(su_md_algo,algo), estate);

   if(mdp == NIL)
      rv = NIL;
   else{
      su_NEWF_BLK(rv, a_md, estate, (mdp));
      if(rv == NIL){
         su_THREAD_ERR_NO_SCOPE_IN();
            su_md_del(mdp);
         su_THREAD_ERR_NO_SCOPE_OU();
      }
   }

   NYD_OU;
   return rv;
}

STA md *
md::new_by_name(char const *name, u32 estate){
   md *rv;
   struct su_md *mdp;
   NYD_IN;
   ASSERT_NYD_EXEC(name != NIL, rv = NIL);

   estate &= state::err_mask;

   mdp = su_md_new_by_name(name, estate);

   if(mdp == NIL)
      rv = NIL;
   else{
      su_NEWF_BLK(rv, a_md, estate, (mdp));
      if(rv == NIL){
         su_THREAD_ERR_NO_SCOPE_IN();
            su_md_del(mdp);
         su_THREAD_ERR_NO_SCOPE_OU();
      }
   }

   NYD_OU;
   return rv;
}

STA s32
md::install(char const *name, md *(*ctor)(u32 estate), u32 estate){
   s32 rv;
   NYD_IN;
   ASSERT_NYD_EXEC(name != NIL, rv = -err::efault);
   ASSERT_NYD_EXEC(ctor != NIL, rv = -err::efault);

   rv = su__md_install(name, &a_md_marshal_vtbl, R(su_new_fun,ctor), estate);

   NYD_OU;
   return rv;
}

STA boole
md::uninstall(char const *name, md *(*ctor)(u32 estate)){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(name != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(ctor != NIL, rv = FAL0);

   rv = su__md_uninstall(name, &a_md_marshal_vtbl, R(su_new_fun,ctor));

   NYD_OU;
   return rv;
}
#endif // su_HAVE_MD

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_CXX_CORE_CODE
/* s-it-mode */
