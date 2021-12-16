/*@ Implementation of mem.h: allocation functions.
 *@ TODO - flux memory: pool specific (like _auto_ and _lofi_), but normal
 *@ TODO   heap beside, which can be free()d in random order etc.
 *@ TODO - dump,trace,etc. should take a log::domain object; if NIL: builtin.
 *@ TODO   (we need a logdom_detach() or so which ensures all resources are
 *@ TODO   initialized [better than asserting it is device-based dom]).
 *@ TODO - port C++ memcache
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
#define su_FILE su_mem_alloc
#define su_SOURCE
#define su_SOURCE_MEM_ALLOC

#include "su/code.h"

#include <stdlib.h> /* TODO -> port C++ cache */

#ifdef su_HAVE_DEBUG /* (MEM_ALLOC_DEBUG not yet) */
# include "su/cs.h"
#endif

#include "su/mem.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#ifndef su_MEM_ALLOC_DEBUG
# define a_MEMA_DBG(X)
# define a_MEMA_HOPE_SIZE_ADD 0
#else
# define a_MEMA_DBG(X) X
# define a_MEMA_DUMP_SIZE 80
# define a_MEMA_HOPE_SIZE (2 * 8 * sizeof(u8))
# define a_MEMA_HOPE_INC(P) (P) += 8 * sizeof(u8)
# define a_MEMA_HOPE_DEC(P) (P) -= 8 * sizeof(u8)
# define a_MEMA_HOPE_SIZE_ADD \
   (a_MEMA_HOPE_SIZE + sizeof(struct a_mema_heap_chunk))

   /* We use address-induced canary values, inspiration (but he did not invent)
    * and primes from maxv@netbsd.org, src/sys/kern/subr_kmem.c */
# define a_MEMA_HOPE_LOWER(M,P) \
do{\
   u64 __h__ = R(up,P);\
   __h__ *= (S(u64,0x9E37FFFFu) << 32) | 0xFFFC0000u;\
   __h__ >>= 56;\
   (M) = S(u8,__h__);\
}while(0)

# define a_MEMA_HOPE_UPPER(M,P) \
do{\
   u32 __i__;\
   u64 __x__, __h__ = R(up,P);\
   __h__ *= (S(u64,0x9E37FFFFu) << 32) | 0xFFFC0000u;\
   for(__i__ = 56; __i__ != 0; __i__ -= 8)\
      if((__x__ = (__h__ >> __i__)) != 0){\
         (M) = S(u8,__x__);\
         break;\
      }\
   if(__i__ == 0)\
      (M) = 0xAAu;\
}while(0)

# define a_MEMA_HOPE_SET(T,C) \
do{\
   union a_mema_ptr __xp;\
   struct a_mema_chunk *__xc;\
   __xp.map_vp = (C).map_vp;\
   __xc = R(struct a_mema_chunk*,__xp.T - 1);\
   a_MEMA_HOPE_INC((C).map_cp);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[0], &__xp.map_u8p[0]);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[1], &__xp.map_u8p[1]);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[2], &__xp.map_u8p[2]);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[3], &__xp.map_u8p[3]);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[4], &__xp.map_u8p[4]);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[5], &__xp.map_u8p[5]);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[6], &__xp.map_u8p[6]);\
   a_MEMA_HOPE_LOWER(__xp.map_u8p[7], &__xp.map_u8p[7]);\
   a_MEMA_HOPE_INC(__xp.map_u8p) + __xc->mac_size - __xc->mac_user_off;\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[0], &__xp.map_u8p[0]);\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[1], &__xp.map_u8p[1]);\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[2], &__xp.map_u8p[2]);\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[3], &__xp.map_u8p[3]);\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[4], &__xp.map_u8p[4]);\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[5], &__xp.map_u8p[5]);\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[6], &__xp.map_u8p[6]);\
   a_MEMA_HOPE_UPPER(__xp.map_u8p[7], &__xp.map_u8p[7]);\
}while(0)

# define a_MEMA_HOPE_GET_TRACE(T,C,BAD) \
do{\
   a_MEMA_HOPE_INC((C).map_cp);\
   a_MEMA_HOPE_GET(T, C, BAD);\
   a_MEMA_HOPE_INC((C).map_cp);\
}while(0)

# define a_MEMA_HOPE_GET(T,C,BAD) \
do{\
   union a_mema_ptr __xp;\
   struct a_mema_chunk *__xc;\
   u32 __i;\
   u8 __m;\
   __xp.map_vp = (C).map_vp;\
   a_MEMA_HOPE_DEC(__xp.map_cp);\
   (C).map_cp = __xp.map_cp;\
   __xc = R(struct a_mema_chunk*,__xp.T - 1);\
   (BAD) = FAL0;\
   __i = 0;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[0]);\
      if(__xp.map_u8p[0] != __m) __i |= 1<<7;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[1]);\
      if(__xp.map_u8p[1] != __m) __i |= 1<<6;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[2]);\
      if(__xp.map_u8p[2] != __m) __i |= 1<<5;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[3]);\
      if(__xp.map_u8p[3] != __m) __i |= 1<<4;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[4]);\
      if(__xp.map_u8p[4] != __m) __i |= 1<<3;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[5]);\
      if(__xp.map_u8p[5] != __m) __i |= 1<<2;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[6]);\
      if(__xp.map_u8p[6] != __m) __i |= 1<<1;\
   a_MEMA_HOPE_LOWER(__m, &__xp.map_u8p[7]);\
      if(__xp.map_u8p[7] != __m) __i |= 1<<0;\
   if(__i != 0){\
      (BAD) = (__i >= (1<<3)) ? TRUM1 : TRU1;\
      a_MEMA_HOPE_INC((C).map_cp);\
      su_log_write(su_LOG_ALERT | su_LOG_F_CORE,\
         "! SU memory: %p: corrupt lower canary: " \
            "0x%02X: %s, line %" PRIu32 "\n",\
         (C).map_cp, __i, su_DVL_LOC_ARGS_FILE, su_DVL_LOC_ARGS_LINE);\
      a_MEMA_HOPE_DEC((C).map_cp);\
   }\
   a_MEMA_HOPE_INC(__xp.map_u8p) + __xc->mac_size - __xc->mac_user_off;\
   __i = 0;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[0]);\
      if(__xp.map_u8p[0] != __m) __i |= 1<<0;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[1]);\
      if(__xp.map_u8p[1] != __m) __i |= 1<<1;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[2]);\
      if(__xp.map_u8p[2] != __m) __i |= 1<<2;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[3]);\
      if(__xp.map_u8p[3] != __m) __i |= 1<<3;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[4]);\
      if(__xp.map_u8p[4] != __m) __i |= 1<<4;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[5]);\
      if(__xp.map_u8p[5] != __m) __i |= 1<<5;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[6]);\
      if(__xp.map_u8p[6] != __m) __i |= 1<<6;\
   a_MEMA_HOPE_UPPER(__m, &__xp.map_u8p[7]);\
      if(__xp.map_u8p[7] != __m) __i |= 1<<7;\
   if(__i != 0){\
      (BAD) |= (__i >= (1<<3)) ? TRUM1 : TRU1;\
      a_MEMA_HOPE_INC((C).map_cp);\
      su_log_write(su_LOG_ALERT | su_LOG_F_CORE,\
         "! SU memory: %p: corrupt upper canary: " \
            "0x%02X: %s, line %" PRIu32 "\n",\
         (C).map_cp, __i, su_DVL_LOC_ARGS_FILE, su_DVL_LOC_ARGS_LINE);\
      a_MEMA_HOPE_DEC((C).map_cp);\
   }\
   if(BAD)\
      su_log_write(su_LOG_ALERT | su_LOG_F_CORE,\
         "! SU memory:   ..canary last seen: %s, line %" PRIu32 "\n",\
         __xc->mac_file, __xc->mac_line);\
}while(0)
#endif /* su_MEM_ALLOC_DEBUG */

#ifdef su_MEM_ALLOC_DEBUG
struct a_mema_chunk{
   char const *mac_file;
   u32 mac_line : 29;
   u32 mac_isfree : 1;
   u32 mac_mark : 2;
   u32 mac_user_off;    /* .mac_size-.mac_user_off: user size */
   uz mac_size;
};
# define a_MEMA_MARK_TO_STORE(X) \
   ((S(u32,X) >> su__MEM_ALLOC_MARK_SHIFT) & su__MEM_ALLOC_MARK_MASK)
# define a_MEMA_STORE_TO_MARK(MACP) \
   ((MACP)->mac_mark << su__MEM_ALLOC_MARK_SHIFT)

/* The heap memory mem_free() may become delayed to detect double frees */
struct a_mema_heap_chunk{
   struct a_mema_chunk mahc_super;
   struct a_mema_heap_chunk *mahc_prev;
   struct a_mema_heap_chunk *mahc_next;
};

struct a_mema_stats{
   u64 mas_cnt_all;
   u64 mas_cnt_curr;
   u64 mas_cnt_max;
   u64 mas_mem_all;
   u64 mas_mem_curr;
   u64 mas_mem_max;
};
#endif /* su_MEM_ALLOC_DEBUG */

union a_mema_ptr{
   void *map_vp;
   char *map_cp;
   u8 *map_u8p;
#ifdef su_MEM_ALLOC_DEBUG
   struct a_mema_chunk *map_c;
   struct a_mema_heap_chunk *map_hc;
#endif
};

#ifdef su_MEM_ALLOC_DEBUG
static char const * const a_mema_mark_names[] = {
# ifdef su_USECASE_MX
   "heap", "auto", "auto-huge", "lofi"
# else
   "zero/0", "one/1", "two/2", "three/3"
# endif
};
CTAV(a_MEMA_MARK_TO_STORE(su_MEM_ALLOC_MARK_0) == 0);
CTAV(a_MEMA_MARK_TO_STORE(su_MEM_ALLOC_MARK_1) == 1);
CTAV(a_MEMA_MARK_TO_STORE(su_MEM_ALLOC_MARK_2) == 2);
CTAV(a_MEMA_MARK_TO_STORE(su_MEM_ALLOC_MARK_3) == 3);
#endif

static uz a_mema_conf /*= su_MEM_CONF_NONE*/;
CTAV(su_MEM_CONF_NONE == 0);

#ifdef su_MEM_ALLOC_DEBUG
static struct a_mema_heap_chunk *a_mema_heap_list;
static struct a_mema_heap_chunk *a_mema_free_list;

static struct a_mema_stats a_mema_stats[su__MEM_ALLOC_MARK_MAX + 1];
#endif

#ifdef su_MEM_ALLOC_DEBUG
/* */
static void a_mema_release_free(void);

/* */
static void a_mema_dump_chunk(boole how, char buf[a_MEMA_DUMP_SIZE],
      void const *vp, uz size);
#endif

#ifdef su_MEM_ALLOC_DEBUG
static void
a_mema_release_free(void){
   uz c, s;
   union a_mema_ptr p;
   NYD2_IN;

   if((p.map_hc = a_mema_free_list) != NIL){
      a_mema_free_list = NIL;
      c = s = 0;

      for(; p.map_hc != NIL;){
         void *vp;

         vp = p.map_hc;
         ++c;
         s += p.map_c->mac_size;
         p.map_hc = p.map_hc->mahc_next;
         free(vp);
      }

      su_log_write(su_LOG_INFO | su_LOG_F_CORE,
         "su_mem_set_conf(LINGER_FREE_RELEASE): freed %" PRIuZ
            " chunks / %" PRIuZ " bytes\n",
         c, s);
   }

   NYD2_OU;
}

static void
a_mema_dump_chunk(boole how, char buf[a_MEMA_DUMP_SIZE],
      void const *vp, uz size){
   char const *cp;
   char *dp;
   NYD2_IN;

   LCTAV(a_MEMA_DUMP_SIZE > 5);

   dp = buf;
   if(how < FAL0)
      *dp++ = '\n';
   dp[0] = ' ';
   dp[1] = ' ';
   dp += 2;

   if((size = MIN(size, a_MEMA_DUMP_SIZE - 4)) == 0)
      dp = buf;
   else for(cp = vp; size > 0; ++cp, --size)
      *dp++ = su_cs_is_print(*cp) ? *cp : '?';

   *dp = '\0';

   NYD2_OU;
}
#endif /* su_MEM_ALLOC_DEBUG */

#ifdef su_MEM_ALLOC_DEBUG
boole
su__mem_get_can_book(uz size, uz no){
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if(UZ_MAX - a_MEMA_HOPE_SIZE_ADD < size)
      goto jleave;
   size += a_MEMA_HOPE_SIZE_ADD;

   if(UZ_MAX / no <= size)
      goto jleave;

   rv = TRU1;
jleave:
   NYD2_OU;
   return rv;
}

boole
su__mem_check(su_DVL_LOC_ARGS_DECL_SOLE){
   union a_mema_ptr p, xp;
   boole anybad, isbad;
   NYD2_IN;

   anybad = FAL0;

   for(p.map_hc = a_mema_heap_list; p.map_hc != NIL;
         p.map_hc = p.map_hc->mahc_next){
      xp = p;
      ++xp.map_hc;
      a_MEMA_HOPE_GET_TRACE(map_hc, xp, isbad);
      if(isbad){
         anybad |= isbad;
         su_log_write(su_LOG_ALERT | su_LOG_F_CORE,
            "! SU memory: CANARY ERROR (heap): %p (%" PRIuZ
               " bytes): %s, line %" PRIu32 "\n",
            xp.map_vp, (p.map_c->mac_size - p.map_c->mac_user_off),
            p.map_c->mac_file, p.map_c->mac_line);
      }
   }

   for(p.map_hc = a_mema_free_list; p.map_hc != NIL;
         p.map_hc = p.map_hc->mahc_next){
      xp = p;
      ++xp.map_hc;
      a_MEMA_HOPE_GET_TRACE(map_hc, xp, isbad);
      if(isbad){
         anybad |= isbad;
         su_log_write(su_LOG_ALERT | su_LOG_F_CORE,
            "! SU memory: CANARY ERROR (free list): %p (%" PRIuZ
               " bytes): %s, line %" PRIu32 "\n",
            xp.map_vp, (p.map_c->mac_size - p.map_c->mac_user_off),
            p.map_c->mac_file, p.map_c->mac_line);
      }else{
         uz i;
         u8 const *ubp;

         ubp = xp.map_u8p;
         i = p.map_c->mac_size - p.map_c->mac_user_off;

         while(i-- != 0)
            if(ubp[i] != 0xBA){
               anybad |= 1;
               su_log_write(su_LOG_ALERT | su_LOG_F_CORE,
                  "! SU memory: FREED BUFFER MODIFIED: %p (%" PRIuZ
                     " bytes): %s, line %" PRIu32 "\n",
                  xp.map_vp, (p.map_c->mac_size - p.map_c->mac_user_off),
                  p.map_c->mac_file, p.map_c->mac_line);
            }
      }
   }

   if(anybad)
      su_log_write(((a_mema_conf & su_MEM_CONF_ON_ERROR_EMERG)
            ? su_LOG_EMERG : su_LOG_ALERT) | su_LOG_F_CORE,
         "SU memory check: errors encountered\n");

   NYD2_OU;
   return anybad;
}

boole
su__mem_trace(boole dumpmem  su_DVL_LOC_ARGS_DECL){
   char dump[a_MEMA_DUMP_SIZE];
   union a_mema_ptr p, xp;
   u32 mark;
   boole anybad, isbad;
   NYD2_IN;

   anybad = FAL0;
   dump[0] = '\0';

   for(mark = su__MEM_ALLOC_MARK_MAX;; --mark){
      struct a_mema_stats const *masp;

      masp = &a_mema_stats[mark];

      su_log_write(su_LOG_INFO | su_LOG_F_CORE,
         "MARK \"%s\" MEMORY:\n"
         "   Count cur/peek/all: %7" PRIu64 "/%7" PRIu64 "/%10" PRIu64 "\n"
         "  Memory cur/peek/all: %7" PRIu64 "/%7" PRIu64 "/%10" PRIu64 "\n\n",
         a_mema_mark_names[mark],
         masp->mas_cnt_curr, masp->mas_cnt_max, masp->mas_cnt_all,
         masp->mas_mem_curr, masp->mas_mem_max, masp->mas_mem_all);

      for(p.map_hc = a_mema_heap_list; p.map_hc != NIL;
            p.map_hc = p.map_hc->mahc_next){
         if(p.map_c->mac_mark != mark)
            continue;

         xp = p;
         ++xp.map_hc;
         a_MEMA_HOPE_GET_TRACE(map_hc, xp, isbad);
         anybad |= isbad;

         if(dumpmem)
            a_mema_dump_chunk(dumpmem, dump, xp.map_vp,
               p.map_c->mac_size - p.map_c->mac_user_off);

         su_log_write((isbad ? su_LOG_ALERT : su_LOG_INFO) | su_LOG_F_CORE,
            "  %s%p (%" PRIuZ " bytes): %s, line %" PRIu32 "%s\n",
            (isbad ? "! SU memory: CANARY ERROR: " : ""), xp.map_vp,
            p.map_c->mac_size - p.map_c->mac_user_off,
            p.map_c->mac_file, p.map_c->mac_line, dump);
      }

      if(mark == su_MEM_ALLOC_MARK_0)
         break;
   }

   if(a_mema_free_list != NIL){
      su_log_write(su_LOG_INFO | su_LOG_F_CORE,
         "Freed memory lingering for release:\n");

      for(p.map_hc = a_mema_free_list; p.map_hc != NIL;
            p.map_hc = p.map_hc->mahc_next){
         xp = p;
         ++xp.map_hc;
         a_MEMA_HOPE_GET_TRACE(map_hc, xp, isbad);
         anybad |= isbad;

         if(dumpmem)
            a_mema_dump_chunk(dumpmem, dump, xp.map_vp,
               p.map_c->mac_size - p.map_c->mac_user_off);

         su_log_write((isbad ? su_LOG_ALERT : su_LOG_INFO) | su_LOG_F_CORE,
            "  %s%p (%" PRIuZ " bytes): %s, line %" PRIu32 "%s\n",
            (isbad ? "! SU memory: CANARY ERROR: " : ""), xp.map_vp,
            p.map_c->mac_size - p.map_c->mac_user_off,
            p.map_c->mac_file, p.map_c->mac_line, dump);
      }
   }

   NYD2_OU;
   return anybad;
}
#endif /* su_MEM_ALLOC_DEBUG */

void *
su_mem_allocate(uz size, uz no, BITENUM_IS(u32,su_mem_alloc_flags) maf
      su_DVL_LOC_ARGS_DECL){
#ifdef su_MEM_ALLOC_DEBUG
   u32 mark;
   union a_mema_ptr p;
   uz user_sz, user_no;
#endif
   void *rv;
   NYD_IN;
   su_DVL_LOC_ARGS_UNUSED();

   a_MEMA_DBG( user_sz = size su_COMMA user_no = no; )
   if(UNLIKELY(size == 0))
      size = 1;
   if(UNLIKELY(no == 0))
      no = 1;
   maf &= su__MEM_ALLOC_USER_MASK;

   rv = NIL;

   if(a_MEMA_DBG( UZ_MAX - a_MEMA_HOPE_SIZE_ADD > size && )
         LIKELY(((maf & su_MEM_ALLOC_32BIT_OVERFLOW) ? U32_MAX :
               ((maf & su_MEM_ALLOC_31BIT_OVERFLOW) ? U32_MAX >> 1 : UZ_MAX))
            / no > size + a_MEMA_HOPE_SIZE_ADD)){
      size *= no;
#if !defined su_MEM_ALLOC_DEBUG && !defined su_HAVE_MEM_CANARIES_DISABLE
      if(size < su_MEM_ALLOC_MIN)
         size = su_MEM_ALLOC_MIN;
#endif
#ifdef su_MEM_ALLOC_DEBUG
      size += a_MEMA_HOPE_SIZE_ADD;
      user_sz *= user_no;
#endif

      if(LIKELY((rv = malloc(size)) != NIL)){
         /* XXX Of course this may run on odd ranges, but once upon a time
          * XXX i will port my C++ cache and then we're fine again (it will not
          * XXX even be handled in here) */
         if(maf & su_MEM_ALLOC_ZERO)
            su_mem_set(rv, 0, size);
#ifdef su_MEM_ALLOC_DEBUG
         else
            su_mem_set(rv, 0xAA, size);
         p.map_vp = rv;

         p.map_hc->mahc_prev = NIL;
         if((p.map_hc->mahc_next = a_mema_heap_list) != NIL)
            a_mema_heap_list->mahc_prev = p.map_hc;
         p.map_c->mac_file = su_DVL_LOC_ARGS_FILE;
         p.map_c->mac_line = su_DVL_LOC_ARGS_LINE;
         p.map_c->mac_isfree = FAL0;
         p.map_c->mac_mark = mark = a_MEMA_MARK_TO_STORE(maf);
         ASSERT(size - user_sz <= S32_MAX);
         p.map_c->mac_user_off = S(u32,size - user_sz);
         p.map_c->mac_size = size;
         a_mema_heap_list = p.map_hc++;

         a_MEMA_HOPE_SET(map_hc, p);
         rv = p.map_vp;

         ++a_mema_stats[mark].mas_cnt_all;
         ++a_mema_stats[mark].mas_cnt_curr;
         a_mema_stats[mark].mas_cnt_max = MAX(
               a_mema_stats[mark].mas_cnt_max,
               a_mema_stats[mark].mas_cnt_curr);
         a_mema_stats[mark].mas_mem_all += user_sz;
         a_mema_stats[mark].mas_mem_curr += user_sz;
         a_mema_stats[mark].mas_mem_max = MAX(
               a_mema_stats[mark].mas_mem_max,
               a_mema_stats[mark].mas_mem_curr);
#endif /* su_MEM_ALLOC_DEBUG */
      }else
         su_state_err(su_STATE_ERR_NOMEM, maf,
            _("SU memory: allocation request"));
   }else
      su_state_err(su_STATE_ERR_OVERFLOW, maf,
         _("SU memory: allocation request"));

   NYD_OU;
   return rv;
}

void *
su_mem_reallocate(void *ovp, uz size, uz no,
      BITENUM_IS(u32,su_mem_alloc_flags) maf  su_DVL_LOC_ARGS_DECL){
#ifdef su_MEM_ALLOC_DEBUG
   u32 mark;
   union a_mema_ptr p;
   void *origovp;
   uz user_sz, user_no, orig_sz;
#endif
   void *rv;
   NYD_IN;
   su_DVL_LOC_ARGS_UNUSED();

   a_MEMA_DBG( user_sz = size su_COMMA user_no = no su_COMMA orig_sz = 0; )
   if(UNLIKELY(size == 0))
      size = 1;
   if(UNLIKELY(no == 0))
      no = 1;
   maf &= su__MEM_ALLOC_USER_MASK;

   rv = NIL;

   /* In the debug case we always allocate a new buffer */
#ifdef su_MEM_ALLOC_DEBUG
   if((p.map_vp = origovp = ovp) != NIL){
      boole isbad;

      ovp = NIL;
      a_MEMA_HOPE_GET(map_hc, p, isbad);
      --p.map_hc;

      if(!p.map_c->mac_isfree)
         orig_sz = p.map_c->mac_size - p.map_c->mac_user_off;
      else if(isbad == TRUM1){
         su_log_write(su_LOG_ALERT | su_LOG_F_CORE,
            "SU memory: reallocation: pointer corrupted!  At %s, line %" PRIu32
               "\n\tLast seen: %s, line %" PRIu32 "\n"
            su_DVL_LOC_ARGS_USE, p.map_c->mac_file, p.map_c->mac_line);
         su_state_err(su_STATE_ERR_NOMEM, maf,
            _("SU memory: reallocation of corrupted pointer"));
         goto NYD_OU_LABEL;
      }else{
         su_log_write(su_LOG_ALERT | su_LOG_F_CORE,
            "SU memory: reallocation: pointer freed!  At %s, line %" PRIu32
               "\n\tLast seen: %s, line %" PRIu32 "\n"
            su_DVL_LOC_ARGS_USE, p.map_c->mac_file, p.map_c->mac_line);
         su_state_err(su_STATE_ERR_NOMEM, maf,
            _("SU memory: reallocation of a freed pointer"));
         goto NYD_OU_LABEL;
      }
   }
#endif /* su_MEM_ALLOC_DEBUG */

   if(a_MEMA_DBG( UZ_MAX - a_MEMA_HOPE_SIZE_ADD > size && )
         LIKELY(((maf & su_MEM_ALLOC_32BIT_OVERFLOW) ? U32_MAX :
               ((maf & su_MEM_ALLOC_31BIT_OVERFLOW) ? U32_MAX >> 1 : UZ_MAX))
            / no > size + a_MEMA_HOPE_SIZE_ADD)){
      size *= no;
#if !defined su_MEM_ALLOC_DEBUG && !defined su_HAVE_MEM_CANARIES_DISABLE
      if(size < su_MEM_ALLOC_MIN)
         size = su_MEM_ALLOC_MIN;
#endif
      size *= no;
#ifdef su_MEM_ALLOC_DEBUG
      size += a_MEMA_HOPE_SIZE_ADD;
      user_sz *= user_no;
#endif

      if(UNLIKELY((rv = realloc(ovp, size)) == NIL))
         su_state_err(su_STATE_ERR_NOMEM, maf,
            _("SU memory: reallocation request"));
#ifdef su_MEM_ALLOC_DEBUG
      else{
         p.map_vp = rv;

         p.map_hc->mahc_prev = NIL;
         if((p.map_hc->mahc_next = a_mema_heap_list) != NIL)
            a_mema_heap_list->mahc_prev = p.map_hc;
         p.map_c->mac_file = su_DVL_LOC_ARGS_FILE;
         p.map_c->mac_line = su_DVL_LOC_ARGS_LINE;
         p.map_c->mac_isfree = FAL0;
         p.map_c->mac_mark = mark = a_MEMA_MARK_TO_STORE(maf);
         ASSERT(size - user_sz <= S32_MAX);
         p.map_c->mac_user_off = S(u32,size - user_sz);
         p.map_c->mac_size = size;
         size -= p.map_c->mac_user_off; /* Real user size for potential copy */
         a_mema_heap_list = p.map_hc++;

         a_MEMA_HOPE_SET(map_hc, p);
         rv = p.map_vp;

         ++a_mema_stats[mark].mas_cnt_all;
         ++a_mema_stats[mark].mas_cnt_curr;
         a_mema_stats[mark].mas_cnt_max = MAX(
               a_mema_stats[mark].mas_cnt_max,
               a_mema_stats[mark].mas_cnt_curr);
         a_mema_stats[mark].mas_mem_all += user_sz;
         a_mema_stats[mark].mas_mem_curr += user_sz;
         a_mema_stats[mark].mas_mem_max = MAX(
               a_mema_stats[mark].mas_mem_max,
               a_mema_stats[mark].mas_mem_curr);

         if(origovp != NIL){
            su_mem_copy(rv, origovp, MIN(orig_sz, size));
            su_mem_free(origovp su_DVL_LOC_ARGS_USE);
         }
      }
#endif /* su_MEM_ALLOC_DEBUG */
   }else
      su_state_err(su_STATE_ERR_OVERFLOW, maf,
         _("SU memory: reallocation request"));

   NYD_OU;
   return rv;
}

void
su_mem_free(void *ovp  su_DVL_LOC_ARGS_DECL){
   NYD_IN;
   su_DVL_LOC_ARGS_UNUSED();

   if(LIKELY(ovp != NIL)){
#ifdef su_MEM_ALLOC_DEBUG
      u32 mark;
      uz orig_sz;
      union a_mema_ptr p;
      boole isbad;

      p.map_vp = ovp;
      a_MEMA_HOPE_GET(map_hc, p, isbad);
      --p.map_hc;

      if(isbad == TRUM1){
         su_log_write(su_LOG_ALERT | su_LOG_F_CORE,
            "SU memory: free of corrupted pointer at %s, line %" PRIu32 "\n"
            "\tLast seen: %s, line %" PRIu32 "\n"
            su_DVL_LOC_ARGS_USE, p.map_c->mac_file, p.map_c->mac_line);
         goto NYD_OU_LABEL;
      }else if(p.map_c->mac_isfree){
         su_log_write(su_LOG_ALERT | su_LOG_F_CORE,
            "SU memory: double-free avoided at %s, line %" PRIu32 "\n"
            "\tLast seen: %s, line %" PRIu32 "\n"
            su_DVL_LOC_ARGS_USE, p.map_c->mac_file, p.map_c->mac_line);
         goto NYD_OU_LABEL;
      }

      orig_sz = p.map_c->mac_size - p.map_c->mac_user_off;
      su_mem_set(ovp, 0xBA, orig_sz);
      ovp = p.map_vp;

      p.map_c->mac_file = su_DVL_LOC_ARGS_FILE;
      p.map_c->mac_line = su_DVL_LOC_ARGS_LINE;
      p.map_c->mac_isfree = TRU1;
      if(p.map_hc == a_mema_heap_list){
         if((a_mema_heap_list = p.map_hc->mahc_next) != NIL)
            a_mema_heap_list->mahc_prev = NIL;
      }else
         p.map_hc->mahc_prev->mahc_next = p.map_hc->mahc_next;
      if(p.map_hc->mahc_next != NIL)
         p.map_hc->mahc_next->mahc_prev = p.map_hc->mahc_prev;

      mark = p.map_c->mac_mark;
      --a_mema_stats[mark].mas_cnt_curr;
      a_mema_stats[mark].mas_mem_curr -= orig_sz;

      if(a_mema_conf & su_MEM_CONF_LINGER_FREE){
         p.map_hc->mahc_next = a_mema_free_list;
         a_mema_free_list = p.map_hc;
      }else
#endif /* su_MEM_ALLOC_DEBUG */
         free(ovp);
   }
#ifdef su_MEM_ALLOC_DEBUG
   else
      su_log_write(su_LOG_DEBUG,
         "SU memory: free(NIL) from %s, line %" PRIu32 "\n"
         su_DVL_LOC_ARGS_USE);
#endif

   NYD_OU;
}

void
su_mem_set_conf(BITENUM_IS(u32,su_mem_conf_option) mco, uz val){
   uz rmco;
   NYD_IN;

   rmco = S(uz,mco);
   ASSERT_NYD(rmco <= su__MEM_CONF_MAX);

   if((rmco & su_MEM_CONF_LINGER_FREE_RELEASE) ||
         (!val && (rmco & su_MEM_CONF_LINGER_FREE))){
      rmco &= ~su_MEM_CONF_LINGER_FREE_RELEASE;
#ifdef su_MEM_ALLOC_DEBUG
      su_mem_check();
      a_mema_release_free();
#endif
   }

   /* xxx !MEM_DEBUG does not test whether mem_conf_option is available */
   if(rmco != 0){
      if(val != FAL0)
         a_mema_conf |= rmco;
      else
         a_mema_conf &= ~rmco;
   }

   NYD_OU;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_MEM_ALLOC
/* s-it-mode */
