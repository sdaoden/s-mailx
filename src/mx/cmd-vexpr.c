/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-vexpr.h.
 *@ TODO - better commandline parser that can dive into subcommands could get rid of a lot of ERR_SYNOPSIS cruft.
 *@ TODO - _VEXPR -> _CVEXPR
 *
 * Copyright (c) 2017 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_vexpr
#define mx_SOURCE
#define mx_SOURCE_CMD_VEXPR

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_CMD_VEXPR

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/time.h>

#ifdef mx_HAVE_REGEX
# include <su/re.h>
#endif

#include "mx/cmd.h"
#include "mx/random.h"
#include "mx/time.h"
#include "mx/ui-str.h"

#include "mx/cmd-vexpr.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_vexpr_cmd{
	a_VEXPR_CMD_NUM__MIN,
	a_VEXPR_CMD_NUM_EQUAL = a_VEXPR_CMD_NUM__MIN,
	a_VEXPR_CMD_NUM_NOT,
	a_VEXPR_CMD_NUM_PLUS,
	a_VEXPR_CMD_NUM_MINUS,
	a_VEXPR_CMD_NUM_MUL,
	a_VEXPR_CMD_NUM_DIV,
	a_VEXPR_CMD_NUM_MOD,
	a_VEXPR_CMD_NUM_OR,
	a_VEXPR_CMD_NUM_AND,
	a_VEXPR_CMD_NUM_XOR,
	a_VEXPR_CMD_NUM_LSHIFT,
	a_VEXPR_CMD_NUM_RSHIFT,
	a_VEXPR_CMD_NUM_URSHIFT,
	a_VEXPR_CMD_NUM_PBASE,
	a_VEXPR_CMD_NUM__MAX,

	a_VEXPR_CMD_AGN__MIN = a_VEXPR_CMD_NUM__MAX,
	a_VEXPR_CMD_AGN_DATE_UTC = a_VEXPR_CMD_AGN__MIN,
	a_VEXPR_CMD_AGN_DATE_STAMP_UTC,
	a_VEXPR_CMD_AGN_EPOCH,
	a_VEXPR_CMD_AGN_SECONDS,
	a_VEXPR_CMD_AGN_RANDOM,
	a_VEXPR_CMD_AGN__MAX,

	a_VEXPR_CMD_STR__MIN = a_VEXPR_CMD_AGN__MAX,
	a_VEXPR_CMD_STR_MAKEPRINT = a_VEXPR_CMD_STR__MIN,
#ifdef mx_HAVE_REGEX
	a_VEXPR_CMD_STR_REGEX,
	a_VEXPR_CMD_STR_IREGEX, /* v15compat */
#endif
	a_VEXPR_CMD_STR__MAX,

	a_VEXPR_CMD__MAX
};

enum a_vexpr_err{
	a_VEXPR_ERR_NONE,
	a_VEXPR_ERR_SYNOPSIS,
	a_VEXPR_ERR_SUBCMD,
	a_VEXPR_ERR_MOD_NOT_ALLOWED,
	a_VEXPR_ERR_MOD_NOT_SUPPORTED,
	a_VEXPR_ERR_NUM_RANGE,
	a_VEXPR_ERR_NUM_OVERFLOW,
	a_VEXPR_ERR_STR_NUM_RANGE,
	a_VEXPR_ERR_STR_OVERFLOW,
	a_VEXPR_ERR_STR_NODATA,
	a_VEXPR_ERR_STR_GENERIC
};
enum {a_VEXPR_ERR__MAX = a_VEXPR_ERR_STR_GENERIC};

CTA(S(uz,a_VEXPR_CMD__MAX | a_VEXPR_ERR__MAX) <= 0x7Fu, "Bit range excess");

enum a_vexpr_flags{
	a_VEXPR_NONE,
	a_VEXPR_ERR = 1u<<0, /* There was an error */
	a_VEXPR_MOD_SATURATED = 1u<<1, /* Saturated / case-insensitive / XY */
	a_VEXPR_MOD_CASE = 1u<<2, /* Saturated / case-insensitive / XY */
	a_VEXPR_MOD_MASK = a_VEXPR_MOD_SATURATED | a_VEXPR_MOD_CASE,
	a_VEXPR_ISNUM = 1u<<3,
	a_VEXPR_ISDECIMAL = 1u<<4, /* Print only decimal result */
	a_VEXPR_UNSIGNED_OP = 1u<<5, /* Force unsigned interpretation */
	a_VEXPR_SOFTOVERFLOW = 1u<<6,
	a_VEXPR_PBASE = 1u<<7, /* Print additional number base */
	a_VEXPR_PBASE_FORCE_UNSIGNED = 1u<<8, /* We saw an u prefix for it */

	a_VEXPR__FMASK = 0x1FFu,
	a_VEXPR__FSHIFT = 9u,
	a_VEXPR__FCMDMASK = 0xFE00u,
	a_VEXPR__TMP = 1u<<30
};
/* .vc_cmderr=8-bit, and so a_vexpr_subcmd can store CMD+MOD flags in 16-bit */
CTA(((S(u32,a_VEXPR_CMD__MAX | a_VEXPR_ERR__MAX) << a_VEXPR__FSHIFT) & ~a_VEXPR__FCMDMASK) == 0, "Bit ranges overlap");

struct a_vexpr_ctx{
	u32 vc_flags;
	u8 vc_cmderr; /* On input, a_vexpr_cmd, on output (maybe) a_vexpr_err */
	u8 vc_pbase;
	u8 vc__pad[2];
	char const *vc_cmd_name;
	char const *vc_varres;
	char const *vc_arg; /* The current arg (_ERR: which caused failure) */
	s64 vc_lhv;
	s64 vc_rhv;
	char const *vc_argv[7];
	char vc_iencbuf[2+1/* BASE# prefix*/ + su_IENC_BUFFER_SIZE + 1];
};

struct a_vexpr_subcmd{
	u16 vs_mpv;
	char vs_name[14];
};

static struct a_vexpr_subcmd const a_vexpr_subcmds[] = {
#undef a_X
#define a_X(C,F) (S(u16,C) << a_VEXPR__FSHIFT) | F

	{a_X(a_VEXPR_CMD_NUM_EQUAL, a_VEXPR_MOD_SATURATED), "="},
	{a_X(a_VEXPR_CMD_NUM_NOT, a_VEXPR_MOD_SATURATED), "~"},
	{a_X(a_VEXPR_CMD_NUM_PLUS, a_VEXPR_MOD_SATURATED), "+"},
	{a_X(a_VEXPR_CMD_NUM_MINUS, a_VEXPR_MOD_SATURATED), "-"},
	{a_X(a_VEXPR_CMD_NUM_MUL, a_VEXPR_MOD_SATURATED), "*"},
	{a_X(a_VEXPR_CMD_NUM_DIV, a_VEXPR_MOD_SATURATED), "/"},
	{a_X(a_VEXPR_CMD_NUM_MOD, a_VEXPR_MOD_SATURATED), "%"},
	{a_X(a_VEXPR_CMD_NUM_OR, a_VEXPR_MOD_SATURATED), "|"},
	{a_X(a_VEXPR_CMD_NUM_AND, a_VEXPR_MOD_SATURATED), "&"},
	{a_X(a_VEXPR_CMD_NUM_XOR, a_VEXPR_MOD_SATURATED), "^"},
	{a_X(a_VEXPR_CMD_NUM_LSHIFT, a_VEXPR_MOD_SATURATED), "<<"},
	{a_X(a_VEXPR_CMD_NUM_RSHIFT, a_VEXPR_MOD_SATURATED), ">>"},
	{a_X(a_VEXPR_CMD_NUM_URSHIFT, a_VEXPR_MOD_SATURATED), ">>>"},
	{a_X(a_VEXPR_CMD_NUM_PBASE, a_VEXPR_MOD_SATURATED), "pbase\0"},

	{a_X(a_VEXPR_CMD_AGN_DATE_UTC, 0), "date-utc"},
	{a_X(a_VEXPR_CMD_AGN_DATE_STAMP_UTC, 0), "date-stamp-utc"},
	{a_X(a_VEXPR_CMD_AGN_EPOCH, 0), "epoch"},
		{a_X(a_VEXPR_CMD_AGN_SECONDS, 0), "seconds"},
	{a_X(a_VEXPR_CMD_AGN_RANDOM, 0), "random"},

	{a_X(a_VEXPR_CMD_STR_MAKEPRINT, 0), "makeprint"},
#ifdef mx_HAVE_REGEX
	{a_X(a_VEXPR_CMD_STR_REGEX, a_VEXPR_MOD_CASE), "regex"},
	{a_X(a_VEXPR_CMD_STR_IREGEX, 0), "iregex"}, /* v15compat*/
#endif

#undef a_X
};

/* Entered with .vc_flags=NONE(|MOD)? */
static void a_vexpr_numeric(struct a_vexpr_ctx *vcp);
static void a_vexpr_agnostic(struct a_vexpr_ctx *vcp);
static void a_vexpr_string(struct a_vexpr_ctx *vcp);

#ifdef mx_HAVE_REGEX
static char *a_vexpr__regex_replace(void *uservp);/* v15-compat */
#endif

static void
a_vexpr_numeric(struct a_vexpr_ctx *vcp){
	u8 cmd;
	u32 idecs, idecm;
	s64 lhv, rhv;
	char const *cp;
	u32 f;
	NYD2_IN;

	lhv = rhv = 0;
	f = vcp->vc_flags;
	f |= a_VEXPR_ISNUM;

	if((cp = vcp->vc_argv[0]) == NIL){
		f |= a_VEXPR_ERR;
		vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
		goto jleave;
	}

jlhv_redo:
	if(*cp == '\0')
		lhv = 0;
	else{
		vcp->vc_arg = cp;
		idecm = (((*cp == 'u' || *cp == 'U') ? (f |= a_VEXPR_PBASE_FORCE_UNSIGNED, ++cp, su_IDEC_MODE_NONE)
					: ((*cp == 's' || *cp == 'S') ? (++cp, su_IDEC_MODE_SIGNED_TYPE)
						: su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_POW2BASE_UNSIGNED)) |
				su_IDEC_MODE_BASE0_NUMSIG_RESCAN);
		if(((idecs = su_idec_cp(&lhv, cp, 0, idecm, NIL)) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
				) != su_IDEC_STATE_CONSUMED){
			if(!(idecs & su_IDEC_STATE_EOVERFLOW) || !(f & a_VEXPR_MOD_SATURATED)){
				f |= a_VEXPR_ERR;
				vcp->vc_cmderr = a_VEXPR_ERR_NUM_RANGE;
			}else
				f |= a_VEXPR_SOFTOVERFLOW;
			goto jleave;
		}
	}

	switch((cmd = S(u8,vcp->vc_cmderr))){ /* break==goto jleave */
	case a_VEXPR_CMD_NUM_NOT:
		lhv = ~lhv;
		/* FALLTHRU */

	default:
	case a_VEXPR_CMD_NUM_EQUAL:
		if(vcp->vc_argv[1] != NIL){
			f |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
		}
		break;

	case a_VEXPR_CMD_NUM_PLUS:
		if(vcp->vc_argv[1] == NIL){
			lhv = +lhv;
			break;
		}
		goto jbinop;
	case a_VEXPR_CMD_NUM_MINUS:
		if(vcp->vc_argv[1] == NIL){
			lhv = -lhv;
			break;
		}
		goto jbinop;

	case a_VEXPR_CMD_NUM_MUL:
	case a_VEXPR_CMD_NUM_DIV:
	case a_VEXPR_CMD_NUM_MOD:
	case a_VEXPR_CMD_NUM_OR:
	case a_VEXPR_CMD_NUM_AND:
	case a_VEXPR_CMD_NUM_XOR:
	case a_VEXPR_CMD_NUM_LSHIFT:
	case a_VEXPR_CMD_NUM_RSHIFT:
	case a_VEXPR_CMD_NUM_URSHIFT:
jbinop:
		if((cp = vcp->vc_argv[1]) == NIL || (vcp->vc_arg = cp, vcp->vc_argv[2] != NIL)){
			f |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
			break;
		}

		if(*cp == '\0')
			rhv = 0;
		else{
			idecm = (((*cp == 'u' || *cp == 'U') ? (++cp, su_IDEC_MODE_NONE)
						: ((*cp == 's' || *cp == 'S') ? (++cp, su_IDEC_MODE_SIGNED_TYPE)
							: su_IDEC_MODE_SIGNED_TYPE | su_IDEC_MODE_POW2BASE_UNSIGNED)) |
					su_IDEC_MODE_BASE0_NUMSIG_RESCAN);
			if(((idecs = su_idec_cp(&rhv, cp, 0, idecm, NIL)) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
					) != su_IDEC_STATE_CONSUMED){
				if(!(idecs & su_IDEC_STATE_EOVERFLOW) || !(f & a_VEXPR_MOD_SATURATED)){
					f |= a_VEXPR_ERR;
					vcp->vc_cmderr = a_VEXPR_ERR_NUM_RANGE;
				}else
					f |= a_VEXPR_SOFTOVERFLOW;
				break;
			}
		}

jbinop_again:
		switch(cmd){
		default:
		case a_VEXPR_CMD_NUM_PLUS:
			if(rhv < 0){
				if(rhv != S64_MIN){
					rhv = -rhv;
					cmd = a_VEXPR_CMD_NUM_MINUS;
					goto jbinop_again;
				}else if(lhv < 0)
					goto jenum_plusminus;
				else if(lhv == 0){
					lhv = rhv;
					break;
				}
			}else if(S64_MAX - rhv < lhv)
				goto jenum_plusminus;
			lhv += rhv;
			break;

		case a_VEXPR_CMD_NUM_MINUS:
			if(rhv < 0){
				if(rhv != S64_MIN){
					rhv = -rhv;
					cmd = a_VEXPR_CMD_NUM_PLUS;
					goto jbinop_again;
				}else if(lhv > 0)
					goto jenum_plusminus;
				else if(lhv == 0){
					lhv = rhv;
					break;
				}
			}else if(S64_MIN + rhv > lhv){
jenum_plusminus:
				if(!(f & a_VEXPR_MOD_SATURATED)){
					f |= a_VEXPR_ERR;
					vcp->vc_cmderr = a_VEXPR_ERR_NUM_OVERFLOW;
					break;
				}
				f |= a_VEXPR_SOFTOVERFLOW;
				lhv = (lhv < 0 || cmd == a_VEXPR_CMD_NUM_MINUS) ? S64_MIN : S64_MAX;
				break;
			}
			lhv -= rhv;
			break;

		case a_VEXPR_CMD_NUM_MUL:
			/* Will the result be positive? */
			if((lhv < 0) == (rhv < 0)){
				if(lhv > 0){
					lhv = -lhv;
					rhv = -rhv;
				}
				if(rhv != 0 && lhv != 0 && S64_MAX / rhv > lhv){
					if(!(f & a_VEXPR_MOD_SATURATED)){
						f |= a_VEXPR_ERR;
						vcp->vc_cmderr = a_VEXPR_ERR_NUM_OVERFLOW;
						break;
					}
					f |= a_VEXPR_SOFTOVERFLOW;
					lhv = S64_MAX;
				}else
					lhv *= rhv;
			}else{
				if(rhv > 0){
					if(lhv != 0 && S64_MIN / lhv < rhv){
						if(!(f & a_VEXPR_MOD_SATURATED)){
							f |= a_VEXPR_ERR;
							vcp->vc_cmderr = a_VEXPR_ERR_NUM_OVERFLOW;
							break;
						}
						f |= a_VEXPR_SOFTOVERFLOW;
						lhv = S64_MIN;
					}else
						lhv *= rhv;
				}else{
					if(rhv != 0 && lhv != 0 && S64_MIN / rhv < lhv){
						if(!(f & a_VEXPR_MOD_SATURATED)){
							f |= a_VEXPR_ERR;
							vcp->vc_cmderr = a_VEXPR_ERR_NUM_OVERFLOW;
						}
						f |= a_VEXPR_SOFTOVERFLOW;
						lhv = S64_MIN;
					}else
						lhv *= rhv;
				}
			}
			break;

		case a_VEXPR_CMD_NUM_DIV:
			if(rhv == 0){
				if(!(f & a_VEXPR_MOD_SATURATED)){
					f |= a_VEXPR_ERR;
					vcp->vc_cmderr = a_VEXPR_ERR_NUM_RANGE;
					break;
				}
				f |= a_VEXPR_SOFTOVERFLOW;
				lhv = S64_MAX;
			}else if(lhv != S64_MIN || rhv != -1)
				lhv /= rhv;
			break;

		case a_VEXPR_CMD_NUM_MOD:
			if(rhv == 0){
				if(!(f & a_VEXPR_MOD_SATURATED)){
					f |= a_VEXPR_ERR;
					vcp->vc_cmderr = a_VEXPR_ERR_NUM_RANGE;
					break;
				}
				f |= a_VEXPR_SOFTOVERFLOW;
				lhv = S64_MAX;
			}else if(lhv != S64_MIN || rhv != -1)
				lhv %= rhv;
			else
				lhv = 0;
			break;

		case a_VEXPR_CMD_NUM_OR:
			lhv |= rhv;
			break;
		case a_VEXPR_CMD_NUM_AND:
			lhv &= rhv;
			break;
		case a_VEXPR_CMD_NUM_XOR:
			lhv ^= rhv;
			break;

		case a_VEXPR_CMD_NUM_LSHIFT:
		case a_VEXPR_CMD_NUM_RSHIFT:
		case a_VEXPR_CMD_NUM_URSHIFT:{
			u8 sv;

			if(S(u64,rhv) <= 63) /* xxx 63? */
				sv = S(u8,rhv);
			else if(!(f & a_VEXPR_MOD_SATURATED)){
				f |= a_VEXPR_ERR;
				vcp->vc_cmderr = a_VEXPR_ERR_NUM_OVERFLOW;
				break;
			}else
				sv = 63;

			if(cmd == a_VEXPR_CMD_NUM_LSHIFT)
				lhv <<= sv;
			else if(cmd == a_VEXPR_CMD_NUM_RSHIFT)
				lhv >>= sv;
			else
				lhv = S(u64,lhv) >> sv;
			}break;
		}
		break;

	case a_VEXPR_CMD_NUM_PBASE:
		/* Have been here already? */
		if(f & a_VEXPR_PBASE)
			break;
		if((cp = vcp->vc_argv[1]) == NIL || vcp->vc_argv[2] != NIL){
			f |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
			break;
		}
		if(lhv < 2 || lhv > 64){
			f |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_NUM_RANGE;
			break;
		}
		f |= a_VEXPR_PBASE;
		vcp->vc_pbase = S(u8,lhv);
		goto jlhv_redo;
	}

jleave:
	vcp->vc_flags = f;
	vcp->vc_lhv = lhv;
	vcp->vc_rhv = rhv;
	NYD2_OU;
}

static void
a_vexpr_agnostic(struct a_vexpr_ctx *vcp){
	u32 utc[6];
	struct su_timespec ts;
	struct n_string s_b, *s;
	NYD2_IN;

	switch(vcp->vc_cmderr){
	case a_VEXPR_CMD_AGN_DATE_UTC:
		if(vcp->vc_argv[0] == NIL)
			su_timespec_current(&ts);
		else{
			if(vcp->vc_argv[1] != NIL){
				vcp->vc_flags |= a_VEXPR_ERR;
				vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
				break;
			}

			if((su_idec_s64_cp(&ts.ts_sec, vcp->vc_argv[0], 0, NIL
					) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)) != su_IDEC_STATE_CONSUMED)
				goto jedutc_num;
		}

		if(!su_time_epoch_to_gregor(ts.ts_sec, &utc[0], &utc[1], &utc[2], &utc[3], &utc[4], &utc[5])){
jedutc_num:
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_STR_NUM_RANGE;
			break;
		}

		s = n_string_book(n_string_creat_auto(&s_b), 31);

		s = n_string_push_cp(s, "dutc_year=");
		s = n_string_push_cp(s, su_ienc_u32(vcp->vc_iencbuf, utc[0], 10));
		s = n_string_push_c(s, ' ');

		s = n_string_push_cp(s, "dutc_month=");
		s = n_string_push_cp(s, su_ienc_u32(vcp->vc_iencbuf, utc[1], 10));
		s = n_string_push_c(s, ' ');

		s = n_string_push_cp(s, "dutc_month_abbrev=");
		s = n_string_push_cp(s, su_time_month_names_abbrev[utc[1] - 1]);
		s = n_string_push_c(s, ' ');

		s = n_string_push_cp(s, "dutc_day=");
		s = n_string_push_cp(s, su_ienc_u32(vcp->vc_iencbuf, utc[2], 10));
		s = n_string_push_c(s, ' ');

		s = n_string_push_cp(s, "dutc_hour=");
		s = n_string_push_cp(s, su_ienc_u32(vcp->vc_iencbuf, utc[3], 10));
		s = n_string_push_c(s, ' ');

		s = n_string_push_cp(s, "dutc_min=");
		s = n_string_push_cp(s, su_ienc_u32(vcp->vc_iencbuf, utc[4], 10));
		s = n_string_push_c(s, ' ');

		s = n_string_push_cp(s, "dutc_sec=");
		s = n_string_push_cp(s, su_ienc_u32(vcp->vc_iencbuf, utc[5], 10));

		vcp->vc_varres = n_string_cp(s);
		/* n_string_gut(n_string_drop_ownership(s)); */
		break;

	default:
	case a_VEXPR_CMD_AGN_DATE_STAMP_UTC:
		if(vcp->vc_argv[0] != NIL){
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
		}else{
			struct mx_time_current tc;

			mx_time_current_update(&tc, TRU1);
			(void)snprintf(vcp->vc_iencbuf, sizeof(vcp->vc_iencbuf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
				tc.tc_gm.tm_year + 1900, tc.tc_gm.tm_mon + 1, tc.tc_gm.tm_mday,
				tc.tc_gm.tm_hour, tc.tc_gm.tm_min, tc.tc_gm.tm_sec);

			vcp->vc_varres = vcp->vc_iencbuf;
		}
		break;

	case a_VEXPR_CMD_AGN_EPOCH:
	case a_VEXPR_CMD_AGN_SECONDS:
		if(vcp->vc_argv[0] == NIL)
			su_timespec_current(&ts);
		else{
			uz i;

			su_mem_set(utc, 0, sizeof utc);

			for(i = 0;; ++i){
				if(vcp->vc_argv[i] == NIL)
					break;
				if(i == 6){
					i = 0;
					break;
				}
				if((su_idec_u32_cp(&utc[i], vcp->vc_argv[i], 0, NIL
							) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
						) != su_IDEC_STATE_CONSUMED)
					goto jeepoch_num;
			}
			if(i == 0){
				vcp->vc_flags |= a_VEXPR_ERR;
				vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
				break;
			}

			if((ts.ts_sec = su_time_gregor_to_epoch(utc[0], utc[1], utc[2], utc[3], utc[4], utc[5])) == -1){
jeepoch_num:
				vcp->vc_flags |= a_VEXPR_ERR;
				vcp->vc_cmderr = a_VEXPR_ERR_STR_NUM_RANGE;
				break;
			}
			ts.ts_nano = 0;
		}

		s = n_string_book(n_string_creat_auto(&s_b), 31);

		if(vcp->vc_cmderr != a_VEXPR_CMD_AGN_SECONDS)
			s = n_string_push_cp(s, "epoch_sec=");
		s = n_string_push_cp(s, su_ienc_s64(vcp->vc_iencbuf, ts.ts_sec, 10));
		if(vcp->vc_cmderr != a_VEXPR_CMD_AGN_SECONDS){
			s = n_string_push_c(s, ' ');

			s = n_string_push_cp(s, "epoch_nsec=");
			s = n_string_push_cp(s, su_ienc_s64(vcp->vc_iencbuf, ts.ts_nano, 10));
		}

		vcp->vc_varres = n_string_cp(s);
		/* n_string_gut(n_string_drop_ownership(s)); */
		break;

	case a_VEXPR_CMD_AGN_RANDOM:
		if(vcp->vc_argv[0] == NIL || vcp->vc_argv[1] != NIL){
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
			break;
		}
		vcp->vc_arg = vcp->vc_argv[0];

		if((su_idec_s64_cp(&vcp->vc_lhv, vcp->vc_argv[0], 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
				) != su_IDEC_STATE_CONSUMED || vcp->vc_lhv < 0 || vcp->vc_lhv > PATH_MAX){
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_STR_NUM_RANGE;
			break;
		}
		if(vcp->vc_lhv == 0)
			vcp->vc_lhv = NAME_MAX;
		vcp->vc_varres = mx_random_create_cp(S(uz,vcp->vc_lhv), NIL);
		break;
	 }

	NYD2_OU;
}

static void
a_vexpr_string(struct a_vexpr_ctx *vcp){
	NYD2_IN;

	switch(vcp->vc_cmderr){
	default:
	case a_VEXPR_CMD_STR_MAKEPRINT:{
		struct str sin, sout;

		if(vcp->vc_argv[0] == NIL || vcp->vc_argv[1] != NIL){
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
			break;
		}
		vcp->vc_arg = vcp->vc_argv[0];

		/* XXX using su_cs_len for `vexpr makeprint' is wrong for UTF-16 */
		sin.l = su_cs_len(sin.s = UNCONST(char*,vcp->vc_arg));
		mx_makeprint(&sin, &sout);
		vcp->vc_varres = savestrbuf(sout.s, sout.l);
		su_FREE(sout.s);
		}break;
	/* TODO `vexpr': (wide) string length, find, etc!! */
#ifdef mx_HAVE_REGEX
	case a_VEXPR_CMD_STR_IREGEX:
		n_OBSOLETE(_("vexpr: iregex: simply use regex?[case] instead, please"));
		vcp->vc_flags |= a_VEXPR_MOD_CASE;
		/* FALLTHRU */
	case a_VEXPR_CMD_STR_REGEX:{
		struct su_re re;

		vcp->vc_flags |= a_VEXPR_ISNUM | a_VEXPR_ISDECIMAL;
		if(vcp->vc_argv[0] == NIL || vcp->vc_argv[1] == NIL ||
				(vcp->vc_argv[2] != NIL && vcp->vc_argv[3] != NIL)){
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_SYNOPSIS;
			break;
		}
		vcp->vc_arg = vcp->vc_argv[1];

		su_re_create(&re);

		if(su_re_setup_cp(&re, vcp->vc_arg, (su_RE_SETUP_EXT |
					((vcp->vc_flags & a_VEXPR_MOD_CASE) ? su_RE_SETUP_ICASE
				 : su_RE_SETUP_NONE))) != su_RE_ERROR_NONE){
			n_err(_("vexpr: invalid regular expression: %s: %s\n"),
				n_shexp_quote_cp(vcp->vc_arg, FAL0), su_re_error_doc(&re));
			su_re_gut(&re);
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_STR_GENERIC;
			n_pstate_err_no = su_ERR_INVAL;
			break;
		}

		if(!su_re_eval_cp(&re, vcp->vc_argv[0], su_RE_EVAL_NONE)){
			vcp->vc_flags |= a_VEXPR_ERR;
			vcp->vc_cmderr = a_VEXPR_ERR_STR_NODATA;
		}
		/* Search only? */
		else if(vcp->vc_argv[2] == NIL){
			if(UCMP(64, re.re_match[0].rem_start, <=, S64_MAX))
				vcp->vc_lhv = S(s64,re.re_match[0].rem_start);
			else{
				vcp->vc_flags |= a_VEXPR_ERR;
				vcp->vc_cmderr = a_VEXPR_ERR_STR_OVERFLOW;
			}
		}
		/* Search+Replace */
		else{
			mx_var_re_match_set(re.re_group_count, vcp->vc_argv[0], re.re_match);
			{/* TODO v15-compat vexpr regex: directly do a_vexpr__regex_replace() */
			char const *name, **argv, **ccpp;
			uz i, argc;

			n_OBSOLETE(_("`vexpr': regex: search+replace: now uses regular expression match group accesses via \\$^NUMBER; \\$NUMBER positional parameter support will vanish in v15"));

			name = savestrbuf(&vcp->vc_argv[0][re.re_match[0].rem_start],
					re.re_match[0].rem_end - re.re_match[0].rem_start);

			argc = re.re_group_count + 1;
			argv = su_LOFI_TALLOC(char const*,argc +1);

			for(ccpp = argv, i = 1; i < argc; ++ccpp, ++i)
				if(re.re_match[i].rem_start != -1)
					*ccpp = savestrbuf(&vcp->vc_argv[0][re.re_match[i].rem_start],
							re.re_match[i].rem_end - re.re_match[i].rem_start);
				else
					*ccpp = su_empty;
			*ccpp = NIL;

			/* Logical unconst */
			vcp->vc_varres = temporary_pospar_access_hook(name, argv, argc,
					&a_vexpr__regex_replace, UNCONST(char*,vcp->vc_argv[2]));

			su_LOFI_FREE(argv);

			if(vcp->vc_varres != NIL)
				vcp->vc_flags ^= (a_VEXPR_ISNUM | a_VEXPR_ISDECIMAL);
			else{
				vcp->vc_flags |= a_VEXPR_ERR;
				vcp->vc_cmderr = a_VEXPR_ERR_STR_NODATA;/* XXX could be OVERFLOW */
			}
			}
		}

		su_re_gut(&re);

		}break;
#endif /* mx_HAVE_REGEX */
	}

	NYD2_OU;
}

#ifdef mx_HAVE_REGEX
static char *
a_vexpr__regex_replace(void *uservp){
	struct str templ;
	struct n_string s_b;
	char *rv;
	BITENUM(u32,n_shexp_state) shs;
	NYD2_IN;

	templ.s = S(char*,uservp);
	templ.l = UZ_MAX;
	shs = n_shexp_parse_token((n_SHEXP_PARSE_LOG | n_SHEXP_PARSE_IGN_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_FIXED |
				n_SHEXP_PARSE_QUOTE_AUTO_DSQ), mx_SCOPE_NONE, n_string_creat_auto(&s_b), &templ, NIL);
	if((shs & (n_SHEXP_STATE_ERR_MASK | n_SHEXP_STATE_STOP)) == n_SHEXP_STATE_STOP){
		rv = n_string_cp(&s_b);
		n_string_drop_ownership(&s_b);
	}else
		rv = NIL;

	NYD2_OU;
	return rv;
}
#endif /* mx_HAVE_REGEX */

int
c_vexpr(void *vp){ /* TODO POSIX expr(1) comp. exit status */
	struct a_vexpr_ctx vc;
	char const *cp;
	u32 f;
	uz i, j;
	struct mx_cmd_arg_ctx *cacp;
	NYD_IN;

	STRUCT_ZERO(struct a_vexpr_ctx, &vc);
	cacp = vp;
	vc.vc_flags = a_VEXPR_ERR | a_VEXPR_ISNUM;
	vc.vc_cmderr = a_VEXPR_ERR_SUBCMD;
	vc.vc_varres = su_empty;
	/* C99 */{
		struct mx_cmd_arg *cap;

		cap = cacp->cac_arg;
		vc.vc_arg =
		vc.vc_cmd_name = cap->ca_arg.ca_str.s;

		for(i = 0; (cap = cap->ca_next) != NIL;){
			if(i >= NELEM(vc.vc_argv))
				goto jesyno;
			vc.vc_argv[i++] = cap->ca_arg.ca_str.s;
		}
	}

	if((cp = su_cs_find_c(vc.vc_cmd_name, '?')) != NIL){
		j = P2UZ(cp - vc.vc_cmd_name);
		if(cp[1] == '\0')
			f = a_VEXPR_MOD_MASK;
		else if(su_cs_starts_with_case("case", &cp[1]))
			f = a_VEXPR_MOD_CASE;
		else if(su_cs_starts_with_case("saturated", &cp[1]))
			f = a_VEXPR_MOD_SATURATED;
		else{
			n_err(_("vexpr: invalid modifier: %s\n"), n_shexp_quote_cp(vc.vc_cmd_name, FAL0));
			f = a_VEXPR_ERR;
			goto jleave;
		}
	}else{
		f = a_VEXPR_NONE;
		if(*vc.vc_cmd_name == '@'){ /* v15compat */
			n_OBSOLETE2(_("vexpr: please use ? modifier suffix, not @ prefix"),
				n_shexp_quote_cp(vc.vc_cmd_name, FAL0));
			++vc.vc_cmd_name;
			f = a_VEXPR_MOD_MASK;
		}
		j = su_cs_len(vc.vc_cmd_name);
	}

	for(i = 0; i < NELEM(a_vexpr_subcmds); ++i){
		if(su_cs_starts_with_case_n(a_vexpr_subcmds[i].vs_name, vc.vc_cmd_name, j)){
			vc.vc_cmd_name = a_vexpr_subcmds[i].vs_name;
			i = a_vexpr_subcmds[i].vs_mpv;

			if(UNLIKELY(f & a_VEXPR_MOD_MASK)){
				u32 f2;

				f2 = f & a_VEXPR_MOD_MASK;

				if(UNLIKELY(!(i & a_VEXPR_MOD_MASK))){
					vc.vc_cmderr = a_VEXPR_ERR_MOD_NOT_ALLOWED;
					break;
				}else if(UNLIKELY(f2 != a_VEXPR_MOD_MASK && f2 != (i & a_VEXPR_MOD_MASK))){
					vc.vc_cmderr = a_VEXPR_ERR_MOD_NOT_SUPPORTED;
					break;
				}
			}

			vc.vc_arg = vc.vc_cmd_name;
			vc.vc_flags = f;
			i = (i & a_VEXPR__FCMDMASK) >> a_VEXPR__FSHIFT;
			if((vc.vc_cmderr = S(u8,i)) < a_VEXPR_CMD_NUM__MAX)
				a_vexpr_numeric(&vc);
			else if(i < a_VEXPR_CMD_AGN__MAX)
				a_vexpr_agnostic(&vc);
			else /*if(i < a_VEXPR_CMD_STR__MAX)*/
				a_vexpr_string(&vc);
			break;
		}
	}
	f = vc.vc_flags;

	if(LIKELY(!(f & a_VEXPR_ERR)))
		n_pstate_err_no = (f & a_VEXPR_SOFTOVERFLOW) ? su_ERR_OVERFLOW : su_ERR_NONE;
	else switch(vc.vc_cmderr){
	case a_VEXPR_ERR_NONE:
		ASSERT(0);
		break;
	case a_VEXPR_ERR_SYNOPSIS:
jesyno:
		mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("vexpr"), NIL);
		n_pstate_err_no = su_ERR_INVAL;
		goto jenum;
	case a_VEXPR_ERR_SUBCMD:
		n_err(_("vexpr: invalid subcommand: %s\n"), n_shexp_quote_cp(vc.vc_arg, FAL0));
		n_pstate_err_no = su_ERR_INVAL;
		goto jenum;
	case a_VEXPR_ERR_MOD_NOT_ALLOWED:
		n_err(_("vexpr: modifiers not allowed for subcommand: %s\n"), n_shexp_quote_cp(vc.vc_arg, FAL0));
		n_pstate_err_no = su_ERR_INVAL;
		goto jenum;
	case a_VEXPR_ERR_MOD_NOT_SUPPORTED:
		n_err(_("vexpr: given modifier not supported for subcommand: %s\n"), n_shexp_quote_cp(vc.vc_arg, FAL0));
		n_pstate_err_no = su_ERR_INVAL;
		goto jenum;
	case a_VEXPR_ERR_NUM_RANGE:
		n_err(_("vexpr: numeric argument invalid or out of range: %s\n"), n_shexp_quote_cp(vc.vc_arg, FAL0));
		n_pstate_err_no = su_ERR_RANGE;
		goto jenum;
	case a_VEXPR_ERR_NUM_OVERFLOW:
		n_err(_("vexpr: expression overflows datatype: %" PRId64 " %s %" PRId64 "\n"),
			vc.vc_lhv, vc.vc_cmd_name, vc.vc_rhv);
		n_pstate_err_no = su_ERR_OVERFLOW;
		goto jenum;
	default:
jenum:
		f = a_VEXPR_ERR | a_VEXPR_ISNUM | a_VEXPR_ISDECIMAL;
		vc.vc_lhv = -1;
		break;
	case a_VEXPR_ERR_STR_NUM_RANGE:
		n_err(_("vexpr: numeric argument invalid or out of range: %s\n"), n_shexp_quote_cp(vc.vc_arg, FAL0));
		n_pstate_err_no = su_ERR_RANGE;
		goto jestr;
	case a_VEXPR_ERR_STR_OVERFLOW:
		n_err(_("vexpr: string length or offset overflows datatype\n"));
		n_pstate_err_no = su_ERR_OVERFLOW;
		goto jestr;
	case a_VEXPR_ERR_STR_NODATA:
		n_pstate_err_no = su_ERR_NODATA;
		/* FALLTHRU*/
	case a_VEXPR_ERR_STR_GENERIC:
jestr:
		vc.vc_varres = su_empty;
		f = a_VEXPR_ERR;
		break;
	}

	/* Generate the variable value content for numerics.
	 * Anticipate in our handling below!  (Avoid needless work) */
	if((f & a_VEXPR_ISNUM) && ((f & (a_VEXPR_ISDECIMAL | a_VEXPR_PBASE)) || cacp->cac_vput != NIL)){
		cp = su_ienc(vc.vc_iencbuf, vc.vc_lhv, ((!(f & a_VEXPR_ERR) && (f & a_VEXPR_PBASE)) ? vc.vc_pbase : 10),
				(((f & (a_VEXPR_PBASE | a_VEXPR_PBASE_FORCE_UNSIGNED)) ==
						(a_VEXPR_PBASE | a_VEXPR_PBASE_FORCE_UNSIGNED))
					? su_IENC_MODE_NONE : su_IENC_MODE_SIGNED_TYPE));
		if(cp != NIL)
			vc.vc_varres = cp;
		else{
			f |= a_VEXPR_ERR;
			vc.vc_varres = su_empty;
		}
	}

	if(cacp->cac_vput == NIL){
		/* If no error and we are printing a numeric result, print some more bases for the fun of it */
		if((f & (a_VEXPR_ERR | a_VEXPR_ISNUM | a_VEXPR_ISDECIMAL)) == a_VEXPR_ISNUM){
			char binabuf[64 + 64 / 8 +1];

			for(j = 1, i = 0; i < 64; ++i){
				binabuf[63 + 64 / 8 -j - i] = (vc.vc_lhv & (S(u64,1) << i)) ? '1' : '0';
				if((i & 7) == 7 && i != 63){
					++j;
					binabuf[63 + 64 / 8 -j - i] = ' ';
				}
			}
			binabuf[64 + 64 / 8 -1] = '\0';

			if(fprintf(n_stdout, "0b %s\n0%" PRIo64 " | 0x%" PRIX64 " | %" PRId64 "\n",
						binabuf, S(u64,vc.vc_lhv), S(u64,vc.vc_lhv), vc.vc_lhv) < 0 ||
					((f & a_VEXPR_PBASE) && fprintf(n_stdout, "%s\n", vc.vc_varres) < 0)){
				n_pstate_err_no = su_err_by_errno();
				f |= a_VEXPR_ERR;
			}
		}else if(vc.vc_varres != NIL && fprintf(n_stdout, "%s\n", vc.vc_varres) < 0){
			n_pstate_err_no = su_err_by_errno();
			f |= a_VEXPR_ERR;
		}
	}else if(!n_var_vset(cacp->cac_vput, R(up,vc.vc_varres), cacp->cac_scope_vput)){
		n_pstate_err_no = su_ERR_NOTSUP;
		f |= a_VEXPR_ERR;
	}

jleave:
	NYD_OU;
	return (f & a_VEXPR_ERR) ? su_EX_ERR : su_EX_OK;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_CMD_VEXPR */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_VEXPR
/* s-itt-mode */
