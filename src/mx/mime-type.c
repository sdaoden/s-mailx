/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mime-type.h.
 *@ "Keep in sync with" ../../mime.types.
 *@ TODO With an on_loop_tick_event, trigger cache update once per loop max.
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
#define su_FILE mime_type
#define mx_SOURCE
#define mx_SOURCE_MIME_TYPE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>
#include <su/utf.h>

#include "mx/fexpand.h"
#include "mx/file-streams.h"
#include "mx/iconv.h"
#include "mx/mime.h"
#include "mx/mime-enc.h"
#include "mx/mime-probe.h"

#ifdef mx_HAVE_MAILCAP
# include "mx/mailcap.h"
#endif

#include "mx/mime-type.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* mime.types template for builtin MIME types refers to this enumeration! */
enum a_mt_flags{
	a_MT_APPLICATION,
	a_MT_AUDIO,
	a_MT_IMAGE,
	a_MT_MESSAGE,
	a_MT_MULTIPART,
	a_MT_TEXT,
	a_MT_VIDEO,
	a_MT_OTHER,
	a_MT__TMIN = 0u,
	a_MT__TMAX = a_MT_OTHER,
	a_MT__TMASK = 0x07u,

	a_MT_CMD = 1u<<8, /* Via `mimetype' (not struct a_mt_bltin) */
	a_MT_USR = 1u<<9, /* VAL_MIME_TYPES_USR */
	a_MT_SYS = 1u<<10, /* VAL_MIME_TYPES_SYS */
	a_MT_FSPEC = 1u<<11, /* Via f= *mimetypes-load-control* spec. */

	a_MT_TM_PLAIN = 1u<<16, /* Without pipe handler display as text */
	a_MT_TM_SOUP_h = 2u<<16, /* Ditto, but HTML tagsoup parser iff */
	a_MT_TM_SOUP_H = 3u<<16, /* HTML tagsoup, else NOT plain text */
	a_MT_TM_QUIET = 4u<<16, /* No "no mime handler available" message */
	a_MT__TM_MARKMASK = 7u<<16,

	/* Only used when evaluating MIME type handlers, aka marked by the ?only-handler type
	 * marker: these are stored ONLY in a_mt_hdl_list */
	a_MT_TM_ONLY_HANDLER = 1u<<20,
	/* Only used when classifying: forcefully send as text/plain */
	a_MT_TM_SEND_TEXT = 1u<<21,
	a_MT_TM__FLAG_MASK = a_MT_TM_ONLY_HANDLER | a_MT_TM_SEND_TEXT
};

enum a_mt_counter_evidence{
	a_MT_CE_NONE,
	a_MT_CE_SET = 1u<<0, /* *mime-counter-evidence* was set */
	a_MT_CE_BIN_OVWR = 1u<<1, /* appli../o.-s.: check, ovw if possible */
	a_MT_CE_ALL_OVWR = 1u<<2, /* all: check, ovw if possible */
	a_MT_CE_BIN_PARSE = 1u<<3 /* appli../o.-s.: classify contents last */
};

struct a_mt_bltin{
	BITENUM(u32,a_mt_flags) mtb_flags;
	u32 mtb_mtlen;
	char const *mtb_line;
};

struct a_mt_node{
	struct a_mt_node *mtn_next;
	BITENUM(u32,a_mt_flags) mtn_flags;
	u32 mtn_len; /* Length of MIME type string, rest thereafter */
	char const *mtn_line;
};

struct a_mt_lookup{
	struct a_mt_node const *mtl_node;
	char *mtl_result; /* If requested, AUTO_ALLOC()ed MIME type */
};

static struct a_mt_bltin const a_mt_bltin[] = {
#include "gen-mime-types.h" /* */
};

static char const a_mt_names[][16] = {"application/", "audio/", "image/", "message/", "multipart/", "text/", "video/"};
CTAV(a_MT_APPLICATION == 0 && a_MT_AUDIO == 1 && a_MT_IMAGE == 2 && a_MT_MESSAGE == 3 && a_MT_MULTIPART == 4 &&
	a_MT_TEXT == 5 && a_MT_VIDEO == 6);

/* */
static boole a_mt_is_init;
static struct a_mt_node *a_mt_list;
static struct a_mt_node *a_mt_hdl_list;

/* Initialize MIME type list in order */
static void a_mt_init(void);
static boole a_mt__load_file(BITENUM(u32,a_mt_flags) orflags, char const *file, char **line, uz *linesize);

DVL( static void a_mt__on_gut(BITENUM(u32,su_state_gut_flags) flags); )

/* Create (prepend) a new MIME type; cmdcalled results in a bit more verbosity for `mimetype'; line is terminated, len
 * is just an optimization */
static struct a_mt_node *a_mt_create(boole cmdcalled, BITENUM(u32,a_mt_flags) orflags, char const *line, uz len);

/* Try to find MIME type by X (after zeroing mtlp), return NIL if not found.
 * is_hdl: whether for handler aka user context (aka whether ?* handler-only *can* match (then: first)) */
static struct a_mt_lookup *a_mt_by_filename(struct a_mt_lookup *mtlp, char const *name, boole is_hdl);
static struct a_mt_lookup *a_mt_by_name(struct a_mt_lookup *mtlp, char const *name, boole is_hdl);

/* We need an in-depth inspection of an application/octet-stream part */
static enum mx_mime_type a_mt_classify_o_s_part(BITENUM(u32,a_mt_counter_envidence) mce, struct mimepart *mpp,
		boole deep_inspect);

/* Check whether a *pipe-XY* handler is applicable, and adjust flags according to the defined trigger characters; upon
 * entry MIME_TYPE_HDL_NIL is set, and that is not changed if mthp does not apply */
static BITENUM(u32,mx_mime_type_handler_flags) a_mt_pipe_check(struct mx_mime_type_handler *mthp, enum sendaction action);

static void
a_mt_init(void){ /* {{{ */
	uz linesize;
	char c, *line;
	char const *srcs_arr[10], *ccp, **srcs;
	u32 i, j;
	struct a_mt_node *tail, *htail;
	NYD_IN;

	ASSERT(!a_mt_is_init);
	/*if(a_mt_is_init)
	 *  goto jleave;*/

	DVL( su_state_on_gut_install(&a_mt__on_gut, FAL0, su_STATE_ERR_NOPASS); )

	/* Always load our built-ins */
	for(tail = htail = NIL, i = 0; i < NELEM(a_mt_bltin); ++i){
		struct a_mt_bltin const *mtbp;
		struct a_mt_node *mtnp;

		mtnp = su_ALLOC(sizeof *mtnp);
		mtbp = &a_mt_bltin[i];

		if(!(mtbp->mtb_flags & a_MT_TM_ONLY_HANDLER)){
			if(tail != NIL)
				tail->mtn_next = mtnp;
			else
				a_mt_list = mtnp;
			tail = mtnp;
		}else{
			if(htail != NIL)
				htail->mtn_next = mtnp;
			else
				a_mt_hdl_list = mtnp;
			htail = mtnp;
		}
		mtnp->mtn_next = NIL;
		mtnp->mtn_flags = mtbp->mtb_flags;
		mtnp->mtn_len = mtbp->mtb_mtlen;
		mtnp->mtn_line = mtbp->mtb_line;
	}

	/* Decide which files sources have to be loaded */
	if((ccp = ok_vlook(mimetypes_load_control)) == NIL)
		ccp = "US";
	else if(*ccp == '\0')
		goto jleave;

	srcs = &srcs_arr[2];
	srcs[-1] = srcs[-2] = NIL;

	if(su_cs_find_c(ccp, '=') != NIL){
		line = savestr(ccp);

		while((ccp = su_cs_sep_c(&line, ',', TRU1)) != NIL){
			switch((c = *ccp)){
			case 'S': case 's':
				srcs_arr[1] = VAL_MIME_TYPES_SYS;
				if(0){
					/* FALLTHRU */
			case 'U': case 'u':
					srcs_arr[0] = VAL_MIME_TYPES_USR;
				}
				if(ccp[1] != '\0')
					goto jecontent;
				break;
			case 'F': case 'f':
				if(*++ccp == '=' && *++ccp != '\0'){
					if(P2UZ(srcs - srcs_arr) < NELEM(srcs_arr))
						*srcs++ = ccp;
					else
						n_err(_("*mimetypes-load-control*: too many sources, skipping %s\n"),
							n_shexp_quote_cp(ccp, FAL0));
					continue;
				}
				/* FALLTHRU */
			default:
				goto jecontent;
			}
		}
	}else for(i = 0; (c = ccp[i]) != '\0'; ++i)
		switch(c){
		case 'S': case 's': srcs_arr[1] = VAL_MIME_TYPES_SYS; break;
		case 'U': case 'u': srcs_arr[0] = VAL_MIME_TYPES_USR; break;
		default:
jecontent:
			n_err(_("*mimetypes-load-control*: unsupported value: %s\n"), ccp);
			goto jleave;
		}

	/* Load all file-based sources in the desired order */
	mx_fs_linepool_aquire(&line, &linesize);
	for(j = 0, i = S(u32,P2UZ(srcs - srcs_arr)), srcs = srcs_arr;
			i > 0; ++j, ++srcs, --i)
		if(*srcs == NIL)
			continue;
		else if(!a_mt__load_file((j == 0 ? a_MT_USR : (j == 1 ? a_MT_SYS : a_MT_FSPEC)), *srcs, &line, &linesize)){
			s32 eno;

			if((eno = su_err()) != su_ERR_NOENT || (n_poption & n_PO_D_V) || j > 1)
				n_err(_("*mimetypes-load-control*: cannot open or load %s: %s\n"),
					n_shexp_quote_cp(*srcs, FAL0), su_err_doc(eno));
		}
	mx_fs_linepool_release(line, linesize);

jleave:
	a_mt_is_init = TRU1;
	NYD_OU;
} /* }}} */

static boole
a_mt__load_file(u32 orflags, char const *file, char **line, uz *linesize){ /* {{{ */
	uz len;
	struct a_mt_node *head, *tail, *hhead, *htail, *mtnp;
	FILE *fp;
	char const *cp;
	NYD_IN;

	if((cp = mx_fexpand(file, mx_FEXP_DEF_LOCAL_FILE_VAR)) == NIL || (fp = mx_fs_open(cp, mx_FS_O_RDONLY)) == NIL){
		cp = NIL;
		goto jleave;
	}

	head = tail = hhead = htail = NIL;

	while(fgetline(line, linesize, NIL, &len, fp, FAL0) != NIL)
		if((mtnp = a_mt_create(FAL0, orflags, *line, len)) != NIL){
			if(!(mtnp->mtn_flags & a_MT_TM_ONLY_HANDLER)){
				if(head == NIL)
					head = tail = mtnp;
				else
					tail->mtn_next = mtnp;
				tail = mtnp;
			}else{
				if(hhead == NIL)
					hhead = htail = mtnp;
				else
					htail->mtn_next = mtnp;
				htail = mtnp;
			}
		}

	if(ferror(fp))
		cp = NIL;
	else{
		if(head != NIL){
			tail->mtn_next = a_mt_list;
			a_mt_list = head;
		}
		if(hhead != NIL){
			htail->mtn_next = a_mt_hdl_list;
			a_mt_hdl_list = hhead;
		}
	}

	mx_fs_close(fp);

jleave:
	NYD_OU;
	return (cp != NIL);
} /* }}} */

#if DVLOR(1, 0)
static void
a_mt__on_gut(BITENUM(u32,su_state_gut_flags) flags){
	struct a_mt_node **mtnpp, *mtnp;
	NYD2_IN;

	if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
		for(mtnpp = &a_mt_hdl_list;; mtnpp = &a_mt_list){
			while((mtnp = *mtnpp) != NIL){
				*mtnpp = mtnp->mtn_next;
				su_FREE(mtnp);
			}
			if(mtnpp != &a_mt_hdl_list)
				break;
		}
	}

	a_mt_hdl_list = a_mt_list = NIL;
	a_mt_is_init = FAL0;

	NYD2_OU;
}
#endif /* DVLOR(1, 0) */

static struct a_mt_node *
a_mt_create(boole cmdcalled, BITENUM(u32,a_mt_flags) orflags, char const *line, uz len){ /* {{{ */
	struct str work;
	uz len_orig, tlen, i;
	char const *line_orig, *typ, *subtyp;
	struct a_mt_node *mtnp;
	NYD_IN;

	mtnp = NIL;

	/* Drop anything after a comment first TODO v15: only when read from file */
	if((typ = su_mem_find(line, '#', len)) != NIL)
		len = P2UZ(typ - line);

	line_orig = line;
	len_orig = len;

	/* Then trim any whitespace from line (including NL/CR) */
	work.s = UNCONST(char*,line);
	work.l = len;
	line = n_str_trim(&work, n_STR_TRIM_BOTH)->s;
	len = work.l;

	/* (But wait - is there a type marker?) */
	if(!(orflags & (a_MT_USR | a_MT_SYS)) && (*line == '?' || *line == '@')){
		if(*line == '@') /* v15compat (plus trailing below) */
			n_OBSOLETE2(_("mimetype: type markers (and much more) use ? not @"), line_orig);
		if(len < 2)
			goto jeinval;

		if(line[1] == ' '){
			orflags |= a_MT_TM_PLAIN;
			i = 2;
			line += i;
			len -= i;
		}else{
			for(typ = ++line;; ++typ){
				if(su_cs_is_space(*typ))
					break;
				else if(*typ == '\0')
					goto jeinval;
			}
			i = P2UZ(typ - line);
			len -= i;
			work.s = savestrbuf(line, i);
			line = typ;

			while((typ = su_cs_sep_c(&work.s, ',', TRU1)) != NIL){
				char c;

				c = typ[0];
				if(typ[1] != '\0'){
					if(!su_cs_cmp_case(typ, "only-handler"))
						goto jf_oh;
					else if(!su_cs_cmp_case(typ, "send-text"))
						goto jf_st;
					else
						goto jeinval;
				}else if(c == 'o'){
jf_oh:
					if(orflags & a_MT_TM__FLAG_MASK)
						goto jeinval;
					orflags |= a_MT_TM_ONLY_HANDLER;
				}else if(c == 's'){
jf_st:
					if(orflags & a_MT_TM__FLAG_MASK)
						goto jeinval;
					orflags |= a_MT_TM_SEND_TEXT;
				}else if(orflags & a_MT__TM_MARKMASK)
					goto jeinval;
				else switch(c){
				default: goto jeinval;
				case 't': orflags |= a_MT_TM_PLAIN; break;
				case 'h': orflags |= a_MT_TM_SOUP_h; break;
				case 'H': orflags |= a_MT_TM_SOUP_H; break;
				case 'q': orflags |= a_MT_TM_QUIET; break;
				}
			}

			if(!(orflags & (a_MT__TM_MARKMASK | a_MT_TM__FLAG_MASK)))
				orflags |= a_MT_TM_PLAIN;
		}

		work.s = UNCONST(char*,line);
		work.l = len;
		line = n_str_trim(&work, n_STR_TRIM_FRONT)->s;
		len = work.l;
	}

	typ = line;
	while(len > 0 && !su_cs_is_space(*line))
		++line, --len;

	/* Ignore empty lines and even incomplete specifications (only MIME type): quite common in mime.types(5) files */
	if(len == 0 || (tlen = P2UZ(line - typ)) == 0){
		if(cmdcalled || (orflags & a_MT_FSPEC))
			n_err(_("Empty MIME type or no extensions: %.*s\n"), S(int,len_orig), line_orig);
		goto jleave;
	}

	if((subtyp = su_mem_find(typ, '/', tlen)) == NIL || subtyp[1] == '\0' || su_cs_is_space(subtyp[1])) {
jeinval:
		if(cmdcalled || (orflags & a_MT_FSPEC) || (n_poption & n_PO_D_V))
			n_err(_("%s MIME type: %.*s\n"),
				(cmdcalled ? _("Invalid") : _("mime.types(5): invalid")), S(int,len_orig), line_orig);
		goto jleave;
	}
	++subtyp;

	/* Map to mime_type */
	tlen = P2UZ(subtyp - typ);
	for(i = a_MT__TMIN;;){
		if(!su_cs_cmp_case_n(a_mt_names[i], typ, tlen)){
			orflags |= i;
			tlen = P2UZ(line - subtyp);
			typ = subtyp;
			break;
		}
		if(++i == a_MT__TMAX){
			orflags |= a_MT_OTHER;
			tlen = P2UZ(line - typ);
			break;
		}
	}

	/* Strip leading whitespace from the list of extensions; trailing WS has already been trimmed away above.
	 * Be silent on slots which define a mimetype without any value */
	work.s = UNCONST(char*,line);
	work.l = len;
	line = n_str_trim(&work, n_STR_TRIM_FRONT)->s;
	len = work.l;
	if(len == 0)
		goto jleave;

	/*  */
	mtnp = su_ALLOC(sizeof(*mtnp) + tlen + len +1);
	mtnp->mtn_next = NIL;
	mtnp->mtn_flags = orflags;
	mtnp->mtn_len = S(u32,tlen);
	/* C99 */{
		char *l;

		l = S(char*,&mtnp[1]);
		mtnp->mtn_line = l;
		su_mem_copy(l, typ, tlen);

		su_mem_copy(&l[tlen], line, len);
		l[tlen + len] = '\0';
	}

jleave:
	NYD_OU;
	return mtnp;
} /* }}} */

static struct a_mt_lookup *
a_mt_by_filename(struct a_mt_lookup *mtlp, char const *name, boole is_hdl){ /* {{{ */
	boole ever;
	uz nlen, i, j;
	char const *ext, *cp;
	NYD2_IN;

	STRUCT_ZERO(struct a_mt_lookup, mtlp);

	/* "basename()" it */
	nlen = su_cs_len(name); /* TODO should be URI! */
	for(i = nlen; i > 0 && name[i - 1] != su_PATH_SEP_C;)
		--i;
	name += i;
	nlen -= i;

	if(nlen == 0)
		goto jnil_leave;

	/* A leading dot does not belong to an extension */
	if(*name == '.'){
		if(--nlen == 0)
			goto jnil_leave;
		++name;
	}

	/* We seem to have something to do */
	if(!a_mt_is_init)
		a_mt_init();

	for(ever = FAL0;; ever = TRU1){
		struct a_mt_node *mtnp_base, *mtnp;

		if((cp = su_mem_find(name, '.', nlen)) != NIL){
			if((nlen -= P2UZ(++cp - name)) == 0)
				break;
			name = cp;
		}else if(ever)
			break;
		ASSERT(nlen > 0);

		/* ..all the MIME types */
		mtnp_base = is_hdl ? a_mt_hdl_list : a_mt_list;
j1by1:
		for(mtnp = mtnp_base; mtnp != NIL; mtnp = mtnp->mtn_next){
			for(ext = &mtnp->mtn_line[mtnp->mtn_len];; ext = cp){
				cp = ext;
				while(su_cs_is_space(*cp))
					++cp;
				ext = cp;
				while(!su_cs_is_space(*cp) && *cp != '\0')
					++cp;

				if((i = P2UZ(cp - ext)) == 0)
					break;

				if(i == nlen && !su_cs_cmp_case_n(name, ext, nlen)){
					mtlp->mtl_node = mtnp;
					if((mtnp->mtn_flags & a_MT__TMASK) == a_MT_OTHER){
						name = su_empty;
						j = 0;
					}else{
						name = a_mt_names[mtnp->mtn_flags & a_MT__TMASK];
						j = su_cs_len(name);
					}
					i = mtnp->mtn_len;

					mtlp->mtl_result = su_AUTO_ALLOC(i + j +1);
					if(j > 0)
						su_mem_copy(mtlp->mtl_result, name, j);
					su_mem_copy(&mtlp->mtl_result[j], mtnp->mtn_line, i);
					mtlp->mtl_result[j += i] = '\0';
					goto jleave;
				}
			}
		}
		if(mtnp_base != a_mt_list){
			mtnp_base = a_mt_list;
			goto j1by1;
		}
	}

jnil_leave:
	mtlp = NIL;
jleave:
	NYD2_OU;
	return mtlp;
} /* }}} */

static struct a_mt_lookup *
a_mt_by_name(struct a_mt_lookup *mtlp, char const *name, boole is_hdl){ /* {{{ */
	uz nlen, i, j;
	char const *cp;
	struct a_mt_node *mtnp_base, *mtnp;
	NYD2_IN;

	STRUCT_ZERO(struct a_mt_lookup, mtlp);

	if((nlen = su_cs_len(name)) == 0)
		goto jnil_leave;

	if(!a_mt_is_init)
		a_mt_init();

	/* ..all the MIME types */
	mtnp_base = is_hdl ? a_mt_hdl_list : a_mt_list;
j1by1:
	for(mtnp = mtnp_base; mtnp != NIL; mtnp = mtnp->mtn_next){
		if((mtnp->mtn_flags & a_MT__TMASK) == a_MT_OTHER){
			cp = su_empty;
			j = 0;
		}else{
			cp = a_mt_names[mtnp->mtn_flags & a_MT__TMASK];
			j = su_cs_len(cp);
		}
		i = mtnp->mtn_len;

		if(i + j == nlen){
			char *xmt;

			xmt = su_LOFI_ALLOC(i + j +1);
			if(j > 0)
				su_mem_copy(xmt, cp, j);
			su_mem_copy(&xmt[j], mtnp->mtn_line, i);
			xmt[j += i] = '\0';
			i = su_cs_cmp_case(name, xmt);
			su_LOFI_FREE(xmt);

			/* Found it? */
			if(!i){
				mtlp->mtl_node = mtnp;
				goto jleave;
			}
		}
	}
	if(mtnp_base != a_mt_list){
		mtnp_base = a_mt_list;
		goto j1by1;
	}

jnil_leave:
	mtlp = NIL;
jleave:
	NYD2_OU;
	return mtlp;
} /* }}} */

static enum mx_mime_type
a_mt_classify_o_s_part(BITENUM(u32,a_mt_counter_evidence) mce, struct mimepart *mpp, boole deep_inspect){ /* {{{ */
	struct str in = {NIL, 0}, outrest, inrest, dec;
	struct mx_mime_probe_ctx mpc;
	int lc, c;
	uz cnt, lsz;
	FILE *ibuf;
	long start_off;
	boole did_inrest;
	BITENUM(u32,mx_mime_probe_flags) mpf;
	enum mx_mime_type mt;
	NYD2_IN;

	ASSERT(mpp->m_mime_enc != mx_MIME_ENC_BIN);

	outrest = inrest = dec = in;
	mt = mx_MIME_TYPE_UNKNOWN;
	mpf = mx_MIME_PROBE_NONE;
	did_inrest = FAL0;

	/* TODO v15-compat Note we actually bypass our usual file handling by
	 * TODO directly using fseek() on mb.mb_itf -- the v15 rewrite will change
	 * TODO all of this, and until then doing it like this is the only option
	 * TODO to integrate nicely into whoever calls us */
	if((start_off = ftell(mb.mb_itf)) == -1)
		goto jleave;
	if((ibuf = setinput(&mb, R(struct message*,mpp), NEED_BODY)) == NIL){
jos_leave:
		(void)fseek(mb.mb_itf, start_off, SEEK_SET);
		goto jleave;
	}
	cnt = mpp->m_size;

	/* Skip part headers */
	for(lc = '\0'; cnt > 0; lc = c, --cnt)
		if((c = getc(ibuf)) == EOF || (c == '\n' && lc == '\n'))
			break;
	if(cnt == 0 || ferror(ibuf))
		goto jos_leave;

	/* So now let's inspect the part content, decoding content-transfer-encoding along the way */
	/* TODO this should simply be "mime_factory_create(MPP)"!
	 * TODO In fact m_mime_classifier_(setup|call|call_part|finalize)() and the
	 * TODO state(s) should become reported to the outer
	 * TODO world like that (see MIME boundary TODO around here) */
	mx_mime_probe_setup(&mpc,
		(mx_MIME_PROBE_CT_TXT | (deep_inspect ? mx_MIME_PROBE_DEEP_INSPECT : mx_MIME_PROBE_NONE)));

	for(lsz = 0;;){
		boole dobuf;

		c = (--cnt == 0) ? EOF : getc(ibuf);
		if((dobuf = (c == '\n'))){
			/* Ignore empty lines */
			if(lsz == 0)
				continue;
		}else if((dobuf = (c == EOF))){
			if(lsz == 0 && outrest.l == 0)
				break;
		}

		if(in.l + 1 >= lsz)
			in.s = su_REALLOC(in.s, lsz += mx_LINESIZE);
		if(c != EOF)
			in.s[in.l++] = S(char,c);
		if(!dobuf)
			continue;

		/* On failure set PROBE_HASNUL so mt stays TYPE_UNKNOWN below */
jdobuf:
		switch(mpp->m_mime_enc){
		case mx_MIME_ENC_B64:
			if(!mx_b64_dec_part(&dec, &in, &outrest, (did_inrest ? NIL : &inrest))){
				mpc.mpc_mpf = mx_MIME_PROBE_HASNUL;
				goto jstopit;
			}
			break;
		case mx_MIME_ENC_QP:
			/* Drin */
			if(!mx_qp_dec_part(&dec, &in, &outrest, &inrest)){
				mpc.mpc_mpf = mx_MIME_PROBE_HASNUL;
				goto jstopit;
			}
			if(dec.l == 0 && c != EOF){
				in.l = 0;
				continue;
			}
			break;
		default:
			/* Temporarily switch those two buffers.. */
			dec = in;
			in.s = NIL;
			in.l = 0;
			break;
		}

		if((mpf = mx_mime_probe_round(&mpc, dec.s, S(sz,dec.l))) & mx_MIME_PROBE_NO_TXT_4SURE){
			mpf = mx_MIME_PROBE_HASNUL;
			goto jstopit;
		}

		if(c == EOF)
			break;
		/* ..and restore switched */
		if(in.s == NIL){
			in = dec;
			dec.s = NIL;
		}
		in.l = dec.l = 0;
	}

	if((in.l = inrest.l) > 0){
		in.s = inrest.s;
		inrest.s = NIL;
		did_inrest = TRU1;
		goto jdobuf;
	}
	if(outrest.l > 0)
		goto jdobuf;

jstopit:
	if(in.s != NIL)
		su_FREE(in.s);
	if(dec.s != NIL)
		su_FREE(dec.s);
	if(outrest.s != NIL)
		su_FREE(outrest.s);
	if(inrest.s != NIL)
		su_FREE(inrest.s);

	/* Restore file position to what caller expects (sic) */
	fseek(mb.mb_itf, start_off, SEEK_SET);

	if(!(mpf & (mx_MIME_PROBE_HASNUL /*| mx_MIME_PROBE_CTRLCHAR XXX really? */))){
		/* In that special relaxed case we may very well wave through octet-streams full of control characters,
		 * as they do no harm */
		/* TODO This should be part of m_mime_classifier_finalize() then! */
		if(deep_inspect && mpc.mpc_all_len - mpc.mpc_all_bogus < mpc.mpc_all_len >> 2)
			goto jleave;

		mt = mx_MIME_TYPE_TEXT_PLAIN;
		if(mce & a_MT_CE_ALL_OVWR)
			mpp->m_ct_type_plain = "text/plain";
		if(mce & (a_MT_CE_BIN_OVWR | a_MT_CE_ALL_OVWR))
			mpp->m_ct_type_usr_ovwr = "text/plain";
	}

jleave:
	NYD2_OU;
	return mt;
} /* }}} */

static BITENUM(u32,mx_mime_type_handler_flags)
a_mt_pipe_check(struct mx_mime_type_handler *mthp, enum sendaction action){ /* {{{ */
	char const *cp;
	BITENUM(u32,mx_mime_type_handler_flags) rv_orig, rv;
	NYD2_IN;

	rv_orig = rv = mthp->mth_flags;
	ASSERT((rv & mx_MIME_TYPE_HDL_TYPE_MASK) == mx_MIME_TYPE_HDL_NIL);

	/* Do we have any handler for this part? */
	if(*(cp = mthp->mth_shell_cmd) == '\0')
		goto jleave;
	else if(*cp++ != '?' && cp[-1] != '@'/* v15compat */){
		rv |= mx_MIME_TYPE_HDL_CMD;
		goto jleave;
	}else{
		if(cp[-1] == '@')/* v15compat */
			n_OBSOLETE2(_("*pipe-TYPE/SUBTYPE*+': type markers (and much more) use ? not @"),
				mthp->mth_shell_cmd);
		if(*cp == '\0'){
			rv |= mx_MIME_TYPE_HDL_TEXT | mx_MIME_TYPE_HDL_COPIOUSOUTPUT;
			goto jleave;
		}
	}

jnextc:
	switch(*cp){
	case '*': rv |= mx_MIME_TYPE_HDL_COPIOUSOUTPUT; ++cp; goto jnextc;
	case '#': rv |= mx_MIME_TYPE_HDL_NOQUOTE; ++cp; goto jnextc;
	case '&': rv |= mx_MIME_TYPE_HDL_ASYNC; ++cp; goto jnextc;
	case '!': rv |= mx_MIME_TYPE_HDL_NEEDSTERM; ++cp; goto jnextc;
	case '+':
		if(rv & mx_MIME_TYPE_HDL_TMPF)
			rv |= mx_MIME_TYPE_HDL_TMPF_UNLINK;
		rv |= mx_MIME_TYPE_HDL_TMPF;
		++cp;
		goto jnextc;
	case '=':
		rv |= mx_MIME_TYPE_HDL_TMPF_FILL;
		++cp;
		goto jnextc;

	case 't':
		switch(rv & mx_MIME_TYPE_HDL_TYPE_MASK){
		case mx_MIME_TYPE_HDL_NIL: /* FALLTHRU */
		case mx_MIME_TYPE_HDL_TEXT: break;
		default:
			cp = N_("only one type-marker can be used");
			goto jerrlog;
		}
		rv |= mx_MIME_TYPE_HDL_TEXT | mx_MIME_TYPE_HDL_COPIOUSOUTPUT;
		++cp;
		goto jnextc;
	case 'h':
		switch(rv & mx_MIME_TYPE_HDL_TYPE_MASK){
		case mx_MIME_TYPE_HDL_NIL: /* FALLTHRU */
		case mx_MIME_TYPE_HDL_HTML: break;
		default:
			cp = N_("only one type-marker can be used");
			goto jerrlog;
		}
#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
		mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s = UNCONST(char*,_("Built-in HTML tagsoup filter")));
		rv |= mx_MIME_TYPE_HDL_HTML | mx_MIME_TYPE_HDL_COPIOUSOUTPUT;
		++cp;
		goto jnextc;
#else
		cp = N_("?h type-marker unsupported (HTML tagsoup filter not built-in)");
		goto jerrlog;
#endif

	case '@':/* v15compat */
		/* FALLTHRU */
	case '?': /* End of flags */
		++cp;
		/* FALLTHRU */
	default:
		break;
	}
	mthp->mth_shell_cmd = cp;

	/* Implications */
	if(rv & mx_MIME_TYPE_HDL_TMPF_FILL)
		rv |= mx_MIME_TYPE_HDL_TMPF;

	/* Exceptions */
	if(action == SEND_QUOTE || action == SEND_QUOTE_ALL){
		if(rv & mx_MIME_TYPE_HDL_NOQUOTE)
			goto jerr;
		/* Cannot fetch data back from asynchronous process */
		if(rv & mx_MIME_TYPE_HDL_ASYNC)
			goto jerr;
		if(rv & mx_MIME_TYPE_HDL_NEEDSTERM) /* XXX for now */
			goto jerr;
		/* xxx Need copiousoutput, and nothing else (for now) */
		if(!(rv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT))
			goto jerr;
	}
	/* Log errors for the rest */

	if(rv & mx_MIME_TYPE_HDL_NEEDSTERM){
		if(rv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT){
			cp = N_("cannot combine needsterminal and copiousoutput");
			goto jerrlog;
		}
		if(rv & mx_MIME_TYPE_HDL_ASYNC){
			cp = N_("cannot combine needsterminal and x-mailx-async");
			goto jerrlog;
		}
		/* needsterminal needs a terminal */
		if(!(n_psonce & n_PSO_INTERACTIVE))
			goto jerr;
	}

	if(rv & mx_MIME_TYPE_HDL_ASYNC){
		if(rv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT){
			cp = N_("cannot combine x-mailx-async and copiousoutput");
			goto jerrlog;
		}
		if(rv & mx_MIME_TYPE_HDL_TMPF_UNLINK){
			cp = N_("cannot combine x-mailx-async and x-mailx-tmpfile-unlink");
			goto jerrlog;
		}
	}

	if((rv & mx_MIME_TYPE_HDL_TYPE_MASK) != mx_MIME_TYPE_HDL_NIL){
		if(rv & ~(mx_MIME_TYPE_HDL_TYPE_MASK | mx_MIME_TYPE_HDL_COPIOUSOUTPUT | mx_MIME_TYPE_HDL_NOQUOTE)){
			cp = N_("?[th] type-markers only support flags * and #");
			goto jerrlog;
		}
	}else
		rv |= mx_MIME_TYPE_HDL_CMD;

jleave:
	mthp->mth_flags = rv;
	NYD2_OU;
	return rv;

jerrlog:
	n_err(_("MIME type handlers: %s\n"), V_(cp));
jerr:
	rv = rv_orig;
	goto jleave;
} /* }}} */

boole
mx_mime_type_is_valid(char const *name, boole t_a_subt, boole subt_wildcard_ok){
	char c;
	NYD2_IN;

	if(t_a_subt)
		t_a_subt = TRU1;

	while((c = *name++) != '\0'){
		/* RFC 4288, section 4.2 */
		if(su_cs_is_alnum(c) || c == '!' || c == '#' || c == '$' || c == '&' || c == '.' || c == '+' ||
				c == '-' || c == '^' || c == '_')
			continue;

		if(c == '/'){
			if(t_a_subt != TRU1)
				break;
			t_a_subt = TRUM1;
			continue;
		}

		if(c == '*' && t_a_subt == TRUM1 && subt_wildcard_ok)
			/* Must be last character, then */
			c = *name;
		break;
	}

	NYD2_OU;
	return (c == '\0');
}

char *
mx_mime_type_classify_path(char const *path){
	struct a_mt_lookup mtl;
	NYD_IN;

	a_mt_by_filename(&mtl, path, FAL0);

	NYD_OU;
	return mtl.mtl_result;
}

boole
mx_mime_type_classify_fp(struct mx_mime_type_classify_fp_ctx *mtcfcp, FILE *fp){ /* {{{ XXX cleanup */
	/* TODO message/rfc822 is special in that it may only be 7bit, 8bit or
	 * TODO binary according to RFC 2046, 5.2.1
	 * TODO The handling of which is a hack */
	struct mx_mime_probe_ctx mpc;
	off_t start_off, fpsz;
	enum mx_mime_enc menc;
	enum mx_mime_probe_flags mpf;
	boole rfc822;
	struct mx_mime_probe_charset_ctx mpcc, *mpccp;
	NYD_IN;

	if((mpccp = mtcfcp->mtcfc_mpccp_or_nil) == NIL)
		mpccp = mx_mime_probe_charset_ctx_setup(&mpcc);

	rfc822 = FAL0;
	if(mtcfcp->mtcfc_content_type == NIL)
		mpf = mx_MIME_PROBE_CT_NONE;
	else if(!su_cs_cmp_case_n(mtcfcp->mtcfc_content_type, "text/", 5))
		mpf = ok_blook(mime_allow_text_controls) ? mx_MIME_PROBE_CT_TXT | mx_MIME_PROBE_CT_TXT_COK
				: mx_MIME_PROBE_CT_TXT;
	else{
		struct a_mt_lookup mtl;

		if(a_mt_by_name(&mtl, mtcfcp->mtcfc_content_type, FAL0) != NIL &&
				(mtl.mtl_node->mtn_flags & a_MT_TM_SEND_TEXT)){
			mpf = mx_MIME_PROBE_CT_TXT;
			mtcfcp->mtcfc_content_type = "text/plain";
		}else if(!su_cs_cmp_case(mtcfcp->mtcfc_content_type, "message/rfc822")){
			mpf = mx_MIME_PROBE_CT_TXT;
			rfc822 = TRU1;
		}else
			mpf = mx_MIME_PROBE_CLEAN;
	}

	mpf = mx_mime_probe_setup(&mpc, mpf)->mpc_mpf;

	start_off = ftell(fp);
	fpsz = fsize(fp);
	if(start_off == -1 || fpsz == -1){
		su_err_set(su_ERR_IO);
		mtcfcp = NIL;
		goto jerr;
	}else if(fpsz == 0 || start_off == fpsz){
		menc = mx_MIME_ENC_7B;
		goto jleave;
	}else{
		char *buf;

		buf = su_LOFI_ALLOC(mx_BUFFER_SIZE);
		for(;;){
			uz i;

			i = fread(buf, sizeof(buf[0]), mx_BUFFER_SIZE, fp);
			if(i == 0 && !feof(fp)){
jioerr:
				if(su_err_by_errno() == su_ERR_INTR)
					continue;
				if(su_err() == su_ERR_NONE)
					su_err_set(su_ERR_IO);
				mtcfcp = NIL;
				break;
			}
			if((mpf = mx_mime_probe_round(&mpc, buf, i)) & mx_MIME_PROBE_NO_TXT_4SURE)
				break;
			if(i == 0)
				break;
		}
		su_LOFI_FREE(buf);

		if(mtcfcp == NIL)
			goto jerr;

		clearerr(fp);
		if(fseek(fp, start_off, SEEK_SET) == -1)
			goto jioerr;
	}

	if(mpf & mx_MIME_PROBE_HASNUL){
		menc = mx_MIME_ENC_B64;
		/* XXX Do not overwrite text content-type to allow UTF-16 and such, but
		 * XXX only on request; otherwise enforce what file(1)/libmagic(3) would
		 * XXX suggest.  This is crap, of course, need to deal with UTF->8! */
		if(mpf & mx_MIME_PROBE_CT_TXT_COK)
			goto jleave;
		if(mpf & (mx_MIME_PROBE_CT_NONE | mx_MIME_PROBE_CT_TXT)){
			mpf &= ~(mx_MIME_PROBE_CT_NONE | mx_MIME_PROBE_CT_TXT | mx_MIME_PROBE_CT_TXT_COK |
					mx_MIME_PROBE_TTYC5T_UTF8 | mx_MIME_PROBE_TTYC5T_OTHER);
			mtcfcp->mtcfc_content_type = "application/octet-stream";
		}
		rfc822 = FAL0;
		goto jleave;
	}else
		menc = mx_mime_enc_target();

	if(mpf & (mx_MIME_PROBE_LONGLINES | mx_MIME_PROBE_CRLF | mx_MIME_PROBE_CTRLCHAR | mx_MIME_PROBE_NOTERMNL |
			mx_MIME_PROBE_FROM_)){
		if(menc != mx_MIME_ENC_B64 && menc != mx_MIME_ENC_QP){
			/* If the user chooses 8bit, and we do not privacy-sign the message,
			 * then if encoding would be enforced only because of a ^From_, no */
			if((mpf & (mx_MIME_PROBE_LONGLINES | mx_MIME_PROBE_CRLF | mx_MIME_PROBE_CTRLCHAR |
						mx_MIME_PROBE_NOTERMNL | mx_MIME_PROBE_FROM_)) != mx_MIME_PROBE_FROM_ ||
					!mtcfcp->mtcfc_cte_not_for_from_)
				menc = mx_MIME_ENC_QP;
			else{
				ASSERT(menc != mx_MIME_ENC_7B);
				menc = (mpf & mx_MIME_PROBE_HIGHBIT) ? mx_MIME_ENC_8B : mx_MIME_ENC_7B;
			}
		}

		mtcfcp->mtcfc_do_iconv = ((mpf & mx_MIME_PROBE_HIGHBIT) != 0);
	}else if(mpf & mx_MIME_PROBE_HIGHBIT){
		ASSERT(menc != mx_MIME_ENC_7B);
		ASSERT(!(mpf & (mx_MIME_PROBE_TTYC5T_UTF8 | mx_MIME_PROBE_TTYC5T_OTHER)) ||
			(mpf & (mx_MIME_PROBE_CT_NONE | mx_MIME_PROBE_CT_TXT)));
		if(mpf & (mx_MIME_PROBE_CT_NONE | mx_MIME_PROBE_CT_TXT))
			mtcfcp->mtcfc_do_iconv = TRU1;
	}else{
		ASSERT(menc != mx_MIME_ENC_7B);
		menc = mx_MIME_ENC_7B;
	}

jleave:
	if(!(mpf & (mx_MIME_PROBE_CT_NONE | mx_MIME_PROBE_CT_TXT | mx_MIME_PROBE_CT_TXT_COK))){
		mtcfcp->mtcfc_7bit_clean = mtcfcp->mtcfc_do_iconv = FAL0;
		ASSERT(mtcfcp->mtcfc_charset == NIL);
		ASSERT(mtcfcp->mtcfc_input_charset == NIL);
		ASSERT(mtcfcp->mtcfc_charset_is_ascii == FAL0);
		mtcfcp->mtcfc_conversion = (menc == mx_MIME_ENC_7B ? CONV_7BIT
				: (menc == mx_MIME_ENC_8B ? CONV_8BIT
				: (menc == mx_MIME_ENC_QP ? CONV_TOQP : CONV_TOB64)));
	}else{
		if(mpf & mx_MIME_PROBE_CT_NONE){
			mpf |= mx_MIME_PROBE_CT_TXT;
			mtcfcp->mtcfc_content_type = "text/plain";
			mtcfcp->mtcfc_ct_is_text_plain = TRU1;
		}else if((mpf & mx_MIME_PROBE_CT_TXT) && !su_cs_cmp_case(mtcfcp->mtcfc_content_type, "text/plain"))
			mtcfcp->mtcfc_ct_is_text_plain = TRU1;

		if(!(mpf & mx_MIME_PROBE_HIGHBIT))
			mtcfcp->mtcfc_7bit_clean = TRU1;
		if(mpccp->mpcc_iconv_disable)
			mtcfcp->mtcfc_do_iconv = FAL0;

		if(rfc822){
			if(mpf & mx_MIME_PROBE_FROM_1STLINE){
				n_err(
_("Pre-v15 %s cannot handle message/rfc822 that indeed is a RFC 4155 MBOX!\n  Forcing a content-type of application/mbox!\n"),
					n_uagent);
				mtcfcp->mtcfc_content_type = "application/mbox";
				goto jnorfc822;
			}
			mtcfcp->mtcfc_conversion = (menc == mx_MIME_ENC_7B ? CONV_7BIT
					: (menc == mx_MIME_ENC_8B ? CONV_8BIT
					/* May have only 7-bit, 8-bit and binary.  Try to avoid latter */
					: ((mpf & mx_MIME_PROBE_HASNUL) ? CONV_NONE
					: ((mpf & mx_MIME_PROBE_HIGHBIT) ? CONV_8BIT : CONV_7BIT))));
		}else
jnorfc822:
			mtcfcp->mtcfc_conversion = (menc == mx_MIME_ENC_7B ? CONV_7BIT
					: (menc == mx_MIME_ENC_8B ? CONV_8BIT
					: (menc == mx_MIME_ENC_QP ? CONV_TOQP : CONV_TOB64)));
		mtcfcp->mtcfc_mime_enc = menc;

		ASSERT(!(mpf & (mx_MIME_PROBE_TTYC5T_UTF8 | mx_MIME_PROBE_TTYC5T_OTHER)) ||
			(mpf & (mx_MIME_PROBE_CT_NONE | mx_MIME_PROBE_CT_TXT)));

		mtcfcp->mtcfc_input_charset = ((mpf & mx_MIME_PROBE_TTYC5T_UTF8) ? su_utf8_name_lower
				: (((mpf & mx_MIME_PROBE_TTYC5T_OTHER) && *mpc.mpc_ttyc5t_detect != '\0')
					? mpc.mpc_ttyc5t_detect :
					  ((mpf & mx_MIME_PROBE_HIGHBIT) ? mpccp->mpcc_ttyc5t : mpccp->mpcc_cs_7bit)));
		if(!mtcfcp->mtcfc_do_iconv){
			mtcfcp->mtcfc_charset = mtcfcp->mtcfc_input_charset;
			mtcfcp->mtcfc_charset_is_ascii = n_iconv_name_is_ascii(mtcfcp->mtcfc_charset);
		}else{
			ASSERT(mtcfcp->mtcfc_charset == NIL);
			ASSERT(mtcfcp->mtcfc_charset_is_ascii == FAL0);
		}
	}

jerr:
	NYD_OU;
	return (mtcfcp != NIL);
} /* }}} */

enum mx_mime_type
mx_mime_type_classify_part(struct mimepart *mpp, boole is_hdl){ /* {{{ */
	/* TODO n_mimetype_classify_part() <-> m_mime_classifier_ with life cycle */
	struct a_mt_lookup mtl;
	boole is_os;
	union {char const *cp; u32 f;} mce;
	char const *ct;
	enum mx_mime_type mc;
	NYD_IN;

	mc = mx_MIME_TYPE_UNKNOWN;
	if((ct = mpp->m_ct_type_plain) == NIL) /* TODO may not */
		ct = su_empty;

	if((mce.cp = ok_vlook(mime_counter_evidence)) != NIL && *mce.cp != '\0'){
		if((su_idec_u32_cp(&mce.f, mce.cp, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
				) != su_IDEC_STATE_CONSUMED){
			n_err(_("Invalid *mime-counter-evidence* value content\n"));
			is_os = FAL0;
		}else{
			mce.f |= a_MT_CE_SET;
			is_os = !su_cs_cmp_case(ct, "application/octet-stream");

			if(mpp->m_filename != NIL && (is_os || (mce.f & a_MT_CE_ALL_OVWR))){
				if(a_mt_by_filename(&mtl, mpp->m_filename, is_hdl) == NIL){
					if(is_os)
						goto jos_content_check;
				}else if(is_os || su_cs_cmp_case(ct, mtl.mtl_result)){
					if(mce.f & a_MT_CE_ALL_OVWR)
						mpp->m_ct_type_plain = ct = mtl.mtl_result;
					if(mce.f & (a_MT_CE_BIN_OVWR | a_MT_CE_ALL_OVWR))
						mpp->m_ct_type_usr_ovwr = ct = mtl.mtl_result;
				}
			}
		}
	}else
		is_os = FAL0;

	if(*ct == '\0' || su_cs_find_c(ct, '/') == NIL) /* Compat with non-MIME */
		mc = mx_MIME_TYPE_TEXT;
	else if(su_cs_starts_with_case(ct, "text/")){
		ct += sizeof("text/") -1;
		if(!su_cs_cmp_case(ct, "plain"))
			mc = mx_MIME_TYPE_TEXT_PLAIN;
		else if(!su_cs_cmp_case(ct, "html"))
			mc = mx_MIME_TYPE_TEXT_HTML;
		else
			mc = mx_MIME_TYPE_TEXT;
	}else if(su_cs_starts_with_case(ct, "message/")){
		ct += sizeof("message/") -1;
		if(!su_cs_cmp_case(ct, "rfc822"))
			mc = mx_MIME_TYPE_822;
		else
			mc = mx_MIME_TYPE_MESSAGE;
	}else if(su_cs_starts_with_case(ct, "multipart/")){
		struct multi_types{
			char mt_name[12];
			enum mx_mime_type mt_mc;
		} const mta[] = {
			{"alternative\0", mx_MIME_TYPE_ALTERNATIVE},
			{"related", mx_MIME_TYPE_RELATED},
			{"digest", mx_MIME_TYPE_DIGEST},
			{"signed", mx_MIME_TYPE_SIGNED},
			{"encrypted", mx_MIME_TYPE_ENCRYPTED}
		}, *mtap;

		for(ct += sizeof("multipart/") -1, mtap = mta;;)
			if(!su_cs_cmp_case(ct, mtap->mt_name)){
				mc = mtap->mt_mc;
				break;
			}else if(++mtap == &mta[NELEM(mta)]){
				mc = mx_MIME_TYPE_MULTI;
				break;
			}
	}else if(su_cs_starts_with_case(ct, "application/")){
		if(is_os)
			goto jos_content_check;
		ct += sizeof("application/") -1;
		if(!su_cs_cmp_case(ct, "pkcs7-mime") || !su_cs_cmp_case(ct, "x-pkcs7-mime"))
			mc = mx_MIME_TYPE_PKCS7;
	}

jleave:
	NYD_OU;
	return mc;

jos_content_check:
	if((mce.f & a_MT_CE_BIN_PARSE) && mpp->m_mime_enc != mx_MIME_ENC_BIN && mpp->m_charset_or_nil != NIL)
		mc = a_mt_classify_o_s_part(mce.f, mpp, is_hdl);
	goto jleave;
} /* }}} */

BITENUM(u32,mx_mime_type_handler_flags)
mx_mime_type_handler(struct mx_mime_type_handler *mthp, struct mimepart const *mpp, enum sendaction action){ /* {{{ */
#define a__S "pipe-"
#define a__L (sizeof(a__S) -1)

	struct a_mt_lookup mtl;
	char const *es, *cs;
	uz el, cl, l;
	char *buf, *cp;
	BITENUM(u32,mx_mime_type_hander_flags) rv, xrv;
	NYD_IN;

	STRUCT_ZERO(struct mx_mime_type_handler, mthp);
	buf = NIL;
	xrv = rv = mx_MIME_TYPE_HDL_NIL;

	if(action != SEND_QUOTE && action != SEND_QUOTE_ALL &&
			action != SEND_TODISP && action != SEND_TODISP_ALL && action != SEND_TODISP_PARTS &&
			action != SEND_TOFILE)
		goto jleave;

	el = ((es = mpp->m_filename) != NIL) ? su_cs_len(es) : 0;
	cl = ((cs = mpp->m_ct_type_usr_ovwr) != NIL || (cs = mpp->m_ct_type_plain) != NIL) ? su_cs_len(cs) : 0;
	if((l = MAX(el, cl)) == 0)
		/* TODO this should be done during parse time! */
		goto jleave;

	/* We do not pass the flags around, so ensure carrier is up-to-date */
	mthp->mth_flags = rv;

	buf = su_LOFI_ALLOC(a__L + l +1);
	su_mem_copy(buf, a__S, a__L);

	/* I. *pipe-EXTENSION* handlers take precedence.  We "fail" here for file extensions which clash MIME types */
	if(el > 0){
		boole ever;

		/* "basename()" it */
		for(l = el; l > 0 && es[l - 1] != su_PATH_SEP_C;)
			--l;
		es += l;
		el -= l;

		if(el == 0)
			goto jc_t;

		/* A leading dot does not belong to an extension */
		if(*es == '.'){
			++es;
			--el;
		}

		for(ever = FAL0;; ever = TRU1){
			char const *ccp;

			if((ccp = su_mem_find(es, '.', el)) != NIL){
				el -= P2UZ(++ccp - es);
				es = ccp;
			}else if(ever)
				break;
			ASSERT(el > 0);

			su_mem_copy(&buf[a__L], es, el +1);
			for(cp = &buf[a__L]; *cp != '\0'; ++cp)
				*cp = su_cs_to_lower(*cp);

			if((mthp->mth_shell_cmd = ccp = n_var_vlook(buf, FAL0)) != NIL){
				rv = a_mt_pipe_check(mthp, action);
				if((rv & mx_MIME_TYPE_HDL_TYPE_MASK) != mx_MIME_TYPE_HDL_NIL)
					goto jleave;
			}
		}
	}

	/* Only MIME Content-Type: to follow, if any */
jc_t:
	if(cl == 0)
		goto jleave;

	/* ..and ensure a case-normalized variant is henceforth used */
	su_mem_copy(cp = &buf[a__L], cs, cl +1);
	cs = cp;
	for(; *cp != '\0'; ++cp)
		*cp = su_cs_to_lower(*cp);

	/* II.: *pipe-TYPE/SUBTYPE* */
	if((mthp->mth_shell_cmd = n_var_vlook(buf, FAL0)) != NIL){
		rv = a_mt_pipe_check(mthp, action);
		if((rv & mx_MIME_TYPE_HDL_TYPE_MASK) != mx_MIME_TYPE_HDL_NIL)
			goto jleave;
	}

	/* III. RFC 1524 / Mailcap lookup */
#ifdef mx_HAVE_MAILCAP
	if(action != SEND_TOFILE){ /* xxx use "print" slot?  no.. */
		xrv = rv = mx_MIME_TYPE_HDL_NIL;
		switch(mx_mailcap_handler(mthp, cs, action, mpp)){
		case TRU1:
			rv = mthp->mth_flags;
			goto jleave;
		case TRUM1:
			xrv = mthp->mth_flags; /* "Use at last-resort" handler */
			break;
		default:
			break;
		}
	}
#endif

	/* IV. and final: `mimetype' type-marker extension induced handler */
	if(a_mt_by_name(&mtl, cs, TRU1) != NIL){
		switch(mtl.mtl_node->mtn_flags & a_MT__TM_MARKMASK){
#ifndef mx_HAVE_FILTER_HTML_TAGSOUP
		case a_MT_TM_SOUP_H:
			break;
#endif
		case a_MT_TM_SOUP_h:
#ifdef mx_HAVE_FILTER_HTML_TAGSOUP
		case a_MT_TM_SOUP_H:
			mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s = UNCONST(char*,_("Built-in HTML tagsoup filter")));
			rv ^= mx_MIME_TYPE_HDL_NIL | mx_MIME_TYPE_HDL_HTML;
			goto jleave;
#endif
			/* FALLTHRU */
		case a_MT_TM_PLAIN:
			mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s = UNCONST(char*,_("Plain text")));
			rv ^= mx_MIME_TYPE_HDL_NIL | mx_MIME_TYPE_HDL_TEXT;
			goto jleave;
		case a_MT_TM_QUIET:
			mthp->mth_msg.l = 0;
			mthp->mth_msg.s = UNCONST(char*,su_empty);
			goto jleave;
		default:
			break;
		}
	}

	/* x-mailx-last-resort, anyone? XXX support that for *pipe-TYPE/SUBTYPE* */
	if(xrv != mx_MIME_TYPE_HDL_NIL)
		rv = xrv;

jleave:
	if(buf != NIL)
		su_LOFI_FREE(buf);

	xrv = rv;
	if((rv &= mx_MIME_TYPE_HDL_TYPE_MASK) == mx_MIME_TYPE_HDL_NIL){
		if(mthp->mth_msg.s == NIL)
			mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s =
					UNCONST(char*,A_("[-- No handler usable (maybe try command `mimeview') --]\n")));
	}else if(rv == mx_MIME_TYPE_HDL_CMD && !(xrv & mx_MIME_TYPE_HDL_COPIOUSOUTPUT) && action != SEND_TODISP_PARTS){
		mthp->mth_msg.l = su_cs_len(mthp->mth_msg.s =
				UNCONST(char*,_("[-- Use the command `mimeview' to display this --]\n")));
		xrv &= ~mx_MIME_TYPE_HDL_TYPE_MASK;
		xrv |= (rv = mx_MIME_TYPE_HDL_MSG);
	}

	mthp->mth_flags = xrv;

	NYD_OU;
	return rv;

#undef a__L
#undef a__S
} /* }}} */

int
c_mimetype(void *vp){ /* {{{ */
	struct n_string s_b, *s;
	struct a_mt_node *mtnp_base, *mtnp;
	char **argv;
	NYD_IN;

	if(!a_mt_is_init)
		a_mt_init();

	s = n_string_creat_auto(&s_b);
	s = n_string_reserve(s, 127);

	if(LIKELY(*(argv = vp) != NIL)){
		for(; *argv != NIL; ++argv){
			if(s->s_len > 0)
				s = n_string_push_c(s, ' ');
			s = n_string_push_cp(s, *argv);
		}

		mtnp = a_mt_create(TRU1, a_MT_CMD, n_string_cp(s), s->s_len);
		if(mtnp != NIL){
			if(!(mtnp->mtn_flags & a_MT_TM_ONLY_HANDLER)){
				mtnp->mtn_next = a_mt_list;
				a_mt_list = mtnp;
			}else{
				mtnp->mtn_next = a_mt_hdl_list;
				a_mt_hdl_list = mtnp;
			}
		}else
			vp = NIL;
	}else{
		FILE *fp;
		uz l;

		if(a_mt_list == NIL && a_mt_hdl_list == NIL){
			fprintf(n_stdout, _("# `mimetype': no mime.types(5) data available\n"));
			goto jleave;
		}

		if((fp = mx_fs_tmp_open(NIL, "mimetype", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)) == NIL){
			n_perr(_("tmpfile"), 0);
			fp = n_stdout;
		}

		l = 0;
		mtnp_base = a_mt_list;
jiter:
		for(mtnp = mtnp_base; mtnp != NIL; l += 2, mtnp = mtnp->mtn_next){
			s = n_string_trunc(s, 0);

			if(n_poption & n_PO_D_V){
				s = n_string_push_cp(s,
						(mtnp->mtn_flags & a_MT_USR ? "# user"
							: (mtnp->mtn_flags & a_MT_SYS ? "# system"
							: (mtnp->mtn_flags & a_MT_FSPEC ? "# f= file"
							: (mtnp->mtn_flags & a_MT_CMD ? "# command"
							: "# built-in")))));
				if(mtnp->mtn_flags & a_MT_TM_ONLY_HANDLER)
					s = n_string_push_cp(s, _(" (not for sending mail)"));
				else if(mtnp->mtn_flags & a_MT_TM_SEND_TEXT)
					s = n_string_push_cp(s, _(" (send/attach as text/plain)"));
				s = n_string_push_cp(s, "\n  ");
			}

			s = n_string_push_buf(s, "mimetype ", sizeof("mimetype ")-1);

			if(mtnp->mtn_flags & (a_MT__TM_MARKMASK | a_MT_TM__FLAG_MASK)){
				char c;

				s = n_string_push_c(s, '?');

				switch(mtnp->mtn_flags & a_MT__TM_MARKMASK){
				case a_MT_TM_PLAIN: c = 't'; break;
				case a_MT_TM_SOUP_h: c = 'h'; break;
				case a_MT_TM_SOUP_H: c = 'H'; break;
				case a_MT_TM_QUIET: c = 'q'; break;
				default: c = '\0'; break;
				}
				if(c != '\0'){
					s = n_string_push_c(s, c);

					if(mtnp->mtn_flags & a_MT_TM__FLAG_MASK)
						s = n_string_push_c(s, ',');
				}

				if(mtnp->mtn_flags & a_MT_TM_ONLY_HANDLER)
					s = n_string_push_cp(s, "only-handler");
				else if(mtnp->mtn_flags & a_MT_TM_SEND_TEXT)
					s = n_string_push_cp(s, "send-text");

				s = n_string_push_c(s, ' ');
			}

			if((mtnp->mtn_flags & a_MT__TMASK) != a_MT_OTHER)
				s = n_string_push_cp(s, a_mt_names[mtnp->mtn_flags & a_MT__TMASK]);

			s = n_string_push_buf(s, mtnp->mtn_line, mtnp->mtn_len);
			s = n_string_push_c(s, ' ');
			s = n_string_push_c(s, ' ');
			s = n_string_push_cp(s, &mtnp->mtn_line[mtnp->mtn_len]);

			s = n_string_push_c(s, '\n');
			fputs(n_string_cp(s), fp);
		}
		if(mtnp_base == a_mt_list){
			mtnp_base = a_mt_hdl_list;
			goto jiter;
		}

		if(fp != n_stdout){
			page_or_print(fp, l);

			mx_fs_close(fp);
		}else
			clearerr(fp);
	}

jleave:
	NYD_OU;
	return (vp == NIL ? su_EX_ERR : su_EX_OK);
} /* }}} */

int
c_unmimetype(void *vp){ /* {{{ */
	boole match;
	struct a_mt_node **mtnpp, *mtnp, **lnpp;
	char **argv;
	NYD_IN;

	argv = vp;

	/* Need to load that first as necessary */
	if(!a_mt_is_init)
		a_mt_init();

	for(; *argv != NIL; ++argv){
		if(!su_cs_cmp_case(*argv, "reset")){
			a_mt_is_init = FAL0;
			goto jdelall;
		}

		if(argv[0][0] == '*' && argv[0][1] == '\0'){
jdelall:
			for(mtnpp = &a_mt_hdl_list;; mtnpp = &a_mt_list){
				while((mtnp = *mtnpp) != NIL){
					*mtnpp = mtnp->mtn_next;
					su_FREE(mtnp);
				}
				if(mtnpp != &a_mt_hdl_list)
					break;
			}
			continue;
		}

		match = FAL0;
		mtnpp = &a_mt_hdl_list;
j1by1:
		for(mtnp = *(lnpp = mtnpp); mtnp != NIL;){
			char *val;
			uz i;
			char const *typ;

			if((mtnp->mtn_flags & a_MT__TMASK) == a_MT_OTHER){
				typ = su_empty;
				i = 0;
			}else{
				typ = a_mt_names[mtnp->mtn_flags & a_MT__TMASK];
				i = su_cs_len(typ);
			}

			val = su_LOFI_ALLOC(i + mtnp->mtn_len +1);
			su_mem_copy(val, typ, i);
			su_mem_copy(&val[i], mtnp->mtn_line, mtnp->mtn_len);
			val[i += mtnp->mtn_len] = '\0';
			i = su_cs_cmp_case(val, *argv);
			su_LOFI_FREE(val);

			if(!i){
				*lnpp = mtnp->mtn_next;
				su_FREE(mtnp);
				mtnp = *lnpp;
				match = TRU1;
			}else
				mtnp = *(lnpp = &mtnp->mtn_next);
		}
		if(mtnpp == &a_mt_hdl_list){
			mtnpp = &a_mt_list;
			goto j1by1;
		}

		if(!match){
			if(!(n_pstate & n_PS_ROBOT) || (n_poption & n_PO_D_V))
				n_err(_("No such MIME type: %s\n"), n_shexp_quote_cp(*argv, FAL0));
			vp = NIL;
		}
	}

	NYD_OU;
	return (vp == NIL ? su_EX_ERR : su_EX_OK);
} /* }}} */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MIME_TYPE
/* s-itt-mode */
