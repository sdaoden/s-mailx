/*@ Code of the basic infrastructure (POD types, macros etc.) and functions.
 *@ And main documentation entry point, as below.
 *@ - Reacts upon su_HAVE_DEBUG, su_HAVE_DEVEL, and NDEBUG.
 *@   The latter is a precondition for su_HAVE_INLINE.
 *@ - Some macros require su_FILE to be defined to a literal.
 *@ - Define su_MASTER to inject what is to be injected once; for example,
 *@   it enables su_M*CTA() compile time assertions.
 *
 * Copyright (c) 2001 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

/*!
 * \mainpage SU --- Steffen's Utilities
 *
 * Afters years of finding myself too busy to port my old C++ library of which
 * i am so prowd to the C language, and because of the ever increasing
 * necessity to have a foundation of things i like using nonetheless,
 * i finally have started creating a minimal set of tools instead.
 *
 * Some introductional notes:
 *
 * \list{\li{
 * The basic infrastructure of \SU is provided by the file \r{su/code.h}.
 * Because all other \SU headers include it (thus), having it available is
 * almost always implicit.
 * It should be noted, however, that the \r{CORE} reacts upon a few
 * preprocessor switches, as documented there.
 * }\li{
 * Datatype overflow errors and out-of-memory situations are usually detected
 * and result in abortions (via \r{su_LOG_EMERG} logs).
 * Alternatively errors are reported to callers.
 * The actual mode of operation is configurable via \r{su_state_set()} and
 * \r{su_state_clear()}, and often the default can also be changed by-call
 * or by-object.
 *
 * C++ object creation failures via \c{su_MEM_NEW()} etc. will however always
 * cause program abortion due to standard imposed execution flow.
 * This can be worked around by using \c{su_MEM_NEW_HEAP()} as appropriate.
 * }\li{
 * Most collection and string object types work on 32-bit (or even 31-bit)
 * lengths a.k.a. counts a.k.a. sizes.
 * For simplicity of use, and because datatype overflow is a handled case, the
 * user interface very often uses \r{su_uz} (i.e., \c{size_t}).
 * Other behaviour is explicitly declared with a "big" prefix, as in
 * "biglist", but none such object does yet exist.
 * }}
 */

/*!
 * \file
 * \ingroup CORE
 * \brief \r{CORE}
 */

/* CONFIG {{{ *//*!
 * \defgroup CONFIG SU configuration
 * \ingroup CORE
 * \brief Overall \SU configuration (\r{su/code.h})
 *
 * It reflects the chosen configuration and the build time environment.
 * @{
 */

#ifdef DOXYGEN
   /*! Whether the \SU namespace exists.
    * If not, facilities exist in the global namespace. */
# define su_HAVE_NSPC

# define su_HAVE_DEBUG        /*!< \_ */
# define su_HAVE_DEVEL        /*!< \_ */
# define su_HAVE_MEM_BAG_AUTO /*!< \_ */
# define su_HAVE_MEM_BAG_LOFI /*!< \_ */
# define su_HAVE_MEM_CANARIES_DISABLE  /*!< \_ */
#endif

/*! @} *//* CONFIG }}} */
/*!
 * \defgroup CORE Basic infrastructure
 * \brief Macros, POD types, and basic interfaces (\r{su/code.h})
 *
 * The basic infrastructure:
 *
 * \list{\li{
 * Reacts upon \vr{su_HAVE_DEBUG}, \vr{su_HAVE_DEVEL}, and \vr{NDEBUG}.
 * }\li{
 * The latter is a precondition for \vr{su_HAVE_INLINE}.
 * }\li{
 * Some macros require \vr{su_FILE} to be defined to a literal.
 * }\li{
 * Define \vr{su_MASTER} to inject what is to be injected once; for example,
 * it enables \c{su_M*CTA()} compile time assertions.
 * }}
 * @{
 */

/* OS {{{ */

#define su_OS_CYGWIN 0     /*!< \_ */
#define su_OS_DARWIN 0     /*!< \_ */
#define su_OS_DRAGONFLY 0  /*!< \_ */
#define su_OS_EMX 0        /*!< \_ */
#define su_OS_FREEBSD 0    /*!< \_ */
#define su_OS_LINUX 0      /*!< \_ */
#define su_OS_MINIX 0      /*!< \_ */
#define su_OS_MSDOS 0      /*!< \_ */
#define su_OS_NETBSD 0     /*!< \_ */
#define su_OS_OPENBSD 0    /*!< \_ */
#define su_OS_SOLARIS 0    /*!< \_ */
#define su_OS_SUNOS 0      /*!< \_ */
#define su_OS_WIN32 0      /*!< \_ */
#define su_OS_WIN64 0      /*!< \_ */

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
# define su_C_LANG 1       /*!< \_ */
# define su_C_DECL_BEGIN   /*!< \_ */
# define su_C_DECL_END     /*!< \_ */

   /* Casts */
# define su_S(T,I) ((T)(I))   /*!< \_ */
# define su_R(T,I) ((T)(I))   /*!< \_ */
# define su_C(T,I) ((T)(I))   /*!< \_ */

# define su_NIL ((void*)0)    /*!< \_ */
#else
# define su_C_LANG 0
# define su_C_DECL_BEGIN extern "C" {
# define su_C_DECL_END }
# define su_HAVE_NSPC 0
# if su_HAVE_NSPC
#  define su_NSPC_BEGIN(X) namespace X {
#  define su_NSPC_END(X) }
#  define su_NSPC_USE(X) using namespace X;
#  define su_NSPC(X) X ## ::
# else
#  define su_NSPC_BEGIN(X) /**/
#  define su_NSPC_END(X) /**/
#  define su_NSPC_USE(X) /**/
#  define su_NSPC(X) /**/ ::
# endif

   /* Disable copy-construction and assigment of class */
# define su_CLASS_NO_COPY(C) private:C(C const &);C &operator=(C const &);

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

/*! The \r{su_state_err()} mechanism can be configured to not cause
 * abortion in case of datatype overflow and out-of-memory situations.
 * Most functions return error conditions to pass them to their caller,
 * but this is impossible for, e.g., C++ copy-constructors and assignment
 * operators.
 * And \SU does not use exceptions.
 * So if those errors could occur and thus be hidden, the prototype is marked
 * with this "keyword" so that callers can decide whether they want to take
 * alternative routes to come to the desired result or not. */
#define su_SHADOW

/* "su_EXPORT myfun()", "class su_EXPORT myclass" */
#if su_OS_WIN32 || su_OS_WIN64
# define su_EXPORT __declspec((dllexport))
# define su_EXPORT_DATA __declspec((dllexport))
# define su_IMPORT __declspec((dllimport))
# define su_IMPORT_DATA __declspec((dllimport))
#else
# define su_EXPORT /*extern*/    /*!< \_ */
# define su_EXPORT_DATA extern   /*!< \_ */
# define su_IMPORT /*extern*/    /*!< \_ */
# define su_IMPORT_DATA extern   /*!< \_ */
#endif

/* Compile-Time-Assert
 * Problem is that some compilers warn on unused local typedefs, so add
 * a special local CTA to overcome this */
#if !su_C_LANG && __cplusplus +0 >= 201103L
# define su_CTA(T,M) static_assert(T, M)
# define su_LCTA(T,M) static_assert(T, M)
#elif defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112L
# define su_CTA(T,M) _Static_assert(T, M)    /*!< \_ */
# define su_LCTA(T,M) _Static_assert(T, M)   /*!< \_ */
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

#define su_CTAV(T) su_CTA(T, "Unexpected value of constant")   /*!< \_ */
#define su_LCTAV(T) su_LCTA(T, "Unexpected value of constant") /*!< \_ */
#ifdef su_MASTER
# define su_MCTA(T,M) su_CTA(T, M);
#else
# define su_MCTA(T,M)
#endif

/* LANG }}} */
/* CC {{{ */

#define su_CC_CLANG 0               /*!< \_ */
#define su_CC_VCHECK_CLANG(X,Y) 0   /*!< \_ */
#define su_CC_GCC 0                 /*!< \_ */
#define su_CC_VCHECK_GCC(X,Y) 0     /*!< \_ */
#define su_CC_PCC 0                 /*!< \_ */
#define su_CC_VCHECK_PCC(X,Y) 0     /*!< \_ */
#define su_CC_SUNPROC 0             /*!< \_ */
#define su_CC_VCHECK_SUNPROC(X,Y) 0 /*!< \_ */
#define su_CC_TINYC 0               /*!< \_ */
#define su_CC_VCHECK_TINYC(X,Y) 0   /*!< \_ */

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

#elif defined __GNUC__ /* __clang__ */
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

#elif defined __PCC__ /* __GNUC__ */
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

#elif !defined su_CC_IGNORE_UNKNOWN
# error SU: This compiler is not yet supported.
# error SU: To continue with your CFLAGS etc., define su_CC_IGNORE_UNKNOWN.
# error SU: It may be necessary to define su_CC_PACKED to a statement that
# error SU: enables structure packing; it may not be a #pragma, but a _Pragma
#endif

#ifndef su_CC_EXTEN
# define su_CC_EXTEN /*!< \_ */
#endif
#ifndef su_CC_PACKED
   /*! \_ */
# define su_CC_PACKED TODO: PACKED attribute not supported for this compiler
#endif
#if defined su_CC_BOM || defined DOXYGEN
# ifdef DOXYGEN
   /*! If the CC offers \r{su_BOM} classification macros, defined to either
    * \r{su_CC_BOM_LITTLE} or \r{su_CC_BOM_BIG}, otherwise not defined. */
#  define su_CC_BOM
# endif
# define su_CC_BOM_LITTLE 1234   /*!< Only if there is \r{su_CC_BOM}. */
# define su_CC_BOM_BIG 4321      /*!< Only if there is \r{su_CC_BOM}. */
#endif
#if !defined su_CC_UZ_TYPE && defined __SIZE_TYPE__
# define su_CC_UZ_TYPE __SIZE_TYPE__
#endif

/* Suppress some technical warnings via #pragma's unless developing.
 * XXX Wild guesses: clang(1) 1.7 and (OpenBSD) gcc(1) 4.2.1 do not work */
#ifndef su_HAVE_DEVEL
# if su_CC_VCHECK_CLANG(3, 4)
/*#  pragma clang diagnostic ignored "-Wformat"*/
#  pragma clang diagnostic ignored "-Wunused-result"
# elif su_CC_VCHECK_GCC(4, 7) || su_CC_PCC || su_CC_TINYC
/*#  pragma GCC diagnostic ignored "-Wformat"*/
#  pragma GCC diagnostic ignored "-Wunused-result"
# endif
#endif

/* Function name */
#if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L
# define su_FUN __func__   /*!< "Not a literal". */
#elif su_CC_CLANG || su_CC_VCHECK_GCC(3, 4) || su_CC_PCC || su_CC_TINYC
# define su_FUN __extension__ __FUNCTION__
#else
# define su_FUN su_empty /* Something that is not a literal */
#endif

/* inline keyword */
#define su_HAVE_INLINE
#if su_C_LANG
# if su_CC_CLANG || su_CC_GCC || su_CC_PCC || defined DOXYGEN
#  if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L
#   define su_INLINE inline         /*!< \_ */
#   define su_SINLINE static inline /*!< \_ */
#  else
#   define su_INLINE __inline
#   define su_SINLINE static __inline
#  endif
# else
#  define su_INLINE static /* TODO __attribute__((unused)) alikes? */
#  define su_SINLINE static /* TODO __attribute__((unused)) alikes? */
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
# define su_LIKELY(X) ((X) != 0)    /*!< \_ */
# define su_UNLIKELY(X) ((X) != 0)  /*!< \_ */
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
/*! \_ */
#define su_ABS(A) ((A) < 0 ? -(A) : (A))
/*! \_ */
#define su_CLIP(X,A,B) (((X) <= (A)) ? (A) : (((X) >= (B)) ? (B) : (X)))
/*! \_ */
#define su_MAX(A,B) ((A) < (B) ? (B) : (A))
/*! \_ */
#define su_MIN(A,B) ((A) < (B) ? (A) : (B))
/*! \_ */
#define su_IS_POW2(X) ((((X) - 1) & (X)) == 0)

/* Alignment.  Note: su_uz POW2 asserted in POD section below! */
#if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112L
# include <stdalign.h>
# define su_ALIGNOF(X) _Alignof(X)
#else
   /*! \c{_Alignof()} if available, something hacky otherwise */
# define su_ALIGNOF(X) ((sizeof(X) + sizeof(su_uz)) & ~(sizeof(su_uz) - 1))
#endif

/* Roundup/align an integer;  Note: POW2 asserted in POD section below! */
/*! Overalign an integer value to a size that cannot cause just any problem
 * for anything which does not use special alignment directives. */
#define su_Z_ALIGN(X) ((su_S(su_uz,X) + 2*su__ZAL_L) & ~((2*su__ZAL_L) - 1))

/*! Smaller than \r{su_Z_ALIGN()}, but sufficient for basic plain-old-data. */
#define su_Z_ALIGN_SMALL(X) ((su_S(su_uz,X) + su__ZAL_L) & ~(su__ZAL_L - 1))

/*! \r{su_Z_ALIGN()}, but only for pointers and \r{su_uz}. */
#define su_Z_ALIGN_PZ(X) ((su_S(su_uz,X) + su__ZAL_S) & ~(su__ZAL_S - 1))

# define su__ZAL_S su_MAX(su_ALIGNOF(su_uz), su_ALIGNOF(void*))
# define su__ZAL_L su_MAX(su__ZAL_S, su_ALIGNOF(su_u64))/* XXX FP,128bit */

/* Variants of ASSERT */
#if defined NDEBUG || !defined su_HAVE_DEBUG || defined DOXYGEN
# define su_ASSERT_INJ(X)                                /*!< Injection! */
# define su_ASSERT(X) do{}while(0)                       /*!< \_ */
# define su_ASSERT_LOC(X,FNAME,LNNO) do{}while(0)        /*!< \_ */
# define su_ASSERT_EXEC(X,S) do{}while(0)                /*!< \_ */
# define su_ASSERT_EXEC_LOC(X,S,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_JUMP(X,L) do{}while(0)                /*!< \_ */
# define su_ASSERT_JUMP_LOC(X,L,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_RET(X,Y) do{}while(0)                 /*!< \_ */
# define su_ASSERT_RET_LOC(X,Y,FNAME,LNNO) do{}while(0)  /*!< \_ */
# define su_ASSERT_RET_VOID(X) do{}while(0)              /*!< \_ */
# define su_ASSERT_RET_VOID_LOC(X,Y,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_NYD_RET(X,Y) do{}while(0)             /*!< \_ */
# define su_ASSERT_NYD_RET_LOC(X,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_NYD_RET_VOID(X) do{}while(0)          /*!< \_ */
# define su_ASSERT_NYD_RET_VOID_LOC(X,FNAME,LNNO) do{}while(0) /*!< \_ */
#else
# define su_ASSERT_INJ(X) X
# define su_ASSERT(X) su_ASSERT_LOC(X, __FILE__, __LINE__)
# define su_ASSERT_LOC(X,FNAME,LNNO) \
do if(!(X))\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, TRU1);\
while(0)

# define su_ASSERT_EXEC(X,S) su_ASSERT_EXEC_LOC(X, S, __FILE__, __LINE__)
# define su_ASSERT_EXEC_LOC(X,S,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, FAL0);\
   S;\
}while(0)

# define su_ASSERT_JUMP(X,L) su_ASSERT_JUMP_LOC(X, L, __FILE__, __LINE__)
# define su_ASSERT_JUMP_LOC(X,L,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, FAL0);\
   goto L;\
}while(0)

# define su_ASSERT_RET(X,Y) su_ASSERT_RET_LOC(X, Y, __FILE__, __LINE__)
# define su_ASSERT_RET_LOC(X,Y,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, FAL0);\
   return Y;\
}while(0)

# define su_ASSERT_RET_VOID(X) su_ASSERT_RET_VOID_LOC(X, __FILE__, __LINE__)
# define su_ASSERT_RET_VOID_LOC(X,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, FAL0);\
   return;\
}while(0)

# define su_ASSERT_NYD_RET(X,Y) su_ASSERT_NYD_RET_LOC(X, Y, __FILE__, __LINE__)
# define su_ASSERT_NYD_RET_LOC(X,Y,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, FAL0);\
   Y; goto su_NYD_OU_LABEL;\
}while(0)

# define su_ASSERT_NYD_RET_VOID(X) \
   su_ASSERT_NYD_RET_VOID_LOC(X, __FILE__, __LINE__)
# define su_ASSERT_NYD_RET_VOID_LOC(X,FNAME,LNNO) \
do if(!(X)){\
   su_assert(su_STRING(X), FNAME, LNNO, su_FUN, FAL0);\
   goto su_NYD_OU_LABEL;\
}while(0)
#endif /* defined NDEBUG || !defined su_HAVE_DEBUG */

/*! Create a bit mask for the bit range LO..HI -- HI cannot use highest bit! */
#define su_BITENUM_MASK(LO,HI) (((1u << ((HI) + 1)) - 1) & ~((1u << (LO)) - 1))

/* For injection macros like su_DBG(), NDBG, DBGOR, 64, 32, 6432 */
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

#if defined su_HAVE_DEVEL || defined su_HAVE_DEBUG /* Not: !defined NDEBUG) */
# define su_DVL(X) X
# define su_NDVL(X)
# define su_DVLOR(X,Y) X
#else
# define su_DVL(X)
# define su_NDVL(X) X
# define su_DVLOR(X,Y) Y
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

/* To avoid files are overall empty */
#define su_EMPTY_FILE() typedef int su_CONCAT(su_notempty_shall_b_, su_FILE);

/* C field init */
#if su_C_LANG && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L
# define su_FIELD_INITN(N) .N =
# define su_FIELD_INITI(I) [I] =
#else
# define su_FIELD_INITN(N)
# define su_FIELD_INITI(I)
#endif

/* XXX offsetof+: clang,pcc check faked! */
#if su_CC_VCHECK_CLANG(5, 0) || su_CC_VCHECK_GCC(4, 1) ||\
      su_CC_VCHECK_PCC(1, 2) || defined DOXYGEN
   /*! The offset of field \a{F} in the type \a{T}. */
# define su_FIELD_OFFSETOF(T,F) __builtin_offsetof(T, F)
#else
# define su_FIELD_OFFSETOF(T,F) su_S(su_uz,su_S(su_up,&(su_S(T *,su_NIL)->F)))
#endif

/*! Distance in between the fields \a{S}tart and \a{E}end in type \a{T}. */
#define su_FIELD_RANGEOF(T,S,E) \
        (su_FIELD_OFFSETOF(T, E) - su_FIELD_OFFSETOF(T, S))

/*! sizeof() for member fields */
#define su_FIELD_SIZEOF(T,F) sizeof(su_S(T *,su_NIL)->F)

/*! Members in constant array */
#define su_NELEM(A) (sizeof(A) / sizeof((A)[0]))

/* NYD comes from code-{in,ou}.h (support function below).
 * Instrumented functions will always have one label for goto: purposes */
#define su_NYD_OU_LABEL su__nydou

/*! Pointer to size_t */
#define su_P2UZ(X) su_S(su_uz,su_S(su_up,X))

/*! Pointer comparison */
#define su_PCMP(A,C,B) (su_S(su_up,A) C su_S(su_up,B))

/* String stuff.
 * __STDC_VERSION__ is ISO C99, so also use __STDC__, which should work */
#if defined __STDC__ || defined __STDC_VERSION__ || su_C_LANG
# define su_STRING(X) #X
# define su_XSTRING(X) su_STRING(X)
# define su_CONCAT(S1,S2) su__CONCAT_1(S1, S2)
# define su__CONCAT_1(S1,S2) S1 ## S2
#else
# define su_STRING(X) "X"
# define su_XSTRING STRING
# define su_CONCAT(S1,S2) S1/* will no work out though */S2
#endif

/* Compare (maybe mixed-signed) integers cases to T bits, unsigned,
 * T is one of our homebrew integers, e.g., UCMP(32, su_ABS(n), >, wleft).
 * Note: does not sign-extend correctly, that is still up to the caller */
#if su_C_LANG
# define su_UCMP(T,A,C,B) (su_S(su_ ## u ## T,A) C su_S(su_ ## u ## T,B))
#else
# define su_UCMP(T,A,C,B) \
      (su_S(su_NSPC(su) u ## T,A) C su_S(su_NSPC(su) u ## T,B))
#endif

/* Casts-away (*NOT* cast-away) */
#if su_C_LANG
# define su_UNCONST(P) su_S(void*,su_S(su_up,su_S(void const*,P)))
# define su_UNVOLATILE(P) su_S(void*,su_S(su_up,su_S(void volatile*,P)))
  /* To avoid warnings with modern compilers for "char*i; *(s32_t*)i=;" */
# define su_UNALIGN(T,P) su_S(T,su_S(su_up,P))
# define su_UNXXX(T,C,P) su_S(T,su_S(su_up,su_S(C,P)))
#endif

/* Avoid "may be used uninitialized" warnings */
#if defined NDEBUG && !(defined su_HAVE_DEBUG || defined su_HAVE_DEVEL)
# define su_UNINIT(N,V) su_S(void,0)
# define su_UNINIT_DECL(V)
#else
# define su_UNINIT(N,V) N = V
# define su_UNINIT_DECL(V) = V
#endif

/*! Avoid "unused" warnings */
#define su_UNUSED(X) ((void)(X))

/* Variable-type size (with byte array at end) */
#if su_C_LANG && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L
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

/* POD TYPE SUPPORT TODO maybe configure-time, from a su/config.h?! {{{ */
/* TODO Note: the PRI* series will go away once we have FormatCtx! */
C_DECL_BEGIN

/* First some shorter aliases for "normal" integers */
typedef unsigned long su_ul;  /*!< \_ */
typedef unsigned int su_ui;   /*!< \_ */
typedef unsigned short su_us; /*!< \_ */
typedef unsigned char su_uc;  /*!< \_ */

typedef signed long su_sl;    /*!< \_ */
typedef signed int su_si;     /*!< \_ */
typedef signed short su_ss;   /*!< \_ */
typedef signed char su_sc;    /*!< \_ */

#if defined UINT8_MAX || defined DOXYGEN
# define su_U8_MAX UINT8_MAX  /*!< \_ */
# define su_S8_MIN INT8_MIN   /*!< \_ */
# define su_S8_MAX INT8_MAX   /*!< \_ */
typedef uint8_t su_u8;        /*!< \_ */
typedef int8_t su_s8;         /*!< \_ */
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
# define su_U16_MAX UINT16_MAX   /*!< \_ */
# define su_S16_MIN INT16_MIN    /*!< \_ */
# define su_S16_MAX INT16_MAX    /*!< \_ */
typedef uint16_t su_u16;         /*!< \_ */
typedef int16_t su_s16;          /*!< \_ */
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
# define su_U32_MAX UINT32_MAX   /*!< \_ */
# define su_S32_MIN INT32_MIN    /*!< \_ */
# define su_S32_MAX INT32_MAX    /*!< \_ */
typedef uint32_t su_u32;         /*!< \_ */
typedef int32_t su_s32;          /*!< \_ */
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
# define su_U64_MAX UINT64_MAX   /*!< \_ */
# define su_S64_MIN INT64_MIN    /*!< \_ */
# define su_S64_MAX INT64_MAX    /*!< \_ */
# define su_S64_C(C) INT64_C(C)  /*!< \_ */
# define su_U64_C(C) UINT64_C(C) /*!< \_ */
typedef uint64_t su_u64;         /*!< \_ */
typedef int64_t su_s64;          /*!< \_ */
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
typedef size_t su_uz;   /*!< \_ */
#endif

#undef PRIuZ
#undef PRIdZ
#if (defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901L) ||\
      defined DOXYGEN
# define PRIuZ "zu"
# define PRIdZ "zd"
# define su_UZ_MAX SIZE_MAX /*!< \_ */
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
# define su_SZ_MIN su_S32_MIN /*!< \_ */
# define su_SZ_MAX su_S32_MAX /*!< \_ */
# define su_UZ_BITS 32u       /*!< \_ */
# define su_64(X)             /*!< \_ */
# define su_32(X) X           /*!< \_ */
# define su_6432(X,Y) Y       /*!< \_ */
typedef su_s32 su_sz;         /*!< \_ */
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
typedef uintptr_t su_up;   /*!< \_ */
typedef intptr_t su_sp;    /*!< \_ */
#else
# ifdef SIZE_MAX
typedef su_uz su_up;
typedef su_sz su_sp;
# else
typedef su_ul su_up;
typedef su_sl su_sp;
# endif
#endif

/*! Values for #su_boole. */
enum {su_FAL0 /*!< \_ */, su_TRU1 /*!< \_ */, su_TRUM1 = -1 /*!< \_ */};
typedef su_s8 su_boole; /*!< The \SU boolean type (see \FAL0 etc.). */

C_DECL_END
#if !C_LANG || defined DOXYGEN_CXX
NSPC_BEGIN(su)

// All instanceless static encapsulators.
class min;
class max;

// Define in-namespace wrappers for C types.  code-in/ou do not define short
// names for POD when used from within C++
typedef su_ul ul; /*!< \_ */
typedef su_ui ui; /*!< \_ */
typedef su_us us; /*!< \_ */
typedef su_uc uc; /*!< \_ */

typedef su_sl sl; /*!< \_ */
typedef su_si si; /*!< \_ */
typedef su_ss ss; /*!< \_ */
typedef su_sc sc; /*!< \_ */

typedef su_u8 u8;    /*!< \_ */
typedef su_s8 s8;    /*!< \_ */
typedef su_u16 u16;  /*!< \_ */
typedef su_s16 s16;  /*!< \_ */
typedef su_u32 u32;  /*!< \_ */
typedef su_s32 s32;  /*!< \_ */
typedef su_u64 u64;  /*!< \_ */
typedef su_s64 s64;  /*!< \_ */

typedef su_uz uz; /*!< \_ */
typedef su_sz sz; /*!< \_ */

typedef su_up up; /*!< \_ */
typedef su_sp sp; /*!< \_ */

/*! Values for \r{su_boole}. */
enum {
   FAL0 = su_FAL0,   /*!< \_ */
   TRU1 = su_TRU1,   /*!< \_ */
   TRUM1 = su_TRUM1  /*!< All bits set. */
};
typedef su_boole boole; /*!< \_ */

/* Place the mentioned alignment CTAs */
MCTA(IS_POW2(sizeof(uz)), "Must be power of two")
MCTA(IS_POW2(su__ZAL_S), "Must be power of two")
MCTA(IS_POW2(su__ZAL_L), "Must be power of two")

/*! \_ */
class min{
public:
   static NSPC(su)s8 const s8 = su_S8_MIN;      /*!< \r{su_S8_MIN} */
   static NSPC(su)s16 const s16 = su_S16_MIN;   /*!< \r{su_S16_MIN} */
   static NSPC(su)s32 const s32 = su_S32_MIN;   /*!< \r{su_S32_MIN} */
   static NSPC(su)s64 const s64 = su_S64_MIN;   /*!< \r{su_S64_MIN} */
   static NSPC(su)sz const sz = su_SZ_MIN;      /*!< \r{su_SZ_MIN} */
};

/*! \_ */
class max{
public:
   static NSPC(su)s8 const s8 = su_S8_MAX;      /*!< \r{su_S8_MAX} */
   static NSPC(su)s16 const s16 = su_S16_MAX;   /*!< \r{su_S16_MAX} */
   static NSPC(su)s32 const s32 = su_S32_MAX;   /*!< \r{su_S32_MAX} */
   static NSPC(su)s64 const s64 = su_S64_MAX;   /*!< \r{su_S64_MAX} */
   static NSPC(su)sz const sz = su_SZ_MAX;      /*!< \r{su_SZ_MAX} */

   static NSPC(su)u8 const u8 = su_U8_MAX;      /*!< \r{su_U8_MAX} */
   static NSPC(su)u16 const u16 = su_U16_MAX;   /*!< \r{su_U16_MAX} */
   static NSPC(su)u32 const u32 = su_U32_MAX;   /*!< \r{su_U32_MAX} */
   static NSPC(su)u64 const u64 = su_U64_MAX;   /*!< \r{su_U64_MAX} */
   static NSPC(su)uz const uz = su_UZ_MAX;      /*!< \r{su_UZ_MAX} */
};

NSPC_END(su)
#endif /* !C_LANG */
/* POD TYPE SUPPORT }}} */
/* BASIC TYPE TRAITS {{{ */
C_DECL_BEGIN

struct su_toolbox;
/* plus PTF typedefs */

/*! \_ */
typedef void *(*su_new_fun)(void);
/*! \_ */
typedef void *(*su_clone_fun)(void const *t);
/*! \_ */
typedef void (*su_delete_fun)(void *self);
/*! Update of \SELF should not be assumed, use return value instead! */
typedef void *(*su_assign_fun)(void *self, void const *t);
/*! \_ */
typedef su_sz (*su_compare_fun)(void const *a, void const *b);
/*! \_ */
typedef su_uz (*su_hash_fun)(void const *self);

/*! Needs to be binary compatible with \c{su::{toolbox,type_toolbox<T>}}!
 * Also see \r{COLL}. */
struct su_toolbox{
   su_new_fun tb_clone;       /*!< \_ */
   su_delete_fun tb_delete;   /*!< \_ */
   su_assign_fun tb_assign;   /*!< \_ */
   su_compare_fun tb_compare; /*!< \_ */
   su_hash_fun tb_hash;       /*!< \_ */
};
/*! Initialize a \r{su_toolbox}.
 * Use C-style casts, not and ever \r{su_R()}! */
#define su_TOOLBOX_I9R(CLONE,DELETE,ASSIGN,COMPARE,HASH) \
{\
   su_FIELD_INITN(tb_clone) (su_new_fun)CLONE,\
   su_FIELD_INITN(tb_delete) (su_delete_fun)DELETE,\
   su_FIELD_INITN(tb_assign) (su_assign_fun)ASSIGN,\
   su_FIELD_INITN(tb_compare) (su_compare_fun)COMPARE,\
   su_FIELD_INITN(tb_hash) (su_hash_fun)HASH\
}

C_DECL_END
#if !C_LANG || defined DOXYGEN_CXX
NSPC_BEGIN(su)

template<class T> class type_traits;
template<class T> struct type_toolbox;
// Plus C wrapper typedef

// External forward, defined in a-t-t.h.
template<class T> class auto_type_toolbox;

typedef su_toolbox toolbox; /*!< See \r{type_toolbox}, \r{COLL}. */

/*! See \r{type_toolbox}, \r{COLL}. */
template<class T>
class type_traits{
public:
   typedef T type; /*!< \_ */
   typedef T *tp; /*!< \_ */
   typedef T const const_type; /*!< \_ */
   typedef T const *const_tp; /*!< \_ */

   typedef NSPC(su)type_toolbox<type> toolbox; /*!< \_ */
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox; /*!< \_ */

   /*! Non-pointer types are by default own-guessed, pointer based ones not. */
   static boole const ownguess = TRU1;
   /*! Ditto, associative collections, keys. */
   static boole const ownguess_key = TRU1;

   /*! \_ */
   static void *to_vp(const_tp t) {return C(void*,S(void const*,t));}
   /*! \_ */
   static void const *to_const_vp(const_tp t) {return t;}

   /*! \_ */
   static tp to_tp(void const *t) {return C(tp,S(const_tp,t));}
   /*! \_ */
   static const_tp to_const_tp(void const *t) {return S(const_tp,t);}
};

// Some specializations
template<class T>
class type_traits<T const>{ // (ugly, but required for some node based colls..)
public:
   typedef T type;
   typedef T *tp;
   typedef T const const_type;
   typedef T const *const_tp;
   typedef NSPC(su)type_toolbox<type> toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(const_tp t) {return C(tp,t);}
   static void const *to_const_vp(const_tp t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(const_tp,t));}
   static const_tp to_const_tp(void const *t) {return S(const_tp,t);}
};

template<class T>
class type_traits<T *>{
public:
   typedef T type;
   typedef T *tp;
   typedef T const const_type;
   typedef T const *const_tp;
   typedef NSPC(su)type_toolbox<type> toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(const_tp t) {return C(tp,t);}
   static void const *to_const_vp(const_tp t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(const_tp,t));}
   static const_tp to_const_tp(void const *t) {return S(const_tp,t);}
};

template<>
class type_traits<void *>{
public:
   typedef void *type;
   typedef void *tp;
   typedef void const *const_type;
   typedef void const *const_tp;
   typedef NSPC(su)toolbox toolbox;
   typedef NSPC(su)auto_type_toolbox<void *> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = FAL0;

   static void *to_vp(const_tp t) {return C(tp,t);}
   static void const *to_const_vp(const_tp t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(const_tp,t));}
   static const_tp to_const_tp(void const *t) {return S(const_tp,t);}
};

template<>
class type_traits<void const *>{
public:
   typedef void const *type;
   typedef void const *tp;
   typedef void const *const_type;
   typedef void const *const_tp;
   typedef NSPC(su)toolbox toolbox;
   typedef NSPC(su)auto_type_toolbox<void const *> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = FAL0;

   static void *to_vp(const_tp t) {return C(void*,t);}
   static void const *to_const_vp(const_tp t) {return t;}
   static tp to_tp(void const *t) {return C(void*,t);}
   static const_tp to_const_tp(void const *t) {return t;}
};

template<>
class type_traits<char *>{
public:
   typedef char *type;
   typedef char *tp;
   typedef char const *const_type;
   typedef char const *const_tp;
   typedef NSPC(su)type_toolbox<type> toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(const_tp t) {return C(tp,t);}
   static void const *to_const_vp(const_tp t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(const_tp,t));}
   static const_tp to_const_tp(void const *t) {return S(const_tp,t);}
};

template<>
class type_traits<char const *>{
public:
   typedef char const *type;
   typedef char const *tp;
   typedef char const *const_type;
   typedef char const *const_tp;
   typedef NSPC(su)type_toolbox<type> toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(const_tp t) {return C(char*,t);}
   static void const *to_const_vp(const_tp t) {return t;}
   static tp to_tp(void const *t) {return C(char*,S(const_tp,t));}
   static const_tp to_const_tp(void const *t) {return S(const_tp,t);}
};

/*! This is binary compatible with \r{toolbox} (and \r{su_toolbox})!
 * Also see \r{COLL}. */
template<class T>
struct type_toolbox{
   /*! \_ */
   typedef NSPC(su)type_traits<T> type_traits;

   /*! \_ */
   typename type_traits::tp (*ttb_clone)(typename type_traits::const_tp t);
   /*! \_ */
   void (*ttb_delete)(typename type_traits::tp self);
   /*! \_ */
   typename type_traits::tp (*ttb_assign)(typename type_traits::tp self,
         typename type_traits::const_tp t);
   /*! \_ */
   sz (*ttb_compare)(typename type_traits::const_tp self,
         typename type_traits::const_tp t);
   /*! \_ */
   uz (*ttb_hash)(typename type_traits::const_tp self);
};
/*! \_ */
#define su_TYPE_TOOLBOX_I9R(CLONE,DELETE,ASSIGN,COMPARE,HASH) \
{\
   su_FIELD_INITN(ttb_clone) CLONE,\
   su_FIELD_INITN(ttb_delete) DELETE,\
   su_FIELD_INITN(ttb_assign) ASSIGN,\
   su_FIELD_INITN(ttb_compare) COMPARE,\
   su_FIELD_INITN(ttb_hash) HASH\
}

// abc,clip,max,min,pow2 -- the C macros are in SUPPORT MACROS+
/*! \_ */
template<class T> inline T get_abs(T const &a) {return su_ABS(a);}
/*! \_ */
template<class T>
inline T const &get_clip(T const &a, T const &min, T const &max){
   return su_CLIP(a, min, max);
}
/*! \_ */
template<class T>
inline T const &get_max(T const &a, T const &b) {return su_MAX(a, b);}
/*! \_ */
template<class T>
inline T const &get_min(T const &a, T const &b) {return su_MIN(a, b);}
template<class T> inline int is_pow2(T const &a) {return su_IS_POW2(a);}

NSPC_END(su)
#endif /* !C_LANG */
/* BASIC TYPE TRAITS }}} */
/* BASIC C/C++ INTERFACE (SYMBOLS) {{{ */
C_DECL_BEGIN

/*! Byte order mark macro; there is \r{su_bom} plus endianess support below.
 * We optionally may have \c{su_CC_BOM{,_BIG,_LITTLE}} support, too! */
#define su_BOM 0xFEFFu

/*! Log priorities, for simplicity of use without _LEVEL or _LVL prefix */
enum su_log_level{
   su_LOG_EMERG,  /*!< System is unusable (abort()s the program) */
   su_LOG_ALERT,  /*!< Action must be taken immediately */
   su_LOG_CRIT,   /*!< Critical conditions */
   su_LOG_ERR,    /*!< Error conditions */
   su_LOG_WARN,   /*!< Warning conditions */
   su_LOG_NOTICE, /*!< Normal but significant condition */
   su_LOG_INFO,   /*!< Informational */
   su_LOG_DEBUG   /*!< Debug-level message */
};

/*! \_ */
enum su_state_flags{
   /* enum su_log_level is first "member" */
   su__STATE_LOG_MASK = 0xFFu,

   su_STATE_DEBUG = 1u<<8,    /*!< \_ */
   su_STATE_VERBOSE = 1u<<9,  /*!< \_ */
   su__STATE_D_V = su_STATE_DEBUG | su_STATE_VERBOSE,

   /*! Reproducible behaviour switch.
    * See \r{su_reproducible_build},
    * and \xln{https://reproducible-builds.org}. */
   su_STATE_REPRODUCIBLE = 1u<<10,

   /*! By default out-of-memory situations, or container and string etc.
    * insertions etc. which cause count/offset datatype overflow result in
    * \r{su_LOG_EMERG}s, and thus program abortion.
    *
    * This default can be changed by setting the corresponding su_state*()
    * bit, \c{su_STATE_ERR_NOMEM} and \r{su_STATE_ERR_OVERFLOW}, respectively,
    * in which case the functions will return corresponding error codes, then,
    * and the log will happen with a \r{su_LOG_ALERT} level instead.
    *
    * \r{su_STATE_ERR_PASS} may be used only as an argument to
    * \r{su_state_err()}, when \r{su_LOG_EMERG} is to be avoided at all cost
    * (only \r{su_LOG_DEBUG} logs happen then).
    * Likewise, \r{su_STATE_ERR_NOPASS}, to be used when \r{su_state_err()}
    * must not return.
    * Ditto, \r{su_STATE_ERR_NOERRNO}, to be used when \r{su_err_no()} shall
    * not be set. */
   su_STATE_ERR_NOMEM = 1u<<26,
   su_STATE_ERR_OVERFLOW = 1u<<27,  /*!< \r{su_STATE_ERR_NOMEM}. */
   /*! \_ */
   su_STATE_ERR_TYPE_MASK = su_STATE_ERR_NOMEM | su_STATE_ERR_OVERFLOW,

   su_STATE_ERR_PASS = 1u<<28,      /*!< \r{su_STATE_ERR_NOMEM}. */
   su_STATE_ERR_NOPASS = 1u<<29,    /*!< \r{su_STATE_ERR_NOMEM}. */
   su_STATE_ERR_NOERRNO = 1u<<30,   /*!< \r{su_STATE_ERR_NOMEM}. */
   su__STATE_ERR_MASK = su_STATE_ERR_TYPE_MASK |
         su_STATE_ERR_PASS | su_STATE_ERR_NOPASS | su_STATE_ERR_NOERRNO,

   su__STATE_USER_MASK = ~(su__STATE_LOG_MASK |
         su_STATE_ERR_PASS | su_STATE_ERR_NOPASS | su_STATE_ERR_NOERRNO)
};
MCTA((uz)su_LOG_DEBUG <= (uz)su__STATE_LOG_MASK, "Bit ranges may not overlap")

#ifdef DOXYGEN
/*! The \SU error number constants.
 * In order to achieve a 1:1 mapping of the \SU and the host value, e.g.,
 * of \ERR{INTR} and \c{EINTR}, the actual values will be detected at
 * compilation time.
 * Non resolvable (native) mappings will map to \ERR{NOTOBACCO},
 * \SU mappings with no (native) mapping will have high unsigned numbers. */
enum su_err_number{
   su_ERR_NONE,      /*!< No error. */
   su_ERR_NOTOBACCO  /*!< No such errno, fallback: no mapping exists. */
};
#endif

union su__bom_union{
   char bu_buf[2];
   u16 bu_val;
};

/* Known endianess bom versions, see su_bom_little, su_bom_big */
EXPORT_DATA union su__bom_union const su__bom_little;
EXPORT_DATA union su__bom_union const su__bom_big;

/* (Not yet) Internal enum su_state_flags bit carrier */
EXPORT_DATA uz su__state;

/*! The byte order mark \r{su_BOM} in host, little and big byte order.
 * The latter two are macros which access constant union data.
 * We also define two helpers \r{su_BOM_IS_BIG()} and \r{su_BOM_IS_LITTLE()},
 * which will expand to preprocessor statements if possible. */
EXPORT_DATA u16 const su_bom;
/*! \_ */
#define su_bom_little su__bom_little.bu_val
/*! \_ */
#define su_bom_big su__bom_big.bu_val

#if defined su_CC_BOM || defined DOXYGEN
# define su_BOM_IS_BIG() (su_CC_BOM == su_CC_BOM_BIG)       /*!< \r{su_bom}. */
# define su_BOM_IS_LITTLE() (su_CC_BOM == su_CC_BOM_LITTLE) /*!< \r{su_bom}. */
#else
# define su_BOM_IS_BIG() (su_bom == su_bom_big)
# define su_BOM_IS_LITTLE() (su_bom == su_bom_little)
#endif

/*! The empty string. */
EXPORT_DATA char const su_empty[1];

/*! The string \c{reproducible_build}, see \r{su_STATE_REPRODUCIBLE}. */
EXPORT_DATA char const su_reproducible_build[sizeof "reproducible_build"];

/*! Set to the name of the program to create a common log message prefix. */
EXPORT_DATA char const *su_program;

/*! Interaction with the SU library \r{su_state_flags} machine.
 * The last to be called once one of the \c{STATE_ERR*} conditions occurred,
 * it returns (if it returns) the corresponding \r{su_err_number} */
SINLINE boole su_state_has(uz flags){
   return ((su__state & (flags & su__STATE_USER_MASK)) != 0);
}
/*! \_ */
SINLINE void su_state_set(uz flags) {su__state |= flags & su__STATE_USER_MASK;}
/*! \_ */
SINLINE void su_state_clear(uz flags){
   su__state &= ~(flags & su__STATE_USER_MASK);
}
/*! Notify an error to the \SU state machine; see \r{su_STATE_ERR_NOMEM}. */
EXPORT s32 su_state_err(uz state, char const *msg_or_nil);

/*! \_ */
EXPORT s32 su_err_no(void);
/*! \_ */
EXPORT s32 su_err_set_no(s32 eno);

/*! Return string(s) describing C error number eno */
EXPORT char const *su_err_doc(s32 eno);
/*! \_ */
EXPORT char const *su_err_name(s32 eno);

/*! Try to map an error name to an error number.
 * Returns the fallback error as a negative value if none found */
EXPORT s32 su_err_from_name(char const *name);

/*! \_ */
EXPORT s32 su_err_no_via_errno(void);

/*! \_ */
SINLINE enum su_log_level su_log_get_level(void){
   return S(enum su_log_level,su__state & su__STATE_LOG_MASK);
}
/*! \_ */
SINLINE void su_log_set_level(enum su_log_level nlvl){
   su__state = (su__state & su__STATE_USER_MASK) |
         (S(uz,nlvl) & su__STATE_LOG_MASK);
}

/*! Log functions of various sort.
 * Regardless of the level these also log if \c{STATE_DEBUG|STATE_VERBOSE}. */
EXPORT void su_log_write(enum su_log_level lvl, char const *fmt, ...);
/*! See \r{su_log_write()}.  The vp is a &va_list. */
EXPORT void su_log_vwrite(enum su_log_level lvl, char const *fmt, void *vp);

/*! Like perror(3). */
EXPORT void su_perr(char const *msg, s32 eno_or_0);

#if !defined su_ASSERT_EXPAND_NOTHING || defined DOXYGEN
/*! With a \FAL0 crash this only logs.
 * In order to get rid of linkage define \c{su_ASSERT_EXPAND_NOTHING}. */
EXPORT void su_assert(char const *expr, char const *file, u32 line,
      char const *fun, boole crash);
#else
# define su_assert(EXPR,FILE,LINE,FUN,CRASH)
#endif

#if defined su_HAVE_DEBUG || defined su_HAVE_DEVEL
void su_nyd_chirp(u8 act, char const *file, u32 line, char const *fun);
void su_nyd_dump(void (*ptf)(up cookie, char const *buf, uz blen), up cookie);
#endif

C_DECL_END
#if !C_LANG || defined DOXYGEN_CXX
NSPC_BEGIN(su)

// FIXME C++ does not yet expose the public C EXPORT_DATA symbols

// All instanceless static encapsulators.
class bom;
class err;
class log;
class state;

/*! \_ */
class bom{
public:
   /*! \r{su_BOM} */
   static u16 host(void) {return su_BOM;}
   /*! \r{su_bom_little} */
   static u16 little(void) {return su_bom_little;}
   /*! \r{su_bom_big} */
   static u16 big(void) {return su_bom_big;}
};

/*! \_ */
class err{
public:
   /*! \r{su_err_no()} */
   static s32 no(void) {return su_err_no();}
   /*! \r{su_err_set_no()} */
   static void set_no(s32 eno) {su_err_set_no(eno);}

   /*! \r{su_err_doc()} */
   static char const *doc(s32 eno) {return su_err_doc(eno);}
   /*! \r{su_err_name()} */
   static char const *name(s32 eno) {return su_err_name(eno);}
   /*! \r{su_err_from_name()} */
   static s32 from_name(char const *name) {return su_err_from_name(name);}

   /*! \r{su_err_no_via_errno()} */
   static s32 no_via_errno(void) {return su_err_no_via_errno();}
};

/*! \_ */
class log{
public:
   /*! Log priorities, for simplicity of use without _LEVEL or _LVL prefix */
   enum level{
      emerg = su_LOG_EMERG,   /*! \r{su_LOG_EMERG} */
      alert = su_LOG_ALERT,   /*! \r{su_LOG_ALERT} */
      crit = su_LOG_CRIT,     /*! \r{su_LOG_CRIT} */
      err = su_LOG_ERR,       /*! \r{su_LOG_ERR} */
      warn = su_LOG_WARN,     /*! \r{su_LOG_WARN} */
      notice = su_LOG_NOTICE, /*! \r{su_LOG_NOTICE} */
      info = su_LOG_INFO,     /*! \r{su_LOG_INFO} */
      debug = su_LOG_DEBUG    /*! \r{su_LOG_DEBUG} */
   };

   // Log functions of various sort.
   // Regardless of the level these also log if state_debug|state_verbose.
   // The vp is a &va_list
   /*! \r{su_log_get_level()} */
   static level get_level(void) {return S(level,su_log_get_level());}
   /*! \r{su_log_set_level()} */
   static void set_level(level lvl) {su_log_set_level(S(su_log_level,lvl));}
   /*! \r{su_log_write()} */
   static void write(level lvl, char const *fmt, ...);
   /*! \r{su_log_vwrite()} */
   static void vwrite(level lvl, char const *fmt, void *vp){
      su_log_vwrite(S(enum su_log_level,lvl), fmt, vp);
   }
   /*! \r{su_perr()} */
   static void perr(char const *msg, s32 eno_or_0) {su_perr(msg, eno_or_0);}
};

/*! \_ */
class state{
public:
   /*! \_ */
   enum flags{
      debug = su_STATE_DEBUG, /*!< \r{su_STATE_DEBUG} */
      verbose = su_STATE_VERBOSE, /*!< \r{su_STATE_VERBOSE} */

      // reproducible-build.org
      reproducible = su_STATE_REPRODUCIBLE, /*!< \r{su_STATE_REPRODUCIBLE} */

      err_nomem = su_STATE_ERR_NOMEM, /*!< \r{su_STATE_ERR_NOMEM} */
      err_overflow = su_STATE_ERR_OVERFLOW, /*!< \r{su_STATE_ERR_OVERFLOW} */
      err_type_mask = su_STATE_ERR_TYPE_MASK,/*!< \r{su_STATE_ERR_TYPE_MASK} */

      err_pass = su_STATE_ERR_PASS, /*!< \r{su_STATE_ERR_PASS} */
      err_nopass = su_STATE_ERR_NOPASS, /*!< \r{su_STATE_ERR_NOPASS} */
      err_noerrno = su_STATE_ERR_NOERRNO /*!< \r{su_STATE_ERR_NOERRNO} */
   };

   // Interaction with the SU library "enum state" machine.
   // The last to be called once one of the state_err* conditions occurred
   // it returns (if it returns) the corresponding enum \r{su_err_number}
   /*! \r{su_state_has()} */
   static boole has(uz state) {return su_state_has(state);}
   /*! \r{su_state_set()} */
   static void set(uz state) {su_state_set(state);}
   /*! \r{su_state_clear()} */
   static void clear(uz state) {su_state_clear(state);}
   /*! \r{su_state_err()} */
   static s32 err(uz state, char const *msg_or_nil=NIL){
      return su_state_err(state, msg_or_nil);
   }
};

NSPC_END(su)
#endif /* !C_LANG */
/* BASIC C/C++ INTERFACE (SYMBOLS) }}} */

/* MORE DOXYGEN TOP GROUPS {{{ */
/*!
 * \defgroup COLL Collections
 * \brief Collections
 *
 * In \SU, and by default, collections learn how to deal with managed objects
 * through \r{su_toolbox} objects.
 * The C++ variants deduce many more things, and automatically, through
 * (specializations of) \r{type_traits}, \r{type_toolbox}, and
 * \r{auto_type_toolbox}.
 */

/*!
 * \defgroup IO Input/Output
 * \brief Input and Output
 */

/*!
 * \defgroup NET Network
 * \brief Network
 */

/*!
 * \defgroup MEM Memory
 * \brief Memory
 */

/*!
 * \defgroup MISC Miscellaneous
 * \brief Miscellaneous
 */

/*!
 * \defgroup SMP SMP
 * \brief Simultaneous Multi Processing
 */

/*!
 * \defgroup TEXT Text
 * \brief Text
 */
/* MORE DOXYGEN TOP GROUPS }}} */

#include <su/code-ou.h>
/*! @} */
#endif /* !su_CODE_H */
/* s-it-mode */
