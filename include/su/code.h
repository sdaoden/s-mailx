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
 * Alternatively all or individual \r{su_state_err_type}s will not cause
 * hard-failures.
 *
 * The actual global mode of operation can be queried via \r{su_state_get()}
 * (presence checks with \r{su_state_has()},
 * is configurable via \r{su_state_set()} and \r{su_state_clear()}, and often
 * the default can also be changed on a by-call or by-object basis, see
 * \r{su_state_err_flags} and \r{su_clone_fun} for more on this.
 *
 * \remarks{C++ object creation failures via \c{su_MEM_NEW()} etc. will however
 * always cause program abortion due to standard imposed execution flow.
 * This can be worked around by using \c{su_MEM_NEW_HEAP()} as appropriate.}
 * }\li{
 * Most collection and string object types work on 32-bit (or even 31-bit)
 * lengths a.k.a. counts a.k.a. sizes.
 * For simplicity of use, and because datatype overflow is a handled case, the
 * user interface very often uses \r{su_uz} (i.e., \c{size_t}).
 * Other behaviour is explicitly declared with a "big" prefix, as in
 * "biglist", but none such object does exist at the time of this writing.
 * }\li{
 * \SU requires an eight (8) or more byte alignment on the stack and heap.
 * This is because some of its facilities may use the lower (up to) three
 * bits of pointers for internal, implementation purposes.
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
   /* Features */
   /*! Whether the \SU namespace exists.
    * If not, facilities exist in the global namespace. */
# define su_HAVE_NSPC

# define su_HAVE_DEBUG /*!< Debug variant, including assertions etc. */
   /*! Test paths available in non-debug code.
    * Also, compiler pragmas which suppress some warnings are not set, etc.*/
# define su_HAVE_DEVEL
# define su_HAVE_DOCSTRINGS /*!< Some more helpful strings. */
# define su_HAVE_MEM_BAG_AUTO /*!< \_ */
# define su_HAVE_MEM_BAG_LOFI /*!< \_ */
   /*! Normally the debug library provides memory write boundary excess via
    * canaries (see \r{MEM_CACHE_ALLOC} and \r{su_MEM_ALLOC_DEBUG}).
    * Since this counteracts external memory checkers like \c{valgrind(1)} or
    * the ASAN (address sanitizer) compiler extensions, the \SU checkers can be
    * disabled explicitly. */
# define su_HAVE_MEM_CANARIES_DISABLE
# define su_HAVE_RE /*!< \r{RE} support available? */
# define su_HAVE_SMP /*!< \r{SMP} support available? */
   /*!< Multithreading support available?
    * This is a subfeature of \r{SMP}. */
# define su_HAVE_MT

   /* Values */
# define su_PAGE_SIZE /*!< \_ */
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
 * Whereas the former two are configuration-time constants which will create
 * additional API and cause a different ABI, the latter will only cause
 * preprocessor changes, for example for \r{su_ASSERT()}.
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

#define su_OS_CYGWIN 0 /*!< \_ */
#define su_OS_DARWIN 0 /*!< \_ */
#define su_OS_DRAGONFLY 0 /*!< \_ */
#define su_OS_EMX 0 /*!< \_ */
#define su_OS_FREEBSD 0 /*!< \_ */
#define su_OS_LINUX 0 /*!< \_ */
#define su_OS_MINIX 0 /*!< \_ */
#define su_OS_MSDOS 0 /*!< \_ */
#define su_OS_NETBSD 0 /*!< \_ */
#define su_OS_OPENBSD 0 /*!< \_ */
#define su_OS_SOLARIS 0 /*!< \_ */
#define su_OS_SUNOS 0 /*!< \_ */
#define su_OS_WIN32 0 /*!< \_ */
#define su_OS_WIN64 0 /*!< \_ */

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
# define su_C_LANG 1 /*!< \_ */
# define su_C_DECL_BEGIN /*!< \_ */
# define su_C_DECL_END /*!< \_ */

   /* Casts */
# define su_S(T,I) ((T)(I)) /*!< \_ */
# define su_R(T,I) ((T)(I)) /*!< \_ */
# define su_C(T,I) ((T)su_R(su_up,I)) /*!< \_ */

# define su_NIL ((void*)0) /*!< \_ */
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
# define su_EXPORT /*extern*/ /*!< \_ */
# define su_EXPORT_DATA extern /*!< \_ */
# define su_IMPORT /*extern*/ /*!< \_ */
# define su_IMPORT_DATA extern /*!< \_ */
#endif

/* Compile-Time-Assert
 * Problem is that some compilers warn on unused local typedefs, so add
 * a special local CTA to overcome this */
#if (!su_C_LANG && __cplusplus +0 >= 201103L) || defined DOXYGEN
# define su_CTA(T,M) static_assert(T, M) /*!< \_ */
# define su_LCTA(T,M) static_assert(T, M) /*!< \_ */
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

#define su_CTAV(T) su_CTA(T, "Unexpected value of constant") /*!< \_ */
#define su_LCTAV(T) su_LCTA(T, "Unexpected value of constant") /*!< \_ */
#ifdef su_MASTER
# define su_MCTA(T,M) su_CTA(T, M);
#else
# define su_MCTA(T,M)
#endif
#define CXXCAST(T1,T2) \
   CTA(sizeof(T1) == sizeof(T2),\
      "Wild C++ type==C type cast constrained not fullfilled!")

/* LANG }}} */

/* CC {{{ */

#define su_CC_CLANG 0 /*!< \_ */
#define su_CC_VCHECK_CLANG(X,Y) 0 /*!< \_ */
#define su_CC_GCC 0 /*!< \_ */
#define su_CC_VCHECK_GCC(X,Y) 0 /*!< \_ */
#define su_CC_PCC 0 /*!< \_ */
#define su_CC_VCHECK_PCC(X,Y) 0 /*!< \_ */
#define su_CC_SUNPROC 0 /*!< \_ */
#define su_CC_VCHECK_SUNPROC(X,Y) 0 /*!< \_ */
#define su_CC_TINYC 0 /*!< \_ */
#define su_CC_VCHECK_TINYC(X,Y) 0 /*!< \_ */

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
# define su_CC_BOM_LITTLE 1234 /*!< Only if there is \r{su_CC_BOM}. */
# define su_CC_BOM_BIG 4321 /*!< Only if there is \r{su_CC_BOM}. */
#endif
#if !defined su_CC_UZ_TYPE && defined __SIZE_TYPE__
# define su_CC_UZ_TYPE __SIZE_TYPE__
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
# ifdef DOXYGEN
#  define su_INLINE inline /*!< \_ */
#  define su_SINLINE inline /*!< \_ */
# elif su_CC_CLANG || su_CC_GCC || su_CC_PCC
#  if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901l
#   if !defined NDEBUG || !defined __OPTIMIZE__
#    define su_INLINE static inline
#    define su_SINLINE static inline
#   else
     /* clang does not like inline with <-O2 */
#    define su_INLINE inline __attribute__((always_inline))
#    define su_SINLINE static inline __attribute__((always_inline))
#   endif
#  else
#   if su_CC_VCHECK_GCC(3, 1)
#    define su_INLINE static __inline __attribute__((always_inline))
#    define su_SINLINE static __inline __attribute__((always_inline))
#   else
#    define su_INLINE static __inline
#    define su_SINLINE static __inline
#   endif
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
# define su_LIKELY(X) ((X) != 0) /*!< \_ */
# define su_UNLIKELY(X) ((X) != 0) /*!< \_ */
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
/*! Absolute value. */
#define su_ABS(A) ((A) < 0 ? -(A) : (A))
/*! Cramp \a{X} to be in between \a{A} and \a{B}, inclusive. */
#define su_CLIP(X,A,B) (((X) <= (A)) ? (A) : (((X) >= (B)) ? (B) : (X)))
/*! Is power of two? */
#define su_IS_POW2(X) ((((X) - 1) & (X)) == 0)
/*! Maximum value. */
#define su_MAX(A,B) ((A) < (B) ? (B) : (A))
/*! Minimum value. */
#define su_MIN(A,B) ((A) < (B) ? (A) : (B))
/*! Round down \a{X} to nearest multiple of \a{BASE}. */
#define su_ROUND_DOWN(X,BASE) (((X) / (BASE)) * (BASE))
/*! Ditto, if \a{BASE} is a power of two. */
#define su_ROUND_DOWN2(X,BASE) ((X) & (~((BASE) - 1)))
/*! Round up \a{X} to nearest multiple of \a{BASE}. */
#define su_ROUND_UP(X,BASE) ((((X) + ((BASE) - 1)) / (BASE)) * (BASE))
/*! Ditto, if \a{BASE} is a power of two. */
#define su_ROUND_UP2(X,BASE) (((X) + ((BASE) - 1)) & (~((BASE) - 1)))

/* Alignment.  Note: su_uz POW2 asserted in POD section below! */
/* Commented out: "_Alignof() applied to an expression is a GNU extension" */
#if 0 && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112L
# include <stdalign.h>
# define su_ALIGNOF(X) _Alignof(X)
#else
   /*! \c{_Alignof()} if available, something hacky otherwise */
# define su_ALIGNOF(X) su_ROUND_UP2(sizeof(X), su__ZAL_L)
#endif

/*! Align a pointer \a{MEM} by the \r{su_ALIGNOF()} of \a{OTYPE}, and cast
 * the result to \a{DTYPE}. */
#define su_P_ALIGN(DTYPE,OTYPE,MEM) \
   su_R(DTYPE,\
      su_IS_POW2(su_ALIGNOF(OTYPE))\
         ? su_ROUND_UP2(su_R(su_up,MEM), su_ALIGNOF(OTYPE))\
         : su_ROUND_UP(su_R(su_up,MEM), su_ALIGNOF(OTYPE)))

/* Roundup/align an integer;  Note: POW2 asserted in POD section below! */
/*! Overalign an integer value to a size that cannot cause just any problem
 * for anything which does not use special alignment directives.
 * \remarks{It is safe to assume that \r{su_P_ALIGN()} can be used to place an
 * object into a memory region spaced with it.} */
#define su_Z_ALIGN_OVER(X) su_ROUND_UP2(su_S(su_uz,X), 2 * su__ZAL_L)

/*! Smaller than \r{su_Z_ALIGN_OVER()}, but sufficient for plain-old-data. */
#define su_Z_ALIGN(X) su_ROUND_UP2(su_S(su_uz,X), su__ZAL_L)

/*! \r{su_Z_ALIGN()}, but only for pointers and \r{su_uz}. */
#define su_Z_ALIGN_PZ(X) su_ROUND_UP2(su_S(su_uz,X), su__ZAL_S)

/* (These are below MCTA()d to be of equal size[, however].)
 * _L must adhere to the minimum aligned claimed in the \mainpage */
# define su__ZAL_S su_MAX(sizeof(su_uz), sizeof(void*))
# define su__ZAL_L su_MAX(su__ZAL_S, sizeof(su_u64))/* XXX FP,128bit */

/* Variants of ASSERT */
#if defined NDEBUG || defined DOXYGEN
# define su_ASSERT_INJ(X) /*!< Injection! */
# define su_ASSERT_INJOR(X,Y) Y /*!< Injection! */
# define su_ASSERT_NB(X) ((void)0) /*!< No block. */
# define su_ASSERT(X) do{}while(0) /*!< \_ */
# define su_ASSERT_LOC(X,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_EXEC(X,S) do{}while(0) /*!< \_ */
# define su_ASSERT_EXEC_LOC(X,S,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_JUMP(X,L) do{}while(0) /*!< \_ */
# define su_ASSERT_JUMP_LOC(X,L,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_RET(X,Y) do{}while(0) /*!< \_ */
# define su_ASSERT_RET_LOC(X,Y,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_RET_VOID(X) do{}while(0) /*!< \_ */
# define su_ASSERT_RET_VOID_LOC(X,Y,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_NYD_EXEC(X,Y) do{}while(0) /*!< \_ */
# define su_ASSERT_NYD_EXEC_LOC(X,FNAME,LNNO) do{}while(0) /*!< \_ */
# define su_ASSERT_NYD(X) do{}while(0) /*!< \_ */
# define su_ASSERT_NYD_LOC(X,FNAME,LNNO) do{}while(0) /*!< \_ */
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

/*! There are no bit-\c{enum}erations, but we use \c{enum}s as such, since the
 * only other option for bit constants would be preprocessor macros.
 * Since enumerations are expected to represent a single value, a normal
 * integer is often used to store enumeration values.
 * This macro is used instead to be explicit, \a{X} is the pod, \a{Y} is the
 * name of the enumeration that is meant. */
#define su_BITENUM_IS(X,Y) X /* enum X */

/*! Create a bit mask for the inclusive bit range \a{LO} to \a{HI}.
 * \remarks{\a{HI} cannot use highest bit!}
 * \remarks{Identical to \r{su_BITS_RANGE_MASK().} */
#define su_BITENUM_MASK(LO,HI) (((1u << ((HI) + 1)) - 1) & ~((1u << (LO)) - 1))

/*! For injection macros like su_DBG(), NDBG, DBGOR, 64, 32, 6432 */
#define su_COMMA ,

/* Debug injections */
#if defined su_HAVE_DEBUG && !defined NDEBUG
# define su_DBG(X) X /*!< \_ */
# define su_NDBG(X) /*!< \_ */
# define su_DBGOR(X,Y) X /*!< \_ */
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
# define su_DVL(X) X /*!< \_ */
# define su_NDVL(X) /*!< \_ */
# define su_DVLOR(X,Y) X /*!< \_ */
#else
# define su_DVL(X)
# define su_NDVL(X) X
# define su_DVLOR(X,Y) Y
#endif

/*! To avoid files that are overall empty */
#define su_EMPTY_FILE() typedef int su_CONCAT(su_notempty_shall_b_, su_FILE);

/* C field init */
#if (su_C_LANG && defined __STDC_VERSION__ && \
      __STDC_VERSION__ +0 >= 199901L) || defined DOXYGEN
# define su_FIELD_INITN(N) .N = /*!< \_ */
# define su_FIELD_INITI(I) [I] = /*!< \_ */
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
# define su_FIELD_OFFSETOF(T,F) \
      su_S(su_uz,su_S(su_up,&(su_R(T *,0x1)->F)) - 1)
#endif

/*! Distance in between the fields \a{S}tart and \a{E}end in type \a{T}. */
#define su_FIELD_RANGEOF(T,S,E) \
      (su_FIELD_OFFSETOF(T, E) - su_FIELD_OFFSETOF(T, S))

/*! sizeof() for member fields */
#define su_FIELD_SIZEOF(T,F) sizeof(su_S(T *,su_NIL)->F)

/* Multithread injections */
#ifdef su_HAVE_MT
# define su_MT(X) X /*!< \_ */
#else
# define su_MT(X)
#endif

/*! Members in constant array */
#define su_NELEM(A) (sizeof(A) / sizeof((A)[0]))

/*! NYD comes from code-{in,ou}.h (support function below).
 * Instrumented functions will always have one label for goto: purposes. */
#define su_NYD_OU_LABEL su__nydou

/*! Pointer to size_t */
#define su_P2UZ(X) su_S(su_uz,(su_up)(X))

/*! Pointer comparison */
#define su_PCMP(A,C,B) (su_R(su_up,A) C su_R(su_up,B))

/* SMP injections */
#ifdef su_HAVE_SMP
# define su_SMP(X) X /*!< \_ */
#else
# define su_SMP(X)
#endif

/* String stuff.
 * __STDC_VERSION__ is ISO C99, so also use __STDC__, which should work */
#if defined __STDC__ || defined __STDC_VERSION__ || su_C_LANG || \
      defined DOXYGEN
# define su_STRING(X) #X /*!< \_ */
# define su_XSTRING(X) su_STRING(X) /*!< \_ */
# define su_CONCAT(S1,S2) su__CONCAT_1(S1, S2) /*!< \_ */
# define su__CONCAT_1(S1,S2) S1 ## S2
#else
# define su_STRING(X) "X"
# define su_XSTRING STRING
# define su_CONCAT(S1,S2) S1/* will no work out though */S2
#endif

#if su_C_LANG || defined DOXYGEN
   /*! Compare (maybe mixed-signed) integers cases to \a{T} bits, unsigned,
    * \a{T} is one of our homebrew integers, e.g.,
    * \c{UCMP(32, su_ABS(n), >, wleft)}.
    * \remarks{Does not sign-extend correctly, this is up to the caller.} */
# define su_UCMP(T,A,C,B) (su_S(su_ ## u ## T,A) C su_S(su_ ## u ## T,B))
#else
# define su_UCMP(T,A,C,B) \
      (su_S(su_NSPC(su) u ## T,A) C su_S(su_NSPC(su) u ## T,B))
#endif

/*! Casts-away (*NOT* cast-away) */
#define su_UNCONST(T,P) su_R(T,su_R(su_up,su_S(void const*,P)))
/*! Casts-away (*NOT* cast-away) */
#define su_UNVOLATILE(T,P) su_R(T,su_R(su_up,su_S(void volatile*,P)))
/*! To avoid warnings with modern compilers for "char*i; *(s32_t*)i=;" */
#define su_UNALIGN(T,P) su_R(T,su_R(su_up,P))
#define su_UNXXX(T,C,P) su_R(T,su_R(su_up,su_S(C,P)))

/* Avoid "may be used uninitialized" warnings */
#if (defined NDEBUG && !(defined su_HAVE_DEBUG || defined su_HAVE_DEVEL)) || \
      defined DOYGEN
# define su_UNINIT(N,V) su_S(void,0) /*!< \_ */
# define su_UNINIT_DECL(V) /*!< \_ */
#else
# define su_UNINIT(N,V) N = V
# define su_UNINIT_DECL(V) = V
#endif

/*! Avoid "unused" warnings */
#define su_UNUSED(X) ((void)(X))

#if (su_C_LANG && defined __STDC_VERSION__ && \
      __STDC_VERSION__ +0 >= 199901L) || defined DOXYGEN
   /*! Variable-type size (with byte array at end) */
# define su_VFIELD_SIZE(X)
   /*! Variable-type size (with byte array at end) */
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
typedef unsigned long su_ul; /*!< \_ */
typedef unsigned int su_ui; /*!< \_ */
typedef unsigned short su_us; /*!< \_ */
typedef unsigned char su_uc; /*!< \_ */

typedef signed long su_sl; /*!< \_ */
typedef signed int su_si; /*!< \_ */
typedef signed short su_ss; /*!< \_ */
typedef signed char su_sc; /*!< \_ */

#if defined UINT8_MAX || defined DOXYGEN
# define su_U8_MAX UINT8_MAX /*!< \_ */
# define su_S8_MIN INT8_MIN /*!< \_ */
# define su_S8_MAX INT8_MAX /*!< \_ */
typedef uint8_t su_u8; /*!< \_ */
typedef int8_t su_s8; /*!< \_ */
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
# define su_U16_MAX UINT16_MAX /*!< \_ */
# define su_S16_MIN INT16_MIN /*!< \_ */
# define su_S16_MAX INT16_MAX /*!< \_ */
typedef uint16_t su_u16; /*!< \_ */
typedef int16_t su_s16; /*!< \_ */
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
# define su_U32_MAX UINT32_MAX /*!< \_ */
# define su_S32_MIN INT32_MIN /*!< \_ */
# define su_S32_MAX INT32_MAX /*!< \_ */
typedef uint32_t su_u32; /*!< \_ */
typedef int32_t su_s32; /*!< \_ */
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
# define su_U64_MAX UINT64_MAX /*!< \_ */
# define su_S64_MIN INT64_MIN /*!< \_ */
# define su_S64_MAX INT64_MAX /*!< \_ */
# define su_S64_C(C) INT64_C(C) /*!< \_ */
# define su_U64_C(C) UINT64_C(C) /*!< \_ */
typedef uint64_t su_u64; /*!< \_ */
typedef int64_t su_s64; /*!< \_ */
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
typedef size_t su_uz; /*!< \_ */
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
# define su_UZ_BITS 32u /*!< \_ */
# define su_64(X) /*!< \_ */
# define su_32(X) X /*!< \_ */
# define su_6432(X,Y) Y /*!< \_ */
typedef su_s32 su_sz; /*!< \_ */
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
typedef uintptr_t su_up; /*!< \_ */
typedef intptr_t su_sp; /*!< \_ */
#else
# ifdef SIZE_MAX
typedef su_uz su_up;
typedef su_sz su_sp;
# else
typedef su_ul su_up;
typedef su_sl su_sp;
# endif
#endif

/*! Values for #su_boole (normally only \c{FAL0} and \c{TRU1}). */
enum{
   su_FAL0, /*!< 0 (no bits set). */
   su_TRU1, /*!< The value 1. */
   su_TRU2, /*!< The value 2. */
   su_TRUM1 = -1 /*!< All bits set. */
};
typedef su_s8 su_boole; /*!< The \SU boolean type (see \FAL0 etc.). */

/* POD TYPE SUPPORT }}} */

/* BASIC C INTERFACE (STATE) {{{ */

/* su_state.. machinery: first byte: global log instance.. */

/*! Byte order mark macro; there are also \r{su_bom}, \r{su_BOM_IS_BIG()}
 * and \r{su_BOM_IS_LITTLE()}. */
#define su_BOM 0xFEFFu

/*! Log priorities, for simplicity of use without _LEVEL or _LVL prefix,
 * for \r{su_log_set_level()}. */
enum su_log_level{
   su_LOG_EMERG, /*!< System is unusable (abort()s the program) */
   su_LOG_ALERT, /*!< Action must be taken immediately */
   su_LOG_CRIT, /*!< Critical conditions */
   su_LOG_ERR, /*!< Error conditions */
   su_LOG_WARN, /*!< Warning conditions */
   su_LOG_NOTICE, /*!< Normal but significant condition */
   su_LOG_INFO, /*!< Informational */
   su_LOG_DEBUG /*!< Debug-level message */
};
enum{
   su__LOG_MAX = su_LOG_DEBUG,
   su__LOG_SHIFT = 8,
   su__LOG_MASK = (1u << su__LOG_SHIFT) - 1
};
MCTA(1u<<su__LOG_SHIFT > su__LOG_MAX, "Bit ranges may not overlap")

/*! Flags that can be ORd to \r{su_log_level}. */
enum su_log_flags{
   /*! In environments where \r{su_log_write()} is (also) hooked to an output
    * channel, do not log the message through that. */
   su_LOG_F_CORE = 1u<<(su__LOG_SHIFT+0)
};

/*! Adjustment possibilities for the global log domain (e.g,
 * \r{su_log_write()}), to be set via \r{su_state_set()}, to be queried via
 * \r{su_state_has()}. */
enum su_state_log_flags{
   /*! Prepend a messages \r{su_log_level}. */
   su_STATE_LOG_SHOW_LEVEL = 1u<<4,
   /*! Show the PID (Process IDentification number).
    * This flag is only honoured if \r{su_program} set to non-\NIL. */
   su_STATE_LOG_SHOW_PID = 1u<<5
};

/* ..second byte: hardening errors.. */

/*! Global hardening for out-of-memory and integer etc. overflow: types.
 * By default out-of-memory situations, or container and string etc.
 * insertions etc. which cause count/offset datatype overflow result in
 * \r{su_LOG_EMERG}s, and thus program abortion.
 *
 * This global default can be changed by including the corresponding
 * \c{su_state_err_type} (\r{su_STATE_ERR_NOMEM} and
 * \r{su_STATE_ERR_OVERFLOW}, respectively), in the global \SU state machine
 * via \r{su_state_set()}, in which case logging uses \r{su_LOG_ALERT} level,
 * a corresponding \r{su_err_number} will be assigned for \r{su_err_no()}, and
 * the failed function will return error.
 *
 * Often functions and object allow additional control over the global on
 * a by-call or by-object basis, taking a state argument which consists of
 * \c{su_state_err_type} and \r{su_state_err_flags} bits.
 * In order to support this these values do not form an enumeration, but
 * rather are combinable bits. */
enum su_state_err_type{
   su_STATE_ERR_NOMEM = 1u<<8, /*!< Out-of-memory. */
   su_STATE_ERR_OVERFLOW = 1u<<9 /*!< Integer/xy domain overflow. */
};

/*! Hardening for out-of-memory and integer etc. overflow: adjustment flags.
 * Many functions offer the possibility to adjust the global \r{su_state_get()}
 * (\r{su_state_has()}) \r{su_state_err_type} default on a per-call level, and
 * object types (can) do so on a per-object basis.
 *
 * If so, the global state can (selectively) be bypassed by adding in
 * \r{su_state_err_type}s to be ignored to an (optional) function argument,
 * or object control function or field.
 * It is also possible to instead enforce program abortion regardless of
 * a global ignorance policy, and pass other control flags. */
enum su_state_err_flags{
   /*! A mask containing all \r{su_state_err_type} bits. */
   su_STATE_ERR_TYPE_MASK = su_STATE_ERR_NOMEM | su_STATE_ERR_OVERFLOW,
   /*! Allow passing of all errors.
    * This is just a better name alias for \r{su_STATE_ERR_TYPE_MASK}. */
   su_STATE_ERR_PASS = su_STATE_ERR_TYPE_MASK,
   /*! Regardless of global (and additional local) policy, if this flag is
    * set, an actual error causes a hard program abortion. */
   su_STATE_ERR_NOPASS = 1u<<12,
   /*! If this flag is set and no abortion is about to happen, a corresponding
    * \r{su_err_number} will not be assigned to \r{su_err_no()}. */
   su_STATE_ERR_NOERRNO = 1u<<13,
   /*! This is special in that it plays no role in the global state machine.
    * However, many types or functions which provide \a{estate} arguments and
    * use (NOT) \r{su_STATE_ERR_MASK} to overload that with meaning, adding
    * support for owning \r{COLL} (for \r{su_toolbox} users, to be exact)
    * actually made sense: if this bit is set it indicates that \NIL values
    * returned by \r{su_toolbox} members are acceptible values (and thus do not
    * cause actions like insertion, replacement etc. to fail). */
   su_STATE_ERR_NIL_IS_VALID_OBJECT = 1u<<14,
   /*! Alias for \r{su_STATE_ERR_NIL_IS_VALID_OBJECT}. */
   su_STATE_ERR_NILISVALO = su_STATE_ERR_NIL_IS_VALID_OBJECT,
   /*! Handy mask for the entire family of error \SU error bits,
    * \r{su_state_err_type} and \r{su_state_err_flags}.
    * It can be used by functions or methods which allow fine-tuning of error
    * behaviour to strip down an user argument.
    *
    * \remarks{This mask itself is covered by the mask \c{0xFF00}.
    * This condition is compile-time asserted.} */
   su_STATE_ERR_MASK = su_STATE_ERR_TYPE_MASK |
         su_STATE_ERR_PASS | su_STATE_ERR_NOPASS | su_STATE_ERR_NOERRNO |
         su_STATE_ERR_NIL_IS_VALID_OBJECT
};

/* ..third byte: misc flags */

/*! \_ */
enum su_state_flags{
   su_STATE_NONE, /*!< No flag: this is 0. */
   su_STATE_DEBUG = 1u<<16, /*!< \_ */
   su_STATE_VERBOSE = 1u<<17, /*!< \_ */
   /*! Reproducible behaviour switch.
    * See \r{su_reproducible_build},
    * and \xln{https://reproducible-builds.org}. */
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

/*! The \SU error number constants.
 * In order to achieve a 1:1 mapping of the \SU and the host value, e.g.,
 * of \ERR{INTR} and \c{EINTR}, the actual values will be detected at
 * compilation time.
 * Non resolvable (native) mappings will map to \ERR{NOTOBACCO},
 * \SU mappings with no (native) mapping will have high unsigned numbers. */
enum su_err_number{
#ifdef DOXYGEN
   su_ERR_NONE, /*!< No error. */
   su_ERR_NOTOBACCO /*!< No such errno, fallback: no mapping exists. */
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

/*! The byte order mark \r{su_BOM} in host, \r{su_bom_little} and
 * \r{su_bom_big} byte order.
 * The latter two are macros which access constant union data.
 * We also define two helpers \r{su_BOM_IS_BIG()} and \r{su_BOM_IS_LITTLE()},
 * which will expand to preprocessor statements if possible (by using
 * \r{su_CC_BOM}, \r{su_CC_BOM_LITTLE} and \r{su_CC_BOM_BIG}), but otherwise
 * to comparisons of the external constants. */
EXPORT_DATA u16 const su_bom;

#define su_bom_little su__bom_little.bu_val /*!< \_ */
#define su_bom_big su__bom_big.bu_val /*!< \_ */

#if defined su_CC_BOM || defined DOXYGEN
# define su_BOM_IS_BIG() (su_CC_BOM == su_CC_BOM_BIG) /*!< \r{su_bom}. */
# define su_BOM_IS_LITTLE() (su_CC_BOM == su_CC_BOM_LITTLE) /*!< \r{su_bom}. */
#else
# define su_BOM_IS_BIG() (su_bom == su_bom_big)
# define su_BOM_IS_LITTLE() (su_bom == su_bom_little)
#endif

/*! The empty string. */
EXPORT_DATA char const su_empty[1];

/*! The string \c{reproducible_build}, see \r{su_STATE_REPRODUCIBLE}. */
EXPORT_DATA char const su_reproducible_build[];

/*! Can be set to the name of the program to, e.g., create a common log
 * message prefix.
 * Also see \r{su_STATE_LOG_SHOW_PID}, \r{su_STATE_LOG_SHOW_LEVEL}. */
EXPORT_DATA char const *su_program;

/**/
#ifdef su_HAVE_MT
EXPORT void su__glock(enum su__glock_type gt);
EXPORT void su__gunlock(enum su__glock_type gt);
#endif

/*! Interaction with the SU library (global) state machine.
 * This covers \r{su_state_log_flags}, \r{su_state_err_type},
 * and \r{su_state_flags} flags and values. */
INLINE u32 su_state_get(void){
   return (su__state & su__STATE_GLOBAL_MASK);
}

/*! Interaction with the SU library (global) state machine:
 * test whether all (not any) of \a{flags} are set in \r{su_state_get()}. */
INLINE boole su_state_has(uz flags){
   flags &= su__STATE_GLOBAL_MASK;
   return ((su__state & flags) == flags);
}

/*! \_ */
INLINE void su_state_set(uz flags){
   MT( su__glock(su__GLOCK_STATE); )
   su__state |= flags & su__STATE_GLOBAL_MASK;
   MT( su__gunlock(su__GLOCK_STATE); )
}

/*! \_ */
INLINE void su_state_clear(uz flags){
   MT( su__glock(su__GLOCK_STATE); )
   su__state &= ~(flags & su__STATE_GLOBAL_MASK);
   MT( su__gunlock(su__GLOCK_STATE); )
}

/*! Notify an error to the \SU (global) state machine.
 * If the function is allowd to return a corresponding \r{su_err_number} will
 * be returned. */
EXPORT s32 su_state_err(enum su_state_err_type err, uz state,
      char const *msg_or_nil);

/*! \_ */
EXPORT s32 su_err_no(void);

/*! \_ */
EXPORT s32 su_err_set_no(s32 eno);

/*! Return string(s) describing C error number \a{eno}.
 * Effectively identical to \r{su_err_name()} if either the compile-time
 * option \r{su_HAVE_DOCSTRINGS} is missing (always), or when
 *  \r{su_state_has()} \r{su_STATE_REPRODUCIBLE} set. */
EXPORT char const *su_err_doc(s32 eno);

/*! \_ */
EXPORT char const *su_err_name(s32 eno);

/*! Try to map an error name to an error number.
 * Returns the fallback error as a negative value if none found */
EXPORT s32 su_err_from_name(char const *name);

/*! \_ */
EXPORT s32 su_err_no_via_errno(void);

/*! \_ */
INLINE enum su_log_level su_log_get_level(void){
   return S(enum su_log_level,su__state & su__STATE_LOG_MASK);
}

/*! \_ */
INLINE void su_log_set_level(enum su_log_level nlvl){
   uz lvl;
   /*MT( su__glock(su__GLOCK_STATE); )*/
   lvl = S(uz,nlvl) & su__STATE_LOG_MASK;
   su__state = (su__state & su__STATE_GLOBAL_MASK) | lvl;
   /*MT( su__gunlock(su__GLOCK_STATE); )*/
}

/*! \_ */
INLINE boole su_log_would_write(enum su_log_level lvl){
   return ((S(u32,lvl) & su__LOG_MASK) <= (su__state & su__STATE_LOG_MASK) ||
      (su__state & su__STATE_D_V));
}

/*! Log functions of various sort.
 * \a{lvl} is a bitmix of a \r{su_log_level} and \r{su_log_flags}.
 * Regardless of the level these also log if \c{STATE_DEBUG|STATE_VERBOSE}.
 * If \r{su_program} is set, it will be prepended to messages. */
EXPORT void su_log_write(BITENUM_IS(u32,su_log_level) lvl,
      char const *fmt, ...);

/*! See \r{su_log_write()}.  The \a{vp} is a \c{&va_list}. */
EXPORT void su_log_vwrite(BITENUM_IS(u32,su_log_level) lvl,
      char const *fmt, void *vp);

/*! Like perror(3). */
EXPORT void su_perr(char const *msg, s32 eno_or_0);

/*! SMP lock the global log domain. */
INLINE void su_log_lock(void){
   MT( su__glock(su__GLOCK_LOG); )
}

/*! SMP unlock the global log domain. */
INLINE void su_log_unlock(void){
   MT( su__gunlock(su__GLOCK_LOG); )
}

#if !defined su_ASSERT_EXPAND_NOTHING || defined DOXYGEN
/*! With a \FAL0 crash this only logs.
 * In order to get rid of linkage define \c{su_ASSERT_EXPAND_NOTHING}. */
EXPORT void su_assert(char const *expr, char const *file, u32 line,
      char const *fun, boole crash);
#else
# define su_assert(EXPR,FILE,LINE,FUN,CRASH)
#endif

#if DVLOR(1, 0)
/*! When \a{disabled}, \r{su_nyd_chirp()} will return quick. */
EXPORT void su_nyd_set_disabled(boole disabled);

/*! In event-loop driven software that uses long jumps it may be desirable to
 * reset the recursion level at times.  \a{nlvl} is only honoured when smaller
 * than the current recursion level. */
EXPORT void su_nyd_reset_level(u32 nlvl);

/*! Not-yet-dead chirp.
 * Normally used from the support macros in code-{in,ou}.h. */
EXPORT void su_nyd_chirp(u8 act, char const *file, u32 line, char const *fun);

/*! Dump all existing not-yet-dead entries via \a{ptf}.
 * \a{buf} is NUL terminated despite \a{blen} being passed, too. */
EXPORT void su_nyd_dump(void (*ptf)(up cookie, char const *buf, uz blen),
      up cookie);
#endif

/* BASIC C INTERFACE (STATE) }}} */

/* BASIC TYPE TOOLBOX AND TRAITS {{{ */

struct su_toolbox;
/* plus PTF typedefs */

/*! Create a new default instance of an object type, return it or \NIL.
 * See \r{su_clone_fun} for the meaning of \a{estate}. */
typedef void *(*su_new_fun)(u32 estate);

/*! Create a clone of \a{t}, and return it.
 * \a{estate} might be set to some \r{su_state_err_type}s to be turned to
 * non-fatal errors, and contain \r{su_state_err_flags} with additional
 * control requests.
 * Otherwise (\a{estate} is 0) \NIL can still be returned for
 * \r{su_STATE_ERR_NOMEM} or \r{su_STATE_ERR_OVERFLOW}, dependent on the
 * global \r{su_state_get()} / \r{su_state_has()} setting,
 * as well as for other errors and with other \r{su_err_number}s, of course.
 * Also see \r{su_STATE_ERR_NIL_IS_VALID_OBJECT}. */
typedef void *(*su_clone_fun)(void const *t, u32 estate);

/*! Delete an instance returned by \r{su_new_fun} or \r{su_clone_fun} (or
 * \r{su_assign_fun}). */
typedef void (*su_delete_fun)(void *self);

/*! Assign \a{t}; see \r{su_clone_fun} for the meaning of \a{estate}.
 * In-place update of \SELF is (and should) not (be) assumed, but instead the
 * return value has to be used, with the exception as follows.
 * First all resources of \a{self} should be released (an operation which is
 * not supposed to fail), then the assignment be performed.
 * If this fails, \a{self} should be turned to cleared state again,
 * and \NIL should be returned.
 *
 * \remarks{This function is not used by (object owning) \r{COLL} unless
 * \r{su_STATE_ERR_NIL_IS_VALID_OBJECT} is set.  Regardless, if \NIL is
 * returned to indicate error then the caller which passed a non-\NIL object
 * is responsible for deletion or taking other appropriate steps.}
 *
 * \remarks{If \a{self} and \a{t} are \r{COLL}, then if assignment fails then
 * whereas \a{self} will not manage any elements, it has been assigned \a{t}'s
 * possible existent \r{su_toolbox} as well as other attributes.
 * Some \r{COLL} will provide an additional \c{assign_elems()} function.} */
typedef void *(*su_assign_fun)(void *self, void const *t, u32 estate);

/*! Compare \a{a} and \a{b}, and return a value less than 0 if \a{a} is "less
 * than \a{b}", 0 on equality, and a value greater than 0 if \a{a} is
 * "greate than \a{b}". */
typedef su_sz (*su_compare_fun)(void const *a, void const *b);

/*! Create a hash that reproducibly represents \SELF. */
typedef su_uz (*su_hash_fun)(void const *self);

/* Needs to be binary compatible with \c{su::{toolbox,type_toolbox<T>}}! */
/*! A toolbox provides object handling knowledge to \r{COLL}.
 * Also see \r{su_TOOLBOX_I9R()}. */
struct su_toolbox{
   su_clone_fun tb_clone; /*!< \copydoc{su_clone_fun}. */
   su_delete_fun tb_delete; /*!< \copydoc{su_delete_fun}. */
   su_assign_fun tb_assign; /*!< \copydoc{su_assign_fun}. */
   su_compare_fun tb_compare; /*!< \copydoc{su_compare_fun}. */
   su_hash_fun tb_hash; /*!< \copydoc{su_hash_fun}. */
};

/* Use C-style casts, not and ever su_R()! */
/*! Initialize a \r{su_toolbox}. */
#define su_TOOLBOX_I9R(CLONE,DELETE,ASSIGN,COMPARE,HASH) \
{\
   su_FIELD_INITN(tb_clone) (su_clone_fun)(CLONE),\
   su_FIELD_INITN(tb_delete) (su_delete_fun)(DELETE),\
   su_FIELD_INITN(tb_assign) (su_assign_fun)(ASSIGN),\
   su_FIELD_INITN(tb_compare) (su_compare_fun)(COMPARE),\
   su_FIELD_INITN(tb_hash) (su_hash_fun)(HASH)\
}

/* BASIC TYPE TRAITS }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

/* POD TYPE SUPPORT {{{ */

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

typedef su_u8 u8; /*!< \_ */
typedef su_s8 s8; /*!< \_ */
typedef su_u16 u16; /*!< \_ */
typedef su_s16 s16; /*!< \_ */
typedef su_u32 u32; /*!< \_ */
typedef su_s32 s32; /*!< \_ */
typedef su_u64 u64; /*!< \_ */
typedef su_s64 s64; /*!< \_ */

typedef su_uz uz; /*!< \_ */
typedef su_sz sz; /*!< \_ */

typedef su_up up; /*!< \_ */
typedef su_sp sp; /*!< \_ */

typedef su_boole boole; /*!< \_ */
/*! Values for \r{su_boole}. */
enum{
   FAL0 = su_FAL0, /*!< \_ */
   TRU1 = su_TRU1, /*!< \_ */
   TRU2 = su_TRU2, /*!< \_ */
   TRUM1 = su_TRUM1 /*!< All bits set. */
};

/* Place the mentioned alignment CTAs */
MCTA(IS_POW2(sizeof(uz)), "Must be power of two")
MCTA(IS_POW2(su__ZAL_S), "Must be power of two")
MCTA(IS_POW2(su__ZAL_L), "Must be power of two")

/*! \_ */
class min{
public:
   static NSPC(su)s8 const s8 = su_S8_MIN; /*!< \copydoc{su_S8_MIN} */
   static NSPC(su)s16 const s16 = su_S16_MIN; /*!< \copydoc{su_S16_MIN} */
   static NSPC(su)s32 const s32 = su_S32_MIN; /*!< \copydoc{su_S32_MIN} */
   static NSPC(su)s64 const s64 = su_S64_MIN; /*!< \copydoc{su_S64_MIN} */
   static NSPC(su)sz const sz = su_SZ_MIN; /*!< \copydoc{su_SZ_MIN} */
};

/*! \_ */
class max{
public:
   static NSPC(su)s8 const s8 = su_S8_MAX; /*!< \copydoc{su_S8_MAX} */
   static NSPC(su)s16 const s16 = su_S16_MAX; /*!< \copydoc{su_S16_MAX} */
   static NSPC(su)s32 const s32 = su_S32_MAX; /*!< \copydoc{su_S32_MAX} */
   static NSPC(su)s64 const s64 = su_S64_MAX; /*!< \copydoc{su_S64_MAX} */
   static NSPC(su)sz const sz = su_SZ_MAX; /*!< \copydoc{su_SZ_MAX} */

   static NSPC(su)u8 const u8 = su_U8_MAX; /*!< \copydoc{su_U8_MAX} */
   static NSPC(su)u16 const u16 = su_U16_MAX; /*!< \copydoc{su_U16_MAX} */
   static NSPC(su)u32 const u32 = su_U32_MAX; /*!< \copydoc{su_U32_MAX} */
   static NSPC(su)u64 const u64 = su_U64_MAX; /*!< \copydoc{su_U64_MAX} */
   static NSPC(su)uz const uz = su_UZ_MAX; /*!< \copydoc{su_UZ_MAX} */
};

/* POD TYPE SUPPORT }}} */

/* BASIC C++ INTERFACE (STATE) {{{ */

// FIXME C++ does not yet expose the public C EXPORT_DATA symbols

// All instanceless static encapsulators.
class bom;
class err;
class log;
class state;

/*! \copydoc{su_bom} */
class bom{
public:
   /*! \copydoc{su_BOM} */
   static u16 host(void) {return su_BOM;}

   /*! \copydoc{su_bom_little} */
   static u16 little(void) {return su_bom_little;}

   /*! \copydoc{su_bom_big} */
   static u16 big(void) {return su_bom_big;}
};

/*! \_ */
class err{
public:
   /*! \copydoc{su_err_number} */
   enum err_number{
#ifdef DOXYGEN
      enone,      /*!< No error. */
      enotobacco  /*!< No such errno, fallback: no mapping exists. */
#else
      su__CXX_ERR_NUMBER_ENUM
# undef su__CXX_ERR_NUMBER_ENUM
#endif
   };

   /*! \copydoc{su_err_no()} */
   static s32 no(void) {return su_err_no();}

   /*! \copydoc{su_err_set_no()} */
   static void set_no(s32 eno) {su_err_set_no(eno);}

   /*! \copydoc{su_err_doc()} */
   static char const *doc(s32 eno) {return su_err_doc(eno);}

   /*! \copydoc{su_err_name()} */
   static char const *name(s32 eno) {return su_err_name(eno);}

   /*! \copydoc{su_err_from_name()} */
   static s32 from_name(char const *name) {return su_err_from_name(name);}

   /*! \copydoc{su_err_no_via_errno()} */
   static s32 no_via_errno(void) {return su_err_no_via_errno();}
};

/*! \_ */
class log{
public:
   /*! \copydoc{su_log_level} */
   enum level{
      emerg = su_LOG_EMERG, /*!< \copydoc{su_LOG_EMERG} */
      alert = su_LOG_ALERT, /*!< \copydoc{su_LOG_ALERT} */
      crit = su_LOG_CRIT, /*!< \copydoc{su_LOG_CRIT} */
      err = su_LOG_ERR, /*!< \copydoc{su_LOG_ERR} */
      warn = su_LOG_WARN, /*!< \copydoc{su_LOG_WARN} */
      notice = su_LOG_NOTICE, /*!< \copydoc{su_LOG_NOTICE} */
      info = su_LOG_INFO, /*!< \copydoc{su_LOG_INFO} */
      debug = su_LOG_DEBUG /*!< \copydoc{su_LOG_DEBUG} */
   };

   /*! \copydoc{su_log_flags} */
   enum flags{
      f_core = su_LOG_F_CORE, /*!< \copydoc{su_LOG_F_CORE} */
   };

   // Log functions of various sort.
   // Regardless of the level these also log if state_debug|state_verbose.
   // The vp is a &va_list
   /*! \copydoc{su_log_get_level()} */
   static level get_level(void) {return S(level,su_log_get_level());}

   /*! \copydoc{su_log_set_level()} */
   static void set_level(level lvl) {su_log_set_level(S(su_log_level,lvl));}

   /*! \copydoc{su_STATE_LOG_SHOW_LEVEL} */
   static boole get_show_level(void){
      return su_state_has(su_STATE_LOG_SHOW_LEVEL);
   }

   /*! \copydoc{su_STATE_LOG_SHOW_LEVEL} */
   static void set_show_level(boole on){
      if(on)
         su_state_set(su_STATE_LOG_SHOW_LEVEL);
      else
         su_state_clear(su_STATE_LOG_SHOW_LEVEL);
   }

   /*! \copydoc{su_STATE_LOG_SHOW_PID} */
   static boole get_show_pid(void){
      return su_state_has(su_STATE_LOG_SHOW_PID);
   }

   /*! \copydoc{su_STATE_LOG_SHOW_PID} */
   static void set_show_pid(boole on){
      if(on)
         su_state_set(su_STATE_LOG_SHOW_PID);
      else
         su_state_clear(su_STATE_LOG_SHOW_PID);
   }

   /*! \copydoc{su_log_would_write()} */
   static boole would_write(level lvl){
      return su_log_would_write(S(su_log_level,lvl));
   }

   /*! \copydoc{su_log_write()} */
   static void write(BITENUM_IS(u32,level) lvl, char const *fmt, ...);

   /*! \copydoc{su_log_vwrite()} */
   static void vwrite(BITENUM_IS(u32,level) lvl, char const *fmt, void *vp){
      su_log_vwrite(lvl, fmt, vp);
   }

   /*! \copydoc{su_perr()} */
   static void perr(char const *msg, s32 eno_or_0) {su_perr(msg, eno_or_0);}

   /*! \copydoc{su_log_lock()} */
   static void lock(void) {su_log_lock();}

   /*! \copydoc{su_log_unlock()} */
   static void unlock(void) {su_log_unlock();}
};

/*! \_ */
class state{
public:
   /*! \copydoc{su_state_err_type} */
   enum err_type{
      /*! \copydoc{su_STATE_ERR_NOMEM} */
      err_nomem = su_STATE_ERR_NOMEM,
      /*! \copydoc{su_STATE_ERR_OVERFLOW} */
      err_overflow = su_STATE_ERR_OVERFLOW
   };

   /*! \copydoc{su_state_err_flags} */
   enum err_flags{
      /*! \copydoc{su_STATE_ERR_TYPE_MASK} */
      err_type_mask = su_STATE_ERR_TYPE_MASK,
      /*! \copydoc{su_STATE_ERR_PASS} */
      err_pass = su_STATE_ERR_PASS,
      /*! \copydoc{su_STATE_ERR_NOPASS} */
      err_nopass = su_STATE_ERR_NOPASS,
      /*! \copydoc{su_STATE_ERR_NOERRNO} */
      err_noerrno = su_STATE_ERR_NOERRNO,
      /*! \copydoc{su_STATE_ERR_MASK} */
      err_mask = su_STATE_ERR_MASK
   };

   /*! \copydoc{su_state_flags} */
   enum flags{
      /*! \copydoc{su_STATE_NONE} */
      none = su_STATE_NONE,
      /*! \copydoc{su_STATE_DEBUG} */
      debug = su_STATE_DEBUG,
      /*! \copydoc{su_STATE_VERBOSE} */
      verbose = su_STATE_VERBOSE,
      /*! \copydoc{su_STATE_REPRODUCIBLE} */
      reproducible = su_STATE_REPRODUCIBLE
   };

   /*! \copydoc{su_program} */
   static char const *get_program(void) {return su_program;}

   /*! \copydoc{su_program} */
   static void set_program(char const *name) {su_program = name;}

   /*! \copydoc{su_state_get()} */
   static boole get(void) {return su_state_get();}

   /*! \copydoc{su_state_has()} */
   static boole has(uz state) {return su_state_has(state);}

   /*! \copydoc{su_state_set()} */
   static void set(uz state) {su_state_set(state);}

   /*! \copydoc{su_state_clear()} */
   static void clear(uz state) {su_state_clear(state);}

   /*! \copydoc{su_state_err()} */
   static s32 err(err_type err, uz state, char const *msg_or_nil=NIL){
      return su_state_err(S(su_state_err_type,err), state, msg_or_nil);
   }
};

/* BASIC C++ INTERFACE (SYMBOLS) }}} */

/* BASIC TYPE TOOLBOX AND TRAITS {{{ */

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
   typedef T const type_const; /*!< \_ */
   typedef T const *tp_const; /*!< \_ */

   typedef NSPC(su)type_toolbox<type> type_toolbox; /*!< \_ */
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox; /*!< \_ */

   /*! Non-pointer types are by default own-guessed, pointer based ones not. */
   static boole const ownguess = TRU1;
   /*! Ditto, associative collections, keys. */
   static boole const ownguess_key = TRU1;

   /*! \_ */
   static void *to_vp(tp_const t) {return C(void*,S(void const*,t));}
   /*! \_ */
   static void const *to_const_vp(tp_const t) {return t;}

   /*! \_ */
   static tp to_tp(void const *t) {return C(tp,S(tp_const,t));}
   /*! \_ */
   static tp_const to_const_tp(void const *t) {return S(tp_const,t);}
};

// Some specializations
template<class T>
class type_traits<T const>{ // (ugly, but required for some node based colls..)
public:
   typedef T type;
   typedef T *tp;
   typedef T const type_const;
   typedef T const *tp_const;
   typedef NSPC(su)type_toolbox<type> type_toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(tp_const t) {return C(tp,t);}
   static void const *to_const_vp(tp_const t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(tp_const,t));}
   static tp_const to_const_tp(void const *t) {return S(tp_const,t);}
};

template<class T>
class type_traits<T *>{
public:
   typedef T type;
   typedef T *tp;
   typedef T const type_const;
   typedef T const *tp_const;
   typedef NSPC(su)type_toolbox<type> type_toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(tp_const t) {return C(tp,t);}
   static void const *to_const_vp(tp_const t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(tp_const,t));}
   static tp_const to_const_tp(void const *t) {return S(tp_const,t);}
};

template<>
class type_traits<void *>{
public:
   typedef void *type;
   typedef void *tp;
   typedef void const *type_const;
   typedef void const *tp_const;
   typedef NSPC(su)toolbox type_toolbox;
   typedef NSPC(su)auto_type_toolbox<void *> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = FAL0;

   static void *to_vp(tp_const t) {return C(tp,t);}
   static void const *to_const_vp(tp_const t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(tp_const,t));}
   static tp_const to_const_tp(void const *t) {return S(tp_const,t);}
};

template<>
class type_traits<void const *>{
public:
   typedef void const *type;
   typedef void const *tp;
   typedef void const *type_const;
   typedef void const *tp_const;
   typedef NSPC(su)toolbox type_toolbox;
   typedef NSPC(su)auto_type_toolbox<void const *> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = FAL0;

   static void *to_vp(tp_const t) {return C(void*,t);}
   static void const *to_const_vp(tp_const t) {return t;}
   static tp to_tp(void const *t) {return C(void*,t);}
   static tp_const to_const_tp(void const *t) {return t;}
};

template<>
class type_traits<char *>{
public:
   typedef char *type;
   typedef char *tp;
   typedef char const *type_const;
   typedef char const *tp_const;
   typedef NSPC(su)type_toolbox<type> type_toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(tp_const t) {return C(tp,t);}
   static void const *to_const_vp(tp_const t) {return t;}
   static tp to_tp(void const *t) {return C(tp,S(tp_const,t));}
   static tp_const to_const_tp(void const *t) {return S(tp_const,t);}
};

template<>
class type_traits<char const *>{
public:
   typedef char const *type;
   typedef char const *tp;
   typedef char const *type_const;
   typedef char const *tp_const;
   typedef NSPC(su)type_toolbox<type> type_toolbox;
   typedef NSPC(su)auto_type_toolbox<type> auto_type_toolbox;

   static boole const ownguess = FAL0;
   static boole const ownguess_key = TRU1;

   static void *to_vp(tp_const t) {return C(char*,t);}
   static void const *to_const_vp(tp_const t) {return t;}
   static tp to_tp(void const *t) {return C(char*,S(tp_const,t));}
   static tp_const to_const_tp(void const *t) {return S(tp_const,t);}
};

/*! This is binary compatible with \r{toolbox} (and \r{su_toolbox})!
 * Also see \r{COLL}. */
template<class T>
struct type_toolbox{
   /*! \_ */
   typedef NSPC(su)type_traits<T> type_traits;

   /*! \copydoc{su_clone_fun} */
   typedef typename type_traits::tp (*clone_fun)(
         typename type_traits::tp_const t, u32 estate);
   /*! \copydoc{su_delete_fun} */
   typedef void (*delete_fun)(typename type_traits::tp self);
   /*! \copydoc{su_assign_fun} */
   typedef typename type_traits::tp (*assign_fun)(
         typename type_traits::tp self, typename type_traits::tp_const t,
         u32 estate);
   /*! \copydoc{su_compare_fun} */
   typedef sz (*compare_fun)(typename type_traits::tp_const self,
         typename type_traits::tp_const t);
   /*! \copydoc{su_hash_fun} */
   typedef uz (*hash_fun)(typename type_traits::tp_const self);

   /*! \r{#clone_fun} */
   clone_fun ttb_clone;
   /*! \r{#delete_fun} */
   delete_fun ttb_delete;
   /*! \r{#assign_fun} */
   assign_fun ttb_assign;
   /*! \r{#compare_fun} */
   compare_fun ttb_compare;
   /*! \r{#hash_fun} */
   hash_fun ttb_hash;
};

/*! Initialize a \r{type_toolbox}. */
#define su_TYPE_TOOLBOX_I9R(CLONE,DELETE,ASSIGN,COMPARE,HASH) \
      { CLONE, DELETE, ASSIGN, COMPARE, HASH }

// abc,clip,max,min,pow2 -- the C macros are in SUPPORT MACROS+
/*! \_ */
template<class T> inline T get_abs(T const &a) {return su_ABS(a);}

/*! \copydoc{su_CLIP()} */
template<class T>
inline T const &get_clip(T const &a, T const &min, T const &max){
   return su_CLIP(a, min, max);
}

/*! \copydoc{su_MAX()} */
template<class T>
inline T const &get_max(T const &a, T const &b) {return su_MAX(a, b);}

/*! \copydoc{su_MIN()} */
template<class T>
inline T const &get_min(T const &a, T const &b) {return su_MIN(a, b);}

/*! \copydoc{su_ROUND_DOWN()} */
template<class T>
inline T const &get_round_down(T const &a, T const &b){
   return su_ROUND_DOWN(a, b);
}

/*! \copydoc{su_ROUND_DOWN2()} */
template<class T>
inline T const &get_round_down2(T const &a, T const &b){
   return su_ROUND_DOWN2(a, b);
}

/*! \copydoc{su_ROUND_UP()} */
template<class T>
inline T const &get_round_up(T const &a, T const &b){
   return su_ROUND_UP(a, b);
}

/*! \copydoc{su_ROUND_UP2()} */
template<class T>
inline T const &get_round_up2(T const &a, T const &b){
   return su_ROUND_UP2(a, b);
}

/*! \copydoc{su_IS_POW2()} */
template<class T> inline int is_pow2(T const &a) {return su_IS_POW2(a);}

/* BASIC TYPE TRAITS }}} */

NSPC_END(su)
#include <su/code-ou.h>
#endif /* !C_LANG || CXX_DOXYGEN */

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
 *
 * Because the C++ versions are template wrappers around their \c{void*} based
 * C "supertypes", it is inefficient or even impossible to use \SU collections
 * for plain-old-data; to overcome this restriction (some) specializations to
 * work with POD exist.
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
 *
 * This covers general \r{su_HAVE_SMP}, as well as its multi-threading subset
 * \r{su_HAVE_MT}.
 */

/*!
 * \defgroup TEXT Text
 * \brief Text
 */
/* MORE DOXYGEN TOP GROUPS }}} */

/*! @} */
#endif /* !su_CODE_H */
/* s-it-mode */
