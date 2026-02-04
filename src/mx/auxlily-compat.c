/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that do not fit anywhere else.
 *
 * Copyright (c) 2012 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE auxlily_compat
#define mx_SOURCE
#define mx_SOURCE_AUXLILY_COMPAT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
#  include <sys/socket.h>
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
#include "mx/okeys.h"

/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

FL enum protocol
which_protocol(char const *name, boole check_stat, boole try_hooks, char const **adjusted_or_nil){
	/* TODO This which_protocol() sickness should be URL::new()->protocol() */
	char const *cp, *orig_name, *real_name, *emsg;
	enum protocol rv, fixrv;
	NYD2_IN;

	rv = fixrv = n_PROTO_UNKNOWN;

	if(name[0] == '%' && name[1] == ':')
		name += 2;
	orig_name = real_name = name;

	for(cp = name; *cp && *cp != ':'; ++cp)
		if(!su_cs_is_alnum(*cp))
			goto jfile;

	if(cp[0] == ':' && cp[1] == '/' && cp[2] == '/'){ /* TODO lookup table */
		uz i;
		boole yeshooks;

		yeshooks = FAL0;
		i = P2UZ(cp - name);

#undef a_X
#undef a_Y
#define a_Y(X) sizeof(X) -1
#define a_X(X) !su_cs_cmp_case_n(name, X, a_Y(X))
		switch(i){
		default:
			break;
		case a_Y("eml"):
			if(a_X("eml")){
				rv = n_PROTO_EML;
				yeshooks = TRU1;
			}
			break;
		case a_Y("file"):
		/* case a_Y("mbox"):
		 * case a_Y("imap"):
		 * case a_Y("pop3"):*/
			if(a_X("file") || a_X("mbox")){
				rv = n_PROTO_FILE;
				yeshooks = TRU1;
			}else if(a_X("imap")){
				rv = n_PROTO_IMAP;
#ifndef mx_HAVE_IMAP
				emsg = N_("No IMAP support compiled in");
				goto jerr;
#endif
			}else if(a_X("pop3")){
				rv = n_PROTO_POP3;
#ifndef mx_HAVE_POP3
				emsg = N_("No POP3 support compiled in");
				goto jerr;
#endif
			}
			break;
		case a_Y("smbox"):
		/* case a_Y("xmbox"):
		 * case a_Y("imaps"):
		 * case a_Y("pop3s"):*/
			if(a_X("smbox")){
				rv = n_PROTO_SMBOX;
				yeshooks = TRU1;
			}else if(a_X("xmbox")){
				rv = n_PROTO_XMBOX;
				yeshooks = TRU1;
			}else if(a_X("imaps")){
				rv = n_PROTO_IMAP;
#if !defined mx_HAVE_IMAP || !defined mx_HAVE_TLS
				emsg = N_("No IMAPS support compiled in");
				goto jerr;
#endif
			}else if(a_X("pop3s")){
				rv = n_PROTO_POP3;
#if !defined mx_HAVE_POP3 || !defined mx_HAVE_TLS
				emsg = N_("No POP3S support compiled in");
				goto jerr;
#endif
			}
			break;
		case a_Y("maildir"):
			if(a_X("maildir")){
				rv = n_PROTO_MAILDIR;
#ifndef mx_HAVE_MAILDIR
				emsg = N_("No Maildir support compiled in");
				goto jerr;
#endif
			}
			break;
		}
#undef a_X
#undef a_Y

		real_name = name = &cp[3];

		if(yeshooks){
			fixrv = rv;
			goto jcheck;
		}
	}else{
jfile:
		rv = n_PROTO_FILE;
jcheck:
		if(check_stat || try_hooks){
			struct su_pathinfo pi;
			struct mx_filetype ft;

			if(su_pathinfo_stat(&pi, name)){
				ASSERT(rv == n_PROTO_FILE);

				if(su_pathinfo_is_reg(&pi)){
				}else{
					static char const pp[3][3 +1] = {"cur", "new", "tmp"};
					char const (*ccpp)[3 +1];
					char *np;
					uz i;

					if(!su_pathinfo_is_dir(&pi)){
						/* But treat su_path_null in a special way! <> n_PO_BATCH_FLAG */
						if(!su_cs_cmp(name, su_path_null))
							goto jleave;
						emsg = N_("Unknown mailbox type");
						goto jerr;
					}
					rv = n_PROTO_MAILDIR;

					i = su_cs_len(name);
					np = su_LOFI_ALLOC(i + 1 + 3 +1);
					su_mem_copy(np, name, i);
					np[i++] = '/';
					np[i + 3] = '\0';

					for(ccpp = &pp[0];; ++ccpp){
						np[i + 0] = (*ccpp)[0];
						np[i + 1] = (*ccpp)[1];
						np[i + 2] = (*ccpp)[2];
						if(!su_pathinfo_stat(&pi, np) || !su_pathinfo_is_dir(&pi)){
							ccpp = NIL;
							break;
						}
						if(ccpp == &pp[NELEM(pp) - 1])
							break;
					}

					su_LOFI_FREE(np);

					if(ccpp == NIL){
						emsg = N_("Not a Maildir (needs cur/, new/ and tmp/ subdirectories)");
						goto jerr;
					}
#ifndef mx_HAVE_MAILDIR
					emsg = N_("No Maildir support compiled in");
					goto jerr;
#endif
				}
			}else if(try_hooks && mx_filetype_trial(&ft, name)){
				real_name = savecatsep(name, '.', ft.ft_ext_dat);
				if(fixrv != n_PROTO_UNKNOWN)
					rv = fixrv;
			}else if(fixrv == n_PROTO_UNKNOWN &&
					(cp = ok_vlook(newfolders)) != NIL && !su_cs_cmp_case(cp, "maildir")){
				rv = n_PROTO_MAILDIR;
#ifndef mx_HAVE_MAILDIR
				emsg = N_("*newfolders*: no Maildir support compiled in");
				goto jerr;
#endif
			}

			if(fixrv != n_PROTO_UNKNOWN && fixrv != rv){
				emsg = N_("Given protocol mismatches reality");
				goto jerr;
			}
		}
	}

jleave:
	if(adjusted_or_nil != NIL)
		*adjusted_or_nil = real_name;

	NYD2_OU;
	return rv;

jerr:
	n_err("%s: %s\n", V_(emsg), n_shexp_quote_cp(orig_name, FAL0));
	rv = n_PROTO_UNKNOWN;
	goto jleave;
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
		STRUCT_ZERO(struct addrinfo, &hints);
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
#endif

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
