/*@ Implementation of md.h.
 *
 * Copyright (c) 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_md
#define su_SOURCE
#define su_SOURCE_MD

#include "su/code.h"

su_USECASE_MX_DISABLED
su_EMPTY_FILE()
#ifdef su_HAVE_MD

#include "su/cs.h"
#include "su/md-siphash.h"
#include "su/mem.h"
#include "su/mutex.h"
#include "su/thread.h"

#include "su/internal.h" /* $(SU_SRCDIR) */

#include "su/md.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct a_md_list{
   struct a_md_list *mdl_last;
   char const *mdl_name;
   struct su_md_vtbl const *mdl_vtbl;
   su_new_fun mdl_cxx_it; /* Only if C++-created */
};

static struct su_mutex *a_md_lock, a__md_lockbuf;
static struct a_md_list *a_md_list;
/* + vtbl's below */

/* su__md_init() */
#ifdef su__STATE_ON_GUT_FUN
static void a_md__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags);
#endif

static void a_md_del(void *self);

static void *a_md_siphash_new(u32 estate);
static up a_md_siphash_prop(void const *self, enum su_md_prop prop);
static s32 a_md_siphash_setup(void *self, void const *k, uz kl, uz ds);

static struct su_md_vtbl const a_md_siphash = {
   FIN(mdvtbl_new) &a_md_siphash_new,
   FIN(mdvtbl_del) &a_md_del,
   FIN(mdvtbl_property) &a_md_siphash_prop,
   FIN(mdvtbl_setup) &a_md_siphash_setup,
   FIN(mdvtbl_update) R(void(*)(void*,void const*,uz),&su_siphash_update),
   FIN(mdvtbl_end) R(void(*)(void*,void*),&su_siphash_end)
};

#ifdef su__STATE_ON_GUT_FUN
static void
a_md__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags){
   NYD2_IN;
   UNUSED(flags);

# if DVLOR(1, 0)
   if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
      struct a_md_list *mdlp, *tmp;

      mdlp = a_md_list;
      a_md_list = NIL;
      while((tmp = mdlp) != NIL){
         mdlp = tmp->mdl_last;
         su_FREE(tmp);
      }

      su_mutex_gut(&a__md_lockbuf);
   }
# endif

   a_md_lock = NIL;

   NYD2_OU;
}
#endif /* su__STATE_ON_GUT_FUN */

static void
a_md_del(void *self){
   NYD2_IN;

   su_FREE(self);

   NYD2_OU;
}

static void *
a_md_siphash_new(u32 estate){
   struct su_siphash *self;
   NYD2_IN;

   self = su_TALLOCF(struct su_siphash, 1, estate);

   NYD2_OU;
   return self;
}

static up
a_md_siphash_prop(void const *self, enum su_md_prop prop){
   up rv;
   NYD2_IN;
   UNUSED(self);

   switch(S(uz,prop)){
   case su_MD_PROP_ALGO: rv = su_MD_ALGO_SIPHASH; break;
   case su_MD_PROP_NAME: rv = R(up,"siphash"); break;
   case su_MD_PROP_DISPLAY_NAME: rv = R(up,"SipHash"); break;
   case su_MD_PROP_KEY_SIZE_MIN: rv = su_SIPHASH_KEY_SIZE_MIN; break;
   case su_MD_PROP_KEY_SIZE_MAX: rv = su_SIPHASH_KEY_SIZE_MAX; break;
   case su_MD_PROP_DIGEST_SIZE_MIN: rv = su_SIPHASH_DIGEST_SIZE_MIN; break;
   case su_MD_PROP_DIGEST_SIZE_MAX: rv = su_SIPHASH_DIGEST_SIZE_MAX; break;
   case su_MD_PROP_BLOCK_SIZE: rv = su_SIPHASH_BLOCK_SIZE; break;
   default: rv = S(up,-1); break;
   }

   NYD2_OU;
   return rv;
}

static s32
a_md_siphash_setup(void *self, void const *k, uz kl, uz ds){
   s32 rv;
   NYD2_IN;

   if(kl != su_SIPHASH_KEY_SIZE ||
         (ds != su_SIPHASH_DIGEST_SIZE_64 && ds != su_SIPHASH_DIGEST_SIZE_128))
      rv = su_ERR_INVAL;
   else
      rv = su_siphash_setup_custom(S(struct su_siphash*,self), k,
            (ds == su_SIPHASH_DIGEST_SIZE_64 ? su_SIPHASH_DIGEST_64
               : su_SIPHASH_DIGEST_128), 0, 0);

   NYD2_OU;
   return rv;
}

/*extern*/s32
su__md_init(u32 estate){
   s32 rv;
   NYD2_IN;

   rv = su_STATE_NONE;

   su__glck_gi9r();

   if(a_md_lock == NIL &&
         (rv = su_mutex_create(&a__md_lockbuf, "SU M(essage) D(igest) DB",
               estate)) == su_STATE_NONE){
#ifdef su__STATE_ON_GUT_FUN
      if((rv = su_state_on_gut_install(&a_md__on_gut, TRU1, estate)
            ) != su_STATE_NONE)
         su_mutex_gut(&a__md_lockbuf);
      else
#endif
         a_md_lock = &a__md_lockbuf;
   }

   su__gnlck_gi9r();

   NYD2_OU;
   return rv;
}

s32
su__md_install(char const *name, struct su_md_vtbl const *vtblp,
      su_new_fun cxx_it, u32 estate){
   struct a_md_list *mdlp;
   s32 rv;
   NYD_IN;

   if(a_md_lock == NIL && (rv = su__md_init(estate)) != su_STATE_NONE)
      ;
   else if((mdlp = su_TALLOCF(struct a_md_list, 1, su_MEM_ALLOC_MAYFAIL)
         ) == NIL)
      rv = -su_err_no();
   else{
      su_MUTEX_LOCK(a_md_lock);
      mdlp->mdl_last = a_md_list;
      mdlp->mdl_name = name;
      mdlp->mdl_vtbl = vtblp;
      mdlp->mdl_cxx_it = cxx_it;
      a_md_list = mdlp;
      su_MUTEX_UNLOCK(a_md_lock);
   }

   NYD_OU;
   return rv;
}

boole
su__md_uninstall(char const *name, struct su_md_vtbl const *vtblp,
      su_new_fun cxx_it){
   struct a_md_list **mdlpp;
   boole rv;
   NYD_IN;

   rv = FAL0;

   if(a_md_lock != NIL){
      su_MUTEX_LOCK(a_md_lock);

      for(mdlpp = &a_md_list; *mdlpp != NIL; mdlpp = &(*mdlpp)->mdl_last)
         if(!su_cs_cmp_case(name, (*mdlpp)->mdl_name) &&
               vtblp == (*mdlpp)->mdl_vtbl &&
               (cxx_it == NIL || cxx_it == (*mdlpp)->mdl_cxx_it)){
            void *vp;

            vp = *mdlpp;
            *mdlpp = (*mdlpp)->mdl_last;
            su_FREE(vp);
            rv = TRU1;
            break;
         }

      su_MUTEX_UNLOCK(a_md_lock);
   }

   NYD_OU;
   return rv;
}

struct su_md *
su_md_new_by_algo(enum su_md_algo algo, u32 estate){
   struct su_md *self;
   struct su_md_vtbl const *vtblp;
   NYD_IN;

   estate &= su_STATE_ERR_MASK;

   switch(S(uz,algo)){
   case su_MD_ALGO_SIPHASH: vtblp = &a_md_siphash; break;
   default: /* FALLTHRU */
   case su_MD_ALGO_EXTRA: vtblp = NIL; break;
   }

   if(UNLIKELY(vtblp == NIL)){
      su_err_set_no(su_ERR_NOTSUP);
      self = NIL;
   }else if((self = su_TALLOCF(struct su_md, 1, estate)) != NIL){
      self->md_vtbl = vtblp;
      if((self->md_vp = (*vtblp->mdvtbl_new)(estate)) == NIL){
         su_THREAD_ERR_NO_SCOPE_IN();
            su_FREE(self);
         su_THREAD_ERR_NO_SCOPE_OU();
         self = NIL;
      }
   }

   NYD_OU;
   return self;
}

struct su_md *
su_md_new_by_name(char const *name, u32 estate){
   struct su_md *self;
   su_new_fun newptf;
   struct su_md_vtbl const *vtblp;
   NYD_IN;

   estate &= su_STATE_ERR_MASK;

   if(!su_cs_cmp_case(name, "siphash")){
      vtblp = &a_md_siphash;
      newptf = &a_md_siphash_new;
   }else if(a_md_lock != NIL){
      struct a_md_list *mdlp;

      su_MUTEX_LOCK(a_md_lock);

      vtblp = NIL;
      for(mdlp = a_md_list; mdlp != NIL; mdlp = mdlp->mdl_last)
         if(!su_cs_cmp_case(name, mdlp->mdl_name)){
            vtblp = mdlp->mdl_vtbl;
            newptf = (mdlp->mdl_cxx_it != NIL ? mdlp->mdl_cxx_it
                  : vtblp->mdvtbl_new);
            break;
         }

      su_MUTEX_UNLOCK(a_md_lock);
   }else
      goto jno;

   if(vtblp == NIL){
jno:
      su_err_set_no(su_ERR_NOTSUP);
      self = NIL;
   }else if((self = su_TALLOCF(struct su_md, 1, estate)) != NIL){
      self->md_vtbl = vtblp;
      if((self->md_vp = (*newptf)(estate)) == NIL){
         su_THREAD_ERR_NO_SCOPE_IN();
            su_FREE(self);
         su_THREAD_ERR_NO_SCOPE_OU();
         self = NIL;
      }
   }

   NYD_OU;
   return self;
}

void
su_md_del(struct su_md *self){
   NYD_IN;
   ASSERT(self);

   (*self->md_vtbl->mdvtbl_del)(self->md_vp);
   su_FREE(self);

   NYD_OU;
}

#include "su/code-ou.h"
#endif /* su_HAVE_MD */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_MD
/* s-it-mode */
