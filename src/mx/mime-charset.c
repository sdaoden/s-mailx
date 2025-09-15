/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mime-charset.h.
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
#define su_FILE mime_charset
#define mx_SOURCE
#define mx_SOURCE_MIME_CHARSET

#include <mx/config.h>

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/iconv.h"
#include "mx/okeys.h"

#ifdef mx_HAVE_ICONV
# include "mx/file-streams.h"
# include "mx/mime-type.h"
#endif

#include "mx/mime-charset.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

static char *a_mime_cs_iter_base, *a_mime_cs_iter;
#define a_MIME_CS_ITER_GET() ((a_mime_cs_iter != NIL) ? a_mime_cs_iter : mx_var_oklook(CHARSET_8BIT_OKEY))
#define a_MIME_CS_ITER_STEP() a_mime_cs_iter = su_cs_sep_c(&a_mime_cs_iter_base, ',', TRU1)

void
mx_mime_charset_iter_clear(void){
	a_mime_cs_iter = NIL;
}

boole
mx_mime_charset_iter_reset(char const *cset_tryfirst_or_nil, char const *cset_isttycs_or_nil){
	char const *sarr[3];
	uz sarrl[3], len;
	char *cp;
	boole id;
	NYD_IN;

	id = ok_blook(iconv_disable);
	/*sarr[0] = sarr[1] = NIL;*/
	sarrl[0] = sarrl[1] = 0;

	sarr[2] = (id ? (cset_isttycs_or_nil != NIL ? cset_isttycs_or_nil : ok_vlook(ttycharset))
			: mx_var_oklook(CHARSET_8BIT_OKEY));
	sarrl[2] = su_cs_len(sarr[2]);

	if(cset_tryfirst_or_nil != NIL){
		ASSERT(!su_cs_cmp(cset_tryfirst_or_nil, n_iconv_norm_name(cset_tryfirst_or_nil, TRU1)));
		if(su_cs_cmp(cset_tryfirst_or_nil, sarr[2]))
			sarrl[0] = su_cs_len(sarr[0] = cset_tryfirst_or_nil);
	}else{
		UNINIT(sarr[0], NIL);
	}

#ifdef mx_HAVE_ICONV
	if(!id){
		if((sarr[1] = ok_vlook(sendcharsets)) != NIL)
			cp = UNCONST(char*,sarr[1]);
		else if(ok_blook(sendcharsets_else_ttycharset)){
			cp = UNCONST(char*,cset_isttycs_or_nil != NIL ? cset_isttycs_or_nil : ok_vlook(ttycharset));
			ASSERT(!su_cs_cmp(cp, n_iconv_norm_name(cp, TRU1)));
		}else
			goto jskip;
		if(su_cs_cmp(cp, sarr[2]) && (sarrl[0] == 0 || su_cs_cmp(cp, sarr[0])))
			sarrl[1] = su_cs_len(sarr[1] = cp);
jskip:;
	}
#endif

	len = sarrl[0] + sarrl[1] + sarrl[2]; /* XXX overflow */
	a_mime_cs_iter_base = cp = su_AUTO_ALLOC(len + 1 + 1 +1);

	if((len = sarrl[0]) != 0){
		su_mem_copy(cp, sarr[0], len);
		cp[len] = ',';
		cp += ++len;
	}
#ifdef mx_HAVE_ICONV
	if((len = sarrl[1]) != 0){
		su_mem_copy(cp, sarr[1], len);
		cp[len] = ',';
		cp += ++len;
	}
#endif
	len = sarrl[2];
	su_mem_copy(cp, sarr[2], len);
	cp[len] = '\0';

	a_MIME_CS_ITER_STEP();

	NYD_OU;
	return (a_mime_cs_iter != NIL);
}

boole
mx_mime_charset_iter_next(void){
	boole rv;
	NYD2_IN;

	a_MIME_CS_ITER_STEP();
	rv = (a_mime_cs_iter != NIL);

	NYD2_OU;
	return rv;
}

boole
mx_mime_charset_iter_is_valid(void){
	boole rv;
	NYD2_IN;

	rv = (a_mime_cs_iter != NIL);

	NYD2_OU;
	return rv;
}

char const *
mx_mime_charset_iter(void){
	char const *rv;
	NYD2_IN;

	rv = a_mime_cs_iter;

	NYD2_OU;
	return rv;
}

char const *
mx_mime_charset_iter_or_fallback(void){
	char const *rv;
	NYD2_IN;

	rv = a_MIME_CS_ITER_GET();

	NYD2_OU;
	return rv;
}

#ifdef mx_HAVE_ICONV
s32
mx_mime_charset_iter_onetime_fp(FILE **ofpp_or_nil, FILE *ifp, struct mx_mime_type_classify_fp_ctx *mtcfcp,
		char const *cset_to_try_first_or_nil, char const **emsg_or_nil){
	char const *emsg, *ics;
	s32 err;
	FILE *nfp;
	NYD_IN;
	ASSERT(mtcfcp->mtcfc_do_iconv);
	ASSERT(mtcfcp->mtcfc_mpccp_or_nil != NIL);

	/* Create temporary ourselves if needed to avoid that iconv_onetime_fp() guts its own creation upon failure */
	if((nfp = *ofpp_or_nil) == NIL){
		nfp = mx_fs_tmp_open(NIL, "mmcnv", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL);
		if(nfp == NIL){
			err = su_err();
			emsg = N_("MIME character set conversion file creation failed");
			goto jleave;
		}
	}

	emsg = NIL;
	ASSERT(mtcfcp->mtcfc_input_charset != NIL);
	ics = mtcfcp->mtcfc_input_charset;

	for(mx_mime_charset_iter_reset(cset_to_try_first_or_nil, NIL);;){
		err = n_iconv_onetime_fp(n_ICONV_NONE, &nfp, ifp, mx_mime_charset_iter(), ics);
		if(err == su_ERR_NONE){
			if(*ofpp_or_nil == NIL)
				*ofpp_or_nil = nfp;
			mtcfcp->mtcfc_charset = mx_mime_charset_iter();
			mtcfcp->mtcfc_charset_is_ascii = n_iconv_name_is_ascii(mtcfcp->mtcfc_charset);
			break;
		}

		rewind(ifp);
		fflush_rewind(nfp); /* not in error case */

		if(UNLIKELY(!mx_mime_charset_iter_next())){
			if(*ofpp_or_nil == NIL)
				mx_fs_close(nfp);

			if(!ok_blook(mime_force_sendout)){
				/*XXX n_err(_("Cannot convert from %s to *sendcharsets*\n"), ics);*/
				err = su_ERR_INVAL; /* XXX NOTSUP */
			}else{
				err = su_ERR_NONE;
				mtcfcp->mtcfc_do_iconv = FAL0;
				mtcfcp->mtcfc_conversion = CONV_TOB64;
				mtcfcp->mtcfc_content_type = "application/octet-stream";
				mtcfcp->mtcfc_charset = mtcfcp->mtcfc_input_charset = NIL;
				mtcfcp->mtcfc_charset_is_ascii = FAL0;
			}
			break;
		}

		ftrunc_x_trunc(nfp, 0, err);
		if(err != 0){
			if((err = su_err_by_errno()) == su_ERR_NONE)
				err = su_ERR_INVAL;
			if(*ofpp_or_nil == NIL)
				mx_fs_close(nfp);
			emsg = N_("I/O error during MIME character set conversion");
			break;
		}
	}

jleave:
	if(err != su_ERR_NONE && emsg_or_nil != NIL)
		*emsg_or_nil = emsg;

	NYD_OU;
	return err;
}
#endif /* mx_HAVE_ICONV */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MIME_CHARSET
/* s-itt-mode */
