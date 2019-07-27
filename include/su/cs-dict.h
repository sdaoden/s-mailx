/*@ Dictionary with char* keys.
 *
 * Copyright (c) 2001 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_CS_DICT_H
#define su_CS_DICT_H
#include <su/code.h>
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
struct su_cs_dict;
struct su_cs_dict_view;
enum su_cs_dict_flags{
   su_CS_DICT_POW2_SPACED = 1u<<0,
   su_CS_DICT_OWNS = 1u<<1,
   su_CS_DICT_CASE = 1u<<2,
   su_CS_DICT_HEAD_RESORT = 1u<<3,
   su_CS_DICT_AUTO_SHRINK = 1u<<4,
   su_CS_DICT_FROZEN = 1u<<5,
   su_CS_DICT_ERR_PASS = su_STATE_ERR_PASS,
   su_CS_DICT_NIL_IS_VALID_OBJECT = su_STATE_ERR_NIL_IS_VALID_OBJECT,
   su_CS_DICT_NILISVALO = su_CS_DICT_NIL_IS_VALID_OBJECT,
   su__CS_DICT_CREATE_MASK = su_CS_DICT_POW2_SPACED |
         su_CS_DICT_OWNS | su_CS_DICT_CASE |
         su_CS_DICT_HEAD_RESORT | su_CS_DICT_AUTO_SHRINK | su_CS_DICT_FROZEN |
         su_CS_DICT_ERR_PASS | su_CS_DICT_NIL_IS_VALID_OBJECT
};
#ifdef su_SOURCE_CS_DICT
CTA((su_STATE_ERR_MASK & ~0xFF00u) == 0,
   "Reuse of low order bits impossible, or mask excesses storage");
#endif
struct su_cs_dict{
   struct su__cs_dict_node **csd_array;
   u16 csd_flags;
   u8 csd_tshift;
   u8 csd__pad[su_6432(5,1)];
   u32 csd_count;
   u32 csd_size;
   struct su_toolbox const *csd_tbox;
};
struct su__cs_dict_node{
   struct su__cs_dict_node *csdn_next;
   void *csdn_data;
   uz csdn_khash;
   u32 csdn_klen;
   char csdn_key[su_VFIELD_SIZE(4)];
};
/* "The const is preserved logically" */
EXPORT struct su__cs_dict_node *su__cs_dict_lookup(
      struct su_cs_dict const *self, char const *key, void *lookarg_or_nil);
EXPORT s32 su__cs_dict_insrep(struct su_cs_dict *self, char const *key,
      void *value, boole replace);
#if DVLOR(1, 0)
EXPORT void su__cs_dict_stats(struct su_cs_dict const *self);
#endif
#define su_CS_DICT_FOREACH(SELF,VIEWNAME) \
   for(su_cs_dict_view_begin(su_cs_dict_view_setup(VIEWNAME, SELF));\
         su_cs_dict_view_is_valid(VIEWNAME);\
         su_cs_dict_view_next(VIEWNAME))
EXPORT struct su_cs_dict *su_cs_dict_create(struct su_cs_dict *self,
      u16 flags, struct su_toolbox const *tbox_or_nil);
EXPORT SHADOW struct su_cs_dict *su_cs_dict_create_copy(
      struct su_cs_dict *self, struct su_cs_dict const *t);
EXPORT void su_cs_dict_gut(struct su_cs_dict *self);
EXPORT s32 su_cs_dict_assign(struct su_cs_dict *self,
      struct su_cs_dict const *t);
EXPORT s32 su_cs_dict_assign_elems(struct su_cs_dict *self,
      struct su_cs_dict const *t);
EXPORT struct su_cs_dict *su_cs_dict_clear(struct su_cs_dict *self);
EXPORT struct su_cs_dict *su_cs_dict_clear_elems(struct su_cs_dict *self);
EXPORT struct su_cs_dict *su_cs_dict_swap(struct su_cs_dict *self,
      struct su_cs_dict *t);
INLINE u16 su_cs_dict_flags(struct su_cs_dict const *self){
   ASSERT(self);
   return self->csd_flags;
}
INLINE struct su_cs_dict *su_cs_dict_add_flags(struct su_cs_dict *self,
      u16 flags){
   ASSERT(self);
   flags &= su__CS_DICT_CREATE_MASK;
   self->csd_flags |= flags;
   return self;
}
INLINE struct su_cs_dict *su_cs_dict_clear_flags(struct su_cs_dict *self,
      u16 flags){
   ASSERT(self);
   flags &= su__CS_DICT_CREATE_MASK;
   self->csd_flags &= ~flags;
   return self;
}
INLINE u8 su_cs_dict_treshold_shift(struct su_cs_dict const *self){
   ASSERT(self);
   return self->csd_tshift;
}
INLINE struct su_cs_dict *su_cs_dict_set_treshold_shift(
      struct su_cs_dict *self, u8 ntshift){
   ASSERT(self);
   self->csd_tshift = CLIP(ntshift, 1, 8);
   return self;
}
INLINE struct su_toolbox const *su_cs_dict_toolbox(
      struct su_cs_dict const *self){
   ASSERT(self);
   return self->csd_tbox;
}
INLINE struct su_cs_dict *su_cs_dict_set_toolbox(struct su_cs_dict *self,
      struct su_toolbox const *tbox_or_nil){
   ASSERT(self);
   ASSERT(!(su_cs_dict_flags(self) & su_CS_DICT_OWNS) ||
      (tbox_or_nil != NIL && tbox_or_nil->tb_clone != NIL &&
       tbox_or_nil->tb_delete != NIL && tbox_or_nil->tb_assign != NIL));
   self->csd_tbox = tbox_or_nil;
   return self;
}
INLINE u32 su_cs_dict_count(struct su_cs_dict const *self){
   ASSERT(self);
   return self->csd_count;
}
EXPORT struct su_cs_dict *su_cs_dict_balance(struct su_cs_dict *self);
INLINE boole su_cs_dict_has_key(struct su_cs_dict const *self,
      char const *key){
   ASSERT(self);
   ASSERT_RET(key != NIL, FAL0);
   return (su__cs_dict_lookup(self, key, NIL) != NIL);
}
INLINE void *su_cs_dict_lookup(struct su_cs_dict *self, char const *key){
   struct su__cs_dict_node *csdnp;
   ASSERT(self != NIL);
   ASSERT_RET(key != NIL, NIL);
   csdnp = su__cs_dict_lookup(self, key, NIL);
   return (csdnp != NIL) ? csdnp->csdn_data : NIL;
}
INLINE s32 su_cs_dict_insert(struct su_cs_dict *self, char const *key,
      void *value){
   ASSERT(self);
   ASSERT_RET(key != NIL, 0);
   return su__cs_dict_insrep(self, key, value, FAL0);
}
INLINE s32 su_cs_dict_replace(struct su_cs_dict *self, char const *key,
      void *value){
   ASSERT(self);
   ASSERT_RET(key != NIL, 0);
   return su__cs_dict_insrep(self, key, value, TRU1);
}
EXPORT boole su_cs_dict_remove(struct su_cs_dict *self, char const *key);
INLINE void su_cs_dict_statistics(struct su_cs_dict const *self){
   UNUSED(self);
#if DVLOR(1, 0)
   su__cs_dict_stats(self);
#endif
}
enum su__cs_dict_view_move_types{
   su__CS_DICT_VIEW_MOVE_BEGIN,
   su__CS_DICT_VIEW_MOVE_HAS_NEXT,
   su__CS_DICT_VIEW_MOVE_NEXT
};
struct su_cs_dict_view{
   struct su_cs_dict *csdv_parent;
   struct su__cs_dict_node *csdv_node;
   u32 csdv_index;
   /* Those only valid after _move(..HAS_NEXT) */
   u32 csdv_next_index;
   struct su__cs_dict_node *csdv_next_node;
};
/* "The const is preserved logically" */
EXPORT struct su_cs_dict_view *su__cs_dict_view_move(
      struct su_cs_dict_view *self, u8 type);
#define su_CS_DICT_VIEW_FOREACH(SELF) \
   for(su_cs_dict_view_begin(SELF); su_cs_dict_view_is_valid(SELF);\
         su_cs_dict_view_next(SELF))
INLINE struct su_cs_dict_view *su_cs_dict_view_setup(
      struct su_cs_dict_view *self, struct su_cs_dict *parent){
   ASSERT(self);
   self->csdv_parent = parent;
   self->csdv_node = NIL;
   ASSERT_RET(parent != NIL, self);
   return self;
}
INLINE struct su_cs_dict *su_cs_dict_view_parent(
      struct su_cs_dict_view const *self){
   ASSERT(self);
   return self->csdv_parent;
}
INLINE boole su_cs_dict_view_is_valid(struct su_cs_dict_view const *self){
   ASSERT(self);
   return (self->csdv_node != NIL);
}
INLINE struct su_cs_dict_view *su_cs_dict_view_invalidate(
      struct su_cs_dict_view *self){
   ASSERT(self);
   self->csdv_node = NIL;
   return self;
}
INLINE char const *su_cs_dict_view_key(struct su_cs_dict_view const *self){
   ASSERT(self);
   ASSERT_RET(su_cs_dict_view_is_valid(self), NIL);
   return self->csdv_node->csdn_key;
}
INLINE u32 su_cs_dict_view_key_len(struct su_cs_dict_view const *self){
   ASSERT(self);
   ASSERT_RET(su_cs_dict_view_is_valid(self), 0);
   return self->csdv_node->csdn_klen;
}
INLINE uz su_cs_dict_view_key_hash(struct su_cs_dict_view const *self){
   ASSERT(self);
   ASSERT_RET(su_cs_dict_view_is_valid(self), 0);
   return self->csdv_node->csdn_khash;
}
INLINE void *su_cs_dict_view_data(struct su_cs_dict_view const *self){
   ASSERT(self);
   ASSERT_RET(su_cs_dict_view_is_valid(self), NIL);
   return self->csdv_node->csdn_data;
}
EXPORT s32 su_cs_dict_view_set_data(struct su_cs_dict_view *self, void *value);
INLINE struct su_cs_dict_view *su_cs_dict_view_begin(
      struct su_cs_dict_view *self){
   ASSERT(self);
   return su__cs_dict_view_move(self, su__CS_DICT_VIEW_MOVE_BEGIN);
}
INLINE boole su_cs_dict_view_has_next(struct su_cs_dict_view const *self){
   ASSERT(self);
   ASSERT_RET(su_cs_dict_view_is_valid(self), FAL0);
   return (su__cs_dict_view_move(C(struct su_cs_dict_view*,self),
      su__CS_DICT_VIEW_MOVE_HAS_NEXT)->csdv_next_node != NIL);
}
INLINE struct su_cs_dict_view *su_cs_dict_view_next(
      struct su_cs_dict_view *self){
   ASSERT(self);
   ASSERT_RET(su_cs_dict_view_is_valid(self), self);
   return su__cs_dict_view_move(self, su__CS_DICT_VIEW_MOVE_NEXT);
}
EXPORT boole su_cs_dict_view_find(struct su_cs_dict_view *self,
      char const *key);
EXPORT struct su_cs_dict_view *su_cs_dict_view_remove(
      struct su_cs_dict_view *self);
INLINE sz su_cs_dict_view_cmp(struct su_cs_dict_view const *self,
      struct su_cs_dict_view const *t){
   ASSERT_RET(self, -(t != NIL));
   ASSERT_RET(t, 1);
   return (self->csdv_node == t->csdv_node);
}
C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# include <su/view.h>
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)
template<class T,boole OWNS> class cs_dict;
template<class T, boole OWNS=type_traits<T>::ownguess>
class cs_dict : private su_cs_dict{
   class gview;
   class gview : private su_cs_dict_view{
   public:
      // xxx clang 5.0.1 BUG: needed this-> to find superclass field
      gview(void) {this->csdv_parent = NIL; this->csdv_node = NIL;}
      gview(gview const &t) {*this = t;}
      ~gview(void) {}
      gview &operator=(gview const &t){
         SELFTHIS_RET(*S(su_cs_dict_view*,this) =
               *S(su_cs_dict_view const*,&t));
      }
      gview &setup(su_cs_dict &parent){
         SELFTHIS_RET(su_cs_dict_view_setup(this, &parent));
      }
      boole is_setup(void) const {return su_cs_dict_view_parent(this) != NIL;}
      boole is_same_parent(gview const &t) const{
         return su_cs_dict_view_parent(this) == su_cs_dict_view_parent(&t);
      }
      boole is_valid(void) const {return su_cs_dict_view_is_valid(this);}
      gview &invalidate(void){
         SELFTHIS_RET(su_cs_dict_view_invalidate(this));
      }
      char const *key(void) const {return su_cs_dict_view_key(this);}
      void *data(void) {return su_cs_dict_view_data(this);}
      void const *data(void) const {return su_cs_dict_view_data(this);}
      s32 set_data(void *value) {return su_cs_dict_view_set_data(this, value);}
      gview &begin(void) {SELFTHIS_RET(su_cs_dict_view_begin(this));}
      boole has_next(void) const {return su_cs_dict_view_has_next(this);}
      gview &next(void) {SELFTHIS_RET(su_cs_dict_view_next(this));}
      boole find(void const *key){
         return su_cs_dict_view_find(this, S(char const*,key));
      }
      gview &remove(void) {SELFTHIS_RET(su_cs_dict_view_remove(this));}
      sz cmp(gview const &t) const {return su_cs_dict_view_cmp(this, &t);}
   };
public:
   enum flags{
      f_none,
      f_pow2_spaced = su_CS_DICT_POW2_SPACED,
      f_case = su_CS_DICT_CASE,
      f_head_resort = su_CS_DICT_HEAD_RESORT,
      f_auto_shrink = su_CS_DICT_AUTO_SHRINK,
      f_frozen = su_CS_DICT_FROZEN,
      f_err_pass = su_CS_DICT_ERR_PASS,
      f_nil_is_valid_object = su_CS_DICT_NIL_IS_VALID_OBJECT,
      f_nilisvalo = su_CS_DICT_NILISVALO
   };
   typedef NSPC(su)type_traits<T> type_traits;
   typedef typename type_traits::type_toolbox type_toolbox;
   typedef typename type_traits::auto_type_toolbox auto_type_toolbox;
   typedef typename type_traits::tp tp;
   typedef typename type_traits::tp_const tp_const;
   typedef NSPC(su)view_traits<su_cs_dict,char const *,T>
      view_traits;
   friend class NSPC(su)view_assoc_unidir<view_traits, gview>;
   friend class NSPC(su)view_assoc_unidir_const<view_traits, gview>;
   typedef NSPC(su)view_assoc_unidir<view_traits, gview> view;
   typedef NSPC(su)view_assoc_unidir_const<view_traits, gview> view_const;
   cs_dict(type_toolbox const *ttbox=NIL, u16 flags=f_none){
      ASSERT(!OWNS || (ttbox != NIL && ttbox->ttb_clone != NIL &&
         ttbox->ttb_delete != NIL && ttbox->ttb_assign != NIL));
      flags &= su__CS_DICT_CREATE_MASK & ~su_CS_DICT_OWNS;
      if(OWNS)
         flags |= su_CS_DICT_OWNS;
      su_cs_dict_create(this, flags, R(su_toolbox const*,ttbox));
   }
   SHADOW cs_dict(cs_dict const &t) {su_cs_dict_create_copy(this, &t);}
   ~cs_dict(void) {su_cs_dict_gut(this);}
   s32 assign(cs_dict const &t) {return su_cs_dict_assign(this, &t);}
   SHADOW cs_dict &operator=(cs_dict const &t) {SELFTHIS_RET(assign(t));}
   s32 assign_elems(cs_dict const &t){
      return su_cs_dict_assign_elems(this, &t);
   }
   cs_dict &clear(void) {SELFTHIS_RET(su_cs_dict_clear(this));}
   cs_dict &clear_elems(void) {SELFTHIS_RET(su_cs_dict_clear_elems(this));}
   cs_dict &swap(cs_dict &t) {SELFTHIS_RET(su_cs_dict_swap(this, &t));}
   u16 flags(void) const {return (su_cs_dict_flags(this) & ~su_CS_DICT_OWNS);}
   cs_dict &add_flags(u16 flags){
      SELFTHIS_RET(su_cs_dict_add_flags(this, flags & ~su_CS_DICT_OWNS));
   }
   cs_dict &clear_flags(u16 flags){
      SELFTHIS_RET(su_cs_dict_clear_flags(this, flags & ~su_CS_DICT_OWNS));
   }
   u8 treshold_shift(void) const {return su_cs_dict_treshold_shift(this);}
   cs_dict &set_treshold_shift(u8 tshift){
      SELFTHIS_RET(su_cs_dict_set_treshold_shift(this, tshift));
   }
   type_toolbox const *toolbox(void) const{
      return R(type_toolbox const*,su_cs_dict_toolbox(this));
   }
   cs_dict &set_toolbox(type_toolbox const *tbox_or_nil){
      SELFTHIS_RET(su_cs_dict_set_toolbox(this,
         R(su_toolbox const*,tbox_or_nil)));
   }
   u32 count(void) const {return csd_count;}
   boole is_empty(void) const {return (count() == 0);}
   cs_dict &balance(void) {SELFTHIS_RET(su_cs_dict_balance(this));}
   boole has_key(char const *key) const{
      ASSERT_RET(key != NIL, FAL0);
      return su_cs_dict_has_key(this, key);
   }
   tp lookup(char const *key){
      ASSERT_RET(key != NIL, NIL);
      return type_traits::to_tp(su_cs_dict_lookup(this, key));
   }
   tp operator[](char const *key) {return lookup(key);}
   tp_const lookup(char const *key) const{
      ASSERT_RET(key != NIL, NIL);
      return type_traits::to_const_tp(su_cs_dict_lookup(C(su_cs_dict*,this),
         key));
   }
   tp_const operator[](char const *key) const {return lookup(key);}
   s32 insert(char const *key, tp_const value){
      ASSERT_RET(key != NIL, 0);
      return su_cs_dict_insert(this, key, type_traits::to_vp(value));
   }
   s32 replace(char const *key, tp_const value){
      ASSERT_RET(key != NIL, 0);
      return su_cs_dict_replace(this, key, type_traits::to_vp(value));
   }
   boole remove(char const *key){
      ASSERT_RET(key != NIL, FAL0);
      return su_cs_dict_remove(this, key);
   }
   void statistics(void) const {su_cs_dict_statistics(this);}
};
NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */
#endif /* su_CS_DICT_H */
/* s-it-mode */
