/*@ Implementation of random.h.
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
#define su_FILE random
#define su_SOURCE
#define su_SOURCE_RANDOM

#include "su/code.h"

/* Possible su_RANDOM_SEED values: */
#define su_RANDOM_SEED_BUILTIN 0
#define su_RANDOM_SEED_GETENTROPY 1
#define su_RANDOM_SEED_GETRANDOM 2
#define su_RANDOM_SEED_URANDOM 3
#define su_RANDOM_SEED_HOOK 4

#ifndef su_RANDOM_SEED
# define su_RANDOM_SEED su_RANDOM_SEED_BUILTIN
#endif

#if su_RANDOM_SEED == su_RANDOM_SEED_BUILTIN
#elif su_RANDOM_SEED == su_RANDOM_SEED_GETENTROPY
#elif su_RANDOM_SEED == su_RANDOM_SEED_GETRANDOM
# ifndef su_RANDOM_GETRANDOM_H
#  error su_RANDOM_SEED==su_RANDOM_SEED_GETRANDOM needs su_RANDOM_GETRANDOM_H
# endif
# ifndef su_RANDOM_GETRANDOM_FUN
#  error su_RANDOM_SEED==su_RANDOM_SEED_GETRANDOM needs su_RANDOM_GETRANDOM_FUN
# endif
#elif su_RANDOM_SEED == su_RANDOM_SEED_URANDOM
#elif su_RANDOM_SEED == su_RANDOM_SEED_HOOK
# ifndef su_RANDOM_HOOK_FUN
#  error su_RANDOM_SEED==su_RANDOM_SEED_HOOK needs su_RANDOM_HOOK_FUN
# endif
#else
# error .
#endif

#if su_RANDOM_SEED == su_RANDOM_SEED_GETENTROPY
# include <unistd.h>
#elif su_RANDOM_SEED == su_RANDOM_SEED_GETRANDOM
# include su_RANDOM_GETRANDOM_H
#elif su_RANDOM_SEED == su_RANDOM_SEED_URANDOM /* TODO SU I/O! */
# include <fcntl.h>
# include <unistd.h>
#endif

#include "su/md-siphash.h"
#include "su/mem.h"
#include "su/thread.h"
#include "su/time.h"

#ifdef su_USECASE_SU
# include <su/mutex.h>
#endif

#include "su/random.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

/* _TYPE_VSP hook */
#if su_RANDOM_SEED == su_RANDOM_SEED_HOOK
extern boole su_RANDOM_HOOK_FUN(void **cookie, void *buf, uz len);
#endif

/* Data buffer.  ARC4 == 256 bytes.
 * (Note: getentropy etc. fail if this is >256, we LCTA() as necessary) */
#define a_RANDOM_SEED_BYTES 256

/* Default bytes after which the array is reseeded.
 * The array permutates as we go; Sixteen is arbitrary, but sweet (maybe) */
#define a_RANDOM_RESEED_AFTER (a_RANDOM_SEED_BYTES * 16)
#define a_RANDOM_RESEED_SEEDER_AFTER (a_RANDOM_RESEED_AFTER * 2)

/* (We use value comparisons in here; _NONE _must_ be 0) */
CTAV(su_RANDOM_TYPE_NONE == 0);
CTAV(su_RANDOM_TYPE_R == 1);
CTAV(su_RANDOM_TYPE_P == 2);
CTAV(su_RANDOM_TYPE_SP == 3);
CTAV(su_RANDOM_TYPE_VSP == 4);

enum a_random_flags{
	a_RANDOM_SEEDER = 1u<<0, /* The seeder object */
	a_RANDOM_HOOK = 1u<<1, /* .rm_vp in fact (*_on_generate)() */
	a_RANDOM_REPRO = 1u<<2, /* _TYPE_R */
	a_RANDOM_IS_SEEDED = 1u<<3
};

struct a_random_bltin{
#ifdef su_USECASE_SU
	struct su_mutex rndb_cntrl_mtx;
	struct su_mutex rndb_rand_mtx;
#endif
	struct su_random rndb_seed;
	struct su_random rndb_rand;
};

union a_random_dat{
	u8 b8[a_RANDOM_SEED_BYTES];
	u32 b32[a_RANDOM_SEED_BYTES / sizeof(u32)];
};
CTAV(FIELD_SIZEOF(union a_random_dat,b8) == 256);
CTAV(FIELD_SIZEOF(union a_random_dat,b8) % sizeof(u32) == 0);
CTAV(a_RANDOM_SEED_BYTES <= a_RANDOM_SEED_BYTES);

static struct a_random_bltin *a_random_bltin;

/* _TYPE_VSP hook */
static su_random_generate_fun a_random_vsp_generate
#if su_RANDOM_SEED == su_RANDOM_SEED_HOOK
		= &su_RANDOM_HOOK_FUN
#endif
		;

/* */
static s32 a_random_init(u32 estate);
#ifdef su__STATE_ON_GUT_FUN
static void a_random__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags);
#endif

/* (_init() needs ctor, but public ctor needs _init()) */
static s32 a_random_create(struct su_random *self, enum su_random_type type, u32 estate);

/* Weak transformation of seed */
static u32 a_random_weak(u32 seed);

/* Get one mixed byte while rotating two seed positions */
SINLINE u8 a_random_get8(struct su_random *self);

static s32
a_random_init(u32 estate){
	s32 rv;
	NYD2_IN;

	rv = su_STATE_NONE;

	su__glck_gi9r();

	if(a_random_bltin == NIL){
		struct a_random_bltin *rndp;

		rndp = su_TCALLOCF(struct a_random_bltin, 1, (estate | su_MEM_ALLOC_CONCEAL));
		if(rndp == NIL){
			rv = su_STATE_ERR_NOMEM;
			goto jleave;
		}

#ifdef su_USECASE_SU
		if((rv = su_mutex_create(&rndp->rndb_cntrl_mtx, "SU random seed/cntrl", estate)) != su_STATE_NONE)
			goto jerrm1;

		if((rv = su_mutex_create(&rndp->rndb_rand_mtx, "SU builtin random", estate)) != su_STATE_NONE)
			goto jerrm2;
#endif

		/* To reuse all the control flow we need to create the built-in seeder as
		 * a _SP and adjust it to _VSP thereafter */
		if((rv = a_random_create(&rndp->rndb_seed, su_RANDOM_TYPE_SP, estate)) != su_STATE_NONE)
			goto jerr1;
		rndp->rndb_seed.rm_type = su_RANDOM_TYPE_VSP;
		rndp->rndb_seed.rm_flags |= a_RANDOM_SEEDER;
		rndp->rndb_seed.rm_reseed_after = a_RANDOM_RESEED_SEEDER_AFTER;

		if((rv = a_random_create(&rndp->rndb_rand, su_RANDOM_TYPE_SP, estate)) != su_STATE_NONE)
			goto jerr;
		rndp->rndb_rand.rm_reseed_after = a_RANDOM_RESEED_AFTER;

		if((rv = (su_STATE_NONE
#ifdef su__STATE_ON_GUT_FUN
					| su_state_on_gut_install(&a_random__on_gut, TRU1, estate)
#endif
				)) == su_STATE_NONE)
			a_random_bltin = rndp;
		else{
			su_random_gut(&rndp->rndb_rand);
jerr:
			su_random_gut(&rndp->rndb_seed);
jerr1:
#ifdef su_USECASE_SU
			su_mutex_gut(&rndp->rndb_rand_mtx);
jerrm2:
			su_mutex_gut(&rndp->rndb_cntrl_mtx);
jerrm1:
#endif
			su_FREE(rndp);
		}
	}

jleave:
	su__gnlck_gi9r();

	NYD2_OU;
	return rv;
}

#ifdef su__STATE_ON_GUT_FUN
static void
a_random__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags){
	NYD2_IN;

# if DVLOR(1, 0)
	if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
		su_random_gut(&a_random_bltin->rndb_rand);
		su_random_gut(&a_random_bltin->rndb_seed);
#  ifdef su_USECASE_SU
		su_mutex_gut(&a_random_bltin->rndb_rand_mtx);
		su_mutex_gut(&a_random_bltin->rndb_cntrl_mtx);
#  endif

		su_FREE(a_random_bltin);
	}
# endif

	a_random_bltin = NIL;

	NYD2_OU;
}
#endif /* su__STATE_ON_GUT_FUN */

static s32
a_random_create(struct su_random *self, enum su_random_type type, u32 estate){
	s32 rv;
	NYD2_IN;

	switch(S(uz,(self->rm_type = S(u8,type)))){
	case su_RANDOM_TYPE_VSP:{
		union {void *vp; su_random_generate_fun rgf;} u;

		self->rm_reseed_after = a_RANDOM_RESEED_AFTER;
		u.rgf = a_random_vsp_generate;
		if((self->rm_vp = u.vp) != NIL){
			self->rm_flags = a_RANDOM_HOOK;
			rv = su_STATE_NONE;
			break;
		}
		}
		FALLTHRU
	case su_RANDOM_TYPE_SP:
		FALLTHRU
	case su_RANDOM_TYPE_P:
		FALLTHRU
	case su_RANDOM_TYPE_R:
		if((self->rm_vp = su_TCALLOCF(union a_random_dat, 1, (estate | su_MEM_ALLOC_CONCEAL))) != NIL){
			if(type == su_RANDOM_TYPE_R)
				self->rm_flags |= a_RANDOM_REPRO;
			rv = su_STATE_NONE;
		}else{
			self->rm_type = su_RANDOM_TYPE_NONE;
			rv = su_STATE_ERR_NOMEM;
		}
		break;
	default:
		FALLTHRU
	case su_RANDOM_TYPE_NONE:
		rv = su_STATE_NONE;
		break;
	}

	NYD2_OU;
	return rv;
}

static u32
a_random_weak(u32 seed){
	/* From "Random number generators: good ones are hard to find",
	 * Park and Miller, Communications of the ACM, vol. 31, no. 10, October 1988, p. 1195.
	 * (In fact: FreeBSD 4.7, /usr/src/lib/libc/stdlib/random.c.) */
	u32 hi;

	if(seed == 0)
		seed = 123459876;
	hi =	seed /  127773;
		seed %= 127773;
	seed = (seed * 16807) - (hi * 2836);
	if(S(s32,seed) < 0)
		seed += S32_MAX;

	return seed;
}

SINLINE u8
a_random_get8(struct su_random *self){
	u8 *pi, i, *pj, j;
	union a_random_dat *rdp;

	rdp = S(union a_random_dat*,self->rm_vp);

	i = *(pi = &rdp->b8[++self->rm_ro1]);
	j = *(pj = &rdp->b8[self->rm_ro2 += i]);
	*pi = j;
	*pj = i;

	return i ^ j;
}

s32
su_random_create(struct su_random *self, enum su_random_type type, u32 estate){
	s32 rv;
	NYD_IN;
	ASSERT(self);

	STRUCT_ZERO(struct su_random, self);

	estate &= su_STATE_ERR_MASK;

	if(a_random_bltin != NIL || (rv = a_random_init(estate)) == su_STATE_NONE)
		rv = a_random_create(self, type, estate);

	NYD_OU;
	return rv;
}

void
su_random_gut(struct su_random *self){
	NYD_IN;
	ASSERT(self);

	switch(self->rm_type){
	case su_RANDOM_TYPE_VSP:
		if(self->rm_flags & a_RANDOM_HOOK){
			if(self->rm_vsp_cookie != NIL){
				union {void *vp; su_random_generate_fun rgf;} u;

				u.vp = self->rm_vp;
				(*u.rgf)(&self->rm_vsp_cookie, NIL, 0);
			}
			break;
		}
		FALLTHRU
	case su_RANDOM_TYPE_SP:
		FALLTHRU
	case su_RANDOM_TYPE_P:
		FALLTHRU
	case su_RANDOM_TYPE_R:
		su_FREE(self->rm_vp);
		break;
	case su_RANDOM_TYPE_NONE:
		break;
	}

	NYD_OU;
}

boole
su_random_seed(struct su_random *self, struct su_random *with_or_nil){
	u8 buf[4];
	sz fill;
	union a_random_dat *rdp;
	union {void *vp; su_random_generate_fun rgf; int fd; uz i;} u;
	boole rv;
	NYD_IN;

	if(UNLIKELY(self->rm_type == su_RANDOM_TYPE_NONE)){
		rv = FAL0;
		goto jleave;
	}

	if(/*self->rm_type == su_RANDOM_TYPE_VSP &&*/ (self->rm_flags & a_RANDOM_HOOK)){
		if(!(rv = (self->rm_vsp_cookie != NIL))){
			u.vp = self->rm_vp;
			rv = (*u.rgf)(&self->rm_vsp_cookie, NIL, 0);
		}
		goto jleave;
	}

	rdp = S(union a_random_dat*,self->rm_vp);

	/* Not the special builtin seeder object? */
	if(LIKELY(!(self->rm_flags & a_RANDOM_SEEDER))){
		u8 *bp;

		if(self->rm_type > su_RANDOM_TYPE_P){
			bp = &rdp->b8[0];
			u.i = a_RANDOM_SEED_BYTES;
		}else{
			if(!(self->rm_flags & a_RANDOM_IS_SEEDED) || (self->rm_flags & a_RANDOM_REPRO))
				for(u.i = NELEM(rdp->b8); u.i-- != 0;)
					rdp->b8[u.i] = S(u8,NELEM(rdp->b8) - 1 - u.i);

			if(self->rm_flags & a_RANDOM_REPRO){
				self->rm_ro1 = NELEM(rdp->b8) - (NELEM(rdp->b8) >> 3);
				self->rm_ro2 = NELEM(rdp->b8) >> 3;
				rv = TRU1;
				goto jleave;
			}

			bp = &buf[0];
			u.i = NELEM(buf);
		}

		if(with_or_nil != NIL){
			if(!(rv = su_random_generate(with_or_nil, bp, u.i)))
				goto jleave;
			fill = (with_or_nil->rm_type > su_RANDOM_TYPE_P) ? a_RANDOM_SEED_BYTES : 0;
		}else{
			SU( su_MUTEX_LOCK(&a_random_bltin->rndb_cntrl_mtx); )
			su_random_generate(&a_random_bltin->rndb_seed, bp, u.i);
			SU( su_MUTEX_UNLOCK(&a_random_bltin->rndb_cntrl_mtx); )
			fill = S(sz,u.i);
		}
	}else{
		/* Operating the special seeder is MT locked!  Get "good" random bytes */
		fill = 0;
#if su_RANDOM_SEED == su_RANDOM_SEED_GETENTROPY || su_RANDOM_SEED == su_RANDOM_SEED_GETRANDOM
		LCTA(a_RANDOM_SEED_BYTES <= 256, "Buffer too large to be served without su_ERR_INTR error");
# if su_RANDOM_SEED == su_RANDOM_SEED_GETENTROPY
		fill = (getentropy(rdp->b8, a_RANDOM_SEED_BYTES) == 0 ? a_RANDOM_SEED_BYTES : 0);
# else
		fill = su_RANDOM_GETRANDOM_FUN(rdp->b8, a_RANDOM_SEED_BYTES);
# endif
#elif su_RANDOM_SEED == su_RANDOM_SEED_URANDOM
		if((u.fd = open("/dev/urandom", O_RDONLY)) != -1){ /* TODO SU I/O!*/
			while((fill = read(u.fd, rdp->b8, a_RANDOM_SEED_BYTES)) == -1 &&
					su_err_by_errno() == su_ERR_INTR){
			}
			close(u.fd);
		}
#elif su_RANDOM_SEED == su_RANDOM_SEED_HOOK
		fill = su_RANDOM_HOOK_FUN(&self->rm_vsp_cookie, rdp->b8, a_RANDOM_SEED_BYTES) ? a_RANDOM_SEED_BYTES : 0;
#endif

		/* An unscientific homebrew seed as a fallback and a default */
		if(fill != a_RANDOM_SEED_BYTES){
			struct su_timespec ts;
			u32 seed, rnd;

			seed = a_random_weak(su_timespec_current(&ts)->ts_sec & U32_MAX);
			seed ^= (R(up,a_random_bltin) ^ R(up,self) ^ R(up,rdp)) & U32_MAX;

			for(rnd = 0; rnd != 2; ++rnd){
				if(rnd != 0)
					su_thread_yield();

				/* Stir the entire pool once */
				for(u.i = NELEM(rdp->b32); u.i-- != 0;){
					u32 t;

					t = S(u32,su_timespec_current(&ts)->ts_nano);
					if(rnd & 1)
						t = (t >> 16) ^ (t << 16);
					rdp->b32[u.i] ^= a_random_weak(seed ^ t);
					rdp->b32[t % NELEM(rdp->b32)] ^= seed;
					/* C99 */{
						u32 k = rdp->b32[u.i] % NELEM(rdp->b32);
						rdp->b32[k] ^= rdp->b32[u.i];
						seed ^= a_random_weak(rdp->b32[k]);
					}
				}
			}
		}
	}

	/* */
	if(self->rm_type > su_RANDOM_TYPE_P){
		self->rm_ro1 = (rdp->b8[rdp->b8[1] ^ rdp->b8[84]]);
		self->rm_ro2 = (rdp->b8[rdp->b8[168] ^ rdp->b8[42]]);
		u.i = (fill != a_RANDOM_SEED_BYTES) ? NELEM(rdp->b8) + rdp->b8[110] + a_random_get8(self) : 0;
	}else{
		self->rm_ro1 = buf[0];
		self->rm_ro2 = buf[2];
		u.i = buf[1];
		u.i <<= 8;
		u.i |= buf[3];
		/*su_mem_zero(buf, NELEM(buf));*/
		u.i = CLIP(u.i, NELEM(rdp->b8), NELEM(rdp->b8) * 4);
	}

	while(u.i-- != 0)
		a_random_get8(self);

	rv = TRU1;
jleave:
	if(rv)
		self->rm_flags |= a_RANDOM_IS_SEEDED;

	NYD_OU;
	return rv;
}

boole
su_random_generate(struct su_random *self, void *buf, uz len){
	u8 b_base[(su_SIPHASH_KEY_SIZE) * 2 + (128 / 8)], *bp;
	boole rv;
	NYD_IN;
	ASSERT(self);
	ASSERT_NYD_EXEC(len == 0 || buf != NIL, rv = FAL0);

	rv = TRU1;

	if(len == 0){
	}else if(!(self->rm_flags & a_RANDOM_IS_SEEDED) && !(rv = su_random_seed(self, NIL))){
	}else if(self->rm_flags & a_RANDOM_HOOK){
		union {void *vp; su_random_generate_fun rgf;} u;

		u.vp = self->rm_vp;
		rv = (*u.rgf)(&self->rm_vsp_cookie, buf, len);
	}else if(self->rm_type < su_RANDOM_TYPE_SP){
		for(bp = S(u8*,buf); len-- != 0;)
			*bp++ = a_random_get8(self);
	}else{
		u8 crounds, drounds;
		enum su_siphash_digest shd;
		boole reseed;
		uz i, resl, datl;

		/* For simplicity we reseed only once after a request is done */
		i = self->rm_reseed_after;
		if(LIKELY(i != U32_MAX))
			reseed = (i > 0);
		else{
			i = self->rm_reseed_after = a_RANDOM_RESEED_AFTER;
			reseed = TRU1;
		}

		if(reseed){
			if((datl = self->rm_reseed_next) == 0)
				self->rm_reseed_next = S(u32,datl = i);
			i = datl;
			reseed = (i < self->rm_bytes || i - self->rm_bytes <= len);

			if(reseed){
				/* Randomize reseed_after */
				for(resl = datl = 0; resl < 4; ++resl){
					datl <<= 6; /* (assuming reseed_after is LT 24-bits) */
					datl ^= a_random_get8(self);
				}
				i = self->rm_reseed_after;
				i += (datl % i);
				self->rm_reseed_next = S(u32,i);
				self->rm_bytes = 0;
			}else
				self->rm_bytes += len;
		}

		/* */
		resl = su_SIPHASH_DIGEST_SIZE_64;
		shd = su_SIPHASH_DIGEST_64;
		if(len > 8){
			resl = su_SIPHASH_DIGEST_SIZE_128;
			shd = su_SIPHASH_DIGEST_128;
		}

		crounds = 4;
		drounds = 8;
		datl = su_SIPHASH_KEY_SIZE;
		if(self->rm_type == su_RANDOM_TYPE_SP){
			crounds >>= 1;
			drounds >>= 1;
			datl = 0;
		}

		while(len > 0){
			for(bp = b_base, i = su_SIPHASH_KEY_SIZE + datl; i-- != 0; ++bp)
				*bp = a_random_get8(self);

			su_siphash_once(bp, shd, &b_base[0], &b_base[su_SIPHASH_KEY_SIZE], datl, crounds, drounds);

			i = MIN(len, resl);
			su_mem_copy(buf, bp, i);
			if((len -= i) == 0)
				break;
			buf = &S(u8*,buf)[i];
		}

		su_mem_zero(b_base, sizeof b_base);

		if(reseed)
			su_random_seed(self, NIL);
	}

	NYD_OU;
	return rv;
}

s32
su_random_vsp_install(su_random_generate_fun on_generate, u32 estate){
	s32 rv;
	NYD_IN;

#if su_RANDOM_SEED == su_RANDOM_SEED_HOOK
	if(on_generate == NIL)
		on_generate = &su_RANDOM_HOOK_FUN;
#endif

	rv = su_STATE_NONE;

	if(a_random_bltin != NIL || (rv = a_random_init(estate)) == su_STATE_NONE)
		a_random_vsp_generate = on_generate; /* unlocked */

	NYD_OU;
	return rv;
}

s32
su_random_builtin_generate(void *buf, uz len, u32 estate){
	s32 rv;
	NYD_IN;
	ASSERT_NYD_EXEC(len == 0 || buf != NIL, rv = su_STATE_NONE);

	if(a_random_bltin == NIL && (rv = a_random_init(estate)) != su_STATE_NONE)
		goto jleave;

	rv = su_STATE_NONE;

	if(len > 0){
		SU( su_MUTEX_LOCK(&a_random_bltin->rndb_rand_mtx); )
		su_random_generate(&a_random_bltin->rndb_rand, buf, len);
		SU( su_MUTEX_UNLOCK(&a_random_bltin->rndb_rand_mtx); )
	}

jleave:
	NYD_OU;
	return rv;
}

boole
su_random_builtin_seed(boole seeder){
	struct su_random *rp;
	SU( struct su_mutex *mp; )
	boole rv;
	NYD_IN;

	if(a_random_bltin == NIL && a_random_init(su_STATE_ERR_PASS) != su_STATE_NONE){
		rv = FAL0;
		goto jleave;
	}

	if(seeder){
		rp = &a_random_bltin->rndb_seed;
		SU( mp = &a_random_bltin->rndb_cntrl_mtx; )
	}else{
		rp = &a_random_bltin->rndb_rand;
		SU( mp = &a_random_bltin->rndb_rand_mtx; )
	}

	SU( su_MUTEX_LOCK(mp); )
	rv = su_random_seed(rp, NIL);
	SU( su_MUTEX_UNLOCK(mp); )

jleave:
	NYD_OU;
	return rv;
}

boole
su_random_builtin_set_reseed_after(boole seeder, u32 reseed_after){
	boole rv;
	NYD_IN;

	rv = (a_random_bltin != NIL || a_random_init(su_STATE_ERR_PASS) == su_STATE_NONE);

	if(rv)
		(seeder ? &a_random_bltin->rndb_seed : &a_random_bltin->rndb_rand)->rm_reseed_after = reseed_after;

	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_RANDOM
/* s-itt-mode */
