/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ v15-compat TODO throw away
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef mx_COMPAT_H
#define mx_COMPAT_H

#include <mx/nail.h>

#include <su/mem-bag.h>

/* Generic heap memory TODO v15-compat */
#define n_alloc su_MEM_ALLOC
#define n_realloc su_MEM_REALLOC
#define n_calloc(NO,SZ) su_MEM_CALLOC_N(SZ, NO)
#define n_free su_MEM_FREE

/* Auto-reclaimed storage TODO v15-compat */
#define n_autorec_relax_create() \
      su_mem_bag_auto_relax_create(su_MEM_BAG_SELF)
#define n_autorec_relax_gut() \
      su_mem_bag_auto_relax_gut(su_MEM_BAG_SELF)
#define n_autorec_relax_unroll() \
      su_mem_bag_auto_relax_unroll(su_MEM_BAG_SELF)
/* (Even older obsolete names!) TODO v15-compat */
#define srelax_hold n_autorec_relax_create
#define srelax_rele n_autorec_relax_gut
#define srelax n_autorec_relax_unroll

#define n_autorec_alloc su_MEM_BAG_SELF_AUTO_ALLOC
#define n_autorec_calloc(NO,SZ) su_MEM_BAG_SELF_AUTO_CALLOC_N(SZ, NO)

/* Pseudo alloca (and also auto-reclaimed) TODO v15-compat */
#define n_lofi_alloc su_MEM_BAG_SELF_LOFI_ALLOC
#define n_lofi_calloc su_MEM_BAG_SELF_LOFI_CALLOC
#define n_lofi_free su_MEM_BAG_SELF_LOFI_FREE

#define n_lofi_snap_create() su_mem_bag_lofi_snap_create(su_MEM_BAG_SELF)
#define n_lofi_snap_unroll(COOKIE) \
   su_mem_bag_lofi_snap_unroll(su_MEM_BAG_SELF, COOKIE)

#endif /* mx_COMPAT_H */
/* s-it-mode */
