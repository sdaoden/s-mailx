/*@ Implementation of mem-bag.h.
 *@ TODO - flux memory: pool specific (like _auto_ and _lofi_), but normal
 *@ TODO   heap beside, which can be free()d in random order etc.
 *@ XXX - With (this) new approach, the memory backing _auto_buf and
 *@ XXX  _lofi_pool is of equal size, and thus a (global?) cache could be added
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE su_mem_bag
#define su_SOURCE
#define su_SOURCE_MEM_BAG

#include "su/code.h"

#include "su/mem.h"

#include "su/mem-bag.h"
#include "su/code-in.h"

su_EMPTY_FILE()
#ifdef su_HAVE_MEM_BAG

/* Are we a managing hull for the heap cache, for ASAN etc. integration? */
#if defined su_HAVE_DEBUG || defined su_HAVE_MEM_CANARIES_DISABLE
# define a_MEMBAG_HULL 1
#else
# define a_MEMBAG_HULL 0
#endif

/* We presume a buffer is full if less than this remain (in order to avoid
 * searching in buffers which will never be able to serve anything again) */
#define a_MEMBAG_BSZ_GAP 32u

/* .mb_bsz limits (lower must be (much) greater than a_MEMBAG_BSZ_BASE plus
 * a_MEMBAG_BSZ_GAP: CTAsserted) */
#define a_MEMBAG_BSZ_LOWER 1024u
#define a_MEMBAG_BSZ_UPPER (1024u * 1024 * 10)

CTA(a_MEMBAG_BSZ_UPPER <= S32_MAX, "Excesses datatype storage");

/* When allocating a .mb_bsz buffer, add management sizes (back) */
#define a_MEMBAG_BSZ_BASE MAX(Z_ALIGN(a_MEMBAG__SZA), Z_ALIGN(a_MEMBAG__SZB))

#ifdef su_HAVE_MEM_BAG_AUTO
# define a_MEMBAG__SZA VSTRUCT_SIZEOF(struct su__mem_bag_auto_buf,mbab_buf)
#else
# define a_MEMBAG__SZA sizeof(up)
#endif
#ifdef su_HAVE_MEM_BAG_LOFI
# define a_MEMBAG__SZB VSTRUCT_SIZEOF(struct su__mem_bag_lofi_pool,mblp_buf)
#else
# define a_MEMBAG__SZB sizeof(up)
#endif

/* enum su_mem_bag_alloc_flags == enum su_mem_alloc_flags */
CTAV(S(u32,su_MEM_BAG_ALLOC_NONE) == S(u32,su_MEM_ALLOC_NONE));
CTAV(S(u32,su_MEM_BAG_ALLOC_CLEAR) == S(u32,su_MEM_ALLOC_CLEAR));
CTAV(S(u32,su_MEM_BAG_ALLOC_OVERFLOW_OK) == S(u32,su_MEM_ALLOC_OVERFLOW_OK));
CTAV(S(u32,su_MEM_BAG_ALLOC_NOMEM_OK) == S(u32,su_MEM_ALLOC_NOMEM_OK));
CTAV(S(u32,su_MEM_BAG_ALLOC_MUSTFAIL) == S(u32,su_MEM_ALLOC_MUSTFAIL));

#ifdef su_HAVE_MEM_BAG_AUTO
struct su__mem_bag_auto_buf{
   struct su__mem_bag_auto_buf *mbab_last;
   char *mbab_bot;      /* For _fixate(): keep startup memory lingering */
   char *mbab_relax;    /* !NIL: used by _relax_unroll(), not .mbab_bot */
   char *mbab_caster;   /* Point of casting off memory */
   char mbab_buf[VFIELD_SIZE(0)]; /* MEMBAG_HULL: void*[] */
};

struct su__mem_bag_auto_huge{
   struct su__mem_bag_auto_huge *mbah_last;
   char mbah_buf[VFIELD_SIZE(0)];   /* MEMBAG_HULL: void* to real chunk */
};
#endif /* su_HAVE_MEM_BAG_AUTO */

#ifdef su_HAVE_MEM_BAG_LOFI
struct su__mem_bag_lofi_pool{
   struct su__mem_bag_lofi_pool *mblp_last;
   char *mblp_caster;
   char mblp_buf[VFIELD_SIZE(0)];   /* su__mem_bag_lofi_chunk* */
};

struct su__mem_bag_lofi_chunk{
   /* Bit 0x1 set: .mblc_buf contains indeed a void* to user heap memory.
    * (Always so with MEMBAG_HULL) */
   struct su__mem_bag_lofi_chunk *mblc_last;
   char mblc_buf[VFIELD_SIZE(0)];
};
#endif

CTA(a_MEMBAG_BSZ_BASE + a_MEMBAG_BSZ_GAP < a_MEMBAG_BSZ_LOWER,
   "The chosen buffer size minimum is (much) too small");

/* Free .mb_lofi_top */
#ifdef su_HAVE_MEM_BAG_LOFI
su_SINLINE struct su_mem_bag *a_membag_lofi_free_top(struct su_mem_bag *self);
#endif

/* Free vp, which really is storage for (a) user heap pointer(s) */
#ifdef su_HAVE_MEM_BAG_AUTO
su_SINLINE void a_membag_free_auto_hulls(void *vp, char *maxp);
su_SINLINE void a_membag_free_auto_huge_hull(void *vp);
#endif
#ifdef su_HAVE_MEM_BAG_LOFI
su_SINLINE void a_membag_free_lofi_hulls(void *vp, char *maxp);
#endif

#ifdef su_HAVE_MEM_BAG_LOFI
su_SINLINE struct su_mem_bag *
a_membag_lofi_free_top(struct su_mem_bag *self){
   struct su__mem_bag_lofi_pool *mblpp;
   boole isheap;
   union {struct su__mem_bag_lofi_chunk *mblc; up u; void *v; void **vp;
      struct su__mem_bag_lofi_pool *mblp;} p;
   struct su__mem_bag_lofi_chunk *mblcp;
   NYD2_IN;

   mblcp = self->mb_lofi_top;
   p.mblc = mblcp->mblc_last;
   if((isheap = (p.u & 0x1)))
      p.u ^= 0x1;
   self->mb_lofi_top = p.mblc;

   if(isheap){
      p.v = &mblcp->mblc_buf[0];
      su_FREE(p.vp[0]);
   }else
      ASSERT(!a_MEMBAG_HULL);

   /* We remove free pools but the last (== the first) one */
   mblpp = self->mb_lofi_pool;
   if((mblpp->mblp_caster = R(char*,mblcp)) == mblpp->mblp_buf &&
         (p.mblp = mblpp->mblp_last) != NIL){
      self->mb_lofi_pool = p.mblp;
      su_FREE(mblpp);
   }
   NYD2_OU;
   return self;
}
#endif /* su_HAVE_MEM_BAG_LOFI */

#ifdef su_HAVE_MEM_BAG_AUTO
su_SINLINE void
a_membag_free_auto_hulls(void *vp, char *maxp){
   NYD2_IN;
   UNUSED(vp);
   UNUSED(maxp);

# if a_MEMBAG_HULL
   /* C99 */{
      union {char *c; char **cp;} p;

      for(p.c = S(char*,vp); p.c < maxp; ++p.cp)
         su_FREE(*p.cp);
   }
# endif
   NYD2_OU;
}

su_SINLINE void
a_membag_free_auto_huge_hull(void *vp){
   NYD2_IN;
   UNUSED(vp);

# if a_MEMBAG_HULL
   /* C99 */{
      union {void *v; void **vp;} p;

      p.v = vp;
      su_FREE(p.vp[0]);
   }
# endif
   NYD2_OU;
}
#endif /* su_HAVE_MEM_BAG_AUTO */

#ifdef su_HAVE_MEM_BAG_LOFI
su_SINLINE void
a_membag_free_lofi_hulls(void *vp, char *maxp){
   NYD2_IN;
   UNUSED(vp);
   UNUSED(maxp);

# if a_MEMBAG_HULL
   /* C99 */{
      union {char *c; struct su__mem_bag_lofi_chunk *mblc;} p;

      for(p.c = S(char*,vp); p.c < maxp;
            p.c += VSTRUCT_SIZEOF(struct su__mem_bag_lofi_chunk,mblc_buf) +
                  sizeof p.c){
         union {void *v; void **vp;} p2;

         p2.v = &p.mblc->mblc_buf[0];
         su_FREE(p2.vp[0]);
      }
   }
# endif
   NYD2_OU;
}
#endif /* su_HAVE_MEM_BAG_LOFI */

struct su_mem_bag *
su_mem_bag_create(struct su_mem_bag *self, uz bsz){
   NYD_IN;
   ASSERT(self);

   su_mem_set(self, 0, sizeof *self);

   if(bsz == 0)
      bsz = su_PAGE_SIZE * 2;
   else
      bsz = CLIP(bsz, a_MEMBAG_BSZ_LOWER, a_MEMBAG_BSZ_UPPER);
   bsz -= a_MEMBAG_BSZ_BASE;
   self->mb_bsz = S(u32,bsz);
   bsz -= a_MEMBAG_BSZ_GAP;
   self->mb_bsz_wo_gap = S(u32,bsz);
   NYD_OU;
   return self;
}

void
su_mem_bag_gut(struct su_mem_bag *self){
   NYD_IN;
   ASSERT(self);

   DBG( if(self->mb_top != NIL)
      su_log_write(su_LOG_DEBUG, "su_mem_bag_gut(%p): has bag stack!\n",
         self); )

   self = su_mem_bag_reset(self);

   /* _fixate()d auto-reclaimed memory will still linger now */
#ifdef su_HAVE_MEM_BAG_AUTO
   ASSERT_EXEC(self->mb_auto_full == NIL, (void)0);
   ASSERT_EXEC(self->mb_auto_huge == NIL, (void)0);
   /* C99 */{
      struct su__mem_bag_auto_buf *mbabp;

      while((mbabp = self->mb_auto_top) != NIL){
         self->mb_auto_top = mbabp->mbab_last;
         a_membag_free_auto_hulls(mbabp->mbab_buf, mbabp->mbab_caster);
         su_FREE(mbabp);
      }
   }
#endif

   /* We (may) have kept a single LOFI buffer */
#ifdef su_HAVE_MEM_BAG_LOFI
   /* C99 */{
      struct su__mem_bag_lofi_pool *mblpp;

      if((mblpp = self->mb_lofi_pool) != NIL){
         ASSERT_EXEC(mblpp->mblp_last == NIL, (void)0);
         DBG( self->mb_lofi_pool = NIL; )
         a_membag_free_lofi_hulls(mblpp->mblp_buf, mblpp->mblp_caster);
         su_FREE(mblpp);
      }
   }
#endif
   NYD_OU;
}

struct su_mem_bag *
su_mem_bag_fixate(struct su_mem_bag *self){
   struct su_mem_bag *oself;
   NYD2_IN;
   ASSERT(self);

   if((oself = self)->mb_top != NIL)
      self = self->mb_top;

#ifdef su_HAVE_MEM_BAG_AUTO
   /* C99 */{
      struct su__mem_bag_auto_buf *mbabp;

      for(mbabp = self->mb_auto_top; mbabp != NIL; mbabp = mbabp->mbab_last)
         mbabp->mbab_bot = mbabp->mbab_caster;
      for(mbabp = self->mb_auto_full; mbabp != NIL; mbabp = mbabp->mbab_last)
         mbabp->mbab_bot = mbabp->mbab_caster;
   }
#endif

   self = oself;
   NYD2_OU;
   return self;
}

struct su_mem_bag *
su_mem_bag_reset(struct su_mem_bag *self){
   NYD_IN;
   ASSERT(self);

   /* C99 */{
      struct su_mem_bag *mbp;

      while((mbp = self->mb_top) != NIL){
         self->mb_top = mbp->mb_outer;
         su_mem_bag_gut(mbp);
      }
   }

#ifdef su_HAVE_MEM_BAG_AUTO
   /* C99 */{
      struct su__mem_bag_auto_buf **mbabpp, *mbabp;

      /* Forcefully gut() an active relaxation */
      if(self->mb_auto_relax_recur > 0){
         DBG( su_log_write(su_LOG_DEBUG,
            "su_mem_bag_reset(): has relaxation!\n"); )
         self->mb_auto_relax_recur = 1;
         self = su_mem_bag_auto_relax_gut(self);
      }

      /* Simply move buffers away from .mb_auto_full */
      for(mbabpp = &self->mb_auto_full; (mbabp = *mbabpp) != NIL;){
         *mbabpp = mbabp->mbab_last;
         mbabp->mbab_last = self->mb_auto_top;
         self->mb_auto_top = mbabp;
      }

      /* Then give away all buffers which are not _fixate()d */
      mbabp = *(mbabpp = &self->mb_auto_top);
      *mbabpp = NIL;
      while(mbabp != NIL){
         struct su__mem_bag_auto_buf *tmp;

         tmp = mbabp;
         mbabp = mbabp->mbab_last;

         a_membag_free_auto_hulls(tmp->mbab_bot, tmp->mbab_caster);
         if(tmp->mbab_bot != tmp->mbab_buf){
            tmp->mbab_relax = NIL;
            tmp->mbab_caster = tmp->mbab_bot;
            tmp->mbab_last = *mbabpp;
            *mbabpp = tmp;
         }else
            su_FREE(tmp);
      }

      /* Huge allocations simply vanish */
      /* C99 */{
         struct su__mem_bag_auto_huge **mbahpp, *mbahp;

         for(mbahpp = &self->mb_auto_huge; (mbahp = *mbahpp) != NIL;){
            *mbahpp = mbahp->mbah_last;
            a_membag_free_auto_huge_hull(mbahp->mbah_buf);
            su_FREE(mbahp);
         }
      }
   }
#endif /* su_HAVE_MEM_BAG_AUTO */

#ifdef su_HAVE_MEM_BAG_LOFI
   if(self->mb_lofi_top != NIL){
      DBG( su_log_write(su_LOG_DEBUG,
         "su_mem_bag_reset(%p): still has LOFI memory!\n", self); )
      do
         self = a_membag_lofi_free_top(self);
      while(self->mb_lofi_top != NIL);
   }
#endif /* su_HAVE_MEM_BAG_LOFI */

   NYD_OU;
   return self;
}

struct su_mem_bag *
su_mem_bag_push(struct su_mem_bag *self, struct su_mem_bag *that_one){
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD(that_one != NIL);
   ASSERT_NYD(that_one->mb_outer_save == NIL /* max once yet! */);

   that_one->mb_outer_save = that_one->mb_outer;
   that_one->mb_outer = self->mb_top;
   self->mb_top = that_one;
   NYD_OU;
   return self;
}

struct su_mem_bag *
su_mem_bag_pop(struct su_mem_bag *self, struct su_mem_bag *that_one){
   NYD_IN;
   ASSERT(self);
   ASSERT_NYD(that_one != NIL);

   for(;;){
      struct su_mem_bag *mbp;

      mbp = self->mb_top;
      ASSERT_EXEC(mbp != NIL /* False stack pop()ped! */, break);
      self->mb_top = mbp->mb_outer;
      mbp->mb_outer = mbp->mb_outer_save;
      mbp->mb_outer_save = NIL;

      if(mbp == that_one)
         break;
   }
   NYD_OU;
   return self;
}

#ifdef su_HAVE_MEM_BAG_AUTO
struct su_mem_bag *
su_mem_bag_auto_relax_create(struct su_mem_bag *self){
   struct su_mem_bag *oself;
   NYD2_IN;
   ASSERT(self);

   if((oself = self)->mb_top != NIL)
      self = oself->mb_top;

   if(self->mb_auto_relax_recur++ == 0){
      struct su__mem_bag_auto_buf *mbabp;

      for(mbabp = self->mb_auto_top; mbabp != NIL; mbabp = mbabp->mbab_last)
         mbabp->mbab_relax = mbabp->mbab_caster;
      for(mbabp = self->mb_auto_full; mbabp != NIL; mbabp = mbabp->mbab_last)
         mbabp->mbab_relax = mbabp->mbab_caster;
   }

   self = oself;
   NYD2_OU;
   return self;
}

struct su_mem_bag *
su_mem_bag_auto_relax_gut(struct su_mem_bag *self){
   struct su_mem_bag *oself;
   NYD2_IN;
   ASSERT(self);

   if((oself = self)->mb_top != NIL)
      self = oself->mb_top;
   ASSERT_NYD_EXEC(self->mb_auto_relax_recur > 0, self = oself);

   if(--self->mb_auto_relax_recur == 0){
      struct su__mem_bag_auto_buf *mbabp;

      self->mb_auto_relax_recur = 1;
      self = su_mem_bag_auto_relax_unroll(self);
      self->mb_auto_relax_recur = 0;

      for(mbabp = self->mb_auto_top; mbabp != NIL; mbabp = mbabp->mbab_last)
         mbabp->mbab_relax = NIL;
      for(mbabp = self->mb_auto_full; mbabp != NIL; mbabp = mbabp->mbab_last)
         mbabp->mbab_relax = NIL;
   }

   self = oself;
   NYD2_OU;
   return self;
}

struct su_mem_bag *
su_mem_bag_auto_relax_unroll(struct su_mem_bag *self){
   /* The purpose of relaxation is to reset the casters, *not* to give back
    * memory to the system.  Presumably in some kind of iteration, it would be
    * counterproductive to give the system allocator a chance to waste time */
   struct su_mem_bag *oself;
   NYD2_IN;
   ASSERT(self);

   if((oself = self)->mb_top != NIL)
      self = oself->mb_top;
   ASSERT_NYD_EXEC(self->mb_auto_relax_recur > 0, self = oself);

   if(self->mb_auto_relax_recur == 1){
      struct su__mem_bag_auto_buf **pp, *mbabp, *p;

      /* Buffers in the full list may become usable again.. */
      for(pp = &self->mb_auto_full, mbabp = *pp; (p = mbabp) != NIL;){
         mbabp = mbabp->mbab_last;

         /* ..if either they are not covered by relaxation, or if they have
          * been relaxed with "sufficient" buffer size available */
         if(p->mbab_relax == NIL ||
               p->mbab_relax <= &p->mbab_buf[self->mb_bsz_wo_gap]){
            p->mbab_last = self->mb_auto_top;
            self->mb_auto_top = p;
         }else{
            *pp = p;
            pp = &p->mbab_last;
         }
      }
      *pp = NIL;

      for(mbabp = self->mb_auto_top; mbabp != NIL; mbabp = mbabp->mbab_last){
         a_membag_free_auto_hulls(((mbabp->mbab_relax != NIL)
            ? mbabp->mbab_relax : mbabp->mbab_bot), mbabp->mbab_caster);
         mbabp->mbab_caster = (mbabp->mbab_relax != NIL) ? mbabp->mbab_relax
               : mbabp->mbab_bot;
      }
   }

   self = oself;
   NYD2_OU;
   return self;
}

void *
su_mem_bag_auto_allocate(struct su_mem_bag *self, uz size, uz no, u32 mbaf
      su_DBG_LOC_ARGS_DECL){
   void *rv;
   NYD_IN;
   ASSERT(self);

   if(self->mb_top != NIL)
      self = self->mb_top;
   if(UNLIKELY(size == 0))
      size = 1;
   if(UNLIKELY(no == 0))
      no = 1;
   mbaf &= su__MEM_BAG_ALLOC_USER_MASK;

   rv = NIL;

   /* Any attempt to allocate more is prevented (for simplicity, going for
    * UZ_MAX would require more expensive care: for nothing) */
   if(LIKELY(S(uz,S32_MAX) / no > size)){
      uz chunksz;

      size *= no;
      chunksz = a_MEMBAG_HULL ? sizeof(up) : Z_ALIGN(size);

      /* Pool allocation possible?  Huge allocations are special */
      if(LIKELY(chunksz <= self->mb_bsz)){
         char *top, *cp;
         struct su__mem_bag_auto_buf **mbabpp, *mbabp;

         /* For all non-full pools.. */
         for(mbabpp = &self->mb_auto_top, mbabp = *mbabpp; mbabp != NIL;
               mbabpp = &mbabp->mbab_last, mbabp = mbabp->mbab_last){
            top = &mbabp->mbab_buf[self->mb_bsz];

            /* Check whether allocation can be placed.. */
            if(top - chunksz >= (cp = mbabp->mbab_caster)){
               rv = cp;
               mbabp->mbab_caster = cp = &cp[chunksz];
               /* ..and move this pool to the full list if the possibility
                * of further allocations is low */
               if(cp > (top -= a_MEMBAG_BSZ_GAP)){
                  *mbabpp = mbabp->mbab_last;
                  mbabp->mbab_last = self->mb_auto_full;
                  self->mb_auto_full = mbabp;
               }
               goto jhave_pool;
            }
         }

         /* Ran out of usable pools.  Allocate one, and make it the serving
          * head (top) if possible */
         mbabp = S(struct su__mem_bag_auto_buf*,su_ALLOCATE_LOC((self->mb_bsz +
                  a_MEMBAG_BSZ_BASE), 1, (mbaf | su_MEM_ALLOC_MARK_AUTO),
               su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE));
         if(mbabp == NIL)
            goto jleave;
         cp = mbabp->mbab_buf;
         rv = mbabp->mbab_bot = top = mbabp->mbab_buf;
         mbabp->mbab_relax = NIL;
         mbabp->mbab_caster = cp = &top[chunksz];
         if(LIKELY(cp <= (top += self->mb_bsz_wo_gap))){
            mbabp->mbab_last = self->mb_auto_top;
            self->mb_auto_top = mbabp;
         }else{
            mbabp->mbab_last = self->mb_auto_full;
            self->mb_auto_full = mbabp;
         }

         /* Have a pool, chunk in rv (and: cp==.mbab_caster == rv+chunksz) */
jhave_pool:;
#if a_MEMBAG_HULL
         /* C99 */{
            union {void *p; void **pp;} v;

            v.p = rv;
            rv = su_ALLOCATE_LOC(size, 1, (mbaf | su_MEM_ALLOC_MARK_AUTO),
                  su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE);
            if(rv != NIL)
               *v.pp = rv;
            else{
               mbabp->mbab_caster = v.p;
               goto jleave;
            }
         }
#endif
      }else{
         struct su__mem_bag_auto_huge *mbahp;

         DBG( su_log_write(su_LOG_DEBUG, "su_mem_bag_auto_allocate(): huge: "
            "%" PRIuZ " bytes from %s:%" PRIu32 "!\n",
            size  su_DBG_LOC_ARGS_USE); )

         mbahp = S(struct su__mem_bag_auto_huge*,su_ALLOCATE_LOC(
               VSTRUCT_SIZEOF(struct su__mem_bag_auto_huge,mbah_buf) + chunksz,
               1, (mbaf | su_MEM_ALLOC_MARK_AUTO_HUGE),
               su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE));
         if(UNLIKELY(mbahp == NIL))
            goto jleave;
#if !a_MEMBAG_HULL
         rv = mbahp->mbah_buf;
#else
         rv = su_ALLOCATE_LOC(size, 1,
               (mbaf | su_MEM_ALLOC_MARK_AUTO_HUGE),
               su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE);
         if(rv != NIL){
            union {void *p; void **pp;} v;

            v.p = mbahp->mbah_buf;
            *v.pp = rv;
         }else{
            su_FREE(mbahp);
            goto jleave;
         }
#endif
         mbahp->mbah_last = self->mb_auto_huge;
         self->mb_auto_huge = mbahp;
      }

      if(mbaf & su_MEM_BAG_ALLOC_CLEAR)
         su_mem_set(rv, 0, size);
   }else
      su_state_err(su_STATE_ERR_OVERFLOW, mbaf,
         _("SU memory bag: auto allocation request"));
jleave:
   NYD_OU;
   return rv;
}
#endif /* su_HAVE_MEM_BAG_AUTO */

#ifdef su_HAVE_MEM_BAG_LOFI
void *
su_mem_bag_lofi_snap_create(struct su_mem_bag *self){
   void *rv;
   NYD2_IN;
   ASSERT(self);

   if(self->mb_top != NIL)
      self = self->mb_top;

   /* XXX Before SU this allocated one and returned that, now we do
    * XXX have no real debug support no more... */
   rv = self->mb_lofi_top;
   NYD2_OU;
   return rv;
}

struct su_mem_bag *
su_mem_bag_lofi_snap_unroll(struct su_mem_bag *self, void *cookie){
   struct su__mem_bag_lofi_chunk *mblcp;
   struct su_mem_bag *oself;
   NYD2_IN;
   ASSERT(self);

   if((oself = self)->mb_top != NIL)
      self = oself->mb_top;

   while((mblcp = self->mb_lofi_top) != cookie){
#ifdef su_HAVE_DEBUG
      if(mblcp == NIL){
         su_log_write(su_LOG_DEBUG, "su_mem_bag_lofi_snap_unroll(%p): no such "
            "snap exists: non-debug crash!\n", oself);
         break;
      }
#endif
      self = a_membag_lofi_free_top(self);
   }

   self = oself;
   NYD2_OU;
   return self;
}

void *
su_mem_bag_lofi_allocate(struct su_mem_bag *self, uz size, uz no, u32 mbaf
      su_DBG_LOC_ARGS_DECL){
   void *rv;
   NYD_IN;
   ASSERT(self);

   if(self->mb_top != NIL)
      self = self->mb_top;
   if(UNLIKELY(size == 0))
      size = 1;
   if(UNLIKELY(no == 0))
      no = 1;
   mbaf &= su__MEM_BAG_ALLOC_USER_MASK;

   rv = NIL;

   /* Any attempt to allocate more is prevented (for simplicity, going for
    * UZ_MAX would require more expensive care: for nothing) */
   if(LIKELY(S(uz,S32_MAX) / no > size)){
      struct su__mem_bag_lofi_chunk *mblcp;
      char *top, *cp;
      struct su__mem_bag_lofi_pool *mblpp;
      boole isheap;
      uz chunksz;

      size *= no;
      /* C99 */{
         uz realsz;

         realsz = Z_ALIGN(size);
         DBG( if(realsz > self->mb_bsz)
            su_log_write(su_LOG_DEBUG, "su_mem_bag_lofi_allocate(): huge: "
                  "%" PRIuZ " bytes from %s:%" PRIu32 "!\n",
               size  su_DBG_LOC_ARGS_USE); )
         isheap = (a_MEMBAG_HULL || realsz > self->mb_bsz);
         chunksz = Z_ALIGN(VSTRUCT_SIZEOF(struct su__mem_bag_lofi_chunk,
                  mblc_buf)) + (isheap ? sizeof(up) : realsz);
      }

      if((mblpp = self->mb_lofi_pool) != NIL){
         top = &mblpp->mblp_buf[self->mb_bsz];

         /* Check whether allocation can be placed.. */
         if(LIKELY(top - chunksz >= (cp = mblpp->mblp_caster))){
            rv = cp;
            mblpp->mblp_caster = cp = &cp[chunksz];
            goto jhave_pool;
         }
      }

      /* Need a pool */
      mblpp = S(struct su__mem_bag_lofi_pool*,su_ALLOCATE_LOC((self->mb_bsz +
            a_MEMBAG_BSZ_BASE), 1, (mbaf | su_MEM_ALLOC_MARK_LOFI),
            su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE));
      if(mblpp == NIL)
         goto jleave;
      rv = cp = mblpp->mblp_buf;
      mblpp->mblp_caster = cp = &cp[chunksz];
      mblpp->mblp_last = self->mb_lofi_pool;
      self->mb_lofi_pool = mblpp;

      /* Have a pool, chunk in rv (and: cp==.mbab_caster == rv+chunksz) */
jhave_pool:
      mblcp = S(struct su__mem_bag_lofi_chunk*,rv);
      mblcp->mblc_last = R(struct su__mem_bag_lofi_chunk*,
            R(up,self->mb_lofi_top) | isheap);
      if(!isheap)
         rv = mblcp->mblc_buf;
      else{
         union {void *p; void **pp;} v;

         v.p = rv;
         rv = su_ALLOCATE_LOC(size, 1, (mbaf | su_MEM_ALLOC_MARK_LOFI),
               su_DBG_LOC_ARGS_FILE, su_DBG_LOC_ARGS_LINE);
         if(rv != NIL){
            v.p = mblcp->mblc_buf;
            *v.pp = S(void*,rv);
         }else{
            mblpp->mblp_caster = v.p;
            goto jleave;
         }
      }
      self->mb_lofi_top = mblcp;

      if(mbaf & su_MEM_BAG_ALLOC_CLEAR)
         su_mem_set(rv, 0, size);
   }else
      su_state_err(su_STATE_ERR_OVERFLOW, mbaf,
         _("SU memory bag: lofi allocation request"));
jleave:
   NYD_OU;
   return rv;
}

struct su_mem_bag *
su_mem_bag_lofi_free(struct su_mem_bag *self, void *ovp  su_DBG_LOC_ARGS_DECL){
   NYD_IN;
   UNUSED(ovp);
   ASSERT(self);

   if(self->mb_top != NIL)
      self = self->mb_top;

#ifdef su_HAVE_DEBUG
   /* C99 */{
      struct su__mem_bag_lofi_chunk *mblcp;

      if(ovp == NIL){
         su_log_write(su_LOG_DEBUG,
            "su_mem_bag_lofi_free(): NIL from %s:%" PRIu32 "\n"
            su_DBG_LOC_ARGS_USE);
         goto NYD_OU_LABEL;
      }

      if(((mblcp = self->mb_lofi_top) == NIL)){
         su_log_write(su_LOG_DEBUG, "su_mem_bag_lofi_free(): "
            "no LOFI memory exists at %s:%" PRIu32 "!\n"  su_DBG_LOC_ARGS_USE);
         goto NYD_OU_LABEL;
      }

      /* From heap? */
      if(R(up,mblcp->mblc_last) & 0x1){
         union {void *p; void **pp;} v;

         v.p = mblcp->mblc_buf;
         if(*v.pp != ovp)
            goto jeinval;
      }else if(ovp != mblcp->mblc_buf){
jeinval:
         su_log_write(su_LOG_DEBUG, "su_mem_bag_lofi_free(): "
            "invalid pointer from %s:%" PRIu32 "!\n"  su_DBG_LOC_ARGS_USE);
         goto NYD_OU_LABEL;
      }
   }
#endif /* su_HAVE_DEBUG */

   self = a_membag_lofi_free_top(self);

   NYD_OU;
   return self;
}
#endif /* su_HAVE_MEM_BAG_LOFI */

#endif /* su_HAVE_MEM_BAG */
#include "su/code-ou.h"
/* s-it-mode */
