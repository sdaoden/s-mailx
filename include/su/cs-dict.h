/*@ Dictionary with char* keys.
 *
 * Copyright (c) 2001 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
/* *lookarg_or_nil is always updated */
EXPORT s32 su__cs_dict_insrep(struct su_cs_dict *self, char const *key,
      void *value, up replace_and_view_or_nil);
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
INLINE struct su_cs_dict_view *su_cs_dict_view_reset(
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
INLINE s32 su_cs_dict_view_reset_insert(struct su_cs_dict_view *self,
      char const *key, void *value){
   ASSERT(self);
   ASSERT_RET(key != NIL, 0);
   return su__cs_dict_insrep(self->csdv_parent, key, value, FAL0 | R(up,self));
}
INLINE s32 su_cs_dict_view_reset_replace(struct su_cs_dict_view *self,
      char const *key, void *value){
   ASSERT(self);
   ASSERT_RET(key != NIL, 0);
   return su__cs_dict_insrep(self->csdv_parent, key, value, TRU1 | R(up,self));
}
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
#endif /* su_CS_DICT_H */
/* s-it-mode */
