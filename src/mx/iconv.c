/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of iconv.h.
 *
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE iconv
#define mx_SOURCE
#define mx_SOURCE_ICONV

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/utf.h>

#ifdef mx_HAVE_ICONV
# include "mx/file-streams.h"
#endif

#include "mx/iconv.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#ifdef mx_HAVE_ICONV
s32 n_iconv_err; /* TODO HACK: part of CTX to not get lost */
iconv_t iconvd;
#endif

/* Define a_iconv_dump() (DB devel-only) */
#if DVLDBGOR(1, 0) && 0
# define a_ICONV_DUMP
#endif

/* TODO IANA character set names: "compress" data+structure, etc (mibenum,name) */
struct a_iconv_cs{
	u16 ics_mibenum;
	u8 ics_cnt; /* name+alias count */
	u8 ics_mime_off; /* offset to preferred MIME name (not counting [0]) */
	u8 ics_norm_cnt; /* normalized "name" count */
	u8 ics__pad[3];
	/* [0] = official MIME name, lowercased
	 * [1] = official name
	 * [..] = name+aliases
	 * [.ics_norm_off] = first normalized name */
	char const * const *ics_dat;
};

/* Include the constant su-make-character-set-update.sh output */
#include "mx/gen-iconv-db.h" /* $(MX_SRCDIR) */

/* cset must already be lowercase */
static char *a_iconv_db_lookup(char const *cset);

static char *
a_iconv_db_lookup(char const *cset){
	struct a_iconv_cs const *icsp, *icsp_max;
	enum {a_NONE, a_WS, a_ALPHA, a_DIGIT} sm;
	char const *cp;
	char *buf, *tcp, c;
	uz i, j;
	NYD2_IN;

	i = su_cs_len(cset) << 1;

	tcp = buf = su_LOFI_ALLOC(i +1);
	cp = cset;

	/* Generate normalized variant (of already lowercased cset) */
	for(sm = a_NONE; (c = *(cp++)) != '\0';){
		boole sep;

		sep = FAL0;

		if(su_cs_is_space(c) || su_cs_is_punct(c)){
			if(sm == a_WS)
				continue;
			sm = a_WS;
			c = ' ';
		}else if(su_cs_is_lower(c)){
			sep = (sm == a_DIGIT);
			sm = a_ALPHA;
		}else if(su_cs_is_digit(c)){
			sep = (sm == a_ALPHA);
			sm = a_DIGIT;
		}else
			continue;

		do
			*(tcp++) = (sep ? ' ' : c);
		while(--sep >= FAL0);
	}
	*tcp = '\0';

	/* Search our DB */
	icsp = a_iconv_db;
	icsp_max = &icsp[NELEM(a_iconv_db)];
	do{
		i = icsp->ics_cnt + 1;
		j = icsp->ics_norm_cnt;
		do{
			cp = icsp->ics_dat[i++];
			if(!su_cs_cmp(buf, cp)){
				cset = icsp->ics_dat[0];
				goto jdb_break;
			}
		}while(--j != 0);
	}while(++icsp != icsp_max);

	/*Does not exist in DB, take (lowercased) as is cset = NIL;*/
jdb_break:
	su_LOFI_FREE(buf);

	NYD2_OU;
	return UNCONST(char*,cset);
}

boole
n__iconv_name_is(char const *cset, enum n__iconv_mib mib){
	struct a_iconv_cs const *icsp;
	NYD2_IN;

	icsp = a_iconv_db;
	while(icsp->ics_mibenum != mib)
		++icsp;

	if(!su_cs_cmp(icsp->ics_dat[0], cset))
		cset = NIL;

	NYD2_OU;
	return (cset == NIL);
}

char *
n_iconv_norm_name(char const *cset, boole mime_norm_name){
	char *cp, c, *tcp, tc;
	boole any;
	NYD2_IN;

	/* We need to strip //SUFFIXes off, we want to normalize to all lowercase,
	 * and we perform some slight content testing, too */
	for(any = FAL0, cp = UNCONST(char*,cset); (c = *cp) != '\0'; ++cp){
		if(!su_cs_is_alnum(c) && !su_cs_is_punct(c)){
			n_err(_("Invalid character set name %s\n"), n_shexp_quote_cp(cset, FAL0));
			cset = NIL;
			goto jleave;
		}else if(c == '/')
			break;
		else if(su_cs_is_upper(c))
			any = TRU1;
	}

	if(any || c != '\0'){
		cp = savestrbuf(cset, P2UZ(cp - cset));
		for(tcp = cp; (tc = *tcp) != '\0'; ++tcp)
			*tcp = su_cs_to_lower(tc);
		if(c != '\0' && (n_poption & n_PO_D_V))
			n_err(_("Stripped off character set suffix: %s -> %s\n"),
				n_shexp_quote_cp(cset, FAL0), n_shexp_quote_cp(cp, FAL0));
		cset = cp;
	}

	/* And some names just cannot be used as such */
	if(!su_cs_cmp_case(cset, "unknown-8bit") || !su_cs_cmp_case(cset, "binary")){
		if((cset = ok_vlook(charset_unknown_8bit)) == NIL)
			cset = n_var_oklook(CHARSET_8BIT_OKEY);
		cset = n_iconv_norm_name(cset, mime_norm_name);
	}else if(mime_norm_name)
		cset = a_iconv_db_lookup(cset);

jleave:
	NYD2_OU;
	return UNCONST(char*,cset);
}

#ifdef mx_HAVE_ICONV
iconv_t
n_iconv_open(char const *tocode, char const *fromcode){
# ifndef mx_ICONV_NEEDS_TRANSLIT
	static boole mx_ICONV_NEEDS_TRANSLIT;
# endif
	iconv_t id;
	char const *tocode_orig;
	NYD_IN;

	if((tocode = n_iconv_norm_name(tocode, TRU1)) == NIL || (fromcode = n_iconv_norm_name(fromcode, TRU1)) == NIL){
		su_err_set(su_ERR_INVAL);
		id = R(iconv_t,-1);
		goto jleave;
	}
	tocode_orig = tocode;

	/* For cross-compilations this needs to be evaluated once at runtime */
# ifndef mx_ICONV_NEEDS_TRANSLIT
	if(mx_ICONV_NEEDS_TRANSLIT == FAL0){
		for(;;){
			char inb[8], *inbp, oub[8], *oubp;
			uz inl, oul;

			/* U+1FA78/f0 9f a9 b9/;DROP OF BLOOD */
			su_mem_copy(inbp = inb, "\360\237\251\271", sizeof("\360\237\251\271"));
			inl = sizeof("\360\237\251\271") -1;
			oul = sizeof oub;
			oubp = oub;

			if((id = iconv_open((mx_ICONV_NEEDS_TRANSLIT ? "us-ascii//TRANSLIT" : "us-ascii"), "utf-8")
					) == (iconv_t)-1)
				break;

			if(iconv(id, &inbp, &inl, &oubp, &oul) == (size_t)-1){
				iconv_close(id);
				if(mx_ICONV_NEEDS_TRANSLIT)
					break;
				mx_ICONV_NEEDS_TRANSLIT = TRUM1;
			}else{
				iconv_close(id);
				mx_ICONV_NEEDS_TRANSLIT = TRU1;
				break;
			}
		}
	}
# endif /* ifndef mx_ICONV_NEEDS_TRANSLIT */

	if(mx_ICONV_NEEDS_TRANSLIT == TRU1)
		tocode = savecat(tocode, "//TRANSLIT");

	id = iconv_open(tocode, fromcode);

	/* If the encoding names are equal at this point, they are just not understood by iconv(), and we cannot
	 * sensibly use it in any way.  We do not perform this as an optimization above since iconv() can otherwise be
	 * used to check the validity of the input even with identical encoding names */
	if(id == R(iconv_t,-1) && !su_cs_cmp_case(tocode_orig, fromcode))
		su_err_set(su_ERR_NONE);

jleave:
	NYD_OU;
	return id;
}

void
n_iconv_close(iconv_t cd){
	NYD_IN;

	iconv_close(cd);
	if(cd == iconvd)
		iconvd = R(iconv_t,-1);

	NYD_OU;
}

void
n_iconv_reset(iconv_t cd){
	NYD_IN;

	iconv(cd, NIL, NIL, NIL, NIL);

	NYD_OU;
}

/* (2012-09-24: export and use it exclusively to isolate prototype problems (*inb* is 'char const **' except in POSIX)
 * in a single place.  GNU libiconv even allows for configuration time const/non-const.  In the end it's an ugly guess,
 * but we can't do better since make(1) doesn't support compiler invocations which bail on error, so no -Werror */
/* Citrus project? */
# if defined _ICONV_H_ && defined __ICONV_F_HIDE_INVALID
  /* DragonFly 3.2.1 is special TODO newer DragonFly too, but different */
#  if su_OS_DRAGONFLY
#   define a_X(X) S(char** __restrict__,S(void*,UNCONST(char*,X)))
#  else
#   define a_X(X) S(char const**,S(void*,UNCONST(char*,X)))
#  endif
# elif su_OS_SUNOS || su_OS_SOLARIS
#  define a_X(X) S(char const** __restrict__,S(void*,UNCONST(char*,X)))
# endif
# ifndef a_X
#  define a_X(X) S(char**,S(void*,UNCONST(char*,X)))
# endif
int
n_iconv_buf(iconv_t cd, enum n_iconv_flags icf, char const **inb, uz *inbleft, char **outb, uz *outbleft){
	int err;
	NYD2_IN;

	if((icf & n_ICONV_UNIREPL) && !(n_psonce & n_PSO_UNICODE)) /* TODO depends on iconv_open tocode though! */
		icf &= ~n_ICONV_UNIREPL;

	for(;;){
		uz i;

		if((i = iconv(cd, a_X(inb), inbleft, outb, outbleft)) == 0)
			break;
		if(UCMP(z, i, !=, -1)){
			if(!(icf & n_ICONV_IGN_NOREVERSE)){
				err = su_ERR_NOENT;
				goto jleave;
			}
			break;
		}

		if((err = su_err_by_errno()) == su_ERR_2BIG)
			goto jleave;

		if(!(icf & n_ICONV_IGN_ILSEQ) || err != su_ERR_ILSEQ)
			goto jleave;

		if(*inbleft > 0){
			++(*inb);
			--(*inbleft);
			if(icf & n_ICONV_UNIREPL){
				if(*outbleft >= sizeof(su_utf8_replacer) -1){
					su_mem_copy(*outb, su_utf8_replacer, sizeof(su_utf8_replacer) -1);
					*outb += sizeof(su_utf8_replacer) -1;
					*outbleft -= sizeof(su_utf8_replacer) -1;
					continue;
				}
			}else if(*outbleft > 0){
				*(*outb)++ = '?';
				--*outbleft;
				continue;
			}
			err = su_ERR_2BIG;
			goto jleave;
		}else if(*outbleft > 0){
			**outb = '\0';
			goto jleave;
		}
	}

	err = 0;
jleave:
	n_iconv_err = err;
	NYD2_OU;
	return err;
}
# undef a_X

int
n_iconv_str(iconv_t cd, enum n_iconv_flags icf, struct str *out, struct str const *in, struct str *in_rest_or_nil){
	struct n_string s_b, *s;
	char const *ib;
	int err;
	uz il;
	NYD2_IN;

	il = in->l;
	if(!n_string_get_can_book(il) || !n_string_get_can_book(out->l)){
		err = su_ERR_INVAL;
		goto j_leave;
	}
	ib = in->s;

	s = n_string_creat(&s_b);
	s = n_string_take_ownership(s, out->s, out->l, 0);

	for(;;){
		char *ob_base, *ob;
		uz ol, nol;

		if((nol = ol = s->s_len) < il)
			nol = il;
		ASSERT(sizeof(s->s_len) == sizeof(u32));
		if(nol < 128)
			nol += 32;
		else{
			u64 xnol;

			xnol = S(u64,nol << 1) - (nol >> 4);
			if(!n_string_can_book(s, xnol)){
				xnol = ol + 64;
				if(!n_string_can_book(s, xnol)){
					err = su_ERR_INVAL;
					goto jleave;
				}
			}
			nol = S(uz,xnol);
		}
		s = n_string_resize(s, nol);

		ob = ob_base = &s->s_dat[ol];
		nol -= ol;
		err = n_iconv_buf(cd, icf, &ib, &il, &ob, &nol);

		s = n_string_trunc(s, ol + P2UZ(ob - ob_base));
		if(err == 0 || err != su_ERR_2BIG)
			break;
	}

	if(in_rest_or_nil != NIL){
		in_rest_or_nil->s = UNCONST(char*,ib);
		in_rest_or_nil->l = il;
	}

jleave:
	out->s = n_string_cp(s);
	out->l = s->s_len;
	s = n_string_drop_ownership(s);
	/* n_string_gut(s)*/

j_leave:
	NYD2_OU;
	return err;
}

char *
n_iconv_onetime_cp(enum n_iconv_flags icf, char const *tocode, char const *fromcode, char const *input){
	struct str out, in;
	iconv_t icd;
	char *rv;
	NYD2_IN;

	rv = NIL;
	if(tocode == NIL)
		tocode = ok_vlook(ttycharset);
	if(fromcode == NIL)
		fromcode = su_utf8_name_lower;

	if((icd = iconv_open(tocode, fromcode)) == R(iconv_t,-1))
		goto jleave;

	in.l = su_cs_len(in.s = UNCONST(char*,input)); /* logical */
	out.s = NIL, out.l = 0;
	if(!n_iconv_str(icd, icf, &out, &in, NIL))
		rv = savestrbuf(out.s, out.l);
	if(out.s != NIL)
		su_FREE(out.s);

	iconv_close(icd);

jleave:
	NYD2_OU;
	return rv;
}

s32
n_iconv_onetime_fp(enum n_iconv_flags icf, FILE **ofpp_or_nil, FILE *ifp, char const *tocode, char const *fromcode){
	struct str in, ou;
	uz cnt, len;
	iconv_t icd;
	s32 err;
	FILE *ofp;
	NYD2_IN;

	if(tocode == NIL)
		tocode = ok_vlook(ttycharset);
	if(fromcode == NIL)
		fromcode = su_utf8_name_lower;

	if((icd = iconv_open(tocode, fromcode)) == R(iconv_t,-1)){
		err = su_err();
		if(err == su_ERR_NONE)
			err = su_ERR_INVAL;
		goto jleave;
	}

	if((ofp = *ofpp_or_nil) == NIL &&
			(ofp = mx_fs_tmp_open(NIL, "iconv", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)) == NIL){
		err = su_err();
		goto jerr1;
	}

	mx_fs_linepool_aquire(&in.s, &len);
	STRUCT_ZERO(struct str, &ou);

	for(cnt = S(uz,fsize(ifp));;){ /* XXX s64 */
		if(fgetline(&in.s, &len, &cnt, &in.l, ifp, FAL0) == NIL){
			if(ferror(ifp)){
				err = su_err();
				if(err == su_ERR_NONE)
					err = su_ERR_IO;
				goto jerr;
			}
			/* cnt==0||feof */
			break;
		}

		if(n_iconv_str(icd, icf, &ou, &in, NIL) != 0){
			err = n_iconv_err;
			goto jerr;
		}

		if(fwrite(ou.s, sizeof(*ou.s), ou.l, ofp) != ou.l){
			err = su_err_by_errno();
			if(err == su_ERR_NONE)
				err = su_ERR_IO;
			goto jerr;
		}
	}
	fflush_rewind(ofp);

	err = su_ERR_NONE;
jerr:
	if(ou.s != NIL)
		su_FREE(ou.s);
	mx_fs_linepool_release(in.s, len);

	if(*ofpp_or_nil == NIL){
		if(err == su_ERR_NONE)
			*ofpp_or_nil = ofp;
		else
			mx_fs_close(ofp);
	}

jerr1:
	iconv_close(icd);

jleave:
	NYD2_OU;
	return err;
}
#endif /* mx_HAVE_ICONV */

#ifdef a_ICONV_DUMP
void
mx_iconv_dump(void){
	struct a_iconv_cs const *icsp, *icsp_max;

	icsp = a_iconv_db;
	icsp_max = &icsp[NELEM(a_iconv_db)];

	fprintf(stderr, "DB has %zu entries\n", NELEM(a_iconv_db));
	do{
		u32 i;

		fprintf(stderr, "- MIB=%u CNT=%u NORM_CNT=%u\n",
			icsp->ics_mibenum, icsp->ics_cnt, icsp->ics_norm_cnt);
		fprintf(stderr, "    MIME<%s>", icsp->ics_dat[0]);
		if(icsp->ics_cnt > 0)
			fprintf(stderr, " NAME<%s>%s",
				icsp->ics_dat[1], (icsp->ics_mime_off == 0 ? " (MIME)" : su_empty));
		fprintf(stderr, "\n    ");
		for(i = 1 + (icsp->ics_cnt != 0); i < 1 + icsp->ics_cnt + icsp->ics_norm_cnt; ++i){
			if(i == icsp->ics_cnt + 1)
				fprintf(stderr, "%sNORM: ", ((i == 1 + (icsp->ics_cnt != 0)) ? su_empty : "\n    "));
			fprintf(stderr, " %u<%s>", i, icsp->ics_dat[i]);
			if(i == 1 + icsp->ics_mime_off)
				fprintf(stderr, " (MIME)");
			putc(' ', stderr);
		}
		putc('\n', stderr);
	}while(++icsp != icsp_max);
}
# undef a_ICONV_DUMP
#endif /* a_ICONV_DUMP */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_ICONV
/* s-itt-mode */
