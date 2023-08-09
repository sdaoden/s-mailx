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

/* TODO character set names should be ctext/su */
struct a_iconv_set{
	uz is_cnt;
	char const * const *is_dat;
};

/* In reversed MIME preference order, lowercased */
static char const
#if 0
	* const a_iconv_l1_names[] = {"csISOLatin1", "iso-ir-100", "l1",
			"IBM819", "CP819",  "ISO_8859-1",
			"ISO_8859-1:1987", "latin1", "ISO-8859-1"},
	* const a_iconv_u8_names[] = {"utf8", "utf-8"},
	* const a_iconv_us_names[] = {"csASCII", "cp367", "IBM367", "us",
			"ISO646-US", "ISO_646.irv:1991", "ANSI_X3.4-1986", "iso-ir-6",
			"ANSI_X3.4-1968", "ASCII", "US-ASCII"};
#else
#if 0
	* const a_iconv_l1_names[] = {"csisolatin1", "iso-ir-100", "l1",
			"ibm819", "cp819",  "iso_8859-1",
			"iso_8859-1:1987", "latin1", "iso-8859-1"},
#endif
	* const a_iconv_u8_names[] = {"utf8", "utf-8"},
	* const a_iconv_us_names[] = {"csascii", "cp367", "ibm367", "us",
			"iso646-us", "iso_646.irv:1991", "ansi_x3.4-1986", "iso-ir-6",
			"ansi_x3.4-1968", "ascii", "us-ascii"};
#endif

static struct a_iconv_set const a_iconv_sets[] = {
	FII(n__ICONV_SET_U8) {NELEM(a_iconv_u8_names), a_iconv_u8_names},
	FII(n__ICONV_SET_US) {NELEM(a_iconv_us_names), a_iconv_us_names}
};

boole
n__iconv_name_is(char const *cset, u32 set){
	boole rv;
	char const * const *npp;
	struct a_iconv_set const *isp;
	NYD2_IN;

	isp = &a_iconv_sets[set];
	npp = &isp->is_dat[set = S(u32,isp->is_cnt)];

	do if((rv = !su_cs_cmp_case(cset, *--npp)))
		break;
	while(--set != 0);

	NYD2_OU;
	return rv;
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
	}else if(mime_norm_name){
		u32 i;

		for(i = 0; i < n__ICONV_SET__MAX; ++i)
			if(n__iconv_name_is(cset, i)){
				cset = a_iconv_sets[i].is_dat[a_iconv_sets[i].is_cnt - 1];
				break;
			}
	}

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

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_ICONV
/* s-itt-mode */
