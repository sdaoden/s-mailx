/*@ Code of the basic infrastructure (POD types, macros etc.) and functions.
 *@ And main documentation entry point, as below.
 *@ - Reacts upon su_HAVE_DEBUG, su_HAVE_DEVEL, and NDEBUG.
 *@   The latter is a precondition for su_HAVE_INLINE; dependent upon compiler
 *@   __OPTIMIZE__ (and __OPTIMIZE_SIZE__) may be looked at in addition, then.
 *@   su_HAVE_DEVEL is meant as a possibility to enable test paths with
 *@   debugging disabled.
 *@ - Some macros require su_FILE to be defined to a literal.
 *@ - Define su_MASTER to inject what is to be injected once; for example,
 *@   it enables su_M*CTA() compile time assertions.
 *
 * Copyright (c) 2001 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_CODE_H
#include <su/config.h>
/* CONFIG {{{ */
#ifdef DOXYGEN
   /* Features */
# define su_HAVE_NSPC
# define su_HAVE_DEBUG
# define su_HAVE_DEVEL
# define su_HAVE_DOCSTRINGS
# define su_HAVE_MEM_BAG_AUTO
# define su_HAVE_MEM_BAG_LOFI
# define su_HAVE_MEM_CANARIES_DISABLE
# define su_HAVE_SMP
# define su_HAVE_MT
   /* Values */
# define su_PAGE_SIZE
#endif
/* CONFIG }}} */
/* OS {{{ */
#define su_OS_CYGWIN 0
#define su_OS_DARWIN 0
#define su_OS_DRAGONFLY 0
#define su_OS_EMX 0
#define su_OS_FREEBSD 0
#define su_OS_LINUX 0
#define su_OS_MINIX 0
#define su_OS_MSDOS 0
#define su_OS_NETBSD 0
#define su_OS_OPENBSD 0
#define su_OS_SOLARIS 0
#define su_OS_SUNOS 0
#define su_OS_WIN32 0
#define su_OS_WIN64 0
#if 0
#elif defined __CYGWIN__
# undef su_OS_CYGWIN
# define su_OS_CYGWIN 1
#elif defined DARWIN || defined _DARWIN
# undef su_OS_DARWIN
# define su_OS_DARWIN 1
#elif defined __DragonFly__
# undef su_OS_DRAGONFLY
# define su_OS_DRAGONFLY 1
#elif defined __EMX__
# undef su_OS_EMX
# define su_OS_EMX 1
#elif defined __FreeBSD__
# undef su_OS_FREEBSD
# define su_OS_FREEBSD 1
#elif defined __linux__ || defined __linux
# undef su_OS_LINUX
# define su_OS_LINUX 1
#elif defined __minix
# undef su_OS_MINIX
# define su_OS_MINIX 1
#elif defined __MSDOS__
# undef su_OS_MSDOS
# define su_OS_MSDOS 1
#elif defined __NetBSD__
# undef su_OS_NETBSD
# define su_OS_NETBSD 1
#elif defined __OpenBSD__
# undef su_OS_OPENBSD
# define su_OS_OPENBSD 1
#elif defined __solaris__ || defined __sun
# if defined __SVR4 || defined __svr4__
#  undef su_OS_SOLARIS
#  define su_OS_SOLARIS 1
# else
#  undef su_OS_SUNOS
#  define su_OS_SUNOS 1
# endif
#endif
/* OS }}} */
/* LANG {{{ */
#ifndef __cplusplus
# define su_C_LANG 1
# define su_C_DECL_BEGIN
# define su_C_DECL_END
   /* Casts */
# define su_S(T,I) ((T)(I))
# define su_R(T,I) ((T)(I))
# define su_C(T,I) ((T)su_R(su_up,I))
# define su_NIL ((void*)0)
#else
# define su_C_LANG 0
# define su_C_DECL_BEGIN extern "C" {
# define su_C_DECL_END }
# ifdef su_HAVE_NSPC
#  define su_NSPC_BEGIN(X) namespace X {
#  define su_NSPC_END(X) }
#  define su_NSPC_USE(X) using namespace X;
#  define su_NSPC(X) X::
# else
#  define su_NSPC_BEGIN(X) /**/
#  define su_NSPC_END(X) /**/
#  define su_NSPC_USE(X) /**/
#  define su_NSPC(X) /**/::
# endif
   /* Disable copy-construction and assignment of class */
# define su_CLASS_NO_COPY(C) private:C(C const &);C &operator=(C const &);
   /* If C++ class inherits from a C class, and the C class "return self", we
    * have to waste a return register even if self==this */
# define su_SELFTHIS_RET(X) /* return *(X); */ X; return *this
   /* C++ only allows those at the declaration, not the definition */
# define su_PUB
# define su_PRO
# define su_PRI
# define su_STA
# define su_VIR
# define su_OVR
   /* This is for the declarator only */
# if __cplusplus +0 < 201103L
#  define su_OVRX
# else
#  define su_OVRX override
# endif
   /* Casts */
# define su_S(T,I) static_cast<T>(I)
# define su_R(T,I) reinterpret_cast<T>(I)
# define su_C(T,I) const_cast<T>(I)
# define su_NIL (0L)
#endif /* __cplusplus */
#define su_SHADOW
/* "su_EXPORT myfun()", "class su_EXPORT myclass" */
#if su_OS_WIN32 || su_OS_WIN64
# define su_EXPORT __declspec((dllexport))
# define su_EXPORT_DATA __declspec((dllexport))
# define su_IMPORT __declspec((dllimport))
# define su_IMPORT_DATA __declspec((dllimport))
#else
# define su_EXPORT /*extern*/
# define su_EXPORT_DATA extern
# define su_IMPORT /*extern*/
# define su_IMPORT_DATA extern
#endif
/* Compile-Time-Assert
 * Problem is that some compilers warn on unused local typedefs, so add
 * a special local CTA to overcome this */
#if (!su_C_LANG && __cplusplus +0 >= 201103L) || defined DOXYGEN
# define su_CTA(T,M) static_assert(T, M)
# define su_LCTA(T,M) static_assert(T, M)
#elif 0 /* unusable! */ && \
      defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112L
# define su_CTA(T,M) _Static_assert(T, M)
# define su_LCTA(T,M) _Static_assert(T, M)
#else
# define su_CTA(T,M) su__CTA_1(T, su_FILE, __LINE__)
# define su_LCTA(T,M) su__LCTA_1(T, su_FILE, __LINE__)
# define su__CTA_1(T,F,L) su__CTA_2(T, F, L)
# define su__CTA_2(T,F,L) \
      typedef char ASSERTION_failed_file_ ## F ## _line_ ## L[(T) ? 1 : -1]
# define su__LCTA_1(T,F,L) su__LCTA_2(T, F, L)
# define su__LCTA_2(T,F,L) \
do{\
   typedef char ASSERT_failed_file_ ## F ## _line_ ## L[(T) ? 1 : -1];\
   ASSERT_failed_file_ ## F ## _line_ ## L __i_am_unused__;\
   su_UNUSED(__i_am_unused__);\
}while(0)
#endif
#define su_CTAV(T) su_CTA(T, "Unexpected value of constant")
#define su_LCTAV(T) su_LCTA(T, "Unexpected value of constant")
#ifdef su_MASTER
# define su_MCTA(T,M) su_CTA(T, M);
#else
# define su_MCTA(T,M)
#endif
/* LANG }}} */
/* CC {{{ */
#define su_CC_CLANG 0
#define su_CC_VCHECK_CLANG(X,Y) 0
#define su_CC_GCC 0
#define su_CC_VCHECK_GCC(X,Y) 0
#define su_CC_PCC 0
#define su_CC_VCHECK_PCC(X,Y) 0
#define su_CC_SUNPROC 0
#define su_CC_VCHECK_SUNPROC(X,Y) 0
#define su_CC_TINYC 0
#define su_CC_VCHECK_TINYC(X,Y) 0
#ifdef __clang__
# undef su_CC_CLANG
# undef su_CC_VCHECK_CLANG
# define su_CC_CLANG 1
# define su_CC_VCHECK_CLANG(X,Y) \
      (__clang_major__ +0 > (X) || \
       (__clang_major__ +0 == (X) && __clang_minor__ +0 >= (Y)))
# define su_CC_EXTEN __extension__
# define su_CC_PACKED __attribute__((packed))
# if !defined su_CC_BOM &&\
      defined __BYTE_ORDER__ && defined __ORDER_LITTLE_ENDIAN__ &&\
      defined __ORDER_BIG_ENDIAN
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#   define su_CC_BOM su_CC_BOM_LITTLE
#  elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define su_CC_BOM su_CC_BOM_BIG
#  else
#   error Unsupported __BYTE_ORDER__
#  endif
# endif
/* __GNUC__ after some other Unix compilers which also define __GNUC__ */
#elif defined __PCC__ /* __clang__ */
# undef su_CC_PCC
# undef su_CC_VCHECK_PCC
# define su_CC_PCC 1
# define su_CC_VCHECK_PCC(X,Y) \
      (__PCC__ +0 > (X) || (__PCC__ +0 == (X) && __PCC_MINOR__ +0 >= (Y)))
# define su_CC_EXTEN __extension__
# define su_CC_PACKED __attribute__((packed))
# if !defined su_CC_BOM &&\
      defined __BYTE_ORDER__ && defined __ORDER_LITTLE_ENDIAN__ &&\
      defined __ORDER_BIG_ENDIAN
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#   define su_CC_BOM su_CC_BOM_LITTLE
#  elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define su_CC_BOM su_CC_BOM_BIG
#  else
#   error Unsupported __BYTE_ORDER__
#  endif
# endif
#elif defined __SUNPRO_C /* __PCC__ */
# undef su_CC_SUNPROC
# define su_CC_SUNPROC 1
# define su_CC_PACKED TODO: PACKED attribute not supported for SunPro C
#elif defined __TINYC__ /* __SUNPRO_C */
# undef su_CC_TINYC
# define su_CC_TINYC 1
# define su_CC_EXTEN /* __extension__ (ignored) */
# define su_CC_PACKED __attribute__((packed))
#elif defined __GNUC__ /* __TINYC__ */
# undef su_CC_GCC
# undef su_CC_VCHECK_GCC
# define su_CC_GCC 1
# define su_CC_VCHECK_GCC(X,Y) \
      (__GNUC__ +0 > (X) || (__GNUC__ +0 == (X) && __GNUC_MINOR__ +0 >= (Y)))
# define su_CC_EXTEN __extension__
# define su_CC_PACKED __attribute__((packed))
# if !defined su_CC_BOM &&\
      defined __BYTE_ORDER__ && defined __ORDER_LITTLE_ENDIAN__ &&\
      defined __ORDER_BIG_ENDIAN
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#   define su_CC_BOM su_CC_BOM_LITTLE
#  elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define su_CC_BOM su_CC_BOM_BIG
#  else
#   error Unsupported __BYTE_ORDER__
#  endif
# endif
#elif !defined su_CC_IGNORE_UNKNOWN
# error SU: This compiler is not yet supported.
# error SU: To continue with your CFLAGS etc., define su_CC_IGNORE_UNKNOWN.
# error SU: It may be necessary to define su_CC_PACKED to a statement that
# error SU: enables structure packing; it may not be a #pragma, but a _Pragma
#endif
#ifndef su_CC_EXTEN
# define su_CC_EXTEN
#endif
#ifndef su_CC_PACKED
# define su_CC_PACKED TODO: PACKED attribute not supported for this compiler
#endif
#if defined su_CC_BOM || defined DOXYGEN
# ifdef DOXYGEN
#  define su_CC_BOM
# endif
# define su_CC_BOM_LITTLE 1234
# define su_CC_BOM_BIG 4321
#endif
#if !defined su_CC_UZ_TYPE && defined __SIZE_TYPE__
# define su_CC_UZ_TYPE __SIZE_TYPE__
#endif
/* Function name */
#if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L
# define su_FUN __func__
#elif su_CC_CLANG || su_CC_VCHECK_GCC(3, 4) || su_CC_PCC || su_CC_TINYC
# define su_FUN __extension__ __FUNCTION__
#else
# define su_FUN su_empty /* Something that is not a literal */
#endif
/* inline keyword */
#define su_HAVE_INLINE
#if su_C_LANG
# ifdef DOXYGEN
#  define su_INLINE inline
#  define su_SINLINE inline
# elif su_CC_GCC
   /* After lots of trouble with OpenBSD/gcc 4.2.1 and SunOS/gcc 3.4.3 */
#  if !su_CC_VCHECK_GCC(3, 2) /* Unsure: only used C++ at that time */
#   define su_INLINE extern __inline
#   define su_SINLINE static __inline
#  elif !su_CC_VCHECK_GCC(4, 3)
#   define su_INLINE extern __inline __attribute__((always_inline))
#   define su_SINLINE static __inline __attribute__((always_inline))
   /* xxx gcc 8.3.0 bug: does not truly inline with -Os */
#  elif !su_CC_VCHECK_GCC(8, 3) || !defined __OPTIMIZE__ ||\
      !defined __STDC_VERSION__ || __STDC_VERSION__ +0 < 199901L
#   define su_INLINE extern __inline __attribute__((gnu_inline))
#   define su_SINLINE static __inline __attribute__((gnu_inline))
#  elif !defined NDEBUG || !defined __OPTIMIZE__
#   define su_INLINE static inline
#   define su_SINLINE static inline
#  else
#   define su_INLINE inline
#   define su_SINLINE static inline
#  endif
# elif su_CC_CLANG || su_CC_PCC
#  if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L
#   if !defined NDEBUG || !defined __OPTIMIZE__
#    define su_INLINE static inline
#    define su_SINLINE static inline
#   else
     /* clang does not like inline with <-O2 */
#    define su_INLINE inline __attribute__((always_inline))
#    define su_SINLINE static inline __attribute__((always_inline))
#   endif
#  else
#   define su_INLINE static __inline
#   define su_SINLINE static __inline
#  endif
# else
#  define su_INLINE static /* TODO __attribute__((unused)) alike? */
#  define su_SINLINE static /* TODO __attribute__((unused)) alike? */
#  undef su_HAVE_INLINE
# endif
#else
# define su_INLINE inline
# define su_SINLINE static inline
#endif
#ifndef NDEBUG
# undef su_HAVE_INLINE
#endif
#if defined __predict_true && defined __predict_false
# define su_LIKELY(X) __predict_true((X) != 0)
# define su_UNLIKELY(X) __predict_false((X) != 0)
#elif su_CC_CLANG || su_CC_VCHECK_GCC(2, 96) || su_CC_PCC || su_CC_TINYC
# define su_LIKELY(X) __builtin_expect((X) != 0, 1)
# define su_UNLIKELY(X) __builtin_expect((X) != 0, 0)
#else
# define su_LIKELY(X) ((X) != 0)
# define su_UNLIKELY(X) ((X) != 0)
#endif
/* CC }}} */
/* SUPPORT MACROS+ {{{ */
/* USECASE_XY_DISABLED for tagging unused files:
 * git rm `git grep ^su_USECASE_MX_DISABLED` */
#ifdef su_USECASE_MX
# define su_USECASE_MX_DISABLED This file is not a (valid) compilation unit
#endif
#ifndef su_USECASE_MX_DISABLED
# define su_USECASE_MX_DISABLED
#endif
/* Basic support macros, with side effects */
#define su_ABS(A) ((A) < 0 ? -(A) : (A))
#define su_CLIP(X,A,B) (((X) <= (A)) ? (A) : (((X) >= (B)) ? (B) : (X)))
#define su_IS_POW2(X) ((((X) - 1) & (X)) == 0)
#define su_MAX(A,B) ((A) < (B) ? (B) : (A))
#define su_MIN(A,B) ((A) < (B) ? (A) : (B))
#define su_ROUND_DOWN(X,BASE) (((X) / (BASE)) * (BASE))
#define su_ROUND_DOWN2(X,BASE) ((X) & (~((BASE) - 1)))
#define su_ROUND_UP(X,BASE) ((((X) + ((BASE) - 1)) / (BASE)) * (BASE))
#define su_ROUND_UP2(X,BASE) (((X) + ((BASE) - 1)) & (~((BASE) - 1)))
/* Alignment.  Note: su_uz POW2 asserted in POD section below! */
/* Commented out: "_Alignof() applied to an expression is a GNU extension" */
#if 0 && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112L
# include <stdalign.h>
# define su_ALIGNOF(X) _Alignof(X)
#else
# define su_ALIGNOF(X) su_ROUND_UP2(sizeof(X), su__ZAL_L)
#endif
#define su_P_ALIGN(DTYPE,OTYPE,MEM) \
   su_R(DTYPE,\
      su_IS_POW2(su_ALIGNOF(OTYPE))\
         ? su_ROUND_UP2(su_R(su_up,MEM), su_ALIGNOF(OTYPE))\
         : su_ROUND_UP(su_R(su_up,MEM), su_ALIGNOF(OTYPE)))
/* Roundup/align an integer;  Note: POW2 asserted in POD section below! */
#define su_Z_ALIGN_OVER(X) su_ROUND_UP2(su_S(su_uz,X), 2 * su__ZAL_L)
#define su_Z_ALIGN(X) su_ROUND_UP2(su_S(su_uz,X), su__ZAL_L)
#define su_Z_ALIGN_PZ(X) su_ROUND_UP2(su_S(su_uz,X), su__ZAL_S)
/* (These are below MCTA()d to be of equal size[, however].)
 * _L must adhere to the minimum aligned claimed in the \mainpage */
# define su__ZAL_S su_MAX(sizeof(su_uz), sizeof(void*))
# define su__ZAL_L su_MAX(su__ZAL_S, sizeof(su_u64))/* XXX FP,128bit */
/* Variants of ASSERT */
#if defined NDEBUG || defined DOXYGEN
# define su_ASSERT_INJ(X)
# define su_ASSERT_INJOR(X,Y) Y
# define su_ASSERT_NB(X) ((void)0)
# define su_ASSERT(X) do{}while(0)
# define su_ASSERT_LOC(X,FNAME,LNNO) do{}while(0)
# define su_ASSERT_EXEC(X,S) do{}while(0)
# define su_ASSERT_EXEC_LOC(X,S,FNAME,LNNO) do{}while(0)
# define su_ASSERT_JUMP(X,L) do{}while(0)
# define su_ASSERT_JUMP_LOC(X,L,FNAME,LNNO) do{}while(0)
# define su_ASSERT_RET(X,Y) do{}while(0)
# define su_ASSERT_RET_LOC(X,Y,FNAME,LNNO) do{}while(0)
# define su_ASSERT_RET_VOID(X) do{}while(0)
# define su_ASSERT_RET_VOID_LOC(X,Y,FNAME,LNNO) do{}while(0)
# define su_ASSERT_NYD_EXEC(X,Y) do{}while(0)
# define su_ASSERT_NYD_EXEC_LOC(X,FNAME,LNNO) do{}while(0)
# define su_ASSERT_NYD(X) do{}while(0)
# define su_ASSERT_NYD_LOC(X,FNAME,LNNO) do{}while(0)
#else
# define su_ASSERT_INJ(X) X
# define su_ASSERT_INJOR(X,Y) X
# define su_ASSERT_NB(X) \
   su_R(void,((X) ? su_TRU1 \
      : su_assert(su_STRING(X), __FILE__, __LINE__, su_FUN, su_TRU1), su_FAL0))
# define su_ASSERT(X) su_ASSERT_LOC(X, __FILE__, __LINE__)
# define su_ASSERT_LOC(X,FNAME,LNNO) \
do if(!(X))\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, su_TRU1);\
while(0)
# define su_ASSERT_EXEC(X,S) su_ASSERT_EXEC_LOC(X, S, __FILE__, __LINE__)
# define su_ASSERT_EXEC_LOC(X,S,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, su_FAL0);\
   S;\
}while(0)
# define su_ASSERT_JUMP(X,L) su_ASSERT_JUMP_LOC(X, L, __FILE__, __LINE__)
# define su_ASSERT_JUMP_LOC(X,L,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, su_FAL0);\
   goto L;\
}while(0)
# define su_ASSERT_RET(X,Y) su_ASSERT_RET_LOC(X, Y, __FILE__, __LINE__)
# define su_ASSERT_RET_LOC(X,Y,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, su_FAL0);\
   return Y;\
}while(0)
# define su_ASSERT_RET_VOID(X) su_ASSERT_RET_VOID_LOC(X, __FILE__, __LINE__)
# define su_ASSERT_RET_VOID_LOC(X,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, su_FAL0);\
   return;\
}while(0)
# define su_ASSERT_NYD_EXEC(X,Y) \
   su_ASSERT_NYD_EXEC_LOC(X, Y, __FILE__, __LINE__)
# define su_ASSERT_NYD_EXEC_LOC(X,Y,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, su_FAL0);\
   Y; goto su_NYD_OU_LABEL;\
}while(0)
# define su_ASSERT_NYD(X) su_ASSERT_NYD_LOC(X, __FILE__, __LINE__)
# define su_ASSERT_NYD_LOC(X,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, su_FAL0);\
   goto su_NYD_OU_LABEL;\
}while(0)
#endif /* defined NDEBUG || defined DOXYGEN */
#define su_BITENUM_IS(X,Y) X
#define su_BITENUM_MASK(LO,HI) (((1u << ((HI) + 1)) - 1) & ~((1u << (LO)) - 1))
#define su_COMMA ,
/* Debug injections */
#if defined su_HAVE_DEBUG && !defined NDEBUG
# define su_DBG(X) X
# define su_NDBG(X)
# define su_DBGOR(X,Y) X
#else
# define su_DBG(X)
# define su_NDBG(X) X
# define su_DBGOR(X,Y) Y
#endif
/* Debug file location arguments.  (For an usage example see su/mem.h.) */
#if defined su_HAVE_DEVEL || defined su_HAVE_DEBUG
# define su_HAVE_DBG_LOC_ARGS
# define su_DBG_LOC_ARGS_FILE su__dbg_loc_args_file
# define su_DBG_LOC_ARGS_LINE su__dbg_loc_args_line
# define su_DBG_LOC_ARGS_DECL_SOLE \
   char const *su_DBG_LOC_ARGS_FILE, su_u32 su_DBG_LOC_ARGS_LINE
# define su_DBG_LOC_ARGS_DECL , su_DBG_LOC_ARGS_DECL_SOLE
# define su_DBG_LOC_ARGS_INJ_SOLE __FILE__, __LINE__
# define su_DBG_LOC_ARGS_INJ , su_DBG_LOC_ARGS_INJ_SOLE
# define su_DBG_LOC_ARGS_USE_SOLE su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE
# define su_DBG_LOC_ARGS_USE , su_DBG_LOC_ARGS_USE_SOLE
# define su_DBG_LOC_ARGS_ORUSE su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE
# define su_DBG_LOC_ARGS_UNUSED() \
do{\
   su_UNUSED(su_DBG_LOC_ARGS_FILE);\
   su_UNUSED(su_DBG_LOC_ARGS_LINE);\
}while(0)
#else
# define su_DBG_LOC_ARGS_FILE "unused"
# define su_DBG_LOC_ARGS_LINE 0
#
# define su_DBG_LOC_ARGS_DECL_SOLE
# define su_DBG_LOC_ARGS_DECL
# define su_DBG_LOC_ARGS_INJ_SOLE
# define su_DBG_LOC_ARGS_INJ
# define su_DBG_LOC_ARGS_USE_SOLE
# define su_DBG_LOC_ARGS_USE
# define su_DBG_LOC_ARGS_ORUSE su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE
# define su_DBG_LOC_ARGS_UNUSED() do{}while(0)
#endif /* su_HAVE_DEVEL || su_HAVE_DEBUG */
/* Development injections */
#if defined su_HAVE_DEVEL || defined su_HAVE_DEBUG /* Not: !defined NDEBUG) */\
      || defined DOXYGEN
# define su_DVL(X) X
# define su_NDVL(X)
# define su_DVLOR(X,Y) X
#else
# define su_DVL(X)
# define su_NDVL(X) X
# define su_DVLOR(X,Y) Y
#endif
#define su_EMPTY_FILE() typedef int su_CONCAT(su_notempty_shall_b_, su_FILE);
/* C field init */
#if (su_C_LANG && defined __STDC_VERSION__ && \
      __STDC_VERSION__ +0 >= 199901L) || defined DOXYGEN
# define su_FIELD_INITN(N) .N =
# define su_FIELD_INITI(I) [I] =
#else
# define su_FIELD_INITN(N)
# define su_FIELD_INITI(I)
#endif
/* XXX offsetof+: clang,pcc check faked! */
#if su_CC_VCHECK_CLANG(5, 0) || su_CC_VCHECK_GCC(4, 1) ||\
      su_CC_VCHECK_PCC(1, 2) || defined DOXYGEN
# define su_FIELD_OFFSETOF(T,F) __builtin_offsetof(T, F)
#else
# define su_FIELD_OFFSETOF(T,F) \
      su_S(su_uz,su_S(su_up,&(su_R(T *,0x1)->F)) - 1)
#endif
#define su_FIELD_RANGEOF(T,S,E) \
      (su_FIELD_OFFSETOF(T, E) - su_FIELD_OFFSETOF(T, S))
#define su_FIELD_SIZEOF(T,F) sizeof(su_S(T *,su_NIL)->F)
/* Multithread injections */
#ifdef su_HAVE_MT
# define su_MT(X) X
#else
# define su_MT(X)
#endif
#define su_NELEM(A) (sizeof(A) / sizeof((A)[0]))
#define su_NYD_OU_LABEL su__nydou
#define su_P2UZ(X) su_S(su_uz,(su_up)(X))
#define su_PCMP(A,C,B) (su_R(su_up,A) C su_R(su_up,B))
/* SMP injections */
#ifdef su_HAVE_SMP
# define su_SMP(X) X
#else
# define su_SMP(X)
#endif
/* String stuff.
 * __STDC_VERSION__ is ISO C99, so also use __STDC__, which should work */
#if defined __STDC__ || defined __STDC_VERSION__ || su_C_LANG || \
      defined DOXYGEN
# define su_STRING(X) #X
# define su_XSTRING(X) su_STRING(X)
# define su_CONCAT(S1,S2) su__CONCAT_1(S1, S2)
# define su__CONCAT_1(S1,S2) S1 ## S2
#else
# define su_STRING(X) "X"
# define su_XSTRING STRING
# define su_CONCAT(S1,S2) S1/* will no work out though */S2
#endif
#if su_C_LANG || defined DOXYGEN
# define su_UCMP(T,A,C,B) (su_S(su_ ## u ## T,A) C su_S(su_ ## u ## T,B))
#else
# define su_UCMP(T,A,C,B) \
      (su_S(su_NSPC(su) u ## T,A) C su_S(su_NSPC(su) u ## T,B))
#endif
#define su_UNCONST(T,P) su_R(T,su_R(su_up,su_S(void const*,P)))
#define su_UNVOLATILE(T,P) su_R(T,su_R(su_up,su_S(void volatile*,P)))
#define su_UNALIGN(T,P) su_R(T,su_R(su_up,P))
#define su_UNXXX(T,C,P) su_R(T,su_R(su_up,su_S(C,P)))
/* Avoid "may be used uninitialized" warnings */
#if (defined NDEBUG && !(defined su_HAVE_DEBUG || defined su_HAVE_DEVEL)) || \
      defined DOYGEN
# define su_UNINIT(N,V) su_S(void,0)
# define su_UNINIT_DECL(V)
#else
# define su_UNINIT(N,V) N = V
# define su_UNINIT_DECL(V) = V
#endif
#define su_UNUSED(X) ((void)(X))
#if (su_C_LANG && defined __STDC_VERSION__ && \
      __STDC_VERSION__ +0 >= 199901L) || defined DOXYGEN
# define su_VFIELD_SIZE(X)
# define su_VSTRUCT_SIZEOF(T,F) sizeof(T)
#else
# define su_VFIELD_SIZE(X) \
      ((X) == 0 ? sizeof(su_uz) \
      : (su_S(su_sz,X) < 0 ? sizeof(su_uz) - su_ABS(X) : su_S(su_uz,X)))
# define su_VSTRUCT_SIZEOF(T,F) (sizeof(T) - su_FIELD_SIZEOF(T, F))
#endif
/* SUPPORT MACROS+ }}} */
/* We are ready to start using our own style */
#ifndef su_CC_SIZE_TYPE
# include <sys/types.h> /* TODO create config time script, */
#endif
#include <inttypes.h> /* TODO query infos and drop */
#include <limits.h> /* TODO those includes! */
#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN
/* POD TYPE SUPPORT TODO maybe configure-time, from a su/config.h?! {{{ */
/* TODO Note: the PRI* series will go away once we have FormatCtx! */
/* First some shorter aliases for "normal" integers */
typedef unsigned long su_ul;
typedef unsigned int su_ui;
typedef unsigned short su_us;
typedef unsigned char su_uc;
typedef signed long su_sl;
typedef signed int su_si;
typedef signed short su_ss;
typedef signed char su_sc;
#if defined UINT8_MAX || defined DOXYGEN
# define su_U8_MAX UINT8_MAX
# define su_S8_MIN INT8_MIN
# define su_S8_MAX INT8_MAX
typedef uint8_t su_u8;
typedef int8_t su_s8;
#elif UCHAR_MAX != 255
# error UCHAR_MAX must be 255
#else
# define su_U8_MAX UCHAR_MAX
# define su_S8_MIN CHAR_MIN
# define su_S8_MAX CHAR_MAX
typedef unsigned char su_u8;
typedef signed char su_s8;
#endif
#if !defined PRIu8 || !defined PRId8
# undef PRIu8
# undef PRId8
# define PRIu8 "hhu"
# define PRId8 "hhd"
#endif
#if defined UINT16_MAX || defined DOXYGEN
# define su_U16_MAX UINT16_MAX
# define su_S16_MIN INT16_MIN
# define su_S16_MAX INT16_MAX
typedef uint16_t su_u16;
typedef int16_t su_s16;
#elif USHRT_MAX != 0xFFFFu
# error USHRT_MAX must be 0xFFFF
#else
# define su_U16_MAX USHRT_MAX
# define su_S16_MIN SHRT_MIN
# define su_S16_MAX SHRT_MAX
typedef unsigned short su_u16;
typedef signed short su_s16;
#endif
#if !defined PRIu16 || !defined PRId16
# undef PRIu16
# undef PRId16
# if su_U16_MAX == UINT_MAX
#  define PRIu16 "u"
#  define PRId16 "d"
# else
#  define PRIu16 "hu"
#  define PRId16 "hd"
# endif
#endif
#if defined UINT32_MAX || defined DOXYGEN
# define su_U32_MAX UINT32_MAX
# define su_S32_MIN INT32_MIN
# define su_S32_MAX INT32_MAX
typedef uint32_t su_u32;
typedef int32_t su_s32;
#elif ULONG_MAX == 0xFFFFFFFFu
# define su_U32_MAX ULONG_MAX
# define su_S32_MIN LONG_MIN
# define su_S32_MAX LONG_MAX
typedef unsigned long int su_u32;
typedef signed long int su_s32;
#elif UINT_MAX != 0xFFFFFFFFu
# error UINT_MAX must be 0xFFFFFFFF
#else
# define su_U32_MAX UINT_MAX
# define su_S32_MIN INT_MIN
# define su_S32_MAX INT_MAX
typedef unsigned int su_u32;
typedef signed int su_s32;
#endif
#if !defined PRIu32 || !defined PRId32
# undef PRIu32
# undef PRId32
# if su_U32_MAX == ULONG_MAX
#  define PRIu32 "lu"
#  define PRId32 "ld"
# else
#  define PRIu32 "u"
#  define PRId32 "d"
# endif
#endif
#if defined UINT64_MAX || defined DOXYGEN
# define su_U64_MAX UINT64_MAX
# define su_S64_MIN INT64_MIN
# define su_S64_MAX INT64_MAX
# define su_S64_C(C) INT64_C(C)
# define su_U64_C(C) UINT64_C(C)
typedef uint64_t su_u64;
typedef int64_t su_s64;
#elif ULONG_MAX <= 0xFFFFFFFFu
# if !defined ULLONG_MAX
#  error We need a 64 bit integer
# else
#  define su_U64_MAX ULLONG_MAX
#  define su_S64_MIN LLONG_MIN
#  define su_S64_MAX LLONG_MAX
#  define su_S64_C(C) su_CONCAT(C, ll)
#  define su_U64_C(C) su_CONCAT(C, ull)
su_CC_EXTEN typedef unsigned long long su_u64;
su_CC_EXTEN typedef signed long long su_s64;
# endif
#else
# define su_U64_MAX ULONG_MAX
# define su_S64_MIN LONG_MIN
# define su_S64_MAX LONG_MAX
# define su_S64_C(C) su_CONCAT(C, l)
# define su_U64_C(C) su_CONCAT(C, ul)
typedef unsigned long su_u64;
typedef signed long su_s64;
#endif
#if !defined PRIu64 || !defined PRId64 || !defined PRIX64 || !defined PRIo64
# undef PRIu64
# undef PRId64
# undef PRIX64
# undef PRIo64
# if defined ULLONG_MAX && su_U64_MAX == ULLONG_MAX
#  define PRIu64 "llu"
#  define PRId64 "lld"
#  define PRIX64 "llX"
#  define PRIo64 "llo"
# else
#  define PRIu64 "lu"
#  define PRId64 "ld"
#  define PRIX64 "lX"
#  define PRIo64 "lo"
# endif
#endif
/* (So that we can use UCMP() for size_t comparison, too) */
#ifdef su_CC_SIZE_TYPE
typedef su_CC_SIZE_TYPE su_uz;
#else
typedef size_t su_uz;
#endif
#undef PRIuZ
#undef PRIdZ
#if (defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L) ||\
      defined DOXYGEN
# define PRIuZ "zu"
# define PRIdZ "zd"
# define su_UZ_MAX SIZE_MAX
#elif defined SIZE_MAX
   /* UnixWare has size_t as unsigned as required but uses a signed limit
    * constant (which is thus false!) */
# if SIZE_MAX == su_U64_MAX || SIZE_MAX == su_S64_MAX
#  define PRIuZ PRIu64
#  define PRIdZ PRId64
MCTA(sizeof(size_t) == sizeof(u64),
   "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
# elif SIZE_MAX == su_U32_MAX || SIZE_MAX == su_S32_MAX
#  define PRIuZ PRIu32
#  define PRIdZ PRId32
MCTA(sizeof(size_t) == sizeof(u32),
   "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
# else
#  error SIZE_MAX is neither su_U64_MAX nor su_U32_MAX (please report this)
# endif
# define su_UZ_MAX SIZE_MAX
#endif
#if !defined PRIuZ && !defined DOXYGEN
# define PRIuZ "lu"
# define PRIdZ "ld"
MCTA(sizeof(size_t) == sizeof(unsigned long),
   "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
#endif
/* The signed equivalence is not really compliant to the standard */
#if su_UZ_MAX == su_U32_MAX || su_UZ_MAX == su_S32_MAX || defined DOXYGEN
# define su_SZ_MIN su_S32_MIN
# define su_SZ_MAX su_S32_MAX
# define su_UZ_BITS 32u
# define su_64(X)
# define su_32(X) X
# define su_6432(X,Y) Y
typedef su_s32 su_sz;
#elif su_UZ_MAX == su_U64_MAX
# define su_SZ_MIN su_S64_MIN
# define su_SZ_MAX su_S64_MAX
# define su_UZ_BITS 64u
# define su_64(X) X
# define su_32(X)
# define su_6432(X,Y) X
typedef su_s64 su_sz;
#else
# error I cannot handle this maximum value of size_t
#endif
MCTA(sizeof(su_uz) == sizeof(void*),
   "SU cannot handle sizeof(su_uz) != sizeof(void*)")
/* Regardless of P2UZ provide this one; only use it rarely */
#if defined UINTPTR_MAX || defined DOXYGEN
typedef uintptr_t su_up;
typedef intptr_t su_sp;
#else
# ifdef SIZE_MAX
typedef su_uz su_up;
typedef su_sz su_sp;
# else
typedef su_ul su_up;
typedef su_sl su_sp;
# endif
#endif
enum{
   su_FAL0,
   su_TRU1,
   su_TRU2,
   su_TRUM1 = -1
};
typedef su_s8 su_boole;
/* POD TYPE SUPPORT }}} */
/* BASIC TYPE TRAITS {{{ */
struct su_toolbox;
/* plus PTF typedefs */
typedef void *(*su_new_fun)(u32 estate);
typedef void *(*su_clone_fun)(void const *t, u32 estate);
typedef void (*su_delete_fun)(void *self);
typedef void *(*su_assign_fun)(void *self, void const *t, u32 estate);
typedef su_sz (*su_compare_fun)(void const *a, void const *b);
typedef su_uz (*su_hash_fun)(void const *self);
/* Needs to be binary compatible with \c{su::{toolbox,type_toolbox<T>}}! */
struct su_toolbox{
   su_clone_fun tb_clone;
   su_delete_fun tb_delete;
   su_assign_fun tb_assign;
   su_compare_fun tb_compare;
   su_hash_fun tb_hash;
};
/* Use C-style casts, not and ever su_R()! */
#define su_TOOLBOX_I9R(CLONE,DELETE,ASSIGN,COMPARE,HASH) \
{\
   su_FIELD_INITN(tb_clone) (su_clone_fun)(CLONE),\
   su_FIELD_INITN(tb_delete) (su_delete_fun)(DELETE),\
   su_FIELD_INITN(tb_assign) (su_assign_fun)(ASSIGN),\
   su_FIELD_INITN(tb_compare) (su_compare_fun)(COMPARE),\
   su_FIELD_INITN(tb_hash) (su_hash_fun)(HASH)\
}
/* BASIC TYPE TRAITS }}} */
/* BASIC C INTERFACE (SYMBOLS) {{{ */
#define su_BOM 0xFEFFu
/* su_state.. machinery: first byte: global log instance.. */
enum su_log_level{
   su_LOG_EMERG,
   su_LOG_ALERT,
   su_LOG_CRIT,
   su_LOG_ERR,
   su_LOG_WARN,
   su_LOG_NOTICE,
   su_LOG_INFO,
   su_LOG_DEBUG
};
enum{
   su__LOG_MAX = su_LOG_DEBUG,
   su__LOG_SHIFT = 8,
   su__LOG_MASK = (1u << su__LOG_SHIFT) - 1
};
MCTA(1u<<su__LOG_SHIFT > su__LOG_MAX, "Bit ranges may not overlap")
enum su_log_flags{
   su_LOG_F_CORE = 1u<<(su__LOG_SHIFT+0)
};
enum su_state_log_flags{
   su_STATE_LOG_SHOW_LEVEL = 1u<<4,
   su_STATE_LOG_SHOW_PID = 1u<<5
};
/* ..second byte: hardening errors.. */
enum su_state_err_type{
   su_STATE_ERR_NOMEM = 1u<<8,
   su_STATE_ERR_OVERFLOW = 1u<<9
};
enum su_state_err_flags{
   su_STATE_ERR_TYPE_MASK = su_STATE_ERR_NOMEM | su_STATE_ERR_OVERFLOW,
   su_STATE_ERR_PASS = su_STATE_ERR_TYPE_MASK,
   su_STATE_ERR_NOPASS = 1u<<12,
   su_STATE_ERR_NOERRNO = 1u<<13,
   su_STATE_ERR_NIL_IS_VALID_OBJECT = 1u<<14,
   su_STATE_ERR_NILISVALO = su_STATE_ERR_NIL_IS_VALID_OBJECT,
   su_STATE_ERR_MASK = su_STATE_ERR_TYPE_MASK |
         su_STATE_ERR_PASS | su_STATE_ERR_NOPASS | su_STATE_ERR_NOERRNO |
         su_STATE_ERR_NIL_IS_VALID_OBJECT
};
/* ..third byte: misc flags */
enum su_state_flags{
   su_STATE_NONE,
   su_STATE_DEBUG = 1u<<16,
   su_STATE_VERBOSE = 1u<<17,
   su_STATE_REPRODUCIBLE = 1u<<18
};
enum su__state_flags{
   /* enum su_log_level is first "member" */
   su__STATE_LOG_MASK = 0x0Fu,
   su__STATE_D_V = su_STATE_DEBUG | su_STATE_VERBOSE,
   /* What is not allowed in the global state machine */
   su__STATE_GLOBAL_MASK = 0x00FFFFFFu & ~(su__STATE_LOG_MASK |
         (su_STATE_ERR_MASK & ~su_STATE_ERR_TYPE_MASK))
};
MCTA(S(uz,su_LOG_DEBUG) <= S(uz,su__STATE_LOG_MASK),
   "Bit ranges may not overlap")
MCTA((S(uz,su_STATE_ERR_MASK) & ~0xFF00) == 0, "Bits excess documented bounds")
#ifdef su_HAVE_MT
enum su__glock_type{
   su__GLOCK_STATE,
   su__GLOCK_LOG,
   su__GLOCK_MAX = su__GLOCK_LOG
};
#endif
enum su_err_number{
#ifdef DOXYGEN
   su_ERR_NONE,
   su_ERR_NOTOBACCO
#else
   su__ERR_NUMBER_ENUM_C
# undef su__ERR_NUMBER_ENUM_C
#endif
};
union su__bom_union{
   char bu_buf[2];
   u16 bu_val;
};
/* Known endianness bom versions, see su_bom_little, su_bom_big */
EXPORT_DATA union su__bom_union const su__bom_little;
EXPORT_DATA union su__bom_union const su__bom_big;
/* (Not yet) Internal enum su_state_* bit carrier */
EXPORT_DATA uz su__state;
EXPORT_DATA u16 const su_bom;
#define su_bom_little su__bom_little.bu_val
#define su_bom_big su__bom_big.bu_val
#if defined su_CC_BOM || defined DOXYGEN
# define su_BOM_IS_BIG() (su_CC_BOM == su_CC_BOM_BIG)
# define su_BOM_IS_LITTLE() (su_CC_BOM == su_CC_BOM_LITTLE)
#else
# define su_BOM_IS_BIG() (su_bom == su_bom_big)
# define su_BOM_IS_LITTLE() (su_bom == su_bom_little)
#endif
EXPORT_DATA char const su_empty[1];
EXPORT_DATA char const su_reproducible_build[];
EXPORT_DATA char const *su_program;
/**/
#ifdef su_HAVE_MT
EXPORT void su__glock(enum su__glock_type gt);
EXPORT void su__gunlock(enum su__glock_type gt);
#endif
INLINE u32 su_state_get(void){
   return (su__state & su__STATE_GLOBAL_MASK);
}
INLINE boole su_state_has(uz flags){
   flags &= su__STATE_GLOBAL_MASK;
   return ((su__state & flags) == flags);
}
INLINE void su_state_set(uz flags){
   MT( su__glock(su__GLOCK_STATE); )
   su__state |= flags & su__STATE_GLOBAL_MASK;
   MT( su__gunlock(su__GLOCK_STATE); )
}
INLINE void su_state_clear(uz flags){
   MT( su__glock(su__GLOCK_STATE); )
   su__state &= ~(flags & su__STATE_GLOBAL_MASK);
   MT( su__gunlock(su__GLOCK_STATE); )
}
EXPORT s32 su_state_err(enum su_state_err_type err, uz state,
      char const *msg_or_nil);
EXPORT s32 su_err_no(void);
EXPORT s32 su_err_set_no(s32 eno);
EXPORT char const *su_err_doc(s32 eno);
EXPORT char const *su_err_name(s32 eno);
EXPORT s32 su_err_from_name(char const *name);
EXPORT s32 su_err_no_via_errno(void);
INLINE enum su_log_level su_log_get_level(void){
   return S(enum su_log_level,su__state & su__STATE_LOG_MASK);
}
INLINE void su_log_set_level(enum su_log_level nlvl){
   uz lvl;
   /*MT( su__glock(su__GLOCK_STATE); )*/
   lvl = S(uz,nlvl) & su__STATE_LOG_MASK;
   su__state = (su__state & su__STATE_GLOBAL_MASK) | lvl;
   /*MT( su__gunlock(su__GLOCK_STATE); )*/
}
INLINE boole su_log_would_write(enum su_log_level lvl){
   return ((S(u32,lvl) & su__LOG_MASK) <= (su__state & su__STATE_LOG_MASK) ||
      (su__state & su__STATE_D_V));
}
EXPORT void su_log_write(BITENUM_IS(u32,su_log_level) lvl,
      char const *fmt, ...);
EXPORT void su_log_vwrite(BITENUM_IS(u32,su_log_level) lvl,
      char const *fmt, void *vp);
EXPORT void su_perr(char const *msg, s32 eno_or_0);
INLINE void su_log_lock(void){
   MT( su__glock(su__GLOCK_LOG); )
}
INLINE void su_log_unlock(void){
   MT( su__gunlock(su__GLOCK_LOG); )
}
#if !defined su_ASSERT_EXPAND_NOTHING || defined DOXYGEN
EXPORT void su_assert(char const *expr, char const *file, u32 line,
      char const *fun, boole crash);
#else
# define su_assert(EXPR,FILE,LINE,FUN,CRASH)
#endif
#if DVLOR(1, 0)
EXPORT void su_nyd_set_disabled(boole disabled);
EXPORT void su_nyd_reset_level(u32 nlvl);
EXPORT void su_nyd_chirp(u8 act, char const *file, u32 line, char const *fun);
EXPORT void su_nyd_dump(void (*ptf)(up cookie, char const *buf, uz blen),
      up cookie);
#endif
/* BASIC C INTERFACE (SYMBOLS) }}} */
C_DECL_END
#include <su/code-ou.h>
/* MORE DOXYGEN TOP GROUPS {{{ */
/* MORE DOXYGEN TOP GROUPS }}} */
#endif /* !su_CODE_H */
/* s-it-mode */
