/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of fexpand.h.
 *@ TODO v15: peek signal states while opendir/readdir/etc.
 *@ TODO "Magic solidus" used as path separator.
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
#define su_FILE fexpand
#define mx_SOURCE
#define mx_SOURCE_FEXPAND

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <pwd.h>

#ifdef mx_HAVE_FNMATCH
# include <dirent.h>
# include <fnmatch.h>
#endif

#include <su/cs.h>
#include <su/sort.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>

#include "mx/cmd-shortcut.h"

#include "mx/fexpand.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct a_fexpand_ctx{
	char const *fc_name;
	BITENUM(u32,mx_fexp_mode) fc_fexpm;
	boole fc_multi_ok;
	u8 fc__pad[3];
	char **fc_res;
};

#ifdef mx_HAVE_FNMATCH
struct a_fexpand_glob_ctx{
	char const *fgc_patdat; /* Remaining pattern (at and below level) */
	uz fgc_patlen;
	struct n_string *fgc_outer; /* Resolved path up to this level */
	u32 fgc_flags;
	su_64(u8 fgc__pad[4];)
};

struct a_fexpand_glob_one_ctx{
	struct a_fexpand_glob_ctx *fgoc_fgcp;
	struct a_fexpand_glob_ctx *fgoc_new_fgcp;
	struct n_strlist **fgoc_slpp;
	uz fgoc_old_outer_len;
	sz fgoc_dt_type; /* Can be -1 even if mx_HAVE_DIRENT_TYPE */
	char const *fgoc_name;
};
#endif

/* Logic behind *fexpand() */
static void a_fexpand(struct a_fexpand_ctx *fcp);

/* Locate the user's mailbox file (where new, unread mail is queued) */
static char *a_fexpand_findmail(char const *user, boole force);

/* Expand ^~/? and ^~USER/? constructs.
 * Returns the completely resolved (maybe empty or identical to input)
 * AUTO_ALLOC()ed string */
static char *a_fexpand_tilde(char const *s);

/* Perform fnmatch(3).  Returns an ERR_ */
static s32 a_fexpand_globname(struct a_fexpand_ctx *fcp);

#ifdef mx_HAVE_FNMATCH
static boole a_fexpand_glob__it(struct a_fexpand_glob_ctx *fgcp, struct n_strlist **slpp);
static char const *a_fexpand_glob__one(struct a_fexpand_glob_one_ctx *sgocp);
static sz a_fexpand_glob__sort(void const *cvpa, void const *cvpb);
#endif

static void
a_fexpand(struct a_fexpand_ctx *fcp){ /* {{{ */
	/* The order of evaluation is "%" and "#" expand into constants.  "&" can expand into "+".  "+" can expand into
	 * shell meta( character)s.  Shell metas expand into constants.  This way, we make no recursive expansion */
	struct str proto, s;
	char const *res, *cp;
	boole haveproto;
	s32 eno;
	NYD_IN;

	eno = su_ERR_NONE;

	if(!(fcp->fc_fexpm & mx_FEXP_SHORTCUT) || (res = mx_shortcut_expand(fcp->fc_name)) == NIL)
		res = fcp->fc_name;

jprotonext:
	UNINIT(proto.s, NIL), UNINIT(proto.l, 0);
	haveproto = FAL0;
	for(cp = res; *cp && *cp != ':'; ++cp)
		if(!su_cs_is_alnum(*cp))
			goto jnoproto;
	if(cp[0] == ':' && cp[1] == '/' && cp[2] == '/'){
		haveproto = TRU1;
		proto.s = UNCONST(char*,res);
		cp += 3;
		proto.l = P2UZ(cp - res);
		res = cp;
	}

jnoproto:
	if(!(fcp->fc_fexpm & mx_FEXP_NSPECIAL)){
jnext:
		switch(*res){
		case '%':
			if(res[1] == ':' && res[2] != '\0'){
				res = &res[2];
				goto jprotonext;
			}else{
				boole force;

				force = (res[1] != '\0');
				res = a_fexpand_findmail((force ? &res[1] : ok_vlook(LOGNAME)), force);
				if(force)
					goto jislocal;
			}
			goto jnext;
		case '#':
			if(res[1] != '\0')
				break;
			if(prevfile[0] == '\0'){
				n_err(_("No previous file\n"));
				res = NIL;
				eno = su_ERR_NODATA;
				goto jleave;
			}
			res = prevfile;
			goto jislocal;
		case '&':
			if(res[1] == '\0')
				res = ok_vlook(MBOX);
			break;
		default:
			break;
		}
	}

#ifdef mx_HAVE_IMAP
	if(res[0] == '@' && which_protocol(mailname, FAL0, FAL0, NIL) == PROTO_IMAP)
		res = str_concat_csvl(&s, protbase(mailname), "/", &res[1], NIL)->s;
#endif

	/* POSIX: if *folder* unset or null, "+" shall be retained */
	if(!(fcp->fc_fexpm & mx_FEXP_NFOLDER) && *res == '+' && *(cp = n_folder_query()) != '\0')
		res = str_concat_csvl(&s, cp, &res[1], NIL)->s;

	/* Do some meta expansions */
	if((fcp->fc_fexpm & (mx_FEXP_NSHELL | mx_FEXP_NVAR)) != mx_FEXP_NVAR){
		boole doexp;

		if(fcp->fc_fexpm & (mx_FEXP_LOCAL | mx_FEXP_LOCAL_FILE))
			doexp = TRU1;
		else{
			cp = haveproto ? savecat(savestrbuf(proto.s, proto.l), res) : res;
			switch(which_protocol(cp, TRU1, FAL0, NIL)){
			case n_PROTO_EML:
			case n_PROTO_FILE:
			case n_PROTO_MAILDIR:
				doexp = TRU1;
				break;
			default:
				doexp = FAL0;
				break;
			}
		}

		if(doexp){
			struct str shin;
			struct n_string shou, *shoup;

			/* XXX when backtick eval ++ prepend proto if haveproto!? */
			shin.s = UNCONST(char*,res);
			shin.l = UZ_MAX;
			shoup = n_string_creat_auto(&shou);
			for(;;){
				BITENUM(u32,n_shexp_state) shs;

				/* TODO shexp: take care: not include backtick eval once avail! */
				shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG_D_V | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
							n_SHEXP_PARSE_QUOTE_AUTO_DQ | n_SHEXP_PARSE_QUOTE_AUTO_CLOSE),
						mx_SCOPE_NONE, shoup, &shin, NIL);
				if(shs & n_SHEXP_STATE_STOP)
					break;
			}
			res = n_string_cp(shoup);
			/*shoup = n_string_drop_ownership(shoup);*/

			/* XXX haveproto?  might have changed?!? */
		}
	}

	if(!(fcp->fc_fexpm & mx_FEXP_NTILDE) && res[0] == '~')
		res = a_fexpand_tilde(res);

	if(!(fcp->fc_fexpm & mx_FEXP_NGLOB) && su_cs_first_of(res, "{}[]*?") != UZ_MAX){
		fcp->fc_name = res;
		if((eno = a_fexpand_globname(fcp)) != su_ERR_NONE){
			res = NIL;
			goto jleave;
		}

		if(fcp->fc_res[1] != NIL){
			if(fcp->fc_multi_ok)
				goto su_NYD_OU_LABEL;
			eno = su_ERR_RANGE;
			res = NIL;
			goto jleave;
		}
		res = fcp->fc_res[0];
	}

jislocal:
	if(res != NIL && haveproto)
		res = savecat(savestrbuf(proto.s, proto.l), res);

	if(fcp->fc_fexpm & (mx_FEXP_LOCAL | mx_FEXP_LOCAL_FILE)){
		switch(which_protocol(res, FAL0, FAL0, &cp)){
		case n_PROTO_MAILDIR:
			if(!(fcp->fc_fexpm & mx_FEXP_LOCAL_FILE)){
				FALLTHRU
		case n_PROTO_FILE:
		case n_PROTO_EML:
				/* Drop a possible PROTO:// etc */
				if(fcp->fc_fexpm & mx_FEXP_LOCAL_FILE)
					res = cp;
				break;
			}
			/* FALLTHRU */
		default:
			n_err(_("Not a local file or directory: %s\n"), n_shexp_quote_cp(fcp->fc_name, FAL0));
			res = NIL;
			eno = su_ERR_INVAL;
			break;
		}
	}

jleave:
	if(res == NIL){
		su_err_set(eno);
		fcp->fc_res = NIL;
	}else{
		uz l;

		/* Very ugly that res most likely is already heap */
		l = su_cs_len(res) +1;

		fcp->fc_res = S(char**,su_AUTO_ALLOC((sizeof(*fcp->fc_res) * 2) +l));
		fcp->fc_res[0] = R(char*,&fcp->fc_res[2]);
		fcp->fc_res[1] = NIL;
		su_mem_copy(fcp->fc_res[0], res, l);
	}

	NYD_OU;
} /* }}} */

static char *
a_fexpand_findmail(char const *user, boole force){
	char *rv;
	char const *cp;
	NYD2_IN;

	if(!force){
		if((cp = ok_vlook(inbox)) != NIL && *cp != '\0'){
			/* _NFOLDER extra introduced to avoid % recursion loops */
			if((rv = mx_fexpand(cp, mx_FEXP_NSPECIAL | mx_FEXP_NFOLDER | mx_FEXP_NSHELL)) != NIL)
				goto jleave;
			n_err(_("*inbox* expansion failed, using $MAIL/built-in: %s\n"), cp);
		}
		/* Heirloom compatibility: an IMAP *folder* becomes "%" */
#ifdef mx_HAVE_IMAP
		else if(cp == NIL && !su_cs_cmp(user, ok_vlook(LOGNAME)) &&
				which_protocol(cp = n_folder_query(), FAL0, FAL0, NIL) == PROTO_IMAP){
			/* TODO v15 Compat handling of *folder* with IMAP! */
			n_OBSOLETE("no more expansion of *folder* in \"%\": please set *inbox*");
			rv = savestr(cp);
			goto jleave;
		}
#endif

		if((cp = ok_vlook(MAIL)) != NIL){
			rv = savestr(cp);
			goto jleave;
		}
	}

	/* C99 */{
		uz ulen, i;

		ulen = su_cs_len(user) +1;
		i = sizeof(VAL_MAIL) -1 + 1 + ulen;

		rv = su_AUTO_ALLOC(i);
		su_mem_copy(rv, VAL_MAIL, (i = sizeof(VAL_MAIL) -1));
		rv[i] = '/';
		su_mem_copy(&rv[++i], user, ulen);
	}

jleave:
	NYD2_OU;
	return rv;
}

static char *
a_fexpand_tilde(char const *s){
	uz rl, nl;
	char const *rp, *np;
	char *rv;
	NYD2_IN;

	if(*(rp = &s[1]) == '/' || *rp == '\0'){
		np = ok_vlook(HOME);
		nl = su_cs_len(np);
		ASSERT(nl == 1 || (nl > 0 && np[nl - 1] != '/')); /* VIP var */
	}else{
		struct passwd *pwp;

		if((rp = su_cs_find_c(np = rp, '/')) != NIL){
			nl = P2UZ(rp - np);
			np = savestrbuf(np, nl);
		}

		if((pwp = getpwnam(np)) == NIL){
			rv = savestr(s);
			goto jleave;
		}
		nl = su_cs_len(np = pwp->pw_dir);
	}

	if(rp == NIL)
		rl = 0;
	else{
		while(*rp == '/')
			++rp;
		rl = su_cs_len(rp);
	}

	rv = su_AUTO_ALLOC(nl + 1 + rl +1);
	su_mem_copy(rv, np, nl);

	if(rl > 0){
		if(nl > 1) /* DIRSEP: assumes / == ROOT */
			rv[nl++] = '/';
		else{
			ASSERT(*rv == '/');
		}
		su_mem_copy(&rv[nl], rp, rl);
		nl += rl;
	}
	rv[nl] = '\0';

jleave:
	NYD2_OU;
	return rv;
}

static s32
a_fexpand_globname(struct a_fexpand_ctx *fcp){ /* {{{ */
#ifdef mx_HAVE_FNMATCH
	struct a_fexpand_glob_ctx sgc;
	struct n_string outer;
	struct n_strlist *slp;
	char *cp;
	void *lofi_snap;
	s32 rv;
	NYD_IN;

	rv = su_ERR_NONE;
	lofi_snap = su_mem_bag_lofi_snap_create(su_MEM_BAG_SELF);

	STRUCT_ZERO(struct a_fexpand_glob_ctx, &sgc);
	/* C99 */{
		uz i;

		sgc.fgc_patlen = i = su_cs_len(fcp->fc_name);
		sgc.fgc_patdat = cp = su_LOFI_ALLOC(++i);
		su_mem_copy(cp, fcp->fc_name, i);
		sgc.fgc_outer = n_string_book(n_string_creat(&outer), i);
	}
	/* a_fexpand_glob__it():a_SILENT */
	sgc.fgc_flags = ((fcp->fc_fexpm & mx_FEXP_SILENT) != 0);

	slp = NIL;
	if(a_fexpand_glob__it(&sgc, &slp))
		cp = R(char*,0x1);
	else
		cp = NIL;

	n_string_gut(&outer);

	if(cp == NIL){
		rv = su_ERR_INVAL;
		goto jleave;
	}

	if(slp == NIL){
		cp = UNCONST(char*,N_("File pattern does not match"));
		rv = su_ERR_NODATA;
		goto jerr;
	}else if(slp->sl_next == NIL){
		fcp->fc_res = S(char**,su_AUTO_ALLOC((sizeof(*fcp->fc_res) * 2) + slp->sl_len +1));
		fcp->fc_res[1] = NIL;
		cp = fcp->fc_res[0] = R(char*,&fcp->fc_res[2]);
		su_mem_copy(cp, slp->sl_dat, slp->sl_len +1);
	}else if(fcp->fc_multi_ok){
		struct n_strlist **sorta, *xslp;
		uz i, no, l;

		no = l = 0;
		for(xslp = slp; xslp != NIL; xslp = xslp->sl_next){
			++no;
			l += xslp->sl_len + 1;
		}
		ASSERT(no > 1);

		sorta = su_LOFI_ALLOC(sizeof(*sorta) * no);

		no = 0;
		for(xslp = slp; xslp != NIL; xslp = xslp->sl_next)
			sorta[no++] = xslp;
		su_sort_shell_vpp(su_S(void const**,sorta), no, &a_fexpand_glob__sort);

		fcp->fc_res = S(char**,su_AUTO_ALLOC((sizeof(*fcp->fc_res) * (no +1)) + ++l));
		fcp->fc_res[no] = NIL;
		cp = R(char*,&fcp->fc_res[no +1]);

		l = 0;
		for(i = 0; i < no; ++i){
			xslp = sorta[i];
			su_mem_copy(fcp->fc_res[i] = &cp[l], xslp->sl_dat, xslp->sl_len);
			l += xslp->sl_len;
			cp[l++] = '\0';
		}
		ASSERT(fcp->fc_res[no] == NIL);

		/* unrolled below su_LOFI_FREE(sorta);*/
	}else{
		cp = UNCONST(char*,N_("File pattern matches multiple results"));
		rv = su_ERR_RANGE;
		goto jerr;
	}

jleave:
	su_mem_bag_lofi_snap_unroll(su_MEM_BAG_SELF, lofi_snap);

	NYD_OU;
	return rv;

jerr:
	if(!(fcp->fc_fexpm & mx_FEXP_SILENT))
		n_err("%s: %s\n", V_(cp), n_shexp_quote_cp(fcp->fc_name, FAL0));
	goto jleave;

#else /* mx_HAVE_FNMATCH */
	UNUSED(fexpm);

	if(!(fexpm & mx_FEXP_SILENT))
		n_err(_("No filename pattern support (fnmatch(3) not available)\n"));

	fcp->fc_res[0] = fcp->fc_name;
	fcp->fc_res[1] = NIL;

	return su_ERR_NONE;
#endif
} /* }}} */

#ifdef mx_HAVE_FNMATCH /* {{{ */
static boole
a_fexpand_glob__it(struct a_fexpand_glob_ctx *fgcp, struct n_strlist **slpp){
	/* a_SILENT == a_fexpand_globname():((fexpm & mx_FEXP_SILENT) != 0) */
	enum{a_SILENT = 1<<0, a_DEEP=1<<1};

	struct a_fexpand_glob_ctx nsgc;
	struct a_fexpand_glob_one_ctx sgoc;
	struct dirent *dep;
	DIR *dp;
	char const *ccp, *myp;
	NYD2_IN;

	/* We need some special treatment for the outermost level.
	 * All along our way, normalize path separators */
	if(!(fgcp->fgc_flags & a_DEEP)){
		if(fgcp->fgc_patlen > 0 && fgcp->fgc_patdat[0] == '/'){
			myp = n_string_cp(n_string_push_c(fgcp->fgc_outer, '/'));
			do
				++fgcp->fgc_patdat;
			while(--fgcp->fgc_patlen > 0 && fgcp->fgc_patdat[0] == '/');
		}else
			myp = "./";
	}else
		myp = n_string_cp(fgcp->fgc_outer);

	sgoc.fgoc_fgcp = fgcp;
	sgoc.fgoc_new_fgcp = &nsgc;
	sgoc.fgoc_slpp = slpp;
	sgoc.fgoc_old_outer_len = fgcp->fgc_outer->s_len;

	/* Separate current directory/pattern level from any possible remaining pattern in order to be able to use it
	 * for fnmatch(3) */
	if((ccp = su_mem_find(fgcp->fgc_patdat, '/', fgcp->fgc_patlen)) == NIL)
		nsgc.fgc_patlen = 0;
	else{
		nsgc = *fgcp;
		nsgc.fgc_flags |= a_DEEP;
		fgcp->fgc_patlen = P2UZ((nsgc.fgc_patdat = &ccp[1]) - &fgcp->fgc_patdat[0]);
		nsgc.fgc_patlen -= fgcp->fgc_patlen;

		/* Trim solidus, everywhere */
		if(fgcp->fgc_patlen > 0){
			ASSERT(fgcp->fgc_patdat[fgcp->fgc_patlen -1] == '/');
			UNCONST(char*,fgcp->fgc_patdat)[--fgcp->fgc_patlen] = '\0';
		}
		while(nsgc.fgc_patlen > 0 && nsgc.fgc_patdat[0] == '/'){
			--nsgc.fgc_patlen;
			++nsgc.fgc_patdat;
		}
	}

	/* Quickshot: cannot be a fnmatch(3) pattern? */
	if(fgcp->fgc_patlen == 0 || su_cs_first_of(fgcp->fgc_patdat, "?*[") == su_UZ_MAX){
		dp = NIL;
		sgoc.fgoc_dt_type = -1;
		sgoc.fgoc_name = fgcp->fgc_patdat;
		if((ccp = a_fexpand_glob__one(&sgoc)) == NIL || ccp == R(char*,-1))
			goto jleave;
		goto jerr;
	}

	if((dp = opendir(myp)) == NIL){
		int err;

		switch((err = su_err_by_errno())){
		case su_ERR_NOTDIR:
			ccp = N_("cannot access paths under non-directory");
			goto jerr;
		case su_ERR_NOENT:
			ccp = N_("path component of (sub)pattern non-existent");
			goto jerr;
		case su_ERR_NFILE:
		case su_ERR_MFILE:
			ccp = N_("file descriptor limit reached, cannot open directory");
			goto jerr;
		case su_ERR_ACCES:
			/* Special case: an intermediate directory may not be read, but we
			 * possibly could dive into it? */
			if(fgcp->fgc_patlen > 0 && su_cs_first_of(fgcp->fgc_patdat, "?*[") == su_UZ_MAX){
				sgoc.fgoc_dt_type = -1;
				sgoc.fgoc_name = fgcp->fgc_patdat;
				if((ccp = a_fexpand_glob__one(&sgoc)) == su_NIL || ccp == R(char*,-1))
					goto jleave;
				goto jerr;
			}
			ccp = N_("file permission for file (sub)pattern denied");
			goto jerr;
		default:
			ccp = N_("cannot open path component as directory");
			goto jerr;
		}
	}

	for(myp = fgcp->fgc_patdat; (dep = readdir(dp)) != NIL;){
		switch(fnmatch(myp, dep->d_name, FNM_PATHNAME | FNM_PERIOD)){
		case 0:
			sgoc.fgoc_dt_type =
#ifdef mx_HAVE_DIRENT_TYPE
					dep->d_type
#else
					-1
#endif
					;
			sgoc.fgoc_name = dep->d_name;
			if((ccp = a_fexpand_glob__one(&sgoc)) != NIL){
				if(ccp == R(char*,-1))
					goto jleave;
				goto jerr;
			}
			break;
		case FNM_NOMATCH:
			break;
		default:
			ccp = N_("fnmatch(3) cannot handle file (sub)pattern");
			goto jerr;
		}
	}

	ccp = NIL;
jleave:
	if(dp != NIL)
		closedir(dp);
	NYD2_OU;
	return (ccp == NIL);

jerr:
	if(!(fgcp->fgc_flags & a_SILENT)){
		char const *s2, *s3;

		if(fgcp->fgc_outer->s_len > 0){
			s2 = n_shexp_quote_cp(n_string_cp(fgcp->fgc_outer), FAL0);
			s3 = "/";
		}else
			s2 = s3 = n_empty;

		n_err("%s: %s%s%s\n", V_(ccp), s2, s3,
			n_shexp_quote_cp(fgcp->fgc_patdat, FAL0));
	}
	goto jleave;
}

static char const *
a_fexpand_glob__one(struct a_fexpand_glob_one_ctx *sgocp){
	char const *rv;
	struct n_string *ousp;
	NYD2_IN;

	ousp = sgocp->fgoc_fgcp->fgc_outer;

	/* A match expresses the desire to recurse if there is more pattern */
	if(sgocp->fgoc_new_fgcp->fgc_patlen > 0){
		boole isdir;

		if(ousp->s_len > 0 && (ousp->s_len > 1 || ousp->s_dat[0] != '/'))
			ousp = n_string_push_c(ousp, '/');
		n_string_push_cp(ousp, sgocp->fgoc_name);

		isdir = FAL0;
		if(sgocp->fgoc_dt_type == -1)
#ifdef mx_HAVE_DIRENT_TYPE
Jstat:
#endif
		{
			struct su_pathinfo pi;

			if(!su_pathinfo_stat(&pi, n_string_cp(ousp))){
				rv = N_("I/O error when querying file status");
				goto jleave;
			}else if(su_pathinfo_is_dir(&pi))
				isdir = TRU1;
		}
#ifdef mx_HAVE_DIRENT_TYPE
		else if(sgocp->fgoc_dt_type == DT_DIR)
			isdir = TRU1;
		else if(sgocp->fgoc_dt_type == DT_LNK || sgocp->fgoc_dt_type == DT_UNKNOWN)
			goto Jstat;
#endif

		/* TODO Recurse with current dir FD open, which could E[MN]FILE!
		 * TODO Instead save away a list of such n_string's for later */
		if(isdir && !a_fexpand_glob__it(sgocp->fgoc_new_fgcp, sgocp->fgoc_slpp))
			rv = R(char*,-1);
		else{
			n_string_trunc(ousp, sgocp->fgoc_old_outer_len);
			rv = su_NIL;
		}
	}else{
		struct n_strlist *slp;
		uz i, j;

		i = su_cs_len(sgocp->fgoc_name);
		j = (sgocp->fgoc_old_outer_len > 0) ? sgocp->fgoc_old_outer_len + 1 + i : i;
		slp = n_STRLIST_LOFI_ALLOC(j);
		*sgocp->fgoc_slpp = slp;
		sgocp->fgoc_slpp = &slp->sl_next;
		slp->sl_next = NIL;
		if((j = sgocp->fgoc_old_outer_len) > 0){
			su_mem_copy(&slp->sl_dat[0], ousp->s_dat, j);
			if(slp->sl_dat[j -1] != '/')
				slp->sl_dat[j++] = '/';
		}
		su_mem_copy(&slp->sl_dat[j], sgocp->fgoc_name, i);
		slp->sl_dat[j += i] = '\0';
		slp->sl_len = j;
		rv = su_NIL;
	}

jleave:
	NYD2_OU;
	return rv;
}

static sz
a_fexpand_glob__sort(void const *cvpa, void const *cvpb){
	sz rv;
	struct n_strlist const *slpa, *slpb;
	NYD2_IN;

	slpa = cvpa;
	slpb = cvpb;
	rv = su_cs_cmp_case(slpa->sl_dat, slpb->sl_dat);

	NYD2_OU;
	return rv;
}
#endif /* mx_HAVE_FNMATCH }}} */

char *
mx_fexpand(char const *name, BITENUM(u32,mx_fexp_mode) fexpm){
	struct a_fexpand_ctx fc;
	NYD_IN;

	fc.fc_name = name;
	fc.fc_fexpm = fexpm;
	fc.fc_multi_ok = FAL0;
	a_fexpand(&fc);

	NYD_OU;
	return (fc.fc_res != NIL ? fc.fc_res[0] : NIL);
}

char **
mx_fexpand_multi(char const *name, BITENUM(u32,mx_fexp_mode) fexpm){
	struct a_fexpand_ctx fc;
	NYD_IN;

	fc.fc_name = name;
	fc.fc_fexpm = fexpm;
	fc.fc_multi_ok = TRU1;
	a_fexpand(&fc);

	NYD_OU;
	return fc.fc_res;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_FEXPAND
/* s-itt-mode */
