/*@ Sorting (of arrays).
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
#ifndef su_SORT_H
#define su_SORT_H

/*!
 * \file
 * \ingroup SORT
 * \brief \r{SORT}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* sort {{{ */
/*!
 * \defgroup SORT Sorting of arrays
 * \ingroup MISC
 * \brief Sorting of arrays (\r{su/sort.h})
 * @{
 */

/*! Sort an array of pointers with Knuth's shell sort algorithm
 * (Volume 3, page 84).
 * \a{arr} may be \NIL if \a{entries} is 0.
 * If \a{cmp_or_nil} is \NIL by-pointer comparison is performed.
 * Otherwise, \a{cmp_or_nil} will not be called for \NIL array entries,
 * which thus can result in false sorting. */
EXPORT void su_sort_shell_vpp(void const **arr, uz entries,
      su_cmp_fun cmp_or_nil);

/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class sort;

/* sort {{{ */
/*!
 * \ingroup SORT
 * C++ variant of \r{SORT} (\r{su/sort.h})
 */
class sort{
   su_CLASS_NO_COPY(sort);
public:
   /*! \copydoc{su_sort_shell_vpp()} */
   template<class T>
   static void shell(T const **arr, uz entries,
         typename type_toolbox<T>::cmp_fun cmp_or_nil){
      ASSERT_RET_VOID(entries == 0 || arr != NIL);
      su_sort_shell_vpp(R(void const**,arr), entries,
         R(su_cmp_fun,cmp_or_nil));
   }
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_SORT_H */
/* s-it-mode */
