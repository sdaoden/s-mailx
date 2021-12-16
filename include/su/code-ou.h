/*@ Internal: opposite of code-in.h.
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
#ifndef su_CODE_IN_H
# error su/code-ou.h is useless if su/code-in.h has not been included
#endif
#undef su_CODE_IN_H

/* LANG */

#undef C_LANG
#undef C_DECL_BEGIN
#undef C_DECL_END
#undef NSPC_BEGIN
#undef NSPC_END
#undef NSPC_USE
#undef NSPC

#ifdef __cplusplus
# undef CLASS_NO_COPY
# undef SELFTHIS_RET

# undef PUB
# undef PRO
# undef PRI
# undef STA
# undef VIR
# undef OVR
# undef OVRX
#endif

#undef S
#undef R
#undef C

#undef NIL

#undef SHADOW

#undef EXPORT
#undef EXPORT_DATA
#undef IMPORT
#undef IMPORT_DATA

#undef CTA
#undef LCTA
#undef CTAV
#undef LCTAV
#undef MCTA

/* CC */

#undef PACKED

#undef INLINE
#undef SINLINE

#undef LIKELY
#undef UNLIKELY

/* SUPPORT MACROS+ */

#undef SU
#undef MX

#undef ABS
#undef CLIP
#undef IS_POW2
#undef MAX
#undef MIN
#undef ROUND_DOWN
#undef ROUND_DOWN2
#undef ROUND_UP
#undef ROUND_UP2

#undef ALIGNOF
#undef P_ALIGN
#undef Z_ALIGN_OVER
#undef Z_ALIGN
#undef Z_ALIGN_PZ

#undef ASSERT_INJ
#undef ASSERT_INJOR
#undef ASSERT_NB
#undef ASSERT
#undef ASSERT_LOC
#undef ASSERT_EXEC
#undef ASSERT_EXEC_LOC
#undef ASSERT_JUMP
#undef ASSERT_JUMP_LOC
#undef ASSERT_RET
#undef ASSERT_RET_LOC
#undef ASSERT_RET_VOID
#undef ASSERT_RET_VOID_LOC
#undef ASSERT_NYD_EXEC
#undef ASSERT_NYD_EXEC_LOC
#undef ASSERT_NYD
#undef ASSERT_NYD_LOC

#undef ATOMIC

#undef BITENUM_IS
#undef BITENUM_MASK

#undef DBG
#undef NDGB
#undef DBGOR
#undef DVL
#undef NDVL
#undef DVLOR
#undef DVLDBG
#undef NDVLDBG
#undef DVLDBGOR

#undef FIELD_DISTANCEOF
#undef FIELD_INITN
#undef FIN
#undef FIELD_INITI
#undef FII
#undef FIELD_OFFSETOF
#undef FIELD_RANGEOF
#undef FIELD_RANGE_COPY
#undef FIELD_RANGE_ZERO
#undef FIELD_SIZEOF

#undef MT

#undef NELEM

#ifdef su_FILE
# undef NYD_OU_LABEL
# undef su__NYD_IN_NOOP
# undef su__NYD_OU_NOOP
# undef su__NYD_NOOP
# undef su__NYD_IN
# undef su__NYD_OU
# undef su__NYD
# undef NYD_IN
# undef NYD_OU
# undef NYD
# undef NYD2_IN
# undef NYD2_OU
# undef NYD2
#endif

#undef P2UZ

#undef PCMP

#ifdef mx_SOURCE
# undef A_
#endif
#if defined su_SOURCE || defined mx_SOURCE
# undef _
# undef N_
# undef V_
#endif

#undef SMP

#undef STRING
#undef CONCAT

#undef STRUCT_ZERO

#undef UCMP

#undef UNCONST
#undef UNVOLATILE
#undef UNALIGN

#undef UNINIT
#undef UNINIT_DECL

#undef UNUSED

#undef VFIELD_SIZE
#undef VSTRUCT_SIZEOF

/* POD TYPE SUPPORT (only if !C++) */

#if defined su_HEADER || (defined su_FILE && su_C_LANG)
# undef ul
# undef ui
# undef us
# undef uc

# undef sl
# undef si
# undef ss
# undef sc

# undef u8
# undef s8

# undef u16
# undef s16

# undef u32
# undef s32

# undef u64
# undef s64

# undef uz
# undef sz

# undef up
# undef sp

# undef FAL0
# undef TRU1
# undef TRU2
# undef TRUM1
# undef boole
#endif /* su_HEADER || (su_FILE && su_C_LANG) */

#undef U8_MAX
#undef S8_MIN
#undef S8_MAX

#undef U16_MAX
#undef S16_MIN
#undef S16_MAX

#undef U32_MAX
#undef S32_MIN
#undef S32_MAX

#undef U64_MAX
#undef S64_MIN
#undef S64_MAX
#undef U64_C
#undef S64_C

#undef UZ_MAX
#undef SZ_MIN
#undef SZ_MAX
#undef UZ_BITS

/* state_gut */
#undef su__STATE_ON_GUT_FUN

/* MEMORY */

#undef su_ALLOCATE
#undef su_ALLOCATE_LOC
#undef su_REALLOCATE
#undef su_REALLOCATE_LOC

#undef su_ALLOC
#undef su_ALLOC_LOC
#undef su_ALLOC_LOCOR
#undef su_ALLOC_N
#undef su_ALLOC_N_LOC
#undef su_ALLOC_N_LOCOR
#undef su_CALLOC
#undef su_CALLOC_LOC
#undef su_CALLOC_LOCOR
#undef su_CALLOC_N
#undef su_CALLOC_N_LOC
#undef su_CALLOC_N_LOCOR
#undef su_REALLOC
#undef su_REALLOC_LOC
#undef su_REALLOC_LOCOR
#undef su_REALLOC_N
#undef su_REALLOC_N_LOC
#undef su_REALLOC_N_LOCOR
#undef su_TALLOC
#undef su_TALLOC_LOC
#undef su_TALLOC_LOCOR
#undef su_TCALLOC
#undef su_TCALLOC_LOC
#undef su_TCALLOC_LOCOR
#undef su_TREALLOC
#undef su_TREALLOC_LOC
#undef su_TREALLOC_LOCOR
#undef su_TALLOCF
#undef su_TALLOCF_LOC
#undef su_TALLOCF_LOCOR
#undef su_TCALLOCF
#undef su_TCALLOCF_LOC
#undef su_TCALLOCF_LOCOR
#undef su_TREALLOCF
#undef su_TREALLOCF_LOC
#undef su_TREALLOCF_LOCOR
#undef su_FREE
#undef su_FREE_LOC
#undef su_FREE_LOCOR

#if !su_C_LANG
# undef su_NEW
# undef su_NEW_LOC
# undef su_NEW_LOCOR
# undef su_CNEW
# undef su_CNEW_LOC
# undef su_CNEW_LOCOR
# undef su_NEWF_BLK
# undef su_NEWF_BLK_LOC
# undef su_NEWF_BLK_LOCOR
# undef su_NEW_HEAP
# undef su_NEW_HEAP_LOC
# undef su_NEW_HEAP_LOCOR
# undef su_DEL
# undef su_DEL_LOC
# undef su_DEL_LOCOR
# undef su_DEL_HEAP
# undef su_DEL_HEAP_LOC
# undef su_DEL_HEAP_LOCOR
# undef su_DEL_PRIVATE
# undef su_DEL_PRIVATE_LOC
# undef su_DEL_PRIVATE_LOCOR
# undef su_DEL_HEAP_PRIVATE
# undef su_DEL_HEAP_PRIVATE_LOC
# undef su_DEL_HEAP_PRIVATE_LOCOR
#endif /* !C_LANG */

#ifdef su_MEM_BAG_SELF
# ifdef su_HAVE_MEM_BAG_AUTO
#  undef su_AUTO_ALLOC
#  undef su_AUTO_ALLOC_LOC
#  undef su_AUTO_ALLOC_LOCOR
#  undef su_AUTO_ALLOC_N
#  undef su_AUTO_ALLOC_N_LOC
#  undef su_AUTO_ALLOC_N_LOCOR
#  undef su_AUTO_CALLOC
#  undef su_AUTO_CALLOC_LOC
#  undef su_AUTO_CALLOC_LOCOR
#  undef su_AUTO_CALLOC_N
#  undef su_AUTO_CALLOC_N_LOC
#  undef su_AUTO_CALLOC_N_LOCOR
#  undef su_AUTO_TALLOC
#  undef su_AUTO_TALLOC_LOC
#  undef su_AUTO_TALLOC_LOCOR
#  undef su_AUTO_TCALLOC
#  undef su_AUTO_TCALLOC_LOC
#  undef su_AUTO_TCALLOC_LOCOR
# endif /* su_HAVE_MEM_BAG_AUTO */
# ifdef su_HAVE_MEM_BAG_LOFI
#  undef su_LOFI_ALLOC
#  undef su_LOFI_ALLOC_LOC
#  undef su_LOFI_ALLOC_LOCOR
#  undef su_LOFI_ALLOC_N
#  undef su_LOFI_ALLOC_N_LOC
#  undef su_LOFI_ALLOC_N_LOCOR
#  undef su_LOFI_CALLOC
#  undef su_LOFI_CALLOC_LOC
#  undef su_LOFI_CALLOC_LOCOR
#  undef su_LOFI_CALLOC_N
#  undef su_LOFI_CALLOC_N_LOC
#  undef su_LOFI_CALLOC_N_LOCOR
#  undef su_LOFI_TALLOC
#  undef su_LOFI_TALLOC_LOC
#  undef su_LOFI_TALLOC_LOCOR
#  undef su_LOFI_TCALLOC
#  undef su_LOFI_TCALLOC_LOC
#  undef su_LOFI_TCALLOC_LOCOR
#  undef su_LOFI_FREE
#  undef su_LOFI_FREE_LOC
#  undef su_LOFI_FREE_LOCOR
# endif /* su_HAVE_MEM_BAG_LOFI */
#endif /* su_MEM_BAG_SELF */

#undef su_HEADER
#undef su_CXX_HEADER
#undef mx_HEADER
#undef rf_HEADER

/* s-it-mode */
