/*@ SipHash pseudorandom function (message authentication code).
 *
 * Copyright (c) 2021 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC AND CC0-1.0
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
#ifndef su_MD_SIPHASH_H
#define su_MD_SIPHASH_H

/*!
 * \file
 * \ingroup MD_SIPHASH
 * \brief \r{MD_SIPHASH}
 */

#include <su/code.h>
#if defined su_HAVE_MD || defined DOXYGEN /*XXX DOXYGEN bug; ifdef su_HAVE_MD*/

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_siphash;

/* su_siphash {{{ */
/*!
 * \defgroup MD_SIPHASH SipHash pseudorandom function
 * \ingroup MD
 * \brief SipHash pseudorandom function (\r{su/md-siphash.h})
 *
 * SipHash is a family of pseudorandom functions (PRFs) optimized for speed on
 * short messages.
 * SipHash can also be used as a secure message authentication code (MAC).
 * SipHash was designed in 2012 by
 * Jean-Philippe Aumasson (\xln{https://aumasson.jp}) and
 * Daniel J. Bernstein (\xln{http://cr.yp.to});
 * see \xln{https://github.com/veorq/SipHash.git}.
 * \c{SPDX-License-Identifier: CC0-1.0}.
 *
 * \remarks{Only available if \r{su_HAVE_MD} is defined.}
 * @{
 */

enum{
   su_SIPHASH_KEY_SIZE = 16u, /*!< \_ */
   su_SIPHASH_KEY_SIZE_MIN = su_SIPHASH_KEY_SIZE, /*!< \_ */
   su_SIPHASH_KEY_SIZE_MAX = su_SIPHASH_KEY_SIZE, /*!< \_ */
   su_SIPHASH_DIGEST_SIZE_64 = 8u, /*!< \_ */
   su_SIPHASH_DIGEST_SIZE_128 = 16u, /*!< \_ */
   su_SIPHASH_DIGEST_SIZE_MIN = su_SIPHASH_DIGEST_SIZE_64, /*!< \_ */
   su_SIPHASH_DIGEST_SIZE_MAX = su_SIPHASH_DIGEST_SIZE_128, /*!< \_ */
   su_SIPHASH_BLOCK_SIZE = 8u, /*!< \_ */
   su__SIPHASH_DEFAULT_CROUNDS = 2,
   su__SIPHASH_DEFAULT_DROUNDS = 4
};

/*! \_ */
enum su_siphash_digest{
   su_SIPHASH_DIGEST_64, /*!< 64-bit digest type. */
   su_SIPHASH_DIGEST_128 /*!< 128-bit digest type. */
};

/*! \_ */
struct su_siphash{
   BITENUM_IS(u8,su_siphash_digest) sh_digest; /*!< \_ */
   u8 sh_compress_rounds; /*!< \_ */
   u8 sh_finalize_rounds; /*!< \_ */
   u8 sh__pad[1];
   u32 sh_carry_size;
   u64 sh_bytes;
   u64 sh_v0, sh_v1, sh_v2, sh_v3;
   u8 sh_carry[su_SIPHASH_BLOCK_SIZE];
};

/*! Setup a customized SipHash context.
 * \a{key} must be \r{su_SIPHASH_KEY_SIZE} bytes.
 * \a{crounds} and \a{drounds} may be given as 0, in which case the default
 * values (2 and 4, respectively) are used.
 * Returns \r{su_STATE_NONE}. */
EXPORT s32 su_siphash_setup_custom(struct su_siphash *self,
      void const *key, enum su_siphash_digest digest_size,
      u8 crounds, u8 drounds);

/*! Setup a default SipHash context (64-bit digest, 2 crounds, 4 drounds)
 * via \r{su_siphash_setup_custom()}. */
INLINE s32 su_siphash_setup(struct su_siphash *self, void const *key){
   ASSERT(self);
   return su_siphash_setup_custom(self, key, su_SIPHASH_DIGEST_64,
      su__SIPHASH_DEFAULT_CROUNDS, su__SIPHASH_DEFAULT_DROUNDS);
}

/*! Feed in more data into the setup context.
 * It is advisable to feed multiples of \r{su_SIPHASH_BLOCK_SIZE} bytes. */
EXPORT void su_siphash_update(struct su_siphash *self,
      void const *dat, uz dat_len);

/*! Calculate the digest and place it in \a{digest_store}, which must be large
 * enough to hold the chosen digest size. */
EXPORT void su_siphash_end(struct su_siphash *self, void *digest_store);

/*! The plain algorithm from setup to finalization, with \a{crounds} (2 if 0)
 * compression and \a{drounds} (4 if 0) finalization rounds.
 * \a{digest_store} must be large enough to hold the chosen digest size.
 * \a{key} must be \r{su_SIPHASH_KEY_SIZE} bytes.
 * \a{dat} can be \NIL if \a{dat_len} is 0. */
EXPORT void su_siphash_once(void *digest_store,
      enum su_siphash_digest digest_type, void const *key,
      void const *dat, uz dat_len, u8 crounds, u8 drounds);
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class siphash;

/* siphash {{{ */
/*!
 * \ingroup MD_SIPHASH
 * C++ variant of \r{MD_SIPHASH} (\r{su/md-siphash.h})
 *
 * \remarks{Debug assertions are performed in the C base only.}
 */
class siphash : private su_siphash{
   friend class cs; // Upcast capability
public:
   enum{
      /*! \copydoc{su_SIPHASH_KEY_SIZE} */
      key_size = su_SIPHASH_KEY_SIZE,
      /*! \copydoc{su_SIPHASH_KEY_SIZE_MIN} */
      key_size_min = su_SIPHASH_KEY_SIZE_MIN,
      /*! \copydoc{su_SIPHASH_KEY_SIZE_MAX} */
      key_size_max = su_SIPHASH_KEY_SIZE_MAX,

      /*! \copydoc{su_SIPHASH_DIGEST_SIZE_64} */
      digest_size_64 = su_SIPHASH_DIGEST_SIZE_64,
      /*! \copydoc{su_SIPHASH_DIGEST_SIZE_128} */
      digest_size_128 = su_SIPHASH_DIGEST_SIZE_128,
      /*! \copydoc{su_SIPHASH_DIGEST_SIZE_MIN} */
      digest_size_min = su_SIPHASH_DIGEST_SIZE_MIN,
      /*! \copydoc{su_SIPHASH_DIGEST_SIZE_MAX} */
      digest_size_max = su_SIPHASH_DIGEST_SIZE_MAX,

      /*! \copydoc{su_SIPHASH_BLOCK_SIZE} */
      block_size = su_SIPHASH_BLOCK_SIZE
   };

   /*! \copydoc{su_siphash_digest} */
   enum digest{
      /*! \copydoc{su_SIPHASH_DIGEST_64} */
      digest_64 = su_SIPHASH_DIGEST_64,
      /*! \copydoc{su_SIPHASH_DIGEST_128} */
      digest_128 = su_SIPHASH_DIGEST_128
   };

   /*! \NOOP */
   siphash(void) {DBG( STRUCT_ZERO(su_siphash, this); )}

   /*! \_ */
   siphash(siphash const &t) {*this = t;}

   /*! \NOOP */
   ~siphash(void) {}

   /*! \_ */
   siphash &operator=(siphash const &t){
      *S(su_siphash*,this) = t;
      return *this;
   }

   /*! \copydoc{su_siphash_setup()} */
   s32 setup(void const *key) {return su_siphash_setup(this, key);}

   /*! \copydoc{su_siphash_setup_custom()} */
   s32 setup(void const *key, digest digest_size, u8 crounds=0, u8 drounds=0){
      return su_siphash_setup_custom(this, key,
         S(su_siphash_digest,digest_size), crounds, drounds);
   }

   /*! \copydoc{su_siphash::sh_digest} */
   digest digest(void) const {return S(enum digest,sh_digest);}

   /*! \_ */
   u8 digest_size(void) const{
      return (digest() == digest_128) ? digest_size_128 : digest_size_64;
   }

   /*! \copydoc{su_siphash::sh_compress_rounds} */
   u8 compress_rounds(void) const {return sh_compress_rounds;}

   /*! \copydoc{su_siphash::sh_finalize_rounds} */
   u8 finalize_rounds(void) const {return sh_finalize_rounds;}

   /*! \copydoc{su_siphash_update()} */
   void update(void const *dat, uz dat_len){
      su_siphash_update(this, dat, dat_len);
   }

   /*! \copydoc{su_siphash_end()} */
   void end(void *digest_store) {su_siphash_end(this, digest_store);}

   /*! \copydoc{su_siphash_once()}
    * \remarks{For default argument policies the order is different to C.} */
   static void once(void *digest_store, void const *key,
         void const *dat, uz dat_len,
         enum digest digest_type=digest_64, u8 crounds=0, u8 drounds=0){
      su_siphash_once(digest_store, S(su_siphash_digest,digest_type),
         key, dat, dat_len, crounds, drounds);
   }
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_HAVE_MD */
#endif /* su_MD_SIPHASH_H */
/* s-it-mode */
