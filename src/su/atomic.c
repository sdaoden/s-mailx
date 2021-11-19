/*@ Implementation of atomic.h.
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
#define su_FILE su_atomic
#define su_SOURCE
#define su_SOURCE_ATOMIC

#include "su/code.h"

su_USECASE_MX_DISABLED
su_EMPTY_FILE()
#if defined su_ATOMIC_IS_REAL || defined su_HAVE_MT

#ifndef su_ATOMIC_IS_REAL
# include "su/mutex.h"
#endif

#include "su/atomic.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#if su_ATOMIC_IS_REAL
# define a_LOCK_CAS()
# define a_UNLOCK_CAS()
# define a_LOCK_XCHG()
# define a_UNLOCK_XCHG()
#else
   /* In core-code.c, then. */
extern struct su_mutex su__atomic_cas_mtx;
extern struct su_mutex su__atomic_xchg_mtx;

# define a_LOCK_CAS() su_MUTEX_LOCK(&su__atomic_cas_mtx)
# define a_UNLOCK_CAS() su_MUTEX_UNLOCK(&su__atomic_cas_mtx)
# define a_LOCK_XCHG() su_MUTEX_LOCK(&su__atomic_xchg_mtx)
# define a_UNLOCK_XCHG() su_MUTEX_UNLOCK(&su__atomic_xchg_mtx)
#endif

#define a_X(X,Y) \
X \
CONCAT(su__atomic_g_xchg_,Y)(X ATOMIC *store, X nval){\
   X rv;\
\
   a_LOCK_XCHG();\
   rv = CONCAT(su_atomic_xchg_,Y)(store, nval);\
   a_UNLOCK_XCHG();\
\
   return rv;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#undef a_X
#define a_X(X,Y) \
X \
CONCAT(su__atomic_g_busy_xchg_,Y)(X ATOMIC *store, X nval){\
   X rv;\
\
   a_LOCK_XCHG();\
   rv = CONCAT(su_atomic_busy_xchg_,Y)(store, nval);\
   a_UNLOCK_XCHG();\
\
   return rv;\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#undef a_X
#define a_X(X,Y) \
boole \
CONCAT(su__atomic_g_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   boole rv;\
\
   a_LOCK_CAS();\
   rv = CONCAT(su_atomic_cas_,Y)(store, oval, nval);\
   a_UNLOCK_CAS();\
\
   return rv;\
}

#undef a_X
#define a_X(X,Y) \
void \
CONCAT(su__atomic_g_busy_cas_,Y)(X ATOMIC *store, X oval, X nval){\
   a_LOCK_CAS();\
   CONCAT(su_atomic_busy_cas_,Y)(store, oval, nval);\
   a_UNLOCK_CAS();\
}
a_X(u8,8) a_X(u16,16) a_X(u32,32) a_X(u64,64) a_X(uz,z) a_X(up,p)

#undef a_X

# undef a_LOCK_CAS
# undef a_UNLOCK_CAS
# undef a_LOCK_XCHG
# undef a_UNLOCK_XCHG

#include "su/code-ou.h"
#endif /* su_ATOMIC_IS_REAL || su_HAVE_MT */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_ATOMIC
/* s-it-mode */
