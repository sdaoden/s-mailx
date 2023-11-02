/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Auxiliary functions that do not fit anywhere else.
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
#define su_FILE auxlily
#define mx_SOURCE
#define mx_SOURCE_AUXLILY

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#ifdef mx_HAVE_NET
# ifdef mx_HAVE_GETADDRINFO
#  include <sys/socket.h>
# endif
#endif

#include <stdarg.h>

#ifdef mx_HAVE_IDNA
# if mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN2
#  include <idn2.h>
# elif mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN
#  include <idna.h>
#  include <idn-free.h>
# elif mx_HAVE_IDNA == n_IDNA_IMPL_IDNKIT
#  include <idn/api.h>
# endif
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/path.h>
#include <su/sort.h>

#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/colour.h"
#include "mx/fexpand.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/termios.h"
#include "mx/tty.h"

#ifdef mx_HAVE_ERRORS
# include "mx/cmd.h"
#endif
#ifdef mx_HAVE_IDNA
# include "mx/iconv.h"
#endif

/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* The difference in between mx_HAVE_ERRORS and not, is size of queue only */
struct a_aux_err_node{
	struct a_aux_err_node *ae_next;
	u32 ae_cnt;
	boole ae_done;
	u8 ae_pad[3];
	uz ae_dumped_till;
	struct n_string ae_str;
};

/* Error ring, for `errors' */
static struct a_aux_err_node *a_aux_err_head;
static struct a_aux_err_node *a_aux_err_tail;

/* Get our $PAGER; if env_addon is not NIL it is checked whether we know about some environment variable that supports
 * colour+ and set *env_addon to that, e.g., "LESS=FRSXi" */
static char const *a_aux_pager_get(char const **env_addon);

static char const *
a_aux_pager_get(char const **env_addon){
	char const *rv;
	NYD2_IN;

	rv = ok_vlook(PAGER);

	if(env_addon != NIL){
		*env_addon = NIL;
		/* Update the manual upon any changes: *colour-pager*, $PAGER */
		if(su_cs_find(rv, "less") != NIL){
			if(getenv("LESS") == NIL)
				*env_addon = "LESS=RIFE";
		}else if(su_cs_find(rv, "lv") != NIL){
			if(getenv("LV") == NIL)
				*env_addon = "LV=-c";
		}
	}

	NYD2_OU;
	return rv;
}

FL struct n_string *
mx_version(struct n_string *s){
	NYD2_IN;

	s = n_string_push_cp(s, n_uagent);
	s = n_string_push_c(s, ' ');
	s = n_string_push_cp(s, ok_vlook(version));
	s = n_string_push_c(s, ',');
	s = n_string_push_c(s, ' ');
	s = n_string_push_cp(s, ok_vlook(version_date));
	s = n_string_push_c(s, ' ');
	s = n_string_push_c(s, '(');
	s = n_string_push_cp(s, _("built for "));
	s = n_string_push_cp(s, ok_vlook(build_os));
	s = n_string_push_c(s, ')');
	s = n_string_push_c(s, '\n');

	NYD2_OU;
	return s;
}

FL uz
n_screensize(void){
	char const *cp;
	uz rv;
	NYD2_IN;

	rv = 0;
	if((cp = ok_vlook(screen)) != NIL)
		su_idec_uz_cp(&rv, cp, 0, NIL);

	if(rv == 0)
		rv = mx_termios_dimen.tiosd_height;

	if(rv > 2)
		rv -= 2; /* XXX i have forgotten why this (prompt line + keep command line? */

	NYD2_OU;
	return rv;
}

FL FILE *
mx_pager_open(void){
	char const *env_add[2], *pager;
	FILE *rv;
	NYD2_IN;
	ASSERT(n_psonce & n_PSO_INTERACTIVE);

	pager = a_aux_pager_get(env_add + 0);
	env_add[1] = NIL;

	if((rv = mx_fs_pipe_open(pager, mx_FS_PIPE_WRITE_CHILD_PASS, NIL, env_add, -1)) == NIL)
		n_perr(pager, 0);

	NYD2_OU;
	return rv;
}

FL boole
mx_pager_close(FILE *fp){
	boole rv;
	NYD2_IN;

	rv = mx_fs_pipe_close(fp, TRU1);

	NYD2_OU;
	return rv;
}

FL void
page_or_print(FILE *fp, uz lines){
	int c;
	char const *cp;
	NYD_IN;

	fflush_rewind(fp);

	if(mx_go_may_yield_control() && (cp = ok_vlook(crt)) != NIL){
		uz rows;

		if(*cp == '\0')
			rows = mx_termios_dimen.tiosd_height;
		else
			su_idec_uz_cp(&rows, cp, 0, NIL);
		/* Avoid overflow later on */
		if(rows == UZ_MAX)
			--rows;

		if(rows > 0 && lines == 0){
			while((c = getc(fp)) != EOF)
				if(c == '\n' && ++lines >= rows)
					break;
			really_rewind(fp, c);
		}

		/* Take account for the follow-up prompt */
		if(lines + 1 >= rows)
			goto jpager;
	}

	while((c = getc(fp)) != EOF)
		putc(c, n_stdout);

jleave:
	NYD_OU;
	return;

jpager:/* C99 */{
	struct mx_child_ctx cc;
	char const *env_addon[2];

	mx_child_ctx_setup(&cc);
	cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
	cc.cc_fds[mx_CHILD_FD_IN] = fileno(fp);
	cc.cc_cmd = a_aux_pager_get(&env_addon[0]);
	env_addon[1] = NIL;
	cc.cc_env_addon = env_addon;
	mx_child_run(&cc);
	}goto jleave;
}

FL char *
n_c_to_hex_base16(char store[3], char c){
	static char const itoa16[] = "0123456789ABCDEF";
	NYD2_IN;

	store[2] = '\0';
	store[1] = itoa16[S(u8,c) & 0x0F];
	c = (S(u8,c) >> 4) & 0x0F;
	store[0] = itoa16[S(u8,c)];

	NYD2_OU;
	return store;
}

FL s32
n_c_from_hex_base16(char const hex[2]){
	static u8 const atoi16[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* 0x30-0x37 */
		0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x38-0x3F */
		0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, /* 0x40-0x47 */
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x48-0x4f */
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x50-0x57 */
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x58-0x5f */
		0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF	/* 0x60-0x67 */
	};
	u8 i1, i2;
	s32 rv;
	NYD2_IN;

	if((i1 = S(u8,hex[0]) - '0') >= NELEM(atoi16) || (i2 = S(u8,hex[1]) - '0') >= NELEM(atoi16))
		goto jerr;
	i1 = atoi16[i1];
	i2 = atoi16[i2];
	if((i1 | i2) & 0xF0u)
		goto jerr;
	rv = i1;
	rv <<= 4;
	rv += i2;

jleave:
	NYD2_OU;
	return rv;
jerr:
	rv = -1;
	goto jleave;
}

FL char const *
n_getdeadletter(void){
	char const *cp;
	boole bla;
	NYD_IN;

	bla = FAL0;
jredo:
	cp = mx_fexpand(ok_vlook(DEAD), (mx_FEXP_DEF_LOCAL_FILE_VAR));
	if(cp == NIL || su_cs_len(cp) >= PATH_MAX){
		if(!bla){
			n_err(_("Failed to expand *DEAD*, setting default (%s): %s\n"),
				VAL_DEAD, n_shexp_quote_cp((cp == NIL ? su_empty : cp), FAL0));
			ok_vclear(DEAD);
			bla = TRU1;
			goto jredo;
		}else{
			cp = savecatsep(ok_vlook(TMPDIR), '/', VAL_DEAD_BASENAME);
			n_err(_("Cannot expand *DEAD*, using: %s\n"), cp);
		}
	}

	NYD_OU;
	return cp;
}

#ifdef mx_HAVE_IDNA
FL boole
n_idna_to_ascii(struct n_string *out, char const *ibuf, uz ilen){
	char *idna_utf8;
	boole lofi, rv;
	NYD_IN;

	if(ilen == UZ_MAX)
		ilen = su_cs_len(ibuf);

	lofi = FAL0;

	if((rv = (ilen == 0)))
		goto jleave;
	if(ibuf[ilen] != '\0'){
		lofi = TRU1;
		idna_utf8 = su_LOFI_ALLOC(ilen +1);
		su_mem_copy(idna_utf8, ibuf, ilen);
		idna_utf8[ilen] = '\0';
		ibuf = idna_utf8;
	}
	ilen = 0;

# ifndef mx_HAVE_ALWAYS_UNICODE_LOCALE
	if(n_psonce & n_PSO_UNICODE)
# endif
		idna_utf8 = UNCONST(char*,ibuf);
# ifndef mx_HAVE_ALWAYS_UNICODE_LOCALE
	else if((idna_utf8 = n_iconv_onetime_cp(n_ICONV_NONE, "utf-8", ok_vlook(ttycharset), ibuf)) == NIL)
		goto jleave;
# endif

# if mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN2
	/* C99 */{
		char *idna_ascii;
		int f, rc;

		f = IDN2_NONTRANSITIONAL;
jidn2_redo:
		if((rc = idn2_to_ascii_8z(idna_utf8, &idna_ascii, f)) == IDN2_OK){
			out = n_string_assign_cp(out, idna_ascii);
			idn2_free(idna_ascii);
			rv = TRU1;
			ilen = out->s_len;
		}else if(rc == IDN2_DISALLOWED && f != IDN2_TRANSITIONAL){
			f = IDN2_TRANSITIONAL;
			goto jidn2_redo;
		}
	}

# elif mx_HAVE_IDNA == n_IDNA_IMPL_LIBIDN
	/* C99 */{
		char *idna_ascii;

		if(idna_to_ascii_8z(idna_utf8, &idna_ascii, 0) == IDNA_SUCCESS){
			out = n_string_assign_cp(out, idna_ascii);
			idn_free(idna_ascii);
			rv = TRU1;
			ilen = out->s_len;
		}
	}

# elif mx_HAVE_IDNA == n_IDNA_IMPL_IDNKIT
	ilen = su_cs_len(idna_utf8);
jredo:
	switch(idn_encodename(
		/* LOCALCONV changed meaning in v2 and is no longer available for encoding.  This makes sense, bu */
			(
#  ifdef IDN_UNICODECONV /* v2 */
			IDN_ENCODE_APP & ~IDN_UNICODECONV
#  else
			IDN_DELIMMAP | IDN_LOCALMAP | IDN_NAMEPREP | IDN_IDNCONV |
			IDN_LENCHECK | IDN_ASCCHECK
#  endif
			), idna_utf8,
			n_string_resize(n_string_trunc(out, 0), ilen)->s_dat, ilen)){
	case idn_buffer_overflow:
		ilen += HOST_NAME_MAX +1;
		goto jredo;
	case idn_success:
		rv = TRU1;
		ilen = su_cs_len(out->s_dat);
		break;
	default:
		ilen = 0;
		break;
	}

# else
#  error Unknown mx_HAVE_IDNA
# endif

jleave:
	if(lofi)
		su_LOFI_FREE(UNCONST(char*,ibuf));
	out = n_string_trunc(out, ilen);

	NYD_OU;
	return rv;
}
#endif /* mx_HAVE_IDNA */

FL boole
n_boolify(char const *inbuf, uz inlen, boole emptyrv){
	boole rv;
	NYD2_IN;
	ASSERT(inlen == 0 || inbuf != NIL);

	if(inlen == UZ_MAX)
		inlen = su_cs_len(inbuf);

	if(inlen == 0)
		rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
	else{
		if((inlen == 1 && (*inbuf == '1' || *inbuf == 'y' || *inbuf == 'Y')) ||
				(inlen == 4 && !su_cs_cmp_case_n(inbuf, "true", inlen)) ||
				(inlen == 3 && !su_cs_cmp_case_n(inbuf, "yes", inlen)) ||
				(inlen == 2 && !su_cs_cmp_case_n(inbuf, "on", inlen)))
			rv = TRU1;
		else if((inlen == 1 && (*inbuf == '0' || *inbuf == 'n' || *inbuf == 'N')) ||
				(inlen == 5 && !su_cs_cmp_case_n(inbuf, "false", inlen)) ||
				(inlen == 2 && !su_cs_cmp_case_n(inbuf, "no", inlen)) ||
				(inlen == 3 && !su_cs_cmp_case_n(inbuf, "off", inlen)))
			rv = FAL0;
		else{
			u64 ib;

			if((su_idec(&ib, inbuf, inlen, 0, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
					) != su_IDEC_STATE_CONSUMED)
				rv = TRUM1;
			else
				rv = (ib != 0);
		}
	}

	NYD2_OU;
	return rv;
}

FL boole
n_quadify(char const *inbuf, uz inlen, char const *prompt, boole emptyrv){
	boole rv;
	NYD2_IN;
	ASSERT(inlen == 0 || inbuf != NIL);

	if(inlen == UZ_MAX)
		inlen = su_cs_len(inbuf);

	if(inlen == 0)
		rv = (emptyrv >= FAL0) ? (emptyrv == FAL0 ? FAL0 : TRU1) : TRU2;
	else if((rv = n_boolify(inbuf, inlen, emptyrv)) < FAL0 && !su_cs_cmp_case_n(inbuf, "ask-", 4) &&
			(rv = n_boolify(&inbuf[4], inlen - 4, emptyrv)) >= FAL0 &&
			(n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT))
		rv = mx_tty_yesorno(prompt, rv);

	NYD2_OU;
	return rv;
}

FL boole
n_is_all_or_aster(char const *name){
	boole rv;
	NYD2_IN;

	rv = ((name[0] == '*' && name[1] == '\0') || !su_cs_cmp_case(name, "all"));

	NYD2_OU;
	return rv;
}

FL char *
n_filename_to_repro(char const *name){
	char *rv;
	char const *proto, *cp;
	NYD2_IN;

	if((cp = su_cs_find(name, "://")) != NIL){
		cp += 3;
		proto = savestrbuf(name, P2UZ(cp - name));
		name = cp;
	}else
		proto = NIL;

	if(su_cs_cmp(name, su_path_null)) /* xxx ..after canonicalization.. */
		name = su_path_basename(savestr(name));

	rv = (proto != NIL) ? savecat(proto, name) : savestr(name);

	NYD2_OU;
	return rv;
}

FL void
n_err(char const *format, ...){
	va_list ap;
	NYD2_IN;

	va_start(ap, format);
	n_verrx(FAL0, format, &ap);
	va_end(ap);

	NYD2_OU;
}

FL void
n_errx(boole allow_multiple, char const *format, ...){
	va_list ap;
	NYD2_IN;

	va_start(ap, format);
	n_verrx(allow_multiple, format, &ap);
	va_end(ap);

	NYD2_OU;
}

FL void
n_verr(char const *format, void *vlp){
	NYD2_IN;

	n_verrx(FAL0, format, vlp);

	NYD2_OU;
}

FL void
n_verrx(boole allow_multiple, char const *format, void *vlp){/*XXX sigcondom TODO MONSTER! */
	/* Unhappy: too complicated, too slow; should possibly print repitition
	 * count more often, but be aware of n_PS_ERRORS_NEED_PRINT_ONCE docu */
	mx_COLOUR( static uz c5recur; ) /* *termcap* recursion */
#ifdef mx_HAVE_ERRORS
	u32 errlim;
#endif
	struct str s_b, s;
	struct a_aux_err_node *lenp, *enp;
	sz i;
	boole dolog, dosave;
	char const *lpref, *c5pref, *c5suff;
	va_list *vap;
	NYD2_IN;

	vap = S(va_list*,vlp);
	mx_COLOUR( ++c5recur; )
	lpref = NIL;
	c5pref = c5suff = su_empty;

	dosave = ((n_poption & n_PO_D_V) || !(n_pstate & n_PS_ROBOT) || mx_go_may_yield_control());
	/* Test output wants error messages */
	/*dolog = (dosave || su_state_has(su_STATE_REPRODUCIBLE) ||
			!(n_psonce & n_PSO_STARTED_CONFIG))*/;
	dolog = TRU1;

	/* Fully expand the buffer (TODO use fmtenc) */
#undef a_X
#ifdef mx_HAVE_N_VA_COPY
# define a_X 128
#else
# define a_X MIN(mx_LINESIZE, 1024)
#endif
	mx_fs_linepool_aquire(&s_b.s, &s_b.l);
	i = 0;
	if(s_b.l < a_X)
		i = s_b.l = a_X;
#undef a_X

	for(;; s_b.l = ++i /* xxx could wrap, maybe */){
#ifdef mx_HAVE_N_VA_COPY
		va_list vac;

		n_va_copy(vac, *vap);
#else
# define vac *vap
#endif

		if(i != 0)
			s_b.s = su_MEM_REALLOC(s_b.s, s_b.l);

		i = vsnprintf(s_b.s, s_b.l, format, vac);

#ifdef mx_HAVE_N_VA_COPY
		va_end(vac);
#else
# undef vac
#endif

		if(i <= 0)
			goto jleave;
		if(UCMP(z, i, >=, s_b.l)){
#ifdef mx_HAVE_N_VA_COPY
			continue;
#else
			i = S(int,su_cs_len(s_b.s));
#endif
		}
		break;
	}
	s = s_b;
	s.l = S(uz,i);

	/* Remove control characters but \n as we do not makeprint() XXX config */
	/* C99 */{
		char *ins, *curr, *max, c;

		for(ins = curr = s.s, max = &ins[s.l]; curr < max; ++curr)
			if(!su_cs_is_cntrl(c = *curr) || c == '\n')
				*ins++ = c;
		*ins = '\0';
		s.l = P2UZ(ins - s.s);
	}

	/* Have the prepared error message, take it over line-by-line, possibly completing partly prepared one first */
	if(n_pstate & n_PS_ERRORS_NEED_PRINT_ONCE){
		n_pstate ^= n_PS_ERRORS_NEED_PRINT_ONCE;
		allow_multiple = TRU1;
	}

	/* C99 */{
		u32 poption_save;

		poption_save = n_poption; /* XXX sigh */
		n_poption &= ~n_PO_D_V;

		lpref = ok_vlook(log_prefix);

#ifdef mx_HAVE_ERRORS
		su_idec_u32_cp(&errlim, ok_vlook(errors_limit), 0, NIL);
#endif

#ifdef mx_HAVE_COLOUR
		if(c5recur == 1 && (n_psonce & n_PSO_TTYANY)){
			struct str const *pref, *suff;
			struct mx_colour_pen *cp;

			if((cp = mx_colour_get_pen(mx_COLOUR_GET_FORCED, mx_COLOUR_CTX_MLE, mx_COLOUR_ID_MLE_ERROR, NIL)
						) != NIL && (pref = mx_colour_pen_get_cseq(cp)) != NIL &&
					(suff = mx_colour_get_reset_cseq(mx_COLOUR_GET_FORCED)) != NIL){
				c5pref = pref->s;
				c5suff = suff->s;
			}
		}
#endif

		n_poption = poption_save;
	}

	for(i = 0; UCMP(z, i, <, s.l);){
		char *cp;
		boole isdup;

		lenp = enp = a_aux_err_tail;
		if(enp == NIL || enp->ae_done){
			enp = su_TCALLOC(struct a_aux_err_node, 1);
			enp->ae_cnt = 1;
			n_string_creat(&enp->ae_str);

			if(lenp != NIL)
				lenp->ae_next = enp;
			else
				a_aux_err_head = enp;
			a_aux_err_tail = enp;
		}

		/* xxx if(!n_string_book(&enp->ae_str, s.l - i))
		 * xxx	 goto jleave;*/

		/* We have completed a line? */
		/* C99 */{
			uz oi, j, k;

			oi = S(uz,i);
			j = s.l - oi;
			k = enp->ae_str.s_len;
			cp = S(char*,su_mem_find(&s.s[oi], '\n', j));

			if(cp == NIL){
				n_string_push_buf(&enp->ae_str, &s.s[oi], j);
				i = s.l;
			}else{
				j = P2UZ(cp - &s.s[oi]);
				i += j + 1;
				n_string_push_buf(&enp->ae_str, &s.s[oi], j);
			}

			/* We need to write it out regardless of whether it is a complete line
			 * or not, say (for at least `echoerrn') TODO IO errors not handled */
			if((cp == NIL || allow_multiple || !(n_psonce & n_PSO_INTERACTIVE)) && dolog){
				fprintf(n_stderr, "%s%s%s%s%s", c5pref, (enp->ae_dumped_till == 0 ? lpref : su_empty),
					&n_string_cp(&enp->ae_str)[k], c5suff, (cp != NIL ? "\n" : su_empty));
				fflush(n_stderr);
				enp->ae_dumped_till = enp->ae_str.s_len;
			}
		}

		if(cp == NIL)
			continue;
		enp->ae_done = TRU1;

		/* Check whether it is identical to the last one dumped, in which case we throw it away and only
		 * increment the counter, as syslog would.  If not, dump it out, if not already */
		isdup = FAL0;
		if(lenp != NIL){
			if(lenp != enp && lenp->ae_str.s_len == enp->ae_str.s_len &&
					!su_mem_cmp(lenp->ae_str.s_dat, enp->ae_str.s_dat, enp->ae_str.s_len)){
				++lenp->ae_cnt;
				isdup = TRU1;
			}
			/* Otherwise, if last error has a count, say so, unless it would soil and intermix display */
			else if(lenp->ae_cnt > 1 && !allow_multiple && (n_psonce & n_PSO_INTERACTIVE) && dolog){
				fprintf(n_stderr, _("%s%s-- Last message repeated %u times --%s\n"),
					c5pref, lpref, lenp->ae_cnt, c5suff);
				fflush(n_stderr);
			}
		}

		/* When we come here we need to write at least the/a \n! */
		if(!isdup && !allow_multiple && (n_psonce & n_PSO_INTERACTIVE) && dolog){
			fprintf(n_stderr, "%s%s%s%s\n", c5pref, (enp->ae_dumped_till == 0 ? lpref : su_empty),
				&n_string_cp(&enp->ae_str)[enp->ae_dumped_till], c5suff);
			fflush(n_stderr);
		}

		if(isdup || !dosave || !(n_psonce & n_PSO_INTERACTIVE)){
			if(a_aux_err_head == enp){
				ASSERT(a_aux_err_tail == a_aux_err_head);
				ASSERT(lenp == NIL || lenp == a_aux_err_tail);
				ASSERT(enp->ae_next == NIL);
				a_aux_err_head = NIL;
				if(enp == lenp)
					lenp = NIL;
			}else{
				ASSERT(lenp != NIL);
				ASSERT(lenp != enp);
				ASSERT(lenp->ae_next == a_aux_err_tail);
				ASSERT(a_aux_err_tail == enp);
				lenp->ae_next = NIL;
			}
			a_aux_err_tail = lenp;

			n_string_gut(&enp->ae_str);
			su_FREE(enp);
			continue;
		}

#ifdef mx_HAVE_ERRORS
		if(n_pstate_err_cnt < errlim){
			++n_pstate_err_cnt;
			continue;
		}

		ASSERT(a_aux_err_head != NIL);
		lenp = a_aux_err_head;
		if((a_aux_err_head = lenp->ae_next) == NIL)
			a_aux_err_tail = NIL;
#else
		a_aux_err_head = a_aux_err_tail = enp;
#endif
		if(lenp != NIL){
			n_string_gut(&lenp->ae_str);
			su_FREE(lenp);
		}
	}

jleave:
	mx_fs_linepool_release(s_b.s, s_b.l);
	mx_COLOUR( --c5recur; )

	NYD2_OU;
}

FL void
n_su_log_write_fun(u32 lvl_a_flags, char const *msg, uz len){
	UNUSED(len);
	if(!(lvl_a_flags & su_LOG_F_CORE))
		n_err(msg, NIL);
	else
		fprintf(stderr, "%s%s", ok_vlook(log_prefix), msg);
}

FL void
n_err_sighdl(char const *format, ...){ /* TODO sigsafe; obsolete! */
	va_list ap;
	NYD;

	va_start(ap, format);
	vfprintf(n_stderr, format, ap);
	va_end(ap);
	fflush(n_stderr);
}

FL void
n_perr(char const *msg, int errval){
	int e;
	char const *fmt;
	NYD2_IN;

	if(msg == NIL){
		fmt = "%s%s\n";
		msg = su_empty;
	}else
		fmt = "%s: %s\n";

	e = (errval == 0) ? su_err() : errval;
	n_errx(FAL0, fmt, msg, su_err_doc(e));
	if(errval == 0)
		su_err_set(e);

	NYD2_OU;
}

FL void
n_alert(char const *format, ...){
	va_list ap;
	NYD2_IN;

	n_err((a_aux_err_tail != NIL && !a_aux_err_tail->ae_done) ? _("\nAlert: ") : _("Alert: "));

	va_start(ap, format);
	n_verrx(TRU1, format, &ap);
	va_end(ap);

	n_errx(TRU1, "\n");

	NYD2_OU;
}

FL void
n_panic(char const *format, ...){
	va_list ap;

	DVL( su_nyd_set_disabled(TRU1); )

	if(a_aux_err_tail != NIL && !a_aux_err_tail->ae_done){
		a_aux_err_tail->ae_done = TRU1;
		putc('\n', n_stderr);
	}
	fprintf(n_stderr, "%sPanic: ", ok_vlook(log_prefix));

	va_start(ap, format);
	vfprintf(n_stderr, format, ap);
	va_end(ap);

	putc('\n', n_stderr);
	fflush(n_stderr);

	abort(); /* Was exit(n_EXIT_ERR); for a while, but no */
}

#ifdef mx_HAVE_ERRORS
FL int
c_errors(void *v){
	char **argv = v;
	struct a_aux_err_node *enp;
	NYD_IN;

	if(*argv == NIL)
		goto jlist;
	if(argv[1] != NIL)
		goto jerr;
	if(!su_cs_cmp_case(*argv, "show"))
		goto jlist;
	if(!su_cs_cmp_case(*argv, "clear"))
		goto jclear;
jerr:
	mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("errors"), NIL);
	v = NIL;
jleave:
	NYD_OU;
	return (v == NIL) ? !STOP : !OKAY; /* xxx 1:bad 0:good -- do some */

jlist:{
		FILE *fp;
		uz i;

		if(a_aux_err_head == NIL){
			fprintf(n_stderr, _("The error ring is empty\n"));
			goto jleave;
		}

		if((fp = mx_fs_tmp_open(NIL, "errors", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)) == NIL)
			fp = n_stdout;

		for(i = 0, enp = a_aux_err_head; enp != NIL; enp = enp->ae_next)
			fprintf(fp, "%4" PRIuZ "/%-3u %s\n", ++i, enp->ae_cnt, n_string_cp(&enp->ae_str));

		if(fp != n_stdout){
			page_or_print(fp, 0);

			mx_fs_close(fp);
		}else
			clearerr(fp);
	}
	/* FALLTHRU */

jclear:
	a_aux_err_tail = NIL;
	n_pstate_err_cnt = 0;
	while((enp = a_aux_err_head) != NIL){
		a_aux_err_head = enp->ae_next;
		n_string_gut(&enp->ae_str);
		su_FREE(enp);
	}
	goto jleave;
}
#endif /* mx_HAVE_ERRORS */

FL boole
mx_unxy_dict(char const *cmdname, struct su_cs_dict *dp, void *vp){
	struct mx_cmd_arg *cap;
	struct mx_cmd_arg_ctx *cacp;
	boole rv;
	NYD_IN;

	rv = TRU1;
	cacp = vp;

	for(cap = cacp->cac_arg; cap != NIL; cap = cap->ca_next){
		char const *key;

		key = cap->ca_arg.ca_str.s;
		if(key[1] == '\0' && key[0] == '*'){
			if(dp != NIL)
				su_cs_dict_clear(dp);
		}else if(dp == NIL || !su_cs_dict_remove(dp, key)){
			n_err(_("No such `%s': %s\n"), cmdname, n_shexp_quote_cp(key, FAL0));
			rv = FAL0;
		}
	}

	NYD_OU;
	return rv;
}

FL boole
mx_xy_dump_dict(char const *cmdname, struct su_cs_dict *dp, struct n_strlist **result, struct n_strlist **tailpp_or_nil,
		struct n_strlist *(*ptf)(char const *cmdname, char const *key, void const *dat)){
	struct su_cs_dict_view dv;
	char const **cpp, **xcpp;
	u32 cnt;
	struct n_strlist *resp, *tailp;
	boole rv;
	NYD_IN;

	rv = TRU1;

	resp = *result;
	if(tailpp_or_nil != NIL)
		tailp = *tailpp_or_nil;
	else if((tailp = resp) != NIL)
		for(;; tailp = tailp->sl_next)
			if(tailp->sl_next == NIL)
				break;

	if(dp == NIL || (cnt = su_cs_dict_count(dp)) == 0)
		goto jleave;

	if(n_poption & n_PO_D_V)
		su_cs_dict_statistics(dp);

	/* TODO we need LOFI/AUTOREC TALLOC version which check overflow!!
	 * TODO these then could _really_ return NIL... */
	if(U32_MAX / sizeof(*cpp) <= cnt || (cpp = S(char const**,su_AUTO_ALLOC(sizeof(*cpp) * cnt))) == NIL)
		goto jleave;

	xcpp = cpp;
	su_CS_DICT_FOREACH(dp, &dv)
		*xcpp++ = su_cs_dict_view_key(&dv);
	if(cnt > 1)
		/* OK even for case-insensitive keys: cs_dict will store keys in lowercase-normalized versions, then */
		su_sort_shell_vpp(su_S(void const**,cpp), cnt, su_cs_toolbox.tb_cmp);

	for(xcpp = cpp; cnt > 0; ++xcpp, --cnt){
		struct n_strlist *slp;

		if((slp = (*ptf)(cmdname, *xcpp, su_cs_dict_lookup(dp, *xcpp))) == NIL)
			continue;
		if(resp == NIL)
			resp = slp;
		else
			tailp->sl_next = slp;
		tailp = slp;
	}

jleave:
	*result = resp;
	if(tailpp_or_nil != NIL)
		*tailpp_or_nil = tailp;

	NYD_OU;
	return rv;
}

FL struct n_strlist *
mx_xy_dump_dict_gen_ptf(char const *cmdname, char const *key, void const *dat){
	/* XXX real strlist + str_to_fmt() */
	char *cp;
	struct n_strlist *slp;
	uz kl, dl, cl;
	char const *kp, *dp;
	NYD2_IN;

	kp = n_shexp_quote_cp(key, TRU1);
	dp = n_shexp_quote_cp(su_S(char const*,dat), TRU1);
	kl = su_cs_len(kp);
	dl = su_cs_len(dp);
	cl = su_cs_len(cmdname);

	slp = n_STRLIST_AUTO_ALLOC(cl + 1 + kl + 1 + dl +1);
	slp->sl_next = NIL;
	cp = slp->sl_dat;
	su_mem_copy(cp, cmdname, cl);
	cp += cl;
	*cp++ = ' ';
	su_mem_copy(cp, kp, kl);
	cp += kl;
	*cp++ = ' ';
	su_mem_copy(cp, dp, dl);
	cp += dl;
	*cp = '\0';
	slp->sl_len = P2UZ(cp - slp->sl_dat);

	NYD2_OU;
	return slp;
}

FL boole
mx_page_or_print_strlist(char const *cmdname, struct n_strlist *slp, boole cnt_lines){
	uz lines;
	FILE *fp;
	boole rv;
	NYD_IN;

	rv = TRU1;

	if((fp = mx_fs_tmp_open(NIL, cmdname, (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)) == NIL)
		fp = n_stdout;

	/* Create visual result */
	for(lines = 0; slp != NIL; slp = slp->sl_next){
		if(fputs(slp->sl_dat, fp) == EOF){
			rv = FAL0;
			break;
		}

		if(!cnt_lines){
jputnl:
			if(putc('\n', fp) == EOF){
				rv = FAL0;
				break;
			}
			++lines;
		}else{
			char *cp;
			boole lastnl;

			for(lastnl = FAL0, cp = slp->sl_dat; *cp != '\0'; ++cp)
				if((lastnl = (*cp == '\n')))
					++lines;
			if(!lastnl)
				goto jputnl;
		}
	}

	if(rv && lines == 0){
		if(fprintf(fp, _("# `%s': no data available\n"), cmdname) < 0)
			rv = FAL0;
		else
			lines = 1;
	}

	if(fp != n_stdout){
		page_or_print(fp, lines);

		mx_fs_close(fp);
	}else
		clearerr(fp);

	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_AUXLILY
/* s-itt-mode */
