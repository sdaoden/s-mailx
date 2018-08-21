/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Heap memory and automatically reclaimed storage.
 *@ TODO Back the _flux_ heap.
 *@ TODO Add cache for "the youngest" two or three n_MEMORY_AUTOREC_SIZE arenas
 *
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE memory

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/*
 * We use per-execution context memory arenas, to be found in
 * n_go_data->gdc_mempool; if NULL, set to ->gdc__mempool_buf.
 * n_memory_reset() that happens on loop ticks reclaims their memory, and
 * performs debug checks also on the former #ifdef HAVE_MEMORY_DEBUG.
 * The arena that is used already during program startup is special in that
 * _pool_fixate() will set "a lower bound" in order not to reclaim memory that
 * must be kept vivid during the lifetime of the program.
 * That was so in historical code with the globally shared single string dope
 * implementation, too.  (And it still seems easier than bypassing to normal
 * heap memory before _fixate() is called, today.)
 *
 * AutoReclaimedStorage memory is the follow-up to the historical "stringdope"
 * allocator from 1979 (see [timeline:a7342d9]:src/Mail/strings.c), it is
 * a steadily growing pool (but _autorec_relax_create() .. [:_relax_unroll():]
 * ..autorec_relax_gut() will reduce pressure) until n_memory_reset() time.
 *
 * LastOutFirstIn memory is meant as an alloca(3) replacement but which requires
 * lofi_free()ing pointers (otherwise growing until n_memory_reset()).
 *
 * TODO Flux heap memory is like LOFI except that any pointer can be freed (and
 * TODO reused) at any time, just like normal heap memory.  It is notational in
 * TODO that it clearly states that the allocation will go away after a loop
 * TODO tick, and also we can use some buffer caches.
 */

/* If defined (and HAVE_MEMORY_DEBUG), realloc acts like alloc+free, which can
 * help very bogus double-free attempts */
#define a_MEMORY_REALLOC_IS_ALLOC_PLUS_FREE /* TODO runtime opt <> C++ cache */

/* Maximum allocation (directly) handled by A-R-Storage */
#define a_MEMORY_ARS_MAX (n_MEMORY_AUTOREC_SIZE / 2 + n_MEMORY_AUTOREC_SIZE / 4)
#define a_MEMORY_LOFI_MAX a_MEMORY_ARS_MAX

n_CTA(a_MEMORY_ARS_MAX > 1024,
   "Auto-reclaimed memory requires a larger buffer size"); /* Anway > 42! */
n_CTA(n_ISPOW2(n_MEMORY_AUTOREC_SIZE),
   "Buffers should be POW2 (may be wasteful on native allocators otherwise)");

/* Alignment of ARS memory.  Simply go for pointer alignment */
#define a_MEMORY_ARS_ROUNDUP(S) n_ALIGN_SMALL(S)
#define a_MEMORY_LOFI_ROUNDUP(S) a_MEMORY_ARS_ROUNDUP(S)

#ifdef HAVE_MEMORY_DEBUG
n_CTA(sizeof(char) == sizeof(ui8_t), "But POSIX says a byte is 8 bit");

# define a_MEMORY_HOPE_SIZE (2 * 8 * sizeof(char))
# define a_MEMORY_HOPE_INC(P) (P) += 8
# define a_MEMORY_HOPE_DEC(P) (P) -= 8

/* We use address-induced canary values, inspiration (but he didn't invent)
 * and primes from maxv@netbsd.org, src/sys/kern/subr_kmem.c */
# define a_MEMORY_HOPE_LOWER(S,P) \
do{\
   ui64_t __h__ = (uintptr_t)(P);\
   __h__ *= ((ui64_t)0x9E37FFFFu << 32) | 0xFFFC0000u;\
   __h__ >>= 56;\
   (S) = (ui8_t)__h__;\
}while(0)

# define a_MEMORY_HOPE_UPPER(S,P) \
do{\
   ui32_t __i__;\
   ui64_t __x__, __h__ = (uintptr_t)(P);\
   __h__ *= ((ui64_t)0x9E37FFFFu << 32) | 0xFFFC0000u;\
   for(__i__ = 56; __i__ != 0; __i__ -= 8)\
      if((__x__ = (__h__ >> __i__)) != 0){\
         (S) = (ui8_t)__x__;\
         break;\
      }\
   if(__i__ == 0)\
      (S) = 0xAAu;\
}while(0)

# define a_MEMORY_HOPE_SET(T,C) \
do{\
   union a_memory_ptr __xp;\
   struct a_memory_chunk *__xc;\
   __xp.p_vp = (C).p_vp;\
   __xc = (struct a_memory_chunk*)(__xp.T - 1);\
   a_MEMORY_HOPE_INC((C).p_cp);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[0], &__xp.p_ui8p[0]);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[1], &__xp.p_ui8p[1]);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[2], &__xp.p_ui8p[2]);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[3], &__xp.p_ui8p[3]);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[4], &__xp.p_ui8p[4]);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[5], &__xp.p_ui8p[5]);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[6], &__xp.p_ui8p[6]);\
   a_MEMORY_HOPE_LOWER(__xp.p_ui8p[7], &__xp.p_ui8p[7]);\
   a_MEMORY_HOPE_INC(__xp.p_ui8p) + __xc->mc_user_size;\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[0], &__xp.p_ui8p[0]);\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[1], &__xp.p_ui8p[1]);\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[2], &__xp.p_ui8p[2]);\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[3], &__xp.p_ui8p[3]);\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[4], &__xp.p_ui8p[4]);\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[5], &__xp.p_ui8p[5]);\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[6], &__xp.p_ui8p[6]);\
   a_MEMORY_HOPE_UPPER(__xp.p_ui8p[7], &__xp.p_ui8p[7]);\
}while(0)

# define a_MEMORY_HOPE_GET_TRACE(T,C,BAD) \
do{\
   a_MEMORY_HOPE_INC((C).p_cp);\
   a_MEMORY_HOPE_GET(T, C, BAD);\
   a_MEMORY_HOPE_INC((C).p_cp);\
}while(0)

# define a_MEMORY_HOPE_GET(T,C,BAD) \
do{\
   union a_memory_ptr __xp;\
   struct a_memory_chunk *__xc;\
   ui32_t __i;\
   ui8_t __m;\
   __xp.p_vp = (C).p_vp;\
   a_MEMORY_HOPE_DEC(__xp.p_cp);\
   (C).p_cp = __xp.p_cp;\
   __xc = (struct a_memory_chunk*)(__xp.T - 1);\
   (BAD) = FAL0;\
   __i = 0;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[0]);\
      if(__xp.p_ui8p[0] != __m) __i |= 1<<0;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[1]);\
      if(__xp.p_ui8p[1] != __m) __i |= 1<<1;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[2]);\
      if(__xp.p_ui8p[2] != __m) __i |= 1<<2;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[3]);\
      if(__xp.p_ui8p[3] != __m) __i |= 1<<3;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[4]);\
      if(__xp.p_ui8p[4] != __m) __i |= 1<<4;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[5]);\
      if(__xp.p_ui8p[5] != __m) __i |= 1<<5;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[6]);\
      if(__xp.p_ui8p[6] != __m) __i |= 1<<6;\
   a_MEMORY_HOPE_LOWER(__m, &__xp.p_ui8p[7]);\
      if(__xp.p_ui8p[7] != __m) __i |= 1<<7;\
   if(__i != 0){\
      (BAD) = TRU1;\
      a_MEMORY_HOPE_INC((C).p_cp);\
      n_alert("%p: corrupt lower canary: 0x%02X: %s, line %d",\
         (C).p_cp, __i, mdbg_file, mdbg_line);\
      a_MEMORY_HOPE_DEC((C).p_cp);\
   }\
   a_MEMORY_HOPE_INC(__xp.p_ui8p) + __xc->mc_user_size;\
   __i = 0;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[0]);\
      if(__xp.p_ui8p[0] != __m) __i |= 1<<0;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[1]);\
      if(__xp.p_ui8p[1] != __m) __i |= 1<<1;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[2]);\
      if(__xp.p_ui8p[2] != __m) __i |= 1<<2;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[3]);\
      if(__xp.p_ui8p[3] != __m) __i |= 1<<3;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[4]);\
      if(__xp.p_ui8p[4] != __m) __i |= 1<<4;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[5]);\
      if(__xp.p_ui8p[5] != __m) __i |= 1<<5;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[6]);\
      if(__xp.p_ui8p[6] != __m) __i |= 1<<6;\
   a_MEMORY_HOPE_UPPER(__m, &__xp.p_ui8p[7]);\
      if(__xp.p_ui8p[7] != __m) __i |= 1<<7;\
   if(__i != 0){\
      (BAD) = TRU1;\
      a_MEMORY_HOPE_INC((C).p_cp);\
      n_alert("%p: corrupt upper canary: 0x%02X: %s, line %d",\
         (C).p_cp, __i, mdbg_file, mdbg_line);\
      a_MEMORY_HOPE_DEC((C).p_cp);\
   }\
   if(BAD)\
      n_alert("   ..canary last seen: %s, line %u",\
         __xc->mc_file, __xc->mc_line);\
}while(0)
#endif /* HAVE_MEMORY_DEBUG */

#ifdef HAVE_MEMORY_DEBUG
struct a_memory_chunk{
   char const *mc_file;
   ui32_t mc_line;
   ui8_t mc_isfree;
   ui8_t mc__dummy[3];
   ui32_t mc_user_size;
   ui32_t mc_size;
};

/* The heap memory n_free() may become delayed to detect double frees.
 * It is primitive, but ok: speed and memory usage don't matter here */
struct a_memory_heap_chunk{
   struct a_memory_chunk mhc_super;
   struct a_memory_heap_chunk *mhc_prev;
   struct a_memory_heap_chunk *mhc_next;
};
#endif /* HAVE_MEMORY_DEBUG */

struct a_memory_ars_lofi_chunk{
#ifdef HAVE_MEMORY_DEBUG
   struct a_memory_chunk malc_super;
#endif
   struct a_memory_ars_lofi_chunk *malc_last; /* Bit 1 set: it's a heap alloc */
};

union a_memory_ptr{
   void *p_vp;
   char *p_cp;
   ui8_t *p_ui8p;
#ifdef HAVE_MEMORY_DEBUG
   struct a_memory_chunk *p_c;
   struct a_memory_heap_chunk *p_hc;
#endif
   struct a_memory_ars_lofi_chunk *p_alc;
};

struct a_memory_ars_ctx{
   struct a_memory_ars_ctx *mac_outer;
   struct a_memory_ars_ctx *mac_outer_save;
   struct a_memory_ars_buffer *mac_top;   /* Alloc stack */
   struct a_memory_ars_buffer *mac_full;  /* Alloc stack, cpl. filled */
   size_t mac_recur;                      /* _relax_create() recursion */
   struct a_memory_ars_huge *mac_huge;    /* Huge allocation bypass list */
   struct a_memory_ars_lofi *mac_lofi;    /* Pseudo alloca */
   struct a_memory_ars_lofi_chunk *mac_lofi_top;
};
n_CTA(n_MEMORY_POOL_TYPE_SIZEOF >= sizeof(struct a_memory_ars_ctx),
   "struct n_go_data_ctx.gdc_mempool is not large enough for memory pool");

struct a_memory_ars_buffer{
   struct a_memory_ars_buffer *mab_last;
   char *mab_bot;    /* For _autorec_fixate(): keep startup memory lingering */
   char *mab_relax;  /* If !NULL, used by _relax_unroll() instead of .mab_bot */
   char *mab_caster; /* Point of casting off memory */
   char mab_buf[n_MEMORY_AUTOREC_SIZE - (4 * sizeof(void*))];
};
n_CTA(sizeof(struct a_memory_ars_buffer) == n_MEMORY_AUTOREC_SIZE,
   "Resulting structure size is not the expected one");
#ifdef HAVE_MEMORY_DEBUG
n_CTA(a_MEMORY_ARS_MAX + a_MEMORY_HOPE_SIZE + sizeof(struct a_memory_chunk)
      < n_SIZEOF_FIELD(struct a_memory_ars_buffer, mab_buf),
   "Memory layout of auto-reclaimed storage does not work out that way");
#endif

/* Requests that exceed a_MEMORY_ARS_MAX are always served by the normal
 * memory allocator (which panics if memory cannot be served).  This can be
 * seen as a security fallback bypass only */
struct a_memory_ars_huge{
   struct a_memory_ars_huge *mah_last;
   char mah_buf[n_VFIELD_SIZE(a_MEMORY_ARS_ROUNDUP(1))];
};

struct a_memory_ars_lofi{
   struct a_memory_ars_lofi *mal_last;
   char *mal_caster;
   char *mal_max;
   char mal_buf[n_VFIELD_SIZE(a_MEMORY_ARS_ROUNDUP(1))];
};

/* */
#ifdef HAVE_MEMORY_DEBUG
static size_t a_memory_heap_aall, a_memory_heap_acur, a_memory_heap_amax,
      a_memory_heap_mall, a_memory_heap_mcur, a_memory_heap_mmax;
static struct a_memory_heap_chunk *a_memory_heap_list, *a_memory_heap_free;

static size_t a_memory_ars_ball, a_memory_ars_bcur, a_memory_ars_bmax,
      a_memory_ars_hall, a_memory_ars_hcur, a_memory_ars_hmax,
      a_memory_ars_aall, a_memory_ars_mall;

static size_t a_memory_lofi_ball, a_memory_lofi_bcur, a_memory_lofi_bmax,
      a_memory_lofi_aall, a_memory_lofi_acur, a_memory_lofi_amax,
      a_memory_lofi_mall, a_memory_lofi_mcur, a_memory_lofi_mmax;
#endif

/* */
n_INLINE void a_memory_lofi_free(struct a_memory_ars_ctx *macp, void *vp);

/* Reset an ars_ctx */
static void a_memory_ars_reset(struct a_memory_ars_ctx *macp);

n_INLINE void
a_memory_lofi_free(struct a_memory_ars_ctx *macp, void *vp){
   struct a_memory_ars_lofi *malp;
   union a_memory_ptr p;
   NYD2_IN;

   p.p_vp = vp;
#ifdef HAVE_MEMORY_DEBUG
   --a_memory_lofi_acur;
   a_memory_lofi_mcur -= p.p_c->mc_user_size;
#endif

   /* The heap allocations are released immediately */
   if((uintptr_t)p.p_alc->malc_last & 0x1){
      malp = macp->mac_lofi;
      macp->mac_lofi = malp->mal_last;
      macp->mac_lofi_top = (struct a_memory_ars_lofi_chunk*)
            ((uintptr_t)p.p_alc->malc_last & ~0x1);
      n_free(malp);
#ifdef HAVE_MEMORY_DEBUG
      --a_memory_lofi_bcur;
#endif
   }else{
      macp->mac_lofi_top = p.p_alc->malc_last;

      /* The normal arena ones only if the arena is empty, except for when
       * it is the last - that we'll keep until _pool_pop() or exit(3) */
      if(p.p_cp == (malp = macp->mac_lofi)->mal_buf){
         if(malp->mal_last != NULL){
            macp->mac_lofi = malp->mal_last;
            n_free(malp);
#ifdef HAVE_MEMORY_DEBUG
            --a_memory_lofi_bcur;
#endif
         }
      }else
         malp->mal_caster = p.p_cp;
   }
   NYD2_OU;
}

static void
a_memory_ars_reset(struct a_memory_ars_ctx *macp){
   union{
      struct a_memory_ars_lofi_chunk *alcp;
      struct a_memory_ars_lofi *alp;
      struct a_memory_ars_buffer *abp;
      struct a_memory_ars_huge *ahp;
   } m, m2;
   NYD2_IN;

   /* Simply move all buffers away from .mac_full */
   for(m.abp = macp->mac_full; m.abp != NULL; m.abp = m2.abp){
      m2.abp = m.abp->mab_last;
      m.abp->mab_last = macp->mac_top;
      macp->mac_top = m.abp;
   }
   macp->mac_full = NULL;

   for(m2.abp = NULL, m.abp = macp->mac_top; m.abp != NULL;){
      struct a_memory_ars_buffer *x;

      x = m.abp;
      m.abp = m.abp->mab_last;

      /* Give away all buffers that are not covered by autorec_fixate() */
      if(x->mab_bot == x->mab_buf){
         if(m2.abp == NULL)
            macp->mac_top = m.abp;
         else
            m2.abp->mab_last = m.abp;
         n_free(x);
#ifdef HAVE_MEMORY_DEBUG
         --a_memory_ars_bcur;
#endif
      }else{
         m2.abp = x;
         x->mab_caster = x->mab_bot;
         x->mab_relax = NULL;
#ifdef HAVE_MEMORY_DEBUG
         memset(x->mab_caster, 0377,
            PTR2SIZE(&x->mab_buf[sizeof(x->mab_buf)] - x->mab_caster));
#endif
      }
   }

   while((m.ahp = macp->mac_huge) != NULL){
      macp->mac_huge = m.ahp->mah_last;
      n_free(m.ahp);
#ifdef HAVE_MEMORY_DEBUG
      --a_memory_ars_hcur;
#endif
   }

   /* "alloca(3)" memory goes away, too.  XXX Must be last as long we jump */
#ifdef HAVE_MEMORY_DEBUG
   if(macp->mac_lofi_top != NULL &&
         ((n_psonce & n_PSO_REPRODUCIBLE) ||
          (n_poption & (n_PO_DEBUG | n_PO_MEMDEBUG))))
      n_alert("There still is LOFI memory upon ARS reset!");
#endif
   while((m.alcp = macp->mac_lofi_top) != NULL)
      a_memory_lofi_free(macp, m.alcp);
   NYD2_OU;
}

FL void
n_memory_reset(void){
#ifdef HAVE_MEMORY_DEBUG
   union a_memory_ptr p;
   size_t c, s;
#endif
   struct a_memory_ars_ctx *macp;
   NYD_IN;

   n_memory_check();

   if((macp = n_go_data->gdc_mempool) != NULL){
      /* First of all reset auto-reclaimed storage so that heap freed during
       * this can be handled in a second step */
      /* TODO v15 active recursion can only happen after a jump */
      if(macp->mac_recur > 0){
         macp->mac_recur = 1;
         n_autorec_relax_gut();
      }
      a_memory_ars_reset(macp);
   }

   /* Now we are ready to deal with heap */
#ifdef HAVE_MEMORY_DEBUG
   c = s = 0;

   for(p.p_hc = a_memory_heap_free; p.p_hc != NULL;){
      void *vp;

      vp = p.p_hc;
      ++c;
      s += p.p_c->mc_size;
      p.p_hc = p.p_hc->mhc_next;
      (free)(vp);
   }
   a_memory_heap_free = NULL;

   if((n_poption & (n_PO_DEBUG | n_PO_MEMDEBUG)) && c > 0)
      n_err("memreset: freed %" PRIuZ " chunks/%" PRIuZ " bytes\n", c, s);
#endif
   NYD_OU;
}

FL void
n_memory_pool_fixate(void){
   struct a_memory_ars_buffer *mabp;
   struct a_memory_ars_ctx *macp;
   NYD_IN;

   if((macp = n_go_data->gdc_mempool) != NULL){
      for(mabp = macp->mac_top; mabp != NULL; mabp = mabp->mab_last)
         mabp->mab_bot = mabp->mab_caster;
      for(mabp = macp->mac_full; mabp != NULL; mabp = mabp->mab_last)
         mabp->mab_bot = mabp->mab_caster;
   }
   NYD_OU;
}

FL void
n_memory_pool_push(void *vp, bool_t init){
   struct a_memory_ars_ctx *macp;
   NYD_IN;

   macp = vp;

   if(init){
      memset(macp, 0, sizeof *macp);
   }
   assert(macp->mac_outer_save == NULL);

   if(n_go_data->gdc_mempool == NULL)
      n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

   macp->mac_outer_save = macp->mac_outer;
   macp->mac_outer = n_go_data->gdc_mempool;
   n_go_data->gdc_mempool = macp;
   NYD_OU;
}

FL void
n_memory_pool_pop(void *vp, bool_t gut){
   struct a_memory_ars_buffer *mabp;
   struct a_memory_ars_ctx *macp;
   NYD_IN;

   n_memory_check();

   if((macp = vp) == NULL){
      assert(gut);
      macp = n_go_data->gdc_mempool;
      assert(macp != NULL);
   }else{
      /* XXX May not be ARS top upon jump */
      while(n_go_data->gdc_mempool != macp){
         assert(gut);
         DBG( n_err("ARS pop %p to reach freed context\n",
            n_go_data->gdc_mempool); )
         n_memory_pool_pop(n_go_data->gdc_mempool, gut);
      }
   }
   n_go_data->gdc_mempool = macp->mac_outer;
   macp->mac_outer = macp->mac_outer_save;
   macp->mac_outer_save = NULL;

   if(gut){
      a_memory_ars_reset(macp);
      assert(macp->mac_full == NULL);
      assert(macp->mac_huge == NULL);

      mabp = macp->mac_top;
      macp->mac_top = NULL;
      while(mabp != NULL){
         vp = mabp;
         mabp = mabp->mab_last;
         n_free(vp);
      }

      /* We (may) have kept one buffer for our pseudo alloca(3) */
      if((vp = macp->mac_lofi) != NULL){
         assert(macp->mac_lofi->mal_last == NULL);
         macp->mac_lofi = NULL;
#ifdef HAVE_MEMORY_DEBUG
         --a_memory_lofi_bcur;
#endif
         n_free(vp);
      }
   }
   NYD_OU;
}

FL void *
n_memory_pool_top(void){
   void *rv;
   NYD2_IN;

   if((rv = n_go_data->gdc_mempool) == NULL)
      rv = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;
   NYD2_OU;
   return rv;
}

#ifndef HAVE_MEMORY_DEBUG
FL void *
n_alloc(size_t s){
   void *rv;
   NYD2_IN;

   if(s == 0)
      s = 1;
   if((rv = malloc(s)) == NULL)
      n_panic(_("no memory"));
   NYD2_OU;
   return rv;
}

FL void *
n_realloc(void *vp, size_t s){
   void *rv;
   NYD2_IN;

   if(vp == NULL)
      rv = n_alloc(s);
   else{
      if(s == 0)
         s = 1;
      if((rv = realloc(vp, s)) == NULL)
         n_panic(_("no memory"));
   }
   NYD2_OU;
   return rv;
}

FL void *
n_calloc(size_t nmemb, size_t size){
   void *rv;
   NYD2_IN;

   if(size == 0)
      size = 1;
   if((rv = calloc(nmemb, size)) == NULL)
      n_panic(_("no memory"));
   NYD2_OU;
   return rv;
}

FL void
(n_free)(void *vp){
   NYD2_IN;
   (free)(vp);
   NYD2_OU;
}

#else /* !HAVE_MEMORY_DEBUG */
FL void *
(n_alloc)(size_t s n_MEMORY_DEBUG_ARGS){
   union a_memory_ptr p;
   ui32_t user_s;
   NYD2_IN;

   if(s > UI32_MAX - sizeof(struct a_memory_heap_chunk) - a_MEMORY_HOPE_SIZE)
      n_panic("n_alloc(): allocation too large: %s, line %d",
         mdbg_file, mdbg_line);
   if((user_s = (ui32_t)s) == 0)
      s = 1;
   s += sizeof(struct a_memory_heap_chunk) + a_MEMORY_HOPE_SIZE;

   if((p.p_vp = (malloc)(s)) == NULL)
      n_panic(_("no memory"));

   p.p_hc->mhc_prev = NULL;
   if((p.p_hc->mhc_next = a_memory_heap_list) != NULL)
      a_memory_heap_list->mhc_prev = p.p_hc;

   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_user_size = user_s;
   p.p_c->mc_size = (ui32_t)s;

   a_memory_heap_list = p.p_hc++;
   a_MEMORY_HOPE_SET(p_hc, p);

   ++a_memory_heap_aall;
   ++a_memory_heap_acur;
   a_memory_heap_amax = n_MAX(a_memory_heap_amax, a_memory_heap_acur);
   a_memory_heap_mall += user_s;
   a_memory_heap_mcur += user_s;
   a_memory_heap_mmax = n_MAX(a_memory_heap_mmax, a_memory_heap_mcur);
   NYD2_OU;
   return p.p_vp;
}

FL void *
(n_realloc)(void *vp, size_t s n_MEMORY_DEBUG_ARGS){
# ifndef a_MEMORY_REALLOC_IS_ALLOC_PLUS_FREE
   ui32_t user_s;
# endif
   bool_t isbad;
   union a_memory_ptr p;
   NYD2_IN;

   if((p.p_vp = vp) == NULL){
jforce:
      p.p_vp = (n_alloc)(s, mdbg_file, mdbg_line);
      goto jleave;
   }

   a_MEMORY_HOPE_GET(p_hc, p, isbad);
   --p.p_hc;

   if(p.p_c->mc_isfree){
      n_err("n_realloc(): region freed!  At %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jforce;
   }

# ifdef a_MEMORY_REALLOC_IS_ALLOC_PLUS_FREE
   /* C99 */{
      char *xp;

      xp = (n_alloc)(s, mdbg_file, mdbg_line);
      memcpy(xp, vp, n_MIN(s, p.p_c->mc_user_size));
      (n_free)(vp, mdbg_file, mdbg_line);
      p.p_vp = xp;
      goto jleave;
   }
# else

   if(p.p_hc == a_memory_heap_list)
      a_memory_heap_list = p.p_hc->mhc_next;
   else
      p.p_hc->mhc_prev->mhc_next = p.p_hc->mhc_next;
   if (p.p_hc->mhc_next != NULL)
      p.p_hc->mhc_next->mhc_prev = p.p_hc->mhc_prev;

   --a_memory_heap_acur;
   a_memory_heap_mcur -= p.p_c->mc_user_size;

   if(s > UI32_MAX - sizeof(struct a_memory_heap_chunk) - a_MEMORY_HOPE_SIZE)
      n_panic("n_realloc(): allocation too large: %s, line %d",
         mdbg_file, mdbg_line);
   if((user_s = (ui32_t)s) == 0)
      s = 1;
   s += sizeof(struct a_memory_heap_chunk) + a_MEMORY_HOPE_SIZE;

   if((p.p_vp = (realloc)(p.p_c, s)) == NULL)
      n_panic(_("no memory"));
   p.p_hc->mhc_prev = NULL;
   if((p.p_hc->mhc_next = a_memory_heap_list) != NULL)
      a_memory_heap_list->mhc_prev = p.p_hc;

   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_user_size = user_s;
   p.p_c->mc_size = (ui32_t)s;

   a_memory_heap_list = p.p_hc++;
   a_MEMORY_HOPE_SET(p_hc, p);

   ++a_memory_heap_aall;
   ++a_memory_heap_acur;
   a_memory_heap_amax = n_MAX(a_memory_heap_amax, a_memory_heap_acur);
   a_memory_heap_mall += user_s;
   a_memory_heap_mcur += user_s;
   a_memory_heap_mmax = n_MAX(a_memory_heap_mmax, a_memory_heap_mcur);
# endif /* a_MEMORY_REALLOC_IS_ALLOC_PLUS_FREE */
jleave:
   NYD2_OU;
   return p.p_vp;
}

FL void *
(n_calloc)(size_t nmemb, size_t size n_MEMORY_DEBUG_ARGS){
   union a_memory_ptr p;
   ui32_t user_s;
   NYD2_IN;

   if(nmemb == 0)
      nmemb = 1;
   if(size > UI32_MAX - sizeof(struct a_memory_heap_chunk) - a_MEMORY_HOPE_SIZE)
      n_panic("n_calloc(): allocation size too large: %s, line %d",
         mdbg_file, mdbg_line);
   if((user_s = (ui32_t)size) == 0)
      size = 1;
   if((UI32_MAX - sizeof(struct a_memory_heap_chunk) - a_MEMORY_HOPE_SIZE) /
         nmemb < size)
      n_panic("n_calloc(): allocation count too large: %s, line %d",
         mdbg_file, mdbg_line);

   size *= nmemb;
   size += sizeof(struct a_memory_heap_chunk) + a_MEMORY_HOPE_SIZE;

   if((p.p_vp = (malloc)(size)) == NULL)
      n_panic(_("no memory"));
   memset(p.p_vp, 0, size);

   p.p_hc->mhc_prev = NULL;
   if((p.p_hc->mhc_next = a_memory_heap_list) != NULL)
      a_memory_heap_list->mhc_prev = p.p_hc;

   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_user_size = (user_s > 0) ? user_s *= nmemb : 0;
   p.p_c->mc_size = (ui32_t)size;

   a_memory_heap_list = p.p_hc++;
   a_MEMORY_HOPE_SET(p_hc, p);

   ++a_memory_heap_aall;
   ++a_memory_heap_acur;
   a_memory_heap_amax = n_MAX(a_memory_heap_amax, a_memory_heap_acur);
   a_memory_heap_mall += user_s;
   a_memory_heap_mcur += user_s;
   a_memory_heap_mmax = n_MAX(a_memory_heap_mmax, a_memory_heap_mcur);
   NYD2_OU;
   return p.p_vp;
}

FL void
(n_free)(void *vp n_MEMORY_DEBUG_ARGS){
   union a_memory_ptr p;
   bool_t isbad;
   NYD2_IN;

   if((p.p_vp = vp) == NULL){
      n_err("n_free(NULL) from %s, line %d\n", mdbg_file, mdbg_line);
      goto jleave;
   }

   a_MEMORY_HOPE_GET(p_hc, p, isbad);
   --p.p_hc;

   if(p.p_c->mc_isfree){
      n_err("n_free(): double-free avoided at %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jleave;
   }

   if(p.p_hc == a_memory_heap_list){
      if((a_memory_heap_list = p.p_hc->mhc_next) != NULL)
         a_memory_heap_list->mhc_prev = NULL;
   }else
      p.p_hc->mhc_prev->mhc_next = p.p_hc->mhc_next;
   if(p.p_hc->mhc_next != NULL)
      p.p_hc->mhc_next->mhc_prev = p.p_hc->mhc_prev;

   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = TRU1;
   /* Trash contents (also see [21c05f8]) */
   memset(vp, 0377, p.p_c->mc_user_size);

   --a_memory_heap_acur;
   a_memory_heap_mcur -= p.p_c->mc_user_size;

   if((n_psonce & n_PSO_REPRODUCIBLE) ||
         (n_poption & (n_PO_DEBUG | n_PO_MEMDEBUG))){
      p.p_hc->mhc_next = a_memory_heap_free;
      a_memory_heap_free = p.p_hc;
   }else
      (free)(p.p_vp);
jleave:
   NYD2_OU;
}
#endif /* HAVE_MEMORY_DEBUG */

FL void *
(n_autorec_alloc_from_pool)(void *vp, size_t size n_MEMORY_DEBUG_ARGS){
#ifdef HAVE_MEMORY_DEBUG
   ui32_t user_s;
#endif
   union a_memory_ptr p;
   union{
      struct a_memory_ars_buffer *abp;
      struct a_memory_ars_huge *ahp;
   } m, m2;
   struct a_memory_ars_ctx *macp;
   NYD2_IN;

   if((macp = vp) == NULL && (macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

#ifdef HAVE_MEMORY_DEBUG
   user_s = (ui32_t)size;
#endif
   if(size == 0)
      ++size;
#ifdef HAVE_MEMORY_DEBUG
   size += sizeof(struct a_memory_chunk) + a_MEMORY_HOPE_SIZE;
#endif
   size = a_MEMORY_ARS_ROUNDUP(size);

   /* Huge allocations are special */
   if(n_UNLIKELY(size > a_MEMORY_ARS_MAX)){
#ifdef HAVE_MEMORY_DEBUG
      if(n_poption & (n_PO_DEBUG | n_PO_MEMDEBUG))
         n_alert("n_autorec_alloc() of %" PRIuZ " bytes from %s, line %d",
            size, mdbg_file, mdbg_line);
#endif
      goto jhuge;
   }

   /* Search for a buffer with enough free space to serve request */
   for(m2.abp = NULL, m.abp = macp->mac_top; m.abp != NULL;
         m2.abp = m.abp, m.abp = m.abp->mab_last){
      if((p.p_cp = m.abp->mab_caster) <=
            &m.abp->mab_buf[sizeof(m.abp->mab_buf) - size]){
         /* Alignment is the one thing, the other is what is usually allocated,
          * and here about 40 bytes seems to be a good cut to avoid non-usable
          * casters.  Reown buffers supposed to be "full" to .mac_full */
         if(n_UNLIKELY((m.abp->mab_caster = &p.p_cp[size]) >=
               &m.abp->mab_buf[sizeof(m.abp->mab_buf) - 42])){
            if(m2.abp == NULL)
               macp->mac_top = m.abp->mab_last;
            else
               m2.abp->mab_last = m.abp->mab_last;
            m.abp->mab_last = macp->mac_full;
            macp->mac_full = m.abp;
         }
         goto jleave;
      }
   }

   /* Need a new buffer XXX "page" pool */
   m.abp = n_alloc(sizeof *m.abp);
   m.abp->mab_last = macp->mac_top;
   m.abp->mab_caster = &(m.abp->mab_bot = m.abp->mab_buf)[size];
   m.abp->mab_relax = NULL; /* Indicates allocation after _relax_create() */
   macp->mac_top = m.abp;
   p.p_cp = m.abp->mab_bot;

#ifdef HAVE_MEMORY_DEBUG
   ++a_memory_ars_ball;
   ++a_memory_ars_bcur;
   a_memory_ars_bmax = n_MAX(a_memory_ars_bmax, a_memory_ars_bcur);
#endif

jleave:
#ifdef HAVE_MEMORY_DEBUG
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_user_size = user_s;
   p.p_c->mc_size = (ui32_t)size;
   ++p.p_c;
   a_MEMORY_HOPE_SET(p_c, p);

   ++a_memory_ars_aall;
   a_memory_ars_mall += user_s;
#endif
   NYD2_OU;
   return p.p_vp;

jhuge:
   m.ahp = n_alloc(n_VSTRUCT_SIZEOF(struct a_memory_ars_huge, mah_buf) + size);
   m.ahp->mah_last = macp->mac_huge;
   macp->mac_huge = m.ahp;
   p.p_cp = m.ahp->mah_buf;
#ifdef HAVE_MEMORY_DEBUG
   ++a_memory_ars_hall;
   ++a_memory_ars_hcur;
   a_memory_ars_hmax = n_MAX(a_memory_ars_hmax, a_memory_ars_hcur);
#endif
   goto jleave;
}

FL void *
(n_autorec_calloc_from_pool)(void *vp, size_t nmemb, size_t size
      n_MEMORY_DEBUG_ARGS){
   void *rv;
   NYD2_IN;

   size *= nmemb; /* XXX overflow, but only used for struct inits */
   rv = (n_autorec_alloc_from_pool)(vp, size n_MEMORY_DEBUG_ARGSCALL);
   memset(rv, 0, size);
   NYD2_OU;
   return rv;
}

FL void
n_autorec_relax_create(void){
   struct a_memory_ars_ctx *macp;
   NYD2_IN;

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

   if(macp->mac_recur++ == 0){
      struct a_memory_ars_buffer *mabp;

      for(mabp = macp->mac_top; mabp != NULL; mabp = mabp->mab_last)
         mabp->mab_relax = mabp->mab_caster;
      for(mabp = macp->mac_full; mabp != NULL; mabp = mabp->mab_last)
         mabp->mab_relax = mabp->mab_caster;
   }
#if 0 && defined HAVE_DEVEL
   else
      n_err("n_autorec_relax_create(): recursion >0\n");
#endif
   NYD2_OU;
}

FL void
n_autorec_relax_gut(void){
   struct a_memory_ars_ctx *macp;
   NYD2_IN;

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

   assert(macp->mac_recur > 0);

   if(--macp->mac_recur == 0){
      struct a_memory_ars_buffer *mabp;

      macp->mac_recur = 1;
      n_autorec_relax_unroll();
      macp->mac_recur = 0;

      for(mabp = macp->mac_top; mabp != NULL; mabp = mabp->mab_last)
         mabp->mab_relax = NULL;
      for(mabp = macp->mac_full; mabp != NULL; mabp = mabp->mab_last)
         mabp->mab_relax = NULL;
   }
#if 0 && defined HAVE_DEVEL
   else
      n_err("n_autorec_relax_unroll(): recursion >0\n");
#endif
   NYD2_OU;
}

FL void
n_autorec_relax_unroll(void){
   /* The purpose of relaxation is only that it is possible to reset the
    * casters, *not* to give back memory to the system.  We are presumably in
    * an iteration over all messages of a mailbox, and it'd be quite
    * counterproductive to give the system allocator a chance to waste time */
   struct a_memory_ars_ctx *macp;
   NYD2_IN;

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

   assert(macp->mac_recur > 0);
   n_memory_check();

   if(macp->mac_recur == 1){
      struct a_memory_ars_buffer *mabp, *x, *y;

      /* Buffers in the full list may become usable again! */
      for(x = NULL, mabp = macp->mac_full; mabp != NULL; mabp = y){
         y = mabp->mab_last;

         if(mabp->mab_relax == NULL ||
               mabp->mab_relax < &mabp->mab_buf[sizeof(mabp->mab_buf) - 42]){
            if(x == NULL)
               macp->mac_full = y;
            else
               x->mab_last = y;
            mabp->mab_last = macp->mac_top;
            macp->mac_top = mabp;
         }else
            x = mabp;
      }

      for(mabp = macp->mac_top; mabp != NULL; mabp = mabp->mab_last){
         mabp->mab_caster = (mabp->mab_relax != NULL)
               ? mabp->mab_relax : mabp->mab_bot;
#ifdef HAVE_MEMORY_DEBUG
         memset(mabp->mab_caster, 0377,
            PTR2SIZE(&mabp->mab_buf[sizeof(mabp->mab_buf)] - mabp->mab_caster));
#endif
      }
   }
   NYD2_OU;
}

FL void *
(n_lofi_alloc)(size_t size n_MEMORY_DEBUG_ARGS){
#ifdef HAVE_MEMORY_DEBUG
   ui32_t user_s;
#endif
   union a_memory_ptr p;
   struct a_memory_ars_lofi *malp;
   bool_t isheap;
   struct a_memory_ars_ctx *macp;
   NYD2_IN;

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

#ifdef HAVE_MEMORY_DEBUG
   user_s = (ui32_t)size;
#endif
   if(size == 0)
      ++size;
   size += sizeof(struct a_memory_ars_lofi_chunk);
#ifdef HAVE_MEMORY_DEBUG
   size += a_MEMORY_HOPE_SIZE;
#endif
   size = a_MEMORY_LOFI_ROUNDUP(size);

   /* Huge allocations are special */
   if(n_UNLIKELY(isheap = (size > a_MEMORY_LOFI_MAX))){
#ifdef HAVE_MEMORY_DEBUG
      if(n_poption & (n_PO_DEBUG | n_PO_MEMDEBUG))
         n_alert("n_lofi_alloc() of %" PRIuZ " bytes from %s, line %d",
            size, mdbg_file, mdbg_line);
#endif
   }else if((malp = macp->mac_lofi) != NULL &&
         ((p.p_cp = malp->mal_caster) <= &malp->mal_max[-size])){
      malp->mal_caster = &p.p_cp[size];
      goto jleave;
   }

   /* Need a new buffer */
   /* C99 */{
      size_t i;

      i = n_VSTRUCT_SIZEOF(struct a_memory_ars_lofi, mal_buf) + size;
      i = n_MAX(i, n_MEMORY_AUTOREC_SIZE);
      malp = n_alloc(i);
      malp->mal_last = macp->mac_lofi;
      malp->mal_caster = &malp->mal_buf[size];
      i -= n_VSTRUCT_SIZEOF(struct a_memory_ars_lofi, mal_buf);
      malp->mal_max = &malp->mal_buf[i];
      macp->mac_lofi = malp;
      p.p_cp = malp->mal_buf;

#ifdef HAVE_MEMORY_DEBUG
      ++a_memory_lofi_ball;
      ++a_memory_lofi_bcur;
      a_memory_lofi_bmax = n_MAX(a_memory_lofi_bmax, a_memory_lofi_bcur);
#endif
   }

jleave:
   p.p_alc->malc_last = macp->mac_lofi_top;
   macp->mac_lofi_top = p.p_alc;
   if(isheap)
      p.p_alc->malc_last = (struct a_memory_ars_lofi_chunk*)
            ((uintptr_t)p.p_alc->malc_last | 0x1);

#ifndef HAVE_MEMORY_DEBUG
   ++p.p_alc;
#else
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_user_size = user_s;
   p.p_c->mc_size = (ui32_t)size;
   ++p.p_alc;
   a_MEMORY_HOPE_SET(p_alc, p);

   ++a_memory_lofi_aall;
   ++a_memory_lofi_acur;
   a_memory_lofi_amax = n_MAX(a_memory_lofi_amax, a_memory_lofi_acur);
   a_memory_lofi_mall += user_s;
   a_memory_lofi_mcur += user_s;
   a_memory_lofi_mmax = n_MAX(a_memory_lofi_mmax, a_memory_lofi_mcur);
#endif
   NYD2_OU;
   return p.p_vp;
}

FL void
(n_lofi_free)(void *vp n_MEMORY_DEBUG_ARGS){
#ifdef HAVE_MEMORY_DEBUG
   bool_t isbad;
#endif
   union a_memory_ptr p;
   struct a_memory_ars_ctx *macp;
   NYD2_IN;

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

   if((p.p_vp = vp) == NULL){
#ifdef HAVE_MEMORY_DEBUG
      n_err("n_lofi_free(NULL) from %s, line %d\n", mdbg_file, mdbg_line);
#endif
      goto jleave;
   }

#ifdef HAVE_MEMORY_DEBUG
   a_MEMORY_HOPE_GET(p_alc, p, isbad);
   --p.p_alc;

   if(p.p_c->mc_isfree){
      n_err("n_lofi_free(): double-free avoided at %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jleave;
   }
   p.p_c->mc_isfree = TRU1;
   memset(vp, 0377, p.p_c->mc_user_size);

   if(p.p_alc != macp->mac_lofi_top){
      n_err("n_lofi_free(): this is not alloca top at %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jleave;
   }

   ++p.p_alc;
#endif /* HAVE_MEMORY_DEBUG */

   a_memory_lofi_free(macp, --p.p_alc);
jleave:
   NYD2_OU;
}

FL void *
n_lofi_snap_create(void){ /* TODO avoid temporary alloc */
   void *rv;
   NYD2_IN;

   rv = n_lofi_alloc(1);
   NYD2_OU;
   return rv;
}

FL void
n_lofi_snap_unroll(void *cookie){ /* TODO optimise */
   union a_memory_ptr p;
   struct a_memory_ars_ctx *macp;
   NYD2_IN;

   n_memory_check();

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

   for(;;){
      p.p_alc = macp->mac_lofi_top;
      a_memory_lofi_free(macp, p.p_vp);
      ++p.p_alc;
#ifdef HAVE_MEMORY_DEBUG
      a_MEMORY_HOPE_INC(p.p_ui8p);
#endif
      if(p.p_vp == cookie)
         break;
   }
   NYD2_OU;
}

#ifdef HAVE_MEMORY_DEBUG
FL int
c_memtrace(void *vp){
   /* For a_MEMORY_HOPE_GET() */
   char const * const mdbg_file = "memtrace()";
   int const mdbg_line = -1;
   struct a_memory_ars_buffer *mabp;
   struct a_memory_ars_lofi_chunk *malcp;
   struct a_memory_ars_lofi *malp;
   struct a_memory_ars_ctx *macp;
   bool_t isbad;
   union a_memory_ptr p, xp;
   size_t lines;
   FILE *fp;
   NYD2_IN;

   vp = (void*)0x1;
   if((fp = Ftmp(NULL, "memtr", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr("tmpfile", 0);
      goto jleave;
   }
   lines = 0;

   fprintf(fp,
      "Last-Out-First-In (alloca) storage:\n"
      "       Buffer cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n"
      "  Allocations cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n"
      "        Bytes cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n\n",
      a_memory_lofi_bcur, a_memory_lofi_bmax, a_memory_lofi_ball,
      a_memory_lofi_acur, a_memory_lofi_amax, a_memory_lofi_aall,
      a_memory_lofi_mcur, a_memory_lofi_mmax, a_memory_lofi_mall);
   lines += 7;

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;
   for(; macp != NULL; macp = macp->mac_outer){
      fprintf(fp, "  Evaluation stack context %p (outer: %p):\n",
         (void*)macp, (void*)macp->mac_outer);
      ++lines;

      for(malp = macp->mac_lofi; malp != NULL;){
         fprintf(fp, "    Buffer %p%s, %" PRIuZ "/%" PRIuZ " used/free:\n",
            (void*)malp, ((uintptr_t)malp->mal_last & 0x1 ? " (huge)" : ""),
            PTR2SIZE(malp->mal_caster - &malp->mal_buf[0]),
            PTR2SIZE(malp->mal_max - malp->mal_caster));
         ++lines;
         malp = malp->mal_last;
         malp = (struct a_memory_ars_lofi*)((uintptr_t)malp & ~1);
      }

      for(malcp = macp->mac_lofi_top; malcp != NULL;){
         p.p_alc = malcp;
         malcp = (struct a_memory_ars_lofi_chunk*)
               ((uintptr_t)malcp->malc_last & ~0x1);
         xp = p;
         ++xp.p_alc;
         a_MEMORY_HOPE_GET_TRACE(p_alc, xp, isbad);
         fprintf(fp, "      %s%p (%u bytes): %s, line %u\n",
            (isbad ? "! CANARY ERROR (LOFI): " : ""), xp.p_vp,
            p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
      }
   }

   fprintf(fp,
      "\nAuto-reclaimed storage:\n"
      "           Buffers cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n"
      "  Huge allocations cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n"
      "                Allocations all: %" PRIuZ ", Bytes all: %" PRIuZ "\n\n",
      a_memory_ars_bcur, a_memory_ars_bmax, a_memory_ars_ball,
      a_memory_ars_hcur, a_memory_ars_hmax, a_memory_ars_hall,
      a_memory_ars_aall, a_memory_ars_mall);
   lines += 7;

   for(macp = n_go_data->gdc_mempool; macp != NULL; macp = macp->mac_outer){
      fprintf(fp, "  Evaluation stack context %p (outer: %p):\n",
         (void*)macp, (void*)macp->mac_outer);
      ++lines;

      for(mabp = macp->mac_top; mabp != NULL; mabp = mabp->mab_last){
         fprintf(fp, "    Buffer %p, %" PRIuZ "/%" PRIuZ " used/free:\n",
            (void*)mabp,
            PTR2SIZE(mabp->mab_caster - &mabp->mab_buf[0]),
            PTR2SIZE(&mabp->mab_buf[sizeof(mabp->mab_buf)] - mabp->mab_caster));
         ++lines;

         for(p.p_cp = mabp->mab_buf; p.p_cp < mabp->mab_caster;
               ++lines, p.p_cp += p.p_c->mc_size){
            xp = p;
            ++xp.p_c;
            a_MEMORY_HOPE_GET_TRACE(p_c, xp, isbad);
            fprintf(fp, "      %s%p (%u bytes): %s, line %u\n",
               (isbad ? "! CANARY ERROR (ARS, top): " : ""), xp.p_vp,
               p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
         }
         ++lines;
      }

      for(mabp = macp->mac_full; mabp != NULL; mabp = mabp->mab_last){
         fprintf(fp, "    Buffer %p, full:\n", (void*)mabp);
         ++lines;

         for(p.p_cp = mabp->mab_buf; p.p_cp < mabp->mab_caster;
               ++lines, p.p_cp += p.p_c->mc_size){
            xp = p;
            ++xp.p_c;
            a_MEMORY_HOPE_GET_TRACE(p_c, xp, isbad);
            fprintf(fp, "      %s%p (%u bytes): %s, line %u\n",
               (isbad ? "! CANARY ERROR (ARS, full): " : ""), xp.p_vp,
               p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
         }
         ++lines;
      }
   }

   fprintf(fp,
      "\nHeap memory buffers:\n"
      "  Allocation cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n"
      "       Bytes cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n\n",
      a_memory_heap_acur, a_memory_heap_amax, a_memory_heap_aall,
      a_memory_heap_mcur, a_memory_heap_mmax, a_memory_heap_mall);
   lines += 6;

   for(p.p_hc = a_memory_heap_list; p.p_hc != NULL;
         ++lines, p.p_hc = p.p_hc->mhc_next){
      xp = p;
      ++xp.p_hc;
      a_MEMORY_HOPE_GET_TRACE(p_hc, xp, isbad);
      fprintf(fp, "  %s%p (%u bytes): %s, line %u\n",
         (isbad ? "! CANARY ERROR (heap): " : ""), xp.p_vp,
         p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
   }

   if((n_psonce & n_PSO_REPRODUCIBLE) ||
         (n_poption & (n_PO_DEBUG | n_PO_MEMDEBUG))){
      fprintf(fp, "Heap buffers lingering for n_free():\n");
      ++lines;

      for(p.p_hc = a_memory_heap_free; p.p_hc != NULL;
            ++lines, p.p_hc = p.p_hc->mhc_next){
         xp = p;
         ++xp.p_hc;
         a_MEMORY_HOPE_GET_TRACE(p_hc, xp, isbad);
         fprintf(fp, "  %s%p (%u bytes): %s, line %u\n",
            (isbad ? "! CANARY ERROR (free): " : ""), xp.p_vp,
            p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
      }
   }

   page_or_print(fp, lines);
   Fclose(fp);
   vp = NULL;
jleave:
   NYD2_OU;
   return (vp != NULL);
}

FL bool_t
n__memory_check(char const *mdbg_file, int mdbg_line){
   union a_memory_ptr p, xp;
   struct a_memory_ars_buffer *mabp;
   struct a_memory_ars_lofi_chunk *malcp;
   struct a_memory_ars_ctx *macp;
   bool_t anybad, isbad;
   NYD2_IN;

   anybad = FAL0;

   if((macp = n_go_data->gdc_mempool) == NULL)
      macp = n_go_data->gdc_mempool = n_go_data->gdc__mempool_buf;

   /* Alloca */

   for(malcp = macp->mac_lofi_top; malcp != NULL;){
      p.p_alc = malcp;
      malcp = (struct a_memory_ars_lofi_chunk*)
            ((uintptr_t)malcp->malc_last & ~0x1);
      xp = p;
      ++xp.p_alc;
      a_MEMORY_HOPE_GET_TRACE(p_alc, xp, isbad);
      if(isbad){
         anybad = TRU1;
         n_err(
            "! CANARY ERROR (LOFI): %p (%u bytes): %s, line %u\n",
            xp.p_vp, p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
      }
   }

   /* Auto-reclaimed */

   for(mabp = macp->mac_top; mabp != NULL; mabp = mabp->mab_last){
      for(p.p_cp = mabp->mab_buf; p.p_cp < mabp->mab_caster;
            p.p_cp += p.p_c->mc_size){
         xp = p;
         ++xp.p_c;
         a_MEMORY_HOPE_GET_TRACE(p_c, xp, isbad);
         if(isbad){
            anybad = TRU1;
            n_err(
               "! CANARY ERROR (ARS, top): %p (%u bytes): %s, line %u\n",
               xp.p_vp, p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
         }
      }
   }

   for(mabp = macp->mac_full; mabp != NULL; mabp = mabp->mab_last){
      for(p.p_cp = mabp->mab_buf; p.p_cp < mabp->mab_caster;
            p.p_cp += p.p_c->mc_size){
         xp = p;
         ++xp.p_c;
         a_MEMORY_HOPE_GET_TRACE(p_c, xp, isbad);
         if(isbad){
            anybad = TRU1;
            n_err(
               "! CANARY ERROR (ARS, full): %p (%u bytes): %s, line %u\n",
               xp.p_vp, p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
         }
      }
   }

   /* Heap*/

   for(p.p_hc = a_memory_heap_list; p.p_hc != NULL; p.p_hc = p.p_hc->mhc_next){
      xp = p;
      ++xp.p_hc;
      a_MEMORY_HOPE_GET_TRACE(p_hc, xp, isbad);
      if(isbad){
         anybad = TRU1;
         n_err(
            "! CANARY ERROR (heap): %p (%u bytes): %s, line %u\n",
            xp.p_vp, p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
      }
   }

   if((n_psonce & n_PSO_REPRODUCIBLE) ||
         (n_poption & (n_PO_DEBUG | n_PO_MEMDEBUG))){
      for(p.p_hc = a_memory_heap_free; p.p_hc != NULL;
            p.p_hc = p.p_hc->mhc_next){
         xp = p;
         ++xp.p_hc;
         a_MEMORY_HOPE_GET_TRACE(p_hc, xp, isbad);
         if(isbad){
            anybad = TRU1;
            n_err(
              "! CANARY ERROR (free): %p (%u bytes): %s, line %u\n",
               xp.p_vp, p.p_c->mc_user_size, p.p_c->mc_file, p.p_c->mc_line);
         }
      }
   }

   if(anybad && ok_blook(memdebug))
      n_panic("Memory errors encountered");
   NYD2_OU;
   return anybad;
}
#endif /* HAVE_MEMORY_DEBUG */

/* s-it-mode */
