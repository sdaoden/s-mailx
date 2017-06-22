/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that don't fit anywhere else.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE auxlily

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/utsname.h>

#ifdef HAVE_GETRANDOM
# include HAVE_GETRANDOM_HEADER
#endif

#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
#  include <sys/socket.h>
# endif

# include <netdb.h>
#endif

#ifndef HAVE_POSIX_RANDOM
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

#ifdef HAVE_ERRORS
struct a_aux_err_node{
   struct a_aux_err_node *ae_next;
   struct n_string ae_str;
};
#endif

struct a_aux_err_map{
   ui32_t aem_hash;     /* Hash of name */
   ui32_t aem_nameoff;  /* Into a_aux_err_names[] */
   ui32_t aem_docoff;   /* Into a_aux_err docs[] */
   si32_t aem_err_no;   /* The OS error value for this one */
};

static ui8_t a_aux_idec_atoi[256] = {
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

#define a_X(X) ((ui64_t)-1 / (X))
static ui64_t const a_aux_idec_cutlimit[35] = {
   a_X( 2), a_X( 3), a_X( 4), a_X( 5), a_X( 6), a_X( 7), a_X( 8),
   a_X( 9), a_X(10), a_X(11), a_X(12), a_X(13), a_X(14), a_X(15),
   a_X(16), a_X(17), a_X(18), a_X(19), a_X(20), a_X(21), a_X(22),
   a_X(23), a_X(24), a_X(25), a_X(26), a_X(27), a_X(28), a_X(29),
   a_X(30), a_X(31), a_X(32), a_X(33), a_X(34), a_X(35), a_X(36)
};
#undef a_X

/* Include the constant make-errors.sh output */
#include "gen-errors.h"

/* And these things come from mk-config.h (config-time make-errors.sh output) */
static n__ERR_NUMBER_TYPE const a_aux_err_no2mapoff[][2] = {
#undef a_X
#define a_X(N,I) {N,I},
n__ERR_NUMBER_TO_MAPOFF
#undef a_X
};

#ifndef HAVE_POSIX_RANDOM
static union rand_state *a_aux_rand;
#endif

/* Error ring, for `errors' */
#ifdef HAVE_ERRORS
static struct a_aux_err_node *a_aux_err_head, *a_aux_err_tail;
static size_t a_aux_err_cnt, a_aux_err_cnt_noted;
#endif
static size_t a_aux_err_linelen;

/* Our ARC4 random generator with its completely unacademical pseudo
 * initialization (shall /dev/urandom fail) */
#ifndef HAVE_POSIX_RANDOM
static void a_aux_rand_init(void);
SINLINE ui8_t a_aux_rand_get8(void);
# ifndef HAVE_GETRANDOM
static ui32_t a_aux_rand_weak(ui32_t seed);
# endif
#endif

/* Find the descriptive mapping of an error number, or _ERR_INVAL */
static struct a_aux_err_map const *a_aux_err_map_from_no(si32_t eno);

#ifndef HAVE_POSIX_RANDOM
static void
a_aux_rand_init(void){
# ifndef HAVE_GETRANDOM
#  ifdef HAVE_CLOCK_GETTIME
   struct timespec ts;
#  else
   struct timeval ts;
#  endif
   union {int fd; size_t i;} u;
   ui32_t seed, rnd;
# endif
   NYD2_ENTER;

   a_aux_rand = n_alloc(sizeof *a_aux_rand);

# ifdef HAVE_GETRANDOM
   /* getrandom(2) guarantees 256 without n_ERR_INTR.. */
   n_LCTA(sizeof(a_aux_rand->a._dat) <= 256,
      "Buffer too large to be served without n_ERR_INTR error");
   n_LCTA(sizeof(a_aux_rand->a._dat) >= 256,
      "Buffer too small to serve used array indices");
   for(;;){
      ssize_t gr;

      gr = HAVE_GETRANDOM(a_aux_rand->a._dat, sizeof a_aux_rand->a._dat);
      a_aux_rand->a._i = a_aux_rand->a._dat[a_aux_rand->a._dat[1] ^
            a_aux_rand->a._dat[84]];
      a_aux_rand->a._j = a_aux_rand->a._dat[a_aux_rand->a._dat[65] ^
            a_aux_rand->a._dat[42]];
      /* ..but be on the safe side */
      if(UICMP(z, gr, ==, sizeof(a_aux_rand->a._dat)))
         break;
      n_msleep(250, FAL0);
   }

# else
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

   for(seed = (uintptr_t)a_aux_rand & UI32_MAX, rnd = 21; rnd != 0; --rnd){
      for(u.i = n_NELEM(a_aux_rand->b32); u.i-- != 0;){
         ui32_t t, k;

#  ifdef HAVE_CLOCK_GETTIME
         clock_gettime(CLOCK_REALTIME, &ts);
         t = (ui32_t)ts.tv_nsec;
#  else
         gettimeofday(&ts, NULL);
         t = (ui32_t)ts.tv_usec;
#  endif
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
# endif /* !HAVE_GETRANDOM */
   NYD2_LEAVE;
}

SINLINE ui8_t
a_aux_rand_get8(void){
   ui8_t si, sj;

   si = a_aux_rand->a._dat[++a_aux_rand->a._i];
   sj = a_aux_rand->a._dat[a_aux_rand->a._j += si];
   a_aux_rand->a._dat[a_aux_rand->a._i] = sj;
   a_aux_rand->a._dat[a_aux_rand->a._j] = si;
   return a_aux_rand->a._dat[(ui8_t)(si + sj)];
}

# ifndef HAVE_GETRANDOM
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
# endif /* HAVE_GETRANDOM */
#endif /* !HAVE_POSIX_RANDOM */

static struct a_aux_err_map const *
a_aux_err_map_from_no(si32_t eno){
   si32_t ecmp;
   size_t asz;
   n__ERR_NUMBER_TYPE const (*adat)[2], (*tmp)[2];
   struct a_aux_err_map const *aemp;
   NYD2_ENTER;

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
   NYD2_LEAVE;
   return aemp;
}

FL size_t
n_screensize(void){
   char const *cp;
   uiz_t rv;
   NYD2_ENTER;

   if((cp = ok_vlook(screen)) != NULL){
      n_idec_uiz_cp(&rv, cp, 0, NULL);
      if(rv == 0)
         rv = n_scrnheight;
   }else
      rv = n_scrnheight;

   if(rv > 2)
      rv -= 2;
   NYD2_LEAVE;
   return rv;
}

FL char const *
n_pager_get(char const **env_addon){
   char const *rv;
   NYD_ENTER;

   rv = ok_vlook(PAGER);

   if(env_addon != NULL){
      *env_addon = NULL;
      /* Update the manual upon any changes:
       *    *colour-pager*, $PAGER */
      if(strstr(rv, "less") != NULL){
         if(getenv("LESS") == NULL)
            *env_addon =
#ifdef HAVE_TERMCAP
                  (n_psonce & n_PSO_TERMCAP_CA_MODE) ? "LESS=Ri"
                     : !(n_psonce & n_PSO_TERMCAP_DISABLE) ? "LESS=FRi" :
#endif
                        "LESS=FRXi";
      }else if(strstr(rv, "lv") != NULL){
         if(getenv("LV") == NULL)
            *env_addon = "LV=-c";
      }
   }
   NYD_LEAVE;
   return rv;
}

FL void
page_or_print(FILE *fp, size_t lines)
{
   int c;
   char const *cp;
   NYD_ENTER;

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
   NYD_LEAVE;
}

FL enum protocol
which_protocol(char const *name, bool_t check_stat, bool_t try_hooks,
   char const **adjusted_or_null)
{
   /* TODO This which_protocol() sickness should be URL::new()->protocol() */
   char const *cp, *orig_name;
   enum protocol rv = PROTO_UNKNOWN;
   NYD_ENTER;

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
      else if(!strncmp(name, "maildir", sizeof("maildir") -1))
         rv = PROTO_MAILDIR;
      else if(!strncmp(name, "pop3", sizeof("pop3") -1)){
#ifdef HAVE_POP3
         rv = PROTO_POP3;
#else
         n_err(_("No POP3 support compiled in\n"));
#endif
      }else if(!strncmp(name, "pop3s", sizeof("pop3s") -1)){
#if defined HAVE_POP3 && defined HAVE_SSL
         rv = PROTO_POP3;
#else
         n_err(_("No POP3S support compiled in\n"));
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
         if(S_ISDIR(stb.st_mode) &&
               (memcpy(&np[sz], "/tmp", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
               (memcpy(&np[sz], "/new", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode)) &&
               (memcpy(&np[sz], "/cur", 5),
                  !stat(np, &stb) && S_ISDIR(stb.st_mode)))
            rv = PROTO_MAILDIR;
      }else if(try_hooks && n_filetype_trial(&ft, name))
         orig_name = savecatsep(name, '.', ft.ft_ext_dat);
      else if((cp = ok_vlook(newfolders)) != NULL &&
            !asccasecmp(cp, "maildir"))
         rv = PROTO_MAILDIR;

      n_lofi_free(np);
   }
jleave:
   if(adjusted_or_null != NULL)
      *adjusted_or_null = orig_name;
   NYD_LEAVE;
   return rv;
}

FL char *
n_c_to_hex_base16(char store[3], char c){
   static char const itoa16[] = "0123456789ABCDEF";
   NYD2_ENTER;

   store[2] = '\0';
   store[1] = itoa16[(ui8_t)c & 0x0F];
   c = ((ui8_t)c >> 4) & 0x0F;
   store[0] = itoa16[(ui8_t)c];
   NYD2_LEAVE;
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
   NYD2_ENTER;

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
   NYD2_LEAVE;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

FL enum n_idec_state
n_idec_buf(void *resp, char const *cbuf, uiz_t clen, ui8_t base,
      enum n_idec_mode idm, char const **endptr_or_null){
   /* XXX Brute simple and */
   ui8_t currc;
   ui64_t res, cut;
   enum n_idec_state rv;
   NYD_ENTER;

   idm &= n__IDEC_MODE_MASK;
   rv = n_IDEC_STATE_NONE | idm;
   res = 0;

   if(clen == UIZ_MAX){
      if(*cbuf == '\0')
         goto jeinval;
   }else if(clen == 0)
      goto jeinval;

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
      if(base == 0)
         base = 10;
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
               /* Char after prefix must be valid */
jprefix_skip:
               if(*++cbuf == '\0' || --clen == 0)
                  goto jeinval;

               /* Character must be valid for base, invalid otherwise */
               currc = a_aux_idec_atoi[(ui8_t)*cbuf];
               if(currc >= base)
                  goto jeinval;
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
      if(rv & n_IDEC_MODE_SIGNED_TYPE)
         uimask >>= 1;

      if(res & ~uimask){
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
   NYD_LEAVE;
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

FL ui32_t
n_torek_hash(char const *name){
   /* Chris Torek's hash */
   char c;
   ui32_t h;
   NYD2_ENTER;

   for(h = 0; (c = *name++) != '\0';)
      h = (h * 33) + c;
   NYD2_LEAVE;
   return h;
}

FL ui32_t
n_torek_ihashn(char const *dat, size_t len){
   /* See n_torek_hash() */
   char c;
   ui32_t h;
   NYD2_ENTER;

   if(len == UIZ_MAX)
      for(h = 0; (c = *dat++) != '\0';)
         h = (h * 33) + lowerconv(c);
   else
      for(h = 0; len > 0; --len){
         c = *dat++;
         h = (h * 33) + lowerconv(c);
      }
   NYD2_LEAVE;
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
   NYD2_ENTER;

   i = (n < primes[n_NELEM(primes) / 4] ? 0
         : (n < primes[n_NELEM(primes) / 2] ? n_NELEM(primes) / 4
         : n_NELEM(primes) / 2));

   do if((mprime = primes[i]) > n)
      break;
   while(++i < n_NELEM(primes));

   if(i == n_NELEM(primes) && mprime < n)
      mprime = n;
   NYD2_LEAVE;
   return mprime;
}

FL char const *
n_getdeadletter(void){
   char const *cp_base, *cp;
   NYD_ENTER;

   cp_base = NULL;
jredo:
   cp = fexpand(ok_vlook(DEAD), FEXP_LOCAL | FEXP_NSHELL);
   if(cp == NULL || strlen(cp) >= PATH_MAX){
      if(cp_base == NULL){
         n_err(_("Failed to expand *DEAD*, setting default (%s): %s\n"),
            VAL_DEAD, n_shexp_quote_cp(cp, FAL0));
         ok_vclear(DEAD);
         goto jredo;
      }else{
         cp = savecatsep(ok_vlook(TMPDIR), '/', VAL_DEAD_BASENAME);
         n_err(_("Cannot expand *DEAD*, using: %s\n"), cp);
      }
   }
   NYD_LEAVE;
   return cp;
}

FL char *
n_nodename(bool_t mayoverride){
   static char *sys_hostname, *hostname; /* XXX free-at-exit */

   struct utsname ut;
   char *hn;
#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
   struct addrinfo hints, *res;
# else
   struct hostent *hent;
# endif
#endif
   NYD2_ENTER;

   if(mayoverride && (hn = ok_vlook(hostname)) != NULL && *hn != '\0'){
      ;
   }else if((hn = sys_hostname) == NULL){
      uname(&ut);
      hn = ut.nodename;
#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_CANONNAME;
      if(getaddrinfo(hn, NULL, &hints, &res) == 0){
         if(res->ai_canonname != NULL){
            size_t l;

            l = strlen(res->ai_canonname) +1;
            hn = n_lofi_alloc(l);
            memcpy(hn, res->ai_canonname, l);
         }
         freeaddrinfo(res);
      }
# else
      hent = gethostbyname(hn);
      if(hent != NULL)
         hn = hent->h_name;
# endif
#endif
      sys_hostname = sstrdup(hn);
#if defined HAVE_SOCKETS && defined HAVE_GETADDRINFO
      if(hn != ut.nodename)
         n_lofi_free(hn);
#endif
      hn = sys_hostname;
   }

   if(hostname != NULL && hostname != sys_hostname)
      n_free(hostname);
   hostname = sstrdup(hn);
   NYD2_LEAVE;
   return hostname;
}

FL char *
n_random_create_cp(size_t length, ui32_t *reprocnt_or_null){
   struct str b64;
   char *data, *cp;
   size_t i;
   NYD_ENTER;

#ifndef HAVE_POSIX_RANDOM
   if(a_aux_rand == NULL)
      a_aux_rand_init();
#endif

   /* We use our base64 encoder with _NOPAD set, so ensure the encoded result
    * with PAD stripped is still longer than what the user requests, easy way */
   data = n_lofi_alloc(i = length + 3);

   if(!(n_psonce & n_PSO_REPRODUCIBLE) || reprocnt_or_null == NULL){
#ifndef HAVE_POSIX_RANDOM
      while(i-- > 0)
         data[i] = (char)a_aux_rand_get8();
#else
      for(cp = data; i > 0;){
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
      for(cp = data; i > 0;){
         union {ui32_t i4; char c[4];} r;
         size_t j;

         r.i4 = ++*reprocnt_or_null;
         if(n_psonce & n_PSO_BIG_ENDIAN){ /* TODO BSWAP */
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

   assert(length + 3 < UIZ_MAX / 4);
   b64_encode_buf(&b64, data, length + 3,
      B64_SALLOC | B64_RFC4648URL | B64_NOPAD);
   n_lofi_free(data);

   assert(b64.l >= length);
   b64.s[length] = '\0';
   NYD_LEAVE;
   return b64.s;
}

FL si8_t
boolify(char const *inbuf, uiz_t inlen, si8_t emptyrv)
{
   si8_t rv;
   NYD_ENTER;

   assert(inlen == 0 || inbuf != NULL);

   if (inlen == UIZ_MAX)
      inlen = strlen(inbuf);

   if (inlen == 0)
      rv = (emptyrv >= 0) ? (emptyrv == 0 ? 0 : 1) : -1;
   else {
      if ((inlen == 1 && (*inbuf == '1' || *inbuf == 'y' || *inbuf == 'Y')) ||
            !ascncasecmp(inbuf, "true", inlen) ||
            !ascncasecmp(inbuf, "yes", inlen) ||
            !ascncasecmp(inbuf, "on", inlen))
         rv = 1;
      else if ((inlen == 1 &&
               (*inbuf == '0' || *inbuf == 'n' || *inbuf == 'N')) ||
            !ascncasecmp(inbuf, "false", inlen) ||
            !ascncasecmp(inbuf, "no", inlen) ||
            !ascncasecmp(inbuf, "off", inlen))
         rv = 0;
      else {
         ui64_t ib;

         if((n_idec_buf(&ib, inbuf, inlen, 0, 0, NULL
                  ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
               ) != n_IDEC_STATE_CONSUMED)
            rv = -1;
         else
            rv = (ib != 0);
      }
   }
   NYD_LEAVE;
   return rv;
}

FL si8_t
quadify(char const *inbuf, uiz_t inlen, char const *prompt, si8_t emptyrv)
{
   si8_t rv;
   NYD_ENTER;

   assert(inlen == 0 || inbuf != NULL);

   if (inlen == UIZ_MAX)
      inlen = strlen(inbuf);

   if (inlen == 0)
      rv = (emptyrv >= 0) ? (emptyrv == 0 ? 0 : 1) : -1;
   else if ((rv = boolify(inbuf, inlen, -1)) < 0 &&
         !ascncasecmp(inbuf, "ask-", 4) &&
         (rv = boolify(inbuf + 4, inlen - 4, -1)) >= 0 &&
         (n_psonce & n_PSO_INTERACTIVE))
      rv = getapproval(prompt, rv);
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_is_all_or_aster(char const *name){
   bool_t rv;
   NYD_ENTER;

   rv = ((name[0] == '*' && name[1] == '\0') || !asccasecmp(name, "all"));
   NYD_LEAVE;
   return rv;
}

FL struct n_timespec const *
n_time_now(bool_t force_update){ /* TODO event loop update IF cmd requests! */
   static struct n_timespec ts_now;
   NYD2_ENTER;

   if(n_psonce & n_PSO_REPRODUCIBLE){
      (void)n_idec_ui64_cp(&ts_now.ts_sec, ok_vlook(SOURCE_DATE_EPOCH), 0,NULL);
      ts_now.ts_nsec = 0;
   }else if(force_update || ts_now.ts_sec == 0){
#ifdef HAVE_CLOCK_GETTIME
      struct timespec ts;

      clock_gettime(CLOCK_REALTIME, &ts);
      ts_now.ts_sec = (si64_t)ts.tv_sec;
      ts_now.ts_nsec = (siz_t)ts.tv_nsec;
#elif defined HAVE_GETTIMEOFDAY
      struct timeval tv;

      gettimeofday(&tv, NULL);
      ts_now.ts_sec = (si64_t)tv.tv_sec;
      ts_now.ts_nsec = (siz_t)tv.tv_usec * 1000;
#else
      ts_now.ts_sec = (si64_t)time(NULL);
      ts_now.ts_nsec = 0;
#endif
   }
   NYD2_LEAVE;
   return &ts_now;
}

FL void
time_current_update(struct time_current *tc, bool_t full_update)
{
   NYD_ENTER;
   tc->tc_time = (time_t)n_time_now(TRU1)->ts_sec;
   if (full_update) {
      memcpy(&tc->tc_gm, gmtime(&tc->tc_time), sizeof tc->tc_gm);
      memcpy(&tc->tc_local, localtime(&tc->tc_time), sizeof tc->tc_local);
      sstpcpy(tc->tc_ctime, ctime(&tc->tc_time));
   }
   NYD_LEAVE;
}

FL uiz_t
n_msleep(uiz_t millis, bool_t ignint){
   uiz_t rv;
   NYD2_ENTER;

#ifdef HAVE_NANOSLEEP
   /* C99 */{
      struct timespec ts, trem;
      int i;

      ts.tv_sec = millis / 1000;
      ts.tv_nsec = (millis %= 1000) * 1000 * 1000;

      while((i = nanosleep(&ts, &trem)) != 0 && ignint)
         ts = trem;
      rv = (i == 0) ? 0 : (trem.tv_sec * 1000) + (trem.tv_nsec / (1000 * 1000));
   }

#elif defined HAVE_SLEEP
   if((millis /= 1000) == 0)
      millis = 1;
   while((rv = sleep((unsigned int)millis)) != 0 && ignint)
      millis = rv;
#else
# error Configuration should have detected a function for sleeping.
#endif

   NYD2_LEAVE;
   return rv;
}

FL void
n_err(char const *format, ...){
   va_list ap;
   NYD2_ENTER;

   va_start(ap, format);
#ifdef HAVE_ERRORS
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
   NYD2_LEAVE;
}

FL void
n_verr(char const *format, va_list ap){
#ifdef HAVE_ERRORS
   struct a_aux_err_node *enp;
#endif
   bool_t doname;
   size_t len;
   NYD2_ENTER;

   doname = FAL0;

   while(*format == '\n'){
      putc('\n', n_stderr);
      doname = TRU1;
      ++format;
   }

   if(doname){
      a_aux_err_linelen = 0;
#ifdef HAVE_ERRORS
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
#ifdef HAVE_ERRORS
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

#ifdef HAVE_ERRORS
   if(!(n_psonce & n_PSO_INTERACTIVE))
#endif
      vfprintf(n_stderr, format, ap);
#ifdef HAVE_ERRORS
   else{
      int imax, i;
      n_LCTAV(ERRORS_MAX > 3);

      /* Link it into the `errors' message ring */
      if((enp = a_aux_err_tail) == NULL){
jcreat:
         enp = smalloc(sizeof *enp);
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

# ifdef HAVE_N_VA_COPY
      imax = 64;
# else
      imax = n_MIN(LINESIZE, 1024);
# endif
      for(i = imax;; imax = ++i /* xxx could wrap, maybe */){
# ifdef HAVE_N_VA_COPY
         va_list vac;

         n_va_copy(vac, ap);
# else
#  define vac ap
# endif

         n_string_resize(&enp->ae_str, (len = enp->ae_str.s_len) + (size_t)i);
         i = vsnprintf(&enp->ae_str.s_dat[len], (size_t)i, format, vac);
# ifdef HAVE_N_VA_COPY
         va_end(vac);
# else
#  undef vac
# endif
         if(i <= 0)
            goto jleave;
         if(UICMP(z, i, >=, imax)){
# ifdef HAVE_N_VA_COPY
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
#endif /* HAVE_ERRORS */

jleave:
   fflush(n_stderr);
   NYD2_LEAVE;
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
   NYD2_ENTER;

   if(msg == NULL){
      fmt = "%s%s\n";
      msg = n_empty;
   }else
      fmt = "%s: %s\n";

   e = (errval == 0) ? n_err_no : errval;
   n_err(fmt, msg, n_err_to_doc(e));
   if(errval == 0)
      n_err_no = e;
   NYD2_LEAVE;
}

FL void
n_alert(char const *format, ...){
   va_list ap;
   NYD2_ENTER;

   n_err(a_aux_err_linelen > 0 ? _("\nAlert: ") : _("Alert: "));

   va_start(ap, format);
   n_verr(format, ap);
   va_end(ap);

   n_err("\n");
   NYD2_LEAVE;
}

FL void
n_panic(char const *format, ...){
   va_list ap;
   NYD2_ENTER;

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
   NYD2_LEAVE;
   abort(); /* Was exit(n_EXIT_ERR); for a while, but no */
}

#ifdef HAVE_ERRORS
FL int
c_errors(void *v){
   char **argv = v;
   struct a_aux_err_node *enp;
   NYD_ENTER;

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
   NYD_LEAVE;
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
      free(enp);
   }
   goto jleave;
}
#endif /* HAVE_ERRORS */

FL char const *
n_err_to_doc(si32_t eno){
   char const *rv;
   struct a_aux_err_map const *aemp;
   NYD2_ENTER;

   aemp = a_aux_err_map_from_no(eno);
   rv = &a_aux_err_docs[aemp->aem_docoff];
   NYD2_LEAVE;
   return rv;
}

FL char const *
n_err_to_name(si32_t eno){
   char const *rv;
   struct a_aux_err_map const *aemp;
   NYD2_ENTER;

   aemp = a_aux_err_map_from_no(eno);
   rv = &a_aux_err_names[aemp->aem_nameoff];
   NYD2_LEAVE;
   return rv;
}

FL si32_t
n_err_from_name(char const *name){
   struct a_aux_err_map const *aemp;
   ui32_t hash, i, j, x;
   si32_t rv;
   NYD2_ENTER;

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
   NYD2_LEAVE;
   return rv;
}

#ifdef HAVE_REGEX
FL char const *
n_regex_err_to_doc(const regex_t *rep, int e){
   char *cp;
   size_t i;
   NYD2_ENTER;

   i = regerror(e, rep, NULL, 0) +1;
   cp = salloc(i);
   regerror(e, rep, cp, i);
   NYD2_LEAVE;
   return cp;
}
#endif

/* s-it-mode */
