/*@ Implementation of imf.h, parse_addr_header().
 *
 * Copyright (c) 2024 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_imf_addr
#define su_SOURCE
#define su_SOURCE_IMF_ADDR

#include "su/code.h"

su_EMPTY_FILE()
#ifdef su_HAVE_IMF

#include "su/imf.h"
#include "su/y-imf.h" /* $(SU_SRCDIR) */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

#include <su/y-imf.h> /* 2. */

/**/
static void a_imf_addr_create(struct su__imf_actx *acp, struct su_mem_bag *membp, struct su_imf_addr ***appp);

static void
a_imf_addr_create(struct su__imf_actx *acp, struct su_mem_bag *membp, struct su_imf_addr ***appp){ /* {{{ */
	struct su_imf_addr *ap;
	u32 i;
	NYD2_IN;

	i = VSTRUCT_SIZEOF(struct su_imf_addr,imfa_dat) +
			acp->ac_.group_display_name +1 + acp->ac_.display_name +1 +
			acp->ac_.locpar +1 + acp->ac_.domain +1 + acp->ac_.comm +1;

	ap = su__IMF_ALLOC(membp, i);
	**appp = ap;
	*appp = &ap->imfa_next;

	ap->imfa_next = NIL;
	ap->imfa_mse = acp->ac_.mse & ~su__IMF_MODE_ADDR_MASK;

	ap->imfa_group_display_name = ap->imfa_dat;
	if((ap->imfa_group_display_name_len = i = acp->ac_.group_display_name) > 0)
		su_mem_copy(ap->imfa_group_display_name, acp->ac_group_display_name, i);
	ap->imfa_group_display_name[i++] = '\0';

	ap->imfa_display_name = &ap->imfa_group_display_name[i];
	if((ap->imfa_display_name_len = i = acp->ac_.display_name) > 0)
		su_mem_copy(ap->imfa_display_name, acp->ac_display_name, i);
	ap->imfa_display_name[i++] = '\0';

	ap->imfa_locpar = &ap->imfa_display_name[i];
	if((ap->imfa_locpar_len = i = acp->ac_.locpar) > 0)
		su_mem_copy(ap->imfa_locpar, acp->ac_locpar, i);
	ap->imfa_locpar[i++] = '\0';

	ap->imfa_domain = &ap->imfa_locpar[i];
	if((ap->imfa_domain_len = i = acp->ac_.domain) > 0)
		su_mem_copy(ap->imfa_domain, acp->ac_domain, i);
	ap->imfa_domain[i++] = '\0';

	ap->imfa_comm = &ap->imfa_domain[i];
	if((ap->imfa_comm_len = i = acp->ac_.comm) > 0)
		su_mem_copy(ap->imfa_comm, acp->ac_comm, i);
	ap->imfa_comm[i++] = '\0';

	NYD2_OU;
} /* }}} */

s32
su_imf_parse_addr_header(struct su_imf_addr **app, char const *header, BITENUM(u32,su_imf_mode) mode, /* {{{ */
		struct su_mem_bag *membp, char const **endptr_or_nil){
	enum{
		a_NONE,
		a_STAGE_DOMAIN = 1u<<0, /* Parsing a domain */
		a_STAGE_ANGLE = 1u<<1, /* Have seen a left angle bracket, parsing addr-spec (or obs-route) */
		a_STAGE_DOT = 1u<<2, /* Have seen a DOT in a display-name: could be a local-part, too! */
		a_STAGE_REST_OR_SEP = 1u<<3, /* Done address(-list): only comments or -list separators may follow */
		a_STAGE_MASK = a_STAGE_DOMAIN | a_STAGE_ANGLE | a_STAGE_DOT | a_STAGE_REST_OR_SEP,

		a_TMP = 1u<<7,

		a_ANY = 1u<<8,
		a_WS = 1u<<9,
		a_WS_SEEN = 1u<<10, /* Strip FWS obs-local-part(->word->atom->[CFWS]1*atext) */
		a_QUOTE = 1u<<11, /* Had seen non-empty quoted-string */
		a_QUOTE_ANYHOW = 1u<<12, /* (..empty..) */
		a_ROUTE = 1u<<13, /* With STAGE_ANGLE */
		a_ROUTE_SEEN = 1u<<14,
		a_NEED_SEP = 1u<<15, /* Beside WS or NUL an address(-list) separator must follow */
		a_MASK = a_ANY | a_WS | a_WS_SEEN | a_QUOTE | a_QUOTE_ANYHOW | a_ROUTE | a_ROUTE_SEEN | a_NEED_SEP
	};

	char *cp, *cpalter;
	u32 f, l;
	struct su__imf_actx *acp;
	s32 rv;
	struct su_imf_addr **app_base;
	NYD_IN;
	ASSERT_NYD_EXEC(app != NIL, rv = -su_ERR_NODATA);
	ASSERT_NYD_EXEC(header != NIL, rv = -su_ERR_NODATA);
	ASSERT_NYD_EXEC(membp != NIL, rv = -su_ERR_NODATA);
	ASSERT_EXEC((mode & ~su__IMF_MODE_ADDR_MASK) == 0, mode &= su__IMF_MODE_ADDR_MASK);

	*(app_base = app) = NIL;

	/* C99 */{
		uz i;

		while(su_imf_c_ANY_WSP(*header))
			++header;
		i = su_cs_len(header);
		if(i == 0){
			rv = -su_ERR_NODATA;
			goto j_leave;
		}

		/* S32_MAX is mem-bag stuff, 6 is 5 strings of maximum size plus room for the structure as such.
		 * We may re-quote some data, so reserve two bytes for quotes, and one for NUL */
		if(i >= (S32_MAX - 5*2 - 5*1) / 6){
			rv = -su_ERR_OVERFLOW;
			goto j_leave;
		}else if(i < VSTRUCT_SIZEOF(struct su__imf_actx,ac_dat))
			i = VSTRUCT_SIZEOF(struct su__imf_actx,ac_dat);
		i += 2 +1;

		acp = su__IMF_ALLOC(membp, i * 6);
		if(acp == NIL){
			rv = -su_ERR_NOMEM;
			goto j_leave;
		}

		acp->ac_.hd = header;
		acp->ac_.mse = mode;
		acp->ac_group_display_name = acp->ac_dat;
		acp->ac_display_name = &acp->ac_group_display_name[i];
		++acp->ac_group_display_name; /* (room for double-quote) */
		acp->ac_locpar = &acp->ac_display_name[i];
		++acp->ac_display_name;
		acp->ac_domain = &acp->ac_locpar[i];
		++acp->ac_locpar;
		acp->ac_comm = &acp->ac_domain[i];
	}

	/* address-list = (address *("," address)): parse one */
	f = a_NONE;
jlist_next:
	rv = su_ERR_NONE;
	l = 0;
	cp = acp->ac_display_name;
	cpalter = acp->ac_locpar;

	STRUCT_ZERO_FROM(struct su__imf_x, &acp->ac_, group_display_name);
	acp->ac_.mse &= su__IMF_MODE_ADDR_MASK | ((acp->ac_.mse & su_IMF_STATE_GROUP_END) ? 0 : su_IMF_STATE_GROUP);

	for(rv = su_IMF_ERR_CONTENT;;){
		char c;
		char const *xcp;
		uz i;

		switch(su__imf_s_CFWS(acp)){
		case FAL0:
			rv = (acp->ac_.mse & (su_IMF_ERR_RELAX | su_IMF_ERR_MASK));
			goto jleave;
		case TRUM1:
			if(l > 0)
				f |= a_WS;
			break;
		default:
			break;
		}

		c = *acp->ac_.hd;

		if(!(f & a_STAGE_MASK)){
			/* Early check for necessary separator (without further validation) */
			if((f & a_NEED_SEP) && (c != ';' && c != ',' && c != '\0'))
				goto jleave;
		}else if(UNLIKELY(f & a_STAGE_REST_OR_SEP)){
jaddr_step_create:
			switch(c){
			case ',':
				f = a_NONE;
jaddr_create:
				++acp->ac_.hd;
				a_imf_addr_create(acp, membp, &app);
				if(mode & su_IMF_MODE_STOP_EARLY)
					goto jlist_done;
				goto jlist_next;
			case ';':
				if(!(acp->ac_.mse & su_IMF_STATE_GROUP))
					goto jleave;
				acp->ac_.mse |= su_IMF_STATE_GROUP_END;
				f &= ~(a_MASK | a_STAGE_MASK);
				f |= a_NEED_SEP;
				goto jaddr_create;
			case '\0':
				if((acp->ac_.mse & su_IMF_STATE_GROUP) && !(acp->ac_.mse & su_IMF_STATE_GROUP_END)){
					if(acp->ac_.mse & su_IMF_MODE_RELAX)
						acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_STATE_GROUP_END |
								su_IMF_ERR_GROUP_OPEN;
					else{
						rv = su_IMF_ERR_GROUP_OPEN;
						goto jleave;
					}
				}
				a_imf_addr_create(acp, membp, &app);
				goto jlist_done;
			default:
				goto jleave;
			}
		}

		if(!(f & a_STAGE_DOMAIN) && su_imf_c_DQUOTE(*acp->ac_.hd)){
			/* Set a_QUOTE only if non-empty quote seen */
			uz j;
			char *ncp;

			/*f |= a_ANY *//*| a_QUOTE*/;
			if((f & (a_STAGE_ANGLE /*| a_STAGE_DOMAIN*/ | a_WS)) == a_WS){
				cp[l++] = ' ';
				f |= a_WS_SEEN;
			}
			f &= ~a_WS;
			f |= a_ANY;

			/* MUST be display-name or local-part; remove quotes, they are "not [a] semantical[ly] part" */
			xcp = ncp = &cp[l];
			ncp = su__imf_s_quoted_string(acp, ncp);
			if(ncp == NIL){
				rv = (acp->ac_.mse & su_IMF_ERR_MASK);
				goto jleave;
			}
			j = P2UZ(ncp - xcp);
			f |= a_QUOTE_ANYHOW;
			if(j > 0){
				f |= a_QUOTE;
				l += S(u32,j);
				if(cpalter != NIL){
					su_mem_copy(cpalter, xcp, j);
					cpalter += j;
				}
			}
			continue;
		}

		xcp = acp->ac_.hd;
		while(su_imf_c_atext(*acp->ac_.hd))
			++acp->ac_.hd;
		if(xcp != acp->ac_.hd){
jpushany:
			i = P2UZ(acp->ac_.hd - xcp);
			if((f & (a_STAGE_ANGLE | a_STAGE_DOMAIN | a_WS)) == a_WS){
				cp[l++] = ' ';
				f |= a_WS_SEEN;
			}
			f &= ~a_WS;
			f |= a_ANY;
			su_mem_copy(&cp[l], xcp, i);
			l += S(u32,i);
			if(cpalter != NIL){
				su_mem_copy(cpalter, xcp, i);
				cpalter += i;
			}
			continue;
		}

		c = *acp->ac_.hd;
		if(c != '\0')
			++acp->ac_.hd;

		if(f & a_STAGE_DOMAIN){
			ASSERT(cp == acp->ac_domain);
			ASSERT(cpalter == NIL);
			switch(c){
			case '.':
				if(l == 0 || cp[l - 1] == '.')
					goto jleave;
				cp[l++] = c;
				continue;
			case '[':
				if(l != 0)
					goto jleave;
				/* Literal better directly */
				cp[l++] = '[';
				for(;;){
					su__imf_skip_FWS(acp); /* XXX Postel: CFWS()? */
					c = *acp->ac_.hd++;
					if(su_imf_c_dtext(c) || su_imf_c_obs_NO_WS_CTL(c))
						cp[l++] = c;
					else if(c == '\\' && su_imf_c_quoted_pair_c2(*acp->ac_.hd)){
						cp[l++] = c;
						cp[l++] = *acp->ac_.hd++;
					}else if(c == ']')
						break;
					else
						goto jleave;
				}
				cp[l++] = ']';
				acp->ac_.mse |= su_IMF_STATE_DOMAIN_LITERAL;

				/* Unfortunately not integratable XXX */
				if(su__imf_s_CFWS(acp) == FAL0)
					goto jleave;
				if(f & a_STAGE_ANGLE){
					if(*acp->ac_.hd == '>')
						++acp->ac_.hd;
					else /* FIXME not with relax*/
						goto jleave;
				}
				c = acp->ac_.hd[-1];
				goto jdomain_done;
			case ';':
				if((f & a_STAGE_ANGLE) || !(acp->ac_.mse & su_IMF_STATE_GROUP))
					goto jleave;
				goto jdomain_done;
			case ',':
			case '\0':
				if(f & a_STAGE_ANGLE) /* FIXME ok with RELAX ?? fix it up!!  DITO ;!! */
					goto jleave;
				goto jdomain_done;
			case '>':
				if(!(f & a_STAGE_ANGLE))
					goto jleave;
jdomain_done:
				ASSERT(cp == acp->ac_domain);
				if(l == 0)
					goto jleave;
				if(cp[l - 1] == '.'){
					if(!(acp->ac_.mse & su_IMF_MODE_RELAX)){
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_CONTENT;
						goto jleave;
					}
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_CONTENT;
				}
jdomain_done_no_len_check:
				ASSERT(cp == acp->ac_domain);
				acp->ac_.domain = l;
				l = 0;
				f ^= a_STAGE_DOMAIN | a_STAGE_REST_OR_SEP;
				f &= ~(a_MASK | a_STAGE_ANGLE);
				if(c == ';' || c == ','){
					--acp->ac_.hd;
					goto jaddr_step_create;
				}
				if(c == '\0')
					goto jaddr_step_create;
				continue;
			default:
				if(!(acp->ac_.mse & su_IMF_MODE_DOMAIN_XLABEL))
					goto jleave;
				acp->ac_.mse |= su_IMF_STATE_DOMAIN_XLABEL;
				cp[l++] = c;
				continue;
			}
		}else if(f & a_STAGE_ANGLE){
			switch(c){
			case '.':
				/* Inside an angle-addr it is dot-atom not atext */
				if(l == 0 || cp[l - 1] == '.')
					goto jleave;
				cp[l++] = c;
				if(cpalter != NIL)
					*cpalter++ = c;
				continue;
			case '@':
				if(f & a_ROUTE)
					goto jleave;
				/* If not yet it can be a route  */
				if(!(f & a_ROUTE_SEEN) && l == 0){
					f |= a_ROUTE;
					continue;
				}
				ASSERT(cp == acp->ac_domain);
				goto jlocpar_copy;
			case ',':
				if(!(f & a_ROUTE))
					goto jleave;
				/* Must be route end or next hop */
				for(;;){
					if(su__imf_s_CFWS(acp) == FAL0)
						goto jleave;
					c = *acp->ac_.hd++;
					if(c == ':')
						goto jroute_end;
					if(c != ',')
						break;
					/* Allow empty entries */
				}
				if(c != '@')
					goto jleave;
				continue;
			case ':':
				if(!(f & a_ROUTE))
					goto jleave;
jroute_end:
				/* Real address to follow */
				f ^= a_ROUTE | a_ROUTE_SEEN;
				acp->ac_.comm = 0;
				l = 0;
				cpalter = acp->ac_locpar;
				ASSERT(cp == acp->ac_domain);
				continue;
			case '>':
				if(f & a_ROUTE)
					goto jleave;
				if(!(acp->ac_.mse & su_IMF_MODE_ADDR_SPEC_NO_DOMAIN))
					goto jleave;
				/* STAGE_ANGLE stripped by jdomain_done! */
				acp->ac_.mse |= su_IMF_STATE_ADDR_SPEC_NO_DOMAIN;
				ASSERT(cp == acp->ac_domain);
				goto jlocpar_copy;
			default:
				goto jleave;
			}
		}else if(f & a_STAGE_DOT){
			switch(c){
			case '.':
				/* We do not know what we are doing for now, keep on going */
				goto jpushany;
			case '@':
				/* An AT "verifies" we were indeed a local-part, now parse domain */
jlocpar_copy:
				if(l == 0)
					goto jleave;
				acp->ac_.locpar = S(u32,P2UZ(cpalter - acp->ac_locpar));

				/* Catch double-dot, and leading and trailing dot; eg "".@ parsed fine so far */
				f |= a_TMP;
				while(l-- != 0){
					if(*--cpalter == '.'){
						if(f & a_TMP)
							goto jleave;
						f |= a_TMP;
					}else
						f &= ~a_TMP;
				}
				if(f & a_TMP)
					goto jleave;

				cp = acp->ac_domain;
				cpalter = NIL;
				l = 0;
				if(f & a_QUOTE){
					*--acp->ac_locpar = '"';
					acp->ac_locpar[++acp->ac_.locpar] = '"';
					++acp->ac_.locpar;
				}
				f &= ~(a_STAGE_DOT | a_MASK);
				f |= a_STAGE_DOMAIN;
				if(acp->ac_.mse & su_IMF_STATE_ADDR_SPEC_NO_DOMAIN)
					goto jdomain_done_no_len_check;
				continue;
			case '<':
				/* Otherwise maybe a 'Dr. Z <y@x>' was falsely seen as a local-part?
				 * .. Then this should be an angle-addr *now* for us to deal with it */
				if(acp->ac_.mse & su_IMF_MODE_DISPLAY_NAME_DOT){
					acp->ac_.mse |= su_IMF_STATE_DISPLAY_NAME_DOT;
					f |= a_QUOTE;
					goto jangleme;
				}
				FALLTHRU
			default:
				rv = su_IMF_ERR_DISPLAY_NAME_DOT;
				goto jleave;
			}
		}else{
			switch(c){
			case '<':
				/* This switches to angle-addr parse mode for sure */
jangleme:
				ASSERT(cp == acp->ac_display_name);
				acp->ac_.display_name = l;
				if(f & (a_QUOTE | a_QUOTE_ANYHOW)){
					*--acp->ac_display_name = '"';
					acp->ac_display_name[++acp->ac_.display_name] = '"';
					++acp->ac_.display_name;
				}
				cp = acp->ac_domain; /* = su_path_null */
				cpalter = acp->ac_locpar;
				l = 0;
				f &= ~(a_STAGE_MASK | a_MASK);
				f |= a_STAGE_ANGLE;
				continue;
			case '.':
				/* Either parse a local-part without knowing; else IMF_MODE_DISPLAY_NAME_DOT */
				f |= a_STAGE_DOT;
				goto jpushany;
			case '@':
				if(!(f & a_ANY))
					goto jleave;
				/* AT "verifies" we were indeed a local-part, now parse domain */
				goto jlocpar_copy;
			case ':':
				/* Group start */
				if(acp->ac_.mse & su_IMF_STATE_GROUP)
					goto jleave;
				acp->ac_.mse |= su_IMF_STATE_GROUP_START | su_IMF_STATE_GROUP;
				if(f & a_ANY){
					su_mem_copy(acp->ac_group_display_name, cp, l);
					if(f & a_QUOTE){
						*--acp->ac_group_display_name = '"';
						acp->ac_group_display_name[++l] = '"';
						++l;
					}
					acp->ac_.group_display_name = l;
				}else{
					ASSERT(!(f & a_QUOTE));
					if(acp->ac_.mse & su_IMF_MODE_RELAX)
						acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY;
					else{
						rv = su_IMF_ERR_RELAX | su_IMF_ERR_GROUP_DISPLAY_NAME_EMPTY;
						goto jleave;
					}
				}
				acp->ac_.comm = 0;
				cpalter = acp->ac_locpar;
				l = 0;
				f &= ~(a_ANY | a_WS | a_QUOTE | a_QUOTE_ANYHOW);
				continue;
			case ';':
				if((f & a_NEED_SEP) || !(acp->ac_.mse & su_IMF_STATE_GROUP))
					goto jleave;
				f |= a_NEED_SEP;
				acp->ac_.comm = 0;
				if(acp->ac_.mse & su_IMF_STATE_GROUP_START){
					acp->ac_.mse |= su_IMF_STATE_GROUP_END | su_IMF_STATE_GROUP_EMPTY;
					a_imf_addr_create(acp, membp, &app);
					goto jlist_next; /* No STOP_EARLY, there was no address */
				}
				acp->ac_.mse &= su__IMF_MODE_ADDR_MASK;
				if(*app_base != NIL){
					struct su_imf_addr *ap;

					for(ap = *app_base; ap->imfa_next != NIL; ap = ap->imfa_next){
					}
					ASSERT(ap->imfa_mse & su_IMF_STATE_GROUP);
					ap->imfa_mse |= su_IMF_STATE_GROUP_END;
				}
				continue;
			case ',':
				f &= ~(a_NEED_SEP | a_WS);
				acp->ac_.comm = 0;
				continue;
			case '\0':
				acp->ac_.comm = 0;
				f &= ~a_WS;
				if((acp->ac_.mse & (su_IMF_MODE_RELAX | su_IMF_STATE_GROUP_START)) ==
						(su_IMF_MODE_RELAX | su_IMF_STATE_GROUP_START)){
					acp->ac_.mse |= su_IMF_STATE_RELAX | su_IMF_STATE_GROUP_END |
							su_IMF_STATE_GROUP_EMPTY | su_IMF_ERR_GROUP_OPEN;
					a_imf_addr_create(acp, membp, &app);
					/* No STOP_EARLY, there was no address */
				}else if(*app_base == NIL)
					goto jleave;
				goto jlist_done;
			default:
				goto jleave;
			}
		}
	}

jlist_done:
	rv = su_ERR_NONE;
jleave:
	header = acp->ac_.hd;
j_leave:
	if(endptr_or_nil != NIL)
		*endptr_or_nil = header;

	NYD_OU;
	return rv;
} /* }}} */

#include <su/y-imf.h> /* 3. */

#include "su/code-ou.h"
#endif /* su_HAVE_IMF */
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_IMF_ADDR
/* s-itt-mode */
