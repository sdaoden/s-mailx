/*@ Generic C++ View (-of-a-collection, for iterating plus purposes) templates.
 *@ (Merely of interest when creating a new C++ collection type.)
 *
 * Copyright (c) 2001 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_VIEW_H
# define su_VIEW_H

#ifdef CXX_DOXYGEN
/*!
 * \file
 * \ingroup VIEW
 * \brief Generic view template(s) (super classes)
 *
 * Merely of interest when creating new C++ collection types.
 */
#endif

#include <su/code.h>

su_USECASE_MX_DISABLED
#if !su_C_LANG || defined CXX_DOXYGEN

#define su_CXX_HEADER
#include <su/code-in.h>
NSPC_BEGIN(su)

template<class BASECOLLT, class KEYT, class T> class view_traits;
template<class VIEWTRAITS, class GBASEVIEWT> class view__base;
template<class VIEWTRAITS, class GBASEVIEWT> class view_unidir;
template<class VIEWTRAITS, class GBASEVIEWT> class view_unidir_const;
template<class VIEWTRAITS, class GBASEVIEWT> class view_bidir;
template<class VIEWTRAITS, class GBASEVIEWT> class view_bidir_const;
template<class VIEWTRAITS, class GBASEVIEWT> class view_random;
template<class VIEWTRAITS, class GBASEVIEWT> class view_random_const;
template<class VIEWTRAITS, class GBASEVIEWT> class view_assoc_unidir;
template<class VIEWTRAITS, class GBASEVIEWT> class view_assoc_unidir_const;
template<class VIEWTRAITS, class GBASEVIEWT> class view_assoc_bidir;
template<class VIEWTRAITS, class GBASEVIEWT> class view_assoc_bidir_const;

/* doc {{{ */
/*!
 * \defgroup VIEW C++ View superclasses
 * \ingroup COLL
 * \brief Generic view template(s) (super classes) (\r{su/view.h})
 *
 * Merely of interest when creating new C++ collection types.
 *
 * \head1{Superclass requirements}
 *
 * The minimum required interface that the typesafe C++ templates require,
 * and its expected behaviour.
 * In general there is a basic interface and several type-specific additions
 * which stack upon each other.
 *
 * \head2{Base}
 *
 * \list{\li{
 * Copy constructor
 * }\li{
 * Assignment operator (C: \fn{_assign()})
 * }\li{
 * \fn{self_class &setup(super_collection &tc)}
 * Create a tie in between this view and its parental collection instance.
 * This is superficial: the programmer is responsible to ensure that the parent
 * remains accessible, remains unmodified, etc.
 * }\li{
 * \fn{boole is_setup(void) const}
 * }\li{
 * \fn{boole is_same_parent(self_class const &t) const}:
 * Whether two views are tied to the same collection object.
 * }\li{
 * \fn{boole is_valid(void) const}:
 * A valid view points to an accessible slot of the collection instance.
 * }\li{
 * \fn{self_class &reset(void)}
 * Invalidate the position, but keep the parent collection tied.
 * }\li{
 * \fn{void const *data(void) const}:
 * \NIL is returned if \fn{is_valid()} assertion triggers.
 * Also avaiable via \fn{operator*(void) const}
 * and \fn{operator->(void) const}.
 * }\li{
 * \fn{self_class &begin(void)}:
 * Move the \fn{is_setup()} view to the first iteratable position, if any.
 * }\li{
 * \fn{boole has_next(void) const}, \fn{self_class &next(void)}:
 * Step the \c{is_valid()} view to the next position, if any.
 * }\li{
 * \fn{sz cmp(self_class const &t) const}:
 * This need not compare all possible cases, only those which make sense;
 * Equality and inequality must always be provided.
 * Neither \fn{is_setup()} nor \fn{is_valid()} are preconditions for
 * comparison; a non-\fn{setup()}d view shall compare less-than an
 * \fn{is_setup()} one, ditto \fn{is_valid()}.
 * }}
 *
 * \head2{Additions for views with non-constant parent}
 *
 * \list{\li{
 * \fn{void *data(void)}:
 * \NIL is returned if \fn{is_valid()} assertion triggers.
 * Also avaiable via \fn{operator*(void)} and \fn{operator->(void)}.
 * }\li{
 * \fn{s32 set_data(void *dat)}:
 * Replace the value of an \fn{is_valid()} position.
 * Returns \err{none} upon success, and \err{einval} if the \fn{is_setup()}
 * assertion fails.
 * }\li{
 * \fn{self_class &remove(void)}:
 * Remove the current \c{is_valid()} entry, and move to the next position,
 * if there is any.
 * }}
 *
 * \head2{Additions for non-associative views, non-constant parent}
 *
 * \list{\li{
 * \fn{s32 insert(void *dat)}:
 * Insert new element after current position if that \fn{is_valid()},
 * otherwise creates a new \fn{begin()}.
 * Returns \err{none} and is positioned at the inserted element upon
 * success, and \err{einval} if the \fn{is_setup()} assertion fails.
 * }\li{
 * \fn{s32 insert_range(self_class &startpos, self_class &endpos)}:
 * Insert new element(s) after current position if that \fn{is_valid()},
 * otherwise creates a new \fn{begin()} first.
 * \a{startpos} must be \fn{is_valid()} and must not be \fn{is_same_parent()}
 * as \THIS; if it is, it is asserted that ranges do not overlap.
 *
 * All positions accessible by iterating \a{startpos} will be inserted.
 * If \a{endpos} is \fn{is_valid()} \c{startpos.is_same_parent(endpos)} must
 * assert and the iteration does not include \a{endpos}.
 * Returns \err{none} and is positioned at the last inserted element
 * upon success, and \err{einval} if the \fn{is_setup()} or any of the
 * argument assertions fail, among others.
 * }\li{
 * \fn{self_class &remove_range(self_class &endpos)}:
 * Remove all elements starting at the current \fn{is_valid()} entry unless
 * \THIS becomes invalid, or a given \fn{is_valid()} \a{endpos}ition is
 * reached, which is not removed.
 *
 * If \a{endpos} \fn{is_valid()} then it must be \fn{is_same_parent()}
 * as this view; it may also become updated in this case in order to
 * stay at the same effective position after the operation completed:
 * for example, removing a range from an array requires \a{endpos} to be
 * moved further to "the front".
 * }}
 *
 * \head2{Additions for associative views}
 *
 * \list{\li{
 * \fn{void const *key(void) const}:
 * \NIL is returned if the \fn{is_valid()} assertion triggers.
 * }}
 *
 * \head2{Additions for associative views, non-constant parent}
 *
 * \list{\li{
 * \fn{s32 reset_insert(void const *key, void *dat)}:
 * Insert a new \a{key} / \a{value} pair in the parent.
 * If \a{key} already exists -1 is returned, but \fn{is_valid()} is true just
 * as for a successful insertion.
 * }\li{
 * \fn{s32 reset_replace(void const *key, void *dat)}:
 * Insert a new, or update an existing \a{key} / \a{value} pair in the parent.
 * If an existing \a{key} has been updated -1 is returned, but \fn{is_valid()}
 * is true just as for insertion of a new \a{key}.
 * }}
 *
 * \head2{Additions for non-associative unidirectional views}
 *
 * \list{\li{
 * \fn{self_class &go_to(uz off)}:
 * Move the \fn{is_setup()} view to the absolute position \a{off}.
 * }\li{
 * \fn{boole find(void const *dat, boole byptr)}:
 * Search for \a{dat} in the \fn{is_setup()} view, either starting at the
 * current position if \fn{is_valid()}, at \fn{begin()} otherwise.
 * \a{byptr} states whether data shall be found by simple pointer comparison;
 * if not, the \c{toolbox} of the \fn{is_setup()}d parent should be
 * asserted (if used by the collection in question).
 * }}
 *
 * \head2{Additions for associative unidirectional views}
 *
 * \list{\li{
 * \fn{boole find(void const *key)}:
 * Search for \a{key} in the \fn{is_setup()} view.
 * }}
 *
 * \head2{Additions for bidirectional views}
 *
 * \list{\li{
 * \fn{self_class &end(void)}:
 * Move the \fn{is_setup()} view to the last position, if any.
 * }\li{
 * \fn{boole has_last(void) const}
 * }\li{
 * \fn{self_class &last(void)}:
 * Move the \fn{is_valid()} view to the position before the current one,
 * if any.
 * }}
 *
 * \head2{Additions for non-associative bidirectional views}
 *
 * \list{\li{
 * \fn{boole rfind(void const *dat, boole byptr)}:
 * Search for \a{dat} in the \fn{is_setup()} view, either starting at the
 * current position if \fn{is_valid()}, at \fn{end()} otherwise.
 * \a{byptr} states whether data shall be found by simple pointer comparison;
 * if not, the \c{toolbox} of the \fn{setup()}d parent should be asserted.
 * }}
 *
 * \head2{Additions for non-associative random-access views}
 *
 * This type extends the requirement of the \fn{cmp()} function in the
 * base set, and requires the additional results less-than,
 * less-than-or-equal, greater-than and greater-than-or-equal.
 *
 * \list{\li{
 * \fn{self_class &go_around(sz reloff)}:
 * Move the \fn{is_valid()} view relative by \a{reloff} positions.
 * }}
 * @{
 */
/* }}} */

/*! \_ */
enum view_category{
   view_category_non_assoc, /*!< \_ */
   view_category_assoc /*!< \_ */
};

/*! \_ */
enum view_type{
   view_type_unidir, /*!< \_ */
   view_type_bidir, /*!< \_ */
   view_type_random, /*!< \_ */
   view_type_assoc_unidir, /*!< \_ */
   view_type_assoc_bidir /*!< \_ */
};

/*! \_ */
template<class BASECOLLT, class KEYT, class T>
class view_traits{
public:
   typedef BASECOLLT base_coll_type; /*!< \_ */

   // Identical to normal traits except for view_category_assoc views
   typedef NSPC(su)type_traits<KEYT> key_type_traits; /*!< \_ */
   typedef NSPC(su)type_traits<T> type_traits; /*!< \_ */
};

/* @} */

// class view__base{{{
template<class VIEWTRAITS, class GBASEVIEWT>
class view__base{
protected:
   GBASEVIEWT m_view;

   view__base(void) : m_view() {}
   view__base(view__base const &t) : m_view(t.m_view) {}

public:
   ~view__base(void) {}

protected:
   view__base &assign(view__base const &t){
      m_view = t.m_view;
      return *this;
   }
   view__base &operator=(view__base const &t) {return assign(t);}

public:
   boole is_setup(void) const {return m_view.is_setup();}

   boole is_same_parent(view__base const &t) const{
      return m_view.is_same_parent(t.m_view);
   }

   boole is_valid(void) const {return m_view.is_valid();}
   operator boole(void) const {return is_valid();}

protected:
   view__base &reset(void){
      (void)m_view.reset();
      return *this;
   }

   sz cmp(view__base const &t) const {return m_view.cmp(t.m_view);}

public:
   boole is_equal(view__base const &t) const {return (cmp(t) == 0);}
   boole operator==(view__base const &t) const {return (cmp(t) == 0);}
   boole operator!=(view__base const &t) const {return (cmp(t) != 0);}
};
// }}}

// Because of the various sorts of views we define helper macros.
// Unfortunately GCC (up to and including 3.4.2) cannot access
// base& _x   ...   _x.m_view
// ("is protected within this context"), but can only access
// base& _x   ...   MYSELF& y = _x   ...   y.m_view
// (and only so if we put a "using" directive, see su__VIEW_IMPL_START__BASE).
// To be (hopefully..) absolutely safe use a C-style cast
#define su__VIEW_DOWNCAST(X) ((su__VIEW_NAME&)X)

#define su__VIEW_IMPL_START /*{{{*/\
template<class VIEWTRAITS, class GBASEVIEWT>\
class su__VIEW_NAME : public view__base<VIEWTRAITS,GBASEVIEWT>{\
   typedef su__VIEW_NAME<VIEWTRAITS,GBASEVIEWT> myself;\
   typedef view__base<VIEWTRAITS,GBASEVIEWT> base;\
   \
   /* XXX All these typedefs could be moved to class view__base!? */\
   typedef typename VIEWTRAITS::base_coll_type base_coll_type;\
   typedef typename VIEWTRAITS::key_type_traits key_type_traits;\
   typedef typename VIEWTRAITS::type_traits type_traits;\
   \
   /* (Simply add _key_ - for non-associative this is eq tp_const) */\
   typedef typename key_type_traits::tp_const key_tp_const;\
   typedef typename type_traits::type type;\
   typedef typename type_traits::tp tp;\
   typedef typename type_traits::tp_const tp_const;\
   \
protected:\
   /* (GCC (up to and incl. 3.4.2) does not find it otherwise) */\
   using base::m_view;\
   \
public:\
   static NSPC(su)view_category const view_category = su__VIEW_CATEGORY;\
   static NSPC(su)view_type const view_type = su__VIEW_TYPE;\
   \
   su__VIEW_NAME(void) : base() {}\
   template<class TCOLL> explicit su__VIEW_NAME(TCOLL &tc) : base(){\
      (void)m_view.setup(S(base_coll_type&,tc)).begin();\
   }\
   /* (Need to offer all forms to allow additional TCOLL template(s)..) */\
   su__VIEW_NAME(su__VIEW_NAME &t) : base(t) {}\
   su__VIEW_NAME(su__VIEW_NAME const &t) : base(t) {}\
   ~su__VIEW_NAME(void) {}\
   \
   su__VIEW_NAME &assign(su__VIEW_NAME const &t){\
      return S(myself&,base::assign(t));\
   }\
   su__VIEW_NAME &operator=(su__VIEW_NAME const &t) {return assign(t);}\
   \
   using base::is_setup;\
   template<class TCOLL> su__VIEW_NAME &setup(TCOLL &tc){\
      (void)m_view.setup(S(base_coll_type&,tc));\
      return *this;\
   }\
   \
   using base::is_same_parent;\
   \
   using base::is_valid;\
   using base::operator boole;\
   \
   su__VIEW_NAME &reset(void) {return S(myself&,base::reset());}\
   \
   tp_const data(void) const{\
      ASSERT_RET(is_valid(), NIL);\
      return type_traits::to_const_tp(m_view.data());\
   }\
   tp_const operator*(void) const {return data();}\
   tp_const operator->(void) const {return data();}\
   \
   su__VIEW_NAME &begin(void){\
      ASSERT_RET(is_setup(), *this);\
      (void)m_view.begin();\
      return *this;\
   }\
   template<class TCOLL> su__VIEW_NAME &begin(TCOLL &tc){\
      (void)m_view.setup(S(base_coll_type&,tc)).begin();\
      return *this;\
   }\
   \
   boole has_next(void) const{\
      ASSERT_RET(is_valid(), FAL0);\
      return m_view.has_next();\
   }\
   su__VIEW_NAME &next(void){\
      ASSERT_RET(is_valid(), *this);\
      (void)m_view.next();\
      return *this;\
   }\
   su__VIEW_NAME &operator++(void) {return next();}\
   \
   using base::is_equal;\
   using base::operator==;\
   using base::operator!=;\
/*}}} }*/

#define su__VIEW_IMPL_NONCONST /*{{{*/\
   tp data(void){\
      ASSERT_RET(is_valid(), NIL);\
      return type_traits::to_tp(m_view.data());\
   }\
   s32 set_data(tp dat){\
      ASSERT_RET(is_valid(), err::einval);\
      return m_view.set_data(type_traits::to_vp(dat));\
   }\
   tp operator*(void) {return data();}\
   tp operator->(void) {return data();}\
   \
   su__VIEW_NAME &remove(void){\
      ASSERT_RET(is_valid(), *this);\
      (void)m_view.remove();\
      return *this;\
   }\
/*}}}*/

#define su__VIEW_IMPL_CONST /*{{{*/\
   /* (We need to cast away the 'const', but it is preserved logically..) */\
   template<class TCOLL> explicit su__VIEW_NAME(TCOLL const &tc){\
      (void)m_view.setup(S(base_coll_type&,C(TCOLL&,tc))).begin();\
   }\
   /* (Need to offer all copy-forms to allow TCOLL template..) */\
   explicit su__VIEW_NAME(su__VIEW_NAME_NONCONST<VIEWTRAITS,GBASEVIEWT> &t)\
         : base(t){\
   }\
   explicit su__VIEW_NAME(\
         su__VIEW_NAME_NONCONST<VIEWTRAITS,GBASEVIEWT> const &t) : base(t) {}\
   \
   su__VIEW_NAME &assign(\
         su__VIEW_NAME_NONCONST<VIEWTRAITS,GBASEVIEWT> const &t){\
      return S(myself&,base::assign(t));\
   }\
   su__VIEW_NAME &operator=(\
         su__VIEW_NAME_NONCONST<VIEWTRAITS,GBASEVIEWT> const &t){\
      return assign(t);\
   }\
   \
   /* (We need to cast away the 'const', but it is preserved logically..) */\
   template<class TCOLL> su__VIEW_NAME &setup(TCOLL const &tc){\
      (void)m_view.setup(S(base_coll_type&,C(TCOLL&,tc)));\
      return *this;\
   }\
/*}}}*/

#define su__VIEW_IMPL_NONASSOC

#define su__VIEW_IMPL_NONASSOC_NONCONST /*{{{*/\
   /* err::enone or error */\
   s32 insert(tp dat){\
      ASSERT_RET(is_setup(), err::einval);\
      return m_view.insert(type_traits::to_vp(dat));\
   }\
   s32 insert(base &startpos, base const &endpos){\
      ASSERT_RET(is_setup(), err::einval);\
      ASSERT_RET(startpos.is_valid(), err::einval);\
      ASSERT_RET(!endpos.is_valid() || startpos.is_same_parent(endpos),\
         err::einval);\
      if(DBGOR(1, 0)){\
         if(is_same_parent(startpos)){\
            myself v(startpos);\
            if(endpos.is_valid()){\
               for(;; ++v)\
                  if(v == endpos)\
                     break;\
                  else if(!v || v == *this)\
                     return err::einval;\
            }else{\
               for(; v; ++v)\
               if(v == *this)\
                  return err::einval;\
            }\
         }\
      }\
      return m_view.insert_range(su__VIEW_DOWNCAST(startpos).m_view,\
         su__VIEW_DOWNCAST(endpos).m_view);\
   }\
   \
   su__VIEW_NAME &remove(base &endpos){\
      ASSERT_RET(is_valid(), *this);\
      ASSERT_RET(!endpos.is_valid() || is_same_parent(endpos), *this);\
      (void)m_view.remove_range(su__VIEW_DOWNCAST(endpos).m_view);\
      return *this;\
   }\
/*}}}*/

#define su__VIEW_IMPL_NONASSOC_CONST

#define su__VIEW_IMPL_ASSOC /*{{{*/\
   key_tp_const key(void) const{\
      ASSERT_RET(is_valid(), NIL);\
      return key_type_traits::to_const_tp(m_view.key());\
   }\
/*}}}*/

#define su__VIEW_IMPL_ASSOC_NONCONST /*{{{*/\
   /* err::enone or error */\
   s32 reset_insert(key_tp_const key, tp dat){\
      ASSERT_RET(is_setup(), err::einval);\
      return m_view.reset_insert(key_type_traits::to_const_vp(key),\
         type_traits::to_vp(dat));\
   }\
   /* err::enone or error */\
   s32 reset_replace(key_tp_const key, tp dat){\
      ASSERT_RET(is_setup(), err::einval);\
      return m_view.reset_replace(key_type_traits::to_const_vp(key),\
         type_traits::to_vp(dat));\
   }\
/*}}}*/

#define su__VIEW_IMPL_ASSOC_CONST

#define su__VIEW_IMPL_UNIDIR
#define su__VIEW_IMPL_UNIDIR_NONCONST

#define su__VIEW_IMPL_UNIDIR_CONST /*{{{*/\
   /* (We need to cast away the 'const', but it is preserved logically..) */\
   template<class TCOLL> su__VIEW_NAME &begin(TCOLL const &tc){\
      (void)m_view.setup(S(base_coll_type&,C(TCOLL&,tc))).begin();\
      return *this;\
   }\
/*}}}*/

#define su__VIEW_IMPL_UNIDIR_NONASSOC /*{{{*/\
   /* is_valid() must be tested thereafter */\
   su__VIEW_NAME &go_to(uz off){\
      ASSERT_RET(is_setup(), *this);\
      (void)m_view.go_to(off);\
      return *this;\
   }\
   \
   boole find(tp_const dat, boole byptr=FAL0){\
      ASSERT_RET(is_setup(), (reset(), FAL0));\
      /* FIXME toolbox assert if !byptr */\
      return m_view.find(type_traits::to_const_vp(dat), byptr);\
   }\
/*}}}*/

#define su__VIEW_IMPL_UNIDIR_ASSOC /*{{{*/\
   boole find(key_tp_const key){\
      ASSERT_RET(is_setup(), (reset(), FAL0));\
      return m_view.find(key_type_traits::to_const_vp(key));\
   }\
/*}}}*/

#define su__VIEW_IMPL_BIDIR /*{{{*/\
   su__VIEW_NAME &end(void){\
      ASSERT_RET(is_setup(), *this);\
      (void)m_view.end();\
      return *this;\
   }\
   \
   boole has_last(void) const{\
      ASSERT_RET(is_valid(), FAL0);\
      return m_view.has_last();\
   }\
   su__VIEW_NAME &last(void){\
      ASSERT_RET(is_valid(), *this);\
      (void)m_view.last();\
      return *this;\
   }\
   su__VIEW_NAME &operator--(void) {return last();}\
/*}}}*/

#define su__VIEW_IMPL_BIDIR_NONCONST /*{{{*/\
   template<class TCOLL> su__VIEW_NAME &end(TCOLL &tc){\
      (void)m_view.setup(S(base_coll_type&,tc)).end();\
      return *this;\
   }\
/*}}}*/

#define su__VIEW_IMPL_BIDIR_CONST /*{{{*/\
   /* (We need to cast away the 'const', but it is preserved logically..) */\
   template<class TCOLL> su__VIEW_NAME &end(TCOLL const &tc){\
      (void)m_view.setup(S(base_coll_type&,C(TCOLL&,tc))).end();\
      return *this;\
   }\
/*}}}*/

#define su__VIEW_IMPL_BIDIR_NONASSOC /*{{{*/\
   boole rfind(tp_const dat, boole byptr=FAL0){\
      ASSERT_RET(is_setup(), (reset(), FAL0));\
      /* FIXME toolbox assert if !byptr */\
      return m_view.rfind(type_traits::to_const_vp(dat), byptr);\
   }\
/*}}}*/

#define su__VIEW_IMPL_BIDIR_ASSOC

#define su__VIEW_IMPL_RANDOM
#define su__VIEW_IMPL_RANDOM_NONCONST
#define su__VIEW_IMPL_RANDOM_CONST

#define su__VIEW_IMPL_RANDOM_NONASSOC /*{{{*/\
   /* is_valid() must be tested thereafter */\
   su__VIEW_NAME &go_around(sz reloff){\
      ASSERT_RET(is_valid(), *this);\
      return m_view.go_around(reloff);\
   }\
   su__VIEW_NAME &operator+=(sz reloff) {return go_around(reloff);}\
   su__VIEW_NAME &operator-=(sz reloff) {return go_around(-reloff);}\
   su__VIEW_NAME operator+(sz reloff) const{\
      su__VIEW_NAME rv(*this);\
      ASSERT_RET(is_valid(), rv);\
      return (rv += reloff);\
   }\
   su__VIEW_NAME operator-(sz reloff) const{\
      su__VIEW_NAME rv(*this);\
      ASSERT_RET(is_valid(), rv);\
      return (rv -= reloff);\
   }\
   \
   boole operator<(base const &t) const {return (base::cmp(t) < 0);}\
   boole operator>(base const &t) const {return (base::cmp(t) > 0);}\
   boole operator<=(base const &t) const {return (base::cmp(t) <= 0);}\
   boole operator>=(base const &t) const {return (base::cmp(t) >= 0);}\
/*}}}*/

#define su__VIEW_IMPL_RANDOM_ASSOC

#define su__VIEW_IMPL_END };

// Puuuh.  Let us go! {{{

#undef su__VIEW_CATEGORY
#define su__VIEW_CATEGORY view_category_non_assoc

#undef su__VIEW_TYPE
#define su__VIEW_TYPE view_type_unidir

#undef su__VIEW_NAME
#undef su__VIEW_NAME_NONCONST
#define su__VIEW_NAME view_unidir
#define su__VIEW_NAME_NONCONST view_unidir
su__VIEW_IMPL_START
   su__VIEW_IMPL_NONCONST
   su__VIEW_IMPL_NONASSOC
   su__VIEW_IMPL_NONASSOC_NONCONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_NONCONST
   su__VIEW_IMPL_UNIDIR_NONASSOC
su__VIEW_IMPL_END

#undef su__VIEW_NAME
#define su__VIEW_NAME view_unidir_const
su__VIEW_IMPL_START
   su__VIEW_IMPL_CONST
   su__VIEW_IMPL_NONASSOC
   su__VIEW_IMPL_NONASSOC_CONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_CONST
   su__VIEW_IMPL_UNIDIR_NONASSOC
su__VIEW_IMPL_END

#undef su__VIEW_TYPE
#define su__VIEW_TYPE view_type_bidir

#undef su__VIEW_NAME
#undef su__VIEW_NAME_NONCONST
#define su__VIEW_NAME view_bidir
#define su__VIEW_NAME_NONCONST view_bidir
su__VIEW_IMPL_START
   su__VIEW_IMPL_NONCONST
   su__VIEW_IMPL_NONASSOC
   su__VIEW_IMPL_NONASSOC_NONCONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_NONCONST
   su__VIEW_IMPL_UNIDIR_NONASSOC
   su__VIEW_IMPL_BIDIR
   su__VIEW_IMPL_BIDIR_NONCONST
   su__VIEW_IMPL_BIDIR_NONASSOC
su__VIEW_IMPL_END

#undef su__VIEW_NAME
#define su__VIEW_NAME view_bidir_const
su__VIEW_IMPL_START
   su__VIEW_IMPL_CONST
   su__VIEW_IMPL_NONASSOC
   su__VIEW_IMPL_NONASSOC_CONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_CONST
   su__VIEW_IMPL_UNIDIR_NONASSOC
   su__VIEW_IMPL_BIDIR
   su__VIEW_IMPL_BIDIR_CONST
   su__VIEW_IMPL_BIDIR_NONASSOC
su__VIEW_IMPL_END

#undef su__VIEW_TYPE
#define su__VIEW_TYPE view_type_random

#undef su__VIEW_NAME
#undef su__VIEW_NAME_NONCONST
#define su__VIEW_NAME view_random
#define su__VIEW_NAME_NONCONST view_random
su__VIEW_IMPL_START
   su__VIEW_IMPL_NONCONST
   su__VIEW_IMPL_NONASSOC
   su__VIEW_IMPL_NONASSOC_NONCONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_NONCONST
   su__VIEW_IMPL_UNIDIR_NONASSOC
   su__VIEW_IMPL_BIDIR
   su__VIEW_IMPL_BIDIR_NONCONST
   su__VIEW_IMPL_BIDIR_NONASSOC
   su__VIEW_IMPL_RANDOM
   su__VIEW_IMPL_RANDOM_NONCONST
   su__VIEW_IMPL_RANDOM_NONASSOC
su__VIEW_IMPL_END

#undef su__VIEW_NAME
#define su__VIEW_NAME view_random_const
su__VIEW_IMPL_START
   su__VIEW_IMPL_CONST
   su__VIEW_IMPL_NONASSOC
   su__VIEW_IMPL_NONASSOC_CONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_CONST
   su__VIEW_IMPL_UNIDIR_NONASSOC
   su__VIEW_IMPL_BIDIR
   su__VIEW_IMPL_BIDIR_CONST
   su__VIEW_IMPL_BIDIR_NONASSOC
   su__VIEW_IMPL_RANDOM
   su__VIEW_IMPL_RANDOM_CONST
   su__VIEW_IMPL_RANDOM_NONASSOC
su__VIEW_IMPL_END

#undef su__VIEW_CATEGORY
#define su__VIEW_CATEGORY view_category_assoc

#undef su__VIEW_TYPE
#define su__VIEW_TYPE view_type_assoc_unidir

#undef su__VIEW_NAME
#undef su__VIEW_NAME_NONCONST
#define su__VIEW_NAME view_assoc_unidir
#define su__VIEW_NAME_NONCONST view_assoc_unidir
su__VIEW_IMPL_START
   su__VIEW_IMPL_NONCONST
   su__VIEW_IMPL_ASSOC
   su__VIEW_IMPL_ASSOC_NONCONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_NONCONST
   su__VIEW_IMPL_UNIDIR_ASSOC
su__VIEW_IMPL_END

#undef su__VIEW_NAME
#define su__VIEW_NAME view_assoc_unidir_const
su__VIEW_IMPL_START
   su__VIEW_IMPL_CONST
   su__VIEW_IMPL_ASSOC
   su__VIEW_IMPL_ASSOC_CONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_CONST
   su__VIEW_IMPL_UNIDIR_ASSOC
su__VIEW_IMPL_END

#undef su__VIEW_TYPE
#define su__VIEW_TYPE view_type_assoc_bidir

#undef su__VIEW_NAME
#undef su__VIEW_NAME_NONCONST
#define su__VIEW_NAME view_assoc_bidir
#define su__VIEW_NAME_NONCONST view_assoc_bidir
su__VIEW_IMPL_START
   su__VIEW_IMPL_NONCONST
   su__VIEW_IMPL_ASSOC
   su__VIEW_IMPL_ASSOC_NONCONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_NONCONST
   su__VIEW_IMPL_UNIDIR_ASSOC
   su__VIEW_IMPL_BIDIR
   su__VIEW_IMPL_BIDIR_NONCONST
   su__VIEW_IMPL_BIDIR_ASSOC
su__VIEW_IMPL_END

#undef su__VIEW_NAME
#define su__VIEW_NAME view_assoc_bidir_const
su__VIEW_IMPL_START
   su__VIEW_IMPL_CONST
   su__VIEW_IMPL_ASSOC
   su__VIEW_IMPL_ASSOC_CONST
   su__VIEW_IMPL_UNIDIR
   su__VIEW_IMPL_UNIDIR_CONST
   su__VIEW_IMPL_UNIDIR_ASSOC
   su__VIEW_IMPL_BIDIR
   su__VIEW_IMPL_BIDIR_CONST
   su__VIEW_IMPL_BIDIR_ASSOC
   su__VIEW_IMPL_RANDOM
   su__VIEW_IMPL_RANDOM_CONST
   su__VIEW_IMPL_RANDOM_ASSOC
su__VIEW_IMPL_END

// }}}

// Cleanup {{{

#undef su__VIEW_DOWNCAST

#undef su__VIEW_IMPL_START
#undef su__VIEW_IMPL_NONCONST
#undef su__VIEW_IMPL_CONST
#undef su__VIEW_IMPL_NONASSOC
#undef su__VIEW_IMPL_NONASSOC_NONCONST
#undef su__VIEW_IMPL_NONASSOC_CONST
#undef su__VIEW_IMPL_ASSOC
#undef su__VIEW_IMPL_ASSOC_NONCONST
#undef su__VIEW_IMPL_ASSOC_CONST
#undef su__VIEW_IMPL_UNIDIR
#undef su__VIEW_IMPL_UNIDIR_NONCONST
#undef su__VIEW_IMPL_UNIDIR_CONST
#undef su__VIEW_IMPL_UNIDIR_NONASSOC
#undef su__VIEW_IMPL_UNIDIR_ASSOC
#undef su__VIEW_IMPL_BIDIR
#undef su__VIEW_IMPL_BIDIR_NONCONST
#undef su__VIEW_IMPL_BIDIR_CONST
#undef su__VIEW_IMPL_BIDIR_NONASSOC
#undef su__VIEW_IMPL_BIDIR_ASSOC
#undef su__VIEW_IMPL_RANDOM
#undef su__VIEW_IMPL_RANDOM_NONCONST
#undef su__VIEW_IMPL_RANDOM_CONST
#undef su__VIEW_IMPL_RANDOM_NONASSOC
#undef su__VIEW_IMPL_RANDOM_ASSOC
#undef su__VIEW_IMPL_END

#undef su__VIEW_CATEGORY
#undef su__VIEW_TYPE
#undef su__VIEW_NAME
#undef su__VIEW_NAME_NONCONST

// }}}

NSPC_END(su)
#include <su/code-ou.h>
#endif /* !su_C_LANG || defined CXX_DOXYGEN */
#endif /* su_VIEW_H */
/* s-it-mode */
