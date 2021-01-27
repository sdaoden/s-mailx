/*@ C++ auto_type_toolbox<T>.  For the lazy sort.
 *@ If su_A_T_T_DECL_ONLY is defined before inclusion, just enough for
 *@ prototyping a deriviation is provided.
 *
 * Copyright (c) 2003 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_A_T_T_H
#ifndef su_A_T_T_DECL_ONLY
# define su_A_T_T_H
#endif

#ifdef CXX_DOXYGEN
/*!
 * \file
 * \ingroup COLL
 * \brief Automatic \r{type_toolbox<T>} toolboxes
 *
 * This file has the special property that if the preprocessor variable
 * \c{su_A_T_T_DECL_ONLY} is defined before it is included, then only minimal
 * declarations are provided, just enough to prototype an actual usage
 * \c{typedef}.
 * A later inclusion (without that variable) will then provide the definitions.
 */
#endif

#include <su/code.h>
su_USECASE_MX_DISABLED
#if !su_C_LANG || defined CXX_DOXYGEN

#ifndef su_A_T_T_DECL_ONLY
# include <su/mem.h>
#endif

#define su_CXX_HEADER
#include <su/code-in.h>
NSPC_BEGIN(su)

template<class T> class auto_type_toolbox;

#ifndef su_A_T_T_DECL_OK
# define su_A_T_T_DECL_OK

/*!
 * \ingroup COLL
 * \brief Automatic \r{type_toolbox<T>} toolboxes (\r{su/a-t-t.h})
 *
 * Supposed that a (newly created) C++ type provides a basic set of
 * functionality, easy creation of a toolbox instance becomes possible:
 *
 * \cb{
 * auto_type_toolbox<TYPE> const att;
 *
 * type_toolbox<TYPE> const *ttp = att.get_instance();
 * }
 *
 * For this to work, we need:
 *
 * \list{\li{
 * A default constructor and an assignment method, the latter of which with the
 * \r{su_state_err_type} plus \r{su_state_err_flags} status argument documented
 * for \r{su_clone_fun}.
 * }\li{
 * An unequality operator \fn{!=}.
 * }\li{
 * A function \fn{uz hash(void) const}.
 * }}
 *
 * \remarks{If \a{T} is a pointer type, the a-t-t will still create heap
 * clones, so \c{T*} and \c{T} are in fact treated alike!}
 *
 * \remarks{Many \SU object types and functionality groups offer
 * specializations, for example \r{CS}.}
 */
template<class T>
class auto_type_toolbox{
   su_CLASS_NO_COPY(auto_type_toolbox);
public:
   /*! \_ */
   typedef NSPC(su)type_traits<T> type_traits;

   /*! Accessing this field should be avoided because there may be
    * specializations which do not offer it -- \r{get_instance()} is inline. */
   static type_toolbox<T> const instance;

   /*! \_ */
   static type_toolbox<T> const *get_instance(void) {return &instance;}

private:
   static typename type_traits::tp s_clone(typename type_traits::tp_const t,
         u32 estate);
   static void s_delete(typename type_traits::tp self);
   static typename type_traits::tp s_assign(typename type_traits::tp self,
         typename type_traits::tp_const t, u32 estate);
   static sz s_compare(typename type_traits::tp_const self,
         typename type_traits::tp_const t);
   static uz s_hash(typename type_traits::tp_const self);
};
#endif /* su_A_T_T_DECL_OK */

#ifdef su_A_T_T_DECL_ONLY
# undef su_A_T_T_DECL_ONLY
#else
template<class T>
PRI STA typename type_traits::tp
auto_type_toolbox<T>::s_clone(typename type_traits::tp_const t, u32 estate){
   ASSERT_RET(t != NIL, NIL);
   type_traits::tp self = su_NEW(typename type_traits::type);
   if(self->assign(t, estate) != 0){
      su_DEL(self);
      self = NIL;
   }
   return self;
}

template<class T>
PRI STA void
auto_type_toolbox<T>::s_delete(typename type_traits::tp self){
   ASSERT_RET_VOID(self != NIL);
   su_DEL(self);
}

template<class T>
PRI STA typename type_traits::tp
auto_type_toolbox<T>::s_assign(typename type_traits::tp self,
      typename type_traits::tp_const t, u32 estate){
   ASSERT_RET(self != NIL, NIL);
   ASSER_RET(t != NIL, self);
   if(self != t){
      if(self->assign(t, estate) != 0)
         self = NIL;
   }
   return self;
}

template<class T>
PRI STA sz
auto_type_toolbox<T>::s_compare(typename type_traits::tp_const self,
      typename type_traits::tp_const t){
   ASSERT_RET(self != NIL, (t != NIL) ? -1 : 0);
   ASSERT_RET(t != NIL, 1);
   return self->compare(*t);
}

template<class T>
PRI STA uz
auto_type_toolbox<T>::s_hash(typename type_traits::tp_const self){
   ASSERT_RET(self != NIL, 0);
   return self->hash();
}

template<class T>
STA type_toolbox<T> const auto_type_toolbox<T>::instance =
      su_TYPE_TOOLBOX_I9R(&s_clone, &s_delete, &s_assign, &s_compare, &s_hash);
#endif // !su_A_T_T_DECL_ONLY

NSPC_END(su)
#include <su/code-ou.h>
#endif /* !su_C_LANG || defined CXX_DOXYGEN */
#endif /* su_A_T_T_H */
/* s-it-mode */
