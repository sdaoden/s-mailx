/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of names.h.
 *@ XXX Use a su_cs_set for alternates stuff?
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause XXX ISC once yank stuff+ changed
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
#define su_FILE names
#define mx_SOURCE
#define mx_SOURCE_NAMES /* XXX a lie - it is rather n_ yet */

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <pwd.h>

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/sort.h>

#include "mx/cmd.h"
#include "mx/cmd-ali-alt.h"
#include "mx/cmd-mlist.h"
#include "mx/compat.h"
#include "mx/go.h"
#include "mx/mime.h"
#include "mx/mta-aliases.h"
#include "mx/ui-str.h"

#include "mx/names.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Must be OUTSIDE enum gfield */
#define a_NM_ISSINGLE_HACK (1u<<30)

/* Extract (list of) name(s): workhorse for mx_name_parse(_as_one)?() */
static struct mx_name *a_nm_parse(char const *line, BITENUM(u32,gfield) ntype, boole not_a_list);

/* Allocate a single element of a name list, initialize its name field to the passed name and return it. */
static struct mx_name *a_nm_alloc(char const *str, BITENUM(u32,gfield) ntype);

/* */
static enum mx_expand_addr_flags a_nm_expandaddr_to_eaf(void);

/* Grab a single name (liberal name) */
static char const *a_nm_yankname(char const *ap, char *wbuf, char const *separators, int keepcomms);

/* Skin *name* and extract *addr-spec* according to RFC 5322 and enum gfield.
 * Store the result in .ag_skinned and also fill in those .ag_ fields that have
 * actually been seen.
 * Return NIL on error, or name again, but which may have been replaced by
 * a version with fixed quotation etc.! */
static char const *a_nm_addrspec_with_guts(struct n_addrguts *agp, char const *name, u32 gfield);

/* Classify and check a (possibly skinned) header body according to RFC
 * *addr-spec* rules; if it (is assumed to has been) skinned it may however be
 * also a file or a pipe command, so check that first, then.
 * Otherwise perform content checking and isolate the domain part (for IDNA) */
static boole a_nm_addrspec_check(struct n_addrguts *agp, boole skinned, boole issingle_hack);

/* Convert the domain part of a skinned address to IDNA.
 * If an error occurs before Unicode information is available, revert the IDNA
 * error to a normal CHAR one so that the error message doesn't talk Unicode */
#ifdef mx_HAVE_IDNA
static struct n_addrguts *a_nm_idna_apply(struct n_addrguts *agp);
#endif

/* Fetch next potentially normalized character from ID *cp */
static char a_nm_msgid_stepc(char const **cp, boole *in_id_right);

/* namelist_elide() helper */
static su_sz a_nm_elide_sort(void const *s1, void const *s2);

static struct mx_name *
a_nm_parse(char const *line, BITENUM(u32,gfield) ntype, boole not_a_list){ /* {{{ */
	char const *seps;
	boole keepcomms;
	char *shell_buf, *yank_buf;
	struct mx_name *rv, *tailp, *np;
	NYD_IN;

	rv = NIL;

	if(!(ntype & GSHEXP_PARSE_HACK) || !(a_nm_expandaddr_to_eaf() & mx_EAF_SHEXP_PARSE))
		shell_buf = NIL;
	else{
		struct str sin;
		struct n_string s_b, *s;
		BITENUM(u32,n_shexp_state) shs;

		su_mem_bag_auto_snap_create(su_MEM_BAG_SELF);

		s = n_string_creat_auto(&s_b);
		sin.s = UNCONST(char*,line); /* logical */
		sin.l = UZ_MAX;
		shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG |
				n_SHEXP_PARSE_IGN_EMPTY |
				n_SHEXP_PARSE_QUOTE_AUTO_FIXED | n_SHEXP_PARSE_QUOTE_AUTO_DSQ),
				mx_SCOPE_NONE, s, &sin, NIL);
		if(!(shs & n_SHEXP_STATE_ERR_MASK) && (shs & n_SHEXP_STATE_STOP)){
			line = shell_buf = su_LOFI_ALLOC(s->s_len +1);
			su_mem_copy(shell_buf, n_string_cp(s), s->s_len +1);
		}else
			line = shell_buf = NIL;

		su_mem_bag_auto_snap_gut(su_MEM_BAG_SELF);
	}

	if(line == NIL || *line == '\0')
		goto jleave;

	if(ntype & GREF){
		seps = " \t,";
		keepcomms = FAL0;
		ntype &= ~GFULLEXTRA;
	}else if(not_a_list || (ntype & GSPECIAL) || su_cs_first_of(line, ",\"\\(<|") != UZ_MAX){
		seps = ",";
		keepcomms = TRU1;
	}else{
		seps = " \t,";
		/*ntype &= ~GFULLEXTRA;*/
		keepcomms = FAL0;
	}

	if(not_a_list){
		/* XXX Our stupid address parser does not dig leading wS */
		while(su_cs_is_space(*line))
			++line;
		rv = a_nm_alloc(line, ntype | a_NM_ISSINGLE_HACK);
	}else{
		char const *cp;

		yank_buf = su_LOFI_ALLOC(su_cs_len(cp = line) +1);

		for(tailp = NIL; ((cp = a_nm_yankname(cp, yank_buf, seps, keepcomms)) != NIL);){
			if((np = a_nm_alloc(yank_buf, ntype)) != NIL){
				if((np->n_blink = tailp) != NIL)
					tailp->n_flink = np;
				else
					rv = np;
				tailp = np;
			}
		}

		su_LOFI_FREE(yank_buf);
	}

jleave:
	if(shell_buf != NIL)
		su_LOFI_FREE(shell_buf);

	NYD_OU;
	return rv;
} /* }}} */

static struct mx_name *
a_nm_alloc(char const *str, BITENUM(u32,gfield) ntype){ /* {{{ */
	struct n_addrguts ag;
	struct str in, out;
	struct mx_name *np;
	NYD_IN;

	if(!(ntype & (GTO | GCC | GBCC | GIDENT)))
		ntype &= ~GFULLEXTRA;

	str = a_nm_addrspec_with_guts(&ag, str, ntype);
	if(str == NIL){
		if(!(ntype & GTRASH_HACK)){
			np = NIL;
			goto jleave;
		}
	}
	str = ag.ag_input; /* Take the possibly reordered thing */

	if (!(ag.ag_n_flags & mx_NAME_NAME_SALLOC)) {
		ag.ag_n_flags |= mx_NAME_NAME_SALLOC;
		np = n_autorec_alloc(sizeof(*np) + ag.ag_slen +1);
		su_mem_copy(np + 1, ag.ag_skinned, ag.ag_slen +1);
		ag.ag_skinned = (char*)(np + 1);
	} else
		np = n_autorec_alloc(sizeof *np);

	np->n_flink = NIL;
	np->n_blink = NIL;
	np->n_type = ntype;
	np->n_fullname = np->n_name = ag.ag_skinned;
	np->n_fullextra = NIL;
	np->n_flags = ag.ag_n_flags;

	if(ntype & (GTO | GCC | GBCC | GIDENT)){
		if (ag.ag_ilen == ag.ag_slen
#ifdef mx_HAVE_IDNA
				&& !(ag.ag_n_flags & mx_NAME_IDNA)
#endif
		)
			goto jleave;
		if (ag.ag_n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE)
			goto jleave;

		/* n_fullextra is only the complete name part without address.
		 * Beware of "-r '<abc@def>'", don't treat that as FULLEXTRA */
		if ((ntype & GFULLEXTRA) && ag.ag_ilen > ag.ag_slen + 2) {
			uz s = ag.ag_iaddr_start, e = ag.ag_iaddr_aend, i;
			char const *cp;

			if (s == 0 || str[--s] != '<' || str[e++] != '>')
				goto jskipfullextra;

			i = ag.ag_ilen - e;
			in.s = su_LOFI_ALLOC(s + 1 + i +1);

			while(s > 0 && su_cs_is_blank(str[s - 1]))
				--s;
			su_mem_copy(in.s, str, s);
			if (i > 0) {
				in.s[s++] = ' ';
				while (su_cs_is_blank(str[e])) {
					++e;
					if (--i == 0)
						break;
				}
				if (i > 0)
					su_mem_copy(&in.s[s], &str[e], i);
			}
			s += i;
			in.s[in.l = s] = '\0';

			/* TODO iconv <> *mime-utf8-detect* instead of *ttycharset*? how? */
			if(!mx_mime_display_from_header(&in, &out,
					mx_MIME_DISPLAY_ICONV /* TODO | mx_MIME_DISPLAY_ISPRINT */)){
				out.s = UNCONST(char*,su_empty);
				out.l = 0;
			}

			for (cp = out.s, i = out.l; i > 0 && su_cs_is_space(*cp); --i, ++cp)
				;
			while (i > 0 && su_cs_is_space(cp[i - 1]))
				--i;
			np->n_fullextra = savestrbuf(cp, i);

			if(out.s != su_empty)
				su_FREE(out.s);

			su_LOFI_FREE(in.s);
		}
jskipfullextra:

		/* n_fullname depends on IDNA conversion */
#ifdef mx_HAVE_IDNA
		if (!(ag.ag_n_flags & mx_NAME_IDNA)) {
#endif
			in.s = UNCONST(char*,str);
			in.l = ag.ag_ilen;
#ifdef mx_HAVE_IDNA
		} else {
			/* The domain name was IDNA and has been converted.  We also have to
			 * ensure that the domain name in .n_fullname is replaced with the
			 * converted version, since MIME doesn't perform encoding of addrs */
			/* TODO This definitily doesn't belong here! */
			uz l, lsuff;

			l = ag.ag_iaddr_start;
			lsuff = ag.ag_ilen - ag.ag_iaddr_aend;

			in.s = su_LOFI_ALLOC(l + ag.ag_slen + lsuff +1);

			su_mem_copy(in.s, str, l);
			su_mem_copy(in.s + l, ag.ag_skinned, ag.ag_slen);
			l += ag.ag_slen;
			su_mem_copy(in.s + l, str + ag.ag_iaddr_aend, lsuff);
			l += lsuff;
			in.s[l] = '\0';
			in.l = l;
		}
#endif

		if(mx_mime_display_from_header(&in, &out,
				mx_MIME_DISPLAY_ICONV /* TODO | mx_MIME_DISPLAY_ISPRINT */)){
			np->n_fullname = savestr(out.s);
			su_FREE(out.s);
		}

#ifdef mx_HAVE_IDNA
		if(ag.ag_n_flags & mx_NAME_IDNA)
			n_lofi_free(in.s);
#endif
	}

jleave:
	NYD_OU;
	return np;
} /* }}} */

static enum mx_expand_addr_flags
a_nm_expandaddr_to_eaf(void){ /* TODO should happen at var assignment time {{{ */
	struct eafdesc{
		char eafd_name[15];
		boole eafd_is_target;
		u32 eafd_andoff;
		u32 eafd_or;
	} const eafa[] = {
		{"restrict", FAL0, mx_EAF_TARGET_MASK, mx_EAF_RESTRICT | mx_EAF_RESTRICT_TARGETS},
		{"fail", FAL0, mx_EAF_NONE, mx_EAF_FAIL},
		{"failinvaddr\0", FAL0, mx_EAF_NONE, mx_EAF_FAILINVADDR | mx_EAF_ADDR},
		{"domaincheck\0", FAL0, mx_EAF_NONE, mx_EAF_DOMAINCHECK | mx_EAF_ADDR},
		{"nametoaddr", FAL0, mx_EAF_NONE, mx_EAF_NAMETOADDR},
		{"shquote", FAL0, mx_EAF_NONE, mx_EAF_SHEXP_PARSE},
		{"all", TRU1, mx_EAF_NONE, mx_EAF_TARGET_MASK},
			{"fcc", TRU1, mx_EAF_NONE, mx_EAF_FCC}, /* Fcc: only */
			{"file", TRU1, mx_EAF_NONE, mx_EAF_FILE | mx_EAF_FCC}, /* Fcc: + other addr */
			{"pipe", TRU1, mx_EAF_NONE, mx_EAF_PIPE}, /* TODO No Pcc: yet! */
			{"name", TRU1, mx_EAF_NONE, mx_EAF_NAME},
			{"addr", TRU1, mx_EAF_NONE, mx_EAF_ADDR}
	}, *eafp;

	char *buf;
	enum mx_expand_addr_flags rv;
	char const *cp;
	NYD2_IN;

	if((cp = ok_vlook(expandaddr)) == NIL)
		rv = mx_EAF_RESTRICT_TARGETS;
	else if(*cp == '\0')
		rv = mx_EAF_TARGET_MASK;
	else{
		rv = mx_EAF_TARGET_MASK;

		for(buf = savestr(cp); (cp = su_cs_sep_c(&buf, ',', TRU1)) != NIL;){
			boole minus;

			if((minus = (*cp == '-')) || (*cp == '+' ? (minus = TRUM1) : FAL0))
				++cp;

			for(eafp = eafa;; ++eafp){
				if(eafp == &eafa[NELEM(eafa)]){
					if(n_poption & n_PO_D_V)
						n_err(_("Unknown *expandaddr* value: %s\n"), cp);
					break;
				}else if(!su_cs_cmp_case(cp, eafp->eafd_name)){
					if(minus){
						if(eafp->eafd_is_target){
							if(minus != TRU1)
								goto jandor;
							else
								rv &= ~eafp->eafd_or;
						}else if(n_poption & n_PO_D_V)
							n_err(_("- or + prefix invalid for *expandaddr* value: %s\n"),
								--cp);
					}else{
jandor:
						rv &= ~eafp->eafd_andoff;
						rv |= eafp->eafd_or;
					}
					break;
				}else if(!su_cs_cmp_case(cp, "noalias")){ /* TODO v15 OBSOLETE */
					n_OBSOLETE(_("*expandaddr*: noalias is henceforth -name"));
					rv &= ~mx_EAF_NAME;
					break;
				}else if(!su_cs_cmp_case(cp, "namehostex")){ /* TODO v15 OBSOLETE*/
					n_OBSOLETE(_("*expandaddr*: "
						"weird namehostex renamed to nametoaddr, "
						"sorry for the inconvenience!"));
					rv |= mx_EAF_NAMETOADDR;
					break;
				}
			}
		}

		if((rv & mx_EAF_RESTRICT) && ((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_TILDE_FLAG)))
			rv |= mx_EAF_TARGET_MASK;
		else if(n_poption & n_PO_D_V){
			if(!(rv & mx_EAF_TARGET_MASK))
				n_err(_("*expandaddr* does not allow any addressees\n"));
			else if((rv & mx_EAF_FAIL) && (rv & mx_EAF_TARGET_MASK) == mx_EAF_TARGET_MASK)
				n_err(_("*expandaddr* with fail, but no restrictions to apply\n"));
		}
	}

	NYD2_OU;
	return rv;
} /* }}} */

static char const *
a_nm_yankname(char const *ap, char *wbuf, char const *separators, int keepcomms){ /* {{{ */
	char const *cp;
	char *wp, c, inquote, lc, lastsp;
	NYD2_IN;

	*(wp = wbuf) = '\0';

	/* Skip over intermediate list trash, as in "ADDR1[ \t,  ]+ADDR2 */
	for(c = *ap; su_cs_is_blank(c) || c == ','; c = *++ap){
	}
	if(c == '\0'){
		cp = NIL;
		goto jleave;
	}

	/* Parse a full name: TODO RFC 5322
	 * - Keep everything in quotes, liberal handle *quoted-pair*s therein
	 * - Skip entire (nested) comments
	 * - In non-quote, non-comment, join adjacent space to a single SP
	 * - Understand separators only in non-quote, non-comment context,
	 *   and only if not part of a *quoted-pair* (XXX too liberal) */
	cp = ap;
	for(inquote = lc = lastsp = 0;; lc = c, ++cp){
		c = *cp;
		if(c == '\0')
			break;
		if(c == '\\')
			goto jwpwc;
		if(c == '"'){
			if(lc != '\\')
				inquote = !inquote;
#if 0 /* TODO when doing real RFC 5322 parsers - why have i done this? */
			else
				--wp;
#endif
			goto jwpwc;
		}
		if(inquote || lc == '\\'){
jwpwc:
			*wp++ = c;
			lastsp = 0;
			continue;
		}
		if(c == '('){
			ap = cp;
			cp = mx_name_skip_comment_cp(cp + 1);
			if(keepcomms)
				while(ap < cp)
					*wp++ = *ap++;
			--cp;
			lastsp = 0;
			continue;
		}
		if(su_cs_find_c(separators, c) != NIL)
			break;

		lc = lastsp;
		lastsp = su_cs_is_blank(c);
		if(!lastsp || !lc)
			*wp++ = c;
	}
	if(su_cs_is_blank(lc))
		--wp;

	*wp = '\0';
jleave:
	NYD2_OU;
	return cp;
} /* }}} */

/* TODO addrspec_with_guts: RFC 5322
 * TODO addrspec_with_guts: trim whitespace ETC. ETC. ETC. BAD BAD BAD!!! */
static char const *
a_nm_addrspec_with_guts(struct n_addrguts *agp, char const *name, u32 gfield){ /* {{{ */
	char const *cp;
	char *cp2, *bufend, *nbuf, c;
	enum{
		a_NONE,
		a_DOSKIN = 1u<<0,
		a_NOLIST = 1u<<1,
		a_QUENCO = 1u<<2,

		a_GOTLT = 1u<<3,
		a_GOTADDR = 1u<<4,
		a_GOTSPACE = 1u<<5,
		a_LASTSP = 1u<<6,
		a_IDX0 = 1u<<7
	} flags;
	NYD_IN;

jredo_uri:
	STRUCT_ZERO(struct n_addrguts, agp);
	UNINIT(agp->ag_n_flags, 0);

	if((agp->ag_input = name) == NIL || (agp->ag_ilen = su_cs_len(name)) == 0){
		agp->ag_skinned = n_UNCONST(n_empty); /* ok: mx_NAME_SALLOC is not set */
		agp->ag_slen = 0;
		agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
				mx_NAME_ADDRSPEC_ERR_EMPTY, '\0');
		goto jleave;
	}

	flags = a_DOSKIN;
	if(gfield & a_NM_ISSINGLE_HACK)
		flags |= a_NOLIST;
	if(gfield & GQUOTE_ENCLOSED_OK){
		gfield ^= GQUOTE_ENCLOSED_OK;
		flags |= a_QUENCO;
	}

	if(!(flags & a_DOSKIN)){
		/*agp->ag_iaddr_start = 0;*/
		agp->ag_iaddr_aend = agp->ag_ilen;
		agp->ag_skinned = n_UNCONST(name); /* (mx_NAME_SALLOC not set) */
		agp->ag_slen = agp->ag_ilen;
		agp->ag_n_flags = mx_NAME_SKINNED;
		goto jcheck;
	}

	/* We will skin that thing */
	nbuf = n_lofi_alloc(agp->ag_ilen +1);
	/*agp->ag_iaddr_start = 0;*/
	cp2 = bufend = nbuf;

	/* TODO This is complete crap and should use a token parser.
	 * TODO It can be fooled and is too stupid to find an email address in
	 * TODO something valid unless it contains <>.	oh my */
	for(cp = name++; (c = *cp++) != '\0';){
		switch (c) {
		case '(':
			cp = mx_name_skip_comment_cp(cp);
			flags &= ~a_LASTSP;
			break;
		case '"':
			/* Start of a "quoted-string".  Copy it in its entirety */
			/* XXX RFC: quotes are "semantically invisible"
			 * XXX But it was explicitly added (Changelog.Heirloom,
			 * XXX [9.23] released 11/15/00, "Do not remove quotes
			 * XXX when skinning names"?	No more info.. */
			*cp2++ = c;
			ASSERT(!(flags & a_IDX0));
			if((flags & a_QUENCO) && cp == name)
				flags |= a_IDX0;
			while ((c = *cp) != '\0') { /* TODO improve */
				++cp;
				if (c == '"') {
					*cp2++ = c;
					/* Special case: if allowed so and anything is placed in quotes
					 * then throw away the quotes and start all over again */
					if((flags & a_IDX0) && *cp == '\0'){
						name = savestrbuf(name, P2UZ(--cp - name));
						goto jredo_uri;
					}
					break;
				}
				if (c != '\\')
					*cp2++ = c;
				else if ((c = *cp) != '\0') {
					*cp2++ = c;
					++cp;
				}
			}
			flags &= ~(a_LASTSP | a_IDX0);
			break;
		case ' ':
		case '\t':
			if((flags & (a_GOTADDR | a_GOTSPACE)) == a_GOTADDR){
				flags |= a_GOTSPACE;
				agp->ag_iaddr_aend = P2UZ(cp - name);
			}
			if (cp[0] == 'a' && cp[1] == 't' && su_cs_is_blank(cp[2]))
				cp += 3, *cp2++ = '@';
			else if (cp[0] == '@' && su_cs_is_blank(cp[1]))
				cp += 2, *cp2++ = '@';
			else
				flags |= a_LASTSP;
			break;
		case '<':
			agp->ag_iaddr_start = P2UZ(cp - (name - 1));
			cp2 = bufend;
			flags &= ~(a_GOTSPACE | a_LASTSP);
			flags |= a_GOTLT | a_GOTADDR;
			break;
		case '>':
			if(flags & a_GOTLT){
				/* (_addrspec_check() verifies these later!) */
				flags &= ~(a_GOTLT | a_LASTSP);
				agp->ag_iaddr_aend = P2UZ(cp - name);

				/* Skip over the entire remaining field */
				while((c = *cp) != '\0'){
					if(c == ',' && !(flags & a_NOLIST))
						break;
					++cp;
					if (c == '(')
						cp = mx_name_skip_comment_cp(cp);
					else if (c == '"')
						while ((c = *cp) != '\0') {
							++cp;
							if (c == '"')
								break;
							if (c == '\\' && *cp != '\0')
								++cp;
						}
				}
				break;
			}
			/* FALLTHRU */
		default:
			if(flags & a_LASTSP){
				flags &= ~a_LASTSP;
				if(flags & a_GOTADDR)
					*cp2++ = ' ';
			}
			*cp2++ = c;
			/* This character is forbidden here, but it may nonetheless be
			 * present: ensure we turn this into something valid!  (E.g., if the
			 * next character would be a "..) */
			if(c == '\\' && *cp != '\0')
				*cp2++ = *cp++;
			else if(c == ','){
				if(!(flags & a_NOLIST)){
					if(!(flags & a_GOTLT)){
						*cp2++ = ' ';
						for(; su_cs_is_blank(*cp); ++cp){
						}
						flags &= ~a_LASTSP;
						bufend = cp2;
					}
				}
			}else if(!(flags & a_GOTADDR)){
				flags |= a_GOTADDR;
				agp->ag_iaddr_start = P2UZ(cp - name);
			}
		}
	}
	agp->ag_slen = P2UZ(cp2 - nbuf);

	if (agp->ag_iaddr_aend == 0)
		agp->ag_iaddr_aend = agp->ag_ilen;
	/* Misses > */
	else if (agp->ag_iaddr_aend < agp->ag_iaddr_start) {
		cp2 = n_autorec_alloc(agp->ag_ilen + 1 +1);
		su_mem_copy(cp2, agp->ag_input, agp->ag_ilen);
		agp->ag_iaddr_aend = agp->ag_ilen;
		cp2[agp->ag_ilen++] = '>';
		cp2[agp->ag_ilen] = '\0';
		agp->ag_input = cp2;
	}

	agp->ag_skinned = savestrbuf(nbuf, agp->ag_slen);
	n_lofi_free(nbuf);
	agp->ag_n_flags = mx_NAME_NAME_SALLOC | mx_NAME_SKINNED;
jcheck:
	if(a_nm_addrspec_check(agp, ((flags & a_DOSKIN) != 0), ((flags & a_NOLIST) != 0)) <= FAL0)
		name = NIL;
	else
		name = agp->ag_input;
jleave:
	NYD_OU;
	return name;
} /* }}} */

static boole
a_nm_addrspec_check(struct n_addrguts *agp, boole skinned, boole issingle_hack){ /* {{{ */
	char *addr, *p;
	union {boole b; char c; unsigned char u; u32 ui32; s32 si32;} c;
	enum{
		a_NONE,
		a_IDNA_ENABLE = 1u<<0,
		a_IDNA_APPLY = 1u<<1,
		a_REDO_NODE_AFTER_ADDR = 1u<<2,
		a_RESET_MASK = a_IDNA_ENABLE | a_IDNA_APPLY | a_REDO_NODE_AFTER_ADDR,
		a_IN_QUOTE = 1u<<8,
		a_IN_AT = 1u<<9,
		a_IN_DOMAIN = 1u<<10,
		a_DOMAIN_V6 = 1u<<11,
		a_DOMAIN_MASK = a_IN_DOMAIN | a_DOMAIN_V6
	} flags;
	NYD_IN;

	flags = a_NONE;
#ifdef mx_HAVE_IDNA
	if(!ok_blook(idna_disable))
		flags = a_IDNA_ENABLE;
#endif

	if (agp->ag_iaddr_aend - agp->ag_iaddr_start == 0) {
		agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
				mx_NAME_ADDRSPEC_ERR_EMPTY, '\0');
		goto jleave;
	}

	addr = agp->ag_skinned;

	/* If the field is not a recipient, it cannot be a file or a pipe */
	if (!skinned)
		goto jaddr_check;

	/* When changing any of the following adjust any RECIPIENTADDRSPEC;
	 * grep the latter for the complete picture */
	if (*addr == '|') {
		agp->ag_n_flags |= mx_NAME_ADDRSPEC_ISPIPE;
		goto jleave;
	}
	if (addr[0] == '/' || (addr[0] == '.' && addr[1] == '/') ||
			(addr[0] == '-' && addr[1] == '\0'))
		goto jisfile;
	if (su_mem_find(addr, '@', agp->ag_slen) == NIL) {
		if (*addr == '+')
			goto jisfile;
		for (p = addr; (c.c = *p); ++p) {
			if (c.c == '!' || c.c == '%')
				break;
			if (c.c == '/') {
jisfile:
				agp->ag_n_flags |= mx_NAME_ADDRSPEC_ISFILE;
				goto jleave;
			}
		}
	}

jaddr_check:
	/* TODO This is false.	If super correct this should work on wide
	 * TODO characters, just in case (some bytes of) the ASCII set is (are)
	 * TODO shared; it may yet tear apart multibyte sequences, possibly.
	 * TODO All this should interact with mime_enc_mustquote(), too!
	 * TODO That is: once this is an object, we need to do this in a way
	 * TODO that it is valid for the wire format (instead)! */
	/* TODO addrspec_check: we need a real RFC 5322 (un)?structured parser!
	 * TODO Note this correlats with addrspec_with_guts() which is in front
	 * TODO of us and encapsulates (what it thinks is, sigh) the address
	 * TODO boundary.  ALL THIS should be one object that knows how to deal */
	flags &= a_RESET_MASK;
	for (p = addr; (c.c = *p++) != '\0';) {
		if (c.c == '"') {
			flags ^= a_IN_QUOTE;
		} else if (c.u < 040 || c.u >= 0177) { /* TODO no magics: !bodychar()? */
#ifdef mx_HAVE_IDNA
			if ((flags & (a_IN_DOMAIN | a_IDNA_ENABLE)) ==
					(a_IN_DOMAIN | a_IDNA_ENABLE))
				flags |= a_IDNA_APPLY;
			else
#endif
				break;
		} else if ((flags & a_DOMAIN_MASK) == a_DOMAIN_MASK) {
			if ((c.c == ']' && *p != '\0') || c.c == '\\' || su_cs_is_white(c.c))
				break;
		} else if ((flags & (a_IN_QUOTE | a_DOMAIN_MASK)) == a_IN_QUOTE) {
			/*EMPTY*/;
		} else if (c.c == '\\' && *p != '\0') {
			++p;
		} else if (c.c == '@') {
			if(flags & a_IN_AT){
				agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
						mx_NAME_ADDRSPEC_ERR_ATSEQ, c.u);
				goto jleave;
			}
			agp->ag_sdom_start = P2UZ(p - addr);
			agp->ag_n_flags |= mx_NAME_ADDRSPEC_ISADDR; /* TODO .. really? */
			flags &= ~a_DOMAIN_MASK;
			flags |= (*p == '[') ? a_IN_AT | a_IN_DOMAIN | a_DOMAIN_V6
					: a_IN_AT | a_IN_DOMAIN;
			continue;
		}
		/* TODO This interferes with our alias handling, which allows :!
		 * TODO Update manual on support (search the several ALIASCOLON)! */
		else if (c.c == '(' || c.c == ')' || c.c == '<' || c.c == '>' ||
				c.c == '[' || c.c == ']' || c.c == ':' || c.c == ';' ||
				c.c == '\\' || c.c == ',' || su_cs_is_blank(c.c))
			break;
		flags &= ~a_IN_AT;
	}
	if (c.c != '\0') {
		agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
				mx_NAME_ADDRSPEC_ERR_CHAR, c.u);
		goto jleave;
	}

	/* If we do not think this is an address we may treat it as an alias name
	 * if and only if the original input is identical to the skinned version */
	if(!(agp->ag_n_flags & mx_NAME_ADDRSPEC_ISADDR) &&
			!su_cs_cmp(agp->ag_skinned, agp->ag_input)){
		/* TODO This may be an UUCP address */
		agp->ag_n_flags |= mx_NAME_ADDRSPEC_ISNAME;
		if(!mx_alias_is_valid_name(agp->ag_input))
			agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
					mx_NAME_ADDRSPEC_ERR_NAME, '.');
	}else{
		/* If we seem to know that this is an address.	Ensure this is correct
		 * according to RFC 5322 TODO the entire address parser should be like
		 * TODO that for one, and then we should know whether structured or
		 * TODO unstructured, and just parse correctly overall!
		 * TODO In addition, this can be optimised a lot.
		 * TODO And it is far from perfect: it should not forget whether no
		 * TODO whitespace followed some snippet, and it was written hastily.
		 * TODO It is even wrong sometimes.  Not only for strange cases */
		struct a_token{
			struct a_token *t_last;
			struct a_token *t_next;
			enum{
				a_T_TATOM = 1u<<0,
				a_T_TCOMM = 1u<<1,
				a_T_TQUOTE = 1u<<2,
				a_T_TADDR = 1u<<3,
				a_T_TMASK = (1u<<4) - 1,

				a_T_SPECIAL = 1u<<8		/* An atom actually needs to go TQUOTE */
			} t_f;
			u8 t__pad[4];
			uz t_start;
			uz t_end;
		} *thead, *tcurr, *tp;

		struct n_string ost, *ostp;
		char const *cp, *cp1st, *cpmax, *xp;
		void *lofi_snap;

		/* Name and domain must be non-empty */
		if(*addr == '@' || &addr[2] >= p || p[-2] == '@'){
jeat:
			c.c = '@';
			agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
					mx_NAME_ADDRSPEC_ERR_ATSEQ, c.u);
			goto jleave;
		}

		cp = agp->ag_input;

		/* Nothing to do if there is only an address (in angle brackets) */
		/* TODO This is wrong since we allow invalid constructs in local-part
		 * TODO and domain, AT LEAST in so far as a"bc"d@abc should become
		 * TODO "abcd"@abc.	Etc. */
		if(agp->ag_iaddr_start == 0){
			/* No @ seen? */
			if(!(agp->ag_n_flags & mx_NAME_ADDRSPEC_ISADDR))
				goto jeat;
			if(agp->ag_iaddr_aend == agp->ag_ilen)
				goto jleave;
		}else if(agp->ag_iaddr_start == 1 && *cp == '<' &&
				agp->ag_iaddr_aend == agp->ag_ilen - 1 &&
				cp[agp->ag_iaddr_aend] == '>'){
			/* No @ seen?	Possibly insert n_nodename() */
			if(!(agp->ag_n_flags & mx_NAME_ADDRSPEC_ISADDR)){
				cp = &agp->ag_input[agp->ag_iaddr_start];
				cpmax = &agp->ag_input[agp->ag_iaddr_aend];
				goto jinsert_domain;
			}
			goto jleave;
		}

		/* It is not, so parse off all tokens, then resort and rejoin */
		lofi_snap = n_lofi_snap_create();

		cp1st = cp;
		if((c.ui32 = agp->ag_iaddr_start) > 0)
			--c.ui32;
		cpmax = &cp[c.ui32];

		thead = tcurr = NIL;
jnode_redo:
		for(tp = NIL; cp < cpmax;){
			switch((c.c = *cp)){
			case '(':
				if(tp != NIL)
					tp->t_end = P2UZ(cp - cp1st);
				tp = n_lofi_alloc(sizeof *tp);
				tp->t_next = NIL;
				if((tp->t_last = tcurr) != NIL)
					tcurr->t_next = tp;
				else
					thead = tp;
				tcurr = tp;
				tp->t_f = a_T_TCOMM;
				tp->t_start = P2UZ(++cp - cp1st);
				xp = mx_name_skip_comment_cp(cp);
				tp->t_end = P2UZ(xp - cp1st);
				cp = xp;
				if(tp->t_end > tp->t_start){
					if(xp[-1] == ')')
						--tp->t_end;
					else{
						/* No closing comment - strip trailing whitespace */
						while(su_cs_is_blank(*--xp))
							if(--tp->t_end == tp->t_start)
								break;
					}
				}
				tp = NIL;
				break;

			case '"':
				if(tp != NIL)
					tp->t_end = P2UZ(cp - cp1st);
				tp = n_lofi_alloc(sizeof *tp);
				tp->t_next = NIL;
				if((tp->t_last = tcurr) != NIL)
					tcurr->t_next = tp;
				else
					thead = tp;
				tcurr = tp;
				tp->t_f = a_T_TQUOTE;
				tp->t_start = P2UZ(++cp - cp1st);
				for(xp = cp; xp < cpmax; ++xp){
					if((c.c = *xp) == '"')
						break;
					if(c.c == '\\' && xp[1] != '\0')
						++xp;
				}
				tp->t_end = P2UZ(xp - cp1st);
				cp = &xp[1];
				if(tp->t_end > tp->t_start){
					/* No closing quote - strip trailing whitespace */
					if(*xp != '"'){
						while(su_cs_is_blank(*xp--))
							if(--tp->t_end == tp->t_start)
								break;
					}
				}
				tp = NIL;
				break;

			default:
				if(su_cs_is_blank(c.c)){
					if(tp != NIL)
						tp->t_end = P2UZ(cp - cp1st);
					tp = NIL;
					++cp;
					break;
				}

				if(tp == NIL){
					tp = n_lofi_alloc(sizeof *tp);
					tp->t_next = NIL;
					if((tp->t_last = tcurr) != NIL)
						tcurr->t_next = tp;
					else
						thead = tp;
					tcurr = tp;
					tp->t_f = a_T_TATOM;
					tp->t_start = P2UZ(cp - cp1st);
				}
				++cp;

				/* Reverse solidus transforms the following into a quoted-pair, and
				 * therefore (must occur in comment or quoted-string only) the
				 * entire atom into a quoted string */
				if(c.c == '\\'){
					tp->t_f |= a_T_SPECIAL;
					if(cp < cpmax)
						++cp;
					break;
				}

				/* Is this plain RFC 5322 "atext", or "specials"?
				 * TODO Because we don't know structured/unstructured, nor anything
				 * TODO else, we need to treat "dot-atom" as being identical to
				 * TODO "specials".
				 * However, if the 8th bit is set, this will be RFC 2047 converted
				 * and the entire sequence is skipped */
				if(!(c.u & 0x80) && !su_cs_is_alnum(c.c) &&
						c.c != '!' && c.c != '#' && c.c != '$' && c.c != '%' &&
						c.c != '&' && c.c != '\'' && c.c != '*' && c.c != '+' &&
						c.c != '-' && c.c != '/' && c.c != '=' && c.c != '?' &&
						c.c != '^' && c.c != '_' && c.c != '`' && c.c != '{' &&
						c.c != '}' && c.c != '|' && c.c != '}' && c.c != '~')
					tp->t_f |= a_T_SPECIAL;
				break;
			}
		}
		if(tp != NIL)
			tp->t_end = P2UZ(cp - cp1st);

		if(!(flags & a_REDO_NODE_AFTER_ADDR)){
			flags |= a_REDO_NODE_AFTER_ADDR;

			/* The local-part may be in quotes.. */
			if((tp = tcurr) != NIL && (tp->t_f & a_T_TQUOTE) &&
					tp->t_end == agp->ag_iaddr_start - 1){
				/* ..so backward extend it, including the starting quote */
				/* TODO This is false and the code below #if 0 away.	We would
				 * TODO need to create a properly quoted local-part HERE AND NOW
				 * TODO and REPLACE the original data with that version, but the
				 * TODO current code cannot do that.  The node needs the data,
				 * TODO not only offsets for that, for example.  If we had all that
				 * TODO the code below could produce a really valid thing */
				if(tp->t_start > 0)
					--tp->t_start;
				if(tp->t_start > 0 &&
						(tp->t_last == NIL || tp->t_last->t_end < tp->t_start) &&
							agp->ag_input[tp->t_start - 1] == '\\')
					--tp->t_start;
				tp->t_f = a_T_TADDR | a_T_SPECIAL;
			}else{
				tp = n_lofi_alloc(sizeof *tp);
				tp->t_next = NIL;
				if((tp->t_last = tcurr) != NIL)
					tcurr->t_next = tp;
				else
					thead = tp;
				tcurr = tp;
				tp->t_f = a_T_TADDR;
				tp->t_start = agp->ag_iaddr_start;
				/* TODO Very special case because of our hacky non-object-based and
				 * TODO non-compliant address parser.	Note */
				if(tp->t_last == NIL && tp->t_start > 0)
					tp->t_start = 0;
				if(agp->ag_input[tp->t_start] == '<')
					++tp->t_start;

				/* TODO Very special check for whether we need to massage the
				 * TODO local part.	This is wrong, but otherwise even more so */
#if 0
				cp = &agp->ag_input[tp->t_start];
				cpmax = &agp->ag_input[agp->ag_iaddr_aend];
				while(cp < cpmax){
					c.c = *cp++;
					if(!(c.u & 0x80) && !su_cs_is_alnum(c.c) &&
							c.c != '!' && c.c != '#' && c.c != '$' && c.c != '%' &&
							c.c != '&' && c.c != '\'' && c.c != '*' && c.c != '+' &&
							c.c != '-' && c.c != '/' && c.c != '=' && c.c != '?' &&
							c.c != '^' && c.c != '_' && c.c != '`' && c.c != '{' &&
							c.c != '}' && c.c != '|' && c.c != '}' && c.c != '~'){
						tp->t_f |= a_T_SPECIAL;
						break;
					}
				}
#endif
			}
			tp->t_end = agp->ag_iaddr_aend;
			ASSERT(tp->t_start <= tp->t_end);
			tp = NIL;

			cp = &agp->ag_input[agp->ag_iaddr_aend + 1];
			cpmax = &agp->ag_input[agp->ag_ilen];
			if(cp < cpmax)
				goto jnode_redo;
		}

		/* Nothing may follow the address, move it to the end */
		ASSERT(tcurr != NIL);
		if(tcurr != NIL && !(tcurr->t_f & a_T_TADDR)){
			for(tp = thead; tp != NIL; tp = tp->t_next){
				if(tp->t_f & a_T_TADDR){
					if(tp->t_last != NIL)
						tp->t_last->t_next = tp->t_next;
					else
						thead = tp->t_next;
					if(tp->t_next != NIL)
						tp->t_next->t_last = tp->t_last;

					tcurr = tp;
					while(tp->t_next != NIL)
						tp = tp->t_next;
					tp->t_next = tcurr;
					tcurr->t_last = tp;
					tcurr->t_next = NIL;
					break;
				}
			}
		}

		/* Make ranges contiguous: ensure a continuous range of atoms is
		 * converted to a SPECIAL one if at least one of them requires it */
		for(tp = thead; tp != NIL; tp = tp->t_next){
			if(tp->t_f & a_T_SPECIAL){
				tcurr = tp;
				while((tp = tp->t_last) != NIL && (tp->t_f & a_T_TATOM))
					tp->t_f |= a_T_SPECIAL;
				tp = tcurr;
				while((tp = tp->t_next) != NIL && (tp->t_f & a_T_TATOM))
					tp->t_f |= a_T_SPECIAL;
				if(tp == NIL)
					break;
			}
		}

		/* And yes, we want quotes to extend as much as possible */
		for(tp = thead; tp != NIL; tp = tp->t_next){
			if(tp->t_f & a_T_TQUOTE){
				tcurr = tp;
				while((tp = tp->t_last) != NIL && (tp->t_f & a_T_TATOM))
					tp->t_f |= a_T_SPECIAL;
				tp = tcurr;
				while((tp = tp->t_next) != NIL && (tp->t_f & a_T_TATOM))
					tp->t_f |= a_T_SPECIAL;
				if(tp == NIL)
					break;
			}
		}

		/* Then rejoin */
		ostp = n_string_creat_auto(&ost);
		if((c.ui32 = agp->ag_ilen) <= U32_MAX >> 1)
			ostp = n_string_reserve(ostp, c.ui32 <<= 1);

		for(tcurr = thead; tcurr != NIL;){
			if(tcurr != thead)
				ostp = n_string_push_c(ostp, ' ');
			if(tcurr->t_f & a_T_TADDR){
				if(tcurr->t_last != NIL)
					ostp = n_string_push_c(ostp, '<');
				agp->ag_iaddr_start = ostp->s_len;
				/* Now it is terrible to say, but if that thing contained
				 * quotes, then those may contain quoted-pairs! */
#if 0
				if(!(tcurr->t_f & a_T_SPECIAL)){
#endif
					ostp = n_string_push_buf(ostp, &cp1st[tcurr->t_start],
							(tcurr->t_end - tcurr->t_start));
#if 0
				}else{
					boole quot, esc;

					ostp = n_string_push_c(ostp, '"');
					quot = TRU1;

					cp = &cp1st[tcurr->t_start];
					cpmax = &cp1st[tcurr->t_end];
					for(esc = FAL0; cp < cpmax;){
						if((c.c = *cp++) == '\\' && !esc){
							if(cp < cpmax && (*cp == '"' || *cp == '\\'))
								esc = TRU1;
						}else{
							if(esc || c.c == '"')
								ostp = n_string_push_c(ostp, '\\');
							else if(c.c == '@'){
								ostp = n_string_push_c(ostp, '"');
								quot = FAL0;
							}
							ostp = n_string_push_c(ostp, c.c);
							esc = FAL0;
						}
					}
				}
#endif
				agp->ag_iaddr_aend = ostp->s_len;

				if(tcurr->t_last != NIL)
					ostp = n_string_push_c(ostp, '>');
				tcurr = tcurr->t_next;
			}else if(tcurr->t_f & a_T_TCOMM){
				ostp = n_string_push_c(ostp, '(');
				ostp = n_string_push_buf(ostp, &cp1st[tcurr->t_start],
						(tcurr->t_end - tcurr->t_start));
				while((tp = tcurr->t_next) != NIL && (tp->t_f & a_T_TCOMM)){
					tcurr = tp;
					ostp = n_string_push_c(ostp, ' '); /* XXX may be artificial */
					ostp = n_string_push_buf(ostp, &cp1st[tcurr->t_start],
							(tcurr->t_end - tcurr->t_start));
				}
				ostp = n_string_push_c(ostp, ')');
				tcurr = tcurr->t_next;
			}else if(tcurr->t_f & a_T_TQUOTE){
jput_quote:
				ostp = n_string_push_c(ostp, '"');
				tp = tcurr;
				do/* while tcurr && TATOM||TQUOTE */{
					cp = &cp1st[tcurr->t_start];
					cpmax = &cp1st[tcurr->t_end];
					if(cp == cpmax)
						continue;

					if(tcurr != tp)
						ostp = n_string_push_c(ostp, ' ');

					if((tcurr->t_f & (a_T_TATOM | a_T_SPECIAL)) == a_T_TATOM)
						ostp = n_string_push_buf(ostp, cp, P2UZ(cpmax - cp));
					else{
						boole esc;

						for(esc = FAL0; cp < cpmax;){
							if((c.c = *cp++) == '\\' && !esc){
								if(cp < cpmax && (*cp == '"' || *cp == '\\'))
									esc = TRU1;
							}else{
								if(esc || c.c == '"'){
jput_quote_esc:
									ostp = n_string_push_c(ostp, '\\');
								}
								ostp = n_string_push_c(ostp, c.c);
								esc = FAL0;
							}
						}
						if(esc){
							c.c = '\\';
							goto jput_quote_esc;
						}
					}
				}while((tcurr = tcurr->t_next) != NIL &&
					(tcurr->t_f & (a_T_TATOM | a_T_TQUOTE)));
				ostp = n_string_push_c(ostp, '"');
			}else if(tcurr->t_f & a_T_SPECIAL)
				goto jput_quote;
			else{
				/* Can we use a fast join mode? */
				for(tp = tcurr; tcurr != NIL; tcurr = tcurr->t_next){
					if(!(tcurr->t_f & a_T_TATOM))
						break;
					if(tcurr != tp)
						ostp = n_string_push_c(ostp, ' ');
					ostp = n_string_push_buf(ostp, &cp1st[tcurr->t_start],
							(tcurr->t_end - tcurr->t_start));
				}
			}
		}

		n_lofi_snap_gut(lofi_snap);

		agp->ag_input = n_string_cp(ostp);
		agp->ag_ilen = ostp->s_len;
		/*ostp = n_string_drop_ownership(ostp);*/

		/* Name and domain must be non-empty, the second */
		cp = &agp->ag_input[agp->ag_iaddr_start];
		cpmax = &agp->ag_input[agp->ag_iaddr_aend];
		if(*cp == '@' || &cp[2] > cpmax || cpmax[-1] == '@'){
			c.c = '@';
			agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
					mx_NAME_ADDRSPEC_ERR_ATSEQ, c.u);
			goto jleave;
		}

		addr = agp->ag_skinned = savestrbuf(cp, P2UZ(cpmax - cp));

		/* TODO This parser is a mess.  We do not know whether this is truly
		 * TODO valid, and all our checks are not truly RFC conforming.
		 * TODO Do check the skinned thing by itself once more, in order
		 * TODO to catch problems from reordering, e.g., this additional
		 * TODO test catches a final address without AT..
		 * TODO This is a plain copy+paste of the weird thing above, no care */
		agp->ag_n_flags &= ~mx_NAME_ADDRSPEC_ISADDR;
		flags &= a_RESET_MASK;
		for (p = addr; (c.c = *p++) != '\0';) {
			if(c.c == '"')
				flags ^= a_IN_QUOTE;
			else if (c.u < 040 || c.u >= 0177) {
#ifdef mx_HAVE_IDNA
					if(!(flags & a_IN_DOMAIN))
#endif
						break;
			} else if ((flags & a_DOMAIN_MASK) == a_DOMAIN_MASK) {
				if ((c.c == ']' && *p != '\0') || c.c == '\\' ||
						su_cs_is_white(c.c))
					break;
			} else if ((flags & (a_IN_QUOTE | a_DOMAIN_MASK)) == a_IN_QUOTE) {
				/*EMPTY*/;
			} else if (c.c == '\\' && *p != '\0') {
				++p;
			} else if (c.c == '@') {
				if(flags & a_IN_AT){
					agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
							mx_NAME_ADDRSPEC_ERR_ATSEQ, c.u);
					goto jleave;
				}
				flags |= a_IN_AT;
				agp->ag_n_flags |= mx_NAME_ADDRSPEC_ISADDR; /* TODO .. really? */
				flags &= ~a_DOMAIN_MASK;
				flags |= (*p == '[') ? a_IN_DOMAIN | a_DOMAIN_V6 : a_IN_DOMAIN;
				continue;
			} else if (c.c == '(' || c.c == ')' || c.c == '<' || c.c == '>' ||
					c.c == '[' || c.c == ']' || c.c == ':' || c.c == ';' ||
					c.c == '\\' || c.c == ',' || su_cs_is_blank(c.c))
				break;
			flags &= ~a_IN_AT;
		}

		if(c.c != '\0')
			agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
					mx_NAME_ADDRSPEC_ERR_CHAR, c.u);
		else if(!(agp->ag_n_flags & mx_NAME_ADDRSPEC_ISADDR)){
			/* This is not an address, but if we had seen angle brackets convert
			 * it to a n_nodename() address if the name is a valid user */
jinsert_domain:
			if(cp > &agp->ag_input[0] && cp[-1] == '<' &&
					cpmax <= &agp->ag_input[agp->ag_ilen] && cpmax[0] == '>'){
				if(su_cs_cmp(addr, ok_vlook(LOGNAME)) && getpwnam(addr) == NIL){
					agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
							mx_NAME_ADDRSPEC_ERR_NAME, '*');
					goto jleave;
				}

				/* XXX However, if hostname is set to the empty string this
				 * XXX indicates that the used *mta* will perform the
				 * XXX auto-expansion instead.  Not so with `addrcodec' though */
				agp->ag_n_flags |= mx_NAME_ADDRSPEC_ISADDR;
				if(!issingle_hack && (cp = ok_vlook(hostname)) != NIL && *cp == '\0')
					agp->ag_n_flags |= mx_NAME_ADDRSPEC_WITHOUT_DOMAIN;
				else{
					c.ui32 = su_cs_len(cp = n_nodename(TRU1));
					/* This is yet IDNA converted.. */
					ostp = n_string_creat_auto(&ost);
					ostp = n_string_assign_buf(ostp, agp->ag_input, agp->ag_ilen);
					ostp = n_string_insert_c(ostp, agp->ag_iaddr_aend++, '@');
					ostp = n_string_insert_buf(ostp, agp->ag_iaddr_aend, cp,
							c.ui32);
					agp->ag_iaddr_aend += c.ui32;
					agp->ag_input = n_string_cp(ostp);
					agp->ag_ilen = ostp->s_len;
					/*ostp = n_string_drop_ownership(ostp);*/

					cp = &agp->ag_input[agp->ag_iaddr_start];
					cpmax = &agp->ag_input[agp->ag_iaddr_aend];
					agp->ag_skinned = savestrbuf(cp, P2UZ(cpmax - cp));
				}
			}else
				agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
						mx_NAME_ADDRSPEC_ERR_ATSEQ, '@');
		}
	}

jleave:
#ifdef mx_HAVE_IDNA
	if(!(agp->ag_n_flags & mx_NAME_ADDRSPEC_INVALID) && (flags & a_IDNA_APPLY))
		agp = a_nm_idna_apply(agp);
#endif
	NYD_OU;
	return !(agp->ag_n_flags & mx_NAME_ADDRSPEC_INVALID);
} /* }}} */

#ifdef mx_HAVE_IDNA
static struct n_addrguts *
a_nm_idna_apply(struct n_addrguts *agp){ /* {{{ */
	struct n_string idna_ascii;
	NYD2_IN;

	n_string_creat_auto(&idna_ascii);

	if(!n_idna_to_ascii(&idna_ascii, &agp->ag_skinned[agp->ag_sdom_start], agp->ag_slen - agp->ag_sdom_start))
		agp->ag_n_flags ^= mx_NAME_ADDRSPEC_ERR_IDNA | mx_NAME_ADDRSPEC_ERR_CHAR;
	else{
		/* Replace the domain part of .ag_skinned with IDNA version */
		n_string_unshift_buf(&idna_ascii, agp->ag_skinned, agp->ag_sdom_start);

		agp->ag_skinned = n_string_cp(&idna_ascii);
		agp->ag_slen = idna_ascii.s_len;
		agp->ag_n_flags = mx_name_flags_set_err(agp->ag_n_flags,
				(mx_NAME_NAME_SALLOC | mx_NAME_SKINNED | mx_NAME_IDNA), '\0');
	}

	NYD2_OU;
	return agp;
} /* }}} */
#endif /* mx_HAVE_IDNA */

static char
a_nm_msgid_stepc(char const **cp, boole *in_id_right){ /* {{{ */
	char c;
	NYD2_IN;

jredo:
	c = *(*cp)++;
	switch(c){
	case '\0':
		break;
	/* Regardless, any WS stops processing */
	case ' ': FALLTHRU
	case '\t': FALLTHRU
	case '>':
		c = '\0';
		break;
	case '(':
		*cp = mx_name_skip_comment_cp(&(*cp)[0]);
		goto jredo;
	case '@':
		*in_id_right = TRU1;
		break;
	default:
		if(*in_id_right)
			c = su_cs_to_lower(c);
		break;
	}

	NYD2_OU;
	return c;
} /* }}} */

static su_sz
a_nm_elide_sort(void const *s1, void const *s2){
	struct mx_name const *np1, *np2;
	su_sz rv;
	NYD2_IN;

	np1 = s1;
	np2 = s2;

	if(!(rv = su_cs_cmp_case(np1->n_name, np2->n_name))){
		LCTAV(GTO < GCC && GCC < GBCC);
		rv = (np1->n_type & (GTO | GCC | GBCC)) - (np2->n_type & (GTO | GCC | GBCC));
	}

	NYD2_OU;
	return rv;
}

int
c_addrcodec(void *vp){ /* {{{ */
	struct n_string s_b, *s;
	u8 mode;
	char const *cp;
	struct mx_cmd_arg *cap;
	struct mx_cmd_arg_ctx *cacp;
	NYD_IN;

	n_pstate_err_no = su_ERR_NONE;
	s = n_string_creat_auto(&s_b);
	cacp = vp;
	cap = cacp->cac_arg;
	cp = cap->ca_arg.ca_str.s;
	cap = cap->ca_next;

	/* C99 */{
		uz i;

		i = su_cs_len(cap->ca_arg.ca_str.s);
		if(i <= UZ_MAX / 4)
			i <<= 1;
		s = n_string_reserve(s, i);
	}

	for(mode = 0; *cp == '+';){
		++cp;
		if(++mode == 3)
			break;
	}

	if(su_cs_starts_with_case("encode", cp)){
		struct mx_name *np;
		char c;

		cp = cap->ca_arg.ca_str.s;

		while((c = *cp++) != '\0'){
			if(((c == '(' || c == ')') && mode < 1) || (c == '"' && mode < 2) || (c == '\\' && mode < 3))
				s = n_string_push_c(s, '\\');
			s = n_string_push_c(s, c);
		}

		if((np = mx_name_parse_as_one(cp = n_string_cp(s), GTO)) != NIL &&
				(np->n_flags & mx_NAME_ADDRSPEC_ISADDR))
			cp = np->n_fullname;
		else{
			n_pstate_err_no = su_ERR_INVAL;
			vp = NIL;
		}
	}else if(UNLIKELY(mode != 0))
		goto jesynopsis;
	else if(su_cs_starts_with_case("decode", cp)){
		char c;

		cp = cap->ca_arg.ca_str.s;

		while((c = *cp++) != '\0'){
			switch(c){
			case '(':
				s = n_string_push_c(s, '(');
				/* C99 */{
					char const *cpx;

					cpx = mx_name_skip_comment_cp(cp);
					if(--cpx > cp)
						s = n_string_push_buf(s, cp, P2UZ(cpx - cp));
					s = n_string_push_c(s, ')');
					cp = ++cpx;
				}
				break;
			case '"':
				while(*cp != '\0'){
					if((c = *cp++) == '"')
						break;
					if(c == '\\' && (c = *cp) != '\0')
						++cp;
					s = n_string_push_c(s, c);
				}
				break;
			default:
				if(c == '\\' && (c = *cp++) == '\0')
					break;
				s = n_string_push_c(s, c);
				break;
			}
		}
		cp = n_string_cp(s);
	}else if(su_cs_starts_with_case("skin", cp) || (mode = 1, su_cs_starts_with_case("skinlist", cp))){
		struct mx_name *np;

		cp = cap->ca_arg.ca_str.s;

		if((np = mx_name_parse_as_one(cp, GTO)) != NIL && (np->n_flags & mx_NAME_ADDRSPEC_ISADDR)){
			s8 mltype;

			cp = np->n_name;

			if(mode == 1 && (mltype = mx_mlist_query(cp, FAL0)) != mx_MLIST_OTHER &&
					mltype != mx_MLIST_POSSIBLY)
				n_pstate_err_no = su_ERR_EXIST;
		}else{
			n_pstate_err_no = su_ERR_INVAL;
			vp = NIL;
		}
	}else if(su_cs_starts_with_case("normalize", cp)){
		struct mx_name *np, *xp;

		cp = cap->ca_arg.ca_str.s;

		if((np = mx_name_parse(cp, GTO)) == NIL){
			n_pstate_err_no = su_ERR_INVAL;
			vp = NIL;
		}else{
			char **argv, *ncp;
			uz l;
			u32 i;

			for(l = i = 0, xp = np; xp != NIL; ++i, xp = xp->n_flink){
				l += su_cs_len(xp->n_fullname) + 3; /* xxx UZ_MAX overflow? */
				/* xxx U32_MAX overflow? */
			}

			argv = su_LOFI_TALLOC(char*, i);
			ncp = su_AUTO_ALLOC(l +1);

			for(l = i = 0, xp = np; xp != NIL; xp = xp->n_flink){
				uz k;

				argv[i++] = xp->n_fullname;
				k = su_cs_len(xp->n_fullname);
				if(l > 0){
					ncp[l++] = ',';
					ncp[l++] = ' ';
				}
				su_mem_copy(&ncp[l], xp->n_fullname, k);
				l += k;
			}
			ncp[l] = '\0';

			cp = ncp;
			mx_var_result_set_set(NIL, ncp, i, NIL, C(char const**,argv));

			su_LOFI_FREE(argv);
		}
	}else
		goto jesynopsis;

	if(cacp->cac_vput == NIL){
		if(fprintf(n_stdout, "%s\n", cp) < 0){
			n_pstate_err_no = su_err_by_errno();
			vp = NIL;
		}
	}else if(!n_var_vset(cacp->cac_vput, R(up,cp), cacp->cac_scope_vput)){
		n_pstate_err_no = su_ERR_NOTSUP;
		vp = NIL;
	}

jleave:
	NYD_OU;
	return (vp != NIL ? su_EX_OK : su_EX_ERR);

jesynopsis:
	mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("addrcodec"), NIL);
	n_pstate_err_no = su_ERR_INVAL;
	vp = NIL;
	goto jleave;
} /* }}} */

struct mx_name *
mx_name_parse(char const *line, BITENUM(u32,gfield) ntype){
	struct mx_name *rv;
	NYD_IN;

	rv = a_nm_parse(line, ntype, FAL0);

	NYD_OU;
	return rv;
}

struct mx_name *
mx_name_parse_as_one(char const *line, BITENUM(u32,gfield) ntype){
	struct mx_name *rv;
	NYD_IN;

	rv = a_nm_parse(line, ntype, TRU1);

	NYD_OU;
	return rv;
}

struct mx_name *
mx_name_parse_fcc(char const *file){
	struct mx_name *nnp;
	NYD_IN;

	nnp = n_autorec_alloc(sizeof *nnp);
	nnp->n_flink = nnp->n_blink = NIL;
	nnp->n_type = GBCC | GBCC_IS_FCC; /* xxx Bcc: <- namelist_vaporise_head */
	nnp->n_flags = mx_NAME_NAME_SALLOC | mx_NAME_SKINNED | mx_NAME_ADDRSPEC_ISFILE;
	nnp->n_fullname = nnp->n_name = savestr(file);
	nnp->n_fullextra = NIL;

	NYD_OU;
	return nnp;
}

struct mx_name *
ndup(struct mx_name *np, enum gfield ntype){
	struct mx_name *nnp;
	NYD_IN;

#if 0
	if(ntype & (GTO | GCC | GBCC | GIDENT))
		ntype |= GFULL | GSKIN;
#endif

	if(!(np->n_flags & mx_NAME_SKINNED)){
		nnp = a_nm_alloc(np->n_name, ntype);
		goto jleave;
	}

	nnp = su_AUTO_ALLOC(sizeof *np);
	nnp->n_flink = nnp->n_blink = NIL;
	nnp->n_type = ntype & ~GFULLEXTRA;
	nnp->n_flags = np->n_flags | mx_NAME_NAME_SALLOC;
	nnp->n_name = savestr(np->n_name);

	if(np->n_name == np->n_fullname){
		nnp->n_fullname = nnp->n_name;
		nnp->n_fullextra = NIL;
	}else{
		nnp->n_fullname = savestr(np->n_fullname);
		nnp->n_fullextra = (!(ntype & GFULLEXTRA) || np->n_fullextra == NIL) ? NIL : savestr(np->n_fullextra);
		nnp->n_type = ntype;
	}

jleave:
	NYD_OU;
	return nnp;
}

s8
mx_name_is_invalid(struct mx_name *np, enum mx_expand_addr_check_mode eacm){ /* {{{ */
	/* TODO This is called much too often!  Message->DOMTree->modify->..
	 * TODO I.e., [verify once before compose-mode], verify once after
	 * TODO compose-mode, store result in object */
	char cbuf[sizeof "'\\U12340'"];
	char const *cs;
	int f;
	s8 rv;
	enum mx_expand_addr_flags eaf;
	NYD_IN;

	eaf = a_nm_expandaddr_to_eaf();
	f = np->n_flags;

	if((rv = ((f & mx_NAME_ADDRSPEC_INVALID) != 0))){
		if(eaf & mx_EAF_FAILINVADDR)
			rv = -rv;

		if(!(eacm & mx_EACM_NOLOG) && !(f & mx_NAME_ADDRSPEC_ERR_EMPTY)){
			u32 c;
			boole ok8bit;
			char const *fmt;

			fmt = "'\\x%02X'";
			ok8bit = TRU1;

			if(f & mx_NAME_ADDRSPEC_ERR_IDNA) {
				cs = _("Invalid domain name: %s, character %s\n");
				fmt = "'\\U%04X'";
				ok8bit = FAL0;
			}else if(f & mx_NAME_ADDRSPEC_ERR_ATSEQ)
				cs = _("%s contains invalid %s sequence\n");
			else if(f & mx_NAME_ADDRSPEC_ERR_NAME)
				cs = _("%s is an invalid name (alias)\n");
			else
				cs = _("%s contains invalid byte %s\n");

			c = mx_name_flags_get_err_wc(f);
			if(ok8bit && c >= 040 && c <= 0177)
				snprintf(cbuf, sizeof cbuf, "'%c'", S(char,c));
			else
				snprintf(cbuf, sizeof cbuf, fmt, c);
			goto jprint;
		}
		goto jleave;
	}

	/* *expandaddr* stuff */
	if(!(rv = ((eacm & mx_EACM_MODE_MASK) != mx_EACM_NONE)))
		goto jleave;

	/* This header does not allow such targets at all (XXX >RFC 5322 parser) */
	if((eacm & mx_EACM_STRICT) && (f & mx_NAME_ADDRSPEC_ISFILEORPIPE)){
		if(eaf & mx_EAF_FAIL)
			rv = -rv;
		cs = _("%s%s: file or pipe addressees not allowed here\n");
		goto j0print;
	}

	eaf |= (eacm & mx_EAF_TARGET_MASK);
	if(eacm & mx_EACM_NONAME)
		eaf &= ~mx_EAF_NAME;
	if(eaf & mx_EAF_FAIL)
		rv = -rv;

	switch(f & mx_NAME_ADDRSPEC_ISMASK){
	case mx_NAME_ADDRSPEC_ISFILE:
		if((eaf & mx_EAF_FILE) || ((eaf & mx_EAF_FCC) && (np->n_type & GBCC_IS_FCC)))
			goto jgood;
		cs = _("%s%s: *expandaddr* does not allow file target\n");
		break;
	case mx_NAME_ADDRSPEC_ISPIPE:
		if(eaf & mx_EAF_PIPE)
			goto jgood;
		cs = _("%s%s: *expandaddr* does not allow command pipe target\n");
		break;
	case mx_NAME_ADDRSPEC_ISNAME:
		if((eaf & mx_EAF_NAMETOADDR) &&
				(!su_cs_cmp(np->n_name, ok_vlook(LOGNAME)) ||
					getpwnam(np->n_name) != NIL)){
			np->n_flags ^= mx_NAME_ADDRSPEC_ISADDR | mx_NAME_ADDRSPEC_ISNAME;
			np->n_name = np->n_fullname = savecatsep(np->n_name, '@',
					n_nodename(TRU1));
			goto jisaddr;
		}

		if(eaf & mx_EAF_NAME)
			goto jgood;
		if(!(eaf & mx_EAF_FAIL) && (eacm & mx_EACM_NONAME_OR_FAIL)){
			rv = -rv;
			cs = _("%s%s: user name (MTA alias) targets are not allowed\n");
		}else
			cs = _("%s%s: *expandaddr* does not allow user name target\n");
		break;
	default:
	case mx_NAME_ADDRSPEC_ISADDR:
jisaddr:
		if(!(eaf & mx_EAF_ADDR)){
			cs = _("%s%s: *expandaddr* does not allow mail address target\n");
			break;
		}
		if(!(eacm & mx_EACM_DOMAINCHECK) || !(eaf & mx_EAF_DOMAINCHECK))
			goto jgood;
		else{
			char const *doms;

			ASSERT(np->n_flags & mx_NAME_SKINNED);
			/* XXX We had this info before, and threw it away.. */
			doms = su_cs_rfind_c(np->n_name, '@');
			ASSERT(doms != NIL);
			++doms;

			if(!su_cs_cmp_case("localhost", doms))
				goto jgood;
			if(!su_cs_cmp_case(n_nodename(TRU1), doms))
				goto jgood;

			if((cs = ok_vlook(expandaddr_domaincheck)) != NIL){
				char *cpb, *cp;

				cpb = savestr(cs);
				while((cp = su_cs_sep_c(&cpb, ',', TRU1)) != NIL)
					if(!su_cs_cmp_case(cp, doms))
						goto jgood;
			}
		}
		cs = _("%s%s: *expandaddr*: not \"domaincheck\" whitelisted\n");
		break;
	}

j0print:
	cbuf[0] = '\0';
	if(!(eacm & mx_EACM_NOLOG))
jprint:
		n_err(cs, n_shexp_quote_cp(np->n_name, TRU1), cbuf);
	goto jleave;
jgood:
	rv = FAL0;
jleave:
	NYD_OU;
	return rv;
} /* }}} */

boole
mx_name_is_same_address(struct mx_name const *n1, struct mx_name const *n2){
	boole rv;
	NYD2_IN;

	rv = TRUM1;
	rv = (mx_name_is_same_cp(n1->n_name, n2->n_name, &rv) && (rv || mx_name_is_same_domain(n1, n2)));

	NYD2_OU;
	return rv;
}

boole
mx_name_is_same_domain(struct mx_name const *n1, struct mx_name const *n2){
	boole rv;
	NYD2_IN;

	if((rv = (n1 != NIL && n2 != NIL))){
		char const *d1, *d2;

		d1 = su_cs_rfind_c(n1->n_name, '@');
		d2 = su_cs_rfind_c(n2->n_name, '@');

		rv = (d1 != NIL && d2 != NIL) ? !su_cs_cmp_case(++d1, ++d2) : FAL0;
	}

	NYD2_OU;
	return rv;
}

char const *
mx_name_skip_comment_cp(char const *cp){ /* {{{ */
	char c;
	uz nesting;
	NYD2_IN;

	for(nesting = 1; (c = *cp) != '\0' && nesting > 0; ++cp){
		switch(c){
		case '\\':
			if(cp[1] != '\0')
				++cp;
			break;
		case '(':
			++nesting;
			break;
		case ')':
			--nesting;
			break;
		}
	}

	NYD2_OU;
	return cp;
} /* }}} */

char const *
mx_name_routeaddr_cp(char const *name){ /* {{{ */
	char c;
	char const *np, *rp;
	NYD_IN;

	for(rp = NIL, np = name; (c = *np) != '\0'; ++np){
		switch(c){
		case '(':
			np = mx_name_skip_comment_cp(&np[1]) - 1;
			break;
		case '"':
			for(;;){
				if((c = *++np) == '\0')
					goto jnil;
				if(c == '"')
					break;
				if(c == '\\' && np[1] != '\0')
					++np;
			}
			break;
		case '<':
			rp = np;
			break;
		case '>':
			goto jleave;
		}
	}

jnil:
	rp = NIL;
jleave:
	NYD_OU;
	return rp;
} /* }}} */

char *
mx_name_real_cp(char const *name){ /* {{{ */
	char *rname, *rp;
	struct str in, out;
	int quoted, good, nogood;
	char const *cp, *cq, *cstart, *cend;
	NYD_IN;

	if((cp = UNCONST(char*,name)) == NIL)
		goto jleave;

	cstart = cend = NIL;

	for(; *cp != '\0'; ++cp){
		switch(*cp){
		case '(':
			if(cstart != NIL){
				/* More than one comment in address, doesn't make sense to display
				 * it without context.	Return the entire field */
				cp = mx_mime_fromaddr(name);
				goto jleave;
			}
			cstart = cp++;
			cp = mx_name_skip_comment_cp(cp);
			cend = cp--;
			if(cend <= cstart)
				cend = cstart = NIL;
			break;
		case '"':
			while(*cp){
				if(*++cp == '"')
					break;
				if(*cp == '\\' && cp[1])
					++cp;
			}
			break;
		case '<':
			if(cp > name){
				cstart = name;
				cend = cp;
			}
			break;
		case ',':
			/* More than one address. Just use the first one */
			goto jbrk;
		}
	}

jbrk:
	if(cstart == NIL){
		if(*name == '<'){
			/* If name contains only a route-addr, the surrounding angle brackets
			 * do not serve any useful purpose when displaying, so remove */
			struct mx_name *np;

			np = a_nm_alloc(name, GTO);
			cp = mx_makeprint_cp((np != NIL) ? np->n_name : su_empty);
		}else
			cp = mx_mime_fromaddr(name);
		goto jleave;
	}

	/* Strip quotes. Note that quotes that appear within a MIME encoded word are
	 * not stripped. The idea is to strip only syntactical relevant things (but
	 * this is not necessarily the most sensible way in practice) */
	rp = rname = su_LOFI_ALLOC(P2UZ(cend - cstart +1));

	quoted = 0;
	for(cp = cstart; cp < cend; ++cp){
		if(*cp == '(' && !quoted){
			cq = mx_name_skip_comment_cp(++cp);
			if(PCMP(--cq, >, cend))
				cq = cend;
			while(cp < cq){
				if(*cp == '\\' && PCMP(cp + 1, <, cq))
					++cp;
				*rp++ = *cp++;
			}
		}else if(*cp == '\\' && PCMP(cp + 1, <, cend))
			*rp++ = *++cp;
		else if(*cp == '"') {
			quoted = !quoted;
			continue;
		}else
			*rp++ = *cp;
	}
	*rp = '\0';
	in.s = rname;
	in.l = rp - rname;

	if(!mx_mime_display_from_header(&in, &out, mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT))
		cp = NIL;

	su_LOFI_FREE(rname);

	if(cp == NIL)
		goto jleave;

	rname = savestr(out.s);
	su_FREE(out.s);

	while(su_cs_is_blank(*rname))
		++rname;
	for(rp = rname; *rp != '\0'; ++rp){
	}
	while(PCMP(--rp, >=, rname) && su_cs_is_blank(*rp))
		*rp = '\0';
	if(rp == rname){
		cp = mx_mime_fromaddr(name);
		goto jleave;
	}

	/* mime_fromhdr() has converted all nonprintable characters to question
	 * marks now. These and blanks are considered uninteresting; if the
	 * displayed part of the real name contains more than 25% of them, it is
	 * probably better to display the plain email address instead */
	good = 0;
	nogood = 0;
	for(rp = rname; *rp != '\0' && PCMP(rp, <, rname + 20); ++rp)
		if(*rp == '?' || su_cs_is_blank(*rp))
			++nogood;
		else
			++good;
	if(good * 3 < nogood){
		struct mx_name *np;

		np = a_nm_alloc(name, GTO);
		cp = mx_makeprint_cp((np != NIL) ? np->n_name : su_empty);
	}else
		cp = rname;

jleave:
	NYD_OU;
	return UNCONST(char*,cp);
} /* }}} */

boole
mx_name_is_same_cp(char const *n1, char const *n2, boole *isallnet_or_nil){ /* {{{ */
	char c1, c2, c1r, c2r;
	boole rv, isall;
	NYD2_IN;

	rv = (isallnet_or_nil != NIL);
	isall = rv ? *isallnet_or_nil : TRUM1;
	if(isall == TRUM1)
		isall = ok_blook(allnet);
	if(rv)
		*isallnet_or_nil = isall;

	if(!isall)
		rv = !su_cs_cmp_case(n1, n2);
	else{
		for(;; ++n1, ++n2){
			c1 = *n1;
			c1 = su_cs_to_lower(c1);
			c1r = (c1 == '\0' || c1 == '@');
			c2 = *n2;
			c2 = su_cs_to_lower(c2);
			c2r = (c2 == '\0' || c2 == '@');

			if(c1r || c2r){
				rv = (c1r == c2r);
				break;
			}else if(c1 != c2){
				rv = FAL0;
				break;
			}
		}
	}

	NYD2_OU;
	return rv;
} /* }}} */

boole
mx_name_is_metoo_cp(char const *name, boole check_reply_to){ /* {{{ */
	struct mx_name *xp;
	boole isall;
	NYD_IN;

	isall = TRUM1;

	if(mx_name_is_same_cp(ok_vlook(LOGNAME), name, &isall))
		goto jleave;

	if(mx_alternates_exists(name, isall))
		goto jleave;

	for(xp = mx_name_parse(ok_vlook(from), GIDENT); xp != NIL; xp = xp->n_flink)
		if(mx_name_is_same_cp(xp->n_name, name, &isall))
			goto jleave;

	if((xp = mx_name_parse_as_one(ok_vlook(sender), GIDENT)) != NIL && mx_name_is_same_cp(xp->n_name, name, &isall))
		goto jleave;

	if(check_reply_to){
		for(xp = mx_name_parse(ok_vlook(reply_to), GIDENT); xp != NIL; xp = xp->n_flink)
			if(mx_name_is_same_cp(xp->n_name, name, &isall))
				goto jleave;
	}

	name = NIL;
jleave:
	NYD_OU;
	return (name != NIL);
} /* }}} */

/* msgid {{{ */
uz
mx_name_msgid_hash_cp(char const *id){
	boole s;
	char *cp_base, *cp;
	uz rv;
	NYD2_IN;

	rv = su_cs_len(id);
	cp_base = cp = su_LOFI_ALLOC(rv +1);

	for(;;){
		if((*cp++ = a_nm_msgid_stepc(&id, &s)) == '\0')
			break;
	}

	rv = P2UZ(cp - cp_base);
	rv = su_cs_hash_cbuf(cp_base, rv);

	su_LOFI_FREE(cp_base);

	NYD2_OU;
	return rv;
}

sz
mx_name_msgid_cmp_cp(char const *id1, char const *id2){
	boole s1, s2;
	char c1, c2;
	NYD2_IN;

	while(*id1 == '<')
		++id1;
	while(*id2 == '<')
		++id2;

	for(s1 = s2 = FAL0;;){
		c1 = a_nm_msgid_stepc(&id1, &s1);
		c2 = a_nm_msgid_stepc(&id2, &s2);
		if(c1 != c2 || c1 == '\0')
			break;
	}

	NYD2_OU;
	return S(sz,c1) - c2;
}
/* }}} */

/* namelist {{{ */

struct mx_name *
mx_namelist_grab(u32 gif, char const *field, struct mx_name *np, int comma, BITENUM(u32,gfield) gflags,
		boole not_a_list){
	struct mx_name *nq;
	char const *cp;
	NYD2_IN;

jloop:
	cp = mx_go_input_cp(gif, field, mx_namelist_detract(np, comma));
	np = (not_a_list ? mx_name_parse_as_one : mx_name_parse)(cp, gflags);
	for(nq = np; nq != NIL; nq = nq->n_flink)
		if(mx_name_is_invalid(nq, mx_EACM_NONE))
			goto jloop;

	NYD2_OU;
	return np;
}

struct mx_name *
n_namelist_dup(struct mx_name const *np, BITENUM(u32,gfield) ntype){
	struct mx_name *nlist, *xnp;
	NYD2_IN;

	for(nlist = xnp = NIL; np != NIL; np = np->n_flink){
		struct mx_name *x;

		if(!(np->n_type & GDEL)){
			x = ndup(UNCONST(struct mx_name*,np), (np->n_type & ~GMASK) | ntype);
			if((x->n_blink = xnp) == NIL)
				nlist = x;
			else
				xnp->n_flink = x;
			xnp = x;
		}
	}

	NYD2_OU;
	return nlist;
}

struct mx_name *
cat(struct mx_name *n1, struct mx_name *n2){
	struct mx_name *tail;
	NYD2_IN;

	tail = n2;
	if(n1 == NIL)
		goto jleave;

	tail = n1;
	if(n2 == NIL || (n2->n_type & GDEL))
		goto jleave;

	while(tail->n_flink != NIL)
		tail = tail->n_flink;
	tail->n_flink = n2;
	n2->n_blink = tail;
	tail = n1;

jleave:
	NYD2_OU;
	return tail;
}

u32
count(struct mx_name const *np){
	u32 c;
	NYD2_IN;

	for(c = 0; np != NIL; np = np->n_flink)
		if(!(np->n_type & GDEL))
			++c;
	NYD2_OU;
	return c;
}

u32
count_nonlocal(struct mx_name const *np){
	u32 c;
	NYD2_IN;

	for(c = 0; np != NIL; np = np->n_flink)
		if(!(np->n_type & GDEL) && !(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE))
			++c;
	NYD2_OU;
	return c;
}

struct mx_name *
mx_namelist_check(struct mx_name *np, enum mx_expand_addr_check_mode eacm, s8 *set_on_error){
	struct mx_name *n;
	NYD2_IN;

	for(n = np; n != NIL; n = n->n_flink){
		s8 rv;

		if((rv = mx_name_is_invalid(n, eacm)) != 0){
			if(set_on_error != NIL)
				*set_on_error |= rv; /* don't loose -1! */
			if(eacm & mx_EAF_MAYKEEP) /* TODO HACK!	See definition! */
				continue;
			if(n->n_blink)
				n->n_blink->n_flink = n->n_flink;
			if(n->n_flink)
				n->n_flink->n_blink = n->n_blink;
			if(n == np)
				np = n->n_flink;
		}
	}

	NYD2_OU;
	return np;
}

struct mx_name *
n_namelist_vaporise_head(struct header *hp, enum mx_expand_addr_check_mode eacm,
	s8 *set_on_error)
{
	/* TODO namelist_vaporise_head() is incredibly expensive and redundant */
	struct mx_name *tolist, *np, **npp;
	NYD_IN;

	tolist = cat(hp->h_to, cat(hp->h_cc, hp->h_fcc));
	np = hp->h_bcc;
	hp->h_to = hp->h_cc = hp->h_bcc = hp->h_fcc = NIL;

	tolist = mx_alias_expand_list(tolist, FAL0);
	np = mx_alias_expand_list(np, TRU1);
	tolist = cat(tolist, np);

	/* MTA aliases are resolved last */
#ifdef mx_HAVE_MTA_ALIASES
	switch(mx_mta_aliases_expand(&tolist)){
	case su_ERR_DESTADDRREQ:
	case su_ERR_NONE:
	case su_ERR_NOENT:
		break;
	default:
		*set_on_error |= TRU1;
		break;
	}
#endif

	tolist = mx_namelist_elide(tolist);

	tolist = mx_namelist_check(tolist, eacm, set_on_error);

	for (np = tolist; np != NIL; np = np->n_flink) {
		switch (np->n_type & (GDEL | GMASK)) {
		case GTO:	npp = &hp->h_to; break;
		case GCC:	npp = &hp->h_cc; break;
		case GBCC:	npp = &hp->h_bcc; break;
		default:		continue;
		}
		*npp = cat(*npp, ndup(np, np->n_type));
	}

	NYD_OU;
	return tolist;
}

struct mx_name *
mx_namelist_elide(struct mx_name *names){
	uz i, j, k;
	struct mx_name *nlist, *np, **nparr;
	NYD_IN;

	nlist = NIL;
	if(names == NIL)
		goto jleave;

	/* Throw away all deleted nodes */
	for(np = NIL, i = 0; names != NIL; names = names->n_flink)
		if(!(names->n_type & GDEL)){
			++i;
			names->n_blink = np;
			if(np != NIL)
				np->n_flink = names;
			else
				nlist = names;
			np = names;
		}
	if(nlist == NIL || i == 1)
		goto jleave;
	np->n_flink = NIL;

	/* Create a temporary array and sort that */
	nparr = su_LOFI_ALLOC(sizeof(*nparr) * i);

	for(i = 0, np = nlist; np != NIL; np = np->n_flink)
		nparr[i++] = np;

	su_sort_shell_vpp(S(void const**,nparr), i, &a_nm_elide_sort);

	/* Remove duplicates XXX speedup, or list_uniq()! */
	for(j = 0, --i; j < i;){
		if(su_cs_cmp_case(nparr[j]->n_name, nparr[k = j + 1]->n_name))
			++j;
		else{
			for(; k < i; ++k)
				nparr[k] = nparr[k + 1];
			--i;
		}
	}

	/* Throw away all list members which are not part of the array.
	 * Note this keeps the original, possibly carefully crafted, order of the
	 * addressees, thus.. */
	for(np = nlist; np != NIL; np = np->n_flink){
		for(j = 0; j <= i; ++j)
			/* The order of pointers depends on the sorting algorithm, and
			 * therefore our desire to keep the original order of addessees cannot
			 * be guaranteed when there are multiple equal names (ham zebra ham)
			 * of equal weight: we need to compare names _once_again_ :( */
			if(nparr[j] != NIL && !su_cs_cmp_case(np->n_name, nparr[j]->n_name)){
				nparr[j] = NIL;
				goto jiter;
			}
		/* Drop it */
		if(np == nlist){
			nlist = np->n_flink;
			np->n_blink = NIL;
		}else
			np->n_blink->n_flink = np->n_flink;
		if(np->n_flink != NIL)
			np->n_flink->n_blink = np->n_blink;
jiter:;
	}

	su_LOFI_FREE(nparr);

jleave:
	NYD_OU;
	return nlist;
}

char *
mx_namelist_detract(struct mx_name const *np, BITENUM(u32,gfield) ntype){
	char *topp, *cp;
	struct mx_name const *p;
	int flags, s;
	NYD_IN;

	topp = NIL;
	if(np == NIL)
		goto jleave;

	flags = ntype & (GCOMMA | GNAMEONLY);
	ntype &= ~(GCOMMA | GNAMEONLY);
	s = 0;

	for(p = np; p != NIL; p = p->n_flink){
		if(ntype && (p->n_type & GMASK) != ntype)
			continue;
		s += su_cs_len((flags & GNAMEONLY) ? p->n_name : p->n_fullname) +1;
		if(flags & GCOMMA)
			++s;
	}
	if(s == 0)
		goto jleave;

	s += 2;
	topp = su_AUTO_ALLOC(s);

	cp = topp;
	for(p = np; p != NIL; p = p->n_flink){
		if(ntype && (p->n_type & GMASK) != ntype)
			continue;
		cp = su_cs_pcopy(cp, ((flags & GNAMEONLY) ? p->n_name : p->n_fullname));
		if((flags & GCOMMA) && p->n_flink != NIL)
			*cp++ = ',';
		*cp++ = ' ';
	}
	*--cp = 0;
	if((flags & GCOMMA) && *--cp == ',')
		*cp = 0;

jleave:
	NYD_OU;
	return topp;
}
/* }}} */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_NAMES
/* s-itt-mode */
