/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that don't fit anywhere else.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#if mx_HAVE_RANDOM != n_RANDOM_IMPL_ARC4 && mx_HAVE_RANDOM != n_RANDOM_IMPL_TLS
# define a_AUX_RAND_USE_BUILTIN
# if mx_HAVE_RANDOM == n_RANDOM_IMPL_GETRANDOM
#  include n_RANDOM_GETRANDOM_H
# endif
# ifdef mx_HAVE_SCHED_YIELD
#  include <sched.h>
# endif
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

# include "mx/iconv.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>

#ifdef a_AUX_RAND_USE_BUILTIN
# include <su/prime.h>
#endif

#ifdef a_AUX_RAND_USE_BUILTIN
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

#ifdef a_AUX_RAND_USE_BUILTIN
static union rand_state *a_aux_rand;
#endif

/* Error ring, for `errors' */
#ifdef mx_HAVE_ERRORS
static struct a_aux_err_node *a_aux_err_head, *a_aux_err_tail;
#endif
static size_t a_aux_err_linelen;

/* Our ARC4 random generator with its completely unacademical pseudo
 * initialization (shall /dev/urandom fail) */
#ifdef a_AUX_RAND_USE_BUILTIN
static void a_aux_rand_init(void);
su_SINLINE ui8_t a_aux_rand_get8(void);
static ui32_t a_aux_rand_weak(ui32_t seed);
#endif

/* */
static int a_aux_qsort_cpp(void const *a, void const *b);

#ifdef a_AUX_RAND_USE_BUILTIN
static void
a_aux_rand_init(void){
   union {int fd; size_t i;} u;
   n_NYD2_IN;

   a_aux_rand = n_alloc(sizeof *a_aux_rand);

# if mx_HAVE_RANDOM == n_RANDOM_IMPL_GETRANDOM
   /* getrandom(2) guarantees 256 without su_ERR_INTR..
    * However, support sequential reading to avoid possible hangs that have
    * been reported on the ML (2017-08-22, s-nail/s-mailx freezes when
    * mx_HAVE_GETRANDOM is #defined) */
   n_LCTA(sizeof(a_aux_rand->a._dat) <= 256,
      "Buffer too large to be served without su_ERR_INTR error");
   n_LCTA(sizeof(a_aux_rand->a._dat) >= 256,
      "Buffer too small to serve used array indices");
   /* C99 */{
      size_t o, i;

      for(o = 0, i = sizeof a_aux_rand->a._dat;;){
         ssize_t gr;

         gr = n_RANDOM_GETRANDOM_FUN(&a_aux_rand->a._dat[o], i);
         if(gr == -1 && su_err_no() == su_ERR_NOSYS)
            break;
         a_aux_rand->a._i = (a_aux_rand->a._dat[a_aux_rand->a._dat[1] ^
               a_aux_rand->a._dat[84]]);
         a_aux_rand->a._j = (a_aux_rand->a._dat[a_aux_rand->a._dat[65] ^
               a_aux_rand->a._dat[42]]);
         /* ..but be on the safe side */
         if(gr > 0){
            i -= (size_t)gr;
            if(i == 0)
               goto jleave;
            o += (size_t)gr;
         }
         n_err(_("Not enough entropy for the "
            "P(seudo)R(andom)N(umber)G(enerator), waiting a bit\n"));
         n_msleep(250, FAL0);
      }
   }

# elif mx_HAVE_RANDOM == n_RANDOM_IMPL_URANDOM
   if((u.fd = open("/dev/urandom", O_RDONLY)) != -1){
      bool_t ok;

      ok = (sizeof(a_aux_rand->a._dat) == (size_t)read(u.fd,
            a_aux_rand->a._dat, sizeof(a_aux_rand->a._dat)));
      close(u.fd);

      a_aux_rand->a._i = (a_aux_rand->a._dat[a_aux_rand->a._dat[1] ^
            a_aux_rand->a._dat[84]]);
      a_aux_rand->a._j = (a_aux_rand->a._dat[a_aux_rand->a._dat[65] ^
            a_aux_rand->a._dat[42]]);
      if(ok)
         goto jleave;
   }
# elif mx_HAVE_RANDOM != n_RANDOM_IMPL_BUILTIN
#  error a_aux_rand_init(): the value of mx_HAVE_RANDOM is not supported
# endif

   /* As a fallback, a homebrew seed */
   if(n_poption & n_PO_D_V)
      n_err(_("P(seudo)R(andom)N(umber)G(enerator): "
         "creating homebrew seed\n"));
   /* C99 */{
# ifdef mx_HAVE_CLOCK_GETTIME
      struct timespec ts;
# else
      struct timeval ts;
# endif
      bool_t slept;
      ui32_t seed, rnd, t, k;

      /* We first do three rounds, and then add onto that a (cramped) random
       * number of rounds; in between we give up our timeslice once (from our
       * point of view) */
      seed = (uintptr_t)a_aux_rand & UI32_MAX;
      rnd = 3;
      slept = FAL0;

      for(;;){
         /* Stir the entire pool once */
         for(u.i = n_NELEM(a_aux_rand->b32); u.i-- != 0;){

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
               a_aux_rand->b32[u.i] ^=
                  a_aux_rand_weak(seed ^ (ui32_t)ts.tv_sec);
            k = a_aux_rand->b32[u.i] % n_NELEM(a_aux_rand->b32);
            a_aux_rand->b32[k] ^= a_aux_rand->b32[u.i];
            seed ^= a_aux_rand_weak(a_aux_rand->b32[k]);
            if((rnd & 3) == 3)
               seed ^= su_prime_lookup_next(seed);
         }

         if(--rnd == 0){
            if(slept)
               break;
            rnd = (a_aux_rand_get8() % 5) + 3;
# ifdef mx_HAVE_SCHED_YIELD
            sched_yield();
# elif defined mx_HAVE_NANOSLEEP
            ts.tv_sec = 0, ts.tv_nsec = 0;
            nanosleep(&ts, NULL);
# else
            rnd += 10;
# endif
            slept = TRU1;
         }
      }

      for(u.i = sizeof(a_aux_rand->b8) * ((a_aux_rand_get8() % 5)  + 1);
            u.i != 0; --u.i)
         a_aux_rand_get8();
      goto jleave; /* (avoid unused warning) */
   }
jleave:
   n_NYD2_OU;
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
#endif /* a_AUX_RAND_USE_BUILTIN */

static int
a_aux_qsort_cpp(void const *a, void const *b){
   int rv;
   n_NYD2_IN;

   rv = su_cs_cmp(*(char**)n_UNCONST(a), *(char**)n_UNCONST(b));
   n_NYD2_OU;
   return rv;
}

FL void
n_locale_init(void){
   n_NYD2_IN;

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
   n_NYD2_OU;
}

FL size_t
n_screensize(void){
   char const *cp;
   uiz_t rv;
   n_NYD2_IN;

   if((cp = ok_vlook(screen)) != NULL){
      su_idec_uz_cp(&rv, cp, 0, NULL);
      if(rv == 0)
         rv = n_scrnheight;
   }else
      rv = n_scrnheight;

   if(rv > 2)
      rv -= 2;
   n_NYD2_OU;
   return rv;
}

FL char const *
n_pager_get(char const **env_addon){
   char const *rv;
   n_NYD_IN;

   rv = ok_vlook(PAGER);

   if(env_addon != NULL){
      *env_addon = NULL;
      /* Update the manual upon any changes:
       *    *colour-pager*, $PAGER */
      if(su_cs_find(rv, "less") != NULL){
         if(getenv("LESS") == NULL)
            *env_addon = "LESS=RXi";
      }else if(su_cs_find(rv, "lv") != NULL){
         if(getenv("LV") == NULL)
            *env_addon = "LV=-c";
      }
   }
   n_NYD_OU;
   return rv;
}

FL void
page_or_print(FILE *fp, size_t lines)
{
   int c;
   char const *cp;
   n_NYD_IN;

   fflush_rewind(fp);

   if (n_go_may_yield_control() && (cp = ok_vlook(crt)) != NULL) {
      size_t rows;

      if(*cp == '\0')
         rows = (size_t)n_scrnheight;
      else
         su_idec_uz_cp(&rows, cp, 0, NULL);

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
   n_NYD_OU;
}

FL enum protocol
which_protocol(char const *name, bool_t check_stat, bool_t try_hooks,
   char const **adjusted_or_null)
{
   /* TODO This which_protocol() sickness should be URL::new()->protocol() */
   char const *cp, *orig_name;
   enum protocol rv = PROTO_UNKNOWN;
   n_NYD_IN;

   if(name[0] == '%' && name[1] == ':')
      name += 2;
   orig_name = name;

   for (cp = name; *cp && *cp != ':'; cp++)
      if (!su_cs_is_alnum(*cp))
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

      np = n_lofi_alloc((sz = su_cs_len(name)) + 4 +1);
      su_mem_copy(np, name, sz + 1);

      if(!stat(name, &stb)){
         if(S_ISDIR(stb.st_mode)
#ifdef mx_HAVE_MAILDIR
               && (su_mem_copy(&np[sz], "/tmp", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
               (su_mem_copy(&np[sz], "/new", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
               (su_mem_copy(&np[sz], "/cur", 5),
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
            !su_cs_cmp_case(cp, "maildir")){
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
   n_NYD_OU;
   return rv;
}

FL char *
n_c_to_hex_base16(char store[3], char c){
   static char const itoa16[] = "0123456789ABCDEF";
   n_NYD2_IN;

   store[2] = '\0';
   store[1] = itoa16[(ui8_t)c & 0x0F];
   c = ((ui8_t)c >> 4) & 0x0F;
   store[0] = itoa16[(ui8_t)c];
   n_NYD2_OU;
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
   n_NYD2_IN;

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
   n_NYD2_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

#if 0
FL enum n_icalc_state
n_icalc_buf(si64_t *resp, char const *cbuf, uiz_t clen, enum n_icalc_mode icm,
      char const **endptr_or_null){
   si64_t res;
   struct a_group{
      struct a_group *outer;
      char unary;
      ui8_t pad[7];
   } *tail;
   void *lofi_snap;
   enum su_idec_state rv;
   n_NYD_IN;

   if(clen == UIZ_MAX)
      clen = su_cs_len(cbuf);

   icm &= n__ICALC_MODE_MASK;
   rv = n_ICALC_STATE_NONE | icm;
   lofi_snap = NULL;
   tail = NULL;
   res = 0;

   while(clen > 0){
      char c, unary;

      if((c = *cbuf) == '-' || c == '+' || c == '~'){
         unary = c;
      }

      if(*cbuf == '('){
         if(lofi_snap == NULL)
            lofi_snap = n_lofi_snap_create();


      }

   }


jleave:
   if(lofi_snap != NULL)
      n_lofi_snap_unroll(lofi_snap);
   *resp = res;
   if(endptr_or_null != NULL)
      *endptr_or_null = cbuf;
   if(clen == 0)
      rv |= n_ICALC_STATE_CONSUMED;
   n_NYD_OU;
   return rv;

jeinval:
   rv |= su_IDEC_STATE_EINVAL;
   goto j_maxval;
jeover:
   rv |= su_IDEC_STATE_EOVERFLOW;
j_maxval:
   res = (rv & su_IDEC_STATE_SEEN_MINUS) ? SI64_MIN : SI64_MAX;
   rv &= ~su_IDEC_STATE_SEEN_MINUS;
   goto jleave;
}
#endif

FL char const *
n_getdeadletter(void){
   char const *cp;
   bool_t bla;
   n_NYD_IN;

   bla = FAL0;
jredo:
   cp = fexpand(ok_vlook(DEAD), FEXP_LOCAL | FEXP_NSHELL);
   if(cp == NULL || su_cs_len(cp) >= PATH_MAX){
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
   n_NYD_OU;
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
   n_NYD2_IN;

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
      su_mem_set(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_CANONNAME;
      if(getaddrinfo(hn, NULL, &hints, &res) == 0){
         if(res->ai_canonname != NULL){
            size_t l;

            l = su_cs_len(res->ai_canonname) +1;
            hn = n_lofi_alloc(l);
            lofi = TRU1;
            su_mem_copy(hn, res->ai_canonname, l);
         }
         freeaddrinfo(res);
      }
# else
      hent = gethostbyname(hn);
      if(hent != NULL)
         hn = hent->h_name;
# endif
#endif /* mx_HAVE_SOCKETS */

      /* Ensure it is non-empty! */
      if(hn[0] == '\0')
         hn = n_UNCONST(n_LOCALHOST_DEFAULT_NAME);

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
      sys_hostname = su_cs_dup(hn, 0);
#endif

      if(lofi)
         n_lofi_free(hn);
      hn = sys_hostname;
   }

   if(hostname != NULL && hostname != sys_hostname)
      n_free(hostname);
   hostname = su_cs_dup(hn, 0);
   n_NYD2_OU;
   return hostname;
}

#ifdef mx_HAVE_IDNA
FL bool_t
n_idna_to_ascii(struct n_string *out, char const *ibuf, size_t ilen){
   char *idna_utf8;
   bool_t lofi, rv;
   n_NYD_IN;

   if(ilen == UIZ_MAX)
      ilen = su_cs_len(ibuf);

   lofi = FAL0;

   if((rv = (ilen == 0)))
      goto jleave;
   if(ibuf[ilen] != '\0'){
      lofi = TRU1;
      idna_utf8 = n_lofi_alloc(ilen +1);
      su_mem_copy(idna_utf8, ibuf, ilen);
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
   ilen = su_cs_len(idna_utf8);
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
      ilen = su_cs_len(out->s_dat);
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
   n_NYD_OU;
   return rv;
}
#endif /* mx_HAVE_IDNA */

FL char *
n_random_create_buf(char *dat, size_t len, ui32_t *reprocnt_or_null){
   struct str b64;
   char *indat, *cp, *oudat;
   size_t i, inlen, oulen;
   n_NYD_IN;

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
         n_err(_("P(seudo)R(andom)N(umber)G(enerator): %s\n"), prngn);
      }

#ifdef a_AUX_RAND_USE_BUILTIN
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
   su_mem_copy(dat, b64.s, len);
   dat[len] = '\0';
   if(oudat != dat)
      n_lofi_free(oudat);

   n_lofi_free(indat);

   n_NYD_OU;
   return dat;
}

FL char *
n_random_create_cp(size_t len, ui32_t *reprocnt_or_null){
   char *dat;
   n_NYD_IN;

   dat = n_autorec_alloc(len +1);
   dat = n_random_create_buf(dat, len, reprocnt_or_null);
   n_NYD_OU;
   return dat;
}

FL bool_t
n_boolify(char const *inbuf, uiz_t inlen, bool_t emptyrv){
   bool_t rv;
   n_NYD2_IN;
   assert(inlen == 0 || inbuf != NULL);

   if(inlen == UIZ_MAX)
      inlen = su_cs_len(inbuf);

   if(inlen == 0)
      rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
   else{
      if((inlen == 1 && (*inbuf == '1' || *inbuf == 'y' || *inbuf == 'Y')) ||
            !su_cs_cmp_case_n(inbuf, "true", inlen) ||
            !su_cs_cmp_case_n(inbuf, "yes", inlen) ||
            !su_cs_cmp_case_n(inbuf, "on", inlen))
         rv = TRU1;
      else if((inlen == 1 &&
               (*inbuf == '0' || *inbuf == 'n' || *inbuf == 'N')) ||
            !su_cs_cmp_case_n(inbuf, "false", inlen) ||
            !su_cs_cmp_case_n(inbuf, "no", inlen) ||
            !su_cs_cmp_case_n(inbuf, "off", inlen))
         rv = FAL0;
      else{
         ui64_t ib;

         if((su_idec(&ib, inbuf, inlen, 0, 0, NULL) & (su_IDEC_STATE_EMASK |
               su_IDEC_STATE_CONSUMED)) != su_IDEC_STATE_CONSUMED)
            rv = TRUM1;
         else
            rv = (ib != 0);
      }
   }
   n_NYD2_OU;
   return rv;
}

FL bool_t
n_quadify(char const *inbuf, uiz_t inlen, char const *prompt, bool_t emptyrv){
   bool_t rv;
   n_NYD2_IN;
   assert(inlen == 0 || inbuf != NULL);

   if(inlen == UIZ_MAX)
      inlen = su_cs_len(inbuf);

   if(inlen == 0)
      rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
   else if((rv = n_boolify(inbuf, inlen, emptyrv)) < FAL0 &&
         !su_cs_cmp_case_n(inbuf, "ask-", 4) &&
         (rv = n_boolify(&inbuf[4], inlen - 4, emptyrv)) >= FAL0 &&
         (n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT))
      rv = getapproval(prompt, rv);
   n_NYD2_OU;
   return rv;
}

FL bool_t
n_is_all_or_aster(char const *name){
   bool_t rv;
   n_NYD2_IN;

   rv = ((name[0] == '*' && name[1] == '\0') || !su_cs_cmp_case(name, "all"));
   n_NYD2_OU;
   return rv;
}

FL struct n_timespec const *
n_time_now(bool_t force_update){ /* TODO event loop update IF cmd requests! */
   static struct n_timespec ts_now;
   n_NYD2_IN;

   if(n_UNLIKELY(su_state_has(su_STATE_REPRODUCIBLE))){
      /* Guaranteed 32-bit posnum TODO SOURCE_DATE_EPOCH should be 64-bit! */
      (void)su_idec_s64_cp(&ts_now.ts_sec, ok_vlook(SOURCE_DATE_EPOCH),
         0,NULL);
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
   n_NYD2_OU;
   return &ts_now;
}

FL void
time_current_update(struct time_current *tc, bool_t full_update){
   n_NYD_IN;
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
      su_mem_copy(&tc->tc_gm, tmp, sizeof tc->tc_gm);
      if((tmp = localtime(&t)) == NULL){
         t = 0;
         goto jredo;
      }
      su_mem_copy(&tc->tc_local, tmp, sizeof tc->tc_local);
      cp = su_cs_pcopy(tc->tc_ctime, n_time_ctime((si64_t)tc->tc_time, tmp));
      *cp++ = '\n';
      *cp = '\0';
      assert(PTR2SIZE(++cp - tc->tc_ctime) < sizeof(tc->tc_ctime));
   }
   n_NYD_OU;
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
   n_NYD_IN;

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
   n_NYD_OU;
   return buf;
}

FL uiz_t
n_msleep(uiz_t millis, bool_t ignint){
   uiz_t rv;
   n_NYD2_IN;

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

   n_NYD2_OU;
   return rv;
}

FL void
n_err(char const *format, ...){
   va_list ap;
   n_NYD2_IN;

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

      if((len = su_cs_len(format)) > 0){
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
   n_NYD2_OU;
}

FL void
n_verr(char const *format, va_list ap){
#ifdef mx_HAVE_ERRORS
   struct a_aux_err_node *enp;
#endif
   bool_t doname;
   size_t len;
   n_NYD2_IN;

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

   if((len = su_cs_len(format)) == 0)
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
         ++n_pstate_err_cnt;
      }else if(doname ||
            (enp->ae_str.s_len > 0 &&
             enp->ae_str.s_dat[enp->ae_str.s_len - 1] == '\n')){
         if(n_pstate_err_cnt < ERRORS_MAX)
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
            i = (int)su_cs_len(&enp->ae_str.s_dat[len]);
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
   n_NYD2_OU;
}

FL void
n_err_sighdl(char const *format, ...){ /* TODO sigsafe; obsolete! */
   va_list ap;
   n_NYD_X;

   va_start(ap, format);
   vfprintf(n_stderr, format, ap);
   va_end(ap);
   fflush(n_stderr);
}

FL void
n_perr(char const *msg, int errval){
   int e;
   char const *fmt;
   n_NYD2_IN;

   if(msg == NULL){
      fmt = "%s%s\n";
      msg = n_empty;
   }else
      fmt = "%s: %s\n";

   e = (errval == 0) ? su_err_no() : errval;
   n_err(fmt, msg, su_err_doc(e));
   if(errval == 0)
      su_err_set_no(e);
   n_NYD2_OU;
}

FL void
n_alert(char const *format, ...){
   va_list ap;
   n_NYD2_IN;

   n_err(a_aux_err_linelen > 0 ? _("\nAlert: ") : _("Alert: "));

   va_start(ap, format);
   n_verr(format, ap);
   va_end(ap);

   n_err("\n");
   n_NYD2_OU;
}

FL void
n_panic(char const *format, ...){
   va_list ap;
   n_NYD2_IN;

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
   n_NYD2_OU;
   abort(); /* Was exit(n_EXIT_ERR); for a while, but no */
}

#ifdef mx_HAVE_ERRORS
FL int
c_errors(void *v){
   char **argv = v;
   struct a_aux_err_node *enp;
   n_NYD_IN;

   if(*argv == NULL)
      goto jlist;
   if(argv[1] != NULL)
      goto jerr;
   if(!su_cs_cmp_case(*argv, "show"))
      goto jlist;
   if(!su_cs_cmp_case(*argv, "clear"))
      goto jclear;
jerr:
   fprintf(n_stderr,
      _("Synopsis: errors: (<show> or) <clear> the error ring\n"));
   v = NULL;
jleave:
   n_NYD_OU;
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
   n_pstate_err_cnt = 0;
   a_aux_err_linelen = 0;
   while((enp = a_aux_err_head) != NULL){
      a_aux_err_head = enp->ae_next;
      n_string_gut(&enp->ae_str);
      n_free(enp);
   }
   goto jleave;
}
#endif /* mx_HAVE_ERRORS */

#ifdef mx_HAVE_REGEX
FL char const *
n_regex_err_to_doc(const regex_t *rep, int e){
   char *cp;
   size_t i;
   n_NYD2_IN;

   i = regerror(e, rep, NULL, 0) +1;
   cp = n_autorec_alloc(i);
   regerror(e, rep, cp, i);
   n_NYD2_OU;
   return cp;
}
#endif

FL su_boole
mx_show_sorted_dict(char const *cmdname, void *vdp,
      su_boole (*ptf)(FILE *fp, char const *key, void const *dat),
      FILE **fpp_or_nil){
   struct su_cs_dict_view dv;
   char const **cpp, **xcpp;
   struct su_cs_dict *dp;
   su_u32 lines, cnt;
   FILE *fp;
   su_boole rv;
   n_NYD_IN;

   rv = TRU1;

   /* Continuation request? */
   if(fpp_or_nil != su_NIL){
      fp = *fpp_or_nil;
      /* Final dump? */
      if(cmdname == su_NIL){
         lines = 0;
         goto jdump;
      }
   }else
      fp = n_stdout;

   dp = su_S(struct su_cs_dict*,vdp);

   if(dp == su_NIL || (cnt = su_cs_dict_count(dp)) == 0){
      fprintf(fp, _("# no %s registered\n"), cmdname);
      rv = (ferror(fp) == 0);
      goto jleave;
   }

   su_cs_dict_statistics(dp);

   /* TODO we need LOFI/AUTOREC TALLOC version which check overflow!!
    * TODO these then could _really_ return NIL... */
   if(su_U32_MAX / sizeof(*cpp) <= cnt ||
         (cpp = su_S(char const**,n_autorec_alloc(sizeof(*cpp) * cnt))
            ) == su_NIL){
      rv = FAL0;
      goto jleave;
   }

   xcpp = cpp;
   su_CS_DICT_FOREACH(dp, &dv)
      *xcpp++ = su_cs_dict_view_key(&dv);
   if(cnt > 1)
      qsort(cpp, cnt, sizeof *cpp, &a_aux_qsort_cpp);

   if(fpp_or_nil == su_NIL || *fpp_or_nil == su_NIL){
      if((fp = Ftmp(NULL, cmdname, OF_RDWR | OF_UNLINK | OF_REGISTER)
            ) == su_NIL)
         fp = n_stdout;
      if(fpp_or_nil != su_NIL)
         *fpp_or_nil = fp;
   }

   /* Create visual result */
   lines = 0;

   for(xcpp = cpp; cnt > 0; ++lines, ++xcpp, --cnt)
      if(!(rv = (*ptf)(fp, *xcpp, su_cs_dict_lookup(dp, *xcpp))))
         break;

jdump:
   if(fp != n_stdout && (fpp_or_nil == su_NIL || cmdname == su_NIL)){
      page_or_print(fp, lines);
      Fclose(fp);
   }

jleave:
   n_NYD_OU;
   return rv;
}

/* s-it-mode */
