/*@ Internal: opposite of code-ou.h.
 *
 * Copyright (c) 2003 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#define CLASS_NO_COPY su_CLASS_NO_COPY

#define PUB su_PUB
#define PRO su_PRO
#define PRI su_PRI
#define STA su_STA
#define VIR su_VIR
#define OVR su_OVR
#define OVRX su_OVRX

#define S su_S
#define R su_R
#define C su_C

#define NIL su_NIL

#define SHADOW su_SHADOW

#ifdef su_HEADER
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

#undef ABS
#undef CLIP
#undef MAX
#undef MIN
#undef IS_POW2
#define ABS su_ABS
#define CLIP su_CLIP
#define MAX su_MAX
#define MIN su_MIN
#define IS_POW2 su_IS_POW2

#define ALIGNOF su_ALIGNOF
#define Z_ALIGN su_Z_ALIGN
#define Z_ALIGN_SMALL su_Z_ALIGN_SMALL
#define Z_ALIGN_PZ su_Z_ALIGN_PZ

/* ASSERT series */
#define ASSERT_INJ su_ASSERT_INJ
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
#define ASSERT_NYD_RET su_ASSERT_NYD_RET
#define ASSERT_NYD_RET_LOC su_ASSERT_NYD_RET_LOC
#define ASSERT_NYD_RET_VOID su_ASSERT_NYD_RET_VOID
#define ASSERT_NYD_RET_VOID_LOC su_ASSERT_NYD_RET_VOID_LOC

#define DBG su_DBG
#define NDGB su_NDBG
#define DBGOR su_DBGOR
#define DVL su_DVL
#define NDVL su_NDVL
#define DVLOR su_DVLOR

#define FIELD_INITN su_FIELD_INITN
#define FIELD_INITI su_FIELD_INITI
#define FIELD_OFFSETOF su_FIELD_OFFSETOF
#define FIELD_RANGEOF su_FIELD_RANGEOF
#define FIELD_SIZEOF su_FIELD_SIZEOF

#define NELEM su_NELEM

/* Not-Yet-Dead macros TODO stubs */
#undef NYD_IN
#undef NYD_OU
#undef NYD
#undef NYD2_IN
#undef NYD2_OU
#undef NYD2

#define NYD_OU_LABEL su_NYD_OU_LABEL
#if defined NDEBUG || (!defined su_HAVE_DEBUG && !defined su_HAVE_DEVEL)
   /**/
#elif defined su_HAVE_DEVEL
# define NYD_IN if(1){su_nyd_chirp(1, __FILE__, __LINE__, su_FUN);
# define NYD_OU \
   goto NYD_OU_LABEL;NYD_OU_LABEL:\
   su_nyd_chirp(2, __FILE__, __LINE__, su_FUN);}else{}
# define NYD if(0){}else{su_nyd_chirp(0, __FILE__, __LINE__, su_FUN);}
# ifdef NYD2
#  undef NYD2
#  define NYD2_IN if(1){su_nyd_chirp(1, __FILE__, __LINE__, su_FUN);
#  define NYD2_OU \
      goto NYD_OU_LABEL;NYD_OU_LABEL:\
      su_nyd_chirp(2, __FILE__, __LINE__, su_FUN);}else{}
#  define NYD2 if(0){}else{su_nyd_chirp(0, __FILE__, __LINE__, su_FUN);}
# endif
#else
# define NYD_IN do{su_nyd_chirp(1, __FILE__, __LINE__, su_FUN);
# define NYD_OU \
      goto NYD_OU_LABEL;NYD_OU_LABEL:\
      su_nyd_chirp(2, __FILE__, __LINE__, su_FUN);}while(0)
# define NYD do{su_nyd_chirp(0, __FILE__, __LINE__, su_FUN);}while(0)
# ifdef NYD2
#  undef NYD2
#  define NYD2_IN do{su_nyd_chirp(1, __FILE__, __LINE__, su_FUN);
#  define NYD2_OU \
      goto NYD_OU_LABEL;NYD_OU_LABEL:\
      su_nyd_chirp(2, __FILE__, __LINE__, su_FUN);}while(0)
#  define NYD2 do{su_nyd_chirp(0, __FILE__, __LINE__, su_FUN);}while(0)
# endif
#endif
/**/
#ifndef NYD
# define NYD_IN do{
# define NYD_OU goto NYD_OU_LABEL;NYD_OU_LABEL:;}while(0)
# define NYD do{}while(0)
#endif
#ifndef NYD2
# define NYD2_IN do{
# define NYD2_OU goto NYD_OU_LABEL;NYD_OU_LABEL:;}while(0)
# define NYD2 do{}while(0)
#endif

#define P2UZ su_P2UZ

#define PCMP su_PCMP

/* Translation: may NOT set errno! */
#ifdef su_SOURCE
# undef _
# undef N_
# undef V_
# define _(S) S
# define N_(S) S
# define V_(S) S
#endif

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
#if su_C_LANG
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
# define TRUM1 su_TRUM1
# define boole su_boole
#endif /* su_C_LANG */

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

/* MEMORY */

#define su_ALLOC su_MEM_ALLOC
#define su_ALLOC_N su_MEM_ALLOC_N
#define su_CALLOC su_MEM_CALLOC
#define su_CALLOC_N su_MEM_CALLOC_N
#define su_REALLOC su_MEM_REALLOC
#define su_REALLOC_n su_MEM_REALLOC_N
#define su_TALLOC su_MEM_TALLOC
#define su_TCALLOC su_MEM_TCALLOC
#define su_TREALLOC su_MEM_TREALLOC
#define su_FREE su_MEM_FREE
#ifdef su_HAVE_MEM_AUTO
# define su_LOFI_ALLOC su_MEM_LOFI_ALLOC
# define su_LOFI_FREE su_MEM_LOFI_FREE
# define su_AUTO_ALLOC_POOL su_MEM_AUTO_ALLOC_POOL
# define su_AUTO_CALLOC_POOL su_MEM_AUTO_CALLOC_POOL
# define su_AUTO_ALLOC su_MEM_AUTO_ALLOC
# define su_AUTO_CALLOC su_MEM_AUTO_CALLOC
#endif
#if !su_C_LANG
# define su_NEW su_MEM_NEW
# define su_CNEW su_MEM_CNEW
# define su_NEW_HEAP su_MEM_NEW_HEAP
# define su_DEL su_MEM_DEL
# define su_DEL_HEAP su_MEM_DEL_HEAP
# define su_DEL_PRIVATE su_MEM_DEL_PRIVATE
# define su_DEL_HEAP_PRIVATE su_MEM_DEL_HEAP_PRIVATE
#endif

/* s-it-mode */
