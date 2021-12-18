/*@ (Yet) Manual config.h.
 *@ XXX Should be split into gen-config.h and config.h.
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
#ifndef su_CODE_H
# error Please include su/code.h not su/config.h.
#endif
#ifndef su_CONFIG_H
#define su_CONFIG_H

/*#define su_HAVE_NSPC*/

/* For now thought of _MX, _ROFF; _SU: standalone library */
#ifndef su_USECASE_MX
# define su_USECASE_SU
#endif

#ifdef su_USECASE_MX
   /* In this case we get our config, error maps etc., all from here.
    * We must take care not to break OPT_AMALGAMATION though */
# ifndef mx_HAVE_AMALGAMATION
#  include <mx/gen-config.h>
# endif
#else
# include <su/gen-config.h>
#endif

/* Global configurables (code.h:CONFIG): values */

/* Hardware page size (xxx additional dynamic lookup support) */
#ifndef su_PAGE_SIZE
# error Need su_PAGE_SIZE configuration
#endif

/* Global configurables (code.h:CONFIG): features */
#ifdef su_USECASE_SU
# define su_HAVE_NSPC
/*# define su_HAVE_DEBUG*/
/*# define su_HAVE_DEVEL*/
# define su_HAVE_DOCSTRINGS
# define su_HAVE_MEM_BAG_AUTO
# define su_HAVE_MEM_BAG_LOFI
/*# define su_HAVE_MEM_CANARIES_DISABLE*/
# define su_HAVE_MD
/*TODO #  define su_HAVE_MD_BLAKE2B*/
# define su_HAVE_RE /* Unconditionally for now xxx */
# undef su_HAVE_SMP /* for now xxx */
#  undef su_HAVE_MT
/*#  define su_ATOMIC_IS_REAL <-> ISO C11++ at SU compile time or other impl */
/*#  define su__MUTEX_IMPL_SIZE "sizeof(pthread_mutex_t)" */
/*#  define su__MUTEX_IMPL_ALIGNMENT 128 <-> the real sizeof */
/*#  define su__THREAD_IMPL_SIZE "sizeof(pthread_t)" */
/*#  define su__THREAD_IMPL_ALIGNMENT 128 <-> the real sizeof */
# undef su_HAVE_STATE_GUT_FORK

#elif defined su_USECASE_MX /* sue_USECASE_SU */
# ifdef mx_HAVE_DEBUG
#  define su_HAVE_DEBUG
# endif
# ifdef mx_HAVE_DEVEL
#  define su_HAVE_DEVEL
#  define su_NYD_ENABLE
#  define su_NYD2_ENABLE
# endif
# ifdef mx_HAVE_DOCSTRINGS
#  define su_HAVE_DOCSTRINGS
# endif
# define su_HAVE_MEM_BAG_AUTO
# define su_HAVE_MEM_BAG_LOFI
# ifdef mx_HAVE_EXTERNAL_MEM_CHECK
#  define su_HAVE_MEM_CANARIES_DISABLE
# endif
# define su_HAVE_MD
#  undef su_HAVE_MD_BLAKE2B
# ifdef mx_HAVE_REGEX
#  define su_HAVE_RE
# endif
# undef su_HAVE_SMP
#  undef su_HAVE_MT
# undef su_HAVE_STATE_GUT_FORK

/* */
struct mx_go_data_ctx;
struct su_mem_bag;
struct su__mem_bag_mx {struct su_mem_bag *mbm_bag;};
extern struct mx_go_data_ctx *mx_go_data;
# define su_MEM_BAG_SELF (su_R(struct su__mem_bag_mx*,mx_go_data)->mbm_bag)

#else /* su_USECASE_MX */
# error Unknown SU usecase
#endif

/* Internal configurables: values */

/* Number of Not-Yet-Dead calls that are remembered */
#ifdef su_HAVE_DEVEL
# define su_NYD_ENTRIES (25 * 84)
#endif

#endif /* !su_CONFIG_H */
/* s-it-mode */
