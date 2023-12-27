/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-ali-alt.h.
 *@ XXX Use a su_cs_set for alternates stuff?
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_ali_alt
#define mx_SOURCE
#define mx_SOURCE_CMD_ALI_ALT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/sort.h>

#include "mx/cmd.h"
#include "mx/cmd-mlist.h"
#include "mx/names.h"

#include "mx/cmd-ali-alt.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* ..of a_calal_alias_dp.
 * We rely on resorting, and use has_key()...lookup() (a_calal_alias_expand()).
 * The value is a n_strlist*, which we manage directly (no toolbox).
 * name::n_name, after .sl_dat[.sl_len] one boole that indicates recursion-allowed,
 * thereafter name::n_fullname (empty if EQ n_name) */
#define a_CALAL_ALIAS_FLAGS (su_CS_DICT_HEAD_RESORT | su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_CALAL_ALIAS_THRESHOLD 2

/* ..of a_calal_a8s_dp */
#define a_CALAL_A8S_FLAGS (su_CS_DICT_CASE | su_CS_DICT_AUTO_SHRINK | su_CS_DICT_ERR_PASS)
#define a_CALAL_A8S_THRESHOLD 2

static struct su_cs_dict *a_calal_alias_dp, a_calal_alias__d;
static struct su_cs_dict *a_calal_a8s_dp, a_calal_a8s__d;

DVL( static void a_calal__on_gut(BITENUM(u32,su_state_gut_flags) flags); )

/* Recursively expand an alias, adjust+return adjusted nlist result; limit expansion. metoo=*metoo* */
static struct mx_name *a_calal_alias_expand(uz level, struct mx_name *nlist, char const *name, int ntype, boole metoo);

static struct n_strlist *a_calal_alias_dump(char const *cmdname, char const *key, void const *dat);

/* Mark all (!file, !pipe) nodes with the given name */
static struct mx_name *a_calal_a8s_mark_name(struct mx_name *np, char const *name, boole *isallnet);

static struct n_strlist *a_calal_a8s_dump(char const *cmdname, char const *key, void const *dat);

#if DVLOR(1, 0)
static void
a_calal__on_gut(BITENUM(u32,su_state_gut_flags) flags){
	NYD2_IN;

	if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
		struct su_cs_dict_view dv;
		void *vp;
		struct n_strlist *slp;

		if(a_calal_alias_dp != NIL){
			su_cs_dict_view_setup(&dv, a_calal_alias_dp);
			su_CS_DICT_VIEW_FOREACH(&dv){
				slp = S(struct n_strlist*,su_cs_dict_view_data(&dv));
				do{
					vp = slp;
					slp = slp->sl_next;
					su_FREE(vp);
				}while(slp != NIL);
			}

			su_cs_dict_gut(&a_calal_alias__d);
		}

		if(a_calal_a8s_dp != NIL)
			su_cs_dict_gut(&a_calal_a8s__d);
	}

	a_calal_alias_dp = NIL;
	a_calal_a8s_dp = NIL;

	NYD2_OU;
}
#endif /* DVLOR(1,0) */

static struct mx_name *
a_calal_alias_expand(uz level, struct mx_name *nlist, char const *name, int ntype, boole metoo){
	struct mx_name *np, *nlist_tail;
	char const *fn, *sn;
	struct n_strlist const *slp, *slp_base, *slp_next;
	NYD2_IN;
	ASSERT_NYD(a_calal_alias_dp != NIL);
	ASSERT(mx_alias_is_valid_name(name));

	UNINIT(slp_base, NIL);

	if(UCMP(z, level++, ==, mx_ALIAS_MAXEXP)){ /* TODO not a real error!! */
		n_err(_("alias: stopping recursion at depth %d\n"), mx_ALIAS_MAXEXP);
		slp_next = NIL;
		fn = sn = name;
		goto jmay_linkin;
	}

	slp_next = slp_base = slp = S(struct n_strlist const*,su_cs_dict_lookup(a_calal_alias_dp, name));

	if(slp == NIL){
		fn = sn = name;
		goto jmay_linkin;
	}

	do{ /*while((slp = slp_next) != NIL);*/
		slp_next = slp->sl_next;

		if(slp->sl_len == 0)
			continue;

		/* Cannot shadow itself */
		if(su_cs_cmp(name, sn = slp->sl_dat)){
			/* Recursion allowed for target? */
			if(sn[slp->sl_len + 1] != FAL0){
				nlist = a_calal_alias_expand(level, nlist, sn, ntype, metoo);
				continue;
			}
		}

		/* Use .n_name if .n_fullname is not set */
		fn = &sn[slp->sl_len + 2];
		if(*fn == '\0')
			fn = sn;

jmay_linkin:
		if(!metoo && mx_name_is_metoo_cp(sn, FAL0))
			continue;

		if((np = mx_name_parse_as_one(fn, ntype)) != NIL){
			if((nlist_tail = nlist) != NIL){ /* XXX su_list_push()! */
				while(nlist_tail->n_flink != NIL)
					nlist_tail = nlist_tail->n_flink;
				nlist_tail->n_flink = np;
				np->n_blink = nlist_tail;
			}else
				nlist = np;
		}
	}while((slp = slp_next) != NIL);

	NYD2_OU;
	return nlist;
}

static struct n_strlist *
a_calal_alias_dump(char const *cmdname, char const *key, void const *dat){
	struct n_string s_b, *s;
	struct n_strlist *slp;
	NYD2_IN;

	s = n_string_creat_auto(&s_b);
	s = n_string_resize(s, 511);
	s = n_string_trunc(s, VSTRUCT_SIZEOF(struct n_strlist,sl_dat)); /* gross */

	s = n_string_push_cp(s, cmdname);
	s = n_string_push_c(s, ' ');
	s = n_string_push_cp(s, key); /*n_shexp_quote_cp(key, TRU1); valid alias */

	for(slp = UNCONST(struct n_strlist*,dat); slp != NIL; slp = slp->sl_next){
		s = n_string_push_c(s, ' ');
		/* Use .n_fullname if available, fall back to .n_name */
		key = &slp->sl_dat[slp->sl_len + 2];
		if(*key == '\0')
			key = slp->sl_dat;
		s = n_string_push_cp(s, n_shexp_quote_cp(key, TRU1));
	}

	slp = C(struct n_strlist*,S(void const*,n_string_cp(s)));
	slp->sl_next = NIL;
	slp->sl_len = s->s_len - VSTRUCT_SIZEOF(struct n_strlist,sl_dat);

	NYD2_OU;
	return slp;
}

static struct mx_name *
a_calal_a8s_mark_name(struct mx_name *np, char const *name, boole *isallnet){
	struct mx_name *p;
	NYD2_IN;

	for(p = np; p != NIL; p = p->n_flink)
		if(!(p->n_type & GDEL) &&
				!(p->n_flags & (S(u32,S32_MIN) | mx_NAME_ADDRSPEC_ISFILE | mx_NAME_ADDRSPEC_ISPIPE)) &&
				mx_name_is_same_cp(p->n_name, name, isallnet))
			p->n_flags |= S(u32,S32_MIN);

	NYD2_OU;
	return np;
}

static struct n_strlist *
a_calal_a8s_dump(char const *cmdname, char const *key, void const *dat){
	/* XXX real strlist + str_to_fmt() */
	struct n_strlist *slp;
	uz kl;
	NYD2_IN;
	UNUSED(cmdname);
	UNUSED(dat);

	/*key = n_shexp_quote_cp(key, TRU1); plain address: not needed */
	kl = su_cs_len(key);

	slp = n_STRLIST_AUTO_ALLOC(kl +1);
	slp->sl_next = NIL;
	slp->sl_len = kl;
	su_mem_copy(slp->sl_dat, key, kl +1);

	NYD2_OU;
	return slp;
}

int
c_alias(void *vp){ /* {{{ */
	struct su_cs_dict_view dv;
	union {void const *cvp; char const *cp; boole haskey; struct n_strlist *slp;} dat;
	int rv;
	char const **argv, *key;
	NYD_IN;

	argv = S(char const**,vp);
	if((key = *argv) == NIL)
		goto jdump;

	/* Full expansion?  metoo=-,-+, !metoo=--, */
	rv = su_EX_OK;
	if(argv[1] != NIL && argv[2] == NIL && key[0] == '-' &&
			(key[1] == '\0' || (key[2] == '\0' &&
				((key[1] == '-' ? (rv = TRU2) : FAL0) ||
				 (key[1] == '+' ? (rv = TRU1) : FAL0)))))
		key = argv[1];

	if(!mx_alias_is_valid_name(key)){
		dat.cp = N_("not a valid name");
		goto jerr;
	}

	if(a_calal_alias_dp != NIL && su_cs_dict_view_find(su_cs_dict_view_setup(&dv, a_calal_alias_dp), key))
		dat.cvp = su_cs_dict_view_data(&dv);
	else
		dat.cvp = NIL;

	if(argv[1] == NIL || key == argv[1]){
		if(dat.cvp != NIL)
			goto jlookup;
		dat.cp = N_("no such alias");
		goto jerr;
	}else
		goto jmod;

jleave:
	NYD_OU;
	return rv;

jerr:
	n_err(_("alias: %s: %s\n"), V_(dat.cp), n_shexp_quote_cp(key, FAL0));
	rv = su_EX_ERR;
	goto jleave;

jdump:
	dat.slp = NIL;
	rv = (mx_xy_dump_dict("alias", a_calal_alias_dp, &dat.slp, NIL, &a_calal_alias_dump) &&
			mx_page_or_print_strlist("alias", dat.slp, FAL0)) ? su_EX_OK : su_EX_ERR;
	goto jleave;

jlookup:
	if(argv[1] == NIL){
		dat.slp = a_calal_alias_dump("alias", key, dat.cvp);
		rv = (fputs(dat.slp->sl_dat, n_stdout) == EOF);
		rv |= (putc('\n', n_stdout) == EOF);
	}else{
		struct mx_name *np;

		if(rv == su_EX_OK)
			rv = ok_blook(metoo);
		else if(rv == TRU2)
			rv = FAL0;
		else{
			ASSERT(rv == TRU1);
		}
		np = a_calal_alias_expand(0, NIL, key, 0, S(boole,rv));
		np = mx_namelist_elide(np);

		rv = (fprintf(n_stdout, "alias %s", key) < 0);
		if(!rv){
			for(; np != NIL; np = np->n_flink){
				rv |= (putc(' ', n_stdout) == EOF);
				rv |= (fputs(n_shexp_quote_cp(np->n_fullname, TRU1), n_stdout) == EOF);
			}
			rv |= (putc('\n', n_stdout) == EOF);
		}
		rv = rv ? su_EX_ERR : su_EX_OK;
	}
	goto jleave;

jmod:/* C99 */{
	struct n_strlist *head, **tailp;
	boole exists;
	char const *val1, *val2;

	if(a_calal_alias_dp == NIL){
		a_calal_alias_dp = su_cs_dict_set_threshold(
				su_cs_dict_create(&a_calal_alias__d, a_CALAL_ALIAS_FLAGS, NIL),
				a_CALAL_ALIAS_THRESHOLD);
		DVL(if(a_calal_a8s_dp == NIL)
			su_state_on_gut_install(&a_calal__on_gut, FAL0, su_STATE_ERR_NOPASS);)
	}

	if((exists = (head = dat.slp) != NIL)){
		while(dat.slp->sl_next != NIL)
			dat.slp = dat.slp->sl_next;
		tailp = &dat.slp->sl_next;
	}else
		head = NIL, tailp = &head;

	while((val1 = *++argv) != NIL){
		uz l1, l2;
		struct mx_name *np;
		boole norecur, name_eq_fullname;

		if((norecur = (*val1 == '\\')))
			++val1;

		/* We need to allow empty targets */
		name_eq_fullname = TRU1;
		if(*val1 == '\0')
			val2 = val1;
		else if((np = mx_name_parse_as_one(val1, GIDENT)) != NIL){
			val1 = np->n_name;
			val2 = np->n_fullname;
			if((name_eq_fullname = !su_cs_cmp(val1, val2)))
				val2 = su_empty;
		}else{
			n_err(_("alias: %s: invalid argument: %s\n"), key, n_shexp_quote_cp(val1, FAL0));
			rv = su_EX_ERR;
			goto jleave;
		}

		l1 = su_cs_len(val1) +1;
		l2 = su_cs_len(val2) +1;
		dat.slp = n_STRLIST_ALLOC(l1 + 1 + l2);
		*tailp = dat.slp;
		dat.slp->sl_next = NIL;
		tailp = &dat.slp->sl_next;
		dat.slp->sl_len = l1 -1;
		su_mem_copy(dat.slp->sl_dat, val1, l1);
		dat.slp->sl_dat[l1++] = (!norecur && name_eq_fullname && mx_alias_is_valid_name(val1));
		su_mem_copy(&dat.slp->sl_dat[l1], val2, l2);
	}

	if(exists){
		su_cs_dict_view_set_data(&dv, head);
		rv = su_EX_OK;
	}else
		rv = (su_cs_dict_insert(a_calal_alias_dp, key, head) == su_ERR_NONE) ? su_EX_OK : su_EX_ERR;
	}goto jleave;
} /* }}} */

int
c_unalias(void *vp){ /* XXX how about toolbox and generic unxy_dict()? {{{ */
	struct su_cs_dict_view dv;
	struct n_strlist *slp;
	char const **argv, *key;
	int rv;
	NYD_IN;

	rv = su_EX_OK;

	if(a_calal_alias_dp != NIL)
		su_cs_dict_view_setup(&dv, a_calal_alias_dp);

	key = (argv = vp)[0];
	do{
		if(key[1] == '\0' && key[0] == '*'){
			if(a_calal_alias_dp != NIL){
				su_CS_DICT_VIEW_FOREACH(&dv){
					slp = S(struct n_strlist*,su_cs_dict_view_data(&dv));
					do{
						vp = slp;
						slp = slp->sl_next;
						su_FREE(vp);
					}while(slp != NIL);
				}
				su_cs_dict_clear(a_calal_alias_dp);
			}
		}else if(a_calal_alias_dp == NIL || !su_cs_dict_view_find(&dv, key)){
			n_err(_("No such `alias': %s\n"), n_shexp_quote_cp(key, FAL0));
			rv = su_EX_ERR;
		}else{
			slp = S(struct n_strlist*,su_cs_dict_view_data(&dv));
			do{
				vp = slp;
				slp = slp->sl_next;
				su_FREE(vp);
			}while(slp != NIL);
			su_cs_dict_view_remove(&dv);
		}
	}while((key = *++argv) != NIL);

	NYD_OU;
	return rv;
} /* }}} */

boole
mx_alias_is_valid_name(char const *name){
	char c;
	char const *cp;
	boole rv;
	NYD2_IN;

	for(rv = TRU1, cp = name; (c = *cp) != '\0'; ++cp){
		/* User names, plus things explicitly mentioned in Postfix aliases(5).
		 * Plus extensions.	On change adjust *mta-aliases* and impl., too */
		/* TODO alias_is_valid_name(): locale dependent validity check,
		 * TODO with Unicode prefix valid UTF-8! */
		if(!su_cs_is_alnum(c) && c != '_'){
			if(cp == name ||
					(c != '-' &&
					/* Extensions, but mentioned by Postfix */
					c != '#' && c != ':' && c != '@' &&
					/* Extensions */
					c != '!' && c != '.' && !(S(u8,c) & 0x80) &&
					!(c == '$' && cp[1] == '\0'))){
				rv = FAL0;
				break;
			}
		}
	}

	NYD2_OU;
	return rv;
}

boole
mx_alias_exists(char const *name){
	boole rv;
	NYD2_IN;

	rv = (a_calal_alias_dp != NIL && su_cs_dict_has_key(a_calal_alias_dp, name));

	NYD2_OU;
	return rv;
}

struct mx_name *
mx_alias_expand(struct mx_name *np, boole force_metoo){
	NYD2_IN;

	np = a_calal_alias_expand(0, NIL, np->n_name, np->n_type, force_metoo);

	NYD2_OU;
	return np;
}

struct mx_name *
mx_alias_expand_list(struct mx_name *names, boole force_metoo){ /* {{{ */
	struct su_cs_dict_view dv;
	struct mx_name *nlist, **nlist_tail, *np, *nxtnp;
	boole metoo;
	NYD_IN;

	metoo = (force_metoo || ok_blook(metoo));
	nlist = NIL;
	nlist_tail = &nlist;
	np = names;

	if(a_calal_alias_dp != NIL)
		su_cs_dict_view_setup(&dv, a_calal_alias_dp);

	for(; np != NIL; np = nxtnp){
		ASSERT(!(np->n_type & GDEL)); /* TODO legacy */
		nxtnp = np->n_flink;

		/* Only valid alias names may enter expansion */
		if(mx_name_is_fileorpipe(np) || (np->n_name != np->n_fullname &&
					su_cs_cmp(np->n_name, np->n_fullname)) ||
				a_calal_alias_dp == NIL || !su_cs_dict_view_find(&dv, np->n_name)){
			np->n_blink = *nlist_tail;
			np->n_flink = NIL;
			*nlist_tail = np;
			nlist_tail = &np->n_flink;
		}else{
			nlist = a_calal_alias_expand(0, nlist, np->n_name, np->n_type, metoo);
			if((np = nlist) == NIL)
				nlist_tail = &nlist;
			else for(;; np = np->n_flink)
				if(np->n_flink == NIL){
					nlist_tail = &np->n_flink;
					break;
				}
		}
	}

	NYD_OU;
	return nlist;
} /* }}} */

int
c_alternates(void *vp){ /* {{{ */
	struct n_string s_b, *s;
	int rv;
	struct mx_cmd_arg *cap;
	struct mx_cmd_arg_ctx *cacp;
	NYD_IN;

	n_pstate_err_no = su_ERR_NONE;
	cacp = vp;
	cap = cacp->cac_arg;

	if(cacp->cac_no == 0){
		struct n_strlist *slp;

		slp = NIL;
		rv = mx_xy_dump_dict("alternates", a_calal_a8s_dp, &slp, NIL, &a_calal_a8s_dump) ? su_EX_OK : su_EX_ERR;

		if(rv == su_EX_OK){
			s = n_string_creat_auto(&s_b);
			s = n_string_book(s, 500); /* xxx */

			for(; slp != NIL; slp = slp->sl_next){
				if(s->s_len > 0)
					s = n_string_push_c(s, ' ');
				s = n_string_push_buf(s, slp->sl_dat, slp->sl_len);
			}
			n_string_cp(s);

			if(cacp->cac_vput != NIL){
				if(!n_var_vset(cacp->cac_vput, R(up,s->s_dat), cacp->cac_scope_vput)){
					n_pstate_err_no = su_ERR_NOTSUP;
					rv = su_EX_ERR;
				}
			}else if(*s->s_dat != '\0') /* xxx s->s_len>0 */
				rv = (fprintf(n_stdout, "alternates %s\n", s->s_dat) >= 0) ? su_EX_OK : su_EX_ERR;
			else
				rv = (fprintf(n_stdout, _("# no alternates registered\n")) >= 0) ? su_EX_OK : su_EX_ERR;
		}
	}else if(cacp->cac_vput != NIL){ /* XXX cmd_arg parser, subcommand.. */
			n_err(_("alternates: `vput' only supported in \"show\" mode\n"));
			n_pstate_err_no = su_ERR_NOTSUP;
			rv = su_EX_ERR;
	}else{
		if(a_calal_a8s_dp == NIL){
			a_calal_a8s_dp = su_cs_dict_set_threshold(
					su_cs_dict_create(&a_calal_a8s__d, a_CALAL_A8S_FLAGS, NIL),
					a_CALAL_A8S_THRESHOLD);
			DVL(if(a_calal_alias_dp == NIL)
				su_state_on_gut_install(&a_calal__on_gut, FAL0, su_STATE_ERR_NOPASS);)
		}
		/* In POSIX mode this command declares a, not appends to a list */
		else if(ok_blook(posix))
			su_cs_dict_clear_elems(a_calal_a8s_dp);

		for(rv = su_EX_OK; cap != NIL; cap = cap->ca_next){
			struct mx_name *np;

			if((np = mx_name_parse_as_one(cap->ca_arg.ca_str.s, 0)) == NIL ||
					(np = mx_namelist_check(np, mx_EACM_STRICT, NIL)) == NIL){
				n_err(_("Invalid `alternates' argument: %s\n"),
					n_shexp_quote_cp(cap->ca_arg.ca_str.s, FAL0));
				n_pstate_err_no = su_ERR_INVAL;
				rv = su_EX_ERR;
				continue;
			}

			if(su_cs_dict_replace(a_calal_a8s_dp, np->n_name, NIL) > 0){
				n_err(_("Failed to create `alternates' storage: %s\n"),
					n_shexp_quote_cp(np->n_name, FAL0));
				n_pstate_err_no = su_ERR_INVAL;
				rv = su_EX_ERR;
			}
		}
	}

	NYD_OU;
	return rv;
} /* }}} */

int
c_unalternates(void *vp){
	int rv;
	NYD_IN;

	rv = !mx_unxy_dict("alternates", a_calal_a8s_dp, vp);

	NYD_OU;
	return rv;
}

boole
mx_alternates_exists(char const *name, boole allnet){
	NYD2_IN;

	if(a_calal_a8s_dp != NIL){
		if(!allnet){
			if(su_cs_dict_has_key(a_calal_a8s_dp, name))
				goto jleave;
		}else{
			struct su_cs_dict_view dv;

			su_CS_DICT_FOREACH(a_calal_a8s_dp, &dv)
				if(mx_name_is_same_cp(name, su_cs_dict_view_key(&dv), &allnet))
					goto jleave;
		}
	}

	name = NIL;
jleave:
	NYD2_OU;
	return (name != NIL);
}

struct mx_name *
mx_alternates_remove(struct mx_name *np, boole keep_single){ /* {{{ */
	/* XXX keep a single pointer, initial null, and immediate remove nodes
	 * XXX on successful match unless keep single and that pointer null! */
	struct mx_name *xp, *newnp;
	boole isall;
	NYD_IN;

	isall = TRUM1;

	/* Delete the temporary bit from all */
	for(xp = np; xp != NIL; xp = xp->n_flink)
		xp->n_flags &= ~S(u32,S32_MIN);

	/* Mark all possible alternate names (xxx sic: instead walk over namelist
	 * and hash-lookup alternate instead (unless *allnet*) */
	if(a_calal_a8s_dp != NIL){
		struct su_cs_dict_view dv;

		su_CS_DICT_FOREACH(a_calal_a8s_dp, &dv)
			np = a_calal_a8s_mark_name(np, su_cs_dict_view_key(&dv), &isall);
	}

	np = a_calal_a8s_mark_name(np, ok_vlook(LOGNAME), &isall);

	if((xp = mx_name_parse_as_one(ok_vlook(sender), GIDENT)) != NIL)
		np = a_calal_a8s_mark_name(np, xp->n_name, &isall);
	else for(xp = mx_name_parse(ok_vlook(from), GIDENT); xp != NIL; xp = xp->n_flink)
		np = a_calal_a8s_mark_name(np, xp->n_name, &isall);

	/* C99 */{
		char const *v15compat;

		if((v15compat = ok_vlook(replyto)) != NIL){
			n_OBSOLETE(_("please use *reply-to*, not *replyto*"));
			for(xp = mx_name_parse(v15compat, GIDENT); xp != NIL; xp = xp->n_flink)
				np = a_calal_a8s_mark_name(np, xp->n_name, &isall);
		}
	}

	for(xp = mx_name_parse(ok_vlook(reply_to), GIDENT); xp != NIL; xp = xp->n_flink)
		np = a_calal_a8s_mark_name(np, xp->n_name, &isall);

	/* Clean the list by throwing away all deleted or marked (but one) nodes */
	for(xp = newnp = NIL; np != NIL; np = np->n_flink){
		if(np->n_type & GDEL)
			continue;
		if(np->n_flags & S(u32,S32_MIN)){
			if(!keep_single)
				continue;
			keep_single = FAL0;
		}

		np->n_blink = xp;
		if(xp != NIL)
			xp->n_flink = np;
		else
			newnp = np;
		xp = np;
		xp->n_flags &= ~S(u32,S32_MIN);
	}
	if(xp != NIL)
		xp->n_flink = NIL;
	np = newnp;

	NYD_OU;
	return np;
} /* }}} */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_ALI_ALT
/* s-itt-mode */
