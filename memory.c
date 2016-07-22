/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Memory functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
# include "nail.h"
#endif

#ifdef HAVE_MEMORY_DEBUG
CTA(sizeof(char) == sizeof(ui8_t));

# define _HOPE_SIZE        (2 * 8 * sizeof(char))
# define _HOPE_SET(C)   \
do {\
   union a_mem_ptr __xl, __xu;\
   struct a_mem_chunk *__xc;\
   __xl.p_p = (C).p_p;\
   __xc = __xl.p_c - 1;\
   __xu.p_p = __xc;\
   (C).p_cp += 8;\
   __xl.p_ui8p[0]=0xDE; __xl.p_ui8p[1]=0xAA;\
   __xl.p_ui8p[2]=0x55; __xl.p_ui8p[3]=0xAD;\
   __xl.p_ui8p[4]=0xBE; __xl.p_ui8p[5]=0x55;\
   __xl.p_ui8p[6]=0xAA; __xl.p_ui8p[7]=0xEF;\
   __xu.p_ui8p += __xc->mc_size - 8;\
   __xu.p_ui8p[0]=0xDE; __xu.p_ui8p[1]=0xAA;\
   __xu.p_ui8p[2]=0x55; __xu.p_ui8p[3]=0xAD;\
   __xu.p_ui8p[4]=0xBE; __xu.p_ui8p[5]=0x55;\
   __xu.p_ui8p[6]=0xAA; __xu.p_ui8p[7]=0xEF;\
} while (0)

# define _HOPE_GET_TRACE(C,BAD) \
do {\
   (C).p_cp += 8;\
   _HOPE_GET(C, BAD);\
   (C).p_cp += 8;\
} while(0)

# define _HOPE_GET(C,BAD) \
do {\
   union a_mem_ptr __xl, __xu;\
   struct a_mem_chunk *__xc;\
   ui32_t __i;\
   __xl.p_p = (C).p_p;\
   __xl.p_cp -= 8;\
   (C).p_cp = __xl.p_cp;\
   __xc = __xl.p_c - 1;\
   (BAD) = FAL0;\
   __i = 0;\
   if (__xl.p_ui8p[0] != 0xDE) __i |= 1<<0;\
   if (__xl.p_ui8p[1] != 0xAA) __i |= 1<<1;\
   if (__xl.p_ui8p[2] != 0x55) __i |= 1<<2;\
   if (__xl.p_ui8p[3] != 0xAD) __i |= 1<<3;\
   if (__xl.p_ui8p[4] != 0xBE) __i |= 1<<4;\
   if (__xl.p_ui8p[5] != 0x55) __i |= 1<<5;\
   if (__xl.p_ui8p[6] != 0xAA) __i |= 1<<6;\
   if (__xl.p_ui8p[7] != 0xEF) __i |= 1<<7;\
   if (__i != 0) {\
      (BAD) = TRU1;\
      n_alert("%p: corrupt lower canary: 0x%02X: %s, line %d",\
         __xl.p_p, __i, mdbg_file, mdbg_line);\
   }\
   __xu.p_p = __xc;\
   __xu.p_ui8p += __xc->mc_size - 8;\
   __i = 0;\
   if (__xu.p_ui8p[0] != 0xDE) __i |= 1<<0;\
   if (__xu.p_ui8p[1] != 0xAA) __i |= 1<<1;\
   if (__xu.p_ui8p[2] != 0x55) __i |= 1<<2;\
   if (__xu.p_ui8p[3] != 0xAD) __i |= 1<<3;\
   if (__xu.p_ui8p[4] != 0xBE) __i |= 1<<4;\
   if (__xu.p_ui8p[5] != 0x55) __i |= 1<<5;\
   if (__xu.p_ui8p[6] != 0xAA) __i |= 1<<6;\
   if (__xu.p_ui8p[7] != 0xEF) __i |= 1<<7;\
   if (__i != 0) {\
      (BAD) = TRU1;\
      n_alert("%p: corrupt upper canary: 0x%02X: %s, line %d",\
         __xl.p_p, __i, mdbg_file, mdbg_line);\
   }\
   if (BAD)\
      n_alert("   ..canary last seen: %s, line %" PRIu16 "",\
         __xc->mc_file, __xc->mc_line);\
} while (0)
#endif /* HAVE_MEMORY_DEBUG */

#ifdef HAVE_MEMORY_DEBUG
struct a_mem_chunk{
   struct a_mem_chunk *mc_prev;
   struct a_mem_chunk *mc_next;
   char const *mc_file;
   ui16_t mc_line;
   ui8_t mc_isfree;
   ui8_t __dummy[1];
   ui32_t mc_size;
};

union a_mem_ptr{
   void *p_p;
   struct a_mem_chunk *p_c;
   char *p_cp;
   ui8_t *p_ui8p;
};
#endif /* HAVE_MEMORY_DEBUG */

/*
 * String dope -- this is a temporary left over
 */

/* In debug mode the "string dope" allocations are enwrapped in canaries, just
 * as we do with our normal memory allocator */
#ifdef HAVE_MEMORY_DEBUG
# define _SHOPE_SIZE       (2u * 8 * sizeof(char) + sizeof(struct schunk))

CTA(sizeof(char) == sizeof(ui8_t));

struct schunk {
   char const     *file;
   ui32_t         line;
   ui16_t         usr_size;
   ui16_t         full_size;
};

union sptr {
   void           *p;
   struct schunk  *c;
   char           *cp;
   ui8_t          *ui8p;
};
#endif /* HAVE_MEMORY_DEBUG */

union __align__ {
   char     *cp;
   size_t   sz;
   ul_i     ul;
};
#define SALIGN    (sizeof(union __align__) - 1)

CTA(ISPOW2(SALIGN + 1));

struct b_base {
   struct buffer  *_next;
   char           *_bot;      /* For spreserve() */
   char           *_relax;    /* If !NULL, used by srelax() instead of ._bot */
   char           *_max;      /* Max usable byte */
   char           *_caster;   /* NULL if full */
};

/* Single instance builtin buffer.  Room for anything, most of the time */
struct b_bltin {
   struct b_base  b_base;
   char           b_buf[SBUFFER_BUILTIN - sizeof(struct b_base)];
};
#define SBLTIN_SIZE  SIZEOF_FIELD(struct b_bltin, b_buf)

/* Dynamically allocated buffers to overcome shortage, always released again
 * once the command loop ticks */
struct b_dyn {
   struct b_base  b_base;
   char           b_buf[SBUFFER_SIZE - sizeof(struct b_base)];
};
#define SDYN_SIZE    SIZEOF_FIELD(struct b_dyn, b_buf)

/* The multiplexer of the several real b_* */
struct buffer {
   struct b_base  b;
   char           b_buf[VFIELD_SIZE(SALIGN + 1)];
};

/* Requests that exceed SDYN_SIZE-1 and thus cannot be handled by string dope
 * are always served by the normal memory allocator (which panics if memory
 * cannot be served).  Note such an allocation has not yet occurred, it is only
 * included as a security fallback bypass */
struct hugebuf {
   struct hugebuf *hb_next;
   char           hb_buf[VFIELD_SIZE(SALIGN + 1)];
};

#ifdef HAVE_MEMORY_DEBUG
static size_t a_mem_aall, a_mem_acur, a_mem_amax,
   a_mem_mall, a_mem_mcur, a_mem_mmax;

static struct a_mem_chunk *a_mem_list, *a_mem_free;
#endif

/*
 * String dope -- this is a temporary left over
 */

static struct b_bltin   _builtin_buf;
static struct buffer    *_buf_head, *_buf_list, *_buf_server, *_buf_relax;
static size_t           _relax_recur_no;
static struct hugebuf   *_huge_list;
#ifdef HAVE_MEMORY_DEBUG
static size_t           _all_cnt, _all_cycnt, _all_cycnt_max,
                        _all_size, _all_cysize, _all_cysize_max, _all_min,
                           _all_max, _all_wast,
                        _all_bufcnt, _all_cybufcnt, _all_cybufcnt_max,
                        _all_resetreqs, _all_resets;
#endif

/* sreset() / srelax() release a buffer, check the canaries of all chunks */
#ifdef HAVE_MEMORY_DEBUG
static void    _salloc_bcheck(struct buffer *b);
#endif

#ifdef HAVE_MEMORY_DEBUG
static void
_salloc_bcheck(struct buffer *b)
{
   union sptr pmax, pp;
   /*NYD2_ENTER;*/

   pmax.cp = (b->b._caster == NULL) ? b->b._max : b->b._caster;
   pp.cp = b->b._bot;

   while (pp.cp < pmax.cp) {
      struct schunk *c;
      union sptr x;
      void *ux;
      ui8_t i;

      c = pp.c;
      pp.cp += c->full_size;
      x.p = c + 1;
      ux = x.cp + 8;

      i = 0;
      if (x.ui8p[0] != 0xDE) i |= 1<<0;
      if (x.ui8p[1] != 0xAA) i |= 1<<1;
      if (x.ui8p[2] != 0x55) i |= 1<<2;
      if (x.ui8p[3] != 0xAD) i |= 1<<3;
      if (x.ui8p[4] != 0xBE) i |= 1<<4;
      if (x.ui8p[5] != 0x55) i |= 1<<5;
      if (x.ui8p[6] != 0xAA) i |= 1<<6;
      if (x.ui8p[7] != 0xEF) i |= 1<<7;
      if (i != 0)
         n_alert("sdope %p: corrupt lower canary: 0x%02X, size %u: %s, line %u",
            ux, i, c->usr_size, c->file, c->line);
      x.cp += 8 + c->usr_size;

      i = 0;
      if (x.ui8p[0] != 0xDE) i |= 1<<0;
      if (x.ui8p[1] != 0xAA) i |= 1<<1;
      if (x.ui8p[2] != 0x55) i |= 1<<2;
      if (x.ui8p[3] != 0xAD) i |= 1<<3;
      if (x.ui8p[4] != 0xBE) i |= 1<<4;
      if (x.ui8p[5] != 0x55) i |= 1<<5;
      if (x.ui8p[6] != 0xAA) i |= 1<<6;
      if (x.ui8p[7] != 0xEF) i |= 1<<7;
      if (i != 0)
         n_alert("sdope %p: corrupt upper canary: 0x%02X, size %u: %s, line %u",
            ux, i, c->usr_size, c->file, c->line);
   }
   /*NYD2_LEAVE;*/
}
#endif /* HAVE_MEMORY_DEBUG */

#ifndef HAVE_MEMORY_DEBUG
FL void *
smalloc(size_t s SMALLOC_DEBUG_ARGS)
{
   void *rv;
   NYD2_ENTER;

   if (s == 0)
      s = 1;
   if ((rv = malloc(s)) == NULL)
      n_panic(_("no memory"));
   NYD2_LEAVE;
   return rv;
}

FL void *
srealloc(void *v, size_t s SMALLOC_DEBUG_ARGS)
{
   void *rv;
   NYD2_ENTER;

   if (s == 0)
      s = 1;
   if (v == NULL)
      rv = smalloc(s);
   else if ((rv = realloc(v, s)) == NULL)
      n_panic(_("no memory"));
   NYD2_LEAVE;
   return rv;
}

FL void *
scalloc(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS)
{
   void *rv;
   NYD2_ENTER;

   if (size == 0)
      size = 1;
   if ((rv = calloc(nmemb, size)) == NULL)
      n_panic(_("no memory"));
   NYD2_LEAVE;
   return rv;
}

#else /* !HAVE_MEMORY_DEBUG */
FL void *
(smalloc)(size_t s SMALLOC_DEBUG_ARGS)
{
   union a_mem_ptr p;
   NYD2_ENTER;

   if (s == 0)
      s = 1;
   if (s > UI32_MAX - sizeof(struct a_mem_chunk) - _HOPE_SIZE)
      n_panic("smalloc(): allocation too large: %s, line %d",
         mdbg_file, mdbg_line);
   s += sizeof(struct a_mem_chunk) + _HOPE_SIZE;

   if ((p.p_p = (malloc)(s)) == NULL)
      n_panic(_("no memory"));
   p.p_c->mc_prev = NULL;
   if ((p.p_c->mc_next = a_mem_list) != NULL)
      a_mem_list->mc_prev = p.p_c;
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_size = (ui32_t)s;

   a_mem_list = p.p_c++;
   _HOPE_SET(p);

   ++a_mem_aall;
   ++a_mem_acur;
   a_mem_amax = MAX(a_mem_amax, a_mem_acur);
   a_mem_mall += s;
   a_mem_mcur += s;
   a_mem_mmax = MAX(a_mem_mmax, a_mem_mcur);
   NYD2_LEAVE;
   return p.p_p;
}

FL void *
(srealloc)(void *v, size_t s SMALLOC_DEBUG_ARGS)
{
   union a_mem_ptr p;
   bool_t isbad;
   NYD2_ENTER;

   if ((p.p_p = v) == NULL) {
      p.p_p = (smalloc)(s, mdbg_file, mdbg_line);
      goto jleave;
   }

   _HOPE_GET(p, isbad);
   --p.p_c;
   if (p.p_c->mc_isfree) {
      n_err("srealloc(): region freed!  At %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jforce;
   }

   if (p.p_c == a_mem_list)
      a_mem_list = p.p_c->mc_next;
   else
      p.p_c->mc_prev->mc_next = p.p_c->mc_next;
   if (p.p_c->mc_next != NULL)
      p.p_c->mc_next->mc_prev = p.p_c->mc_prev;

   --a_mem_acur;
   a_mem_mcur -= p.p_c->mc_size;
jforce:
   if (s == 0)
      s = 1;
   if (s > UI32_MAX - sizeof(struct a_mem_chunk) - _HOPE_SIZE)
      n_panic("srealloc(): allocation too large: %s, line %d",
         mdbg_file, mdbg_line);
   s += sizeof(struct a_mem_chunk) + _HOPE_SIZE;

   if ((p.p_p = (realloc)(p.p_c, s)) == NULL)
      n_panic(_("no memory"));
   p.p_c->mc_prev = NULL;
   if ((p.p_c->mc_next = a_mem_list) != NULL)
      a_mem_list->mc_prev = p.p_c;
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_size = (ui32_t)s;
   a_mem_list = p.p_c++;
   _HOPE_SET(p);

   ++a_mem_aall;
   ++a_mem_acur;
   a_mem_amax = MAX(a_mem_amax, a_mem_acur);
   a_mem_mall += s;
   a_mem_mcur += s;
   a_mem_mmax = MAX(a_mem_mmax, a_mem_mcur);
jleave:
   NYD2_LEAVE;
   return p.p_p;
}

FL void *
(scalloc)(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS)
{
   union a_mem_ptr p;
   NYD2_ENTER;

   if (size == 0)
      size = 1;
   if (nmemb == 0)
      nmemb = 1;
   if (size > UI32_MAX - sizeof(struct a_mem_chunk) - _HOPE_SIZE)
      n_panic("scalloc(): allocation size too large: %s, line %d",
         mdbg_file, mdbg_line);
   if ((UI32_MAX - sizeof(struct a_mem_chunk) - _HOPE_SIZE) / nmemb < size)
      n_panic("scalloc(): allocation count too large: %s, line %d",
         mdbg_file, mdbg_line);

   size *= nmemb;
   size += sizeof(struct a_mem_chunk) + _HOPE_SIZE;

   if ((p.p_p = (malloc)(size)) == NULL)
      n_panic(_("no memory"));
   memset(p.p_p, 0, size);
   p.p_c->mc_prev = NULL;
   if ((p.p_c->mc_next = a_mem_list) != NULL)
      a_mem_list->mc_prev = p.p_c;
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_size = (ui32_t)size;
   a_mem_list = p.p_c++;
   _HOPE_SET(p);

   ++a_mem_aall;
   ++a_mem_acur;
   a_mem_amax = MAX(a_mem_amax, a_mem_acur);
   a_mem_mall += size;
   a_mem_mcur += size;
   a_mem_mmax = MAX(a_mem_mmax, a_mem_mcur);
   NYD2_LEAVE;
   return p.p_p;
}

FL void
(sfree)(void *v SMALLOC_DEBUG_ARGS)
{
   union a_mem_ptr p;
   bool_t isbad;
   NYD2_ENTER;

   if ((p.p_p = v) == NULL) {
      n_err("sfree(NULL) from %s, line %d\n", mdbg_file, mdbg_line);
      goto jleave;
   }

   _HOPE_GET(p, isbad);
   --p.p_c;
   if (p.p_c->mc_isfree) {
      n_err("sfree(): double-free avoided at %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jleave;
   }

   if (p.p_c == a_mem_list)
      a_mem_list = p.p_c->mc_next;
   else
      p.p_c->mc_prev->mc_next = p.p_c->mc_next;
   if (p.p_c->mc_next != NULL)
      p.p_c->mc_next->mc_prev = p.p_c->mc_prev;
   p.p_c->mc_isfree = TRU1;
   /* Trash contents (also see [21c05f8]) */
   memset(v, 0377, p.p_c->mc_size - sizeof(struct a_mem_chunk) - _HOPE_SIZE);

   --a_mem_acur;
   a_mem_mcur -= p.p_c->mc_size;

   if (options & (OPT_DEBUG | OPT_MEMDEBUG)) {
      p.p_c->mc_next = a_mem_free;
      a_mem_free = p.p_c;
   } else
      (free)(p.p_c);
jleave:
   NYD2_LEAVE;
}

FL void
n_memreset(void)
{
   union a_mem_ptr p;
   size_t c = 0, s = 0;
   NYD_ENTER;

   n_memcheck();

   for (p.p_c = a_mem_free; p.p_c != NULL;) {
      void *vp = p.p_c;
      ++c;
      s += p.p_c->mc_size;
      p.p_c = p.p_c->mc_next;
      (free)(vp);
   }
   a_mem_free = NULL;

   if (options & (OPT_DEBUG | OPT_MEMDEBUG))
      n_err("memreset: freed %" PRIuZ " chunks/%" PRIuZ " bytes\n", c, s);
   NYD_LEAVE;
}

FL int
c_memtrace(void *v)
{
   /* For _HOPE_GET() */
   char const * const mdbg_file = "memtrace()";
   int const mdbg_line = -1;
   FILE *fp;
   union a_mem_ptr p, xp;
   bool_t isbad;
   size_t lines;
   NYD_ENTER;

   v = (void*)0x1;
   if ((fp = Ftmp(NULL, "memtr", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL) {
      n_perr("tmpfile", 0);
      goto jleave;
   }

   fprintf(fp, "Memory statistics:\n"
      "  Count cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n"
      "  Bytes cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n\n",
      a_mem_acur, a_mem_amax, a_mem_aall, a_mem_mcur, a_mem_mmax, a_mem_mall);

   fprintf(fp, "Currently allocated memory chunks:\n");
   for (lines = 0, p.p_c = a_mem_list; p.p_c != NULL;
         ++lines, p.p_c = p.p_c->mc_next) {
      xp = p;
      ++xp.p_c;
      _HOPE_GET_TRACE(xp, isbad);
      fprintf(fp, "%s%p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
         (isbad ? "! CANARY ERROR: " : ""), xp.p_p,
         (size_t)(p.p_c->mc_size - sizeof(struct a_mem_chunk)), p.p_c->mc_file,
         p.p_c->mc_line);
   }

   if (options & (OPT_DEBUG | OPT_MEMDEBUG)) {
      fprintf(fp, "sfree()d memory chunks awaiting free():\n");
      for (p.p_c = a_mem_free; p.p_c != NULL; ++lines, p.p_c = p.p_c->mc_next) {
         xp = p;
         ++xp.p_c;
         _HOPE_GET_TRACE(xp, isbad);
         fprintf(fp, "%s%p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
            (isbad ? "! CANARY ERROR: " : ""), xp.p_p,
            (size_t)(p.p_c->mc_size - sizeof(struct a_mem_chunk)),
            p.p_c->mc_file, p.p_c->mc_line);
      }
   }

# if defined HAVE_OPENSSL && defined HAVE_OPENSSL_MEMHOOKS
   fprintf(fp, "OpenSSL leak report:\n");
      CRYPTO_mem_leaks_fp(fp);
# endif

   page_or_print(fp, lines);
   Fclose(fp);
   v = NULL;
jleave:
   NYD_LEAVE;
   return (v != NULL);
}

FL bool_t
n__memcheck(char const *mdbg_file, int mdbg_line)
{
   union a_mem_ptr p, xp;
   bool_t anybad = FAL0, isbad;
   size_t lines;
   NYD_ENTER;

   for (lines = 0, p.p_c = a_mem_list; p.p_c != NULL;
         ++lines, p.p_c = p.p_c->mc_next) {
      xp = p;
      ++xp.p_c;
      _HOPE_GET_TRACE(xp, isbad);
      if (isbad) {
         anybad = TRU1;
         n_err(
            "! CANARY ERROR: %p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
            xp.p_p, (size_t)(p.p_c->mc_size - sizeof(struct a_mem_chunk)),
            p.p_c->mc_file, p.p_c->mc_line);
      }
   }

   if (options & (OPT_DEBUG | OPT_MEMDEBUG)) {
      for (p.p_c = a_mem_free; p.p_c != NULL; ++lines, p.p_c = p.p_c->mc_next) {
         xp = p;
         ++xp.p_c;
         _HOPE_GET_TRACE(xp, isbad);
         if (isbad) {
            anybad = TRU1;
            n_err(
               "! CANARY ERROR: %p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
               xp.p_p, (size_t)(p.p_c->mc_size - sizeof(struct a_mem_chunk)),
               p.p_c->mc_file, p.p_c->mc_line);
         }
      }
   }
   NYD_LEAVE;
   return anybad;
}
#endif /* HAVE_MEMORY_DEBUG */

FL void *
(salloc)(size_t size SALLOC_DEBUG_ARGS)
{
#ifdef HAVE_MEMORY_DEBUG
   size_t orig_size = size;
#endif
   union {struct buffer *b; struct hugebuf *hb; char *cp;} u;
   char *x, *y, *z;
   NYD2_ENTER;

   if (size == 0)
      ++size;
   size += SALIGN;
   size &= ~SALIGN;

#ifdef HAVE_MEMORY_DEBUG
   ++_all_cnt;
   ++_all_cycnt;
   _all_cycnt_max = MAX(_all_cycnt_max, _all_cycnt);
   _all_size += size;
   _all_cysize += size;
   _all_cysize_max = MAX(_all_cysize_max, _all_cysize);
   _all_min = (_all_max == 0) ? size : MIN(_all_min, size);
   _all_max = MAX(_all_max, size);
   _all_wast += size - orig_size;

   size += _SHOPE_SIZE;

   if (size >= SDYN_SIZE - 1)
      n_alert("salloc() of %" PRIuZ " bytes from %s, line %d",
         size, mdbg_file, mdbg_line);
#endif

   /* Huge allocations are special */
   if (UNLIKELY(size >= SDYN_SIZE - 1))
      goto jhuge;

   /* Search for a buffer with enough free space to serve request */
   if ((u.b = _buf_server) != NULL)
      goto jumpin;
jredo:
   for (u.b = _buf_head; u.b != NULL; u.b = u.b->b._next) {
jumpin:
      x = u.b->b._caster;
      if (x == NULL) {
         if (u.b == _buf_server) {
            if (u.b == _buf_head && (u.b = _buf_head->b._next) != NULL) {
               _buf_server = u.b;
               goto jumpin;
            }
            _buf_server = NULL;
            goto jredo;
         }
         continue;
      }
      y = x + size;
      z = u.b->b._max;
      if (PTRCMP(y, <=, z)) {
         /* Alignment is the one thing, the other is what is usually allocated,
          * and here about 40 bytes seems to be a good cut to avoid non-usable
          * non-NULL casters.  However, because of _salloc_bcheck(), we may not
          * set ._caster to NULL because then it would check all chunks up to
          * ._max, which surely doesn't work; speed is no issue with DEBUG */
         u.b->b._caster = NDBG( PTRCMP(y + 42 + 16, >=, z) ? NULL : ) y;
         u.cp = x;
         goto jleave;
      }
   }

   /* Need a new buffer */
   if (_buf_head == NULL) {
      struct b_bltin *b = &_builtin_buf;
      b->b_base._max = b->b_buf + SBLTIN_SIZE - 1;
      _buf_head = (struct buffer*)b;
      u.b = _buf_head;
   } else {
#ifdef HAVE_MEMORY_DEBUG
      ++_all_bufcnt;
      ++_all_cybufcnt;
      _all_cybufcnt_max = MAX(_all_cybufcnt_max, _all_cybufcnt);
#endif
      u.b = smalloc(sizeof(struct b_dyn));
      u.b->b._max = u.b->b_buf + SDYN_SIZE - 1;
   }
   if (_buf_list != NULL)
      _buf_list->b._next = u.b;
   _buf_server = _buf_list = u.b;
   u.b->b._next = NULL;
   u.b->b._caster = (u.b->b._bot = u.b->b_buf) + size;
   u.b->b._relax = NULL;
   u.cp = u.b->b._bot;

jleave:
   /* Encapsulate user chunk in debug canaries */
#ifdef HAVE_MEMORY_DEBUG
   {
      union sptr xl, xu;
      struct schunk *xc;

      xl.p = u.cp;
      xc = xl.c;
      xc->file = mdbg_file;
      xc->line = mdbg_line;
      xc->usr_size = (ui16_t)orig_size;
      xc->full_size = (ui16_t)size;
      xl.p = xc + 1;
      xl.ui8p[0]=0xDE; xl.ui8p[1]=0xAA; xl.ui8p[2]=0x55; xl.ui8p[3]=0xAD;
      xl.ui8p[4]=0xBE; xl.ui8p[5]=0x55; xl.ui8p[6]=0xAA; xl.ui8p[7]=0xEF;
      u.cp = xl.cp + 8;
      xu.p = u.cp;
      xu.cp += orig_size;
      xu.ui8p[0]=0xDE; xu.ui8p[1]=0xAA; xu.ui8p[2]=0x55; xu.ui8p[3]=0xAD;
      xu.ui8p[4]=0xBE; xu.ui8p[5]=0x55; xu.ui8p[6]=0xAA; xu.ui8p[7]=0xEF;
   }
#endif
   NYD2_LEAVE;
   return u.cp;

jhuge:
   u.hb = smalloc(sizeof(*u.hb) - VFIELD_SIZEOF(struct hugebuf, hb_buf) +
         size +1);
   u.hb->hb_next = _huge_list;
   _huge_list = u.hb;
   u.cp = u.hb->hb_buf;
   goto jleave;
}

FL void *
(csalloc)(size_t nmemb, size_t size SALLOC_DEBUG_ARGS)
{
   void *vp;
   NYD2_ENTER;

   size *= nmemb;
   vp = (salloc)(size SALLOC_DEBUG_ARGSCALL);
   memset(vp, 0, size);
   NYD2_LEAVE;
   return vp;
}

FL void
sreset(bool_t only_if_relaxed)
{
   struct buffer *blh, *bh;
   NYD_ENTER;

#ifdef HAVE_MEMORY_DEBUG
   ++_all_resetreqs;
#endif
   if (noreset) {
      /* Reset relaxation after any jump is a MUST */
      if (_relax_recur_no > 0)
         srelax_rele();
      goto jleave;
   }
   if (only_if_relaxed && _relax_recur_no == 0)
      goto jleave;

#ifdef HAVE_MEMORY_DEBUG
   _all_cycnt = _all_cysize = 0;
   _all_cybufcnt = (_buf_head != NULL && _buf_head->b._next != NULL);
   ++_all_resets;
#endif

   /* Reset relaxation after jump */
   if (_relax_recur_no > 0) {
      srelax_rele();
      assert(_relax_recur_no == 0);
   }

   blh = NULL;
   if ((bh = _buf_head) != NULL) {
      do {
         struct buffer *x = bh;
         bh = x->b._next;
#ifdef HAVE_MEMORY_DEBUG
         _salloc_bcheck(x);
#endif

         /* Give away all buffers that are not covered by sreset().
          * _buf_head is builtin and thus cannot be free()d */
         if (blh != NULL && x->b._bot == x->b_buf) {
            blh->b._next = bh;
            free(x);
         } else {
            blh = x;
            x->b._caster = x->b._bot;
            x->b._relax = NULL;
            DBG( memset(x->b._caster, 0377,
               PTR2SIZE(x->b._max - x->b._caster)); )
         }
      } while (bh != NULL);

      _buf_server = _buf_head;
      _buf_list = blh;
      _buf_relax = NULL;
   }

   while (_huge_list != NULL) {
      struct hugebuf *hb = _huge_list;
      _huge_list = hb->hb_next;
      free(hb);
   }

   n_memreset();
jleave:
   NYD_LEAVE;
}

FL void
srelax_hold(void)
{
   struct buffer *b;
   NYD_ENTER;

   if (_relax_recur_no++ == 0) {
      for (b = _buf_head; b != NULL; b = b->b._next)
         b->b._relax = b->b._caster;
      _buf_relax = _buf_server;
   }
   NYD_LEAVE;
}

FL void
srelax_rele(void)
{
   struct buffer *b;
   NYD_ENTER;

   assert(_relax_recur_no > 0);

   if (--_relax_recur_no == 0) {
      for (b = _buf_head; b != NULL; b = b->b._next) {
#ifdef HAVE_MEMORY_DEBUG
         _salloc_bcheck(b);
#endif
         b->b._caster = (b->b._relax != NULL) ? b->b._relax : b->b._bot;
         b->b._relax = NULL;
      }

      _buf_relax = NULL;
   }
#ifdef HAVE_DEVEL
   else
      n_err("srelax_rele(): recursion >0!\n");
#endif
   NYD_LEAVE;
}

FL void
srelax(void)
{
   /* The purpose of relaxation is only that it is possible to reset the
    * casters, *not* to give back memory to the system.  We are presumably in
    * an iteration over all messages of a mailbox, and it'd be quite
    * counterproductive to give the system allocator a chance to waste time */
   struct buffer *b;
   NYD_ENTER;

   assert(_relax_recur_no > 0);

   if (_relax_recur_no == 1) {
      for (b = _buf_head; b != NULL; b = b->b._next) {
#ifdef HAVE_MEMORY_DEBUG
         _salloc_bcheck(b);
#endif
         b->b._caster = (b->b._relax != NULL) ? b->b._relax : b->b._bot;
         DBG( memset(b->b._caster, 0377, PTR2SIZE(b->b._max - b->b._caster)); )
      }
   }
   NYD_LEAVE;
}

FL void
spreserve(void)
{
   struct buffer *b;
   NYD_ENTER;

   for (b = _buf_head; b != NULL; b = b->b._next)
      b->b._bot = b->b._caster;
   NYD_LEAVE;
}

#ifdef HAVE_MEMORY_DEBUG
FL int
c_sstats(void *v)
{
   size_t excess;
   NYD_ENTER;
   UNUSED(v);

   excess = (_all_cybufcnt_max * SDYN_SIZE) + SBLTIN_SIZE;
   excess = (excess >= _all_cysize_max) ? 0 : _all_cysize_max - excess;

   printf("String usage statistics (cycle means one sreset() cycle):\n"
      "  Buffer allocs ever/max a time : %" PRIuZ "/%" PRIuZ "\n"
      "  .. size of the builtin/dynamic: %" PRIuZ "/%" PRIuZ "\n"
      "  Overall alloc count/bytes     : %" PRIuZ "/%" PRIuZ "\n"
      "  .. bytes min/max/align wastage: %" PRIuZ "/%" PRIuZ "/%" PRIuZ "\n"
      "  sreset() cycles               : %" PRIuZ " (%" PRIuZ " performed)\n"
      "  Cycle max.: alloc count/bytes : %" PRIuZ "/%" PRIuZ "+%" PRIuZ "\n",
      _all_bufcnt, _all_cybufcnt_max,
      SBLTIN_SIZE, SDYN_SIZE,
      _all_cnt, _all_size,
      _all_min, _all_max, _all_wast,
      _all_resetreqs, _all_resets,
      _all_cycnt_max, _all_cysize_max, excess);
   NYD_LEAVE;
   return 0;
}
#endif /* HAVE_MEMORY_DEBUG */

#ifdef HAVE_MEMORY_DEBUG
# undef _HOPE_SIZE
# undef _HOPE_SET
# undef _HOPE_GET_TRACE
# undef _HOPE_GET
#endif

/* s-it-mode */
