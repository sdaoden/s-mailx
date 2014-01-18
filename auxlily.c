/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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
#include <fcntl.h>

#ifdef HAVE_SOCKETS
# ifdef HAVE_IPV6
#  include <sys/socket.h>
# endif

# include <netdb.h>
#endif

#ifdef HAVE_MD5
# include "md5.h"
#endif

/* Create an ISO 6429 (ECMA-48/ANSI) terminal control escape sequence */
#ifdef HAVE_COLOUR
static char *  _colour_iso6429(char const *wish);
#endif

/* {hold,rele}_all_sigs() */
static size_t     _alls_depth;
static sigset_t   _alls_nset, _alls_oset;

/* {hold,rele}_sigs() */
static size_t     _hold_sigdepth;
static sigset_t   _hold_nset, _hold_oset;

#ifdef HAVE_COLOUR
static char *
_colour_iso6429(char const *wish)
{
   char const * const wish_orig = wish;
   char *xwish, *cp, cfg[3] = {0, 0, 0};

   /* Since we use salloc(), reuse the strcomma() buffer also for the return
    * value, ensure we have enough room for that */
   {
      size_t i = strlen(wish) + 1;
      xwish = salloc(MAX(i, sizeof("\033[1;30;40m")));
      memcpy(xwish, wish, i);
      wish = xwish;
   }

   /* Iterate over the colour spec */
   while ((cp = strcomma(&xwish, TRU1)) != NULL) {
      char *y, *x = strchr(cp, '=');
      if (x == NULL) {
jbail:
         fprintf(stderr, tr(527,
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
   return UNCONST(wish);
}
#endif /* HAVE_COLOUR */

FL void
panic(char const *format, ...)
{
   va_list ap;

   fprintf(stderr, tr(1, "Panic: "));

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);

   fputs("\n", stderr);
   fflush(stderr);
   exit(EXIT_ERR);
}

#ifdef HAVE_DEBUG
FL void
warn(char const *format, ...)
{
   va_list ap;

   fprintf(stderr, tr(1, "Panic: "));

   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);

   fputs("\n", stderr);
   fflush(stderr);
}
#endif

FL sighandler_type
safe_signal(int signum, sighandler_type handler)
{
   struct sigaction nact, oact;

   nact.sa_handler = handler;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = 0;
#ifdef SA_RESTART
   nact.sa_flags |= SA_RESTART;
#endif
   return ((sigaction(signum, &nact, &oact) != 0) ? SIG_ERR : oact.sa_handler);
}

FL void
hold_all_sigs(void)
{
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
}

FL void
rele_all_sigs(void)
{
   if (--_alls_depth == 0)
      sigprocmask(SIG_SETMASK, &_alls_oset, (sigset_t*)NULL);
}

FL void
hold_sigs(void)
{
   if (_hold_sigdepth++ == 0) {
      sigemptyset(&_hold_nset);
      sigaddset(&_hold_nset, SIGHUP);
      sigaddset(&_hold_nset, SIGINT);
      sigaddset(&_hold_nset, SIGQUIT);
      sigprocmask(SIG_BLOCK, &_hold_nset, &_hold_oset);
   }
}

FL void
rele_sigs(void)
{
   if (--_hold_sigdepth == 0)
      sigprocmask(SIG_SETMASK, &_hold_oset, NULL);
}

/*
 * Touch the named message by setting its MTOUCH flag.
 * Touched messages have the effect of not being sent
 * back to the system mailbox on exit.
 */
FL void
touch(struct message *mp)
{

	mp->m_flag |= MTOUCH;
	if ((mp->m_flag & MREAD) == 0)
		mp->m_flag |= MREAD|MSTATUS;
}

/*
 * Test to see if the passed file name is a directory.
 * Return true if it is.
 */
FL int
is_dir(char const *name)
{
	struct stat sbuf;

	if (stat(name, &sbuf) < 0)
		return(0);
	return(S_ISDIR(sbuf.st_mode));
}

/*
 * Count the number of arguments in the given string raw list.
 */
FL int
argcount(char **argv)
{
	char **ap;

	for (ap = argv; *ap++ != NULL;)
		;
	return ap - argv - 1;
}

FL char *
colalign(const char *cp, int col, int fill, int *cols_decr_used_or_null)
{
	int col_orig = col, n, sz;
	char *nb, *np;

	np = nb = salloc(mb_cur_max * strlen(cp) + col + 1);
	while (*cp) {
#ifdef HAVE_WCWIDTH
		if (mb_cur_max > 1) {
			wchar_t	wc;

			if ((sz = mbtowc(&wc, cp, mb_cur_max)) < 0) {
				n = sz = 1;
			} else {
				if ((n = wcwidth(wc)) < 0)
					n = 1;
			}
		} else
#endif
		{
			n = sz = 1;
		}
		if (n > col)
			break;
		col -= n;
		if (sz == 1 && spacechar(*cp)) {
			*np++ = ' ';
			cp++;
		} else
			while (sz--)
				*np++ = *cp++;
	}

	if (fill && col != 0) {
		if (fill > 0) {
			memmove(nb + col, nb, (size_t)(np - nb));
			memset(nb, ' ', col);
		} else
			memset(np, ' ', col);
		np += col;
		col = 0;
	}

	*np = '\0';
	if (cols_decr_used_or_null != NULL)
		*cols_decr_used_or_null -= col_orig - col;
	return nb;
}

FL char const *
get_pager(void)
{
	char const *cp;

	cp = ok_vlook(PAGER);
	if (cp == NULL || *cp == '\0')
		cp = XPAGER;
	return cp;
}

FL size_t
paging_seems_sensible(void)
{
	size_t ret = 0;
	char const *cp;

	if (IS_TTY_SESSION() && (cp = ok_vlook(crt)) != NULL)
		ret = (*cp != '\0') ? (size_t)atol(cp) : (size_t)scrnheight;
	return ret;
}

FL void
page_or_print(FILE *fp, size_t lines)
{
	size_t rows;
	int c;

	fflush_rewind(fp);

	if ((rows = paging_seems_sensible()) != 0 && lines == 0) {
		while ((c = getc(fp)) != EOF)
			if (c == '\n' && ++lines > rows)
				break;
		rewind(fp);
	}

	if (rows != 0 && lines >= rows)
		run_command(get_pager(), 0, fileno(fp), -1, NULL, NULL, NULL);
	else
		while ((c = getc(fp)) != EOF)
			putchar(c);
}

FL enum protocol
which_protocol(const char *name)
{
	register const char *cp;
	char	*np;
	size_t	sz;
	struct stat	st;
	enum protocol	p;

	if (name[0] == '%' && name[1] == ':')
		name += 2;
	for (cp = name; *cp && *cp != ':'; cp++)
		if (!alnumchar(*cp&0377))
			goto file;
	if (cp[0] == ':' && cp[1] == '/' && cp[2] == '/') {
		if (strncmp(name, "pop3://", 7) == 0)
#ifdef HAVE_POP3
			return PROTO_POP3;
#else
			fprintf(stderr,
				tr(216, "No POP3 support compiled in.\n"));
#endif
		if (strncmp(name, "pop3s://", 8) == 0)
#ifdef HAVE_SSL
			return PROTO_POP3;
#else
			fprintf(stderr,
				tr(225, "No SSL support compiled in.\n"));
#endif
		if (strncmp(name, "imap://", 7) == 0)
#ifdef HAVE_IMAP
			return PROTO_IMAP;
#else
			fprintf(stderr,
				tr(269, "No IMAP support compiled in.\n"));
#endif
		if (strncmp(name, "imaps://", 8) == 0)
#ifdef HAVE_SSL
			return PROTO_IMAP;
#else
			fprintf(stderr,
				tr(225, "No SSL support compiled in.\n"));
#endif
		return PROTO_UNKNOWN;
	} else {
		/* TODO This is the de facto maildir code and thus belongs
		 * TODO into maildir! */
	file:	p = PROTO_FILE;
		np = ac_alloc((sz = strlen(name)) + 5);
		memcpy(np, name, sz + 1);
		if (stat(name, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				strcpy(&np[sz], "/tmp");
				if (stat(np, &st) == 0 && S_ISDIR(st.st_mode)) {
					strcpy(&np[sz], "/new");
					if (stat(np, &st) == 0 &&
							S_ISDIR(st.st_mode)) {
						strcpy(&np[sz], "/cur");
						if (stat(np, &st) == 0 &&
							S_ISDIR(st.st_mode))
						    p = PROTO_MAILDIR;
					}
				}
			}
		} else {
			strcpy(&np[sz], ".gz");
			if (stat(np, &st) < 0) {
				strcpy(&np[sz], ".bz2");
				if (stat(np, &st) < 0) {
					if ((cp = ok_vlook(newfolders)) != NULL &&
						strcmp(cp, "maildir") == 0)
					p = PROTO_MAILDIR;
				}
			}
		}
		ac_free(np);
		return p;
	}
}

FL ui32_t
torek_hash(char const *name)
{
   /* Chris Torek's hash.
    * NOTE: need to change *at least* create-okey-map.pl when changing the
    * algorithm!! */
	ui32_t h = 0;

	while (*name != '\0') {
		h *= 33;
		h += *name++;
	}
	return h;
}

FL unsigned
pjw(const char *cp)
{
	unsigned	h = 0, g;

	cp--;
	while (*++cp) {
		h = (h << 4 & 0xffffffff) + (*cp&0377);
		if ((g = h & 0xf0000000) != 0) {
			h = h ^ g >> 24;
			h = h ^ g;
		}
	}
	return h;
}

FL long
nextprime(long n)
{
	const long	primes[] = {
			509, 1021, 2039, 4093, 8191, 16381, 32749, 65521,
			131071, 262139, 524287, 1048573, 2097143, 4194301,
			8388593, 16777213, 33554393, 67108859, 134217689,
			268435399, 536870909, 1073741789, 2147483647
		};
	long	mprime = 7;
	size_t	i;

	for (i = 0; i < sizeof primes / sizeof *primes; i++)
		if ((mprime = primes[i]) >= (n < 65536 ? n*4 :
					n < 262144 ? n*2 : n))
			break;
	if (i == sizeof primes / sizeof *primes)
		mprime = n;	/* not so prime, but better than failure */
	return mprime;
}

FL int
expand_shell_escape(char const **s, bool_t use_nail_extensions)
{
   char const *xs = *s;
   int c, n;

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
         case '&':   c = ok_blook(bsdcompat) ? '&' : '?';   break;
         case '?':   c = exec_last_comm_error ? '1' : '0';  break;
         case '$':   c = PROMPT_DOLLAR;                     break;
         case '@':   c = PROMPT_AT;                         break;
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
   return c;
}

FL char *
getprompt(void)
{
   static char buf[PROMPT_BUFFER_SIZE];

   char *cp = buf;
   char const *ccp;

   if ((ccp = ok_vlook(prompt)) == NULL || *ccp == '\0')
      goto jleave;

   for (; PTRCMP(cp, <, buf + sizeof(buf) - 1); ++cp) {
      char const *a;
      size_t l;
      int c = expand_shell_escape(&ccp, TRU1);

      if (c > 0) {
         *cp = (char)c;
         continue;
      }
      if (c == 0 || c == PROMPT_STOP)
         break;

      a = (c == PROMPT_DOLLAR) ? account_name : displayname;
      if (a == NULL)
         a = "";
      l = strlen(a);
      if (PTRCMP(cp + l, >=, buf + sizeof(buf) - 1))
         *cp++ = '?';
      else {
         memcpy(cp, a, l);
         cp += --l;
      }
   }
jleave:
   *cp = '\0';
   return buf;
}

FL char *
nodename(int mayoverride)
{
	static char *hostname;
	struct utsname ut;
	char *hn;
#ifdef HAVE_SOCKETS
# ifdef HAVE_IPV6
	struct addrinfo hints, *res;
# else
	struct hostent *hent;
# endif
#endif

	if (mayoverride && (hn = ok_vlook(hostname)) != NULL && *hn != '\0') {
		if (hostname != NULL)
			free(hostname);
		hostname = sstrdup(hn);
	} else if (hostname == NULL) {
		uname(&ut);
		hn = ut.nodename;
#ifdef HAVE_SOCKETS
# ifdef HAVE_IPV6
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;	/* dummy */
		hints.ai_flags = AI_CANONNAME;
		if (getaddrinfo(hn, "0", &hints, &res) == 0) {
			if (res->ai_canonname != NULL) {
				size_t l = strlen(res->ai_canonname);
				hn = ac_alloc(l + 1);
				memcpy(hn, res->ai_canonname, l + 1);
			}
			freeaddrinfo(res);
		}
# else
		hent = gethostbyname(hn);
		if (hent != NULL) {
			hn = hent->h_name;
		}
# endif
#endif
		hostname = sstrdup(hn);
#if defined HAVE_SOCKETS && defined HAVE_IPV6
		if (hn != ut.nodename)
			ac_free(hn);
#endif
	}
	return (hostname);
}

FL char *
lookup_password_for_token(char const *token)
{
	size_t tl;
	char *var, *cp;

	tl = strlen(token);
	var = ac_alloc(tl + 10);

	memcpy(var, "password-", 9);
	memcpy(var + 9, token, tl);
	var[tl + 9] = '\0';

	if ((cp = vok_vlook(var)) != NULL)
		cp = savestr(cp);
	ac_free(var);
	return cp;
}

FL char *
getrandstring(size_t length)
{
	static unsigned char nodedigest[16];
	static pid_t pid;
	struct str b64;
	int fd = -1;
	char *data, *cp;
	size_t i;
#ifdef HAVE_MD5
	md5_ctx	ctx;
#else
	size_t j;
#endif

	data = ac_alloc(length);
	if ((fd = open("/dev/urandom", O_RDONLY)) < 0 ||
			length != (size_t)read(fd, data, length)) {
		if (pid == 0) {
			pid = getpid();
			srand(pid);
			cp = nodename(0);
#ifdef HAVE_MD5
			md5_init(&ctx);
			md5_update(&ctx, (unsigned char*)cp, strlen(cp));
			md5_final(nodedigest, &ctx);
#else
			/* In that case it's only used for boundaries and
			 * Message-Id:s so that srand(3) should suffice */
			j = strlen(cp) + 1;
			for (i = 0; i < sizeof(nodedigest); ++i)
				nodedigest[i] = (unsigned char)(
					cp[i % j] ^ rand());
#endif
		}
		for (i = 0; i < length; i++)
			data[i] = (char)(
				(int)(255 * (rand() / (RAND_MAX + 1.0))) ^
				nodedigest[i % sizeof nodedigest]);
	}
	if (fd >= 0)
		close(fd);

	(void)b64_encode_buf(&b64, data, length, B64_SALLOC);
	ac_free(data);
	assert(length < b64.l);
	b64.s[length] = '\0';
	return b64.s;
}

#ifdef HAVE_MD5
FL char *
md5tohex(char hex[MD5TOHEX_SIZE], void const *vp)
{
	char const *cp = vp;
	size_t i, j;

	for (i = 0; i < MD5TOHEX_SIZE / 2; i++) {
		j = i << 1;
		hex[j] = hexchar((cp[i] & 0xf0) >> 4);
		hex[++j] = hexchar(cp[i] & 0x0f);
	}
	return hex;
}

FL char *
cram_md5_string(char const *user, char const *pass, char const *b64)
{
	struct str in, out;
	char digest[16], *cp;
	size_t lu;

	out.s = NULL;
	in.s = UNCONST(b64);
	in.l = strlen(in.s);
	(void)b64_decode(&out, &in, NULL);
	assert(out.s != NULL);

	hmac_md5((unsigned char*)out.s, out.l, UNCONST(pass), strlen(pass),
		digest);
	free(out.s);
	cp = md5tohex(salloc(MD5TOHEX_SIZE + 1), digest);

	lu = strlen(user);
	in.l = lu + MD5TOHEX_SIZE +1;
	in.s = ac_alloc(lu + 1 + MD5TOHEX_SIZE +1);
	memcpy(in.s, user, lu);
	in.s[lu] = ' ';
	memcpy(in.s + lu + 1, cp, MD5TOHEX_SIZE);
	(void)b64_encode(&out, &in, B64_SALLOC|B64_CRLF);
	ac_free(in.s);
	return out.s;
}
#endif /* HAVE_MD5 */

FL enum okay
makedir(const char *name)
{
	int	e;
	struct stat	st;

	if (mkdir(name, 0700) < 0) {
		e = errno;
		if ((e == EEXIST || e == ENOSYS) &&
				stat(name, &st) == 0 &&
				(st.st_mode&S_IFMT) == S_IFDIR)
			return OKAY;
		return STOP;
	}
	return OKAY;
}

#ifdef	HAVE_FCHDIR
FL enum okay
cwget(struct cw *cw)
{
	if ((cw->cw_fd = open(".", O_RDONLY)) < 0)
		return STOP;
	if (fchdir(cw->cw_fd) < 0) {
		close(cw->cw_fd);
		return STOP;
	}
	return OKAY;
}

FL enum okay
cwret(struct cw *cw)
{
	if (fchdir(cw->cw_fd) < 0)
		return STOP;
	return OKAY;
}

FL void
cwrelse(struct cw *cw)
{
	close(cw->cw_fd);
}
#else	/* !HAVE_FCHDIR */
FL enum okay
cwget(struct cw *cw)
{
	if (getcwd(cw->cw_wd, sizeof cw->cw_wd) == NULL || chdir(cw->cw_wd) < 0)
		return STOP;
	return OKAY;
}

FL enum okay
cwret(struct cw *cw)
{
	if (chdir(cw->cw_wd) < 0)
		return STOP;
	return OKAY;
}

/*ARGSUSED*/
FL void
cwrelse(struct cw *cw)
{
	(void)cw;
}
#endif	/* !HAVE_FCHDIR */

FL void
makeprint(struct str const *in, struct str *out)
{
	static int print_all_chars = -1;
	char const *inp, *maxp;
	char *outp;
	size_t msz;

	if (print_all_chars == -1)
		print_all_chars = ok_blook(print_all_chars);

	msz = in->l + 1;
	out->s = outp = smalloc(msz);
	inp = in->s;
	maxp = inp + in->l;

	if (print_all_chars) {
		out->l = in->l;
		memcpy(outp, inp, out->l);
		goto jleave;
	}

#ifdef HAVE_C90AMEND1
	if (mb_cur_max > 1) {
		char mbb[MB_LEN_MAX + 1];
		wchar_t wc;
		int i, n;
	   size_t dist;

		out->l = 0;
		while (inp < maxp) {
			if (*inp & 0200)
				n = mbtowc(&wc, inp, maxp - inp);
			else {
				wc = *inp;
				n = 1;
			}
			if (n < 0) {
				/* FIXME Why mbtowc() resetting here?
				 * FIXME what about ISO 2022-JP plus -- those
				 * FIXME will loose shifts, then!
				 * FIXME THUS - we'd need special "known points"
				 * FIXME to do so - say, after a newline!!
				 * FIXME WE NEED TO CHANGE ALL USES +MBLEN! */
				(void)mbtowc(&wc, NULL, mb_cur_max);
				wc = utf8 ? 0xFFFD : '?';
				n = 1;
			} else if (n == 0)
				n = 1;
			inp += n;
			if (!iswprint(wc) && wc != '\n' && wc != '\r' &&
					wc != '\b' && wc != '\t') {
				if ((wc & ~(wchar_t)037) == 0)
					wc = utf8 ? 0x2400 | wc : '?';
				else if (wc == 0177)
					wc = utf8 ? 0x2421 : '?';
				else
					wc = utf8 ? 0x2426 : '?';
			}
			if ((n = wctomb(mbb, wc)) <= 0)
				continue;
			out->l += n;
			if (out->l >= msz - 1) {
				dist = outp - out->s;
				out->s = srealloc(out->s, msz += 32);
				outp = &out->s[dist];
			}
			for (i = 0; i < n; i++)
				*outp++ = mbb[i];
		}
	} else
#endif /* C90AMEND1 */
	{
		int c;
		while (inp < maxp) {
			c = *inp++ & 0377;
			if (!isprint(c) && c != '\n' && c != '\r' &&
					c != '\b' && c != '\t')
				c = '?';
			*outp++ = c;
		}
		out->l = in->l;
	}
jleave:
	out->s[out->l] = '\0';
}

FL char *
prstr(const char *s)
{
	struct str	in, out;
	char	*rp;

	in.s = UNCONST(s);
	in.l = strlen(s);
	makeprint(&in, &out);
	rp = salloc(out.l + 1);
	memcpy(rp, out.s, out.l);
	rp[out.l] = '\0';
	free(out.s);
	return rp;
}

FL int
prout(const char *s, size_t sz, FILE *fp)
{
	struct str	in, out;
	int	n;

	in.s = UNCONST(s);
	in.l = sz;
	makeprint(&in, &out);
	n = fwrite(out.s, 1, out.l, fp);
	free(out.s);
	return n;
}

FL size_t
putuc(int u, int c, FILE *fp)
{
	size_t rv;
   UNUSED(u);

#ifdef HAVE_C90AMEND1
	if (utf8 && (u & ~(wchar_t)0177)) {
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
	return rv;
}

#ifdef HAVE_COLOUR
FL void
colour_table_create(char const *pager_used)
{
   union {char *cp; char const *ccp; void *vp; struct colour_table *ctp;} u;
   size_t i;
   struct colour_table *ct;

   if (ok_blook(colour_disable))
      goto jleave;

   /* If pager, check wether it is allowed to use colour */
   if (pager_used != NULL) {
      char *pager;

      if ((u.cp = ok_vlook(colour_pagers)) == NULL)
         u.ccp = COLOUR_PAGERS;
      pager = savestr(u.cp);

      while ((u.cp = strcomma(&pager, TRU1)) != NULL)
         if (strstr(pager_used, u.cp) != NULL)
            goto jok;
      goto jleave;
   }

   /* $TERM is different in that we default to false unless whitelisted */
   {
      char *term, *okterms;

      /* Don't use getenv(), but force copy-in into our own tables.. */
      if ((term = _var_voklook("TERM")) == NULL)
         goto jleave;
      if ((okterms = ok_vlook(colour_terms)) == NULL)
         okterms = UNCONST(COLOUR_TERMS);
      okterms = savestr(okterms);

      i = strlen(term);
      while ((u.cp = strcomma(&okterms, TRU1)) != NULL)
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
   ct->ct_csinfo[COLOURSPEC_RESET].l = sizeof("\033[0m") - 1;
   ct->ct_csinfo[COLOURSPEC_RESET].s = UNCONST("\033[0m");

   if ((u.cp = ok_vlook(colour_user_headers)) == NULL)
      u.ccp = COLOUR_USER_HEADERS;
   ct->ct_csinfo[COLOURSPEC_RESET + 1].l = i = strlen(u.ccp);
   ct->ct_csinfo[COLOURSPEC_RESET + 1].s = (i == 0) ? NULL : savestr(u.ccp);
jleave:
   ;
}

FL void
colour_put(FILE *fp, enum colourspec cs)
{
   if (colour_table != NULL) {
      struct str const *cp = colour_get(cs);

      fwrite(cp->s, cp->l, 1, fp);
   }
}

FL void
colour_put_header(FILE *fp, char const *name)
{
   enum colourspec cs = COLOURSPEC_HEADER;
   struct str const *uheads;
   char *cp, *cp_base, *x;
   size_t namelen;

   if (colour_table == NULL)
      goto j_leave;
   /* Normal header colours if there are no user headers */
   uheads = colour_table->ct_csinfo + COLOURSPEC_RESET + 1;
   if (uheads->s == NULL)
      goto jleave;

   /* Iterate over all entries in the *colour-user-headers* list */
   cp = ac_alloc(uheads->l + 1);
   memcpy(cp, uheads->s, uheads->l + 1);
   cp_base = cp;
   namelen = strlen(name);
   while ((x = strcomma(&cp, TRU1)) != NULL) {
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
   ;
}

FL void
colour_reset(FILE *fp)
{
   if (colour_table != NULL)
      fwrite("\033[0m", 4, 1, fp);
}

FL struct str const *
colour_get(enum colourspec cs)
{
   struct str const *rv = NULL;

   if (colour_table != NULL)
      if ((rv = colour_table->ct_csinfo + cs)->s == NULL)
         rv = NULL;
   return rv;
}
#endif /* HAVE_COLOUR */

FL void
time_current_update(struct time_current *tc, bool_t full_update)
{
	tc->tc_time = time(NULL);
	if (full_update) {
		memcpy(&tc->tc_gm, gmtime(&tc->tc_time), sizeof tc->tc_gm);
		memcpy(&tc->tc_local, localtime(&tc->tc_time),
			sizeof tc->tc_local);
		sstpcpy(tc->tc_ctime, ctime(&tc->tc_time));
	}
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

   if (s == 0)
      s = 1;
   if ((rv = malloc(s)) == NULL)
      _out_of_memory();
   return rv;
}

FL void *
srealloc(void *v, size_t s SMALLOC_DEBUG_ARGS)
{
   void *rv;

   if (s == 0)
      s = 1;
   if (v == NULL)
      rv = smalloc(s);
   else if ((rv = realloc(v, s)) == NULL)
      _out_of_memory();
   return rv;
}

FL void *
scalloc(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS)
{
   void *rv;

   if (size == 0)
      size = 1;
   if ((rv = calloc(nmemb, size)) == NULL)
      _out_of_memory();
   return rv;
}

#else /* !HAVE_DEBUG */
CTA(sizeof(char) == sizeof(ui8_t));

# define _HOPE_SIZE        (2 * 8 * sizeof(char))
# define _HOPE_SET(C)   \
do {\
   union ptr __xl, __xu;\
   struct chunk *__xc;\
   __xl.p = (C).p;\
   __xc = __xl.c - 1;\
   __xu.p = __xc;\
   (C).cp += 8;\
   __xl.ui8p[0]=0xDE; __xl.ui8p[1]=0xAA; __xl.ui8p[2]=0x55; __xl.ui8p[3]=0xAD;\
   __xl.ui8p[4]=0xBE; __xl.ui8p[5]=0x55; __xl.ui8p[6]=0xAA; __xl.ui8p[7]=0xEF;\
   __xu.ui8p += __xc->size - 8;\
   __xu.ui8p[0]=0xDE; __xu.ui8p[1]=0xAA; __xu.ui8p[2]=0x55; __xu.ui8p[3]=0xAD;\
   __xu.ui8p[4]=0xBE; __xu.ui8p[5]=0x55; __xu.ui8p[6]=0xAA; __xu.ui8p[7]=0xEF;\
} while (0)
# define _HOPE_GET_TRACE(C,BAD) do {(C).cp += 8; _HOPE_GET(C, BAD);} while(0)
# define _HOPE_GET(C,BAD) \
do {\
   union ptr __xl, __xu;\
   struct chunk *__xc;\
   ui32_t __i;\
   __xl.p = (C).p;\
   __xl.cp -= 8;\
   (C).cp = __xl.cp;\
   __xc = __xl.c - 1;\
   (BAD) = FAL0;\
   __i = 0;\
   if (__xl.ui8p[0] != 0xDE) __i |= 1<<0;\
   if (__xl.ui8p[1] != 0xAA) __i |= 1<<1;\
   if (__xl.ui8p[2] != 0x55) __i |= 1<<2;\
   if (__xl.ui8p[3] != 0xAD) __i |= 1<<3;\
   if (__xl.ui8p[4] != 0xBE) __i |= 1<<4;\
   if (__xl.ui8p[5] != 0x55) __i |= 1<<5;\
   if (__xl.ui8p[6] != 0xAA) __i |= 1<<6;\
   if (__xl.ui8p[7] != 0xEF) __i |= 1<<7;\
   if (__i != 0) {\
      (BAD) = TRU1;\
      warn("%p: corrupted lower canary: 0x%02X: %s, line %u",\
         __xl.p, __i, mdbg_file, mdbg_line);\
   }\
   __xu.p = __xc;\
   __xu.ui8p += __xc->size - 8;\
   __i = 0;\
   if (__xu.ui8p[0] != 0xDE) __i |= 1<<0;\
   if (__xu.ui8p[1] != 0xAA) __i |= 1<<1;\
   if (__xu.ui8p[2] != 0x55) __i |= 1<<2;\
   if (__xu.ui8p[3] != 0xAD) __i |= 1<<3;\
   if (__xu.ui8p[4] != 0xBE) __i |= 1<<4;\
   if (__xu.ui8p[5] != 0x55) __i |= 1<<5;\
   if (__xu.ui8p[6] != 0xAA) __i |= 1<<6;\
   if (__xu.ui8p[7] != 0xEF) __i |= 1<<7;\
   if (__i != 0) {\
      (BAD) = TRU1;\
      warn("%p: corrupted upper canary: 0x%02X: %s, line %u",\
         __xl.p, __i, mdbg_file, mdbg_line);\
   }\
   if (BAD)\
      warn("   ..canary last seen: %s, line %u", __xc->file, __xc->line);\
} while (0)

struct chunk {
   struct chunk   *prev;
   struct chunk   *next;
   char const     *file;
   ui16_t         line;
   ui8_t          isfree;
   ui8_t          __dummy;
   ui32_t         size;
};

union ptr {
   void           *p;
   struct chunk   *c;
   char           *cp;
   ui8_t          *ui8p;
};

struct chunk   *_mlist, *_mfree;

FL void *
(smalloc)(size_t s SMALLOC_DEBUG_ARGS)
{
   union ptr p;

   if (s == 0)
      s = 1;
   s += sizeof(struct chunk) + _HOPE_SIZE;

   if ((p.p = (malloc)(s)) == NULL)
      _out_of_memory();
   p.c->prev = NULL;
   if ((p.c->next = _mlist) != NULL)
      _mlist->prev = p.c;
   p.c->file = mdbg_file;
   p.c->line = (ui16_t)mdbg_line;
   p.c->isfree = FAL0;
   p.c->size = (ui32_t)s;
   _mlist = p.c++;
   _HOPE_SET(p);
   return p.p;
}

FL void *
(srealloc)(void *v, size_t s SMALLOC_DEBUG_ARGS)
{
   union ptr p;
   bool_t isbad;

   if ((p.p = v) == NULL) {
      p.p = (smalloc)(s, mdbg_file, mdbg_line);
      goto jleave;
   }

   _HOPE_GET(p, isbad);
   --p.c;
   if (p.c->isfree) {
      fprintf(stderr, "srealloc(): region freed!  At %s, line %d\n"
         "\tLast seen: %s, line %d\n",
         mdbg_file, mdbg_line, p.c->file, p.c->line);
      goto jforce;
   }

   if (p.c == _mlist)
      _mlist = p.c->next;
   else
      p.c->prev->next = p.c->next;
   if (p.c->next != NULL)
      p.c->next->prev = p.c->prev;

jforce:
   if (s == 0)
      s = 1;
   s += sizeof(struct chunk) + _HOPE_SIZE;

   if ((p.p = (realloc)(p.c, s)) == NULL)
      _out_of_memory();
   p.c->prev = NULL;
   if ((p.c->next = _mlist) != NULL)
      _mlist->prev = p.c;
   p.c->file = mdbg_file;
   p.c->line = (ui16_t)mdbg_line;
   p.c->isfree = FAL0;
   p.c->size = (ui32_t)s;
   _mlist = p.c++;
   _HOPE_SET(p);
jleave:
   return p.p;
}

FL void *
(scalloc)(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS)
{
   union ptr p;

   if (size == 0)
      size = 1;
   if (nmemb == 0)
      nmemb = 1;
   size *= nmemb;
   size += sizeof(struct chunk) + _HOPE_SIZE;

   if ((p.p = (malloc)(size)) == NULL)
      _out_of_memory();
   memset(p.p, 0, size);
   p.c->prev = NULL;
   if ((p.c->next = _mlist) != NULL)
      _mlist->prev = p.c;
   p.c->file = mdbg_file;
   p.c->line = (ui16_t)mdbg_line;
   p.c->isfree = FAL0;
   p.c->size = (ui32_t)size;
   _mlist = p.c++;
   _HOPE_SET(p);
   return p.p;
}

FL void
(sfree)(void *v SMALLOC_DEBUG_ARGS)
{
   union ptr p;
   bool_t isbad;

   if ((p.p = v) == NULL) {
      fprintf(stderr, "sfree(NULL) from %s, line %d\n", mdbg_file, mdbg_line);
      goto jleave;
   }

   _HOPE_GET(p, isbad);
   --p.c;
   if (p.c->isfree) {
      fprintf(stderr, "sfree(): double-free avoided at %s, line %d\n"
         "\tLast seen: %s, line %d\n",
         mdbg_file, mdbg_line, p.c->file, p.c->line);
      goto jleave;
   }

   if (p.c == _mlist)
      _mlist = p.c->next;
   else
      p.c->prev->next = p.c->next;
   if (p.c->next != NULL)
      p.c->next->prev = p.c->prev;
   p.c->isfree = TRU1;

   if (options & OPT_DEBUG) {
      p.c->next = _mfree;
      _mfree = p.c;
   } else
      (free)(p.c);
jleave:
   ;
}

FL void
smemreset(void)
{
   union ptr p;
   size_t c = 0, s = 0;

   for (p.c = _mfree; p.c != NULL;) {
      void *vp = p.c;
      ++c;
      s += p.c->size;
      p.c = p.c->next;
      (free)(vp);
   }
   _mfree = NULL;

   if (options & OPT_DEBUG)
      fprintf(stderr, "smemreset(): freed %" ZFMT " chunks/%" ZFMT " bytes\n",
         c, s);
}

FL int
smemtrace(void *v)
{
   /* For _HOPE_GET() */
   char const * const mdbg_file = "smemtrace()";
   int const mdbg_line = -1;

   FILE *fp;
   char *cp;
   union ptr p, xp;
   bool_t isbad;
   size_t lines;

   v = (void*)0x1;
   if ((fp = Ftemp(&cp, "Ra", "w+", 0600, 1)) == NULL) {
      perror("tmpfile");
      goto jleave;
   }
   rm(cp);
   Ftfree(&cp);

   fprintf(fp, "Currently allocated memory chunks:\n");
   for (lines = 0, p.c = _mlist; p.c != NULL; ++lines, p.c = p.c->next) {
      xp = p;
      ++xp.c;
      _HOPE_GET_TRACE(xp, isbad);
      fprintf(fp, "%s%p (%5" ZFMT " bytes): %s, line %u\n",
         (isbad ? "! CANARY ERROR: " : ""), xp.p,
         (size_t)(p.c->size - sizeof(struct chunk)), p.c->file, p.c->line);
   }

   if (options & OPT_DEBUG) {
      fprintf(fp, "sfree()d memory chunks awaiting free():\n");
      for (p.c = _mfree; p.c != NULL; ++lines, p.c = p.c->next) {
         xp = p;
         ++xp.c;
         _HOPE_GET_TRACE(xp, isbad);
         fprintf(fp, "%s%p (%5" ZFMT " bytes): %s, line %u\n",
            (isbad ? "! CANARY ERROR: " : ""), xp.p,
            (size_t)(p.c->size - sizeof(struct chunk)), p.c->file, p.c->line);
      }
   }

   page_or_print(fp, lines);
   Fclose(fp);
   v = NULL;
jleave:
   return (v != NULL);
}

# ifdef MEMCHECK
FL bool_t
_smemcheck(char const *mdbg_file, int mdbg_line)
{
   union ptr p, xp;
   bool_t anybad = FAL0, isbad;
   size_t lines;

   for (lines = 0, p.c = _mlist; p.c != NULL; ++lines, p.c = p.c->next) {
      xp = p;
      ++xp.c;
      _HOPE_GET_TRACE(xp, isbad);
      if (isbad) {
         anybad = TRU1;
         fprintf(stderr,
            "! CANARY ERROR: %p (%5" ZFMT " bytes): %s, line %u\n",
            xp.p, (size_t)(p.c->size - sizeof(struct chunk)),
            p.c->file, p.c->line);
      }
   }

   if (options & OPT_DEBUG) {
      for (p.c = _mfree; p.c != NULL; ++lines, p.c = p.c->next) {
         xp = p;
         ++xp.c;
         _HOPE_GET_TRACE(xp, isbad);
         if (isbad) {
            anybad = TRU1;
            fprintf(stderr,
               "! CANARY ERROR: %p (%5" ZFMT " bytes): %s, line %u\n",
               xp.p, (size_t)(p.c->size - sizeof(struct chunk)),
               p.c->file, p.c->line);
         }
      }
   }
   return anybad;
}
# endif /* MEMCHECK */
#endif /* HAVE_DEBUG */

/* vim:set fenc=utf-8:s-it-mode (TODO only partial true) */
