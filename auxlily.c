/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <sys/utsname.h>

#include <ctype.h>
#include <dirent.h>

#ifdef HAVE_SOCKETS
# ifdef HAVE_GETADDRINFO
#  include <sys/socket.h>
# endif

# include <netdb.h>
#endif

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

#ifdef HAVE_NYD
struct nyd_info {
   char const  *ni_file;
   char const  *ni_fun;
   ui32_t      ni_chirp_line;
   ui32_t      ni_level;
};
#endif

#ifdef HAVE_DEBUG
struct mem_chunk {
   struct mem_chunk  *mc_prev;
   struct mem_chunk  *mc_next;
   char const        *mc_file;
   ui16_t            mc_line;
   ui8_t             mc_isfree;
   ui8_t             __dummy;
   ui32_t            mc_size;
};

union mem_ptr {
   void              *p_p;
   struct mem_chunk  *p_c;
   char              *p_cp;
   ui8_t             *p_ui8p;
};
#endif

static union rand_state *_rand;

/* {hold,rele}_all_sigs() */
static size_t           _alls_depth;
static sigset_t         _alls_nset, _alls_oset;

/* {hold,rele}_sigs() */
static size_t           _hold_sigdepth;
static sigset_t         _hold_nset, _hold_oset;

/* NYD, memory pool debug */
#ifdef HAVE_NYD
static ui32_t           _nyd_curr, _nyd_level;
static struct nyd_info  _nyd_infos[NYD_CALLS_MAX];
#endif

#ifdef HAVE_DEBUG
static size_t           _mem_aall, _mem_acur, _mem_amax,
                        _mem_mall, _mem_mcur, _mem_mmax;

static struct mem_chunk *_mem_list, *_mem_free;
#endif

/* Our ARC4 random generator with its completely unacademical pseudo
 * initialization (shall /dev/urandom fail) */
static void    _rand_init(void);
static ui32_t  _rand_weak(ui32_t seed);
SINLINE ui8_t  _rand_get8(void);

/* Create an ISO 6429 (ECMA-48/ANSI) terminal control escape sequence */
#ifdef HAVE_COLOUR
static char *  _colour_iso6429(char const *wish);
#endif

#ifdef HAVE_NYD
static void    _nyd_print(int fd, struct nyd_info *nip);
#endif

static void
_rand_init(void)
{
#ifdef HAVE_CLOCK_GETTIME
   struct timespec ts;
#else
   struct timeval ts;
#endif
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
      for (u.i = NELEM(_rand->b32); u.i-- != 0;) {
         size_t t, k;

#ifdef HAVE_CLOCK_GETTIME
         clock_gettime(CLOCK_REALTIME, &ts);
         t = (ui32_t)ts.tv_nsec;
#else
         gettimeofday(&ts, NULL);
         t = (ui32_t)ts.tv_usec;
#endif
         if (rnd & 1)
            t = (t >> 16) | (t << 16);
         _rand->b32[u.i] ^= _rand_weak(seed ^ t);
         _rand->b32[t % NELEM(_rand->b32)] ^= seed;
         if (rnd == 7 || rnd == 17)
            _rand->b32[u.i] ^= _rand_weak(seed ^ (ui32_t)ts.tv_sec);
         k = _rand->b32[u.i] % NELEM(_rand->b32);
         _rand->b32[k] ^= _rand->b32[u.i];
         seed ^= _rand_weak(_rand->b32[k]);
         if ((rnd & 3) == 3)
            seed ^= nextprime(seed);
      }
   }

   for (u.i = 11 * sizeof(_rand->b8); u.i != 0; --u.i)
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

#ifdef HAVE_COLOUR
static char *
_colour_iso6429(char const *wish)
{
   char const * const wish_orig = wish;
   char *xwish, *cp, cfg[3] = {0, 0, 0};
   NYD_ENTER;

   /* Since we use salloc(), reuse the n_strsep() buffer also for the return
    * value, ensure we have enough room for that */
   {
      size_t i = strlen(wish) +1;
      xwish = salloc(MAX(i, sizeof("\033[1;30;40m")));
      memcpy(xwish, wish, i);
      wish = xwish;
   }

   /* Iterate over the colour spec */
   while ((cp = n_strsep(&xwish, ',', TRU1)) != NULL) {
      char *y, *x = strchr(cp, '=');
      if (x == NULL) {
jbail:
         fprintf(stderr, _(
            "Invalid colour specification \"%s\": >>> %s <<<\n"),
            wish_orig, cp);
         continue;
      }
      *x++ = '\0';

      /* TODO convert the ft/fg/bg parser into a table-based one! */
      if (!asccasecmp(cp, "ft")) {
         if (!asccasecmp(x, "bold"))
            cfg[0] = '1';
         else if (!asccasecmp(x, "inverse"))
            cfg[0] = '7';
         else if (!asccasecmp(x, "underline"))
            cfg[0] = '4';
         else
            goto jbail;
      } else if (!asccasecmp(cp, "fg")) {
         y = cfg + 1;
         goto jiter_colour;
      } else if (!asccasecmp(cp, "bg")) {
         y = cfg + 2;
jiter_colour:
         if (!asccasecmp(x, "black"))
            *y = '0';
         else if (!asccasecmp(x, "blue"))
            *y = '4';
         else if (!asccasecmp(x, "green"))
            *y = '2';
         else if (!asccasecmp(x, "red"))
            *y = '1';
         else if (!asccasecmp(x, "brown"))
            *y = '3';
         else if (!asccasecmp(x, "magenta"))
            *y = '5';
         else if (!asccasecmp(x, "cyan"))
            *y = '6';
         else if (!asccasecmp(x, "white"))
            *y = '7';
         else
            goto jbail;
      } else
         goto jbail;
   }

   /* Restore our salloc() buffer, create return value */
   xwish = UNCONST(wish);
   if (cfg[0] || cfg[1] || cfg[2]) {
      xwish[0] = '\033';
      xwish[1] = '[';
      xwish += 2;
      if (cfg[0])
         *xwish++ = cfg[0];
      if (cfg[1]) {
         if (cfg[0])
            *xwish++ = ';';
         xwish[0] = '3';
         xwish[1] = cfg[1];
         xwish += 2;
      }
      if (cfg[2]) {
         if (cfg[0] || cfg[1])
            *xwish++ = ';';
         xwish[0] = '4';
         xwish[1] = cfg[2];
         xwish += 2;
      }
      *xwish++ = 'm';
   }
   *xwish = '\0';
   NYD_LEAVE;
   return UNCONST(wish);
}
#endif /* HAVE_COLOUR */

#ifdef HAVE_NYD
static void
_nyd_print(int fd, struct nyd_info *nip)
{
   char buf[80];
   union {int i; size_t z;} u;

   u.i = snprintf(buf, sizeof buf,
         "%c [%2" PRIu32 "] %.25s (%.16s:%" PRIu32 ")\n",
         "=><"[(nip->ni_chirp_line >> 29) & 0x3], nip->ni_level, nip->ni_fun,
         nip->ni_file, (nip->ni_chirp_line & 0x1FFFFFFFu));
   if (u.i > 0) {
      u.z = u.i;
      if (u.z > sizeof buf)
         u.z = sizeof buf - 1; /* (Skip \0) */
      write(fd, buf, u.z);
   }
}
#endif

FL void
panic(char const *format, ...)
{
   va_list ap;
   NYD2_ENTER;

   fprintf(stderr, _("Panic: "));

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);

   fputs("\n", stderr);
   fflush(stderr);
   NYD2_LEAVE;
   abort(); /* Was exit(EXIT_ERR); for a while, but no */
}

FL void
alert(char const *format, ...)
{
   va_list ap;
   NYD2_ENTER;

   fprintf(stderr, _("Panic: "));

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);

   fputs("\n", stderr);
   fflush(stderr);
   NYD2_LEAVE;
}

FL sighandler_type
safe_signal(int signum, sighandler_type handler)
{
   struct sigaction nact, oact;
   sighandler_type rv;
   NYD2_ENTER;

   nact.sa_handler = handler;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = 0;
#ifdef SA_RESTART
   nact.sa_flags |= SA_RESTART;
#endif
   rv = (sigaction(signum, &nact, &oact) != 0) ? SIG_ERR : oact.sa_handler;
   NYD2_LEAVE;
   return rv;
}

FL void
hold_all_sigs(void)
{
   NYD2_ENTER;
   if (_alls_depth++ == 0) {
      sigfillset(&_alls_nset);
      sigdelset(&_alls_nset, SIGABRT);
#ifdef SIGBUS
      sigdelset(&_alls_nset, SIGBUS);
#endif
      sigdelset(&_alls_nset, SIGCHLD);
      sigdelset(&_alls_nset, SIGFPE);
      sigdelset(&_alls_nset, SIGILL);
      sigdelset(&_alls_nset, SIGKILL);
      sigdelset(&_alls_nset, SIGSEGV);
      sigdelset(&_alls_nset, SIGSTOP);
      sigprocmask(SIG_BLOCK, &_alls_nset, &_alls_oset);
   }
   NYD2_LEAVE;
}

FL void
rele_all_sigs(void)
{
   NYD2_ENTER;
   if (--_alls_depth == 0)
      sigprocmask(SIG_SETMASK, &_alls_oset, (sigset_t*)NULL);
   NYD2_LEAVE;
}

FL void
hold_sigs(void)
{
   NYD2_ENTER;
   if (_hold_sigdepth++ == 0) {
      sigemptyset(&_hold_nset);
      sigaddset(&_hold_nset, SIGHUP);
      sigaddset(&_hold_nset, SIGINT);
      sigaddset(&_hold_nset, SIGQUIT);
      sigprocmask(SIG_BLOCK, &_hold_nset, &_hold_oset);
   }
   NYD2_LEAVE;
}

FL void
rele_sigs(void)
{
   NYD2_ENTER;
   if (--_hold_sigdepth == 0)
      sigprocmask(SIG_SETMASK, &_hold_oset, NULL);
   NYD2_LEAVE;
}

#ifdef HAVE_NYD
FL void
_nyd_chirp(ui8_t act, char const *file, ui32_t line, char const *fun)
{
   struct nyd_info *nip = _nyd_infos;

   if (_nyd_curr != NELEM(_nyd_infos))
      nip += _nyd_curr++;
   else
      _nyd_curr = 1;
   nip->ni_file = file;
   nip->ni_fun = fun;
   nip->ni_chirp_line = ((ui32_t)(act & 0x3) << 29) | (line & 0x1FFFFFFFu);
   nip->ni_level = ((act == 0) ? _nyd_level
         : (act == 1) ? ++_nyd_level : _nyd_level--);
}

FL void
_nyd_oncrash(int signo)
{
   char s2ibuf[32], *fname, *cp;
   struct sigaction xact;
   sigset_t xset;
   size_t fnl, i;
   int fd;
   struct nyd_info *nip;

   xact.sa_handler = SIG_DFL;
   sigemptyset(&xact.sa_mask);
   xact.sa_flags = 0;
   sigaction(signo, &xact, NULL);

   fnl = strlen(UAGENT);
   i = strlen(tempdir);
   cp =
   fname = ac_alloc(i + 1 + fnl + 1 + sizeof(".dat"));
   memcpy(cp , tempdir, i);
   cp[i++] = '/'; /* xxx pathsep */
   memcpy(cp += i, UAGENT, fnl);
   i += fnl;
   memcpy(cp += fnl, ".dat", sizeof(".dat"));
   fnl = i + sizeof(".dat") -1;

   if ((fd = open(fname, O_WRONLY | O_CREAT | O_EXCL, 0666)) == -1)
      fd = STDERR_FILENO;

# define _X(X) (X), sizeof(X) -1
   write(fd, _X("\n\nNYD: program dying due to signal "));

   cp = s2ibuf + sizeof(s2ibuf) -1;
	*cp = '\0';
   i = signo;
	do {
		*--cp = "0123456789"[i % 10];
		i /= 10;
	} while (i != 0);
   write(fd, cp, PTR2SIZE((s2ibuf + sizeof(s2ibuf) -1) - cp));

   write(fd, _X(":\n"));

   if (_nyd_infos[NELEM(_nyd_infos) - 1].ni_file != NULL)
      for (i = _nyd_curr, nip = _nyd_infos + i; i < NELEM(_nyd_infos); ++i)
         _nyd_print(fd, nip++);
   for (i = 0, nip = _nyd_infos; i < _nyd_curr; ++i)
      _nyd_print(fd, nip++);

   write(fd, _X("----------\nYou'd see a disappointed man.  Sorry.\n"));

   if (fd != STDERR_FILENO) {
      write(STDERR_FILENO, _X("Crash NYD listing written to "));
      write(STDERR_FILENO, fname, fnl);
      write(STDERR_FILENO, _X("\n"));
# undef _X

      close(fd);
   }

   ac_free(fname);

   sigemptyset(&xset);
   sigaddset(&xset, signo);
   sigprocmask(SIG_UNBLOCK, &xset, NULL);
   kill(0, signo);
   for (;;)
      _exit(EXIT_ERR);
}
#endif /* HAVE_NYD */

FL void
touch(struct message *mp)
{
   NYD_ENTER;
   mp->m_flag |= MTOUCH;
   if (!(mp->m_flag & MREAD))
      mp->m_flag |= MREAD | MSTATUS;
   NYD_LEAVE;
}

FL bool_t
is_dir(char const *name)
{
   struct stat sbuf;
   bool_t rv = FAL0;
   NYD_ENTER;

   if (!stat(name, &sbuf))
      rv = (S_ISDIR(sbuf.st_mode) != 0);
   NYD_LEAVE;
   return rv;
}

FL int
argcount(char **argv)
{
   char **ap;
   NYD_ENTER;

   for (ap = argv; *ap++ != NULL;)
      ;
   NYD_LEAVE;
   return (int)PTR2SIZE(ap - argv - 1);
}

FL int
screensize(void)
{
   int s;
   char *cp;
   NYD_ENTER;

   if ((cp = ok_vlook(screen)) == NULL || (s = atoi(cp)) <= 0)
      s = scrnheight - 4; /* XXX no magics */
   NYD_LEAVE;
   return s;
}

FL char const *
get_pager(char const **env_addon)
{
   char const *cp;
   NYD_ENTER;

   cp = ok_vlook(PAGER);
   if (cp == NULL || *cp == '\0')
      cp = XPAGER;

   if (env_addon != NULL) {
      *env_addon = NULL;
      if (strstr(cp, "less") != NULL) {
         if (getenv("LESS") == NULL)
            *env_addon = "LESS=FRSXi";
      } else if (strstr(cp, "lv") != NULL) {
         if (getenv("LV") == NULL)
            *env_addon = "LV=-c";
      }
   }
   NYD_LEAVE;
   return cp;
}

FL size_t
paging_seems_sensible(void)
{
   size_t rv = 0;
   char const *cp;
   NYD_ENTER;

   if (IS_TTY_SESSION() && (cp = ok_vlook(crt)) != NULL)
      rv = (*cp != '\0') ? (size_t)atol(cp) : (size_t)scrnheight;
   NYD_LEAVE;
   return rv;
}

FL void
page_or_print(FILE *fp, size_t lines)
{
   size_t rows;
   int c;
   NYD_ENTER;

   fflush_rewind(fp);

   if ((rows = paging_seems_sensible()) != 0 && lines == 0) {
      while ((c = getc(fp)) != EOF)
         if (c == '\n' && ++lines > rows)
            break;
      really_rewind(fp);
   }

   if (rows != 0 && lines >= rows)
      run_command(get_pager(NULL), 0, fileno(fp), -1, NULL, NULL, NULL);
   else
      while ((c = getc(fp)) != EOF)
         putchar(c);
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
         fprintf(stderr, _("No POP3 support compiled in.\n"));
#endif
      } else if (!strncmp(name, "pop3s://", 8)) {
#if defined HAVE_POP3 && defined HAVE_SSL
         rv = PROTO_POP3;
#else
# ifndef HAVE_POP3
         fprintf(stderr, _("No POP3 support compiled in.\n"));
# endif
# ifndef HAVE_SSL
         fprintf(stderr, _("No SSL support compiled in.\n"));
# endif
#endif
      } else if (!strncmp(name, "imap://", 7)) {
#ifdef HAVE_IMAP
         rv = PROTO_IMAP;
#else
         fprintf(stderr, _("No IMAP support compiled in.\n"));
#endif
      } else if (!strncmp(name, "imaps://", 8)) {
#if defined HAVE_IMAP && defined HAVE_SSL
         rv = PROTO_IMAP;
#else
# ifndef HAVE_IMAP
         fprintf(stderr, _("No IMAP support compiled in.\n"));
# endif
# ifndef HAVE_SSL
         fprintf(stderr, _("No SSL support compiled in.\n"));
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
      else if ((cp = ok_vlook(newfolders)) != NULL && !strcmp(cp, "maildir"))
         rv = PROTO_MAILDIR;
   }
   ac_free(np);
jleave:
   NYD_LEAVE;
   return rv;
}

FL ui32_t
torek_hash(char const *name)
{
   /* Chris Torek's hash.
    * NOTE: need to change *at least* create-okey-map.pl when changing the
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

FL unsigned
pjw(char const *cp) /* TODO obsolete that -> torek_hash */
{
   unsigned h = 0, g;
   NYD_ENTER;

   cp--;
   while (*++cp) {
      h = (h << 4 & 0xffffffff) + (*cp&0377);
      if ((g = h & 0xf0000000) != 0) {
         h = h ^ g >> 24;
         h = h ^ g;
      }
   }
   NYD_LEAVE;
   return h;
}

FL ui32_t
nextprime(ui32_t n)
{
   static ui32_t const primes[] = {
      509, 1021, 2039, 4093, 8191, 16381, 32749, 65521,
      131071, 262139, 524287, 1048573, 2097143, 4194301,
      8388593, 16777213, 33554393, 67108859, 134217689,
      268435399, 536870909, 1073741789, 2147483647
   };

   ui32_t mprime = 7, cutlim;
   size_t i;
   NYD_ENTER;

   cutlim = (n < 65536 ? n << 2 : (n < 262144 ? n << 1 : n));

   for (i = 0; i < NELEM(primes); i++)
      if ((mprime = primes[i]) >= cutlim)
         break;
   if (i == NELEM(primes) && mprime < n)
      mprime = n;
   NYD_LEAVE;
   return mprime;
}

FL int
expand_shell_escape(char const **s, bool_t use_nail_extensions)
{
   char const *xs;
   int c, n;
   NYD2_ENTER;

   xs = *s;

   if ((c = *xs & 0xFF) == '\0')
      goto jleave;
   ++xs;
   if (c != '\\')
      goto jleave;

   switch ((c = *xs & 0xFF)) {
   case '\\':                    break;
   case 'a':   c = '\a';         break;
   case 'b':   c = '\b';         break;
   case 'c':   c = PROMPT_STOP;  break;
   case 'f':   c = '\f';         break;
   case 'n':   c = '\n';         break;
   case 'r':   c = '\r';         break;
   case 't':   c = '\t';         break;
   case 'v':   c = '\v';         break;
   case '0':
      for (++xs, c = 0, n = 4; --n > 0 && octalchar(*xs); ++xs) {
         c <<= 3;
         c |= *xs - '0';
      }
      goto jleave;
   /* S-nail extension for nice (get)prompt(()) support */
   case '&':
   case '?':
   case '$':
   case '@':
      if (use_nail_extensions) {
         switch (c) {
         case '&':   c = ok_blook(bsdcompat)       ? '&' : '?';   break;
         case '?':   c = (pstate & PS_EVAL_ERROR)  ? '1' : '0';   break;
         case '$':   c = PROMPT_DOLLAR;                           break;
         case '@':   c = PROMPT_AT;                               break;
         }
         break;
      }
      /* FALLTHRU */
   case '\0':
      /* A sole <backslash> at EOS is treated as-is! */
      /* FALLTHRU */
   default:
      c = '\\';
      goto jleave;
   }
   ++xs;
jleave:
   *s = xs;
   NYD2_LEAVE;
   return c;
}

FL char *
getprompt(void) /* TODO evaluate only as necessary (needs a bit) */
{
   static char buf[PROMPT_BUFFER_SIZE];

   char *cp;
   char const *ccp_base, *ccp;
   size_t NATCH_CHAR( cclen_base COMMA cclen COMMA ) maxlen, dfmaxlen;
   bool_t run2;
   NYD_ENTER;

   cp = buf;
   if ((ccp_base = ok_vlook(prompt)) == NULL || *ccp_base == '\0')
      goto jleave;
   NATCH_CHAR( cclen_base = strlen(ccp_base); )

   dfmaxlen = 0; /* keep CC happy */
   run2 = FAL0;
jredo:
   ccp = ccp_base;
   NATCH_CHAR( cclen = cclen_base; )
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
         if (run2) {
            memcpy(cp, ccp, l);
            cp += l;
         }
         ccp += l;
         maxlen -= l;
         continue;
      } else
#endif
      if ((c = expand_shell_escape(&ccp, TRU1)) > 0) {
            if (run2)
               *cp++ = (char)c;
            --maxlen;
            continue;
      }
      if (c == 0 || c == PROMPT_STOP)
         break;

      if (run2) {
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

   if (!run2) {
      run2 = TRU1;
      dfmaxlen = maxlen;
      goto jredo;
   }
jleave:
   *cp = '\0';
   NYD_LEAVE;
   return buf;
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
      hints.ai_socktype = SOCK_DGRAM; /* (dummy) */
      hints.ai_flags = AI_CANONNAME;
      if (getaddrinfo(hn, "0", &hints, &res) == 0) {
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

   if (_rand == NULL)
      _rand_init();

   data = ac_alloc(length);
   for (i = length; i-- > 0;)
      data[i] = (char)_rand_get8();
   b64_encode_buf(&b64, data, length, B64_SALLOC);
   ac_free(data);

   /* Base64 includes + and /, replace them with _ and - */
   *(data = b64.s + length) = '\0';
   while (length-- != 0)
      switch (*--data) {
      case '+':   *data = '_'; break;
      case '/':   *data = '-'; break;
      default:    break;
      }
   NYD_LEAVE;
   return b64.s;
}

FL enum okay
makedir(char const *name)
{
   struct stat st;
   enum okay rv = STOP;
   NYD_ENTER;

   if (!mkdir(name, 0700))
      rv = OKAY;
   else {
      int e = errno;
      if ((e == EEXIST || e == ENOSYS) && !stat(name, &st) &&
            S_ISDIR(st.st_mode))
         rv = OKAY;
   }
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_FCHDIR
FL enum okay
cwget(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_ENTER;

   if ((cw->cw_fd = open(".", O_RDONLY)) == -1)
      goto jleave;
   if (fchdir(cw->cw_fd) == -1) {
      close(cw->cw_fd);
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
cwret(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_ENTER;

   if (!fchdir(cw->cw_fd))
      rv = OKAY;
   NYD_LEAVE;
   return rv;
}

FL void
cwrelse(struct cw *cw)
{
   NYD_ENTER;
   close(cw->cw_fd);
   NYD_LEAVE;
}

#else /* !HAVE_FCHDIR */
FL enum okay
cwget(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_ENTER;

   if (getcwd(cw->cw_wd, sizeof cw->cw_wd) != NULL && !chdir(cw->cw_wd))
      rv = OKAY;
   NYD_LEAVE;
   return rv;
}

FL enum okay
cwret(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_ENTER;

   if (!chdir(cw->cw_wd))
      rv = OKAY;
   NYD_LEAVE;
   return rv;
}

FL void
cwrelse(struct cw *cw)
{
   NYD_ENTER;
   UNUSED(cw);
   NYD_LEAVE;
}
#endif /* !HAVE_FCHDIR */

FL size_t
field_detect_clip(size_t maxlen, char const *buf, size_t blen)/*TODO mbrtowc()*/
{
   size_t rv;
   NYD_ENTER;

#ifdef HAVE_NATCH_CHAR
   maxlen = MIN(maxlen, blen);
   for (rv = 0; maxlen > 0;) {
      int ml = mblen(buf, maxlen);
      if (ml <= 0) {
         mblen(NULL, 0);
         break;
      }
      buf += ml;
      rv += ml;
      maxlen -= ml;
   }
#else
   rv = MIN(blen, maxlen);
#endif
   NYD_LEAVE;
   return rv;
}

FL size_t
field_put_bidi_clip(char *store, size_t maxlen, char const *buf, size_t blen)
{
   NATCH_CHAR( struct bidi_info bi; )
   size_t rv NATCH_CHAR( COMMA i );
   NYD_ENTER;

   rv = 0;
   if (maxlen-- == 0)
      goto j_leave;

#ifdef HAVE_NATCH_CHAR
   bidi_info_create(&bi);
   if (bi.bi_start.l == 0 || !bidi_info_needed(buf, blen)) {
      bi.bi_end.l = 0;
      goto jnobidi;
   }

   if (maxlen >= (i = bi.bi_pad + bi.bi_end.l + bi.bi_start.l))
      maxlen -= i;
   else
      goto jleave;

   if ((i = bi.bi_start.l) > 0) {
      memcpy(store, bi.bi_start.s, i);
      store += i;
      rv += i;
   }

jnobidi:
   while (maxlen > 0) {
      int ml = mblen(buf, blen);
      if (ml <= 0) {
         mblen(NULL, 0);
         break;
      }
      if (UICMP(z, maxlen, <, ml))
         break;
      if (ml == 1)
         *store = *buf;
      else
         memcpy(store, buf, ml);
      store += ml;
      buf += ml;
      rv += ml;
      maxlen -= ml;
   }

   if ((i = bi.bi_end.l) > 0) {
      memcpy(store, bi.bi_end.s, i);
      store += i;
      rv += i;
   }
jleave:
   *store = '\0';

#else
   rv = MIN(blen, maxlen);
   memcpy(store, buf, rv);
   store[rv] = '\0';
#endif
j_leave:
   NYD_LEAVE;
   return rv;
}

FL char *
colalign(char const *cp, int col, int fill, int *cols_decr_used_or_null)
{
   NATCH_CHAR( struct bidi_info bi; )
   int col_orig = col, n, sz;
   bool_t isbidi, isuni, istab, isrepl;
   char *nb, *np;
   NYD_ENTER;

   /* Bidi only on request and when there is 8-bit data */
   isbidi = isuni = FAL0;
#ifdef HAVE_NATCH_CHAR
   isuni = ((options & OPT_UNICODE) != 0);
   bidi_info_create(&bi);
   if (bi.bi_start.l == 0)
      goto jnobidi;
   if (!(isbidi = bidi_info_needed(cp, strlen(cp))))
      goto jnobidi;

   if ((size_t)col >= bi.bi_pad)
      col -= bi.bi_pad;
   else
      col = 0;
jnobidi:
#endif

   np = nb = salloc(mb_cur_max * strlen(cp) +
         ((fill ? col : 0)
         NATCH_CHAR( + (isbidi ? bi.bi_start.l + bi.bi_end.l : 0) )
         +1));

#ifdef HAVE_NATCH_CHAR
   if (isbidi) {
      memcpy(np, bi.bi_start.s, bi.bi_start.l);
      np += bi.bi_start.l;
   }
#endif

   while (*cp != '\0') {
      istab = FAL0;
#ifdef HAVE_C90AMEND1
      if (mb_cur_max > 1) {
         wchar_t  wc;

         n = 1;
         isrepl = TRU1;
         if ((sz = mbtowc(&wc, cp, mb_cur_max)) == -1)
            sz = 1;
         else if (wc == L'\t') {
            cp += sz - 1; /* Silly, no such charset known (.. until S-Ctext) */
            isrepl = FAL0;
            istab = TRU1;
         } else if (iswprint(wc)) {
# ifndef HAVE_WCWIDTH
            n = 1 + (wc >= 0x1100u); /* TODO use S-CText isfullwidth() */
# else
            if ((n = wcwidth(wc)) == -1)
               n = 1;
            else
# endif
               isrepl = FAL0;
         }
      } else
#endif
      {
         n = sz = 1;
         istab = (*cp == '\t');
         isrepl = !(istab || isprint((uc_i)*cp));
      }

      if (n > col)
         break;
      col -= n;

      if (isrepl) {
         if (isuni) {
            np[0] = (char)0xEFu;
            np[1] = (char)0xBFu;
            np[2] = (char)0xBDu;
            np += 3;
         } else
            *np++ = '?';
         cp += sz;
      } else if (istab || (sz == 1 && spacechar(*cp))) {
         *np++ = ' ';
         ++cp;
      } else
         while (sz--)
            *np++ = *cp++;
   }

   if (fill && col != 0) {
      if (fill > 0) {
         memmove(nb + col, nb, PTR2SIZE(np - nb));
         memset(nb, ' ', col);
      } else
         memset(np, ' ', col);
      np += col;
      col = 0;
   }

#ifdef HAVE_NATCH_CHAR
   if (isbidi) {
      memcpy(np, bi.bi_end.s, bi.bi_end.l);
      np += bi.bi_end.l;
   }
#endif

   *np = '\0';
   if (cols_decr_used_or_null != NULL)
      *cols_decr_used_or_null -= col_orig - col;
   NYD_LEAVE;
   return nb;
}

FL void
makeprint(struct str const *in, struct str *out)
{
   static int print_all_chars = -1;

   char const *inp, *maxp;
   char *outp;
   DBG( size_t msz; )
   NYD_ENTER;

   if (print_all_chars == -1)
      print_all_chars = ok_blook(print_all_chars);

   out->s = outp = smalloc(DBG( msz = ) in->l*mb_cur_max + 2u*mb_cur_max);
   inp = in->s;
   maxp = inp + in->l;

   if (print_all_chars) {
      out->l = in->l;
      memcpy(outp, inp, out->l);
      goto jleave;
   }

#ifdef HAVE_NATCH_CHAR
   if (mb_cur_max > 1) {
      char mbb[MB_LEN_MAX + 1];
      wchar_t wc;
      int i, n;
      bool_t isuni = ((options & OPT_UNICODE) != 0);

      out->l = 0;
      while (inp < maxp) {
         if (*inp & 0200)
            n = mbtowc(&wc, inp, PTR2SIZE(maxp - inp));
         else {
            wc = *inp;
            n = 1;
         }
         if (n == -1) {
            /* FIXME Why mbtowc() resetting here?
             * FIXME what about ISO 2022-JP plus -- those
             * FIXME will loose shifts, then!
             * FIXME THUS - we'd need special "known points"
             * FIXME to do so - say, after a newline!!
             * FIXME WE NEED TO CHANGE ALL USES +MBLEN! */
            mbtowc(&wc, NULL, mb_cur_max);
            wc = isuni ? 0xFFFD : '?';
            n = 1;
         } else if (n == 0)
            n = 1;
         inp += n;
         if (!iswprint(wc) && wc != '\n' && wc != '\r' && wc != '\b' &&
               wc != '\t') {
            if ((wc & ~(wchar_t)037) == 0)
               wc = isuni ? 0x2400 | wc : '?';
            else if (wc == 0177)
               wc = isuni ? 0x2421 : '?';
            else
               wc = isuni ? 0x2426 : '?';
         }
         if ((n = wctomb(mbb, wc)) <= 0)
            continue;
         out->l += n;
         assert(out->l < msz);
         for (i = 0; i < n; ++i)
            *outp++ = mbb[i];
      }
   } else
#endif /* NATCH_CHAR */
   {
      int c;
      while (inp < maxp) {
         c = *inp++ & 0377;
         if (!isprint(c) && c != '\n' && c != '\r' && c != '\b' && c != '\t')
            c = '?';
         *outp++ = c;
      }
      out->l = in->l;
   }
jleave:
   out->s[out->l] = '\0';
   NYD_LEAVE;
}

FL char *
prstr(char const *s)
{
   struct str in, out;
   char *rp;
   NYD_ENTER;

   in.s = UNCONST(s);
   in.l = strlen(s);
   makeprint(&in, &out);
   rp = savestrbuf(out.s, out.l);
   free(out.s);
   NYD_LEAVE;
   return rp;
}

FL int
prout(char const *s, size_t sz, FILE *fp)
{
   struct str in, out;
   int n;
   NYD_ENTER;

   in.s = UNCONST(s);
   in.l = sz;
   makeprint(&in, &out);
   n = fwrite(out.s, 1, out.l, fp);
   free(out.s);
   NYD_LEAVE;
   return n;
}

FL size_t
putuc(int u, int c, FILE *fp)
{
   size_t rv;
   NYD_ENTER;
   UNUSED(u);

#ifdef HAVE_NATCH_CHAR
   if ((options & OPT_UNICODE) && (u & ~(wchar_t)0177)) {
      char mbb[MB_LEN_MAX];
      int i, n;

      if ((n = wctomb(mbb, u)) > 0) {
         rv = wcwidth(u);
         for (i = 0; i < n; ++i)
            if (putc(mbb[i] & 0377, fp) == EOF) {
               rv = 0;
               break;
            }
      } else if (n == 0)
         rv = (putc('\0', fp) != EOF);
      else
         rv = 0;
   } else
#endif
      rv = (putc(c, fp) != EOF);
   NYD_LEAVE;
   return rv;
}

FL bool_t
bidi_info_needed(char const *bdat, size_t blen)
{
   bool_t rv = FAL0;
   NYD_ENTER;

#ifdef HAVE_NATCH_CHAR
   if (options & OPT_UNICODE)
      for (; blen > 0; ++bdat, --blen) {
         if ((ui8_t)*bdat > 0x7F) {
            /* TODO Checking for BIDI character: use S-CText fromutf8
             * TODO plus isrighttoleft (or whatever there will be)! */
            ui32_t c, x = (ui8_t)*bdat;

            if ((x & 0xE0) == 0xC0) {
               if (blen < 2)
                  break;
               blen -= 1;
               c = x & ~0xC0;
            } else if ((x & 0xF0) == 0xE0) {
               if (blen < 3)
                  break;
               blen -= 2;
               c = x & ~0xE0;
               c <<= 6;
               x = (ui8_t)*++bdat;
               c |= x & 0x7F;
            } else {
               if (blen < 4)
                  break;
               blen -= 3;
               c = x & ~0xF0;
               c <<= 6;
               x = (ui8_t)*++bdat;
               c |= x & 0x7F;
               c <<= 6;
               x = (ui8_t)*++bdat;
               c |= x & 0x7F;
            }
            c <<= 6;
            x = (ui8_t)*++bdat;
            c |= x & 0x7F;

            /* (Very very fuzzy, awaiting S-CText for good) */
            if ((c >= 0x05BE && c <= 0x08E3) ||
                  (c >= 0xFB1D && c <= 0xFEFC) ||
                  (c >= 0x10800 && c <= 0x10C48) ||
                  (c >= 0x1EE00 && c <= 0x1EEF1)) {
               rv = TRU1;
               break;
            }
         }
      }
#endif /* HAVE_NATCH_CHAR */
   NYD_LEAVE;
   return rv;
}

FL void
bidi_info_create(struct bidi_info *bip)
{
   /* Unicode: how to isolate RIGHT-TO-LEFT scripts via *headline-bidi*
    * 1.1 (Jun 1993): U+200E (E2 80 8E) LEFT-TO-RIGHT MARK
    * 6.3 (Sep 2013): U+2068 (E2 81 A8) FIRST STRONG ISOLATE,
    *                 U+2069 (E2 81 A9) POP DIRECTIONAL ISOLATE
    * Worse results seen for: U+202D "\xE2\x80\xAD" U+202C "\xE2\x80\xAC" */
   NATCH_CHAR( char const *hb; )
   NYD_ENTER;

   memset(bip, 0, sizeof *bip);
   bip->bi_start.s = bip->bi_end.s = UNCONST("");

#ifdef HAVE_NATCH_CHAR
   if ((options & OPT_UNICODE) && (hb = ok_vlook(headline_bidi)) != NULL) {
      switch (*hb) {
      case '3':
         bip->bi_pad = 2;
         /* FALLTHRU */
      case '2':
         bip->bi_start.s = bip->bi_end.s = UNCONST("\xE2\x80\x8E");
         break;
      case '1':
         bip->bi_pad = 2;
         /* FALLTHRU */
      default:
         bip->bi_start.s = UNCONST("\xE2\x81\xA8");
         bip->bi_end.s = UNCONST("\xE2\x81\xA9");
         break;
      }
      bip->bi_start.l = bip->bi_end.l = 3;
   }
#endif
   NYD_LEAVE;
}

#ifdef HAVE_COLOUR
FL void
colour_table_create(bool_t pager_used)
{
   union {char *cp; char const *ccp; void *vp; struct colour_table *ctp;} u;
   size_t i;
   struct colour_table *ct;
   NYD_ENTER;

   if (ok_blook(colour_disable) || (pager_used && !ok_blook(colour_pager)))
      goto jleave;
   else {
      char *term, *okterms;

      /* Don't use getenv(), but force copy-in into our own tables.. */
      if ((term = _var_voklook("TERM")) == NULL)
         goto jleave;
      /* terminfo rocks: if we find "color", assume it's right */
      if (strstr(term, "color") != NULL)
         goto jok;
      if ((okterms = ok_vlook(colour_terms)) == NULL)
         okterms = UNCONST(COLOUR_TERMS);
      okterms = savestr(okterms);

      i = strlen(term);
      while ((u.cp = n_strsep(&okterms, ',', TRU1)) != NULL)
         if (!strncmp(u.cp, term, i))
            goto jok;
      goto jleave;
   }

jok:
   colour_table = ct = salloc(sizeof *ct); /* XXX lex.c yet resets (FILTER!) */
   {  static struct {
         enum okeys        okey;
         enum colourspec   cspec;
         char const        *defval;
      } const map[] = {
         {ok_v_colour_msginfo,  COLOURSPEC_MSGINFO,  COLOUR_MSGINFO},
         {ok_v_colour_partinfo, COLOURSPEC_PARTINFO, COLOUR_PARTINFO},
         {ok_v_colour_from_,    COLOURSPEC_FROM_,    COLOUR_FROM_},
         {ok_v_colour_header,   COLOURSPEC_HEADER,   COLOUR_HEADER},
         {ok_v_colour_uheader,  COLOURSPEC_UHEADER,  COLOUR_UHEADER}
      };

      for (i = 0; i < NELEM(map); ++i) {
         if ((u.cp = _var_oklook(map[i].okey)) == NULL)
            u.ccp = map[i].defval;
         u.cp = _colour_iso6429(u.ccp);
         ct->ct_csinfo[map[i].cspec].l = strlen(u.cp);
         ct->ct_csinfo[map[i].cspec].s = u.cp;
      }
   }
   ct->ct_csinfo[COLOURSPEC_RESET].l = sizeof("\033[0m") -1;
   ct->ct_csinfo[COLOURSPEC_RESET].s = UNCONST("\033[0m");

   if ((u.cp = ok_vlook(colour_user_headers)) == NULL)
      u.ccp = COLOUR_USER_HEADERS;
   ct->ct_csinfo[COLOURSPEC_RESET + 1].l = i = strlen(u.ccp);
   ct->ct_csinfo[COLOURSPEC_RESET + 1].s = (i == 0) ? NULL : savestr(u.ccp);
jleave:
   NYD_LEAVE;
}

FL void
colour_put(FILE *fp, enum colourspec cs)
{
   NYD_ENTER;
   if (colour_table != NULL) {
      struct str const *cp = colour_get(cs);

      fwrite(cp->s, cp->l, 1, fp);
   }
   NYD_LEAVE;
}

FL void
colour_put_header(FILE *fp, char const *name)
{
   enum colourspec cs = COLOURSPEC_HEADER;
   struct str const *uheads;
   char *cp, *cp_base, *x;
   size_t namelen;
   NYD_ENTER;

   if (colour_table == NULL)
      goto j_leave;
   /* Normal header colours if there are no user headers */
   uheads = colour_table->ct_csinfo + COLOURSPEC_RESET + 1;
   if (uheads->s == NULL)
      goto jleave;

   /* Iterate over all entries in the *colour-user-headers* list */
   cp = ac_alloc(uheads->l +1);
   memcpy(cp, uheads->s, uheads->l +1);
   cp_base = cp;
   namelen = strlen(name);
   while ((x = n_strsep(&cp, ',', TRU1)) != NULL) {
      size_t l = (cp != NULL) ? PTR2SIZE(cp - x) - 1 : strlen(x);
      if (l == namelen && !ascncasecmp(x, name, namelen)) {
         cs = COLOURSPEC_UHEADER;
         break;
      }
   }
   ac_free(cp_base);
jleave:
   colour_put(fp, cs);
j_leave:
   NYD_LEAVE;
}

FL void
colour_reset(FILE *fp)
{
   NYD_ENTER;
   if (colour_table != NULL)
      fwrite("\033[0m", 4, 1, fp);
   NYD_LEAVE;
}

FL struct str const *
colour_get(enum colourspec cs)
{
   struct str const *rv = NULL;
   NYD_ENTER;

   if (colour_table != NULL)
      if ((rv = colour_table->ct_csinfo + cs)->s == NULL)
         rv = NULL;
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_COLOUR */

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
      if ((inlen == 1 && *inbuf == '1') ||
            !ascncasecmp(inbuf, "yes", inlen) ||
            !ascncasecmp(inbuf, "true", inlen))
         rv = 1;
      else if ((inlen == 1 && *inbuf == '0') ||
            !ascncasecmp(inbuf, "no", inlen) ||
            !ascncasecmp(inbuf, "false", inlen))
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
         (options & OPT_INTERACTIVE)) {
      if (prompt != NULL)
         fputs(prompt, stdout);
      rv = getapproval(NULL, rv);
   }
   NYD_LEAVE;
   return rv;
}

FL void
time_current_update(struct time_current *tc, bool_t full_update)
{
   NYD_ENTER;
   tc->tc_time = time(NULL);
   if (full_update) {
      memcpy(&tc->tc_gm, gmtime(&tc->tc_time), sizeof tc->tc_gm);
      memcpy(&tc->tc_local, localtime(&tc->tc_time), sizeof tc->tc_local);
      sstpcpy(tc->tc_ctime, ctime(&tc->tc_time));
   }
   NYD_LEAVE;
}

static void
_out_of_memory(void)
{
   panic("no memory");
}

#ifndef HAVE_DEBUG
FL void *
smalloc(size_t s SMALLOC_DEBUG_ARGS)
{
   void *rv;
   NYD2_ENTER;

   if (s == 0)
      s = 1;
   if ((rv = malloc(s)) == NULL)
      _out_of_memory();
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
      _out_of_memory();
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
      _out_of_memory();
   NYD2_LEAVE;
   return rv;
}

#else /* !HAVE_DEBUG */
CTA(sizeof(char) == sizeof(ui8_t));

# define _HOPE_SIZE        (2 * 8 * sizeof(char))
# define _HOPE_SET(C)   \
do {\
   union mem_ptr __xl, __xu;\
   struct mem_chunk *__xc;\
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
   union mem_ptr __xl, __xu;\
   struct mem_chunk *__xc;\
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
      alert("%p: corrupt lower canary: 0x%02X: %s, line %d",\
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
      alert("%p: corrupt upper canary: 0x%02X: %s, line %d",\
         __xl.p_p, __i, mdbg_file, mdbg_line);\
   }\
   if (BAD)\
      alert("   ..canary last seen: %s, line %" PRIu16 "",\
         __xc->mc_file, __xc->mc_line);\
} while (0)

FL void *
(smalloc)(size_t s SMALLOC_DEBUG_ARGS)
{
   union mem_ptr p;
   NYD2_ENTER;

   if (s == 0)
      s = 1;
   if (s > UI32_MAX - sizeof(struct mem_chunk) - _HOPE_SIZE)
      panic("smalloc(): allocation too large: %s, line %d\n",
         mdbg_file, mdbg_line);
   s += sizeof(struct mem_chunk) + _HOPE_SIZE;

   if ((p.p_p = (malloc)(s)) == NULL)
      _out_of_memory();
   p.p_c->mc_prev = NULL;
   if ((p.p_c->mc_next = _mem_list) != NULL)
      _mem_list->mc_prev = p.p_c;
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_size = (ui32_t)s;

   _mem_list = p.p_c++;
   _HOPE_SET(p);

   ++_mem_aall;
   ++_mem_acur;
   _mem_amax = MAX(_mem_amax, _mem_acur);
   _mem_mall += s;
   _mem_mcur += s;
   _mem_mmax = MAX(_mem_mmax, _mem_mcur);
   NYD2_LEAVE;
   return p.p_p;
}

FL void *
(srealloc)(void *v, size_t s SMALLOC_DEBUG_ARGS)
{
   union mem_ptr p;
   bool_t isbad;
   NYD2_ENTER;

   if ((p.p_p = v) == NULL) {
      p.p_p = (smalloc)(s, mdbg_file, mdbg_line);
      goto jleave;
   }

   _HOPE_GET(p, isbad);
   --p.p_c;
   if (p.p_c->mc_isfree) {
      fprintf(stderr, "srealloc(): region freed!  At %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jforce;
   }

   if (p.p_c == _mem_list)
      _mem_list = p.p_c->mc_next;
   else
      p.p_c->mc_prev->mc_next = p.p_c->mc_next;
   if (p.p_c->mc_next != NULL)
      p.p_c->mc_next->mc_prev = p.p_c->mc_prev;

   --_mem_acur;
   _mem_mcur -= p.p_c->mc_size;
jforce:
   if (s == 0)
      s = 1;
   if (s > UI32_MAX - sizeof(struct mem_chunk) - _HOPE_SIZE)
      panic("srealloc(): allocation too large: %s, line %d\n",
         mdbg_file, mdbg_line);
   s += sizeof(struct mem_chunk) + _HOPE_SIZE;

   if ((p.p_p = (realloc)(p.p_c, s)) == NULL)
      _out_of_memory();
   p.p_c->mc_prev = NULL;
   if ((p.p_c->mc_next = _mem_list) != NULL)
      _mem_list->mc_prev = p.p_c;
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_size = (ui32_t)s;
   _mem_list = p.p_c++;
   _HOPE_SET(p);

   ++_mem_aall;
   ++_mem_acur;
   _mem_amax = MAX(_mem_amax, _mem_acur);
   _mem_mall += s;
   _mem_mcur += s;
   _mem_mmax = MAX(_mem_mmax, _mem_mcur);
jleave:
   NYD2_LEAVE;
   return p.p_p;
}

FL void *
(scalloc)(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS)
{
   union mem_ptr p;
   NYD2_ENTER;

   if (size == 0)
      size = 1;
   if (nmemb == 0)
      nmemb = 1;
   if (size > UI32_MAX - sizeof(struct mem_chunk) - _HOPE_SIZE)
      panic("scalloc(): allocation size too large: %s, line %d\n",
         mdbg_file, mdbg_line);
   if ((UI32_MAX - sizeof(struct mem_chunk) - _HOPE_SIZE) / nmemb < size)
      panic("scalloc(): allocation count too large: %s, line %d\n",
         mdbg_file, mdbg_line);

   size *= nmemb;
   size += sizeof(struct mem_chunk) + _HOPE_SIZE;

   if ((p.p_p = (malloc)(size)) == NULL)
      _out_of_memory();
   memset(p.p_p, 0, size);
   p.p_c->mc_prev = NULL;
   if ((p.p_c->mc_next = _mem_list) != NULL)
      _mem_list->mc_prev = p.p_c;
   p.p_c->mc_file = mdbg_file;
   p.p_c->mc_line = (ui16_t)mdbg_line;
   p.p_c->mc_isfree = FAL0;
   p.p_c->mc_size = (ui32_t)size;
   _mem_list = p.p_c++;
   _HOPE_SET(p);

   ++_mem_aall;
   ++_mem_acur;
   _mem_amax = MAX(_mem_amax, _mem_acur);
   _mem_mall += size;
   _mem_mcur += size;
   _mem_mmax = MAX(_mem_mmax, _mem_mcur);
   NYD2_LEAVE;
   return p.p_p;
}

FL void
(sfree)(void *v SMALLOC_DEBUG_ARGS)
{
   union mem_ptr p;
   bool_t isbad;
   NYD2_ENTER;

   if ((p.p_p = v) == NULL) {
      fprintf(stderr, "sfree(NULL) from %s, line %d\n", mdbg_file, mdbg_line);
      goto jleave;
   }

   _HOPE_GET(p, isbad);
   --p.p_c;
   if (p.p_c->mc_isfree) {
      fprintf(stderr, "sfree(): double-free avoided at %s, line %d\n"
         "\tLast seen: %s, line %" PRIu16 "\n",
         mdbg_file, mdbg_line, p.p_c->mc_file, p.p_c->mc_line);
      goto jleave;
   }

   if (p.p_c == _mem_list)
      _mem_list = p.p_c->mc_next;
   else
      p.p_c->mc_prev->mc_next = p.p_c->mc_next;
   if (p.p_c->mc_next != NULL)
      p.p_c->mc_next->mc_prev = p.p_c->mc_prev;
   p.p_c->mc_isfree = TRU1;
   /* Trash contents (also see [21c05f8]) */
   memset(v, 0377, p.p_c->mc_size - sizeof(struct mem_chunk) - _HOPE_SIZE);

   --_mem_acur;
   _mem_mcur -= p.p_c->mc_size;

   if (options & (OPT_DEBUG | OPT_MEMDEBUG)) {
      p.p_c->mc_next = _mem_free;
      _mem_free = p.p_c;
   } else
      (free)(p.p_c);
jleave:
   NYD2_LEAVE;
}

FL void
smemreset(void)
{
   union mem_ptr p;
   size_t c = 0, s = 0;
   NYD_ENTER;

   smemcheck();

   for (p.p_c = _mem_free; p.p_c != NULL;) {
      void *vp = p.p_c;
      ++c;
      s += p.p_c->mc_size;
      p.p_c = p.p_c->mc_next;
      (free)(vp);
   }
   _mem_free = NULL;

   if (options & (OPT_DEBUG | OPT_MEMDEBUG))
      fprintf(stderr, "smemreset: freed %" PRIuZ " chunks/%" PRIuZ " bytes\n",
         c, s);
   NYD_LEAVE;
}

FL int
c_smemtrace(void *v)
{
   /* For _HOPE_GET() */
   char const * const mdbg_file = "smemtrace()";
   int const mdbg_line = -1;
   FILE *fp;
   union mem_ptr p, xp;
   bool_t isbad;
   size_t lines;
   NYD_ENTER;

   v = (void*)0x1;
   if ((fp = Ftmp(NULL, "memtr", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror("tmpfile");
      goto jleave;
   }

   fprintf(fp, "Memory statistics:\n"
      "  Count cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n"
      "  Bytes cur/peek/all: %7" PRIuZ "/%7" PRIuZ "/%10" PRIuZ "\n\n",
      _mem_acur, _mem_amax, _mem_aall, _mem_mcur, _mem_mmax, _mem_mall);

   fprintf(fp, "Currently allocated memory chunks:\n");
   for (lines = 0, p.p_c = _mem_list; p.p_c != NULL;
         ++lines, p.p_c = p.p_c->mc_next) {
      xp = p;
      ++xp.p_c;
      _HOPE_GET_TRACE(xp, isbad);
      fprintf(fp, "%s%p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
         (isbad ? "! CANARY ERROR: " : ""), xp.p_p,
         (size_t)(p.p_c->mc_size - sizeof(struct mem_chunk)), p.p_c->mc_file,
         p.p_c->mc_line);
   }

   if (options & (OPT_DEBUG | OPT_MEMDEBUG)) {
      fprintf(fp, "sfree()d memory chunks awaiting free():\n");
      for (p.p_c = _mem_free; p.p_c != NULL; ++lines, p.p_c = p.p_c->mc_next) {
         xp = p;
         ++xp.p_c;
         _HOPE_GET_TRACE(xp, isbad);
         fprintf(fp, "%s%p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
            (isbad ? "! CANARY ERROR: " : ""), xp.p_p,
            (size_t)(p.p_c->mc_size - sizeof(struct mem_chunk)),
            p.p_c->mc_file, p.p_c->mc_line);
      }
   }

   page_or_print(fp, lines);
   Fclose(fp);
   v = NULL;
jleave:
   NYD_LEAVE;
   return (v != NULL);
}

# ifdef HAVE_DEVEL
FL bool_t
_smemcheck(char const *mdbg_file, int mdbg_line)
{
   union mem_ptr p, xp;
   bool_t anybad = FAL0, isbad;
   size_t lines;
   NYD_ENTER;

   for (lines = 0, p.p_c = _mem_list; p.p_c != NULL;
         ++lines, p.p_c = p.p_c->mc_next) {
      xp = p;
      ++xp.p_c;
      _HOPE_GET_TRACE(xp, isbad);
      if (isbad) {
         anybad = TRU1;
         fprintf(stderr,
            "! CANARY ERROR: %p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
            xp.p_p, (size_t)(p.p_c->mc_size - sizeof(struct mem_chunk)),
            p.p_c->mc_file, p.p_c->mc_line);
      }
   }

   if (options & (OPT_DEBUG | OPT_MEMDEBUG)) {
      for (p.p_c = _mem_free; p.p_c != NULL; ++lines, p.p_c = p.p_c->mc_next) {
         xp = p;
         ++xp.p_c;
         _HOPE_GET_TRACE(xp, isbad);
         if (isbad) {
            anybad = TRU1;
            fprintf(stderr,
               "! CANARY ERROR: %p (%5" PRIuZ " bytes): %s, line %" PRIu16 "\n",
               xp.p_p, (size_t)(p.p_c->mc_size - sizeof(struct mem_chunk)),
               p.p_c->mc_file, p.p_c->mc_line);
         }
      }
   }
   NYD_LEAVE;
   return anybad;
}
# endif /* HAVE_DEVEL */

# undef _HOPE_SIZE
# undef _HOPE_SET
# undef _HOPE_GET_TRACE
# undef _HOPE_GET
#endif /* HAVE_DEBUG */

/* s-it-mode */
