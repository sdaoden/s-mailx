/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that don't fit anywhere else.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.	All rights reserved.
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
#define su_FILE auxlily_compat
#define mx_SOURCE
#define mx_SOURCE_AUXLILY_COMPAT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
#	include <sys/socket.h>
# endif
#endif

#include <fcntl.h>

#ifdef mx_HAVE_NET
# include <netdb.h>
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>

#include "mx/cmd-filetype.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

FL enum protocol
which_protocol(char const *name, boole check_stat, boole try_hooks, char const **adjusted_or_nil){
	/* TODO This which_protocol() sickness should be URL::new()->protocol() */
	char const *cp, *orig_name;
	enum protocol rv, fixrv;
	NYD2_IN;

	rv = fixrv = PROTO_UNKNOWN;

	if(name[0] == '%' && name[1] == ':')
		name += 2;
	orig_name = name;

	for(cp = name; *cp && *cp != ':'; cp++)
		if(!su_cs_is_alnum(*cp))
			goto jfile;

	if(cp[0] == ':' && cp[1] == '/' && cp[2] == '/'){ /* TODO lookup table */
		uz i;
		boole yeshooks;

		yeshooks = FAL0;
		i = P2UZ(cp - name);

		if((i == sizeof("file") -1 && !su_cs_cmp_case_n(name, "file", sizeof("file") -1)) ||
				(i == sizeof("mbox") -1 && !su_cs_cmp_case_n(name, "mbox", sizeof("mbox") -1))){
			yeshooks = TRU1;
			rv = PROTO_FILE;
		}else if(i == sizeof("eml") -1 && !su_cs_cmp_case_n(name, "eml", sizeof("eml") -1)){
			yeshooks = TRU1;
			rv = n_PROTO_EML;
		}else if(i == sizeof("maildir") -1 && !su_cs_cmp_case_n(name, "maildir", sizeof("maildir") -1)){
#ifdef mx_HAVE_MAILDIR
			rv = PROTO_MAILDIR;
#else
			n_err(_("No Maildir directory support compiled in\n"));
#endif
		}else if(i == sizeof("pop3") -1 && !su_cs_cmp_case_n(name, "pop3", sizeof("pop3") -1)){
#ifdef mx_HAVE_POP3
			rv = PROTO_POP3;
#else
			n_err(_("No POP3 support compiled in\n"));
#endif
		}else if(i == sizeof("pop3s") -1 && !su_cs_cmp_case_n(name, "pop3s", sizeof("pop3s") -1)){
#if defined mx_HAVE_POP3 && defined mx_HAVE_TLS
			rv = PROTO_POP3;
#else
			n_err(_("No POP3S support compiled in\n"));
#endif
		}else if(i == sizeof("imap") -1 && !su_cs_cmp_case_n(name, "imap", sizeof("imap") -1)){
#ifdef mx_HAVE_IMAP
			rv = PROTO_IMAP;
#else
			n_err(_("No IMAP support compiled in\n"));
#endif
		}else if(i == sizeof("imaps") -1 && !su_cs_cmp_case_n(name, "imaps", sizeof("imaps") -1)){
#if defined mx_HAVE_IMAP && defined mx_HAVE_TLS
			rv = PROTO_IMAP;
#else
			n_err(_("No IMAPS support compiled in\n"));
#endif
		}

		orig_name = name = &cp[3];

		if(yeshooks){
			fixrv = rv;
			goto jcheck;
		}
	}else{
jfile:
		rv = PROTO_FILE;
jcheck:
		if(check_stat || try_hooks){
			struct su_pathinfo pi;
			struct mx_filetype ft;
			char *np;
			uz i;

			np = su_LOFI_ALLOC((i = su_cs_len(name)) + 4 +1);
			su_mem_copy(np, name, i +1);

			if(su_pathinfo_stat(&pi, name)){
				if(su_pathinfo_is_dir(&pi)
#ifdef mx_HAVE_MAILDIR
						&& (su_mem_copy(&np[i], "/tmp", 5),
							su_pathinfo_stat(&pi, np) && su_pathinfo_is_dir(&pi)) &&
						(su_mem_copy(&np[i], "/new", 5),
							su_pathinfo_stat(&pi, np) && su_pathinfo_is_dir(&pi)) &&
						(su_mem_copy(&np[i], "/cur", 5),
							su_pathinfo_stat(&pi, np) && su_pathinfo_is_dir(&pi))
#endif
				){
					rv =
#ifdef mx_HAVE_MAILDIR
							PROTO_MAILDIR
#else
							PROTO_UNKNOWN
#endif
					;
				}
			}else if(try_hooks && mx_filetype_trial(&ft, name)){
				orig_name = savecatsep(name, '.', ft.ft_ext_dat);
				if(fixrv != PROTO_UNKNOWN)
					rv = fixrv;
			}else if(fixrv == PROTO_UNKNOWN && (cp = ok_vlook(newfolders)) != NIL && !su_cs_cmp_case(cp, "maildir")){
				rv =
#ifdef mx_HAVE_MAILDIR
						PROTO_MAILDIR
#else
						PROTO_UNKNOWN
#endif
				;
#ifndef mx_HAVE_MAILDIR
				n_err(_("*newfolders*: no Maildir support compiled in\n"));
#endif
			}

			su_LOFI_FREE(np);

			if(fixrv != PROTO_UNKNOWN && fixrv != rv)
				rv = PROTO_UNKNOWN;
		}
	}

	if(adjusted_or_nil != NIL)
		*adjusted_or_nil = orig_name;

	NYD2_OU;
	return rv;
}

FL char *
n_nodename(boole mayoverride){
	static char *sys_hostname, *hostname; /* XXX free-at-exit */

	struct utsname ut;
	char *hn;
#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
	struct addrinfo hints, *res;
# else
	struct hostent *hent;
# endif
#endif
	NYD2_IN;

	if(mayoverride && (hn = ok_vlook(hostname)) != NIL && *hn != '\0'){
	}else if(su_state_has(su_STATE_REPRODUCIBLE)){
		hn = UNCONST(char*,su_reproducible_build);
	}else if((hn = sys_hostname) == NIL){
		boole lofi;

		lofi = FAL0;
		uname(&ut);
		hn = ut.nodename;

#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
		su_mem_set(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_flags = AI_CANONNAME;
		if(getaddrinfo(hn, NIL, &hints, &res) == 0){
			if(res->ai_canonname != NIL){
				uz l;

				l = su_cs_len(res->ai_canonname) +1;
				hn = su_LOFI_ALLOC(l);
				lofi = TRU1;
				su_mem_copy(hn, res->ai_canonname, l);
			}
			freeaddrinfo(res);
		}
# else
		hent = gethostbyname(hn);
		if(hent != NIL)
			hn = hent->h_name;
# endif
#endif /* mx_HAVE_NET */

		/* Ensure it is non-empty! */
		if(hn[0] == '\0')
			hn = UNCONST(char*,n_LOCALHOST_DEFAULT_NAME);

#ifdef mx_HAVE_IDNA
		/* C99 */{
			struct n_string cnv;

			n_string_creat(&cnv);
			if(!n_idna_to_ascii(&cnv, hn, UZ_MAX))
				n_panic(_("The system hostname is invalid, IDNA conversion failed: %s\n"),
					n_shexp_quote_cp(hn, FAL0));
			sys_hostname = n_string_cp(&cnv);
			n_string_drop_ownership(&cnv);
			/*n_string_gut(&cnv);*/
		}
#else
		sys_hostname = su_cs_dup(hn, 0);
#endif

		if(lofi)
			su_LOFI_FREE(hn);
		hn = sys_hostname;
	}

	if(hostname != NIL && hostname != sys_hostname)
		su_FREE(hostname);
	hostname = su_cs_dup(hn, 0);

	NYD2_OU;
	return hostname;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_AUXLILY_COMPAT
/* s-itt-mode */
