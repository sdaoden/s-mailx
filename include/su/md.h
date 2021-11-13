/*@ Message Digests and Authentication Codes.
 *
 * Copyright (c) 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_MD_H
#define su_MD_H

/*!
 * \file
 * \ingroup MD
 * \brief \r{MD}
 */

#include <su/code.h>

su_USECASE_MX_DISABLED
#if defined su_HAVE_MD || defined DOXYGEN /*XXX DOXYGEN bug; ifdef su_HAVE_MD*/

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_md;

/* su_md {{{ */
/*!
 * \defgroup MD Message Digests and Authentication Codes
 * \ingroup TEXT
 * \brief Message Digests and Authentication Codes (\r{su/md.h})
 *
 * To make it plain the difference in between MD and MAC is that the latter
 * algorithms make use of a (secret) key, whereas digests are reproducable.
 *
 * \remarks{Only available if \r{su_HAVE_MD} is defined.}
 * @{
 */

/*! \_ */
enum su_md_algo{
   su_MD_ALGO_SIPHASH, /*!< \r{MD_SIPHASH}. */
   su_MD_ALGO_EXTRA /*!< Any other, non-builtin algorithm. */
};

/*! List of properties.
 * Unless otherwise noted these are \r{su_uz}. */
enum su_md_prop{
   /*! A \r{su_md_algo}, therefore always \r{su_MD_ALGO_EXTRA} but for
    * (truly aka configure-time enabled) built-in algorithms. */
   su_MD_PROP_ALGO,
   /*! The name as we use it; this is a \c{char const*}. */
   su_MD_PROP_NAME,
   /*! The name for display purposes; this is a \c{char const*}. */
   su_MD_PROP_DISPLAY_NAME,
   su_MD_PROP_KEY_SIZE_MIN, /*!< \_ */
   su_MD_PROP_KEY_SIZE_MAX, /*!< \_ */
   su_MD_PROP_DIGEST_SIZE_MIN, /*!< \_ */
   su_MD_PROP_DIGEST_SIZE_MAX, /*!< \_ */
   su_MD_PROP_BLOCK_SIZE /*!< \_ */
};

/*! \_ */
struct su_md_vtbl{
   su_new_fun mdvtbl_new; /*!< \_ */
   su_del_fun mdvtbl_del; /*!< \_ */
   /*! Return \c{((up)-1)} on error. */
   up (*mdvtbl_property)(void const *self, enum su_md_prop prop);
   /*! The parameters are only debug-asserted, lengths not at all. */
   s32 (*mdvtbl_setup)(void *self, void const *key, uz key_len,
         uz digest_size);
   /*! Only called for \a{dat_len} greater-than 0. */
   void (*mdvtbl_update)(void *self, void const *dat, uz dat_len);
   /*! \a{store} must adhere to the chosen \c{digest_size}. */
   void (*mdvtbl_end)(void *self, void *store);
};

/*! \_ */
struct su_md{
   struct su_md_vtbl const *md_vtbl;
   void *md_vp;
};

EXPORT s32 su__md_install(char const *name, struct su_md_vtbl const *vtblp,
      su_new_fun cxx_it, u32 estate);
EXPORT boole su__md_uninstall(char const *name,
      struct su_md_vtbl const *vtblp, su_new_fun cxx_it);

/*! \ESTATE; \NIL if \a{algo} is unsupported (\ERR{NOTSUP}) or upon failure. */
EXPORT struct su_md *su_md_new_by_algo(enum su_md_algo algo, u32 estate);

/*! Name is compared case-insensitively (ASCII).
 * \ESTATE; \NIL if \a{name} is not supported or upon failure. */
EXPORT struct su_md *su_md_new_by_name(char const *name, u32 estate);

/*! \copydoc{su_md_vtbl::mdvtbl_del} */
EXPORT void su_md_del(struct su_md *self);

/*! Fetch the \r{su_md_prop}erty \a{prop}, or \c{(su_up)-1} on error.
 * Proper casting of the return value is up to the caller. */
INLINE up su_md_property(struct su_md const *self, enum su_md_prop prop){
   ASSERT(self);
   return (*self->md_vtbl->mdvtbl_property)(self->md_vp, prop);
}

/*! Convenience \r{su_MD_PROP_NAME} property fetcher. */
INLINE char const *su_md_name(struct su_md const *self){
   ASSERT(self);
   return R(char const*,su_md_property(self, su_MD_PROP_NAME));
}

/*! Convenience \r{su_MD_PROP_DISPLAY_NAME} property fetcher. */
INLINE char const *su_md_display_name(struct su_md const *self){
   ASSERT(self);
   return R(char const*,su_md_property(self, su_MD_PROP_DISPLAY_NAME));
}

/*! Returns \ERR{INVAL} if \a{digest_size} is invalid, \ERR{NOTSUP} if keys
 * are not supported and a key has been passed, or if keys are supported and
 * \a{key_len} is invalid.
 * Any former state is forgotten.
 * This function must be called before \r{su_md_update()} and \r{su_md_end()}
 * are usable. */
INLINE s32 su_md_setup(struct su_md *self, void const *key, uz key_len,
      uz digest_size){
   ASSERT(self);
   ASSERT_RET(key_len == 0 || key != NIL, -su_ERR_FAULT);
   return (*self->md_vtbl->mdvtbl_setup)(self->md_vp, key, key_len,
            digest_size);
}

/*! \a{dat} may be \NIL if \a{dat_len} is 0. */
INLINE void su_md_update(struct su_md *self, void const *dat, uz dat_len){
   ASSERT(self);
   ASSERT_RET_VOID(dat_len == 0 || dat != NIL);
   if(dat_len > 0)
      (*self->md_vtbl->mdvtbl_update)(self->md_vp, dat, dat_len);
}

/*! Finalize the digest and copy it to \a{store}.
 * Once this returns a new \r{su_md_setup()} must be started. */
INLINE void su_md_end(struct su_md *self, void *store){
   ASSERT(self);
   ASSERT_RET_VOID(store != NIL);
   (*self->md_vtbl->mdvtbl_end)(self->md_vp, store);
}

/*! Install a new algorithm \a{name}.
 * \a{name} must match what \a{vtblp} returns for \r{su_MD_PROP_NAME},
 * and must remain accessible (until \r{su_md_uninstall()} time).
 * \ESTATE_RV.
 * Nothing prevents multiple installations for and of \a{name} and/or
 * \a{vtblp}; the last one installed will be found first. */
INLINE s32 su_md_install(char const *name, struct su_md_vtbl const *vtblp,
      u32 estate){
   ASSERT_RET(name != NIL, -su_ERR_FAULT);
   ASSERT_RET(vtblp != NIL, -su_ERR_FAULT);
   return su__md_install(name, vtblp, NIL, estate);
}

/*! Uninstall \a{vtblp} for \a{name} again, returns whether it was installed.
 * Asserting that no object uses \a{vtblp} is up to the caller. */
INLINE boole su_md_uninstall(char const *name,
      struct su_md_vtbl const *vtblp){
   ASSERT_RET(name != NIL, FAL0);
   ASSERT_RET(vtblp != NIL, FAL0);
   return su__md_uninstall(name, vtblp, NIL);
}
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class md;

/* md {{{ */
/*!
 * \ingroup MD
 * C++ variant of \r{MD} (\r{su/md.h})
 */
class md{
   su_CLASS_NO_COPY(md);
public:
   /*! \copydoc{su_md_algo} */
   enum algo{
      algo_siphash = su_MD_ALGO_SIPHASH, /*!< \copydoc{su_MD_ALGO_SIPHASH} */
      algo_extra = su_MD_ALGO_EXTRA /*!< \copydoc{su_MD_ALGO_EXTRA} */
   };

   /*! \copydoc{su_md_prop} */
   enum prop{
      prop_algo = su_MD_PROP_ALGO, /*!< \copydoc{su_MD_PROP_ALGO} */
      prop_name = su_MD_PROP_NAME, /*!< \copydoc{su_MD_PROP_NAME} */
      /*! \copydoc{su_MD_PROP_DISPLAY_NAME} */
      prop_display_name = su_MD_PROP_DISPLAY_NAME,
      /*! \copydoc{su_MD_PROP_KEY_SIZE_MIN} */
      prop_key_size_min = su_MD_PROP_KEY_SIZE_MIN,
      /*! \copydoc{su_MD_PROP_KEY_SIZE_MAX} */
      prop_key_size_max = su_MD_PROP_KEY_SIZE_MAX,
      /*! \copydoc{su_MD_PROP_DIGEST_SIZE_MIN} */
      prop_digest_size_min = su_MD_PROP_DIGEST_SIZE_MIN,
      /*! \copydoc{su_MD_PROP_DIGEST_SIZE_MAX} */
      prop_digest_size_max = su_MD_PROP_DIGEST_SIZE_MAX,
      /*! \copydoc{su_MD_PROP_BLOCK_SIZE} */
      prop_block_size = su_MD_PROP_BLOCK_SIZE
   };

   /*! \NOOP; \r{setup()} is real constructor. */
   md(void) {}

   /*! \copydoc{su_md_del()} */
   virtual ~md(void) {}

   /*! \copydoc{su_md_property()} */
   virtual up property(prop prop) const = 0;

   /*! \copydoc{su_md_name()} */
   char const *name(void) const {return R(char const*,property(prop_name));}

   /*! \copydoc{su_md_display_name()} */
   char const *display_name(void) const{
      return R(char const*,property(prop_display_name));
   }

   /*! \copydoc{su_md_setup()}
    * For the implementation: \copydoc{su_md_vtbl::mdvtbl_setup} */
   virtual s32 setup(void const *key, uz key_len, uz digest_size) = 0;

   /*! \copydoc{su_md_update()}
    * For the implementation: \copydoc{su_md_vtbl::mdvtbl_update} */
   virtual void update(void const *dat, uz dat_len) = 0;

   /*! \copydoc{su_md_end()}
    * For the implementation: \copydoc{su_md_vtbl::mdvtbl_end} */
   virtual void end(void *store) = 0;

   /*! \copydoc{su_md_new_by_algo()} */
   static md *new_by_algo(algo algo, u32 estate=state::none);

   /*! \copydoc{su_md_new_by_name()} */
   static md *new_by_name(char const *name, u32 estate=state::none);

   /*! \copydoc{su_md_install()}
    * The C++ implementation will be usable from C code, too;
    * and see \a{ctor} matches prototype of \r{su_new_fun}. */
   static s32 install(char const *name, md *(*ctor)(u32 estate), u32 estate);

   /*! \copydoc{su_md_uninstall()} */
   static boole uninstall(char const *name, md *(*ctor)(u32 estate));
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_HAVE_MD */
#endif /* su_MD_H */
/* s-it-mode */
