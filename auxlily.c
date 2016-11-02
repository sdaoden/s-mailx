/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that don't fit anywhere else.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
#  include <sys/socket.h>
# endif

# include <netdb.h>
#endif

#ifndef HAVE_POSIX_RANDOM
union rand_state {
   struct rand_arc4 {
      ui8_t    __pad[6];
      ui8_t    _i;
      ui8_t    _j;
      ui8_t    _dat[256];
   }        a;
   ui8_t    b8[sizeof(struct rand_arc4)];
   ui32_t   b32[sizeof(struct rand_arc4) / sizeof(ui32_t)];
};
#endif

#ifdef HAVE_ERRORS
struct a_aux_err_node{
   struct a_aux_err_node *ae_next;
   struct n_string ae_str;
};
#endif

#ifndef HAVE_POSIX_RANDOM
static union rand_state *_rand;
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
static void    _rand_init(void);
static ui32_t  _rand_weak(ui32_t seed);
SINLINE ui8_t  _rand_get8(void);
#endif

#ifndef HAVE_POSIX_RANDOM
static void
_rand_init(void)
{
# ifdef HAVE_CLOCK_GETTIME
   struct timespec ts;
# else
   struct timeval ts;
# endif
   union {int fd; size_t i;} u;
   ui32_t seed, rnd;
   NYD2_ENTER;

   _rand = smalloc(sizeof *_rand);

   if ((u.fd = open("/dev/urandom", O_RDONLY)) != -1) {
      bool_t ok = (sizeof *_rand == (size_t)read(u.fd, _rand, sizeof *_rand));

      close(u.fd);
      if (ok)
         goto jleave;
   }

   for (seed = (uintptr_t)_rand & UI32_MAX, rnd = 21; rnd != 0; --rnd) {
      for (u.i = n_NELEM(_rand->b32); u.i-- != 0;) {
         ui32_t t, k;

# ifdef HAVE_CLOCK_GETTIME
         clock_gettime(CLOCK_REALTIME, &ts);
         t = (ui32_t)ts.tv_nsec;
# else
         gettimeofday(&ts, NULL);
         t = (ui32_t)ts.tv_usec;
# endif
         if (rnd & 1)
            t = (t >> 16) | (t << 16);
         _rand->b32[u.i] ^= _rand_weak(seed ^ t);
         _rand->b32[t % n_NELEM(_rand->b32)] ^= seed;
         if (rnd == 7 || rnd == 17)
            _rand->b32[u.i] ^= _rand_weak(seed ^ (ui32_t)ts.tv_sec);
         k = _rand->b32[u.i] % n_NELEM(_rand->b32);
         _rand->b32[k] ^= _rand->b32[u.i];
         seed ^= _rand_weak(_rand->b32[k]);
         if ((rnd & 3) == 3)
            seed ^= nextprime(seed);
      }
   }

   for (u.i = 5 * sizeof(_rand->b8); u.i != 0; --u.i)
      _rand_get8();
jleave:
   NYD2_LEAVE;
}

static ui32_t
_rand_weak(ui32_t seed)
{
   /* From "Random number generators: good ones are hard to find",
    * Park and Miller, Communications of the ACM, vol. 31, no. 10,
    * October 1988, p. 1195.
    * (In fact: FreeBSD 4.7, /usr/src/lib/libc/stdlib/random.c.) */
   ui32_t hi;

   if (seed == 0)
      seed = 123459876;
   hi =  seed /  127773;
         seed %= 127773;
   seed = (seed * 16807) - (hi * 2836);
   if ((si32_t)seed < 0)
      seed += SI32_MAX;
   return seed;
}

SINLINE ui8_t
_rand_get8(void)
{
   ui8_t si, sj;

   si = _rand->a._dat[++_rand->a._i];
   sj = _rand->a._dat[_rand->a._j += si];
   _rand->a._dat[_rand->a._i] = sj;
   _rand->a._dat[_rand->a._j] = si;
   return _rand->a._dat[(ui8_t)(si + sj)];
}
#endif /* HAVE_POSIX_RANDOM */

FL int
screensize(void){
   ul_i s;
   char *cp;
   NYD2_ENTER;

   if((cp = ok_vlook(screen)) == NULL || (s = strtoul(cp, NULL, 0)) == 0)
      s = (ul_i)scrnheight;
   s -= 2; /* XXX no magics */
   if(s > INT_MAX) /* TODO function should return unsigned */
      s = INT_MAX;
   NYD2_LEAVE;
   return (int)s;
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
                  (pstate & PS_TERMCAP_CA_MODE) ? "LESS=Ri"
                     : !(pstate & PS_TERMCAP_DISABLE) ? "LESS=FRi" :
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

   if (n_source_may_yield_control() && (cp = ok_vlook(crt)) != NULL) {
      size_t rows;

      rows = (*cp == '\0') ? (size_t)scrnheight : strtoul(cp, NULL, 0);

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
         run_command(pager, NULL, fileno(fp), COMMAND_FD_PASS, NULL,NULL,NULL,
            env_add);
         goto jleave;
      }
   }

   while ((c = getc(fp)) != EOF)
      putchar(c);
jleave:
   NYD_LEAVE;
}

FL enum protocol
which_protocol(char const *name) /* XXX (->URL (yet auxlily.c)) */
{
   struct stat st;
   char const *cp;
   char *np;
   size_t sz;
   enum protocol rv = PROTO_UNKNOWN;
   NYD_ENTER;

   temporary_protocol_ext = NULL;

   if (name[0] == '%' && name[1] == ':')
      name += 2;
   for (cp = name; *cp && *cp != ':'; cp++)
      if (!alnumchar(*cp))
         goto jfile;

   if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
      if (!strncmp(name, "pop3://", 7)) {
#ifdef HAVE_POP3
         rv = PROTO_POP3;
#else
         n_err(_("No POP3 support compiled in\n"));
#endif
      } else if (!strncmp(name, "pop3s://", 8)) {
#if defined HAVE_POP3 && defined HAVE_SSL
         rv = PROTO_POP3;
#else
# ifndef HAVE_POP3
         n_err(_("No POP3 support compiled in\n"));
# endif
# ifndef HAVE_SSL
         n_err(_("No SSL support compiled in\n"));
# endif
#endif
      }
      goto jleave;
   }

   /* TODO This is the de facto maildir code and thus belongs into there!
    * TODO and: we should have maildir:// and mbox:// pseudo-protos, instead of
    * TODO or (more likely) in addition to *newfolders*) */
jfile:
   rv = PROTO_FILE;
   np = ac_alloc((sz = strlen(name)) + 4 +1);
   memcpy(np, name, sz + 1);
   if (!stat(name, &st)) {
      if (S_ISDIR(st.st_mode) &&
            (memcpy(np+sz, "/tmp", 5), !stat(np, &st) && S_ISDIR(st.st_mode)) &&
            (memcpy(np+sz, "/new", 5), !stat(np, &st) && S_ISDIR(st.st_mode)) &&
            (memcpy(np+sz, "/cur", 5), !stat(np, &st) && S_ISDIR(st.st_mode)))
          rv = PROTO_MAILDIR;
   } else {
      if ((memcpy(np+sz, cp=".gz", 4), !stat(np, &st) && S_ISREG(st.st_mode)) ||
            (memcpy(np+sz, cp=".xz",4), !stat(np,&st) && S_ISREG(st.st_mode)) ||
            (memcpy(np+sz, cp=".bz2",5), !stat(np, &st) && S_ISREG(st.st_mode)))
         temporary_protocol_ext = cp;
      else if ((cp = ok_vlook(newfolders)) != NULL &&
            !asccasecmp(cp, "maildir"))
         rv = PROTO_MAILDIR;
   }
   ac_free(np);
jleave:
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

FL ui32_t
torek_hash(char const *name)
{
   /* Chris Torek's hash.
    * NOTE: need to change *at least* mk-okey-map.pl when changing the
    * algorithm!! */
   ui32_t h = 0;
   NYD_ENTER;

   while (*name != '\0') {
      h *= 33;
      h += *name++;
   }
   NYD_LEAVE;
   return h;
}

FL ui32_t
torek_ihashn(char const *dat, size_t len){
   /* See torek_hash() */
   char c;
   ui32_t h;
   NYD_ENTER;

   for(h = 0; len > 0 && (c = *dat++) != '\0'; --len)
      h = (h * 33) + lowerconv(c);
   NYD_LEAVE;
   return h;
}

FL ui32_t
nextprime(ui32_t n)
{
   static ui32_t const primes[] = {
      5, 11, 23, 47, 97, 157, 283,
      509, 1021, 2039, 4093, 8191, 16381, 32749, 65521,
      131071, 262139, 524287, 1048573, 2097143, 4194301,
      8388593, 16777213, 33554393, 67108859, 134217689,
      268435399, 536870909, 1073741789, 2147483647
   };

   ui32_t i, mprime;
   NYD_ENTER;

   i = (n < primes[n_NELEM(primes) / 4] ? 0
         : (n < primes[n_NELEM(primes) / 2] ? n_NELEM(primes) / 4
         : n_NELEM(primes) / 2));
   do
      if ((mprime = primes[i]) > n)
         break;
   while (++i < n_NELEM(primes));
   if (i == n_NELEM(primes) && mprime < n)
      mprime = n;
   NYD_LEAVE;
   return mprime;
}

FL char *
getprompt(void) /* TODO evaluate only as necessary (needs a bit) PART OF UI! */
{ /* FIXME getprompt must mb->wc->mb+reset seq! */
   static char buf[PROMPT_BUFFER_SIZE];

   char *cp;
   char const *ccp_base, *ccp;
   size_t n_NATCH_CHAR( cclen_base COMMA cclen COMMA ) maxlen, dfmaxlen;
   bool_t trigger; /* 1.: `errors' ring note?  2.: first loop tick done */
   NYD_ENTER;

   /* No other place to place this */
#ifdef HAVE_ERRORS
   if (options & OPT_INTERACTIVE) {
      if (!(pstate & PS_ERRORS_NOTED) && a_aux_err_head != NULL) {
         pstate |= PS_ERRORS_NOTED;
         fprintf(stderr, _("There are new messages in the error message ring "
               "(denoted by %s)\n"
            "  The `errors' command manages this message ring\n"),
            V_(n_error));
      }

      if ((trigger = (a_aux_err_cnt_noted != a_aux_err_cnt)))
         a_aux_err_cnt_noted = a_aux_err_cnt;
   } else
      trigger = FAL0;
#endif

   cp = buf;
   if ((ccp_base = ok_vlook(prompt)) == NULL || *ccp_base == '\0') {
#ifdef HAVE_ERRORS
      if (trigger)
         ccp_base = "";
      else
#endif
         goto jleave;
   }
#ifdef HAVE_ERRORS
   if (trigger)
      ccp_base = savecatsep(V_(n_error), '\0', ccp_base);
#endif
   n_NATCH_CHAR( cclen_base = strlen(ccp_base); )

   dfmaxlen = 0; /* keep CC happy */
   trigger = FAL0;
jredo:
   ccp = ccp_base;
   n_NATCH_CHAR( cclen = cclen_base; )
   maxlen = sizeof(buf) -1;

   for (;;) {
      size_t l;
      int c;

      if (maxlen == 0)
         goto jleave;
#ifdef HAVE_NATCH_CHAR
      c = mblen(ccp, cclen); /* TODO use mbrtowc() */
      if (c <= 0) {
         mblen(NULL, 0);
         if (c < 0) {
            *buf = '?';
            cp = buf + 1;
            goto jleave;
         }
         break;
      } else if ((l = c) > 1) {
         if (trigger) {
            memcpy(cp, ccp, l);
            cp += l;
         }
         ccp += l;
         maxlen -= l;
         continue;
      } else
#endif
      if ((c = n_shexp_expand_escape(&ccp, TRU1)) > 0) {
            if (trigger)
               *cp++ = (char)c;
            --maxlen;
            continue;
      }
      if (c == 0 || c == PROMPT_STOP)
         break;

      if (trigger) {
         char const *a = (c == PROMPT_DOLLAR) ? account_name : displayname;
         if (a == NULL)
            a = "";
         if ((l = field_put_bidi_clip(cp, dfmaxlen, a, strlen(a))) > 0) {
            cp += l;
            maxlen -= l;
            dfmaxlen -= l;
         }
      }
   }

   if (!trigger) {
      trigger = TRU1;
      dfmaxlen = maxlen;
      goto jredo;
   }
jleave:
   *cp = '\0';
   NYD_LEAVE;
   return buf;
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
nodename(int mayoverride)
{
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
   NYD_ENTER;

   if (mayoverride && (hn = ok_vlook(hostname)) != NULL && *hn != '\0') {
      ;
   } else if ((hn = sys_hostname) == NULL) {
      uname(&ut);
      hn = ut.nodename;
#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
      memset(&hints, 0, sizeof hints);
      hints.ai_family = AF_UNSPEC;
      hints.ai_flags = AI_CANONNAME;
      if (getaddrinfo(hn, NULL, &hints, &res) == 0) {
         if (res->ai_canonname != NULL) {
            size_t l = strlen(res->ai_canonname) +1;

            hn = ac_alloc(l);
            memcpy(hn, res->ai_canonname, l);
         }
         freeaddrinfo(res);
      }
# else
      hent = gethostbyname(hn);
      if (hent != NULL)
         hn = hent->h_name;
# endif
#endif
      sys_hostname = sstrdup(hn);
#if defined HAVE_SOCKETS && defined HAVE_GETADDRINFO
      if (hn != ut.nodename)
         ac_free(hn);
#endif
      hn = sys_hostname;
   }

   if (hostname != NULL && hostname != sys_hostname)
      free(hostname);
   hostname = sstrdup(hn);
   NYD_LEAVE;
   return hostname;
}

FL char *
getrandstring(size_t length)
{
   struct str b64;
   char *data;
   size_t i;
   NYD_ENTER;

#ifndef HAVE_POSIX_RANDOM
   if (_rand == NULL)
      _rand_init();
#endif

   /* We use our base64 encoder with _NOPAD set, so ensure the encoded result
    * with PAD stripped is still longer than what the user requests, easy way */
   data = ac_alloc(i = length + 3);

#ifndef HAVE_POSIX_RANDOM
   while (i-- > 0)
      data[i] = (char)_rand_get8();
#else
   {  char *cp = data;

      while (i > 0) {
         union {ui32_t i4; char c[4];} r;
         size_t j;

         r.i4 = (ui32_t)arc4random();
         switch ((j = i & 3)) {
         case 0:  cp[3] = r.c[3]; j = 4;
         case 3:  cp[2] = r.c[2];
         case 2:  cp[1] = r.c[1];
         default: cp[0] = r.c[0]; break;
         }
         cp += j;
         i -= j;
      }
   }
#endif

   assert(length + 3 < UIZ_MAX / 4);
   b64_encode_buf(&b64, data, length + 3,
      B64_SALLOC | B64_RFC4648URL | B64_NOPAD);
   ac_free(data);

   assert(b64.l >= length);
   b64.s[length] = '\0';
   NYD_LEAVE;
   return b64.s;
}

FL si8_t
boolify(char const *inbuf, uiz_t inlen, si8_t emptyrv)
{
   char *dat, *eptr;
   sl_i sli;
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
         dat = ac_alloc(inlen +1);
         memcpy(dat, inbuf, inlen);
         dat[inlen] = '\0';

         sli = strtol(dat, &eptr, 0);
         if (*dat != '\0' && *eptr == '\0')
            rv = (sli != 0);
         else
            rv = -1;

         ac_free(dat);
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
         (options & OPT_INTERACTIVE))
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

FL time_t
n_time_epoch(void)
{
#ifdef HAVE_CLOCK_GETTIME
   struct timespec ts;
#elif defined HAVE_GETTIMEOFDAY
   struct timeval ts;
#endif
   time_t rv;
   char const *cp;
   NYD2_ENTER;

   if((cp = ok_vlook(SOURCE_DATE_EPOCH)) != NULL){ /* TODO */
      /* TODO This is marked "posnum", b and therefore 0<=X<=UINT_MAX.
       * TODO This means we have a Sun, 07 Feb 2106 07:28:15 +0100 problem.
       * TODO Therefore we need a num_ui64= type in v15 */
      rv = (time_t)strtoul(cp, NULL, 0);
   }else{
#ifdef HAVE_CLOCK_GETTIME
      clock_gettime(CLOCK_REALTIME, &ts);
      rv = (time_t)ts.tv_sec;
#elif defined HAVE_GETTIMEOFDAY
      gettimeofday(&ts, NULL);
      rv = (time_t)ts.tv_sec;
#else
      rv = time(NULL);
#endif
   }
   NYD2_LEAVE;
   return rv;
}

FL void
time_current_update(struct time_current *tc, bool_t full_update)
{
   NYD_ENTER;
   tc->tc_time = n_time_epoch();
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
   if(options & OPT_INTERACTIVE)
      n_verr(format, ap);
   else
#endif
   {
      size_t len;
      bool_t doname, doflush;

      doflush = FAL0;
      while(*format == '\n'){
         doflush = TRU1;
         putc('\n', stderr);
         ++format;
      }

      if((doname = doflush))
         a_aux_err_linelen = 0;

      if((len = strlen(format)) > 0){
         if(doname || a_aux_err_linelen == 0)
            fputs(VAL_UAGENT ": ", stderr);
         vfprintf(stderr, format, ap);

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

      if(doflush)
         fflush(stderr);
   }
   va_end(ap);
   NYD2_LEAVE;
}

FL void
n_verr(char const *format, va_list ap){
   /* Check use cases of PS_ERRORS_NOTED, too! */
#ifdef HAVE_ERRORS
   struct a_aux_err_node *enp;
#endif
   bool_t doname, doflush;
   size_t len;
   NYD2_ENTER;

   doflush = FAL0;
   while(*format == '\n'){
      doflush = TRU1;
      putc('\n', stderr);
      ++format;
   }

   if((doname = doflush)){
      a_aux_err_linelen = 0;
#ifdef HAVE_ERRORS
      if(options & OPT_INTERACTIVE){
         if((enp = a_aux_err_tail) != NULL &&
               (enp->ae_str.s_len > 0 &&
                enp->ae_str.s_dat[enp->ae_str.s_len - 1] != '\n'))
            n_string_push_c(&enp->ae_str, '\n');
      }
#endif
   }

   if((len = strlen(format)) == 0)
      goto jleave;

   if(doname || a_aux_err_linelen == 0)
      fputs(VAL_UAGENT ": ", stderr);

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
   if(!(options & OPT_INTERACTIVE))
#endif
      vfprintf(stderr, format, ap);
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

      fwrite(&enp->ae_str.s_dat[len], 1, (size_t)i, stderr);
   }
#endif /* HAVE_ERRORS */

jleave:
   if(doflush)
      fflush(stderr);
   NYD2_LEAVE;
}

FL void
n_err_sighdl(char const *format, ...){ /* TODO sigsafe; obsolete! */
   va_list ap;
   NYD_X;

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);
   fflush(stderr);
}

FL void
n_perr(char const *msg, int errval){
   char const *fmt;
   NYD2_ENTER;

   if(msg == NULL){
      fmt = "%s%s\n";
      msg = "";
   }else
      fmt = "%s: %s\n";

   if(errval == 0)
      errval = errno;

   n_err(fmt, msg, strerror(errval));
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
      putc('\n', stderr);
      a_aux_err_linelen = 0;
   }
   fprintf(stderr, VAL_UAGENT ": Panic: ");

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);

   putc('\n', stderr);
   fflush(stderr);
   NYD2_LEAVE;
   abort(); /* Was exit(EXIT_ERR); for a while, but no */
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
   fprintf(stderr, _("Synopsis: errors: (<show> or) <clear> the error ring\n"));
   v = NULL;
jleave:
   NYD_LEAVE;
   return (v == NULL) ? !STOP : !OKAY; /* xxx 1:bad 0:good -- do some */

jlist:{
      FILE *fp;
      size_t i;

      if(a_aux_err_head == NULL){
         fprintf(stderr, _("The error ring is empty\n"));
         goto jleave;
      }

      if((fp = Ftmp(NULL, "errors", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
            NULL){
         fprintf(stderr, _("tmpfile"));
         v = NULL;
         goto jleave;
      }

      for(i = 0, enp = a_aux_err_head; enp != NULL; enp = enp->ae_next)
         fprintf(fp, "%4" PRIuZ ". %u B: %s",
            ++i, enp->ae_str.s_len, n_string_cp(&enp->ae_str));
      /* We don't know whether last string ended with NL; be simple */
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

/* s-it-mode */
