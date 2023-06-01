/*@ Random number generator.
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
#ifndef su_RANDOM_H
#define su_RANDOM_H

/*!
 * \file
 * \ingroup RANDOM
 * \brief \r{RANDOM}
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_random;

/* random {{{ */
/*!
 * \defgroup RANDOM Random number generator
 * \ingroup MISC
 * \brief Random number generator (\r{su/random.h})
 *
 * \head2{Biased numbers}
 *
 * If random numbers are to be generated with an upper limit that cannot be served by multiples of 8-bit the unused
 * top bits are usually removed with a remainder operations, for example for numbers 0 to 9 via \c{random % 10}.
 * The following is by M.E. O'Neill, \xln{https://www.pcg-random.org/posts/bounded-rands.html}:
 *
 * \pb{
 * To understand why rand() % 52 produces biased numbers, [.]
 * observe that 52 does not perfectly divide 2^32, it divides it 82,595,524 times with remainder 48.
 * Meaning that if we use rand() % 52, there will be 82,595,525 ways to select the first 48 cards from our 52-card deck
 * and only 82,595,524 ways to select the final four cards. [.]
 * For a 32-bit PRNG, a bounded range of less than 2^24 has a bias of less than 0.5% but above 2^31 the bias is 50% -
 * some numbers will occur half as often as others.
 * }
 *
 * The modulo bias can be avoided by rejecting random values which lie inside the remainder window of
 * \c{0 .. 2**32 % limit};  Again M.E. O'Neill:
 *
 * \pb{
 * Our code for this version uses a trick to divide 2^32 by range without using any 64-bit math. [.]
 * We observe that for unsigned integers, unary negation of range calculates the positive value 2^32 - range;
 * dividing this value by range will produce an answer one less than 2**32 / range.
 * }
 *
 * This leads us to the following code:
 *
 * \cb{
 *	u32 r, t = -range % range;
 *	for(;;)
 *		if((r = rng()) >= t)
 *			break;
 *		return r % range;
 *	}
 * @{
 */

/*! \_ */
enum su_random_type{
	/*! No type (value 0); not in practice but if \r{su_random_create()} fails
	 * (so \r{su_random_gut()} can do the right thing). */
	su_RANDOM_TYPE_NONE,
	/*! Reproducible pseudo random number generator (fast).
	 * Mixed output bytes of a permutating ARC4 pool.
	 * Uses simplified seeding: this type generates reproducible sequences of random numbers. */
	su_RANDOM_TYPE_R,
	/*! Pseudo random number generator (fast).
	 * Mixed output bytes of a permutating ARC4 pool.
	 * Uses simplified seeding, but ARC4 pool access offsets and an initial permutation count are randomized. */
	su_RANDOM_TYPE_P,
	/*! Strong pseudo (slower).
	 * Message digests of mixed output of a permutating ARC4 pool \r{su_random_seed()}ed by the seeder object.
	 * Periodic reseeding can be enabled via \r{su_random::rm_reseed_after}.
	 * Currently uses \r{MD_SIPHASH}-2-4, with only key set (16 bytes input, up to 16 bytes output). */
	su_RANDOM_TYPE_SP,
	/*! Very strong pseudo (even slower).
	 * Message digests of mixed output of a permutating ARC4 pool \r{su_random_seed()}ed by the seeder object.
	 * Periodic reseeding is by default enabled.
	 * Currently uses \r{MD_SIPHASH}-4-8, with key and key-sized data set (32 bytes input, up to 16 bytes output).
	 *
	 * This type can be hooked via \r{su_random_vsp_install()},
	 * for example to make use of OpenSSL's \c{RAND_bytes(3)} function,
	 * and therefore potentially yield cryptographically strong pseudo random numbers. */
	su_RANDOM_TYPE_VSP
};

/*! See \r{su_random_vsp_install()} (and \r{su_RANDOM_TYPE_VSP}). */
typedef boole (*su_random_generate_fun)(void **cookie, void *buf, uz len);

/*! \_ */
struct su_random{
	BITENUM_IS(u8,su_random_type) rm_type; /*!< \_ */
	u8 rm_flags;
	u8 rm_ro1;
	u8 rm_ro2;
	/*! For \r{su_RANDOM_TYPE_SP}, and also for \r{su_RANDOM_TYPE_VSP} without \r{su_random_vsp_install()}ation,
	 * automatic seed mixing after generating that many random bytes can be performed.
	 * If 0 this is disabled, if \r{su_U32_MAX} it will be set to an internal default when needed first.
	 * If enabled this denotes the minimum value onto which a random of maximally the same value is added.
	 * By default disabled for the former, and enabled for the latter type. */
	u32 rm_reseed_after;
	u32 rm_reseed_next;
	u32 rm_bytes;
	void *rm_vp;
	void *rm_vsp_cookie;
};

/*! Constructor.
 * The object is not seeded, use \r{su_random_seed()} for that.
 * \ESTATE_RV: once the first object is created the internal machinery is initialized,
 * which may fail (but see \r{su_STATE_CREATE_RANDOM})!
 * Dependent upon \a{type} memory allocation may fail for \SELF itself. */
EXPORT s32 su_random_create(struct su_random *self, enum su_random_type type, u32 estate);

/*! Destructor (no harm to call if creation of \SELF failed). */
EXPORT void su_random_gut(struct su_random *self);

/*! Seed an object, either by means of the output of a given, or a built-in seeder object.
 * The \r{su_RANDOM_TYPE_R} type is (re)set to its initial state, ignores \a{with_or_nil},
 * and the function always succeeds.
 * For other types it may block dependent upon the used seeding and the state of the (given) seeder object.
 * This function may fail due to a given \a{with_or_nil},
 * or when seeding \r{su_RANDOM_TYPE_VSP} with a \r{su_random_vsp_install()}ed hook.
 * (The hook is only called if the cookie is not yet set for \SELF,
 * that is to say that a hooked random generator is not assumed to need seeding at all.)
 *
 * The internal built-in seeder uses the algorithm of \r{su_RANDOM_TYPE_VSP},
 * but is itself seeded via the \r{su_RANDOM_SEED} source chosen at build time;
 * see \r{su_random_builtin_seed()}, and \r{su_random_builtin_set_reseed_after()}. */
EXPORT boole su_random_seed(struct su_random *self, struct su_random *with_or_nil);

/*! Generate random bytes.
 * This may fail only for the \r{su_RANDOM_TYPE_VSP} type, and for the reasons shown for \r{su_random_seed()}.
 * This is a no-op if \a{len} is 0. */
EXPORT boole su_random_generate(struct su_random *self, void *buf, uz len);

/*! Install a \r{su_RANDOM_TYPE_VSP} random number generator hook.
 * It will be used by all newly created objects, as long as those exist.
 * If \a{on_generate} is \NIL the default built-in behaviour is (re)established,
 * and be picked up by newly created objects.
 * (If \r{su_RANDOM_SEED} is \c{su_RANDOM_SEED_HOOK} the default behaviour is redirection through a hook already.)
 * \ESTATE_RV; the internal machinery is instantiated as necessary, which may fail; the internal seeder object
 * is not setup: that may still fail later (but see \r{su_STATE_CREATE_RANDOM}).
 *
 * \a{cookie} of \a{on_generate} is object specific and initially \NIL.
 * For as long as \c{*cookie} is \NIL \r{su_random_seed()} will call the hook with all arguments 0,
 * and only upon \r{su_random_gut()} time, when \a{cookie} is not \NIL,
 * will the hook otherwise be called with a \NIL \a{buf} and a length of 0. */
EXPORT s32 su_random_vsp_install(su_random_generate_fun on_generate, u32 estate);

/*! Like \r{su_random_generate()}, but uses an internal random number generator of type \r{su_RANDOM_TYPE_SP}.
 * \ESTATE_RV: once the first object is created the internal machinery is setup, which may fail;
 * for this function this is true even for a request \a{len} of 0 (but see \r{su_STATE_CREATE_RANDOM})! */
EXPORT s32 su_random_builtin_generate(void *buf, uz len, u32 estate);

/*! Like \r{su_random_seed()}, but applies to internal objects, either the \a{seeder}, or the random generator.
 * This uses \r{su_STATE_ERR_PASS} error mode. */
EXPORT boole su_random_builtin_seed(boole seeder);

/*! This sets the su_random::rm_reseed_after of internal objects, either the \a{seeder}, or the random generator.
 * This uses \r{su_STATE_ERR_PASS} error mode. */
EXPORT boole su_random_builtin_set_reseed_after(boole seeder, u32 reseed_after);
/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class random;

/* random {{{ */
/*!
 * \ingroup RANDOM
 * C++ variant of \r{RANDOM} (\r{su/random.h})
 */
class random : private su_random{
	su_CLASS_NO_COPY(random);
public:
	/*! \copydoc{su_random_type} */
	enum type{
		type_r = su_RANDOM_TYPE_R, /*!< \copydoc{su_RANDOM_TYPE_R} */
		type_p = su_RANDOM_TYPE_P, /*!< \copydoc{su_RANDOM_TYPE_P} */
		type_sp = su_RANDOM_TYPE_SP, /*!< \copydoc{su_RANDOM_TYPE_SP} */
		type_vsp = su_RANDOM_TYPE_VSP /*!< \copydoc{su_RANDOM_TYPE_VSP} */
	};

	/*! \copydoc{su_random_generate_fun} */
	typedef su_random_generate_fun generate_fun;

	/*! \remarks{Creation may fail: \r{create()} is real constructor!} */
	random(void) {STRUCT_ZERO(su_random, this);}

	/*! \copydoc{su_random_gut()} */
	~random(void) {su_random_gut(this);}

	/*! \copydoc{su_random_create()} */
	s32 create(type type=type_sp, u32 estate=state::none){
		return su_random_create(this, S(enum su_random_type,type), estate);
	}

	/*! \_ */
	type type(void) const {return S(enum type,rm_type);}

	/*! \copydoc{su_random::rm_reseed_after} */
	u32 reseed_after(void) const {return rm_reseed_after;}

	/*! \copydoc{su_random::rm_reseed_after} */
	random &set_reseed_after(u32 count){
		ASSERT_RET(type() == type_sp || type() == type_vsp, *this);
		rm_reseed_after = count;
		return *this;
	}

	/*! \copydoc{su_random_seed()} */
	boole seed(void) {return su_random_seed(this, NIL);}
	/*! \copydoc{su_random_seed()} */
	boole seed(random &with) {return su_random_seed(this, &with);}

	/*! \copydoc{su_random_generate()} */
	boole generate(void *buf, uz len){
		ASSERT_RET(buf != NIL || len == 0, FAL0);
		return su_random_generate(this, buf, len);
	}

	/*! \copydoc{su_random_generate()} */
	boole operator()(void *buf, uz len) {return generate(buf, len);}

	/*! \copydoc{su_random_vsp_install()} */
	static s32 vsp_install(generate_fun on_generate, u32 estate=state::none){
		return su_random_vsp_install(on_generate, estate);
	}

	/*! \copydoc{su_random_builtin_generate()} */
	static s32 builtin_generate(void *buf, uz len, u32 estate=state::none){
		ASSERT_RET(buf != NIL || len == 0, err::fault);
		return su_random_builtin_generate(buf, len, estate);
	}

	/*! \copydoc{su_random_builtin_seed()} */
	static boole builtin_seed(boole seeder=FAL0) {return su_random_builtin_seed(seeder);}

	/*! \copydoc{su_random_builtin_set_reseed_after()}
	 * \remarks{Reversed argument order for C++ default argument support.} */
	static boole builtin_set_reseed_after(u32 reseed_after, boole seeder=FAL0){
		return su_random_builtin_set_reseed_after(seeder, reseed_after);
	}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_RANDOM_H */
/* s-itt-mode */
