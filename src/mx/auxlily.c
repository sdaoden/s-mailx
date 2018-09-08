/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that don't fit anywhere else.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef su_FILE
#define su_FILE auxlily
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

#ifdef mx_HAVE_SOCKETS
# ifdef mx_HAVE_GETADDRINFO
#  include <sys/socket.h>
# endif

# include <netdb.h>
#endif

#ifdef mx_HAVE_NL_LANGINFO
# include <langinfo.h>
#endif
#ifdef mx_HAVE_SETLOCALE
# include <locale.h>
#endif

#if mx_HAVE_RANDOM == n_RANDOM_IMPL_GETRANDOM
# include n_RANDOM_GETRANDOM_H
#endif

#ifdef mx_HAVE_IDNA
# if mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN2
#  include <idn2.h>
# elif mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN
#  include <idna.h>
#  include <idn-free.h>
# elif mx_HAVE_IDNA == n_IDNA_IMPL_IDNKIT
#  include <idn/api.h>
# endif
#endif

#if mx_HAVE_RANDOM != n_RANDOM_IMPL_ARC4 && mx_HAVE_RANDOM != n_RANDOM_IMPL_TLS
union rand_state{
   struct rand_arc4{
      ui8_t _dat[256];
      ui8_t _i;
      ui8_t _j;
      ui8_t __pad[6];
   } a;
   ui8_t b8[sizeof(struct rand_arc4)];
   ui32_t b32[sizeof(struct rand_arc4) / sizeof(ui32_t)];
};
#endif

#ifdef mx_HAVE_ERRORS
struct a_aux_err_node{
   struct a_aux_err_node *ae_next;
   struct n_string ae_str;
};
#endif

struct a_aux_err_map{
   ui32_t aem_hash;     /* Hash of name */
   ui32_t aem_nameoff;  /* Into a_aux_err_names[] */
   ui32_t aem_docoff;   /* Into a_aux_err docs[] (if mx_HAVE_DOCSTRINGS) */
   si32_t aem_err_no;   /* The OS error value for this one */
};

/* IDEC: byte to integer value lookup table */
static ui8_t const a_aux_idec_atoi[256] = {
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x01,
   0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,
   0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
   0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,
   0x23,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0A,0x0B,0x0C,
   0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,
   0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,
   0x21,0x22,0x23,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
   0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};

/* IDEC: avoid divisions for cutlimit calculation (indexed by base-2) */
#define a_X(X) (UI64_MAX / (X))
static ui64_t const a_aux_idec_cutlimit[35] = {
   a_X( 2), a_X( 3), a_X( 4), a_X( 5), a_X( 6), a_X( 7), a_X( 8),
   a_X( 9), a_X(10), a_X(11), a_X(12), a_X(13), a_X(14), a_X(15),
   a_X(16), a_X(17), a_X(18), a_X(19), a_X(20), a_X(21), a_X(22),
   a_X(23), a_X(24), a_X(25), a_X(26), a_X(27), a_X(28), a_X(29),
   a_X(30), a_X(31), a_X(32), a_X(33), a_X(34), a_X(35), a_X(36)
};
#undef a_X

/* IENC: is power-of-two table, and if, shift (indexed by base-2) */
static ui8_t const a_aux_ienc_shifts[35] = {
         1, 0, 2, 0, 0, 0, 3, 0,   /*  2 ..  9 */
   0, 0, 0, 0, 0, 0, 4, 0, 0, 0,   /* 10 .. 19 */
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0,   /* 20 .. 29 */
   0, 0, 5, 0, 0, 0, 0             /* 30 .. 36 */
};

/* IENC: integer to byte lookup tables */
static char const a_aux_ienc_itoa_upper[36] =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static char const a_aux_ienc_itoa_lower[36] =
      "0123456789abcdefghijklmnopqrstuvwxyz";

/* Include the constant make-errors.sh output */
#include "mx/gen-errors.h"

/* And these things come from mk-config.h (config-time make-errors.sh output) */
static n__ERR_NUMBER_TYPE const a_aux_err_no2mapoff[][2] = {
#undef a_X
#define a_X(N,I) {N,I},
n__ERR_NUMBER_TO_MAPOFF
#undef a_X
};

#if mx_HAVE_RANDOM != n_RANDOM_IMPL_ARC4 && mx_HAVE_RANDOM != n_RANDOM_IMPL_TLS
static union rand_state *a_aux_rand;
#endif

/* Error ring, for `errors' */
#ifdef mx_HAVE_ERRORS
static struct a_aux_err_node *a_aux_err_head, *a_aux_err_tail;
static size_t a_aux_err_cnt, a_aux_err_cnt_noted;
#endif
static size_t a_aux_err_linelen;

/* Our ARC4 random generator with its completely unacademical pseudo
 * initialization (shall /dev/urandom fail) */
#if mx_HAVE_RANDOM != n_RANDOM_IMPL_ARC4 && mx_HAVE_RANDOM != n_RANDOM_IMPL_TLS
static void a_aux_rand_init(void);
su_SINLINE ui8_t a_aux_rand_get8(void);
static ui32_t a_aux_rand_weak(ui32_t seed);
#endif

/* Find the descriptive mapping of an error number, or _ERR_INVAL */
static struct a_aux_err_map const *a_aux_err_map_from_no(si32_t eno);

#if mx_HAVE_RANDOM != n_RANDOM_IMPL_ARC4 && mx_HAVE_RANDOM != n_RANDOM_IMPL_TLS
static void
a_aux_rand_init(void){
# ifdef mx_HAVE_CLOCK_GETTIME
   struct timespec ts;
# else
   struct timeval ts;
# endif
   union {int fd; size_t i;} u;
   ui32_t seed, rnd;
   NYD2_IN;

   a_aux_rand = n_alloc(sizeof *a_aux_rand);

# if mx_HAVE_RANDOM == n_RANDOM_IMPL_GETRANDOM
   /* getrandom(2) guarantees 256 without n_ERR_INTR..
    * However, support sequential reading to avoid possible hangs that have
    * been reported on the ML (2017-08-22, s-nail/s-mailx freezes when
    * mx_HAVE_GETRANDOM is #defined) */
   n_LCTA(sizeof(a_aux_rand->a._dat) <= 256,
      "Buffer too large to be served without n_ERR_INTR error");
   n_LCTA(sizeof(a_aux_rand->a._dat) >= 256,
      "Buffer too small to serve used array indices");
   /* C99 */{
      size_t o, i;

      for(o = 0, i = sizeof a_aux_rand->a._dat;;){
         ssize_t gr;

         gr = n_RANDOM_GETRANDOM_FUN(&a_aux_rand->a._dat[o], i);
         if(gr == -1 && n_err_no == n_ERR_NOSYS)
            break;
         a_aux_rand->a._i = a_aux_rand->a._dat[a_aux_rand->a._dat[1] ^
               a_aux_rand->a._dat[84]];
         a_aux_rand->a._j = a_aux_rand->a._dat[a_aux_rand->a._dat[65] ^
               a_aux_rand->a._dat[42]];
         /* ..but be on the safe side */
         if(gr > 0){
            i -= (size_t)gr;
            if(i == 0)
               goto jleave;
            o += (size_t)gr;
         }
         n_err(_("Not enough entropy for the PseudoRandomNumberGenerator, "
            "waiting a bit\n"));
         n_msleep(250, FAL0);
      }
   }

# elif mx_HAVE_RANDOM == n_RANDOM_IMPL_URANDOM
   if((u.fd = open("/dev/urandom", O_RDONLY)) != -1){
      bool_t ok;

      ok = (sizeof(a_aux_rand->a._dat) == (size_t)read(u.fd, a_aux_rand->a._dat,
            sizeof(a_aux_rand->a._dat)));
      close(u.fd);

      a_aux_rand->a._i = a_aux_rand->a._dat[a_aux_rand->a._dat[1] ^
            a_aux_rand->a._dat[84]];
      a_aux_rand->a._j = a_aux_rand->a._dat[a_aux_rand->a._dat[65] ^
            a_aux_rand->a._dat[42]];
      if(ok)
         goto jleave;
   }
# elif mx_HAVE_RANDOM != n_RANDOM_IMPL_BUILTIN
#  error a_aux_rand_init(): the value of mx_HAVE_RANDOM is not supported
# endif

   /* As a fallback, a homebrew seed */
   if(n_poption & n_PO_D_V)
      n_err(_("P(seudo)R(andomNumber)G(enerator): creating homebrew seed\n"));
   for(seed = (uintptr_t)a_aux_rand & UI32_MAX, rnd = 21; rnd != 0; --rnd){
      for(u.i = n_NELEM(a_aux_rand->b32); u.i-- != 0;){
         ui32_t t, k;

# ifdef mx_HAVE_CLOCK_GETTIME
         clock_gettime(CLOCK_REALTIME, &ts);
         t = (ui32_t)ts.tv_nsec;
# else
         gettimeofday(&ts, NULL);
         t = (ui32_t)ts.tv_usec;
# endif
         if(rnd & 1)
            t = (t >> 16) | (t << 16);
         a_aux_rand->b32[u.i] ^= a_aux_rand_weak(seed ^ t);
         a_aux_rand->b32[t % n_NELEM(a_aux_rand->b32)] ^= seed;
         if(rnd == 7 || rnd == 17)
            a_aux_rand->b32[u.i] ^= a_aux_rand_weak(seed ^ (ui32_t)ts.tv_sec);
         k = a_aux_rand->b32[u.i] % n_NELEM(a_aux_rand->b32);
         a_aux_rand->b32[k] ^= a_aux_rand->b32[u.i];
         seed ^= a_aux_rand_weak(a_aux_rand->b32[k]);
         if((rnd & 3) == 3)
            seed ^= n_prime_next(seed);
      }
   }

   for(u.i = 5 * sizeof(a_aux_rand->b8); u.i != 0; --u.i)
      a_aux_rand_get8();
jleave:
   NYD2_OU;
}

su_SINLINE ui8_t
a_aux_rand_get8(void){
   ui8_t si, sj;

   si = a_aux_rand->a._dat[++a_aux_rand->a._i];
   sj = a_aux_rand->a._dat[a_aux_rand->a._j += si];
   a_aux_rand->a._dat[a_aux_rand->a._i] = sj;
   a_aux_rand->a._dat[a_aux_rand->a._j] = si;
   return a_aux_rand->a._dat[(ui8_t)(si + sj)];
}

static ui32_t
a_aux_rand_weak(ui32_t seed){
   /* From "Random number generators: good ones are hard to find",
    * Park and Miller, Communications of the ACM, vol. 31, no. 10,
    * October 1988, p. 1195.
    * (In fact: FreeBSD 4.7, /usr/src/lib/libc/stdlib/random.c.) */
   ui32_t hi;

   if(seed == 0)
      seed = 123459876;
   hi =  seed /  127773;
         seed %= 127773;
   seed = (seed * 16807) - (hi * 2836);
   if((si32_t)seed < 0)
      seed += SI32_MAX;
   return seed;
}
#endif /* mx_HAVE_RANDOM != IMPL_ARC4 != IMPL_TLS */

static struct a_aux_err_map const *
a_aux_err_map_from_no(si32_t eno){
   si32_t ecmp;
   size_t asz;
   n__ERR_NUMBER_TYPE const (*adat)[2], (*tmp)[2];
   struct a_aux_err_map const *aemp;
   NYD2_IN;

   aemp = &a_aux_err_map[n__ERR_NUMBER_VOIDOFF];

   if(UICMP(z, n_ABS(eno), <=, (n__ERR_NUMBER_TYPE)-1)){
      for(adat = a_aux_err_no2mapoff, asz = n_NELEM(a_aux_err_no2mapoff);
            asz != 0; asz >>= 1){
         tmp = &adat[asz >> 1];
         if((ecmp = (si32_t)((n__ERR_NUMBER_TYPE)eno - (*tmp)[0])) == 0){
            aemp = &a_aux_err_map[(*tmp)[1]];
            break;
         }
         if(ecmp > 0){
            adat = &tmp[1];
            --asz;
         }
      }
   }
   NYD2_OU;
   return aemp;
}

FL void
n_locale_init(void){
   NYD2_IN;

   n_psonce &= ~(n_PSO_UNICODE | n_PSO_ENC_MBSTATE);

#ifndef mx_HAVE_SETLOCALE
   n_mb_cur_max = 1;
#else
   setlocale(LC_ALL, n_empty);
   n_mb_cur_max = MB_CUR_MAX;
# ifdef mx_HAVE_NL_LANGINFO
   /* C99 */{
      char const *cp;

      if((cp = nl_langinfo(CODESET)) != NULL)
         /* (Will log during startup if user set that via -S) */
         ok_vset(ttycharset, cp);
   }
# endif /* mx_HAVE_SETLOCALE */

# ifdef mx_HAVE_C90AMEND1
   if(n_mb_cur_max > 1){
#  ifdef mx_HAVE_ALWAYS_UNICODE_LOCALE
      n_psonce |= n_PSO_UNICODE;
#  else
      wchar_t wc;
      if(mbtowc(&wc, "\303\266", 2) == 2 && wc == 0xF6 &&
            mbtowc(&wc, "\342\202\254", 3) == 3 && wc == 0x20AC)
         n_psonce |= n_PSO_UNICODE;
      /* Reset possibly messed up state; luckily this also gives us an
       * indication whether the encoding has locking shift state sequences */
      if(mbtowc(&wc, NULL, n_mb_cur_max))
         n_psonce |= n_PSO_ENC_MBSTATE;
#  endif
   }
# endif
#endif /* mx_HAVE_C90AMEND1 */
   NYD2_OU;
}

FL size_t
n_screensize(void){
   char const *cp;
   uiz_t rv;
   NYD2_IN;

   if((cp = ok_vlook(screen)) != NULL){
      n_idec_uiz_cp(&rv, cp, 0, NULL);
      if(rv == 0)
         rv = n_scrnheight;
   }else
      rv = n_scrnheight;

   if(rv > 2)
      rv -= 2;
   NYD2_OU;
   return rv;
}

FL char const *
n_pager_get(char const **env_addon){
   char const *rv;
   NYD_IN;

   rv = ok_vlook(PAGER);

   if(env_addon != NULL){
      *env_addon = NULL;
      /* Update the manual upon any changes:
       *    *colour-pager*, $PAGER */
      if(strstr(rv, "less") != NULL){
         if(getenv("LESS") == NULL)
            *env_addon = "LESS=RXi";
      }else if(strstr(rv, "lv") != NULL){
         if(getenv("LV") == NULL)
            *env_addon = "LV=-c";
      }
   }
   NYD_OU;
   return rv;
}

FL void
page_or_print(FILE *fp, size_t lines)
{
   int c;
   char const *cp;
   NYD_IN;

   fflush_rewind(fp);

   if (n_go_may_yield_control() && (cp = ok_vlook(crt)) != NULL) {
      size_t rows;

      if(*cp == '\0')
         rows = (size_t)n_scrnheight;
      else
         n_idec_uiz_cp(&rows, cp, 0, NULL);

      if (rows > 0 && lines == 0) {
         while ((c = getc(fp)) != EOF)
            if (c == '\n' && ++lines >= rows)
               break;
         really_rewind(fp);
      }

      if (lines >= rows) {
         char const *env_add[2], *pager;

         pager = n_pager_get(&env_add[0]);
         env_add[1] = NULL;
         n_child_run(pager, NULL, fileno(fp), n_CHILD_FD_PASS, NULL,NULL,NULL,
            env_add, NULL);
         goto jleave;
      }
   }

   while ((c = getc(fp)) != EOF)
      putc(c, n_stdout);
jleave:
   NYD_OU;
}

FL enum protocol
which_protocol(char const *name, bool_t check_stat, bool_t try_hooks,
   char const **adjusted_or_null)
{
   /* TODO This which_protocol() sickness should be URL::new()->protocol() */
   char const *cp, *orig_name;
   enum protocol rv = PROTO_UNKNOWN;
   NYD_IN;

   if(name[0] == '%' && name[1] == ':')
      name += 2;
   orig_name = name;

   for (cp = name; *cp && *cp != ':'; cp++)
      if (!alnumchar(*cp))
         goto jfile;

   if(cp[0] == ':' && cp[1] == '/' && cp[2] == '/'){
      if(!strncmp(name, "file", sizeof("file") -1) ||
            !strncmp(name, "mbox", sizeof("mbox") -1))
         rv = PROTO_FILE;
      else if(!strncmp(name, "maildir", sizeof("maildir") -1)){
#ifdef mx_HAVE_MAILDIR
         rv = PROTO_MAILDIR;
#else
         n_err(_("No Maildir directory support compiled in\n"));
#endif
      }else if(!strncmp(name, "pop3", sizeof("pop3") -1)){
#ifdef mx_HAVE_POP3
         rv = PROTO_POP3;
#else
         n_err(_("No POP3 support compiled in\n"));
#endif
      }else if(!strncmp(name, "pop3s", sizeof("pop3s") -1)){
#if defined mx_HAVE_POP3 && defined mx_HAVE_TLS
         rv = PROTO_POP3;
#else
         n_err(_("No POP3S support compiled in\n"));
#endif
      }else if(!strncmp(name, "imap", sizeof("imap") -1)){
#ifdef mx_HAVE_IMAP
         rv = PROTO_IMAP;
#else
         n_err(_("No IMAP support compiled in\n"));
#endif
      }else if(!strncmp(name, "imaps", sizeof("imaps") -1)){
#if defined mx_HAVE_IMAP && defined mx_HAVE_TLS
         rv = PROTO_IMAP;
#else
         n_err(_("No IMAPS support compiled in\n"));
#endif
      }
      orig_name = &cp[3];
      goto jleave;
   }

jfile:
   rv = PROTO_FILE;

   if(check_stat || try_hooks){
      struct n_file_type ft;
      struct stat stb;
      char *np;
      size_t sz;

      np = n_lofi_alloc((sz = strlen(name)) + 4 +1);
      memcpy(np, name, sz + 1);

      if(!stat(name, &stb)){
         if(S_ISDIR(stb.st_mode)
#ifdef mx_HAVE_MAILDIR
               && (memcpy(&np[sz], "/tmp", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
               (memcpy(&np[sz], "/new", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
               (memcpy(&np[sz], "/cur", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode))
#endif
               ){
#ifdef mx_HAVE_MAILDIR
            rv = PROTO_MAILDIR;
#else
            rv = PROTO_UNKNOWN;
#endif
         }
      }else if(try_hooks && n_filetype_trial(&ft, name))
         orig_name = savecatsep(name, '.', ft.ft_ext_dat);
      else if((cp = ok_vlook(newfolders)) != NULL &&
            !asccasecmp(cp, "maildir")){
#ifdef mx_HAVE_MAILDIR
         rv = PROTO_MAILDIR;
#else
         n_err(_("*newfolders*: no Maildir directory support compiled in\n"));
#endif
      }

      n_lofi_free(np);
   }
jleave:
   if(adjusted_or_null != NULL)
      *adjusted_or_null = orig_name;
   NYD_OU;
   return rv;
}

FL char *
n_c_to_hex_base16(char store[3], char c){
   static char const itoa16[] = "0123456789ABCDEF";
   NYD2_IN;

   store[2] = '\0';
   store[1] = itoa16[(ui8_t)c & 0x0F];
   c = ((ui8_t)c >> 4) & 0x0F;
   store[0] = itoa16[(ui8_t)c];
   NYD2_OU;
   return store;
}

FL si32_t
n_c_from_hex_base16(char const hex[2]){
   static ui8_t const atoi16[] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x30-0x37 */
      0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x38-0x3F */
      0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, /* 0x40-0x47 */
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x48-0x4f */
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x50-0x57 */
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x58-0x5f */
      0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF  /* 0x60-0x67 */
   };
   ui8_t i1, i2;
   si32_t rv;
   NYD2_IN;

   if ((i1 = (ui8_t)hex[0] - '0') >= n_NELEM(atoi16) ||
         (i2 = (ui8_t)hex[1] - '0') >= n_NELEM(atoi16))
      goto jerr;
   i1 = atoi16[i1];
   i2 = atoi16[i2];
   if ((i1 | i2) & 0xF0u)
      goto jerr;
   rv = i1;
   rv <<= 4;
   rv += i2;
jleave:
   NYD2_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

FL enum n_idec_state
n_idec_buf(void *resp, char const *cbuf, uiz_t clen, ui8_t base,
      enum n_idec_mode idm, char const **endptr_or_null){
   ui8_t currc;
   ui64_t res, cut;
   enum n_idec_state rv;
   NYD_IN;

   idm &= n__IDEC_MODE_MASK;
   rv = n_IDEC_STATE_NONE | idm;
   res = 0;

   if(clen == UIZ_MAX){
      if(*cbuf == '\0')
         goto jeinval;
   }else if(clen == 0)
      goto jeinval;

   assert(base != 1 && base <= 36);
   /*if(base == 1 || base > 36)
    *   goto jeinval;*/

   /* Leading WS */
   while(spacechar(*cbuf))
      if(*++cbuf == '\0' || --clen == 0)
         goto jeinval;

   /* Check sign */
   switch(*cbuf){
   case '-':
      rv |= n_IDEC_STATE_SEEN_MINUS;
      /* FALLTHROUGH */
   case '+':
      if(*++cbuf == '\0' || --clen == 0)
         goto jeinval;
      break;
   }

   /* Base detection/skip */
   if(*cbuf != '0'){
      if(base == 0){
         base = 10;

         /* Support BASE#number prefix, where BASE is decimal 2-36 */
         if(clen > 2){
            char c1, c2, c3;

            if(((c1 = cbuf[0]) >= '0' && c1 <= '9') &&
                  (((c2 = cbuf[1]) == '#') ||
                   (c2 >= '0' && c2 <= '9' && clen > 3 && cbuf[2] == '#'))){
               base = a_aux_idec_atoi[(ui8_t)c1];
               if(c2 == '#')
                  c3 = cbuf[2];
               else{
                  c3 = cbuf[3];
                  base *= 10; /* xxx Inline atoi decimal base */
                  base += a_aux_idec_atoi[(ui8_t)c2];
               }

               /* We do not interpret this as BASE#number at all if either we
                * did not get a valid base or if the first char is not valid
                * according to base, to comply to the latest interpretion of
                * "prefix", see comment for standard prefixes below */
               if(base < 2 || base > 36 || a_aux_idec_atoi[(ui8_t)c3] >= base)
                  base = 10;
               else if(c2 == '#')
                  clen -= 2, cbuf += 2;
               else
                  clen -= 3, cbuf += 3;
            }
         }
      }

      /* Character must be valid for base */
      currc = a_aux_idec_atoi[(ui8_t)*cbuf];
      if(currc >= base)
         goto jeinval;
   }else{
      /* 0 always valid as is, fallback base 10 */
      if(*++cbuf == '\0' || --clen == 0)
         goto jleave;

      /* Base "detection" */
      if(base == 0 || base == 2 || base == 16){
         switch(*cbuf){
         case 'x':
         case 'X':
            if((base & 2) == 0){
               base = 0x10;
               goto jprefix_skip;
            }
            break;
         case 'b':
         case 'B':
            if((base & 16) == 0){
               base = 2; /* 0b10 */
               /* Char after prefix must be valid.  However, after some error
                * in the tor software all libraries (which had to) turned to
                * an interpretation of the C standard which says that the
                * prefix may optionally precede an otherwise valid sequence,
                * which means that "0x" is not a STATE_INVAL error but gives
                * a "0" result with a "STATE_BASE" error and a rest of "x" */
jprefix_skip:
#if 1
               if(clen > 1 && a_aux_idec_atoi[(ui8_t)cbuf[1]] < base)
                  --clen, ++cbuf;
#else
               if(*++cbuf == '\0' || --clen == 0)
                  goto jeinval;

               /* Character must be valid for base, invalid otherwise */
               currc = a_aux_idec_atoi[(ui8_t)*cbuf];
               if(currc >= base)
                  goto jeinval;
#endif
            }
            break;
         default:
            if(base == 0)
               base = 010;
            break;
         }
      }

      /* Character must be valid for base, _EBASE otherwise */
      currc = a_aux_idec_atoi[(ui8_t)*cbuf];
      if(currc >= base)
         goto jebase;
   }

   for(cut = a_aux_idec_cutlimit[base - 2];;){
      if(res >= cut){
         if(res == cut){
            res *= base;
            if(res > UI64_MAX - currc)
               goto jeover;
            res += currc;
         }else
            goto jeover;
      }else{
         res *= base;
         res += currc;
      }

      if(*++cbuf == '\0' || --clen == 0)
         break;

      currc = a_aux_idec_atoi[(ui8_t)*cbuf];
      if(currc >= base)
         goto jebase;
   }

jleave:
   do{
      ui64_t uimask;

      switch(rv & n__IDEC_MODE_LIMIT_MASK){
      case n_IDEC_MODE_LIMIT_8BIT: uimask = UI8_MAX; break;
      case n_IDEC_MODE_LIMIT_16BIT: uimask = UI16_MAX; break;
      case n_IDEC_MODE_LIMIT_32BIT: uimask = UI32_MAX; break;
      default: uimask = UI64_MAX; break;
      }
      if((rv & n_IDEC_MODE_SIGNED_TYPE) &&
            (!(rv & n_IDEC_MODE_POW2BASE_UNSIGNED) || !n_ISPOW2(base)))
         uimask >>= 1;

      if(res & ~uimask){
         /* XXX never entered unless _SIGNED_TYPE! */
         if((rv & (n_IDEC_MODE_SIGNED_TYPE | n_IDEC_STATE_SEEN_MINUS)
               ) == (n_IDEC_MODE_SIGNED_TYPE | n_IDEC_STATE_SEEN_MINUS)){
            if(res > uimask + 1){
               res = uimask << 1;
               res &= ~uimask;
            }else{
               res = -res;
               break;
            }
         }else
            res = uimask;
         if(!(rv & n_IDEC_MODE_LIMIT_NOERROR))
            rv |= n_IDEC_STATE_EOVERFLOW;
      }else if(rv & n_IDEC_STATE_SEEN_MINUS)
         res = -res;
   }while(0);

   switch(rv & n__IDEC_MODE_LIMIT_MASK){
   case n_IDEC_MODE_LIMIT_8BIT:
      if(rv & n_IDEC_MODE_SIGNED_TYPE)
         *(si8_t*)resp = (si8_t)res;
      else
         *(ui8_t*)resp = (ui8_t)res;
      break;
   case n_IDEC_MODE_LIMIT_16BIT:
      if(rv & n_IDEC_MODE_SIGNED_TYPE)
         *(si16_t*)resp = (si16_t)res;
      else
         *(ui16_t*)resp = (ui16_t)res;
      break;
   case n_IDEC_MODE_LIMIT_32BIT:
      if(rv & n_IDEC_MODE_SIGNED_TYPE)
         *(si32_t*)resp = (si32_t)res;
      else
         *(ui32_t*)resp = (ui32_t)res;
      break;
   default:
      if(rv & n_IDEC_MODE_SIGNED_TYPE)
         *(si64_t*)resp = (si64_t)res;
      else
         *(ui64_t*)resp = (ui64_t)res;
      break;
   }

   if(endptr_or_null != NULL)
      *endptr_or_null = cbuf;
   if(*cbuf == '\0' || clen == 0)
      rv |= n_IDEC_STATE_CONSUMED;
   NYD_OU;
   return rv;

jeinval:
   rv |= n_IDEC_STATE_EINVAL;
   goto j_maxval;
jebase:
   /* Not a base error for terminator and whitespace! */
   if(*cbuf != '\0' && !spacechar(*cbuf))
      rv |= n_IDEC_STATE_EBASE;
   goto jleave;

jeover:
   /* Overflow error: consume input until bad character or length out */
   for(;;){
      if(*++cbuf == '\0' || --clen == 0)
         break;
      currc = a_aux_idec_atoi[(ui8_t)*cbuf];
      if(currc >= base)
         break;
   }

   rv |= n_IDEC_STATE_EOVERFLOW;
j_maxval:
   if(rv & n_IDEC_MODE_SIGNED_TYPE)
      res = (rv & n_IDEC_STATE_SEEN_MINUS) ? (ui64_t)SI64_MIN
            : (ui64_t)SI64_MAX;
   else
      res = UI64_MAX;
   rv &= ~n_IDEC_STATE_SEEN_MINUS;
   goto jleave;
}

FL char *
n_ienc_buf(char cbuf[n_IENC_BUFFER_SIZE], ui64_t value, ui8_t base,
      enum n_ienc_mode iem){
   enum{a_ISNEG = 1u<<n__IENC_MODE_SHIFT};

   ui8_t shiftmodu;
   char const *itoa;
   char *rv;
   NYD_IN;

   iem &= n__IENC_MODE_MASK;

   assert(base != 1 && base <= 36);
   /*if(base == 1 || base > 36){
    *   rv = NULL;
    *   goto jleave;
    *}*/

   *(rv = &cbuf[n_IENC_BUFFER_SIZE -1]) = '\0';
   itoa = (iem & n_IENC_MODE_LOWERCASE) ? a_aux_ienc_itoa_lower
         : a_aux_ienc_itoa_upper;

   if((si64_t)value < 0){
      iem |= a_ISNEG;
      if(iem & n_IENC_MODE_SIGNED_TYPE){
         /* self->is_negative = TRU1; */
         value = -value;
      }
   }

   if((shiftmodu = a_aux_ienc_shifts[base - 2]) != 0){
      --base; /* convert to mask */
      do{
         *--rv = itoa[value & base];
         value >>= shiftmodu;
      }while(value != 0);

      if(!(iem & n_IENC_MODE_NO_PREFIX)){
         /* self->before_prefix = cp; */
         if(shiftmodu == 4)
            *--rv = 'x';
         else if(shiftmodu == 1)
            *--rv = 'b';
         else if(shiftmodu != 3){
            ++base; /* Reconvert from mask */
            goto jnumber_sign_prefix;
         }
         *--rv = '0';
      }
   }else{
      do{
         shiftmodu = value % base;
         value /= base;
         *--rv = itoa[shiftmodu];
      }while(value != 0);

      if(!(iem & n_IENC_MODE_NO_PREFIX) && base != 10){
jnumber_sign_prefix:
         value = base;
         base = 10;
         *--rv = '#';
         do{
            shiftmodu = value % base;
            value /= base;
            *--rv = itoa[shiftmodu];
         }while(value != 0);
      }

      if(iem & n_IENC_MODE_SIGNED_TYPE){
         char c;

         if(iem & a_ISNEG)
            c = '-';
         else if(iem & n_IENC_MODE_SIGNED_PLUS)
            c = '+';
         else if(iem & n_IENC_MODE_SIGNED_SPACE)
            c = ' ';
         else
            c = '\0';

         if(c != '\0')
            *--rv = c;
      }
   }
   NYD_OU;
   return rv;
}

FL ui32_t
n_torek_hash(char const *name){
   /* Chris Torek's hash */
   char c;
   ui32_t h;
   NYD2_IN;

   for(h = 0; (c = *name++) != '\0';)
      h = (h * 33) + c;
   NYD2_OU;
   return h;
}

FL ui32_t
n_torek_ihashn(char const *dat, size_t len){
   /* See n_torek_hash() */
   char c;
   ui32_t h;
   NYD2_IN;

   if(len == UIZ_MAX)
      for(h = 0; (c = *dat++) != '\0';)
         h = (h * 33) + lowerconv(c);
   else
      for(h = 0; len > 0; --len){
         c = *dat++;
         h = (h * 33) + lowerconv(c);
      }
   NYD2_OU;
   return h;
}

FL ui32_t
n_prime_next(ui32_t n){
   static ui32_t const primes[] = {
      5, 11, 23, 47, 97, 157, 283,
      509, 1021, 2039, 4093, 8191, 16381, 32749, 65521,
      131071, 262139, 524287, 1048573, 2097143, 4194301,
      8388593, 16777213, 33554393, 67108859, 134217689,
      268435399, 536870909, 1073741789, 2147483647
   };
   ui32_t i, mprime;
   NYD2_IN;

   i = (n < primes[n_NELEM(primes) / 4] ? 0
         : (n < primes[n_NELEM(primes) / 2] ? n_NELEM(primes) / 4
         : n_NELEM(primes) / 2));

   do if((mprime = primes[i]) > n)
      break;
   while(++i < n_NELEM(primes));

   if(i == n_NELEM(primes) && mprime < n)
      mprime = n;
   NYD2_OU;
   return mprime;
}

FL char const *
n_getdeadletter(void){
   char const *cp;
   bool_t bla;
   NYD_IN;

   bla = FAL0;
jredo:
   cp = fexpand(ok_vlook(DEAD), FEXP_LOCAL | FEXP_NSHELL);
   if(cp == NULL || strlen(cp) >= PATH_MAX){
      if(!bla){
         n_err(_("Failed to expand *DEAD*, setting default (%s): %s\n"),
            VAL_DEAD, n_shexp_quote_cp((cp == NULL ? n_empty : cp), FAL0));
         ok_vclear(DEAD);
         bla = TRU1;
         goto jredo;
      }else{
         cp = savecatsep(ok_vlook(TMPDIR), '/', VAL_DEAD_BASENAME);
         n_err(_("Cannot expand *DEAD*, using: %s\n"), cp);
      }
   }
   NYD_OU;
   return cp;
}

FL char *
n_nodename(bool_t mayoverride){
   static char *sys_hostname, *hostname; /* XXX free-at-exit */

   struct utsname ut;
   char *hn;
#ifdef mx_HAVE_SOCKETS
# ifdef mx_HAVE_GETADDRINFO
   struct addrinfo hints, *res;
# else
   struct hostent *hent;
# endif
#endif
   NYD2_IN;

   if(su_state_has(su_STATE_REPRODUCIBLE))
      hn = n_UNCONST(su_reproducible_build);
   else if(mayoverride && (hn = ok_vlook(hostname)) != NULL && *hn != '\0'){
      ;
   }else if((hn = sys_hostname) == NULL){
      bool_t lofi;

      lofi = FAL0;
      uname(&ut);
      hn = ut.nodename;

#ifdef mx_HAVE_SOCKETS
# ifdef mx_HAVE_GETADDRINFO
      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_CANONNAME;
      if(getaddrinfo(hn, NULL, &hints, &res) == 0){
         if(res->ai_canonname != NULL){
            size_t l;

            l = strlen(res->ai_canonname) +1;
            hn = n_lofi_alloc(l);
            lofi = TRU1;
            memcpy(hn, res->ai_canonname, l);
         }
         freeaddrinfo(res);
      }
# else
      hent = gethostbyname(hn);
      if(hent != NULL)
         hn = hent->h_name;
# endif
#endif /* mx_HAVE_SOCKETS */

#ifdef mx_HAVE_IDNA
      /* C99 */{
         struct n_string cnv;

         n_string_creat(&cnv);
         if(!n_idna_to_ascii(&cnv, hn, UIZ_MAX))
            n_panic(_("The system hostname is invalid, "
                  "IDNA conversion failed: %s\n"),
               n_shexp_quote_cp(hn, FAL0));
         sys_hostname = n_string_cp(&cnv);
         n_string_drop_ownership(&cnv);
         /*n_string_gut(&cnv);*/
      }
#else
      sys_hostname = sstrdup(hn);
#endif

      if(lofi)
         n_lofi_free(hn);
      hn = sys_hostname;
   }

   if(hostname != NULL && hostname != sys_hostname)
      n_free(hostname);
   hostname = sstrdup(hn);
   NYD2_OU;
   return hostname;
}

#ifdef mx_HAVE_IDNA
FL bool_t
n_idna_to_ascii(struct n_string *out, char const *ibuf, size_t ilen){
   char *idna_utf8;
   bool_t lofi, rv;
   NYD_IN;

   if(ilen == UIZ_MAX)
      ilen = strlen(ibuf);

   lofi = FAL0;

   if((rv = (ilen == 0)))
      goto jleave;
   if(ibuf[ilen] != '\0'){
      lofi = TRU1;
      idna_utf8 = n_lofi_alloc(ilen +1);
      memcpy(idna_utf8, ibuf, ilen);
      idna_utf8[ilen] = '\0';
      ibuf = idna_utf8;
   }
   ilen = 0;

# ifndef mx_HAVE_ALWAYS_UNICODE_LOCALE
   if(n_psonce & n_PSO_UNICODE)
# endif
      idna_utf8 = n_UNCONST(ibuf);
# ifndef mx_HAVE_ALWAYS_UNICODE_LOCALE
   else if((idna_utf8 = n_iconv_onetime_cp(n_ICONV_NONE, "utf-8",
         ok_vlook(ttycharset), ibuf)) == NULL)
      goto jleave;
# endif

# if mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN2
   /* C99 */{
      char *idna_ascii;
      int f, rc;

      f = IDN2_NONTRANSITIONAL;
jidn2_redo:
      if((rc = idn2_to_ascii_8z(idna_utf8, &idna_ascii, f)) == IDN2_OK){
         out = n_string_assign_cp(out, idna_ascii);
         idn2_free(idna_ascii);
         rv = TRU1;
         ilen = out->s_len;
      }else if(rc == IDN2_DISALLOWED && f != IDN2_TRANSITIONAL){
         f = IDN2_TRANSITIONAL;
         goto jidn2_redo;
      }
   }

# elif mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN
   /* C99 */{
      char *idna_ascii;

      if(idna_to_ascii_8z(idna_utf8, &idna_ascii, 0) == IDNA_SUCCESS){
         out = n_string_assign_cp(out, idna_ascii);
         idn_free(idna_ascii);
         rv = TRU1;
         ilen = out->s_len;
      }
   }

# elif mx_HAVE_IDNA == n_IDNA_IMPL_IDNKIT
   ilen = strlen(idna_utf8);
jredo:
   switch(idn_encodename(
      /* LOCALCONV changed meaning in v2 and is no longer available for
       * encoding.  This makes sense, bu */
         (
#  ifdef IDN_UNICODECONV /* v2 */
         IDN_ENCODE_APP & ~IDN_UNICODECONV
#  else
         IDN_DELIMMAP | IDN_LOCALMAP | IDN_NAMEPREP | IDN_IDNCONV |
         IDN_LENCHECK | IDN_ASCCHECK
#  endif
         ), idna_utf8,
         n_string_resize(n_string_trunc(out, 0), ilen)->s_dat, ilen)){
   case idn_buffer_overflow:
      ilen += HOST_NAME_MAX +1;
      goto jredo;
   case idn_success:
      rv = TRU1;
      ilen = strlen(out->s_dat);
      break;
   default:
      ilen = 0;
      break;
   }

# else
#  error Unknown mx_HAVE_IDNA
# endif
jleave:
   if(lofi)
      n_lofi_free(n_UNCONST(ibuf));
   out = n_string_trunc(out, ilen);
   NYD_OU;
   return rv;
}
#endif /* mx_HAVE_IDNA */

FL char *
n_random_create_buf(char *dat, size_t len, ui32_t *reprocnt_or_null){
   struct str b64;
   char *indat, *cp, *oudat;
   size_t i, inlen, oulen;
   NYD_IN;

   if(!(n_psonce & n_PSO_RANDOM_INIT)){
      n_psonce |= n_PSO_RANDOM_INIT;

      if(n_poption & n_PO_D_V){
         char const *prngn;

#if mx_HAVE_RANDOM == n_RANDOM_IMPL_ARC4
         prngn = "arc4random";
#elif mx_HAVE_RANDOM == n_RANDOM_IMPL_TLS
         prngn = "*TLS RAND_*";
#elif mx_HAVE_RANDOM == n_RANDOM_IMPL_GETRANDOM
         prngn = "getrandom(2/3) + builtin ARC4";
#elif mx_HAVE_RANDOM == n_RANDOM_IMPL_URANDOM
         prngn = "/dev/urandom + builtin ARC4";
#elif mx_HAVE_RANDOM == n_RANDOM_IMPL_BUILTIN
         prngn = "builtin ARC4";
#else
# error n_random_create_buf(): the value of mx_HAVE_RANDOM is not supported
#endif
         n_err(_("P(seudo)R(andomNumber)G(enerator): %s\n"), prngn);
      }

#if mx_HAVE_RANDOM != n_RANDOM_IMPL_ARC4 && mx_HAVE_RANDOM != n_RANDOM_IMPL_TLS
      a_aux_rand_init();
#endif
   }

   /* We use our base64 encoder with _NOPAD set, so ensure the encoded result
    * with PAD stripped is still longer than what the user requests, easy way.
    * The relation of base64 is fixed 3 in = 4 out, and we do not want to
    * include the base64 PAD characters in our random string: give some pad */
   i = len;
   if((inlen = i % 3) != 0)
      i += 3 - inlen;
jinc1:
   inlen = i >> 2;
   oulen = inlen << 2;
   if(oulen < len){
      i += 3;
      goto jinc1;
   }
   inlen = inlen + (inlen << 1);

   indat = n_lofi_alloc(inlen +1);

   if(!su_state_has(su_STATE_REPRODUCIBLE) || reprocnt_or_null == NULL){
#if mx_HAVE_RANDOM == n_RANDOM_IMPL_TLS
      n_tls_rand_bytes(indat, inlen);
#elif mx_HAVE_RANDOM != n_RANDOM_IMPL_ARC4
      for(i = inlen; i-- > 0;)
         indat[i] = (char)a_aux_rand_get8();
#else
      for(cp = indat, i = inlen; i > 0;){
         union {ui32_t i4; char c[4];} r;
         size_t j;

         r.i4 = (ui32_t)arc4random();
         switch((j = i & 3)){
         case 0:  cp[3] = r.c[3]; j = 4; /* FALLTHRU */
         case 3:  cp[2] = r.c[2]; /* FALLTHRU */
         case 2:  cp[1] = r.c[1]; /* FALLTHRU */
         default: cp[0] = r.c[0]; break;
         }
         cp += j;
         i -= j;
      }
#endif
   }else{
      for(cp = indat, i = inlen; i > 0;){
         union {ui32_t i4; char c[4];} r;
         size_t j;

         r.i4 = ++*reprocnt_or_null;
         if(su_BOM_IS_BIG()){ /* TODO BSWAP */
            char x;

            x = r.c[0];
            r.c[0] = r.c[3];
            r.c[3] = x;
            x = r.c[1];
            r.c[1] = r.c[2];
            r.c[2] = x;
         }
         switch((j = i & 3)){
         case 0:  cp[3] = r.c[3]; j = 4; /* FALLTHRU */
         case 3:  cp[2] = r.c[2]; /* FALLTHRU */
         case 2:  cp[1] = r.c[1]; /* FALLTHRU */
         default: cp[0] = r.c[0]; break;
         }
         cp += j;
         i -= j;
      }
   }

   oudat = (len >= oulen) ? dat : n_lofi_alloc(oulen +1);
   b64.s = oudat;
   b64_encode_buf(&b64, indat, inlen, B64_BUF | B64_RFC4648URL | B64_NOPAD);
   assert(b64.l >= len);
   memcpy(dat, b64.s, len);
   dat[len] = '\0';
   if(oudat != dat)
      n_lofi_free(oudat);

   n_lofi_free(indat);

   NYD_OU;
   return dat;
}

FL char *
n_random_create_cp(size_t len, ui32_t *reprocnt_or_null){
   char *dat;
   NYD_IN;

   dat = n_autorec_alloc(len +1);
   dat = n_random_create_buf(dat, len, reprocnt_or_null);
   NYD_OU;
   return dat;
}

FL bool_t
n_boolify(char const *inbuf, uiz_t inlen, bool_t emptyrv){
   bool_t rv;
   NYD2_IN;
   assert(inlen == 0 || inbuf != NULL);

   if(inlen == UIZ_MAX)
      inlen = strlen(inbuf);

   if(inlen == 0)
      rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
   else{
      if((inlen == 1 && (*inbuf == '1' || *inbuf == 'y' || *inbuf == 'Y')) ||
            !ascncasecmp(inbuf, "true", inlen) ||
            !ascncasecmp(inbuf, "yes", inlen) ||
            !ascncasecmp(inbuf, "on", inlen))
         rv = TRU1;
      else if((inlen == 1 &&
               (*inbuf == '0' || *inbuf == 'n' || *inbuf == 'N')) ||
            !ascncasecmp(inbuf, "false", inlen) ||
            !ascncasecmp(inbuf, "no", inlen) ||
            !ascncasecmp(inbuf, "off", inlen))
         rv = FAL0;
      else{
         ui64_t ib;

         if((n_idec_buf(&ib, inbuf, inlen, 0, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED)
            rv = TRUM1;
         else
            rv = (ib != 0);
      }
   }
   NYD2_OU;
   return rv;
}

FL bool_t
n_quadify(char const *inbuf, uiz_t inlen, char const *prompt, bool_t emptyrv){
   bool_t rv;
   NYD2_IN;
   assert(inlen == 0 || inbuf != NULL);

   if(inlen == UIZ_MAX)
      inlen = strlen(inbuf);

   if(inlen == 0)
      rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
   else if((rv = n_boolify(inbuf, inlen, emptyrv)) < FAL0 &&
         !ascncasecmp(inbuf, "ask-", 4) &&
         (rv = n_boolify(&inbuf[4], inlen - 4, emptyrv)) >= FAL0 &&
         (n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT))
      rv = getapproval(prompt, rv);
   NYD2_OU;
   return rv;
}

FL bool_t
n_is_all_or_aster(char const *name){
   bool_t rv;
   NYD2_IN;

   rv = ((name[0] == '*' && name[1] == '\0') || !asccasecmp(name, "all"));
   NYD2_OU;
   return rv;
}

FL struct n_timespec const *
n_time_now(bool_t force_update){ /* TODO event loop update IF cmd requests! */
   static struct n_timespec ts_now;
   NYD2_IN;

   if(n_UNLIKELY(su_state_has(su_STATE_REPRODUCIBLE))){
      /* Guaranteed 32-bit posnum TODO SOURCE_DATE_EPOCH should be 64-bit! */
      (void)n_idec_si64_cp(&ts_now.ts_sec, ok_vlook(SOURCE_DATE_EPOCH), 0,NULL);
      ts_now.ts_nsec = 0;
   }else if(force_update || ts_now.ts_sec == 0){
#ifdef mx_HAVE_CLOCK_GETTIME
      struct timespec ts;

      clock_gettime(CLOCK_REALTIME, &ts);
      ts_now.ts_sec = (si64_t)ts.tv_sec;
      ts_now.ts_nsec = (siz_t)ts.tv_nsec;
#elif defined mx_HAVE_GETTIMEOFDAY
      struct timeval tv;

      gettimeofday(&tv, NULL);
      ts_now.ts_sec = (si64_t)tv.tv_sec;
      ts_now.ts_nsec = (siz_t)tv.tv_usec * 1000;
#else
      ts_now.ts_sec = (si64_t)time(NULL);
      ts_now.ts_nsec = 0;
#endif
   }

   /* Just in case.. */
   if(n_UNLIKELY(ts_now.ts_sec < 0))
      ts_now.ts_sec = 0;
   NYD2_OU;
   return &ts_now;
}

FL void
time_current_update(struct time_current *tc, bool_t full_update){
   NYD_IN;
   tc->tc_time = (time_t)n_time_now(TRU1)->ts_sec;

   if(full_update){
      char *cp;
      struct tm *tmp;
      time_t t;

      t = tc->tc_time;
jredo:
      if((tmp = gmtime(&t)) == NULL){
         t = 0;
         goto jredo;
      }
      memcpy(&tc->tc_gm, tmp, sizeof tc->tc_gm);
      if((tmp = localtime(&t)) == NULL){
         t = 0;
         goto jredo;
      }
      memcpy(&tc->tc_local, tmp, sizeof tc->tc_local);
      cp = sstpcpy(tc->tc_ctime, n_time_ctime((si64_t)tc->tc_time, tmp));
      *cp++ = '\n';
      *cp = '\0';
      assert(PTR2SIZE(++cp - tc->tc_ctime) < sizeof(tc->tc_ctime));
   }
   NYD_OU;
}

FL char *
n_time_ctime(si64_t secsepoch, struct tm const *localtime_or_nil){/* TODO err*/
   /* Problem is that secsepoch may be invalid for representation of ctime(3),
    * which indeed is asctime(localtime(t)); musl libc says for asctime(3):
    *    ISO C requires us to use the above format string,
    *    even if it will not fit in the buffer. Thus asctime_r
    *    is _supposed_ to crash if the fields in tm are too large.
    *    We follow this behavior and crash "gracefully" to warn
    *    application developers that they may not be so lucky
    *    on other implementations (e.g. stack smashing..).
    * So we need to do it on our own or the libc may kill us */
   static char buf[32]; /* TODO static buffer (-> datetime_to_format()) */

   si32_t y, md, th, tm, ts;
   char const *wdn, *mn;
   struct tm const *tmp;
   NYD_IN;

   if((tmp = localtime_or_nil) == NULL){
      time_t t;

      t = (time_t)secsepoch;
jredo:
      if((tmp = localtime(&t)) == NULL){
         /* TODO error log */
         t = 0;
         goto jredo;
      }
   }

   if(n_UNLIKELY((y = tmp->tm_year) < 0 || y >= 9999/*SI32_MAX*/ - 1900)){
      y = 1970;
      wdn = n_weekday_names[4];
      mn = n_month_names[0];
      md = 1;
      th = tm = ts = 0;
   }else{
      y += 1900;
      wdn = (tmp->tm_wday >= 0 && tmp->tm_wday <= 6)
            ? n_weekday_names[tmp->tm_wday] : n_qm;
      mn = (tmp->tm_mon >= 0 && tmp->tm_mon <= 11)
            ? n_month_names[tmp->tm_mon] : n_qm;

      if((md = tmp->tm_mday) < 1 || md > 31)
         md = 1;

      if((th = tmp->tm_hour) < 0 || th > 23)
         th = 0;
      if((tm = tmp->tm_min) < 0 || tm > 59)
         tm = 0;
      if((ts = tmp->tm_sec) < 0 || ts > 60)
         ts = 0;
   }

   (void)snprintf(buf, sizeof buf, "%3s %3s%3d %.2d:%.2d:%.2d %d",
         wdn, mn, md, th, tm, ts, y);
   NYD_OU;
   return buf;
}

FL uiz_t
n_msleep(uiz_t millis, bool_t ignint){
   uiz_t rv;
   NYD2_IN;

#ifdef mx_HAVE_NANOSLEEP
   /* C99 */{
      struct timespec ts, trem;
      int i;

      ts.tv_sec = millis / 1000;
      ts.tv_nsec = (millis %= 1000) * 1000 * 1000;

      while((i = nanosleep(&ts, &trem)) != 0 && ignint)
         ts = trem;
      rv = (i == 0) ? 0 : (trem.tv_sec * 1000) + (trem.tv_nsec / (1000 * 1000));
   }

#elif defined mx_HAVE_SLEEP
   if((millis /= 1000) == 0)
      millis = 1;
   while((rv = sleep((unsigned int)millis)) != 0 && ignint)
      millis = rv;
#else
# error Configuration should have detected a function for sleeping.
#endif

   NYD2_OU;
   return rv;
}

FL void
n_err(char const *format, ...){
   va_list ap;
   NYD2_IN;

   va_start(ap, format);
#ifdef mx_HAVE_ERRORS
   if(n_psonce & n_PSO_INTERACTIVE)
      n_verr(format, ap);
   else
#endif
   {
      size_t len;
      bool_t doname;

      doname = FAL0;

      while(*format == '\n'){
         doname = TRU1;
         putc('\n', n_stderr);
         ++format;
      }

      if(doname)
         a_aux_err_linelen = 0;

      if((len = strlen(format)) > 0){
         if(doname || a_aux_err_linelen == 0){
            char const *cp;

            if(*(cp = ok_vlook(log_prefix)) != '\0')
               fputs(cp, n_stderr);
         }
         vfprintf(n_stderr, format, ap);

         /* C99 */{
            size_t i = len;
            do{
               if(format[--len] == '\n'){
                  a_aux_err_linelen = (i -= ++len);
                  break;
               }
               ++a_aux_err_linelen;
            }while(len > 0);
         }
      }

      fflush(n_stderr);
   }
   va_end(ap);
   NYD2_OU;
}

FL void
n_verr(char const *format, va_list ap){
#ifdef mx_HAVE_ERRORS
   struct a_aux_err_node *enp;
#endif
   bool_t doname;
   size_t len;
   NYD2_IN;

   doname = FAL0;

   while(*format == '\n'){
      putc('\n', n_stderr);
      doname = TRU1;
      ++format;
   }

   if(doname){
      a_aux_err_linelen = 0;
#ifdef mx_HAVE_ERRORS
      if(n_psonce & n_PSO_INTERACTIVE){
         if((enp = a_aux_err_tail) != NULL &&
               (enp->ae_str.s_len > 0 &&
                enp->ae_str.s_dat[enp->ae_str.s_len - 1] != '\n'))
            n_string_push_c(&enp->ae_str, '\n');
      }
#endif
   }

   if((len = strlen(format)) == 0)
      goto jleave;
#ifdef mx_HAVE_ERRORS
   n_pstate |= n_PS_ERRORS_PROMPT;
#endif

   if(doname || a_aux_err_linelen == 0){
      char const *cp;

      if(*(cp = ok_vlook(log_prefix)) != '\0')
         fputs(cp, n_stderr);
   }

   /* C99 */{
      size_t i = len;
      do{
         if(format[--len] == '\n'){
            a_aux_err_linelen = (i -= ++len);
            break;
         }
         ++a_aux_err_linelen;
      }while(len > 0);
   }

#ifdef mx_HAVE_ERRORS
   if(!(n_psonce & n_PSO_INTERACTIVE))
#endif
      vfprintf(n_stderr, format, ap);
#ifdef mx_HAVE_ERRORS
   else{
      int imax, i;
      n_LCTAV(ERRORS_MAX > 3);

      /* Link it into the `errors' message ring */
      if((enp = a_aux_err_tail) == NULL){
jcreat:
         enp = n_alloc(sizeof *enp);
         enp->ae_next = NULL;
         n_string_creat(&enp->ae_str);
         if(a_aux_err_tail != NULL)
            a_aux_err_tail->ae_next = enp;
         else
            a_aux_err_head = enp;
         a_aux_err_tail = enp;
         ++a_aux_err_cnt;
      }else if(doname ||
            (enp->ae_str.s_len > 0 &&
             enp->ae_str.s_dat[enp->ae_str.s_len - 1] == '\n')){
         if(a_aux_err_cnt < ERRORS_MAX)
            goto jcreat;

         a_aux_err_head = (enp = a_aux_err_head)->ae_next;
         a_aux_err_tail->ae_next = enp;
         a_aux_err_tail = enp;
         enp->ae_next = NULL;
         n_string_trunc(&enp->ae_str, 0);
      }

# ifdef mx_HAVE_N_VA_COPY
      imax = 64;
# else
      imax = n_MIN(LINESIZE, 1024);
# endif
      for(i = imax;; imax = ++i /* xxx could wrap, maybe */){
# ifdef mx_HAVE_N_VA_COPY
         va_list vac;

         n_va_copy(vac, ap);
# else
#  define vac ap
# endif

         n_string_resize(&enp->ae_str, (len = enp->ae_str.s_len) + (size_t)i);
         i = vsnprintf(&enp->ae_str.s_dat[len], (size_t)i, format, vac);
# ifdef mx_HAVE_N_VA_COPY
         va_end(vac);
# else
#  undef vac
# endif
         if(i <= 0)
            goto jleave;
         if(UICMP(z, i, >=, imax)){
# ifdef mx_HAVE_N_VA_COPY
            /* XXX Check overflow for upcoming LEN+++i! */
            n_string_trunc(&enp->ae_str, len);
            continue;
# else
            i = (int)strlen(&enp->ae_str.s_dat[len]);
# endif
         }
         break;
      }
      n_string_trunc(&enp->ae_str, len + (size_t)i);

      fwrite(&enp->ae_str.s_dat[len], 1, (size_t)i, n_stderr);
   }
#endif /* mx_HAVE_ERRORS */

jleave:
   fflush(n_stderr);
   NYD2_OU;
}

FL void
n_err_sighdl(char const *format, ...){ /* TODO sigsafe; obsolete! */
   va_list ap;
   NYD_X;

   va_start(ap, format);
   vfprintf(n_stderr, format, ap);
   va_end(ap);
   fflush(n_stderr);
}

FL void
n_perr(char const *msg, int errval){
   int e;
   char const *fmt;
   NYD2_IN;

   if(msg == NULL){
      fmt = "%s%s\n";
      msg = n_empty;
   }else
      fmt = "%s: %s\n";

   e = (errval == 0) ? n_err_no : errval;
   n_err(fmt, msg, n_err_to_doc(e));
   if(errval == 0)
      n_err_no = e;
   NYD2_OU;
}

FL void
n_alert(char const *format, ...){
   va_list ap;
   NYD2_IN;

   n_err(a_aux_err_linelen > 0 ? _("\nAlert: ") : _("Alert: "));

   va_start(ap, format);
   n_verr(format, ap);
   va_end(ap);

   n_err("\n");
   NYD2_OU;
}

FL void
n_panic(char const *format, ...){
   va_list ap;
   NYD2_IN;

   if(a_aux_err_linelen > 0){
      putc('\n', n_stderr);
      a_aux_err_linelen = 0;
   }
   fprintf(n_stderr, "%sPanic: ", ok_vlook(log_prefix));

   va_start(ap, format);
   vfprintf(n_stderr, format, ap);
   va_end(ap);

   putc('\n', n_stderr);
   fflush(n_stderr);
   NYD2_OU;
   abort(); /* Was exit(n_EXIT_ERR); for a while, but no */
}

#ifdef mx_HAVE_ERRORS
FL int
c_errors(void *v){
   char **argv = v;
   struct a_aux_err_node *enp;
   NYD_IN;

   if(*argv == NULL)
      goto jlist;
   if(argv[1] != NULL)
      goto jerr;
   if(!asccasecmp(*argv, "show"))
      goto jlist;
   if(!asccasecmp(*argv, "clear"))
      goto jclear;
jerr:
   fprintf(n_stderr,
      _("Synopsis: errors: (<show> or) <clear> the error ring\n"));
   v = NULL;
jleave:
   NYD_OU;
   return (v == NULL) ? !STOP : !OKAY; /* xxx 1:bad 0:good -- do some */

jlist:{
      FILE *fp;
      size_t i;

      if(a_aux_err_head == NULL){
         fprintf(n_stderr, _("The error ring is empty\n"));
         goto jleave;
      }

      if((fp = Ftmp(NULL, "errors", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
            NULL){
         fprintf(n_stderr, _("tmpfile"));
         v = NULL;
         goto jleave;
      }

      for(i = 0, enp = a_aux_err_head; enp != NULL; enp = enp->ae_next)
         fprintf(fp, "%4" PRIuZ ". %s", ++i, n_string_cp(&enp->ae_str));
      /* We don't know whether last string ended with NL; be simple XXX */
      putc('\n', fp);

      page_or_print(fp, 0);
      Fclose(fp);
   }
   /* FALLTHRU */

jclear:
   a_aux_err_tail = NULL;
   a_aux_err_cnt = a_aux_err_cnt_noted = 0;
   a_aux_err_linelen = 0;
   while((enp = a_aux_err_head) != NULL){
      a_aux_err_head = enp->ae_next;
      n_string_gut(&enp->ae_str);
      n_free(enp);
   }
   goto jleave;
}
#endif /* mx_HAVE_ERRORS */

FL char const *
n_err_to_doc(si32_t eno){
   char const *rv;
   struct a_aux_err_map const *aemp;
   NYD2_IN;

   aemp = a_aux_err_map_from_no(eno);
#ifdef mx_HAVE_DOCSTRINGS
   rv = &a_aux_err_docs[aemp->aem_docoff];
#else
   rv = &a_aux_err_names[aemp->aem_nameoff];
#endif
   NYD2_OU;
   return rv;
}

FL char const *
n_err_to_name(si32_t eno){
   char const *rv;
   struct a_aux_err_map const *aemp;
   NYD2_IN;

   aemp = a_aux_err_map_from_no(eno);
   rv = &a_aux_err_names[aemp->aem_nameoff];
   NYD2_OU;
   return rv;
}

FL si32_t
n_err_from_name(char const *name){
   struct a_aux_err_map const *aemp;
   ui32_t hash, i, j, x;
   si32_t rv;
   NYD2_IN;

   hash = n_torek_hash(name);

   for(i = hash % a_AUX_ERR_REV_PRIME, j = 0; j <= a_AUX_ERR_REV_LONGEST; ++j){
      if((x = a_aux_err_revmap[i]) == a_AUX_ERR_REV_ILL)
         break;

      aemp = &a_aux_err_map[x];
      if(aemp->aem_hash == hash &&
            !strcmp(&a_aux_err_names[aemp->aem_nameoff], name)){
         rv = aemp->aem_err_no;
         goto jleave;
      }

      if(++i == a_AUX_ERR_REV_PRIME){
#ifdef a_AUX_ERR_REV_WRAPAROUND
         i = 0;
#else
         break;
#endif
      }
   }

   /* Have not found it.  But wait, it could be that the user did, e.g.,
    *    eval echo \$^ERR-$: \$^ERRDOC-$!: \$^ERRNAME-$! */
   if((n_idec_si32_cp(&rv, name, 0, NULL) &
         (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) == n_IDEC_STATE_CONSUMED){
      aemp = a_aux_err_map_from_no(rv);
      rv = aemp->aem_err_no;
      goto jleave;
   }

   rv = a_aux_err_map[n__ERR_NUMBER_VOIDOFF].aem_err_no;
jleave:
   NYD2_OU;
   return rv;
}

#ifdef mx_HAVE_REGEX
FL char const *
n_regex_err_to_doc(const regex_t *rep, int e){
   char *cp;
   size_t i;
   NYD2_IN;

   i = regerror(e, rep, NULL, 0) +1;
   cp = n_autorec_alloc(i);
   regerror(e, rep, cp, i);
   NYD2_OU;
   return cp;
}
#endif

/* s-it-mode */
