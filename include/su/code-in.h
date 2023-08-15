/*@ Internal: opposite of code-ou.h.
 *
 * Copyright (c) 2003 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
# error su/code-in.h must be included after su/code.h
#endif
#ifdef su_CODE_IN_H
# error su/code-ou.h must be included before including su/code-in.h again
#endif
#define su_CODE_IN_H

/* LANG */

#undef C_LANG
#undef C_DECL_BEGIN
#undef C_DECL_END
#define C_LANG su_C_LANG
#define C_DECL_BEGIN su_C_DECL_BEGIN
#define C_DECL_END su_C_DECL_END
#define NSPC_BEGIN su_NSPC_BEGIN
#define NSPC_END su_NSPC_END
#define NSPC_USE su_NSPC_USE
#define NSPC su_NSPC

#ifdef __cplusplus
# define CLASS_NO_COPY su_CLASS_NO_COPY
# define SELFTHIS_RET su_SELFTHIS_RET

# define PUB su_PUB
# define PRO su_PRO
# define PRI su_PRI
# define STA su_STA
# define VIR su_VIR
# define OVR su_OVR
# define OVRX su_OVRX
#endif

#define S su_S
#define R su_R
#define C su_C

#define NIL su_NIL

#define SHADOW su_SHADOW

#if defined su_HEADER || defined su_CXX_HEADER
# ifdef su_SOURCE
#  define EXPORT su_EXPORT
#  define EXPORT_DATA su_EXPORT_DATA
#  define IMPORT su_IMPORT
#  define IMPORT_DATA su_IMPORT_DATA
# endif
#elif defined mx_HEADER
# ifdef mx_SOURCE
#  define EXPORT su_EXPORT
#  define EXPORT_DATA su_EXPORT_DATA
#  define IMPORT su_IMPORT
#  define IMPORT_DATA su_IMPORT_DATA
# endif
#elif defined rf_HEADER
# ifdef rf_SOURCE
#  define EXPORT su_EXPORT
#  define EXPORT_DATA su_EXPORT_DATA
#  define IMPORT su_IMPORT
#  define IMPORT_DATA su_IMPORT_DATA
# endif
#endif
#ifndef EXPORT
# define EXPORT su_IMPORT
# define EXPORT_DATA su_IMPORT_DATA
# define IMPORT su_IMPORT
# define IMPORT_DATA su_IMPORT_DATA
#endif

#define CTA su_CTA
#define LCTA su_LCTA
#define CTAV su_CTAV
#define LCTAV su_LCTAV
#define MCTA su_MCTA

/* CC */

#define INLINE su_INLINE
#define SINLINE su_SINLINE

#define LIKELY su_LIKELY
#define UNLIKELY su_UNLIKELY

/* SUPPORT MACROS+ */

#undef SU
#undef MX
#define SU su_SU
#define MX su_MX

#undef ABS
#undef CLIP
#undef IS_POW2
#undef MAX
#undef MIN
#undef ROUND_DOWN
#undef ROUND_DOWN2
#undef ROUND_UP
#undef ROUND_UP2
#define ABS su_ABS
#define CLIP su_CLIP
#define IS_POW2 su_IS_POW2
#define MAX su_MAX
#define MIN su_MIN
#define ROUND_DOWN su_ROUND_DOWN
#define ROUND_DOWN2 su_ROUND_DOWN2
#define ROUND_UP su_ROUND_UP
#define ROUND_UP2 su_ROUND_UP2

#define ALIGNOF su_ALIGNOF
#define ALIGN_P su_ALIGN_P
#define ALIGN_Z_OVER su_ALIGN_Z_OVER
#define ALIGN_Z su_ALIGN_Z
#define ALIGN_Z_PZ su_ALIGN_Z_PZ

/* ASSERT series */
#define ASSERT_INJ su_ASSERT_INJ
#define ASSERT_INJOR su_ASSERT_INJOR
#define ASSERT_NB su_ASSERT_NB
#define ASSERT su_ASSERT
#define ASSERT_LOC su_ASSERT_LOC
#define ASSERT_EXEC su_ASSERT_EXEC
#define ASSERT_EXEC_LOC su_ASSERT_EXEC_LOC
#define ASSERT_JUMP su_ASSERT_JUMP
#define ASSERT_JUMP_LOC su_ASSERT_JUMP_LOC
#define ASSERT_RET su_ASSERT_RET
#define ASSERT_RET_LOC su_ASSERT_RET_LOC
#define ASSERT_RET_VOID su_ASSERT_RET_VOID
#define ASSERT_RET_VOID_LOC su_ASSERT_RET_VOID_LOC
#define ASSERT_NYD_EXEC su_ASSERT_NYD_EXEC
#define ASSERT_NYD_EXEC_LOC su_ASSERT_NYD_EXEC_LOC
#define ASSERT_NYD su_ASSERT_NYD
#define ASSERT_NYD_LOC su_ASSERT_NYD_LOC

#define ATOMIC su_ATOMIC

#define BITENUM_IS su_BITENUM /* FIXME DROP THIS */
#define BITENUM su_BITENUM
#define BITENUM_MASK su_BITENUM_MASK
#define PADENUM su_PADENUM

#define DBG su_DBG
#define NDGB su_NDBG
#define DBGOR su_DBGOR
#define DBGX su_DBGX
#define NDGBX su_NDBGX
#define DBGXOR su_DBGXOR
#define DVL su_DVL
#define NDVL su_NDVL
#define DVLOR su_DVLOR
#define DVLDBG su_DVLDBG
#define NDVLDBG su_NDVLDBG
#define DVLDBGOR su_DVLDBGOR

#define FALLTHRU su_FALLTHRU
#define FIELD_DISTANCEOF su_FIELD_DISTANCEOF
#define FIELD_INITN su_FIELD_INITN
#define FIN su_FIELD_INITN
#define FIELD_INITI su_FIELD_INITI
#define FII su_FIELD_INITI
#define FIELD_OFFSETOF su_FIELD_OFFSETOF
#define FIELD_RANGEOF su_FIELD_RANGEOF
#define FIELD_RANGE_COPY su_FIELD_RANGE_COPY
#define FIELD_RANGE_ZERO su_FIELD_RANGE_ZERO
#define FIELD_SIZEOF su_FIELD_SIZEOF

#define MT su_MT

#define NELEM su_NELEM

/* Not-Yet-Dead macros, only for su_FILE sources */
#ifdef su_FILE
# define NYD_OU_LABEL su_NYD_OU_LABEL
# if 0
#  define su__NYD_IN_NOOP do{do{}while(0)
#  define su__NYD_OU_NOOP goto NYD_OU_LABEL;NYD_OU_LABEL:;}while(0)
#  define su__NYD_NOOP do{}while(0)
# else
#  define su__NYD_IN_NOOP if(1){do{}while(0)
#  define su__NYD_OU_NOOP goto NYD_OU_LABEL;NYD_OU_LABEL:;}do{}while(0)
#  define su__NYD_NOOP do{}while(0)
# endif

# ifndef su_HAVE_DEVEL
#  define su__NYD_IN su__NYD_IN_NOOP
#  define su__NYD_OU su__NYD_OU_NOOP
#  define su__NYD su__NYD_NOOP
# elif defined NYDPROF_ENABLE || defined su_NYDPROF_ENABLE
#  error TODO NYDPROF not yet implemented.
# else
#  define su__NYD_IN \
   do{su_nyd_chirp(su_NYD_ACTION_ENTER,__FILE__,__LINE__,su_FUN)
#  define su__NYD_OU \
   goto NYD_OU_LABEL;NYD_OU_LABEL:\
   su_nyd_chirp(su_NYD_ACTION_LEAVE,__FILE__,__LINE__,su_FUN);}while(0)
#  define su__NYD \
   do{su_nyd_chirp(su_NYD_ACTION_ANYWHERE,__FILE__,__LINE__,su_FUN);}while(0)
# endif

# if defined NYD_ENABLE || defined su_NYD_ENABLE
#  define NYD_IN su__NYD_IN
#  define NYD_OU su__NYD_OU
#  define NYD su__NYD
#  if defined NYD2_ENABLE || defined su_NYD2_ENABLE
#   define NYD2_IN su__NYD_IN
#   define NYD2_OU su__NYD_OU
#   define NYD2 su__NYD
#  endif
# endif

# undef NYD_ENABLE
# undef NYDPROF_ENABLE
# undef NYD2_ENABLE
# undef NYDPROF2_ENABLE

# ifndef NYD_IN
#  define NYD_IN su__NYD_IN_NOOP
#  define NYD_OU su__NYD_OU_NOOP
#  define NYD su__NYD_NOOP
# endif
# ifndef NYD2_IN
#  define NYD2_IN su__NYD_IN_NOOP
#  define NYD2_OU su__NYD_OU_NOOP
#  define NYD2 su__NYD_NOOP
# endif
#endif /* su_FILE */

#define C2UZ su_C2UZ
#define P2UZ su_P2UZ

#define PCMP su_PCMP

/* Translation: may NOT set errno! */
#undef _
#undef N_
#undef V_
#ifdef mx_SOURCE
# undef A_
# define A_(S) S
#endif
#define _(S) S
#define N_(S) S
#define V_(S) S

#define SMP su_SMP

#define STRING su_STRING
#define CONCAT su_CONCAT

#define STRUCT_ZERO su_STRUCT_ZERO

#define UCMP su_UCMP

#define UNCONST su_UNCONST
#define UNVOLATILE su_UNVOLATILE
#define UNALIGN su_UNALIGN

#define UNINIT su_UNINIT
#define UNINIT_DECL su_UNINIT_DECL

#define UNUSED su_UNUSED

#define VFIELD_SIZE su_VFIELD_SIZE
#define VSTRUCT_SIZEOF su_VSTRUCT_SIZEOF

/* POD TYPE SUPPORT (only if !C++) */
#if defined su_HEADER || (defined su_FILE && su_C_LANG)
# define ul su_ul
# define ui su_ui
# define us su_us
# define uc su_uc

# define sl su_sl
# define si su_si
# define ss su_ss
# define sc su_sc

# define u8 su_u8
# define s8 su_s8

# define u16 su_u16
# define s16 su_s16

# define u32 su_u32
# define s32 su_s32

# define u64 su_u64
# define s64 su_s64

# define uz su_uz
# define sz su_sz

# define up su_up
# define sp su_sp

# define FAL0 su_FAL0
# define TRU1 su_TRU1
# define TRU2 su_TRU2
# define TRUM1 su_TRUM1
# define boole su_boole
#endif /* su_HEADER || (su_FILE && su_C_LANG) */

#define U8_MAX su_U8_MAX
#define S8_MIN su_S8_MIN
#define S8_MAX su_S8_MAX

#define U16_MAX su_U16_MAX
#define S16_MIN su_S16_MIN
#define S16_MAX su_S16_MAX

#define U32_MAX su_U32_MAX
#define S32_MIN su_S32_MIN
#define S32_MAX su_S32_MAX

#define U64_MAX su_U64_MAX
#define S64_MIN su_S64_MIN
#define S64_MAX su_S64_MAX
#define U64_C su_U64_C
#define S64_C su_S64_C

#define UZ_MAX su_UZ_MAX
#define SZ_MIN su_SZ_MIN
#define SZ_MAX su_SZ_MAX
#define UZ_BITS su_UZ_BITS

/* state_gut */
#if DVLOR(1, 0) || defined su_HAVE_STATE_GUT_FORK
# define su__STATE_ON_GUT_FUN
#endif

/* MEMORY */

#define su_ALLOCATE su_MEM_ALLOCATE
#define su_ALLOCATE_LOC su_MEM_ALLOCATE_LOC
#define su_REALLOCATE su_MEM_REALLOCATE
#define su_REALLOCATE_LOC su_MEM_REALLOCATE_LOC

#define su_ALLOC su_MEM_ALLOC
#define su_ALLOC_LOC su_MEM_ALLOC_LOC
#define su_ALLOC_LOCOR su_MEM_ALLOC_LOCOR
#define su_ALLOC_N su_MEM_ALLOC_N
#define su_ALLOC_N_LOC su_MEM_ALLOC_N_LOC
#define su_ALLOC_N_LOCOR su_MEM_ALLOC_N_LOCOR
#define su_CALLOC su_MEM_CALLOC
#define su_CALLOC_LOC su_MEM_CALLOC_LOC
#define su_CALLOC_LOCOR su_MEM_CALLOC_LOCOR
#define su_CALLOC_N su_MEM_CALLOC_N
#define su_CALLOC_N_LOC su_MEM_CALLOC_N_LOC
#define su_CALLOC_N_LOCOR su_MEM_CALLOC_N_LOCOR
#define su_REALLOC su_MEM_REALLOC
#define su_REALLOC_LOC su_MEM_REALLOC_LOC
#define su_REALLOC_LOCOR su_MEM_REALLOC_LOCOR
#define su_REALLOC_N su_MEM_REALLOC_N
#define su_REALLOC_N_LOC su_MEM_REALLOC_N_LOC
#define su_REALLOC_N_LOCOR su_MEM_REALLOC_N_LOCOR
#define su_TALLOC su_MEM_TALLOC
#define su_TALLOC_LOC su_MEM_TALLOC_LOC
#define su_TALLOC_LOCOR su_MEM_TALLOC_LOCOR
#define su_TCALLOC su_MEM_TCALLOC
#define su_TCALLOC_LOC su_MEM_TCALLOC_LOC
#define su_TCALLOC_LOCOR su_MEM_TCALLOC_LOCOR
#define su_TREALLOC su_MEM_TREALLOC
#define su_TREALLOC_LOC su_MEM_TREALLOC_LOC
#define su_TREALLOC_LOCOR su_MEM_TREALLOC_LOCOR
#define su_TALLOCF su_MEM_TALLOCF
#define su_TALLOCF_LOC su_MEM_TALLOCF_LOC
#define su_TALLOCF_LOCOR su_MEM_TALLOCF_LOCOR
#define su_TCALLOCF su_MEM_TCALLOCF
#define su_TCALLOCF_LOC su_MEM_TCALLOCF_LOC
#define su_TCALLOCF_LOCOR su_MEM_TCALLOCF_LOCOR
#define su_TREALLOCF su_MEM_TREALLOCF
#define su_TREALLOCF_LOC su_MEM_TREALLOCF_LOC
#define su_TREALLOCF_LOCOR su_MEM_TREALLOCF_LOCOR
#define su_FREE su_MEM_FREE
#define su_FREE_LOC su_MEM_FREE_LOC
#define su_FREE_LOCOR su_MEM_FREE_LOCOR

#if !su_C_LANG
# define su_NEW su_MEM_NEW
# define su_NEW_LOC su_MEM_NEW_LOC
# define su_NEW_LOCOR su_MEM_NEW_LOCOR
# define su_CNEW su_MEM_CNEW
# define su_CNEW_LOC su_MEM_CNEW_LOC
# define su_CNEW_LOCOR su_MEM_CNEW_LOCOR
# define su_NEWF_BLK su_MEM_NEWF_BLK
# define su_NEWF_BLK_LOC su_MEM_NEWF_BLK_LOC
# define su_NEWF_BLK_LOCOR su_MEM_NEWF_BLK_LOCOR
# define su_NEW_HEAP su_MEM_NEW_HEAP
# define su_NEW_HEAP_LOC su_MEM_NEW_HEAP_LOC
# define su_NEW_HEAP_LOCOR su_MEM_NEW_HEAP_LOCOR
# define su_DEL su_MEM_DEL
# define su_DEL_LOC su_MEM_DEL_LOC
# define su_DEL_LOCOR su_MEM_DEL_LOCOR
# define su_DEL_HEAP su_MEM_DEL_HEAP
# define su_DEL_HEAP_LOC su_MEM_DEL_HEAP_LOC
# define su_DEL_HEAP_LOCOR su_MEM_DEL_HEAP_LOCOR
# define su_DEL_PRIVATE su_MEM_DEL_PRIVATE
# define su_DEL_PRIVATE_LOC su_MEM_DEL_PRIVATE_LOC
# define su_DEL_PRIVATE_LOCOR su_MEM_DEL_PRIVATE_LOCOR
# define su_DEL_HEAP_PRIVATE su_MEM_DEL_HEAP_PRIVATE
# define su_DEL_HEAP_PRIVATE_LOC su_MEM_DEL_HEAP_PRIVATE_LOC
# define su_DEL_HEAP_PRIVATE_LOCOR su_MEM_DEL_HEAP_PRIVATE_LOCOR
#endif /* !C_LANG */

#ifdef su_MEM_BAG_SELF
# ifdef su_HAVE_MEM_BAG_AUTO
#  define su_AUTO_ALLOC su_MEM_BAG_SELF_AUTO_ALLOC
#  define su_AUTO_ALLOC_LOC su_MEM_BAG_SELF_AUTO_ALLOC_LOC
#  define su_AUTO_ALLOC_LOCOR su_MEM_BAG_SELF_AUTO_ALLOC_LOCOR
#  define su_AUTO_ALLOC_N su_MEM_BAG_SELF_AUTO_ALLOC_N
#  define su_AUTO_ALLOC_N_LOC su_MEM_BAG_SELF_AUTO_ALLOC_N_LOC
#  define su_AUTO_ALLOC_N_LOCOR su_MEM_BAG_SELF_AUTO_ALLOC_N_LOCOR
#  define su_AUTO_CALLOC su_MEM_BAG_SELF_AUTO_CALLOC
#  define su_AUTO_CALLOC_LOC su_MEM_BAG_SELF_AUTO_CALLOC_LOC
#  define su_AUTO_CALLOC_LOCOR su_MEM_BAG_SELF_AUTO_CALLOC_LOCOR
#  define su_AUTO_CALLOC_N su_MEM_BAG_SELF_AUTO_CALLOC_N
#  define su_AUTO_CALLOC_N_LOC su_MEM_BAG_SELF_AUTO_CALLOC_N_LOC
#  define su_AUTO_CALLOC_N_LOCOR su_MEM_BAG_SELF_AUTO_CALLOC_N_LOCOR
#  define su_AUTO_TALLOC su_MEM_BAG_SELF_AUTO_TALLOC
#  define su_AUTO_TALLOC_LOC su_MEM_BAG_SELF_AUTO_TALLOC_LOC
#  define su_AUTO_TALLOC_LOCOR su_MEM_BAG_SELF_AUTO_TALLOC_LOCOR
#  define su_AUTO_TCALLOC su_MEM_BAG_SELF_AUTO_TCALLOC
#  define su_AUTO_TCALLOC_LOC su_MEM_BAG_SELF_AUTO_TCALLOC_LOC
#  define su_AUTO_TCALLOC_LOCOR su_MEM_BAG_SELF_AUTO_TCALLOC_LOCOR
# endif /* su_HAVE_MEM_BAG_AUTO */
# ifdef su_HAVE_MEM_BAG_LOFI
#  define su_LOFI_ALLOC su_MEM_BAG_SELF_LOFI_ALLOC
#  define su_LOFI_ALLOC_LOC su_MEM_BAG_SELF_LOFI_ALLOC_LOC
#  define su_LOFI_ALLOC_LOCOR su_MEM_BAG_SELF_LOFI_ALLOC_LOCOR
#  define su_LOFI_ALLOC_N su_MEM_BAG_SELF_LOFI_ALLOC_N
#  define su_LOFI_ALLOC_N_LOC su_MEM_BAG_SELF_LOFI_ALLOC_N_LOC
#  define su_LOFI_ALLOC_N_LOCOR su_MEM_BAG_SELF_LOFI_ALLOC_N_LOCOR
#  define su_LOFI_CALLOC su_MEM_BAG_SELF_LOFI_CALLOC
#  define su_LOFI_CALLOC_LOC su_MEM_BAG_SELF_LOFI_CALLOC_LOC
#  define su_LOFI_CALLOC_LOCOR su_MEM_BAG_SELF_LOFI_CALLOC_LOCOR
#  define su_LOFI_CALLOC_N su_MEM_BAG_SELF_LOFI_CALLOC_N
#  define su_LOFI_CALLOC_N_LOC su_MEM_BAG_SELF_LOFI_CALLOC_N_LOC
#  define su_LOFI_CALLOC_N_LOCOR su_MEM_BAG_SELF_LOFI_CALLOC_N_LOCOR
#  define su_LOFI_TALLOC su_MEM_BAG_SELF_LOFI_TALLOC
#  define su_LOFI_TALLOC_LOC su_MEM_BAG_SELF_LOFI_TALLOC_LOC
#  define su_LOFI_TALLOC_LOCOR su_MEM_BAG_SELF_LOFI_TALLOC_LOCOR
#  define su_LOFI_TCALLOC su_MEM_BAG_SELF_LOFI_TCALLOC
#  define su_LOFI_TCALLOC_LOC su_MEM_BAG_SELF_LOFI_TCALLOC_LOC
#  define su_LOFI_TCALLOC_LOCOR su_MEM_BAG_SELF_LOFI_TCALLOC_LOCOR
#  define su_LOFI_FREE su_MEM_BAG_SELF_LOFI_FREE
#  define su_LOFI_FREE_LOC su_MEM_BAG_SELF_LOFI_FREE_LOC
#  define su_LOFI_FREE_LOCOR su_MEM_BAG_SELF_LOFI_FREE_LOCOR
# endif /* su_HAVE_MEM_BAG_LOFI */
#endif /* su_MEM_BAG_SELF */

/* s-itt-mode */
