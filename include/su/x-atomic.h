/*@ atomic.h: internals, generic (dummy or ISO C11++) version.
 *
 * Copyright (c) 2001 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_ATOMIC_H
# error Please include atomic.h instead
#elif !defined su__ATOMIC_X
# define su__ATOMIC_X 1

su_USECASE_MX_DISABLED

   /* ISO C11++ at SU compile time++ */
# if defined su_ATOMIC_IS_REAL && \
      (defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112l && \
       !defined __STDC_NO_ATOMICS__)
#  define su__ATOMIC_HAVE_FUNS
#  include <stdatomic.h>
# endif

#elif su__ATOMIC_X == 1
# undef su__ATOMIC_X
# define su__ATOMIC_X 2

# ifdef su_ATOMIC_IS_REAL
#  ifdef su__ATOMIC_HAVE_FUNS

#   define a_X(X,Y) \
INLINE X CONCAT(su__atomic_xchg_,Y)(X ATOMIC *store, X nval){\
   return atomic_exchange(store, nval);\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
#   define a_X(X,Y) \
INLINE X CONCAT(su__atomic_busy_xchg_,Y)(X ATOMIC *store, X nval){\
   X rv;\
\
   while((rv = atomic_exchange(store, nval)) == nval)\
      ;\
   return rv;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
#   define a_X(X,Y) \
INLINE boole CONCAT(su__atomic_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   X roval = oval;\
\
   return S(boole,atomic_compare_exchange_strong(store, &roval, nval));\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
#   define a_X(X,Y) \
INLINE void CONCAT(su__atomic_busy_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   X roval = oval;\
\
   while(!atomic_compare_exchange_strong(store, &roval, nval))\
      ;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
#  endif /* su__ATOMIC_HAVE_FUNS */

   /* In MT case the source will provide protection via mutex */
# elif !defined su_HAVE_MT || defined su_SOURCE_ATOMIC /* su_ATOMIC_IS_REAL */
#  define su__ATOMIC_HAVE_FUNS

#   define a_X(X,Y) \
INLINE X CONCAT(su__atomic_xchg_,Y)(X ATOMIC *store, X nval){\
   X rv;\
\
   rv = *store;\
   *store = nval;\
   return rv;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
#   define a_X(X,Y) \
INLINE X CONCAT(su__atomic_busy_xchg_,Y)(X ATOMIC *store, X nval){\
   X rv;\
\
   while(*store == nval)\
      ;\
   rv = *store;\
   *store = nval;\
   return rv;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
#   define a_X(X,Y) \
INLINE boole CONCAT(su__atomic_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   boole rv;\
\
   if((rv = (*store == oval)))\
      *store = nval;\
   return rv;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
#   define a_X(X,Y) \
INLINE void CONCAT(su__atomic_busy_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   while(*store != oval)\
      ;\
   *store = nval;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#   undef a_X
# endif /* !defined su_HAVE_MT || defined su_SOURCE_ATOMIC */

#elif su__ATOMIC_X == 2
# undef su__ATOMIC_X
# define su__ATOMIC_X 3

#else
# error .
#endif
/* s-it-mode */
