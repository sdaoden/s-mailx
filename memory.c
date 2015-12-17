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

#ifdef HAVE_MEMORY_DEBUG
static size_t a_mem_aall, a_mem_acur, a_mem_amax,
   a_mem_mall, a_mem_mcur, a_mem_mmax;

static struct a_mem_chunk *a_mem_list, *a_mem_free;
#endif

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

#ifdef HAVE_MEMORY_DEBUG
# undef _HOPE_SIZE
# undef _HOPE_SET
# undef _HOPE_GET_TRACE
# undef _HOPE_GET
#endif

/* s-it-mode */
