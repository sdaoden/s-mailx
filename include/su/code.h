/*@ Code of the basic infrastructure (POD types, macros etc.) and functions.
 *@ And main documentation entry point, as below.
 *@ - Reacts upon su_HAVE_DEBUG, su_HAVE_DEVEL, and the standard NDEBUG.
 *@   The former two are configuration-time constants, they will create
 *@   additional API and even cause a different ABI.
 *@   The latter is a standardized preprocessor macro that is used for example
 *@   to choose \r{su_ASSERT()} expansion.
 *@ - Some macros require su_FILE to be defined to a literal.
 *@ - Define su_MASTER to inject what is to be injected once; for example,
 *@   it enables su_M*CTA() compile time assertions.
 *@ TODO Better describe the state() bitmix
 *
 * Copyright (c) 2001 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

#ifndef DOXYGEN
# include <su/config.h>
#endif

/* MAINPAGE {{{ *//*!
 * \mainpage SU --- Steffen's Utilities
 *
 * Afters years of finding myself too busy to port my old C++ library of which i am so prowd to the C language,
 * and because of the ever increasing necessity to have a foundation of things i like using nonetheless,
 * i finally have started creating a minimal set of tools instead.
 *
 * \head1{Introductional notes}
 *
 * \list{\li{
 * The basic infrastructure of \SU is provided by the file \r{su/code.h}.
 * Because all other \SU headers include it (thus), having it available is always implicit.
 * It should be noted, however, that the \r{CORE} reacts upon a few preprocessor switches, as documented there.
 * }\li{
 * In order to use \SU \r{su_state_create()} or the more basic \r{su_state_create_core()} \b{must} be called \b{first}.
 * The counterpart \r{su_state_gut()} will optionally notify \r{su_state_on_gut_install()}ed handlers.
 * These functions are always in scope.
 * }\li{
 * Overflow and out-of-memory errors are usually detected and result in abortions (via \r{su_LOG_EMERG}).
 * Alternatively all or individual \r{su_state_err_type}s will not cause hard-failures.
 *
 * The actual global mode of operation can be queried via \r{su_state_get()} (presence checks with \r{su_state_has()},
 * is configurable via \r{su_state_set()} and \r{su_state_clear()}, and often the default can also be changed on a
 * by-call or by-object basis, see \r{su_state_err_flags} and \r{su_clone_fun} for more on this.
 *
 * \remarks{C++ object creation failures via \c{su_MEM_NEW()} etc. will always cause program abortion due to standard
 * imposed execution flow.
 * This can be worked around by using \r{su_MEM_NEW_HEAP()} or even \r{su_MEM_NEWF_BLK()} as appropriate.}
 * }\li{
 * Most collection and string object types work on 32-bit (or even 31-bit) lengths a.k.a. counts a.k.a. sizes.
 * For simplicity of use, and because overflow is handled, the interface most often uses \r{su_uz} (i.e., \c{size_t}).
 * Other behaviour is explicitly declared with a "big" prefix, as in the hypothetic "struct su_biglist",
 * but none such object does exist at the time of this writing.
 * }\li{
 * \SU object instances have a \c{_create()} / \c{_gut()} life cycle.
 * Transparent objects or those with special interface condition use a \c{_new()} / \c{_del()} one instead.
 * Non-objects (value carriers etc.) use \c{_setup()} if an equivalent to a constructor is needed.
 *
 * Many objects have an additional \c{_open()} / \c{_close()} or similar usage resource aquisition lifetime.
 * If not, however, and in order to deal with overflow and out-of-memory hardening as above many \a{_create()}
 * interfaces return an error code instead of \SELF, as documented for \r{su_clone_fun}, and usually an \a{estate}
 * argument as documented there is then available to fine-tune error handling.
 * For such types the C++ object constructor will only perform memory clearance, an additional \c{_create()}
 * (or \c{_setup()}) to deal with resource aquisition will be required to perform true initialization.
 * }\li{
 * \SU requires an eight (8) or more byte alignment on the stack and heap.
 * This is because some of its facilities may use the lower (up to) three bits of pointers for internal purposes.
 * }}
 *
 * \head1{Tools in mk/}
 *
 * \list{\li{
 * \c{su-doc-strip.pl}: simple \c{perl(1)} program which strips \c{doxygen(1)}-style comments from all the given files.
 * }\li{
 * \c{su-doxygen.rc}: \c{doxygen(1)} configuration for \SU.
 * \c{doxygen mk/su-doxygen.rc} should generate documentation.
 * }\li{
 * \c{su-find-command.sh}: find an executable command within a POSIX shell.
 * Needed in a shipout.
 * }\li{
 * \c{su-make-cs-ctype.sh}: creates src/su/gen-cs-ctype.h.
 * }\li{
 * \c{su-make-errors.sh}: either create src/su/gen-errors.h, or, at compile time, the \c{OS<>SU} map.
 * Needed in a shipout.
 * }\li{
 * \c{su-make-strip-cxx.sh}: \c{cd(1)}s into include/su and removes C++ code
 * (tagged \c{(SPACE)CXX_DOXYGEN..(SPACE)@CXX_DOXYGEN}) from all header files.
 * POSIX shell and tools.
 * }\li{
 * \c{su-make-version.sh}: (internal) set the \SU version in include/su/code.h.
 * }\li{
 * \c{su-quote-rndtrip.sh}: round trip quote strings within POSIX shell.
 * Needed in a shipout.
 * }}
 *//* MAINPAGE }}} */

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
 *
 * \remarks{The \c{su_HAVE_xy} macros are among the only preprocessor symbols which must be tested via \c{defined}.
 * Normally \SU always defines preprocessor constants, and booleans are denoted via the values 0 and 1, respectively.}
 * @{
 */

#ifdef DOXYGEN
 /* Features */
 /*! Whether the \SU namespace exists.
  * If not, facilities exist in the global namespace. */
# define su_HAVE_NSPC

 /*! Enables a few code check paths and debug-only structure content and function arguments: it creates additional
  * API, and even causes a different ABI.
  * (Code assertions are disabled via the standardized NDEBUG compiler preprocessor variable.) */
# define su_HAVE_DEBUG
 /*! Enable developer mode: it creates additional API, and even causes a different ABI.
  * Expensive in size and runtime development code paths, like extensive memory tracing and lingering, otherwise
  * useless on-gut cleanups, and more verbose compiler pragmas at build time. */
# define su_HAVE_DEVEL
 /*! \r{MD} support available?
  * If so, always included are
  * \list{\li{
  * \r{MD_SIPHASH}; \c{SPDX-License-Identifier: CC0-1.0}.
  * }} */
# define su_HAVE_MD
 /* TODO \r{MD_BLAKE2B} support available?
  * RFC 7693: BLAKE2 Cryptographic Hash and Message Authentication Code (MAC); \c{SPDX-License-Identifier: CC0-1.0}.
  * Subfeature of \r{su_HAVE_MD}. */
/*#  define su_HAVE_MD_BLAKE2B*/
# define su_HAVE_MEM_BAG_AUTO /*!< \r{MEM_BAG}. */
# define su_HAVE_MEM_BAG_LOFI /*!< \r{MEM_BAG}. */
 /*! Normally the development library performs memory write boundary excess detection via canaries (see
  * \r{MEM_CACHE_ALLOC}, \r{su_MEM_ALLOC_DEBUG}).
  * Since this counteracts external memory checkers like \c{valgrind(1)} or the ASAN (address sanitizer) compiler
  * extensions, the \SU checkers can be disabled explicitly. */
# define su_HAVE_MEM_CANARIES_DISABLE
 /*! The seed source for the built-in \r{RANDOM} seed object.
  * Inspected during \SU build time, and must be either \c{su_RANDOM_SEED_BUILTIN} (the default),
  * \c{su_RANDOM_SEED_GETENTROPY} for \c{gentropy(3p)} seeding,
  * \c{su_RANDOM_SEED_GETRANDOM} for \c{getrandom(2/3)} seeding
  * (requires \c{su_RANDOM_GETRANDOM_H} to be defined to the header name,
  * and \c{su_RANDOM_GETRANDOM_FUN} to the name of the function),
  * \c{su_RANDOM_SEED_URANDOM} for seeding via \c{/dev/urandom},
  * or \c{su_RANDOM_SEED_HOOK} for seeding via \c{su_RANDOM_HOOK_FUN}, a \r{su_random_generate_fun};
  * the latter only in special builds, of course. */
# define su_RANDOM_SEED
# define su_HAVE_RE /*!< \r{RE} support available? */
# define su_HAVE_SMP /*!< \r{SMP} support available? */
 /*! Multithreading support available?
  * This is a subfeature of \r{su_HAVE_SMP}. */
#  define su_HAVE_MT
# define su_HAVE_STATE_GUT_FORK /*!< \r{su_STATE_GUT_ACT_FORK} code path. */

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
 * The former two are configuration-time constants, they will create additional API and even cause a different ABI.
 * The latter is a standardized preprocessor macro that is used for example to choose \r{su_ASSERT()} expansion.
 * }\li{
 * The latter is a precondition for \vr{su_HAVE_INLINE}.
 * }\li{
 * Some macros require \vr{su_FILE} to be defined to a literal.
 * That is, they are usually meant to be used by \SU code only.
 * }\li{
 * Define \vr{su_MASTER} to inject what is to be injected once;
 * for example, it enables \c{su_M*CTA()} compile time assertions.
 * }}
 * @{
 */

/*! The hexadecimal version of the library as via \r{su_VERSION_CALC()}. */
#define su_VERSION 0x2000u

/*! \r{su_VERSION} as a MAJOR.MINOR.UPDATE string literal. */
#define su_VERSION_STRING "0.2.0"

/*! Calculate a \SU version number: 8 bits for \a{MA}jor, and 12 bits for each of \a{MI}nor and \a{UP}date components.
 * Because 32-bit range is fully used arguments must be unsigned! */
#define su_VERSION_CALC(MA,MI,UP) ((((MA) & 0xFF) << 24) | (((MI) & 0xFFF) << 12) | (((UP) & 0xFFF) << 12))

/* OS {{{ */

#define su_OS_POSIX 1 /*!< Will be true for POSIX-style operating system(s) (interfaces). */

#define su_OS_CYGWIN 0 /*!< \_ */
#define su_OS_DARWIN 0 /*!< \_ */
#define su_OS_DRAGONFLY 0 /*!< \_ */
#define su_OS_EMX 0 /*!< \_ */
#define su_OS_FREEBSD 0 /*!< \_ */
#define su_OS_HAIKU 0 /*!< \_ */
#define su_OS_LINUX 0 /*!< \_ */
#define su_OS_MINIX 0 /*!< \_ */
#define su_OS_MACOS 0 /*!< \_ */
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
# undef su_OS_POSIX
# define su_OS_CYGWIN 1
# define su_OS_POSIX 0
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
#elif defined __HAIKU__
# undef su_OS_HAIKU
# define su_OS_HAIKU 1
#elif defined __linux__ || defined __linux
# undef su_OS_LINUX
# define su_OS_LINUX 1
#elif defined __minix
# undef su_OS_MINIX
# define su_OS_MINIX 1
#elif defined __APPLE__
# undef su_OS_MACOS
# define su_OS_MACOS 1
#elif defined __MSDOS__
# undef su_OS_MSDOS
# undef su_OS_POSIX
# define su_OS_MSDOS 1
# define su_OS_POSIX 0
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
/*su_OS_WIN32
 *su_OS_WIN64
 *# undef su_OS_POSIX
 *# define su_OS_POSIX 0*/
#endif

/* OS }}} */

/* LANG {{{ */

#ifndef __cplusplus
# define su_C_LANG 1 /*!< \_ */
# define su_C_DECL_BEGIN /*!< \_ */
# define su_C_DECL_END /*!< \_ */

# define su_NSPC_BEGIN(X)
# define su_NSPC_END(X)
# define su_NSPC_USE(X)
# define su_NSPC(X)

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
#  define su_NSPC_BEGIN(X)
#  define su_NSPC_END(X)
#  define su_NSPC_USE(X)
#  define su_NSPC(X) /**/::
# endif

 /* Disable copy-construction and assignment of class */
# define su_CLASS_NO_COPY(C) private:C(C const &);C &operator=(C const &)
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
# if __cplusplus +0 < 201103l /* XXX override ?? */
#  define su_OVRX(X) virtual X
# else
#  define su_OVRX(X) X override
# endif

 /* Casts */
# define su_S(T,I) static_cast<T>(I)
# define su_R(T,I) reinterpret_cast<T>(I)
# define su_C(T,I) const_cast<T>(I)

# if __cplusplus + 0 >= 201103L
#  define su_NIL nullptr
# else
#  define su_NIL (0l)
# endif
#endif /* __cplusplus */

/*! The \r{su_state_err()} mechanism can be configured to not cause abortion in case of datatype overflow and
 * out-of-memory situations.
 * Most functions return error conditions to pass them to their caller, but this is impossible for, for example,
 * C++ copy-constructors and assignment operators.
 * And \SU does not use exceptions.
 * So if those errors could occur and thus be hidden, the prototype is marked with this "keyword" so that callers can
 * decide whether they want to take alternative routes to come to the desired result or not. */
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
 * Problem is that some compilers warn on unused local typedefs, so add a special local CTA to overcome this */
#ifdef DOXYGEN
# define su_CTA(T,M) /*!< \_ */
# define su_LCTA(T,M) /*!< \remarks{Introduces a block scope.} */
/* ISO C variant unusable pre-C23! */
#elif (!su_C_LANG && __cplusplus +0 >= 201103l) || \
	(su_C_LANG && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 202311l)
# define su_CTA(T,M) static_assert(T, M)
# define su_LCTA(T,M) static_assert(T, M)
#else
# define su_CTA(T,M) su__CTA_1(T, su_FILE, __LINE__)
# define su_LCTA(T,M) su__LCTA_1(T, su_FILE, __LINE__)

# define su__CTA_1(T,F,L) su__CTA_2(T, F, L)
# define su__CTA_2(T,F,L) typedef char ASSERTION_failed_file_ ## F ## _line_ ## L[(T) ? 1 : -1]
# define su__LCTA_1(T,F,L) su__LCTA_2(T, F, L)
# define su__LCTA_2(T,F,L) \
do{\
	typedef char ASSERT_failed_file_ ## F ## _line_ ## L[(T) ? 1 : -1];\
	ASSERT_failed_file_ ## F ## _line_ ## L su____i_am_unused__;\
	su_UNUSED(su____i_am_unused__);\
}while(0)
#endif

#define su_CTAV(T) su_CTA(T, "Unexpected value of constant") /*!< \_ */
#define su_LCTAV(T) su_LCTA(T, "Unexpected value of constant") /*!< \_ */
#ifdef su_MASTER
# define su_MCTA(T,M) su_CTA(T, M);
#else
# define su_MCTA(T,M)
#endif
#define su_CXXCAST(T1,T2) CTA(sizeof(T1) == sizeof(T2), "Wild C++ type==C type cast constrained not fulfilled!")

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
# define su_CC_VCHECK_CLANG(X,Y) (__clang_major__ +0 > (X) || (__clang_major__ +0 == (X) && __clang_minor__ +0 >= (Y)))

# define su_CC_ALIGNED(X) __attribute__((aligned(X)))
# define su_CC_EXTEN __extension__
# define su_CC_PACKED __attribute__((packed))
# define su_CC_MEM_ZERO(X,Y) do __builtin_memset(X, 0, Y); while(0)

# if su_CC_VCHECK_CLANG(13,0) /* XXX not "real" */
#  define su_CC_FALLTHRU __attribute__((fallthrough));
# endif

/* __GNUC__ after some other Unix compilers which also define __GNUC__ */
#elif defined __PCC__ /* __clang__ */
# undef su_CC_PCC
# undef su_CC_VCHECK_PCC
# define su_CC_PCC 1
# define su_CC_VCHECK_PCC(X,Y) (__PCC__ +0 > (X) || (__PCC__ +0 == (X) && __PCC_MINOR__ +0 >= (Y)))

# define su_CC_ALIGNED(X) __attribute__((aligned(X)))
# define su_CC_EXTEN __extension__
# define su_CC_PACKED __attribute__((packed))
# define su_CC_MEM_ZERO(X,Y) do __builtin_memset(X, 0, Y); while(0)

#elif defined __SUNPRO_C /* __PCC__ */
# undef su_CC_SUNPROC
# define su_CC_SUNPROC 1

#elif defined __TINYC__ /* __SUNPRO_C */
# undef su_CC_TINYC
# define su_CC_TINYC 1

# define su_CC_ALIGNED(X) __attribute__((aligned(X)))
# define su_CC_EXTEN /* __extension__ (ignored) */
# define su_CC_PACKED __attribute__((packed))
# define su_CC_MEM_ZERO(X,Y) do __builtin_memset(X, 0, Y); while(0)

#elif defined __GNUC__ /* __TINYC__ */
# undef su_CC_GCC
# undef su_CC_VCHECK_GCC
# define su_CC_GCC 1
# define su_CC_VCHECK_GCC(X,Y) (__GNUC__ +0 > (X) || (__GNUC__ +0 == (X) && __GNUC_MINOR__ +0 >= (Y)))

# define su_CC_ALIGNED(X) __attribute__((aligned(X)))
# define su_CC_EXTEN __extension__
# define su_CC_PACKED __attribute__((packed))
# define su_CC_MEM_ZERO(X,Y) do __builtin_memset(X, 0, Y); while(0)

  /* Dunno; unused due to gcc 11.2.0: "a label can only be part of a statement and a declaration is not a statement" */
# if su_CC_VCHECK_GCC(7,0)
#  define su_CC_FALLTHRU UNUSED(0); __attribute__ ((fallthrough));
# endif

#elif !defined su_CC_IGNORE_UNKNOWN
# error SU: This compiler is not yet supported.
# error SU: To continue with your CFLAGS etc., define su_CC_IGNORE_UNKNOWN.
# error SU: It may be necessary to define su_CC_PACKED to a statement that enables structure packing
#endif

#if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112l
# undef su_CC_ALIGNED
# define su_CC_ALIGNED(X) _Alignas(X)
#endif

#ifndef su_CC_ALIGNED
 /*! Structure alignment; will be \c{_Alignas()} (C 2011) if possible. */
# define su_CC_ALIGNED(X) TODO: ALIGNED not supported for this compiler
#endif

#if !defined su_CC_BOM && defined __BYTE_ORDER__ && defined __ORDER_LITTLE_ENDIAN__ && defined __ORDER_BIG_ENDIAN__
# if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define su_CC_BOM su_CC_BOM_LITTLE
# elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define su_CC_BOM su_CC_BOM_BIG
# else
#  error Unsupported __BYTE_ORDER__
# endif
#endif
#if defined su_CC_BOM || defined DOXYGEN
# ifdef DOXYGEN
  /*! Only if the CC offers \r{su_BOM} macros, defined to either \r{su_CC_BOM_LITTLE} or \r{su_CC_BOM_BIG}. */
#  define su_CC_BOM
# endif
# define su_CC_BOM_LITTLE 1234 /*!< Only if there is \r{su_CC_BOM}. */
# define su_CC_BOM_BIG 4321 /*!< Only if there is \r{su_CC_BOM}. */
#endif

#ifndef su_CC_EXTEN
# define su_CC_EXTEN /*!< The \c{__extension__} keyword or equivalent. */
#endif

#ifndef su_CC_FALLTHRU
# define su_CC_FALLTHRU /* FALLTHRU */
#endif

#if !defined su_CC_MEM_ZERO || defined su_HAVE_DEVEL
# undef su_CC_MEM_ZERO
# define su_CC_MEM_ZERO(X,Y) do{\
	su_uz su____l__ = Y;\
	void *su____op__ = X;\
	su_u8 *su____bp__ = su_S(su_u8*,su____op__);\
	while(su____l__-- > 0)\
		*su____bp__++ = 0;\
}while(0)
#endif

#ifndef su_CC_PACKED
  /*! Structure packing. */
# define su_CC_PACKED TODO: PACKED attribute not supported for this compiler
#endif

/* Function name */
#if defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901l
# define su_FUN __func__ /*!< "Not a literal". */
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

/* USECASE_XY_DISABLED for tagging unused files: git rm `git grep ^su_USECASE_MX_DISABLED` */
#ifdef su_USECASE_SU
# define su_SU(X) X
#else
# define su_SU(X)
#endif

#ifdef su_USECASE_MX
# define su_MX(X) X
# define su_USECASE_MX_DISABLED This file is not a (valid) compilation unit
#else
# define su_MX(X)
#endif
#ifndef su_USECASE_MX_DISABLED
# define su_USECASE_MX_DISABLED
#endif

/* If present denotes list of su_HAVE_xy file is using */
#define su_USECASE_CONFIG_CHECKS(X)

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
#if 0 && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 201112l
# i5e <stdalign.h>
# define su_ALIGNOF(X) _Alignof(X)
#else
 /*! Power-of-two alignment for \a{X}.
  * \remarks{Not \c{_Alignof()} even if available because
  * \c{_Alignof()} applied to an expression is a GNU extension}. */
# define su_ALIGNOF(X) su_ROUND_UP2(sizeof(X), su__ZAL_L)
#endif

/*! Align a pointer \a{MEM} by the \r{su_ALIGNOF()} of \a{OTYPE}, and cast the result to \a{DTYPE}. */
#define su_ALIGN_P(DTYPE,OTYPE,MEM) \
	su_R(DTYPE,su_IS_POW2(su_ALIGNOF(OTYPE)) ? su_ROUND_UP2(su_R(su_up,MEM), su_ALIGNOF(OTYPE))\
			: su_ROUND_UP(su_R(su_up,MEM), su_ALIGNOF(OTYPE)))

/* Roundup/align an integer;  Note: POW2 asserted in POD section below! */
/*! Overalign an integer value so it cannot cause problems for anything not using special alignment directives. */
#define su_ALIGN_Z_OVER(X) su_ROUND_UP2(su_S(su_uz,X), 2 * su__ZAL_L)

/*! Smaller than \r{su_ALIGN_Z_OVER()}, but sufficient for plain-old-data. */
#define su_ALIGN_Z(X) su_ROUND_UP2(su_S(su_uz,X), su__ZAL_L)

/*! \r{su_ALIGN_Z()}, but only for pointers and \r{su_uz}. */
#define su_ALIGN_Z_PZ(X) su_ROUND_UP2(su_S(su_uz,X), su__ZAL_S)

/* (Below MCTA()d to be of equal size[, however].)  _L must adhere to the minimum aligned claimed in the \mainpage */
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
	su_R(void,((X) ? su_TRU1 : su_assert(su_STRING(X), __FILE__, __LINE__, su_FUN, su_TRU1), su_FAL0))

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

# define su_ASSERT_NYD_EXEC(X,Y) su_ASSERT_NYD_EXEC_LOC(X, Y, __FILE__, __LINE__)
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

#if !defined __STDC_VERSION__ || __STDC_VERSION__ +0 < 201112l || defined DOXYGEN
# define su_ATOMIC volatile /*!< \c{_Atomic volatile}, or \c{volatile}. */
#else
# define su_ATOMIC _Atomic volatile
#endif

/*! There are no bit-\c{enum}erations, but we use \c{enum}s to store bit values, since the only other option for bit
 * flag constants would be preprocessor macros.
 * Since compilers expect enumerations to represent a single value, a normal integer is often used to store the bit
 * flags that are, effectively, part of a bit enumeration.
 * This macro awaits a future ISO C bit-enumeration: \a{X} is the pod used until then, \a{Y} the name of the
 * enumeration that is really meant.
 * \remarks{Also (mis)used to ensure a certain integer type(-size) is actually used for \c{enum} storage.} */
#define su_BITENUM(X,Y) X /* enum Y */

/*! Create a bit mask for the inclusive bit range \a{LO} to \a{HI}.
 * \remarks{\a{HI} cannot use highest bit!}
 * \remarks{Identical to \r{su_BITS_RANGE_MASK()}.} */
#define su_BITENUM_MASK(LO,HI) (((1u << ((HI) + 1)) - 1) & ~((1u << (LO)) - 1))

/*! It is not possible to enforce a type size for enumerations, thus enforce \a{X}, but mean \a{Y}. */
#define su_PADENUM(X,Y) X /* enum Y */

/*! For injection macros like \r{su_DBG()}, NDBG, DBGOR, 64, 32, 6432. */
#define su_COMMA ,

/* Debug injections */
#if defined su_HAVE_DEBUG
# define su_DBG(X) X /*!< Inject for \r{su_HAVE_DEBUG}. */
# define su_NDBG(X) /*!< \_ */
# define su_DBGOR(X,Y) X /*!< \_ */
# ifndef NDEBUG
#  define su_DBGX(X) X /*!< Inject for \r{su_HAVE_DEBUG} and not \c{NDEBUG}. */
#  define su_NDBGX(X) /*!< \_ */
#  define su_DBGXOR(X,Y) X /*!< \_ */
# endif
#else
# define su_DBG(X)
# define su_NDBG(X) X
# define su_DBGOR(X,Y) Y
#endif
#ifndef su_DBGX
# define su_DBGX(X)
# define su_NDBGX(X) X
# define su_DBGXOR(X,Y) Y
#endif

/* Debug file location arguments.  (For an usage example see su/mem.h.) */
#ifdef su_HAVE_DEVEL
# define su_HAVE_DVL_LOC_ARGS
# define su_DVL_LOC_ARGS_FILE su__dbg_loc_args_file
# define su_DVL_LOC_ARGS_LINE su__dbg_loc_args_line

# define su_DVL_LOC_ARGS_DECL_SOLE \
	char const *su_DVL_LOC_ARGS_FILE, su_u32 su_DVL_LOC_ARGS_LINE
# define su_DVL_LOC_ARGS_DECL , su_DVL_LOC_ARGS_DECL_SOLE
# define su_DVL_LOC_ARGS_INJ_SOLE __FILE__, __LINE__
# define su_DVL_LOC_ARGS_INJ , su_DVL_LOC_ARGS_INJ_SOLE
# define su_DVL_LOC_ARGS_USE_SOLE su_DVL_LOC_ARGS_FILE, su_DVL_LOC_ARGS_LINE
# define su_DVL_LOC_ARGS_USE , su_DVL_LOC_ARGS_USE_SOLE
# define su_DVL_LOC_ARGS_ORUSE su_DVL_LOC_ARGS_FILE, su_DVL_LOC_ARGS_LINE
# define su_DVL_LOC_ARGS_UNUSED() \
do{\
	su_UNUSED(su_DVL_LOC_ARGS_FILE);\
	su_UNUSED(su_DVL_LOC_ARGS_LINE);\
}while(0)
#else
# define su_DVL_LOC_ARGS_FILE "unused"
# define su_DVL_LOC_ARGS_LINE 0
#
# define su_DVL_LOC_ARGS_DECL_SOLE
# define su_DVL_LOC_ARGS_DECL
# define su_DVL_LOC_ARGS_INJ_SOLE
# define su_DVL_LOC_ARGS_INJ
# define su_DVL_LOC_ARGS_USE_SOLE
# define su_DVL_LOC_ARGS_USE
# define su_DVL_LOC_ARGS_ORUSE su_DVL_LOC_ARGS_FILE, su_DVL_LOC_ARGS_LINE
# define su_DVL_LOC_ARGS_UNUSED() do{}while(0)
#endif /* su_HAVE_DEVEL */

/* Development injections */
#ifdef su_HAVE_DEVEL
# define su_DVL(X) X /*!< Inject for \r{su_HAVE_DEVEL}. */
# define su_NDVL(X) /*!< \_ */
# define su_DVLOR(X,Y) X /*!< \_ */
#else
# define su_DVL(X)
# define su_NDVL(X) X
# define su_DVLOR(X,Y) Y
#endif

#if defined su_HAVE_DEVEL || defined su_HAVE_DEBUG
# define su_DVLDBG(X) X /*!< With \r{su_HAVE_DEVEL} or \r{su_HAVE_DEBUG}. */
# define su_NDVLDBG(X) /*!< \_ */
# define su_DVLDBGOR(X,Y) X /*!< \_ */
#else
# define su_DVLDBG(X)
# define su_NDVLDBG(X) X
# define su_DVLDBGOR(X,Y) Y
#endif

/*! To avoid files that are overall empty */
#define su_EMPTY_FILE() typedef int su_CONCAT(su_notempty_shall_b_, su_FILE);

/*! Switch label fall through */
#define su_FALLTHRU su_CC_FALLTHRU

/*! Distance in between the fields \a{S}tart and \a{E}nd of type \a{T}. */
#define su_FIELD_DISTANCEOF(T,S,E) (su_FIELD_OFFSETOF(T, E) - su_FIELD_OFFSETOF(T, S))

/* C field init */
#if (su_C_LANG && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901l) || defined DOXYGEN
# define su_FIELD_INITN(N) .N = /*!< \_ */
# define su_FIELD_INITI(I) [I] = /*!< \_ */
#else
# define su_FIELD_INITN(N)
# define su_FIELD_INITI(I)
#endif

/* XXX offsetof+: clang,pcc check faked! */
#if su_CC_VCHECK_CLANG(5, 0) || su_CC_VCHECK_GCC(4, 1) || su_CC_VCHECK_PCC(1, 2) || defined DOXYGEN
	/*! The offset of field \a{F} in the type \a{T}. */
# define su_FIELD_OFFSETOF(T,F) __builtin_offsetof(T, F)
#else
# define su_FIELD_OFFSETOF(T,F) su_S(su_uz,su_S(su_up,&(su_R(T *,su_NIL)->F)))
#endif

/*! Range in bytes in between and including fields \a{S}tart and \a{E}nd in
 * type \a{T}. */
#define su_FIELD_RANGEOF(T,S,E) (su_FIELD_OFFSETOF(T, E) - su_FIELD_OFFSETOF(T, S) + su_FIELD_SIZEOF(T, E))

/*! Copy memory in the \r{su_FIELD_RANGEOF()} \a{S} and \a{E} of the \a{T}ype instance from the given memory
 * \a{P}ointer to the \a{D}estination, to be used like \c{su_mem_copy(su_FIELD_RANGE_COPY(struct X, D, P, S, E))}. */
#define su_FIELD_RANGE_COPY(T,D,P,S,E) \
	(su_S(su_u8*,su_S(void*,D)) + su_FIELD_OFFSETOF(T,S)),\
	(su_S(su_u8*,su_S(void*,P)) + su_FIELD_OFFSETOF(T,S)),\
	su_FIELD_RANGEOF(T,S,E)

/*! Zero memory in the \r{su_FIELD_RANGEOF()} \a{S} and \a{E} of the \a{T}ype instance at the given memory \a{P}ointer.
 * \remarks{Introduces a block scope.} */
#define su_FIELD_RANGE_ZERO(T,P,S,E) \
	su_CC_MEM_ZERO(su_S(su_u8*,su_S(void*,P))+su_FIELD_OFFSETOF(T,S), su_FIELD_RANGEOF(T,S,E))

/*! sizeof() for member fields. */
#define su_FIELD_SIZEOF(T,F) sizeof(su_S(T *,su_NIL)->F)

/* */
#ifdef su_HAVE_MT
# define su_MT(X) X /*!< Inject for \r{su_HAVE_MT}. */
#else
# define su_MT(X)
#endif

/*! Members in constant array. */
#define su_NELEM(A) (sizeof(A) / sizeof((A)[0]))

/*! NYD comes from code-{in,ou}.h (support like \r{su_nyd_chirp()} below).
 * Instrumented functions will always have one label for goto: purposes. */
#define su_NYD_OU_LABEL su__nydou

/*! Byte character to size_t cast. */
#define su_C2UZ(C) su_S(su_uz,su_S(su_u8,C))

/*! Pointer to size_t cast. */
#define su_P2UZ(X) su_S(su_uz,/*not R(): avoid same-type++ warns*/(su_up)(X))

/*! Pointer comparison. */
#define su_PCMP(A,C,B) (su_R(su_up,A) C su_R(su_up,B))

/* */
#ifdef su_HAVE_SMP
# define su_SMP(X) X /*!< Inject for \r{su_HAVE_SMP}. */
#else
# define su_SMP(X)
#endif

/*! Two indirections for graceful expansion. */
#define su_STRING(X) su__STRING_1(X)
# define su__STRING_1(X) su__STRING_2(X)
# define su__STRING_2(X) #X /* __STDC__||__STDC_VERSION__||su_C_LANG */

/*! Two indirections for graceful expansion. */
#define su_CONCAT(S1,S2) su__CONCAT_1(S1, S2)
# define su__CONCAT_1(S1,S2) su__CONCAT_2(S1, S2)
# define su__CONCAT_2(S1,S2) S1 ## S2 /*__STDC__||__STDC_VERSION__||su_C_LANG*/

/*! Zero an entire \a{T}ype instance at the given memory \a{P}ointer.
 * \remarks{Introduces a block scope.} */
#define su_STRUCT_ZERO(T,P) su_CC_MEM_ZERO(P, sizeof(T))

#if su_C_LANG || defined DOXYGEN
 /*! Compare (maybe mixed-signed) integers cases to \a{T} bits, unsigned, \a{T} is one of our homebrew integers,
  * for example, \c{UCMP(32, su_ABS(n), >, wleft)}.
  * \remarks{Does not sign-extend correctly, this is up to the caller.} */
# define su_UCMP(T,A,C,B) (su_S(su_ ## u ## T,A) C su_S(su_ ## u ## T,B))
#else
# define su_UCMP(T,A,C,B) (su_S(su_NSPC(su) u ## T,A) C su_S(su_NSPC(su) u ## T,B))
#endif

/*! Casts-away (*NOT* cast-away). */
#define su_UNCONST(T,P) su_R(T,su_R(su_up,su_S(void const*,P)))
/*! Casts-away (*NOT* cast-away). */
#define su_UNVOLATILE(T,P) su_R(T,su_R(su_up,su_S(void volatile*,P)))
/*! To avoid warnings with modern compilers for "char*i; *(s32_t*)i=;". */
#define su_UNALIGN(T,P) su_R(T,su_R(su_up,P))
#define su_UNXXX(T,C,P) su_R(T,su_R(su_up,su_S(C,P)))

/* Avoid "may be used uninitialized" warnings */
#if (defined NDEBUG && !(defined su_HAVE_DEBUG || defined su_HAVE_DEVEL)) || defined DOXYGEN
# define su_UNINIT(N,V) su_S(void,0) /*!< \_ */
# define su_UNINIT_DECL(V) /*!< \_ */
#else
# define su_UNINIT(N,V) N = V
# define su_UNINIT_DECL(V) = V
#endif

/*! Avoid "unused" warnings. */
#define su_UNUSED(X) ((void)(X))

#if (su_C_LANG && defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901l) || defined DOXYGEN
 /*! Variable-type size (with byte array at end). */
# define su_VFIELD_SIZE(X)
 /*! Variable-type size (with byte array at end). */
# define su_VSTRUCT_SIZEOF(T,F) sizeof(T)
#else
# define su_VFIELD_SIZE(X) ((X) == 0 ? sizeof(su_uz) : (su_S(su_sz,X) < 0 ? sizeof(su_uz) - su_ABS(X) : su_S(su_uz,X)))
# define su_VSTRUCT_SIZEOF(T,F) (sizeof(T) - su_FIELD_SIZEOF(T, F))
#endif

/* SUPPORT MACROS+ }}} */

/* We are ready to start using our own style */
#ifndef su_CC_SIZE_TYPE
# include <sys/types.h> /* TODO create config time script,.. */
#endif

#include <inttypes.h> /* TODO ..query infos and drop */
#include <limits.h> /* TODO ..those includes! */

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
#elif ULONG_MAX - 1 <= 0xFFFFFFFFu - 1
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
# undef su_CC_SIZE_TYPE
#else
typedef size_t su_uz; /*!< \_ */
#endif

#undef PRIuZ
#undef PRIdZ
#if (defined __STDC_VERSION__ && __STDC_VERSION__ +0 >= 199901l) || defined DOXYGEN
# define PRIuZ "zu"
# define PRIdZ "zd"
# define su_UZ_MAX SIZE_MAX /*!< \_ */
#elif defined SIZE_MAX
 /* UnixWare has size_t as unsigned as required but uses a signed limit constant (which is thus false!) */
# if SIZE_MAX == su_U64_MAX || SIZE_MAX == su_S64_MAX
#  define PRIuZ PRIu64
#  define PRIdZ PRId64
MCTA(sizeof(size_t) == sizeof(u64), "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
# elif SIZE_MAX == su_U32_MAX || SIZE_MAX == su_S32_MAX
#  define PRIuZ PRIu32
#  define PRIdZ PRId32
MCTA(sizeof(size_t) == sizeof(u32), "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
# else
#  error SIZE_MAX is neither su_U64_MAX nor su_U32_MAX (please report this)
# endif
# define su_UZ_MAX SIZE_MAX
#endif
#if !defined PRIuZ && !defined DOXYGEN
# define PRIuZ "lu"
# define PRIdZ "ld"
MCTA(sizeof(size_t) == sizeof(unsigned long), "Format string mismatch, compile with ISO C99 compiler (-std=c99)!")
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

#ifndef DOXYGEN
MCTA(sizeof(su_uz) == sizeof(void*), "SU cannot handle sizeof(su_uz) != sizeof(void*)")
#endif

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

/*! Byte order mark macro; there are also \r{su_bom}, \r{su_BOM_IS_BIG()} and \r{su_BOM_IS_LITTLE()}. */
#define su_BOM 0xFEFFu

/*! Log priorities, for simplicity of use without _LEVEL or _LVL prefix, for \r{su_log_set_level()}. */
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
	su__LOG_PRIMAX = su_LOG_DEBUG,
	su__LOG_PRISHIFT = 3,
	su_LOG_PRIMASK = (1u << su__LOG_PRISHIFT) - 1 /* xxx document? */
};
#ifndef DOXYGEN
MCTA(1u<<su__LOG_PRISHIFT > su__LOG_PRIMAX, "Bit ranges may not overlap")
#endif

/*! Flags that can be ORd to \r{su_log_level}. */
enum su_log_flags{
	/*! In a state in that recursive logging should be avoided at all cost this flag should be set. */
	su_LOG_F_CORE = 1u<<(su__LOG_PRISHIFT+0)
};

/*! Adjustment possibilities for the global log domain (for example, \r{su_log_write()}),
 * to be set via \r{su_state_set()}, to be queried via \r{su_state_has()}. */
enum su_state_log_flags{
	su_STATE_LOG_SHOW_LEVEL = 1u<<4, /*!< Prepend a message's \r{su_log_level}. */
	/*! Show the PID (Process IDentification number).
	 * This flag is only honoured if \r{su_program} set to non-\NIL. */
	su_STATE_LOG_SHOW_PID = 1u<<5
};

/* ..second byte: hardening errors.. */

/*! Global hardening for out-of-memory and integer etc. overflow: types.
 * By default out-of-memory situations, or container and string etc. insertions etc. which cause count/offset
 * datatype overflow result in \r{su_LOG_EMERG}s, and thus program abortion.
 *
 * This global default can be changed by including the corresponding \c{su_state_err_type} (\r{su_STATE_ERR_NOMEM} and
 * \r{su_STATE_ERR_OVERFLOW}, respectively), in the global \SU state machine via \r{su_state_set()}, as well as by
 * setting \r{su_STATE_ERR_PASS} generally, in which case logging uses \r{su_LOG_ALERT} level, a corresponding
 * \r{su_err_number} will be assigned for \r{su_err()}, and the failed function will return error.
 *
 * Often functions and objects allow additional control over the global on a by-call or by-object basis,
 * taking a state argument which consists of \c{su_state_err_type} and \r{su_state_err_flags} bits.
 * These are combinable bits in the second byte (bits 9 to 16, value 256 to 32768, inclusive). */
enum su_state_err_type{
	su_STATE_ERR_NOMEM = 1u<<8, /*!< Out-of-memory. */
	su_STATE_ERR_OVERFLOW = 1u<<9 /*!< Integer/xy domain overflow. */
};

/*! Hardening for out-of-memory and integer etc. overflow: adjustment flags.
 * Many functions offer the possibility to adjust the global \r{su_state_get()} (\r{su_state_has()})
 * \r{su_state_err_type} default on a per-call level, and object types (can) do so on a per-object basis.
 *
 * If so, the global state can (selectively) be bypassed by adding in \r{su_state_err_type}s to be ignored to an
 * (optional) function argument, or object control function or field.
 * It is also possible to instead enforce program abortion regardless of a global ignorance policy, and pass other
 * control flags. */
enum su_state_err_flags{
	su_STATE_ERR_NONE, /*!< 0. */
	/*! A mask containing all \r{su_state_err_type} bits. */
	su_STATE_ERR_TYPE_MASK = su_STATE_ERR_NOMEM | su_STATE_ERR_OVERFLOW,
	/*! Allow passing of all errors. */
	su_STATE_ERR_PASS = su_STATE_ERR_TYPE_MASK, /* .. but also includes custom */
	/*! Regardless of global (and additional local) policy, if this flag is
	 * set, an actual error causes a hard program abortion (via \r{su_LOG_EMERG}). */
	su_STATE_ERR_NOPASS = 1u<<12,
	/*! If this flag is set and no abortion is about to happen, a corresponding
	 * \r{su_err_number} will not be assigned to \r{su_err()}. */
	su_STATE_ERR_NOERROR = 1u<<13,
	/*! Special as it plays no role in the global state machine.
	 * Yet many types or functions that use \a{estate} arguments and use (NOT) \r{su_STATE_ERR_MASK} to overload
	 * that with meaning, adding support for owning \r{COLL} aka \r{su_toolbox} users, actually made sense:
	 * if set \NIL values are acceptable, and do not cause actions like insertion or removals to fail;
	 * with it, \NIL user pointers are not passed to \r{su_toolbox} members,
	 * and \r{su_toolbox} members may return \NIL, too. */
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
			su_STATE_ERR_PASS | su_STATE_ERR_NOPASS | su_STATE_ERR_NOERROR |
			su_STATE_ERR_NIL_IS_VALID_OBJECT
};

/* ..third byte: misc flags */

/*! State flags of \r{su_state_has()}. */
enum su_state_flags{
	su_STATE_NONE, /*!< No flag: this is 0. */
	su_STATE_DEBUG = 1u<<16, /*!< \_ */
	su_STATE_VERBOSE = 1u<<17, /*!< \_ */
	/*! With \r{su_HAVE_MT}: \r{SMP} via multi-threading shall be expected.
	 * \remarks{Only if this flag was set when \r{su_state_create_core()} has
	 * been called \SU will be able to work with multiple \r{THREAD}.} */
	su_STATE_MT = 1u<<18, /* TODO <> su_state_create_core() should act */
	/*! Reproducible behaviour switch.
	 * See \r{su_reproducible_build},
	 * and \xln{https://reproducible-builds.org}. */
	su_STATE_REPRODUCIBLE = 1u<<19
};

enum su__state_flags{
	/* enum su_log_level is first "member" */
	su__STATE_LOG_MASK = 0x0Fu,
	su__STATE_D_V = su_STATE_DEBUG | su_STATE_VERBOSE,
	su__STATE_CREATED = 1u<<24,
	/* What is not allowed in the global state machine */
	su__STATE_GLOBAL_MASK = (su__STATE_CREATED - 1) &
			~(su__STATE_LOG_MASK | (su_STATE_ERR_MASK & ~su_STATE_ERR_TYPE_MASK))
};
#ifndef DOXYGEN
MCTA(S(uz,su_LOG_DEBUG) <= S(uz,su__STATE_LOG_MASK), "Bit ranges may not overlap")
MCTA((S(uz,su_STATE_ERR_MASK) & ~0xFF00u) == 0, "Bits excess documented bounds")
#endif

/*! Argument bits for \r{su_state_create()}. */
enum su_state_create_flags{
	su_STATE_CREATE_RANDOM = 1u<<0, /*!< (V1) Initialize \r{RANDOM}, and seed its internal seeder object. */
	su__STATE_CREATE_RANDOM_MEM_FILLER = 1u<<1,
	/*! (V1) Create a random \r{su_MEM_CONF_FILLER_SET} (only with \r{su_MEM_ALLOC_DEBUG}).
	 * It favours \c{0x00} and \c{0xFF} over other random numbers.
	 * Implies \c{CREATE_RANDOM}.
	 * \remarks{As this creates a random number, a \r{su_RANDOM_SEED} of \c{su_RANDOM_SEED_HOOK} must be carefully
	 * written due to hen-and-egg.} */
	su_STATE_CREATE_RANDOM_MEM_FILLER = su_STATE_CREATE_RANDOM | su__STATE_CREATE_RANDOM_MEM_FILLER,
	su_STATE_CREATE_MD = 1u<<2, /*!< (V1) Initialize \r{MD}. */

	/* Exclusive cover-all's */
	su_STATE_CREATE_V1 = 1u<<27, /*!< (V1) All \r{su_VERSION} 1 subsystems. */
	su_STATE_CREATE_ALL = 15u<<27 /*!< All covered subsystems. */
};

/*! Argument bits for \r{su_state_gut()}. */
enum su_state_gut_flags{
	su_STATE_GUT_ACT_NORM, /*!< The value 0 for normal exit (see \r{su_STATE_GUT_ACT_MASK}). */
	/*! Normal exit, quick (see \r{su_STATE_GUT_ACT_MASK}).
	 * Does less, and a hint to the handlers to follow suit. */
	su_STATE_GUT_ACT_QUICK,
	su_STATE_GUT_ACT_CARE, /*!< Abnormal exit (see \r{su_STATE_GUT_ACT_MASK}). */
#if defined su_HAVE_STATE_GUT_FORK || defined DOXYGEN
	/*! State is destroyed after a child process has been spawned / forked / cloned, from within the child process.
	 * \remarks{This is problematic especially in true \r{su_HAVE_MT} aka threaded \r{su_HAVE_SMP} conditions.
	 * That is to say that the state of locks etc. cannot be guaranteed in a portable fashion (let alone easily),
	 * and simply destroying and giving back such resources seems unwise.
	 * It should be expected that calling \r{su_state_gut()} in this mode mostly leaves old resources laying around,
	 * and only resets some pointers to \NIL, so that a new \r{su_state_create()} cycle becomes possible.
	 * Because of this the \r{CONFIG} option \r{su_HAVE_STATE_GUT_FORK} is a precondition for this code path.} */
	su_STATE_GUT_ACT_FORK,
#endif /* su_HAVE_STATE_GUT_FORK || DOXYGEN */
	/* P.S.: code-{in,ou}. define su__STATE_ON_GUT_FUN */

	su_STATE_GUT_ACT_MASK = 0xF, /*!< A mask of all "actions". */

	su_STATE_GUT_NO_HANDLERS = 1u<<4, /*!< Do not call normal \r{su_state_on_gut_install()}ed handlers. */
	su_STATE_GUT_NO_FINAL_HANDLERS = 1u<<5, /*!< Do not call final \r{su_state_on_gut_install()}ed handlers. */

	/*! Library and called handlers should be aware that \r{SMP} locking may cause deadlocks, for example because
	 * multiple threads were running when one of them initiated program termination. */
	su_STATE_GUT_NO_LOCKS = 1u<<6,
	/*! Do not perform I/O, like flushing some streams etc.
	 * P.S.: general I/O will be flushed before handlers are called. */
	su_STATE_GUT_NO_IO = 1u<<7,

	/*! With \r{su_HAVE_DEVEL}, call \r{su_mem_trace()} as one of the last \SU statements for
	 * \r{su_STATE_GUT_ACT_NORM} invocations. */
	su_STATE_GUT_MEM_TRACE = 1u<<16
};

#ifdef su_USECASE_SU
enum su__glck_type{
	su__GLCK_STATE, /* su_state_set() lock */
	su__GLCK_GI9R, /* Global initializer (first-use-time-init) lock */
	su__GLCK_LOG, /* su_log_*write*() lock */
	su__GLCK_MAX = su__GLCK_LOG
};
#endif

/*! The \SU error number constants.
 * In order to achieve a 1:1 mapping of the \SU and the host value, for example, of \ERR{INTR} and \c{EINTR},
 * the actual values will be detected at compilation time.
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

/*! \SU exit status code constants.
 * They correspond in name, value and meaning the Berkely \c{sysexits.h} constants that are unchanged since the 1980s.
 * After \c{su_EX_ERR} there is a value hole that ends with the first Berkeley constant \c{su_EX_USAGE} (value 64),
 * the last is \c{su_EX_CONFIG} (78). */
enum su_ex_status{
	su_EX_OK = 0, /*!< Successful termination (this is 0). */
	su_EX_SUCCESS = su_EX_OK, /*!< Alias for \r{su_EX_OK}. */
	su_EX_ERR = 1, /*!< Failing termination, unspecified error (value 1). */
	su_EX_FAILURE = su_EX_ERR, /*!< Alias for \r{su_EX_ERR}. */
	su_EX_USAGE = 64, /*!< Command was used incorrectly. */
	su_EX_DATAERR = 65, /*!< User input data format error. */
	su_EX_NOINPUT = 66, /*!< Cannot open user input file / no user data. */
	su_EX_NOUSER = 67, /*!< User does not exist / addressee unknown. */
	su_EX_NOHOST = 68, /*!< Host name unknown. */
	su_EX_UNAVAILABLE = 69, /*!< Service unavailable / support file missing. */
	su_EX_SOFTWARE = 70, /*!< Internal software error. */
	su_EX_OSERR = 71, /*!< System error (for example, cannot fork). */
	su_EX_OSFILE = 72, /*!< OS file missing or data format error. */
	su_EX_CANTCREAT = 73, /*!< Cannot create (user specified) output file. */
	su_EX_IOERR = 74, /*!< Input/output error. */
	su_EX_TEMPFAIL = 75, /*!< Temporary failure; user is invited to retry. */
	su_EX_PROTOCOL = 76, /*!< Remote error in protocol. */
	su_EX_NOPERM = 77, /*!< Permission denied. */
	su_EX_CONFIG = 78 /*!< Configuration error. */
};

#if DVLOR(1, 0) || defined DOXYGEN
/*! Actions for \r{su_nyd_chirp()}. */
enum su_nyd_action{
	su_NYD_ACTION_ENTER, /*!< Function entry (once per function). */
	su_NYD_ACTION_LEAVE, /*!< Function leave (once per function). */
	su_NYD_ACTION_ANYWHERE /*!< Any place (but the other two). */
};
#endif

union su__bom_union{
	char bu_buf[2];
	u16 bu_val;
};

/*! See \r{su_state_on_gut_install()}. */
typedef void (*su_state_on_gut_fun)(BITENUM(u32,su_state_gut_flags) flags);

/*! See \r{su_log_set_write_fun()}.
 * \a{lvl_a_flags} is a bitmix of a \r{su_log_level} and \r{su_log_flags}.
 * \a{msg} is one line of \a{len} bytes (excluding the \NUL terminator;
 * and the final newline can be cancelled, actually).
 * \remarks{If \r{su_LOG_F_CORE} is set in \a{lvl_a_flags} recursively causing log messages should be avoided.} */
typedef void (*su_log_write_fun)(u32 lvl_a_flags, char const *msg, uz len);

/* Known endianness bom versions, see su_bom_little, su_bom_big */
EXPORT_DATA union su__bom_union const su__bom_little;
EXPORT_DATA union su__bom_union const su__bom_big;

/* (Not yet) Internal enum su_state_* bit carrier */
EXPORT_DATA uz su__state;

/*! The byte order mark \r{su_BOM} in host, \r{su_bom_little} and \r{su_bom_big} byte order.
 * The latter two are macros which access constant union data.
 * We also define two helpers \r{su_BOM_IS_BIG()} and \r{su_BOM_IS_LITTLE()}, which will expand to preprocessor
 * statements if possible (by using \r{su_CC_BOM}, \r{su_CC_BOM_LITTLE} and \r{su_CC_BOM_BIG}), but otherwise
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

/*! Usually set via \r{su_state_create_core()} to the name of the program, but may be set freely, for example to create
 * a common log message prefix.
 * Also see \r{su_STATE_LOG_SHOW_PID}, \r{su_STATE_LOG_SHOW_LEVEL}. */
EXPORT_DATA char const *su_program;

#ifdef su_USECASE_SU
EXPORT void su__glck(enum su__glck_type gt);
EXPORT void su__gnlck(enum su__glck_type gt);
# if !defined su_HAVE_MT && defined NDEBUG && !defined su_SOURCE_CORE_CODE
#  define su__glck(X) su_UNUSED(0)
#  define su__gnlck(X) su_UNUSED(0)
# endif
# define su__glck_gi9r() su__glck(su__GLCK_GI9R)
# define su__gnlck_gi9r() su__gnlck(su__GLCK_GI9R)
#endif

#ifndef su__glck_gi9r
# define su__glck(X) su_UNUSED(0)
# define su__gnlck(X) su_UNUSED(0)
# define su__glck_gi9r() su_UNUSED(0)
# define su__gnlck_gi9r() su_UNUSED(0)
#endif

/*! Initialize the \SU core (a more pleasant variant is \r{su_state_create()}).
 * If \a{name_or_nil} is given it will undergo a \c{basename(3)} operation and then be assigned to \r{su_program}.
 * \a{flags} may be a bitmix of what is allowed for \r{su_state_set()} and \r{su_log_set_level()}.
 * \ESTATE_RV; \r{su_STATE_ERR_NOPASS} might be of interest in particular.
 * Note \SU is not usable unless this returns \r{su_ERR_NONE}!
 * The following example initializes the library and emergency exits on error:
 *
 * \cb{
 *	su_state_create_core("StationToStation", (su_STATE_DEBUG | su_LOG_DEBUG),
 *		su_STATE_ERR_NOPASS);
 * }
 *
 * \remarks{This \b{must} be called \b{first}.
 * In threaded applications it must be called from the main thread of execution and before starting (\b{any}) threads,
 * even if, for example, \SU is only used in one specific worker thread.
 * For real MT \r{su_STATE_MT} is a required precondition.}
 *
 * \remarks{Dependent upon the actual configuration it may make use of native libraries and therefore cause itself
 * resource usage / initialization.} */
EXPORT s32 su_state_create_core(char const *name_or_nil, uz flags, u32 estate);

/*! Like \r{su_state_create_core()}, but initializes many more subsystems according to \a{create_flags}.
 * Many subsystems need internal machineries which are initialized when needed first, an operation that may fail.
 * Because of this the public interface may generate errors that need to be handled, which may be undesireable.
 * If this function is used instead of \r{su_state_create_core()}, then internals of a desired subset of subsystems
 * is initialized immediately, which asserts these errors cannot occur. */
EXPORT s32 su_state_create(BITENUM(u32,su_state_create_flags) create_flags, char const *name_or_nil, uz flags,
		u32 estate);

/*! Tear down \SU according to \a{flags}.
 * This should be called upon normal program termination, from within the main and single thread of execution,
 * but \a{flags} can be used for configuration.
 * \SU needs to be reinitialized after this has been called.
 * Also see \r{su_state_on_gut_install()}. */
EXPORT void su_state_gut(BITENUM(u32,su_state_gut_flags) flags);

/*! Install an \a{is_final} \a{hdl} to be called upon \r{su_state_gut()} time in last-in first-out order.
 * Final handlers execute after all (normal) handlers have been processed.
 * \r{su_state_gut()} will pass down its \r{su_state_gut_flags} argument to any handlers that is allowed to be called.
 * Nothing prevents handlers to be installed multiple times.
 * \ESTATE_RV.
 * \remarks{Final handlers should rely on as few as possible infrastructure, network, child processes, date with
 * timezone, locales, random, message digests, and many more facilities which may use dynamic memory etc., will
 * likely have performed cleanup already; do not use unless you cope.} */
EXPORT s32 su_state_on_gut_install(su_state_on_gut_fun hdl, boole is_final, u32 estate);

/*! Remove the \a{if_final} \a{hdl}, and return whether it was installed. */
EXPORT boole su_state_on_gut_uninstall(su_state_on_gut_fun hdl,boole is_final);

/*! Interaction with the SU library (global) state machine.
 * This covers \r{su_state_log_flags}, \r{su_state_err_type}, and \r{su_state_flags} flags and values. */
INLINE u32 su_state_get(void){
	return (su__state & su__STATE_GLOBAL_MASK);
}

/*! Interaction with the SU library (global) state machine:
 * test whether all (not any) of \a{flags} are set in \r{su_state_get()}. */
INLINE boole su_state_has(uz flags){
	flags &= su__STATE_GLOBAL_MASK;
	return ((su__state & flags) == flags);
}

/*! Adjustments of the SU library (global) state machine, see \r{su_state_get()}. */
INLINE void su_state_set(uz flags){ /* xxx not inline; no lock -> atomics? */
	flags &= su__STATE_GLOBAL_MASK;
	su__glck(su__GLCK_STATE);
	su__state |= flags;
	su__gnlck(su__GLCK_STATE);
}

/*! \copydoc{su_state_set()} */
INLINE void su_state_clear(uz flags){ /* xxx not inline; no lock -> atomics? */
	flags &= su__STATE_GLOBAL_MASK;
	flags = ~flags;
	su__glck(su__GLCK_STATE);
	su__state &= flags;
	su__gnlck(su__GLCK_STATE);
}

/*! Notify an error to the \SU (global) state machine.
 * Either \a{err}or is a \r{su_state_err_type}, or a negative \r{su_err_number}.
 * The \r{su_state_err_flags} \a{state} is evaluated in conjunction with \r{su_state_get()} to decide what to do.
 * Since individual (non-\r{su_state_err_type}) errors cannot be suppressed via states, either of \a{state} or
 * \r{su_state_get()} must flag \r{su_STATE_ERR_PASS} to make such errors non-fatal.
 * If this function returns, then with an according \r{su_err_number} (aka non-negated \a{err} itself). */
EXPORT s32 su_state_err(s32 err, BITENUM(uz,su_state_err_flags) state, char const *msg_or_nil);

/*! Get the \SU error number of the calling thread.
 * \remarks{For convenience we avoid the usual \c{_get_} name style.} */
EXPORT s32 su_err(void);

/*! Set the \SU error number of the calling thread. */
EXPORT void su_err_set(s32 eno);

/*! Return string(s) describing C error number \a{eno}, or \r{su_err()} if that is \c{-1}.
 * Effectively identical to \r{su_err_name()} if \r{su_state_has()} \r{su_STATE_REPRODUCIBLE} set. */
EXPORT char const *su_err_doc(s32 eno);

/*! Return the name of the given error number \a{eno}, or \r{su_err()} if that is \c{-1}.  */
EXPORT char const *su_err_name(s32 eno);

/*! Try to (case-insensitively) map an error name to an error number.
 * Returns the fallback error as a negative value if none found */
EXPORT s32 su_err_by_name(char const *name);

/*! Set the \SU error number of the calling thread to the value of the ISO C \c{errno} variable, and return it. */
EXPORT s32 su_err_by_errno(void);

/*! Get the currently installed \r{su_log_write_fun}.
 * The default is \NIL. */
EXPORT su_log_write_fun su_log_get_write_fun(void);

/*! The builtin log "domain" that is used by \SU by default logs to a system
 * dependent error channel, which might even be a "null device".
 * It can be hooked by passing a \r{su_log_write_fun}, passing \NIL restores the default behaviour.
 * See \r{su_log_write()} for more. */
EXPORT void su_log_set_write_fun(su_log_write_fun funp);

/*! \_ */
INLINE enum su_log_level su_log_get_level(void){
	return S(enum su_log_level,su__state & su__STATE_LOG_MASK);
}

/*! \_
 * Also see \r{su_state_get()}. */
INLINE void su_log_set_level(enum su_log_level nlvl){ /* XXX maybe not state */
	uz lvl;
	su__glck(su__GLCK_STATE);
	lvl = S(uz,nlvl) & su__STATE_LOG_MASK;
	su__state = (su__state & su__STATE_GLOBAL_MASK) | lvl;
	su__gnlck(su__GLCK_STATE);
}

/*! \_ */
INLINE boole su_log_would_write(enum su_log_level lvl){
	return ((S(u32,lvl) & su_LOG_PRIMASK) <= (su__state & su__STATE_LOG_MASK) || (su__state & su__STATE_D_V));
}

/*! Log functions of various sort.
 * The global log "domain" protects itself with \r{su_log_lock()}.
 * \a{lvl_a_flags} is a bitmix of a \r{su_log_level} and \r{su_log_flags}.
 * Regardless of the level these also log if \c{STATE_DEBUG|STATE_VERBOSE}.
 * If \r{su_program} is set it will be included in the per-line prefix that prepended to messages, optionally
 * supplemented by \r{su_STATE_LOG_SHOW_PID}.
 * The log level will be part of the prefix with \r{su_STATE_LOG_SHOW_LEVEL}.
 *
 * Control characters within \a{fmt} (but horizontal tabulator HT) will be normalized to space (SPACE), and a line feed
 * (LF) will be appended automatically; if \a{fmt} consists of multiple lines, each line will be logged by itself
 * separately.
 *
 * Printing of the initial prefix (if any) can be cancelled by placing the ECMA-48 control character CAN(cel,
 * octal 030 aka hexadecimal 0x18) as the first byte in \a{fmt}.
 * The automatically placed newline at the end of \a{fmt} can be CANcelled by placing CAN as the last byte.
 * \cb{
 *	su_log_write(su_LOG_WARN, "One line.");
 *	su_log_write(su_LOG_WARN, "One line.\nSecond line.");
 *	su_log_lock();
 *	su_log_write(su_LOG_WARN, "\030No prefix.\nSecond line,\030");
 *	su_log_write(su_LOG_WARN, "\030 to be continued");
 *	su_log_unlock();
 * } */
EXPORT void su_log_write(u32 lvl_a_flags, char const *fmt, ...);

/*! See \r{su_log_write()}.
 * \a{valp} is a \c{va_list*}, but the header is not included. */
EXPORT void su_log_vwrite(u32 lvl_a_flags, char const *fmt, void *valp);

/*! Like perror(3). */
EXPORT void su_perr(char const *msg, s32 eno_or_0);

/*! \r{su_STATE_MT} lock the global log domain. */
INLINE void su_log_lock(void){
	su__glck(su__GLCK_LOG);
}

/*! \r{su_STATE_MT} unlock the global log domain. */
INLINE void su_log_unlock(void){
	su__gnlck(su__GLCK_LOG);
}

#if !defined su_ASSERT_EXPAND_NOTHING || defined DOXYGEN
/*! With a \FAL0 crash this only logs.
 * If it survives it will \r{su_err_set()} \ERR{FAULT}.
 * \remarks{Define \c{su_ASSERT_EXPAND_NOTHING} in order to get rid of linkage and make it expand to a no-op macro.} */
EXPORT void su_assert(char const *expr, char const *file, u32 line, char const *fun, boole crash);
#else
# define su_assert(EXPR,FILE,LINE,FUN,CRASH)
#endif

#if DVLOR(1, 0) || defined DOXYGEN
/*! Control NYD for the calling thread, return former state.
 * When \a{disabled}, \r{su_nyd_chirp()} will return quick.
 * \remarks{Available only with \r{su_HAVE_DEVEL}.} */
EXPORT boole su_nyd_set_disabled(boole disabled);

/*! Reset \r{su_nyd_chirp()} recursion level of the calling thread.
 * In event-loop driven software that uses long jumps it may be desirable to reset the recursion level at times.
 * \a{nlvl} is only honoured when smaller than the current recursion level.
 * \remarks{Available only with \r{su_HAVE_DEVEL}.} */
EXPORT void su_nyd_reset_level(u32 nlvl);

/*! Not-yet-dead chirp of the calling thread.
 * Normally used from the support macros in code-{in,ou}.h when \vr{su_FILE} is defined.
 * Define \c{NYD_ENABLE} on per-file level, or \c{su_NYD_ENABLE} on global level, and injections will happen;
 * in this case \c{NYD2_ENABLE} and \c{su_NYD2_ENABLE} will also be inspected.
 * The same for \c{NYDPROF_ENABLE}, but this is currently not implemented.
 * \remarks{Available only with \r{su_HAVE_DEVEL}.} */
EXPORT void su_nyd_chirp(enum su_nyd_action act, char const *file, u32 line, char const *fun);

/*! Dump all existing not-yet-dead entries of the calling thread via \a{ptf}.
 * \a{buf} is \NUL terminated despite \a{blen} being passed, too.
 * \remarks{Available only with \r{su_HAVE_DEVEL}.} */
EXPORT void su_nyd_dump(void (*ptf)(up cookie, char const *buf, uz blen), up cookie);
#endif /* DVLOR(1,0) || DOXYGEN */

/* BASIC C INTERFACE (STATE) }}} */

/* BASIC TYPE TOOLBOX AND TRAITS {{{ */

struct su_toolbox;
/* plus PTF typedefs */

/*! Create a new default instance of an object type, return it or \NIL.
 * See \r{su_clone_fun} for the meaning of \a{estate}. */
typedef void *(*su_new_fun)(u32 estate);

/*! Create a clone of \a{t}, and return it.
 * \a{estate} might be set to some \r{su_state_err_type}s to be turned to non-fatal errors, and contain
 * \r{su_state_err_flags} with additional control requests.
 * Otherwise (\a{estate} is 0) \NIL can still be returned for \r{su_STATE_ERR_NOMEM} or \r{su_STATE_ERR_OVERFLOW},
 * dependent on the global \r{su_state_get()} / \r{su_state_has()} setting, as well as for other errors and with other
 * \r{su_err_number}s, of course.
 * Also see \r{su_STATE_ERR_NIL_IS_VALID_OBJECT}.
 *
 * Many \SU functions take an \a{estate} parameter with the same meaning as here.
 * Yet, many of those return a \r{su_s32}, which will be \r{su_ERR_NONE} upon success,
 * or a \r{su_err_number} (maybe \r{su_state_err()} mapped \r{su_state_err_type}) on error
 * (in \r{su_ASSERT()} enabled code even \ERR{FAULT} may happen);
 * The negative value range may have meaning on a use-case base.
 * Those which do not usually set \r{su_err()}. */
typedef void *(*su_clone_fun)(void const *t, u32 estate);

/*! Delete an instance returned by \r{su_new_fun} or \r{su_clone_fun} (or \r{su_assign_fun}). */
typedef void (*su_del_fun)(void *self);

/*! Assign \a{t}; see \r{su_clone_fun} for the meaning of \a{estate}.
 * In-place update of \SELF is (and should) not (be) assumed, but instead the return value has to be used, with the
 * exception as follows.
 * First all resources of \a{self} should be released (an operation which is not supposed to fail), then the assignment
 * be performed.
 * If this fails, \a{self} should be turned to cleared state again, and \NIL should be returned.
 *
 * \remarks{This function is not used by (object owning) \r{COLL} unless \r{su_STATE_ERR_NIL_IS_VALID_OBJECT} is set.
 * Regardless, if \NIL is returned to indicate error then the caller which passed a non-\NIL object is responsible for
 * deletion or taking other appropriate steps.}
 *
 * \remarks{If \a{self} and \a{t} are \r{COLL}, then if assignment fails then whereas \a{self} will not manage any
 * elements, it has been assigned \a{t}'s possible existent \r{su_toolbox} as well as other attributes.
 * Some \r{COLL} will provide an additional \c{assign_elems()} function.} */
typedef void *(*su_assign_fun)(void *self, void const *t, u32 estate);

/*! Compare \a{a} and \a{b}, and return a value less than 0 if \a{a} is \e less \e than \a{b}, 0 on equality, and a
 * value greater than 0 if \a{a} is \e greater \e than \a{b}. */
typedef sz (*su_cmp_fun)(void const *a, void const *b);

/*! Create a hash that reproducibly represents \SELF. */
typedef uz (*su_hash_fun)(void const *self);

/* Needs to be binary compatible with \c{su::{toolbox,type_toolbox<T>}}! */
/*! A toolbox provides object handling knowledge to \r{COLL}.
 * Also see \r{su_TOOLBOX_I9R()}. */
struct su_toolbox{
	su_clone_fun tb_clone; /*!< \copydoc{su_clone_fun}. */
	su_del_fun tb_del; /*!< \copydoc{su_del_fun}. */
	su_assign_fun tb_assign; /*!< \copydoc{su_assign_fun}. */
	su_cmp_fun tb_cmp; /*!< \copydoc{su_cmp_fun}. */
	su_hash_fun tb_hash; /*!< \copydoc{su_hash_fun}. */
};

/* Use C-style casts, not and ever su_R()! */
/*! Initialize a \r{su_toolbox}. */
#define su_TOOLBOX_I9R(CLONE,DELETE,ASSIGN,COMPARE,HASH) \
{\
	su_FIELD_INITN(tb_clone) R(su_clone_fun,CLONE),\
	su_FIELD_INITN(tb_del) R(su_del_fun,DELETE),\
	su_FIELD_INITN(tb_assign) R(su_assign_fun,ASSIGN),\
	su_FIELD_INITN(tb_cmp) R(su_cmp_fun,COMPARE),\
	su_FIELD_INITN(tb_hash) R(su_hash_fun,HASH)\
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
#ifndef DOXYGEN
MCTA(IS_POW2(sizeof(uz)), "Must be power of two")
MCTA(IS_POW2(su__ZAL_S), "Must be power of two")
MCTA(IS_POW2(su__ZAL_L), "Must be power of two")
#endif

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
class ex;
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
class err{ // {{{
public:
	/*! \copydoc{su_err_number}.
	 * \remarks{The C++ variant uses lowercased names; those with non-alphabetic first bytes gain an \c{e} prefix,
	 * for example \c{su_ERR_2BIG} becomes \c{err::e2big}}. */
	enum number{
#ifdef DOXYGEN
		none, /*!< No error. */
		notobacco /*!< No such errno, fallback: no mapping exists. */
#else
		su__CXX_ERR_NUMBER_ENUM
# undef su__CXX_ERR_NUMBER_ENUM
#endif
	};

	/*! \copydoc{su_err()} */
	static s32 get(void) {return su_err();}
	/*! \copydoc{su_err()} */
	static s32 no(void) {return su_err();}

	/*! \copydoc{su_err_set()} */
	static void set(s32 eno) {su_err_set(eno);}

	/*! \copydoc{su_err_doc()} */
	static char const *doc(s32 eno=-1) {return su_err_doc(eno);}

	/*! \copydoc{su_err_name()} */
	static char const *name(s32 eno=-1) {return su_err_name(eno);}

	/*! \copydoc{su_err_by_name()} */
	static s32 by_name(char const *name) {return su_err_by_name(name);}

	/*! \copydoc{su_err_by_errno()} */
	static s32 by_errno(void) {return su_err_by_errno();}
}; // }}}

/*! \_ */
class ex{ // {{{
public:
	/*! \copydoc{su_ex_status} */
	enum status{
		ok = su_EX_OK, /*!< \copydoc{su_EX_OK} */
		success = su_EX_SUCCESS, /*!< \copydoc{su_EX_SUCCESS} */
		err = su_EX_ERR, /*!< \copydoc{su_EX_ERR} */
		failure = su_EX_FAILURE, /*!< \copydoc{su_EX_FAILURE} */
		usage = su_EX_USAGE, /*!< \copydoc{su_EX_USAGE} */
		dataerr = su_EX_DATAERR, /*!< \copydoc{su_EX_DATAERR} */
		noinput = su_EX_NOINPUT, /*!< \copydoc{su_EX_NOINPUT} */
		nouser = su_EX_NOUSER, /*!< \copydoc{su_EX_NOUSER} */
		nohost = su_EX_NOHOST, /*!< \copydoc{su_EX_NOHOST} */
		unavailable = su_EX_UNAVAILABLE, /*!< \copydoc{su_EX_UNAVAILABLE} */
		software = su_EX_SOFTWARE, /*!< \copydoc{su_EX_SOFTWARE} */
		oserr = su_EX_OSERR, /*!< \copydoc{su_EX_OSERR} */
		osfile = su_EX_OSFILE, /*!< \copydoc{su_EX_OSFILE} */
		cantcreat = su_EX_CANTCREAT, /*!< \copydoc{su_EX_CANTCREAT} */
		ioerr = su_EX_IOERR, /*!< \copydoc{su_EX_IOERR} */
		tempfail = su_EX_TEMPFAIL, /*!< \copydoc{su_EX_TEMPFAIL} */
		protocol = su_EX_PROTOCOL, /*!< \copydoc{su_EX_PROTOCOL} */
		noperm = su_EX_NOPERM, /*!< \copydoc{su_EX_NOPERM} */
		config = su_EX_CONFIG /*!< \copydoc{su_EX_CONFIG} */
	};
}; // }}}

/*! \_ */
class log{ // {{{
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
		f_core = su_LOG_F_CORE /*!< \copydoc{su_LOG_F_CORE} */
	};

	/*! See \r{su_log_write_fun()}. */
	typedef void (*write_fun)(u32 lvl_a_flags, char const *msg, uz len);

	/*! \copydoc{su_log_get_write_fun()} */
	static write_fun get_write_fun(void) {return R(write_fun,su_log_get_write_fun());}

	/*! \copydoc{su_log_set_write_fun()} */
	static void set_write_fun(write_fun fun) {su_log_set_write_fun(R(su_log_write_fun,fun));}

	/*! \copydoc{su_log_get_level()} */
	static level get_level(void) {return S(level,su_log_get_level());}

	/*! \copydoc{su_log_set_level()} */
	static void set_level(level lvl) {su_log_set_level(S(su_log_level,lvl));}

	/*! \copydoc{su_STATE_LOG_SHOW_LEVEL} */
	static boole get_show_level(void) {return su_state_has(su_STATE_LOG_SHOW_LEVEL);}

	/*! \copydoc{su_STATE_LOG_SHOW_LEVEL} */
	static void set_show_level(boole on){
		if(on)
			su_state_set(su_STATE_LOG_SHOW_LEVEL);
		else
			su_state_clear(su_STATE_LOG_SHOW_LEVEL);
	}

	/*! \copydoc{su_STATE_LOG_SHOW_PID} */
	static boole get_show_pid(void) {return su_state_has(su_STATE_LOG_SHOW_PID);}

	/*! \copydoc{su_STATE_LOG_SHOW_PID} */
	static void set_show_pid(boole on){
		if(on)
			su_state_set(su_STATE_LOG_SHOW_PID);
		else
			su_state_clear(su_STATE_LOG_SHOW_PID);
	}

	/*! \copydoc{su_log_would_write()} */
	static boole would_write(level lvl) {return su_log_would_write(S(su_log_level,lvl));}

	/*! \copydoc{su_log_write()} */
	static void write(u32 lvl_a_flags, char const *fmt, ...);

	/*! \copydoc{su_log_vwrite()} */
	static void vwrite(u32 lvl_a_flags, char const *fmt, void *valp) {su_log_vwrite(lvl_a_flags, fmt, valp);}

	/*! \copydoc{su_perr()} */
	static void perr(char const *msg, s32 eno_or_0) {su_perr(msg, eno_or_0);}

	/*! \copydoc{su_log_lock()} */
	static void lock(void) {su_log_lock();}

	/*! \copydoc{su_log_unlock()} */
	static void unlock(void) {su_log_unlock();}
}; // }}}

/*! \_ */
class state{ // {{{
public:
	/*! \copydoc{su_state_err_type} */
	enum err_type{
		err_nomem = su_STATE_ERR_NOMEM, /*!< \copydoc{su_STATE_ERR_NOMEM} */
		err_overflow = su_STATE_ERR_OVERFLOW /*!< \copydoc{su_STATE_ERR_OVERFLOW} */
	};

	/*! \copydoc{su_state_err_flags} */
	enum err_flags{
		err_none = su_STATE_ERR_NONE, /*!< \copydoc{su_STATE_ERR_NONE} */
		err_type_mask = su_STATE_ERR_TYPE_MASK, /*!< \copydoc{su_STATE_ERR_TYPE_MASK} */
		err_pass = su_STATE_ERR_PASS, /*!< \copydoc{su_STATE_ERR_PASS} */
		err_nopass = su_STATE_ERR_NOPASS, /*!< \copydoc{su_STATE_ERR_NOPASS} */
		err_noerror = su_STATE_ERR_NOERROR, /*!< \copydoc{su_STATE_ERR_NOERROR} */
		err_mask = su_STATE_ERR_MASK /*!< \copydoc{su_STATE_ERR_MASK} */
	};

	/*! \copydoc{su_state_flags} */
	enum flags{
		none = su_STATE_NONE, /*!< \copydoc{su_STATE_NONE} */
		debug = su_STATE_DEBUG, /*!< \copydoc{su_STATE_DEBUG} */
		verbose = su_STATE_VERBOSE, /*!< \copydoc{su_STATE_VERBOSE} */
		reproducible = su_STATE_REPRODUCIBLE /*!< \copydoc{su_STATE_REPRODUCIBLE} */
	};

	/*! \copydoc{su_state_create_flags} */
	enum create_flags{
		create_random = su_STATE_CREATE_RANDOM, /*!< \copydoc{su_STATE_CREATE_RANDOM} */
		create_random_mem_filler = su_STATE_CREATE_RANDOM_MEM_FILLER, /*!< \copydoc{su_STATE_CREATE_RANDOM_MEM_FILLER} */
		create_md = su_STATE_CREATE_MD, /*!< \copydoc{su_STATE_CREATE_MD} */

		create_v1 = su_STATE_CREATE_V1, /*!< \copydoc{su_STATE_CREATE_V1} */
		create_all = su_STATE_CREATE_ALL /*!< \copydoc{su_STATE_CREATE_ALL} */
	};

	/*! \copydoc{su_state_gut_flags} */
	enum gut_flags{
		gut_act_norm = su_STATE_GUT_ACT_NORM, /*!< \copydoc{su_STATE_GUT_ACT_NORM} */
		gut_act_quick = su_STATE_GUT_ACT_QUICK, /*!< \copydoc{su_STATE_GUT_ACT_QUICK} */
		gut_act_care = su_STATE_GUT_ACT_CARE, /*!< \copydoc{su_STATE_GUT_ACT_CARE} */
#if defined su_HAVE_STATE_GUT_FORK || defined DOXYGEN
		gut_act_fork = su_STATE_GUT_ACT_FORK, /*!< \copydoc{su_STATE_GUT_ACT_FORK} */
#endif
		gut_act_mask = su_STATE_GUT_ACT_MASK, /*!< \copydoc{su_STATE_GUT_ACT_MASK} */

		gut_no_handlers = su_STATE_GUT_NO_HANDLERS, /*!< \copydoc{su_STATE_GUT_NO_HANDLERS} */
		gut_no_final_handlers = su_STATE_GUT_NO_FINAL_HANDLERS, /*!< \copydoc{su_STATE_GUT_NO_FINAL_HANDLERS} */
		gut_no_locks = su_STATE_GUT_NO_LOCKS, /*!< \copydoc{su_STATE_GUT_NO_LOCKS} */
		gut_no_io = su_STATE_GUT_NO_IO, /*!< \copydoc{su_STATE_GUT_NO_IO} */

		gut_mem_trace = su_STATE_GUT_MEM_TRACE /*!< \copydoc{su_STATE_GUT_MEM_TRACE} */
	};

	/*! See \r{su_state_on_gut_fun()}. */
	typedef void (*on_gut_fun)(BITENUM(u32,gut_flags) flags);

	/*! \copydoc{su_state_create_core()} */
	static s32 create_core(char const *program_or_nil, uz flags, u32 estate=none){
		return su_state_create_core(program_or_nil, flags, estate);
	}

	/*! \copydoc{su_state_create()} */
	static s32 create(BITENUM(u32,create_flags) create_flags, char const *program_or_nil, uz flags, u32 estate=none){
		return su_state_create(create_flags, program_or_nil, flags, estate);
	}

	/*! \copydoc{su_state_gut()} */
	static void gut(BITENUM(u32,gut_flags) flags=gut_act_norm) {su_state_gut(S(enum su_state_gut_flags,flags));}

	/*! \copydoc{su_state_get()} */
	static u32 get(void) {return su_state_get();}

	/*! \copydoc{su_state_has()} */
	static boole has(uz state) {return su_state_has(state);}

	/*! \copydoc{su_state_set()} */
	static void set(uz state) {su_state_set(state);}

	/*! \copydoc{su_state_clear()} */
	static void clear(uz state) {su_state_clear(state);}

	/*! \copydoc{su_state_err()} */
	static s32 err(s32 err, BITENUM(uz,err_flags) state=err_none, char const *msg_or_nil=NIL){
		return su_state_err(err, state, msg_or_nil);
	}

	/*! \copydoc{su_program} */
	static char const *get_program(void) {return su_program;}

	/*! \copydoc{su_program} */
	static void set_program(char const *name) {su_program = name;}
}; // }}}

/* BASIC C++ INTERFACE (STATE) }}} */

/* BASIC TYPE TOOLBOX AND TRAITS, SUPPORT {{{ */

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

/*! \remarks{Binary compatible with \r{toolbox} (a.k.a. \r{su_toolbox})!
 * Also see \r{COLL}.} */
template<class T>
struct type_toolbox{
	/*! \_ */
	typedef NSPC(su)type_traits<T> type_traits;

	/*! \copydoc{su_clone_fun} */
	typedef typename type_traits::tp (*clone_fun)(typename type_traits::tp_const t, u32 estate);
	/*! \copydoc{su_del_fun} */
	typedef void (*del_fun)(typename type_traits::tp self);
	/*! \copydoc{su_assign_fun} */
	typedef typename type_traits::tp (*assign_fun
		)(typename type_traits::tp self, typename type_traits::tp_const t, u32 estate);
	/*! \copydoc{su_cmp_fun} */
	typedef sz (*cmp_fun)(typename type_traits::tp_const self, typename type_traits::tp_const t);
	/*! \copydoc{su_hash_fun} */
	typedef uz (*hash_fun)(typename type_traits::tp_const self);

	/*! \r{#clone_fun} */
	clone_fun ttb_clone;
	/*! \r{#del_fun} */
	del_fun ttb_del;
	/*! \r{#assign_fun} */
	assign_fun ttb_assign;
	/*! \r{#cmp_fun} */
	cmp_fun ttb_cmp;
	/*! \r{#hash_fun} */
	hash_fun ttb_hash;
};

/*! Initialize a \r{type_toolbox}. */
#define su_TYPE_TOOLBOX_I9R(CLONE,DELETE,ASSIGN,COMPARE,HASH) {CLONE, DELETE, ASSIGN, COMPARE, HASH}

// abc,clip,max,min,pow2 -- the C macros are in SUPPORT MACROS+
/*! \_ */
template<class T> inline T get_abs(T const &a) {return su_ABS(a);}

/*! \copydoc{su_CLIP()} */
template<class T>
inline T const &get_clip(T const &a, T const &min, T const &max) {return su_CLIP(a, min, max);}

/*! \copydoc{su_MAX()} */
template<class T>
inline T const &get_max(T const &a, T const &b) {return su_MAX(a, b);}

/*! \copydoc{su_MIN()} */
template<class T>
inline T const &get_min(T const &a, T const &b) {return su_MIN(a, b);}

/*! \copydoc{su_ROUND_DOWN()} */
template<class T>
inline T const &get_round_down(T const &a, T const &b) {return su_ROUND_DOWN(a, b);}

/*! \copydoc{su_ROUND_DOWN2()} */
template<class T>
inline T const &get_round_down2(T const &a, T const &b) {return su_ROUND_DOWN2(a, b);}

/*! \copydoc{su_ROUND_UP()} */
template<class T>
inline T const &get_round_up(T const &a, T const &b) {return su_ROUND_UP(a, b);}

/*! \copydoc{su_ROUND_UP2()} */
template<class T>
inline T const &get_round_up2(T const &a, T const &b) {return su_ROUND_UP2(a, b);}

/*! \copydoc{su_IS_POW2()} */
template<class T> inline int is_pow2(T const &a) {return su_IS_POW2(a);}

/* BASIC TYPE TOOLBOX AND TRAITS, SUPPORT }}} */

NSPC_END(su)
#include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */

/* MORE DOXYGEN TOP GROUPS {{{ */
/*!
 * \defgroup COLL Collections
 * \brief Collections
 *
 * In \SU, and by default, collections learn how to deal with managed objects through \r{su_toolbox} objects.
 * (For behaviour peculiarities see \r{su_clone_fun} and \r{su_assign_fun}.)
 *
 * The C++ variants deduce many more things, and automatically, through (specializations of) \r{type_traits},
 * \r{type_toolbox}, and \r{auto_type_toolbox}.
 * Because the C++ versions are template wrappers around their \c{void*} based C "supertypes", it is inefficient or
 * even impossible to use \SU collections for plain-old-data; to overcome this restriction (some) specializations to
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
 * This covers general \r{su_HAVE_SMP}, as well as its multi-threading subset \r{su_HAVE_MT}.
 *
 * \remarks{Many facilities in this group are available even if \r{su_HAVE_MT} is not available:
 * they expand to no-op inline dummies, then.}
 *
 * \head1{Object initialization issues}
 *
 * In most real \r{su_HAVE_MT} cases the \SU objects are backed by native implementations, the initialization of which
 * is highly backend dependent.
 * Since \SU offers initialization macros like \r{su_MUTEX_I9R()} true resource aquisition might be performed upon
 * first object functionality usage (for example, mutex locking).
 * If initialization macros are used a resource aquisition failure results in abortion via \r{su_LOG_EMERG} log.
 * If that is not acceptable the normal object \c{_create()} (see \r{index}) function has to be used.
 */

/*!
 * \defgroup TEXT Text
 * \brief Text
 */
/* MORE DOXYGEN TOP GROUPS }}} */

/*! @} */
#endif /* !su_CODE_H */
/* s-itt-mode */
