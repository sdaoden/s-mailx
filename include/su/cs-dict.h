/*@ Dictionary with char* keys.
 *
 * Copyright (c) 2001 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

/*!
 * \file
 * \ingroup CS_DICT
 * \brief \r{CS_DICT}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_cs_dict;
struct su_cs_dict_view;

/* cs_dict {{{ */
/*!
 * \defgroup CS_DICT Dictionary with C-style string keys
 * \ingroup COLL
 * \brief Dictionary with C-style string keys (\r{su/cs-dict.h})
 *
 * A dictionary for unique non-\NIL \c{char*} keys with a maximum length of \r{su_S32_MAX} bytes.
 * The view type is the associative unidirectional \r{su_cs_dict_view}: \r{CS_DICT_VIEW}.
 *
 * \list{\li{
 * Keys will be stored in full in the list nodes which make up the dictionary.
 * They will be hashed and compared by means of \r{CS} (ASCII).
 * }\li{
 * Data values are optionally owned (\r{su_CS_DICT_OWNS}), in which case the given,
 * then mandatory \r{su_toolbox} is used to manage data value objects.
 * Dependent upon \r{su_CS_DICT_NIL_IS_VALID_OBJECT} \NIL values will still be supported.
 * }\li{
 * Most characteristics can be adjusted via \r{su_cs_dict_add_flags()} and \r{su_cs_dict_clear_flags()}.
 * Doing so can cause crashes or tear memory holes later on!
 * The only flags safe to adjust at any time are \r{su_CS_DICT_AUTO_SHRINK}, \r{su_CS_DICT_FROZEN},
 * and \r{su_CS_DICT_HEAD_RESORT}, as below, all others should only be changed directly after
 * \r{su_cs_dict_create()} or \r{su_cs_dict_clear()}.
 * }\li{
 * \r{su_STATE_ERR_PASS} may be enforced on a per-object level by setting \r{su_CS_DICT_ERR_PASS}.
 * This interacts with \r{su_CS_DICT_NIL_IS_VALID_OBJECT}.
 * }\li{
 * The balance of array size / node count spacing relation is controllable via \r{su_cs_dict_set_threshold()}.
 * Automatic shrinks are unaffected by that and happen only if \r{su_CS_DICT_AUTO_SHRINK} is enabled.
 * }\li{
 * It is possible to turn instances into \r{su_CS_DICT_FROZEN} state.
 * When inserting/removing many key/value tuples it may increase efficiency to temporarily freeze the dictionary,
 * then call \r{su_cs_dict_balance()} to thaw the object and make it reflect its new situation, if so desired.
 * (Alternatively the flag may simply be cleared again.)
 * }\li{
 * In-array-index list node head resorting can be controlled via \r{su_CS_DICT_HEAD_RESORT}.
 * In an ideal situation this can help improve lookup times.
 * }}
 *
 * \remarks{This collection does not offer \fn{hash()} and \fn{compare()} functions,
 * because these operations would be too expensive: for reproducable,
 * and thus correct results a temporary sorted copy would be necessary.
 * Due to lack of this functionality it also does not offer a \r{su_toolbox}
 * instance to operate on object-instances of itself.}
 * @{
 */

/* dict {{{ */
/*! Flags for \r{su_cs_dict_create()}, to be queried via \r{su_cs_dict_flags()},
 * and to be adjusted via \r{su_cs_dict_add_flags()} and \r{su_cs_dict_clear_flags()}. */
enum su_cs_dict_flags{
	su_CS_DICT_OWNS = 1u<<0, /*!< Data values are owned. */
	/*! If this is set, prime spacing and modulo indexing will be used.
	 * By default power-of-two spacing and mask indexing is used, which provides
	 * good distribution of "normal" key data via \r{su_cs_hash()}, as well as
	 * with "all" key data via \r{su_cs_hash_strong()}, which is used in
	 * conjunction with \r{su_CS_DICT_STRONG}. */
	su_CS_DICT_PRIME_SPACED = 1u<<1,
	/*! Enable automatic shrinking of management array(s).
	 * It occurs when \r{su_cs_dict_count()} is half of \r{su_cs_dict_size()}.
	 * This is not enabled by default (array(s) only grow(s)). */
	su_CS_DICT_AUTO_SHRINK = 1u<<2,
	/*! Freeze the dictionary.
	 * A frozen dictionary will neither grow nor shrink management array(s) of
	 * nodes automatically. */
	su_CS_DICT_FROZEN = 1u<<3,
	/*! Enable array-index list head rotation?
	 * This dictionary uses nodes which form singly-linked lists.
	 * With this bit set, whenever a key is found in such a list, its list node
	 * will become the new head of the list, potentially improving lookup speed
	 * due to lists becoming sorted by "hotness" over time. */
	su_CS_DICT_HEAD_RESORT = 1u<<4,
	su_CS_DICT_STRONG = 1u<<5, /*!< Strong key hashing shall be used (\r{su_cs_hash_strong_cbuf()}). */
	su_CS_DICT_CASE = 1u<<6, /*!< Keys shall be hashed, compared and stored case-insensitively. */
	/*! In conjunction with \r{su_cs_dict_set_data_space()}, do not copy over non-\NIL data to the internal
	 * reserved data space, but keep that uninitialized. */
	su_CS_DICT_DATA_SPACE_RAW = 1u<<7,
	su_CS_DICT_ERR_PASS = su_STATE_ERR_PASS, /*!< Mapped to \r{su_STATE_ERR_PASS}. */
	/*! Mapped to \r{su_STATE_ERR_NIL_IS_VALID_OBJECT}, but only honoured for
	 * dictionaries which \r{su_CS_DICT_OWNS} its data values. */
	su_CS_DICT_NIL_IS_VALID_OBJECT = su_STATE_ERR_NIL_IS_VALID_OBJECT,
	su_CS_DICT_NILISVALO = su_CS_DICT_NIL_IS_VALID_OBJECT, /*!< Alias for \r{su_CS_DICT_NIL_IS_VALID_OBJECT}. */

	su__CS_DICT_CREATE_MASK = su_CS_DICT_OWNS |
			su_CS_DICT_PRIME_SPACED |
			su_CS_DICT_AUTO_SHRINK | su_CS_DICT_FROZEN |
			su_CS_DICT_HEAD_RESORT |
			su_CS_DICT_STRONG | su_CS_DICT_CASE | su_CS_DICT_DATA_SPACE_RAW |
			su_CS_DICT_ERR_PASS | su_CS_DICT_NIL_IS_VALID_OBJECT
};
#ifdef su_SOURCE_CS_DICT
CTA((su_STATE_ERR_MASK & ~0xFF00u) == 0, "Reuse of low order bits impossible, or mask excesses storage");
#endif

enum{
	su_CS_DICT_THRESHOLD_MAX = 8, /*!< \r{su_cs_dict_set_threshold()} maximum. */
	su_CS_DICT_DATA_SPACE_MAX = 0x0FFFu, /*!< \r{su_cs_dict_set_data_space()} maximum. */
	su__CS_DICT_THRESH_MASK = 0x000Fu,
	su__CS_DICT_THRESH_SHIFT = 4u,
	su__CS_DICT_THRESH_DSPC_DEF = 2
};

/*! Opaque. */
struct su_cs_dict{
	struct su__cs_dict_node **csd_array;
	u16 csd_flags;
	u16 csd_thresh_dspc;
	u32 csd_count;
	u32 csd_size;
	u32 csd_min_size;
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
EXPORT struct su__cs_dict_node *su__cs_dict_lookup(struct su_cs_dict const *self, char const *key, void *lookarg_or_nil);
/* *lookarg_or_nil is always updated */
EXPORT s32 su__cs_dict_insrep(struct su_cs_dict *self, char const *key, void *value, up replace_and_view_or_nil);
#if DVLDBGOR(1, 0)
EXPORT void su__cs_dict_stats(struct su_cs_dict const *self);
#endif

/*! Easy iteration support.
 * \a{VIEWNAME} must be a(n) (non-pointer) instance of \r{su_cs_dict_view}. */
#define su_CS_DICT_FOREACH(SELF,VIEWNAME) \
	for(su_cs_dict_view_begin(su_cs_dict_view_setup(VIEWNAME, SELF)); su_cs_dict_view_is_valid(VIEWNAME);\
			su_cs_dict_view_next(VIEWNAME))

/*! Create an instance.
 * \a{flags} is a mix of \r{su_cs_dict_flags}.
 * If \r{su_CS_DICT_OWNS} is specified, \a{tbox_or_nil} is a mandatory, asserted argument. */
EXPORT struct su_cs_dict *su_cs_dict_create(struct su_cs_dict *self, u16 flags, struct su_toolbox const *tbox_or_nil);

/*! Initialization plus \r{su_cs_dict_assign()}. */
EXPORT SHADOW struct su_cs_dict *su_cs_dict_create_copy(struct su_cs_dict *self, struct su_cs_dict const *t);

/*! Destructor. */
EXPORT void su_cs_dict_gut(struct su_cs_dict *self);

/*! Assign \a{t}; return 0 on success, or, also dependend on \r{su_CS_DICT_ERR_PASS} (of \a{t}), a \r{su_err_number}.
 * (Also see \r{su_clone_fun}.)
 * \remarks{The element order of \SELF and \a{t} may not be identical.}
 * \copydoc{su_assign_fun} */
EXPORT s32 su_cs_dict_assign(struct su_cs_dict *self, struct su_cs_dict const *t);

/*! Different to \r{su_cs_dict_assign()} only assigns elements of \a{t},
 * but not \r{su_cs_dict_flags()}, \r{su_cs_dict_threshold()}, \r{su_cs_dict_data_space()},
 * or \r{su_cs_dict_toolbox()}. */
EXPORT s32 su_cs_dict_assign_elems(struct su_cs_dict *self, struct su_cs_dict const *t);

/*! Remove all elements, and release all memory. */
EXPORT struct su_cs_dict *su_cs_dict_clear(struct su_cs_dict *self);

/*! Remove only the elements, keep other allocations. */
EXPORT struct su_cs_dict *su_cs_dict_clear_elems(struct su_cs_dict *self);

/*! Swap the fields of \a{self} and \a{t}. */
EXPORT struct su_cs_dict *su_cs_dict_swap(struct su_cs_dict *self, struct su_cs_dict *t);

/*! Current number of key/value element pairs. */
INLINE u32 su_cs_dict_count(struct su_cs_dict const *self){
	ASSERT(self);
	return self->csd_count;
}

/*! Current size of the node management array.
 * (Might be of interest to re \r{su_cs_dict_balance()} with \r{su_CS_DICT_AUTO_SHRINK} (temporarily) enabled.) */
INLINE u32 su_cs_dict_size(struct su_cs_dict const *self){
	ASSERT(self);
	return self->csd_size;
}

/*! The minimum size of the node management array.
 * See \r{su_cs_dict_set_min_size()}. */
INLINE u32 su_cs_dict_min_size(struct su_cs_dict const *self){
	ASSERT(self);
	return self->csd_min_size;
}

/*! Set the minimum size of the node management array.
 * If set the array will not be (re)sized to a size smaller than this.
 * \remarks{\a{nmsize} cannot be larger than 0xB947 (a large 16-bit prime).} */
INLINE struct su_cs_dict *su_cs_dict_set_min_size(struct su_cs_dict *self, u32 nmsize){
	ASSERT(self);
	self->csd_min_size = MIN(0xB947u, nmsize);
	return self;
}

/*! Get the used \r{su_cs_dict_flags}. */
INLINE u16 su_cs_dict_flags(struct su_cs_dict const *self){
	ASSERT(self);
	return self->csd_flags;
}

/*! Set some \r{su_cs_dict_flags}. */
INLINE struct su_cs_dict *su_cs_dict_add_flags(struct su_cs_dict *self, u16 flags){
	ASSERT(self);
	flags &= su__CS_DICT_CREATE_MASK;
	self->csd_flags |= flags;
	return self;
}

/*! Clear some \r{su_cs_dict_flags}. */
INLINE struct su_cs_dict *su_cs_dict_clear_flags(struct su_cs_dict *self, u16 flags){
	ASSERT(self);
	flags &= su__CS_DICT_CREATE_MASK;
	self->csd_flags &= ~flags;
	return self;
}

/*! Get current threshold.
 * See \r{su_cs_dict_set_threshold()}. */
INLINE u8 su_cs_dict_threshold(struct su_cs_dict const *self){
	ASSERT(self);
	return (self->csd_thresh_dspc & su__CS_DICT_THRESH_MASK);
}

/*! Set threshold used to decide when internal management array is to be grown.
 * The algorithm is \cb{element-count >= array-capacity << threshold}.
 * \a{nthresh} is \r{su_CLIP()}ped in between 1 and \r{su_CS_DICT_THRESHOLD_MAX}; default is 2.
 * Does not affect shrinking (see \r{su_CS_DICT_AUTO_SHRINK}). */
INLINE struct su_cs_dict *su_cs_dict_set_threshold(struct su_cs_dict *self, u8 nthresh){
	ASSERT(self);
	self->csd_thresh_dspc &= ~su__CS_DICT_THRESH_MASK;
	self->csd_thresh_dspc |= CLIP(nthresh, 1u, S(u8,su_CS_DICT_THRESHOLD_MAX));
	return self;
}

/*! Get in-use data space.
 * See \r{su_cs_dict_set_data_space()}. */
INLINE uz su_cs_dict_data_space(struct su_cs_dict const *self){
	ASSERT(self);
	return ((self->csd_thresh_dspc /*& ~su__CS_DICT_THRESH_MASK*/) >> su__CS_DICT_THRESH_SHIFT);
}

/*! Set data space in bytes.
 * Non-\r{su_CS_DICT_OWNS} instances can make up to \r{su_CS_DICT_DATA_SPACE_MAX} bytes available for data values:
 * if \a{ndspc} is not 0 nodes will henceforth reserve \a{ndspc} additional bytes;
 * when setting a \NIL value that is always used, but otherwise the value (returned by \r{su_cs_dict_lookup()} or
 * \r{su_cs_dict_view_data()}) points to that data space;
 * unless \r{su_CS_DICT_DATA_SPACE_RAW} flag is set \a{ndspc} bytes of set non-\NIL data are also copied over.
 * The data space storage will be \r{su_ALIGN_Z()} aligned.
 * Data space changes may only happen on an empty (\r{su_cs_dict_count()} is 0) \SELF. */
INLINE struct su_cs_dict *su_cs_dict_set_data_space(struct su_cs_dict *self, uz ndspc){
	ASSERT(self);
	ASSERT_RET(!(su_cs_dict_flags(self) & su_CS_DICT_OWNS), self);
	ASSERT_RET(su_cs_dict_count(self) == 0, self);
	ASSERT_RET(ndspc <= su_CS_DICT_DATA_SPACE_MAX, self);
	self->csd_thresh_dspc &= su__CS_DICT_THRESH_MASK;
	self->csd_thresh_dspc |= S(u16,ndspc << su__CS_DICT_THRESH_SHIFT);
	return self;
}

/*! Get the used \r{su_toolbox}, or \NIL. */
INLINE struct su_toolbox const *su_cs_dict_toolbox(struct su_cs_dict const *self){
	ASSERT(self);
	return self->csd_tbox;
}

/*! Set the (possibly) used \r{su_toolbox}.
 * The toolbox is asserted if \r{su_CS_DICT_OWNS} is set. */
INLINE struct su_cs_dict *su_cs_dict_set_toolbox(struct su_cs_dict *self, struct su_toolbox const *tbox_or_nil){
	ASSERT(self);
	ASSERT(!(su_cs_dict_flags(self) & su_CS_DICT_OWNS) || (tbox_or_nil != NIL && tbox_or_nil->tb_clone != NIL &&
		 tbox_or_nil->tb_del != NIL && tbox_or_nil->tb_assign != NIL));
	self->csd_tbox = tbox_or_nil;
	return self;
}

/*! Resize management table to accommodate for \a{xcount} elements.
 * Calculates new size as via \r{su_cs_dict_set_threshold()}.
 * The \r{su_CS_DICT_FROZEN} state is ignored, but can avoid an automatic resize upon the next insertion
 * (or removal with \r{su_CS_DICT_AUTO_SHRINK}).
 * Returns -1 if no action was performed, \ERR{NONE} upon successful resize,
 * or (also dependent on \r{su_CS_DICT_ERR_PASS}) a \r{su_err_number}.
 * In error cases no action has been performed. */
EXPORT s32 su_cs_dict_resize(struct su_cs_dict *self, u32 xcount);

/*! Thaw and balance \a{self}.
 * Thaw \SELF from (a possible) \r{su_CS_DICT_FROZEN} state that may have been set via \r{su_cs_dict_add_flags()},
 * and call \r{su_cs_dict_resize()}.
 * \remarks{If memory failures occur the balancing is simply not performed.} */
EXPORT struct su_cs_dict *su_cs_dict_balance(struct su_cs_dict *self);

/*! Test whether \a{key} is present in \SELF. */
INLINE boole su_cs_dict_has_key(struct su_cs_dict const *self, char const *key){
	ASSERT(self);
	ASSERT_RET(key != NIL, FAL0);
	return (su__cs_dict_lookup(self, key, NIL) != NIL);
}

/*! Lookup a value, return it (possibly \NIL) or \NIL. */
INLINE void *su_cs_dict_lookup(struct su_cs_dict *self, char const *key){
	struct su__cs_dict_node *csdnp;
	ASSERT(self != NIL);
	ASSERT_RET(key != NIL, NIL);
	csdnp = su__cs_dict_lookup(self, key, NIL);
	return (csdnp != NIL) ? csdnp->csdn_data : NIL;
}

/*! Insert a new \a{key} mapping to \a{value}.
 * Returns \ERR{NONE} upon successful insertion, -1 if \a{key} already exists (\r{su_cs_dict_replace()} also updates
 * \a{value}), or (also dependent on \r{su_CS_DICT_ERR_PASS}) a \r{su_err_number}.
 * If \a{value} is \NIL (after cloning) and \r{su_CS_DICT_OWNS} is set and \r{su_CS_DICT_NIL_IS_VALID_OBJECT} is not,
 * \ERR{INVAL} is returned. */
INLINE s32 su_cs_dict_insert(struct su_cs_dict *self, char const *key, void *value){
	ASSERT(self);
	ASSERT_RET(key != NIL, su_ERR_NONE);
	return su__cs_dict_insrep(self, key, value, FAL0);
}

/*! Insert a new, or update an existing \a{key} mapping to \a{value}.
 * Returns \ERR{NONE} upon successful insertion, -1 if \a{key} already existed and \a{value} was updated,
 * or (also dependent on \r{su_CS_DICT_ERR_PASS}) a \r{su_err_number}.
 * If \a{value} is \NIL (after cloning) and \r{su_CS_DICT_OWNS} is set and \r{su_CS_DICT_NIL_IS_VALID_OBJECT} is not,
 * \ERR{INVAL} is returned.
 *
 * \remarks{When \SELF owns its values and \r{su_CS_DICT_NILISVALO} is set,
 * then if updating a non-\NIL value via the \r{su_assign_fun} of the used \r{su_toolbox} fails,
 * the old object will be deleted, \NIL will be inserted, and this function fails.}
 *
 * \remarks{Likewise, if \r{su_CS_DICT_NILISVALO} is not set, then if the \r{su_clone_fun} of the toolbox fails
 * to create a duplicate of \a{value}, then the old value will remain unchanged and this function fails.} */
INLINE s32 su_cs_dict_replace(struct su_cs_dict *self, char const *key, void *value){
	ASSERT(self);
	ASSERT_RET(key != NIL, su_ERR_NONE);
	return su__cs_dict_insrep(self, key, value, TRU1);
}

/*! Returns the false boolean if \a{key} cannot be found. */
EXPORT boole su_cs_dict_remove(struct su_cs_dict *self, char const *key);

/*! Via \r{su_DVLDBG()} it will \r{su_log_write()} statistics about \SELF.
 * Unless too large a visualization is shown: \c{0} denotes an empty slot, \c{.} one with less than 50% of the average
 * number of elements per slot, \c{_} less than average, \c{~} the average, \c{=} more than average, but less than
 * \c{/}, used for half the distance in between average and maximum; \c{^} denotes the maximum number of elements. */
INLINE void su_cs_dict_statistics(struct su_cs_dict const *self){
	UNUSED(self);
#if DVLDBGOR(1, 0)
	su__cs_dict_stats(self);
#endif
}
/* }}} */

/* view {{{ */
/*!
 * \defgroup CS_DICT_VIEW View of and for su_cs_dict
 * \ingroup CS_DICT
 * \brief View of and for \r{su_cs_dict} (\r{su/cs-dict.h})
 *
 * This implements an associative unidirectional view type.
 * Whereas it documents C++ interfaces, \r{su/view.h} also applies to C views.
 *@{
 */

enum su__cs_dict_view_move_types{
	su__CS_DICT_VIEW_MOVE_BEGIN,
	su__CS_DICT_VIEW_MOVE_HAS_NEXT,
	su__CS_DICT_VIEW_MOVE_NEXT
};

/*! \_ */
struct su_cs_dict_view{
	/* *_next_* only valid after _move(..HAS_NEXT) */
	struct su_cs_dict *csdv_parent; /*!< We are \fn{is_setup()} with it. */
	struct su__cs_dict_node *csdv_node;
	u32 csdv_index;
	u32 csdv_next_index;
	struct su__cs_dict_node *csdv_next_node;
};

/* "The const is preserved logically" */
EXPORT struct su_cs_dict_view *su__cs_dict_view_move(struct su_cs_dict_view *self, /*_view_move_types*/ u8 type);

/*! Easy iteration support; \a{SELF} must be \r{su_cs_dict_view_setup()}. */
#define su_CS_DICT_VIEW_FOREACH(SELF) \
	for(su_cs_dict_view_begin(SELF); su_cs_dict_view_is_valid(SELF); su_cs_dict_view_next(SELF))

/*! Create a tie in between \SELF and its parent collection object. */
INLINE struct su_cs_dict_view *su_cs_dict_view_setup(struct su_cs_dict_view *self, struct su_cs_dict *parent){
	ASSERT(self);
	self->csdv_parent = parent;
	self->csdv_node = NIL;
	ASSERT_RET(parent != NIL, self);
	return self;
}

/*! \r{su_cs_dict_view_setup()} must have been called before. */
INLINE struct su_cs_dict *su_cs_dict_view_parent(struct su_cs_dict_view const *self){
	ASSERT(self);
	return self->csdv_parent;
}

/*! \_ */
INLINE boole su_cs_dict_view_is_valid(struct su_cs_dict_view const *self){
	ASSERT(self);
	return (self->csdv_node != NIL);
}

/*! \_ */
INLINE struct su_cs_dict_view *su_cs_dict_view_reset(struct su_cs_dict_view *self){
	ASSERT(self);
	self->csdv_node = NIL;
	return self;
}

/*! \_ */
INLINE char const *su_cs_dict_view_key(struct su_cs_dict_view const *self){
	ASSERT(self);
	ASSERT_RET(su_cs_dict_view_is_valid(self), NIL);
	return self->csdv_node->csdn_key;
}

/*! \_ */
INLINE u32 su_cs_dict_view_key_len(struct su_cs_dict_view const *self){
	ASSERT(self);
	ASSERT_RET(su_cs_dict_view_is_valid(self), 0);
	return self->csdv_node->csdn_klen;
}

/*! \_ */
INLINE uz su_cs_dict_view_key_hash(struct su_cs_dict_view const *self){
	ASSERT(self);
	ASSERT_RET(su_cs_dict_view_is_valid(self), 0);
	return self->csdv_node->csdn_khash;
}

/*! \_ */
INLINE void *su_cs_dict_view_data(struct su_cs_dict_view const *self){
	ASSERT(self);
	ASSERT_RET(su_cs_dict_view_is_valid(self), NIL);
	return self->csdv_node->csdn_data;
}

/*! Replace the data of a \r{su_cs_dict_view_is_valid()} view.
 * If the parent \r{su_CS_DICT_OWNS} its values this behaves like \r{su_cs_dict_replace()},
 * otherwise it returns \ERR{NONE}. */
EXPORT s32 su_cs_dict_view_set_data(struct su_cs_dict_view *self, void *value);

/*! Move a setup view to the first position, if there is one.
 * \r{su_cs_dict_view_is_valid()} must be tested thereafter. */
INLINE struct su_cs_dict_view *su_cs_dict_view_begin(struct su_cs_dict_view *self){
	ASSERT(self);
	return su__cs_dict_view_move(self, su__CS_DICT_VIEW_MOVE_BEGIN);
}

/*! Whether another position follows a \r{su_cs_dict_view_is_valid()} one.
 * A following \r{su_cs_dict_view_next()} will use informations collected by this function. */
INLINE boole su_cs_dict_view_has_next(struct su_cs_dict_view const *self){
	ASSERT(self);
	ASSERT_RET(su_cs_dict_view_is_valid(self), FAL0);
	return (su__cs_dict_view_move(C(struct su_cs_dict_view*,self),
		su__CS_DICT_VIEW_MOVE_HAS_NEXT)->csdv_next_node != NIL);
}

/*! Step forward a \r{su_cs_dict_view_is_valid()} view. */
INLINE struct su_cs_dict_view *su_cs_dict_view_next(struct su_cs_dict_view *self){
	ASSERT(self);
	ASSERT_RET(su_cs_dict_view_is_valid(self), self);
	return su__cs_dict_view_move(self, su__CS_DICT_VIEW_MOVE_NEXT);
}

/*! Search for \a{key} and return the new \r{su_cs_dict_view_is_valid()}. */
EXPORT boole su_cs_dict_view_find(struct su_cs_dict_view *self, char const *key);

/*! See \r{su_cs_dict_insert()}.
 * Upon success \ERR{NONE} is returned and \r{su_cs_dict_view_is_valid()} will be true.
 * It is also true if -1 is returned because an existing \a{key} has not been updated. */
INLINE s32 su_cs_dict_view_reset_insert(struct su_cs_dict_view *self, char const *key, void *value){
	ASSERT(self);
	ASSERT_RET(key != NIL, su_ERR_NONE);
	return su__cs_dict_insrep(self->csdv_parent, key, value, FAL0 | R(up,self));
}

/*! See \r{su_cs_dict_replace()}.
 * Upon success \ERR{NONE} or -1 is returned and \r{su_cs_dict_view_is_valid()}. */
INLINE s32 su_cs_dict_view_reset_replace(struct su_cs_dict_view *self, char const *key, void *value){
	ASSERT(self);
	ASSERT_RET(key != NIL, su_ERR_NONE);
	return su__cs_dict_insrep(self->csdv_parent, key, value, TRU1 | R(up,self));
}

/*! Remove the key/value tuple of a \r{su_cs_dict_view_is_valid()} view, then move to the next position, if any. */
EXPORT struct su_cs_dict_view *su_cs_dict_view_remove(struct su_cs_dict_view *self);

/*! Test two views for equality. */
INLINE sz su_cs_dict_view_cmp(struct su_cs_dict_view const *self, struct su_cs_dict_view const *t){
	ASSERT_RET(self, -(t != NIL));
	ASSERT_RET(t, 1);
	return (self->csdv_node == t->csdv_node);
}
/*! @} *//* }}} */

/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# include <su/view.h>

# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

template<class T,boole OWNS> class cs_dict;

/* cs_dict {{{ */
/*!
 * \ingroup CS_DICT
 * C++ variant of \r{CS_DICT} (\r{su/cs-dict.h})
 */
template<class T, boole OWNS=type_traits<T>::ownguess>
class cs_dict : private su_cs_dict{
	class gview;

	class gview : private su_cs_dict_view{
	public:
		// xxx clang 5.0.1 BUG: needed this-> to find superclass field
		gview(void) {this->csdv_parent = NIL; this->csdv_node = NIL;}
		gview(gview const &t) {*this = t;}
		~gview(void) {}
		gview &operator=(gview const &t) {SELFTHIS_RET(*S(su_cs_dict_view*,this) = *S(su_cs_dict_view const*,&t));}
		gview &setup(su_cs_dict &parent) {SELFTHIS_RET(su_cs_dict_view_setup(this, &parent));}
		boole is_setup(void) const {return su_cs_dict_view_parent(this) != NIL;}
		boole is_same_parent(gview const &t) const {return su_cs_dict_view_parent(this) == su_cs_dict_view_parent(&t);}
		boole is_valid(void) const {return su_cs_dict_view_is_valid(this);}
		gview &reset(void) {SELFTHIS_RET(su_cs_dict_view_reset(this));}
		char const *key(void) const {return su_cs_dict_view_key(this);}
		void *data(void) {return su_cs_dict_view_data(this);}
		void const *data(void) const {return su_cs_dict_view_data(this);}
		s32 set_data(void *value) {return su_cs_dict_view_set_data(this, value);}
		gview &begin(void) {SELFTHIS_RET(su_cs_dict_view_begin(this));}
		boole has_next(void) const {return su_cs_dict_view_has_next(this);}
		gview &next(void) {SELFTHIS_RET(su_cs_dict_view_next(this));}
		boole find(void const *key) {return su_cs_dict_view_find(this, S(char const*,key));}
		s32 reset_insert(void const *key, void *value){
			return su_cs_dict_view_reset_insert(this, S(char const*,key), value);
		}
		s32 reset_replace(void const *key, void *value){
			return su_cs_dict_view_reset_replace(this, S(char const*,key), value);
		}
		gview &remove(void) {SELFTHIS_RET(su_cs_dict_view_remove(this));}
		sz cmp(gview const &t) const {return su_cs_dict_view_cmp(this, &t);}
	};

public:
	/*! \copydoc{su_cs_dict_flags} */
	enum flags{ // owns is template arg
		f_none, /*!< This is 0. */
		f_prime_spaced = su_CS_DICT_PRIME_SPACED, /*!< \copydoc{su_CS_DICT_PRIME_SPACED} */
		f_auto_shrink = su_CS_DICT_AUTO_SHRINK, /*!< \copydoc{su_CS_DICT_AUTO_SHRINK} */
		f_frozen = su_CS_DICT_FROZEN, /*!< \copydoc{su_CS_DICT_FROZEN} */
		f_head_resort = su_CS_DICT_HEAD_RESORT, /*!< \copydoc{su_CS_DICT_HEAD_RESORT} */
		f_strong = su_CS_DICT_STRONG, /*!< \copydoc{su_CS_DICT_STRONG} */
		f_case = su_CS_DICT_CASE, /*!< \copydoc{su_CS_DICT_CASE} */
		f_data_space_raw = su_CS_DICT_DATA_SPACE_RAW, /*!< \copydoc{su_CS_DICT_DATA_SPACE_RAW} */
		f_err_pass = su_CS_DICT_ERR_PASS, /*!< \copydoc{su_CS_DICT_ERR_PASS} */
		f_nil_is_valid_object = su_CS_DICT_NIL_IS_VALID_OBJECT, /*!< \copydoc{su_CS_DICT_NIL_IS_VALID_OBJECT} */
		f_nilisvalo = su_CS_DICT_NILISVALO /*!< Alias for \r{f_nil_is_valid_object}. */
	};

	/*! \_ */
	typedef NSPC(su)type_traits<T> type_traits;

	/*! \_ */
	typedef typename type_traits::type_toolbox type_toolbox;

	/*! \_ */
	typedef typename type_traits::auto_type_toolbox auto_type_toolbox;

	/*! \_ */
	typedef typename type_traits::tp tp;

	/*! \_ */
	typedef typename type_traits::tp_const tp_const;

	/*! \_ */
	typedef NSPC(su)view_traits<su_cs_dict,char const *,T> view_traits;

	friend class NSPC(su)view_assoc_unidir<view_traits, gview>;
	friend class NSPC(su)view_assoc_unidir_const<view_traits, gview>;

	/*! \r{CS_DICT_VIEW} (\r{su/cs-dict.h}) */
	typedef NSPC(su)view_assoc_unidir<view_traits, gview> view;

	/*! \r{CS_DICT_VIEW} (\r{su/cs-dict.h}) */
	typedef NSPC(su)view_assoc_unidir_const<view_traits, gview> view_const;

	/*! \copydoc{su_cs_dict_create()} */
	cs_dict(type_toolbox const *ttbox=NIL, u16 flags=f_none){
		ASSERT(!OWNS || (ttbox != NIL && ttbox->ttb_clone != NIL && ttbox->ttb_del != NIL && ttbox->ttb_assign != NIL));
		flags &= su__CS_DICT_CREATE_MASK & ~su_CS_DICT_OWNS;
		if(OWNS)
			flags |= su_CS_DICT_OWNS;
		su_cs_dict_create(this, flags, R(su_toolbox const*,ttbox));
	}

	/*! \copydoc{su_cs_dict_create_copy()} */
	SHADOW cs_dict(cs_dict const &t) {su_cs_dict_create_copy(this, &t);}

	/*! \copydoc{su_cs_dict_gut()} */
	~cs_dict(void) {su_cs_dict_gut(this);}

	/*! \copydoc{su_cs_dict_assign()} */
	s32 assign(cs_dict const &t) {return su_cs_dict_assign(this, &t);}

	/*! \r{assign()} */
	SHADOW cs_dict &operator=(cs_dict const &t) {SELFTHIS_RET(assign(t));}

	/*! \copydoc{su_cs_dict_assign_elems()} */
	s32 assign_elems(cs_dict const &t) {return su_cs_dict_assign_elems(this, &t);}

	/*! \copydoc{su_cs_dict_clear()} */
	cs_dict &clear(void) {SELFTHIS_RET(su_cs_dict_clear(this));}

	/*! \copydoc{su_cs_dict_clear_elems()} */
	cs_dict &clear_elems(void) {SELFTHIS_RET(su_cs_dict_clear_elems(this));}

	/*! \copydoc{su_cs_dict_swap()} */
	cs_dict &swap(cs_dict &t) {SELFTHIS_RET(su_cs_dict_swap(this, &t));}

	/*! \copydoc{su_cs_dict_count()} */
	u32 count(void) const {return csd_count;}

	/*! Whether \r{count()} is 0. */
	boole is_empty(void) const {return (count() == 0);}

	/*! \copydoc{su_cs_dict_size()} */
	u32 size(void) const {return csd_size;}

	/*! \copydoc{su_cs_dict_min_size()} */
	u32 min_size(void) const {return csd_min_size;}

	/*! \copydoc{su_cs_dict_set_min_size()} */
	cs_dict &set_min_size(u32 nmsize) {SELFTHIS_RET(su_cs_dict_set_min_size(this, nmsize));}

	/*! \copydoc{su_cs_dict_flags()}
	 * \remarks{Since \c{OWNS} is a template argument that flag is stripped.} */
	u16 flags(void) const {return (su_cs_dict_flags(this) & ~su_CS_DICT_OWNS);}

	/*! \copydoc{su_cs_dict_add_flags()} */
	cs_dict &add_flags(u16 flags) {SELFTHIS_RET(su_cs_dict_add_flags(this, flags & ~su_CS_DICT_OWNS));}

	/*! \copydoc{su_cs_dict_clear_flags()} */
	cs_dict &clear_flags(u16 flags) {SELFTHIS_RET(su_cs_dict_clear_flags(this, flags & ~su_CS_DICT_OWNS));}

	/*! \copydoc{su_cs_dict_threshold()} */
	u8 threshold(void) const {return su_cs_dict_threshold(this);}

	/*! \copydoc{su_cs_dict_set_threshold()} */
	cs_dict &set_threshold(u8 thresh) {SELFTHIS_RET(su_cs_dict_set_threshold(this, thresh));}

	/*! \copydoc{su_cs_dict_data_space()} */
	uz data_space(void) const {return su_cs_dict_data_space(this);}

	/*! \copydoc{su_cs_dict_set_data_space()} */
	cs_dict &set_data_space(uz dspc){
		ASSERT_RET(!OWNS, *this);
		ASSERT_RET(is_empty(), *this);
		SELFTHIS_RET(su_cs_dict_set_data_space(this, dspc));
	}

	/*! \copydoc{su_cs_dict_toolbox()} */
	type_toolbox const *toolbox(void) const {return R(type_toolbox const*,su_cs_dict_toolbox(this));}

	/*! \copydoc{su_cs_dict_set_toolbox()} */
	cs_dict &set_toolbox(type_toolbox const *tbox_or_nil){
		SELFTHIS_RET(su_cs_dict_set_toolbox(this, R(su_toolbox const*,tbox_or_nil)));
	}

	/*! \copydoc{su_cs_dict_resize()} */
	s32 resize(u32 xcount) {return su_cs_dict_resize(this, xcount);}

	/*! \copydoc{su_cs_dict_balance()} */
	cs_dict &balance(void) {SELFTHIS_RET(su_cs_dict_balance(this));}

	/*! \copydoc{su_cs_dict_has_key()} */
	boole has_key(char const *key) const{
		ASSERT_RET(key != NIL, FAL0);
		return su_cs_dict_has_key(this, key);
	}

	/*! \copydoc{su_cs_dict_lookup()} */
	tp lookup(char const *key){
		ASSERT_RET(key != NIL, NIL);
		return type_traits::to_tp(su_cs_dict_lookup(this, key));
	}

	/*! \r{lookup()} */
	tp operator[](char const *key) {return lookup(key);}

	/*! \r{lookup()} */
	tp_const lookup(char const *key) const{
		ASSERT_RET(key != NIL, NIL);
		return type_traits::to_const_tp(su_cs_dict_lookup(C(su_cs_dict*,this), key));
	}

	/*! \r{lookup()} */
	tp_const operator[](char const *key) const {return lookup(key);}

	/*! \copydoc{su_cs_dict_insert()} */
	s32 insert(char const *key, tp_const value){
		ASSERT_RET(key != NIL, 0);
		return su_cs_dict_insert(this, key, type_traits::to_vp(value));
	}

	/*! \copydoc{su_cs_dict_replace()} */
	s32 replace(char const *key, tp_const value){
		ASSERT_RET(key != NIL, 0);
		return su_cs_dict_replace(this, key, type_traits::to_vp(value));
	}

	/*! \copydoc{su_cs_dict_remove()} */
	boole remove(char const *key){
		ASSERT_RET(key != NIL, FAL0);
		return su_cs_dict_remove(this, key);
	}

	/*! \copydoc{su_cs_dict_statistics()} */
	void statistics(void) const {su_cs_dict_statistics(this);}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_CS_DICT_H */
/* s-itt-mode */
