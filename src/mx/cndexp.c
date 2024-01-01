/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cndexp.h.
 *
 * Copyright (c) 2014 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cndexp
#define mx_SOURCE
#define mx_SOURCE_CNDEXP

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>

#ifdef mx_HAVE_REGEX
# include <su/re.h>
#endif

#include "mx/cmd.h"

#include "mx/cndexp.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_cndexp_op{
	a_CNDEXP_OP_NONE = 0,

	a_CNDEXP_OP_N,
	a_CNDEXP_OP_MIN_UNARY = a_CNDEXP_OP_N,
	a_CNDEXP_OP_v = a_CNDEXP_OP_N,
	a_CNDEXP_OP_Z,
	a_CNDEXP_OP_n,
	a_CNDEXP_OP_z,

	a_CNDEXP_OP_FU_b,
	a_CNDEXP_OP_FU_c,
	a_CNDEXP_OP_FU_d,
	a_CNDEXP_OP_FU_e,
	a_CNDEXP_OP_FU_f,
	a_CNDEXP_OP_FU_G,
	a_CNDEXP_OP_FU_g,
	a_CNDEXP_OP_FU_k,
	a_CNDEXP_OP_FU_L,
	a_CNDEXP_OP_FU_O,
	a_CNDEXP_OP_FU_p,
	a_CNDEXP_OP_FU_r,
	a_CNDEXP_OP_FU_S,
	a_CNDEXP_OP_FU_s,
	a_CNDEXP_OP_FU_t,
	a_CNDEXP_OP_FU_u,
	a_CNDEXP_OP_FU_w,
	a_CNDEXP_OP_FU_x,
	a_CNDEXP_OP_MAX_UNARY = a_CNDEXP_OP_FU_x,

	a_CNDEXP_OP_INT_lt,
	a_CNDEXP_OP_MIN_BINARY = a_CNDEXP_OP_INT_lt,
	a_CNDEXP_OP_INT_le,
	a_CNDEXP_OP_INT_eq,
	a_CNDEXP_OP_INT_ne,
	a_CNDEXP_OP_INT_ge,
	a_CNDEXP_OP_INT_gt,

	a_CNDEXP_OP_8B_LT,
	a_CNDEXP_OP_8B_LE,
	a_CNDEXP_OP_8B_EQ,
	a_CNDEXP_OP_8B_NE,
	a_CNDEXP_OP_8B_GE,
	a_CNDEXP_OP_8B_GT,
	a_CNDEXP_OP_8B_SUB,
	a_CNDEXP_OP_8B_NSUB,
#ifdef mx_HAVE_REGEX
	a_CNDEXP_OP_RE_MATCH,
	a_CNDEXP_OP_RE_NMATCH,
#endif

	a_CNDEXP_OP_FB_ef,
	a_CNDEXP_OP_FB_nt,
	a_CNDEXP_OP_FB_ot,
	a_CNDEXP_OP_MAX_BINARY = a_CNDEXP_OP_FB_ot,

	a_CNDEXP__OP_MAX
};

/* This is ORd to the op */
enum a_cndexp_op_flags{
	a_CNDEXP_OP_MASK = 0xFFu,

	a_CNDEXP_OP_F_UNANOT = 1u<<8, /* */
	a_CNDEXP_OP_F_LBRACK = 1u<<9,
	a_CNDEXP_OP_F_RBRACK = 1u<<10,
	a_CNDEXP_OP_F_AND = 1u<<11,
	a_CNDEXP_OP_F_OR = 1u<<12,
	a_CNDEXP_OP_F_ANDOR = a_CNDEXP_OP_F_AND | a_CNDEXP_OP_F_OR,
	a_CNDEXP_OP_F_TERMINATOR = a_CNDEXP_OP_F_RBRACK | a_CNDEXP_OP_F_ANDOR,

	a_CNDEXP_OP_F_ERR = 1u<<13,
	a_CNDEXP_OP_F_CASE = 1u<<14,
	a_CNDEXP_OP_F_SATURATED = 1u<<15
	/* Must fit in 16-bit */
};
CTA(S(u32,a_CNDEXP_OP_MASK) >= S(u32,a_CNDEXP__OP_MAX), "Bit range overlap");

struct a_cndexp_ctx{
	char const * const *cec_argv_base;
	char const * const *cec_argv; /* (Moving) */
	boole cec_log_on_error;
	boole cec_have_pi;
	u8 cec__pad[2];
	u32 cec_argc;
	u16 *cec_toks; /* "Analyzed" (moving) */
	struct su_pathinfo cec_pi;
};

/* Entry point.  Anything level>0 is inside [ groups */
static boole a_cndexp_expr(struct a_cndexp_ctx *cecp, uz level, boole noop);

/* */
static boole a_cndexp__op_apply(struct a_cndexp_ctx *cecp, u16 op, char const *lhv, char const *rhv, boole noop);

/* */
static void a_cndexp_error(struct a_cndexp_ctx const *cecp, char const *msg_or_nil);

static boole
a_cndexp_expr(struct a_cndexp_ctx *cecp, uz level, boole noop){
	char const *emsg;
	u32 c;
	boole syntax, rv, xrv;
	NYD_IN;

	for(syntax = FAL0, rv = TRUM1;; syntax = !syntax, cecp->cec_argv += c, cecp->cec_toks += c, cecp->cec_argc -= c){
		u16 op;
		u32 i, unanot;

		if((i = cecp->cec_argc) == 0)
			break;

		if(syntax){
			op = *cecp->cec_toks;

			if(op & a_CNDEXP_OP_F_RBRACK){
				if(level == 0){
					emsg = V_("no group to close here");
					goto jesyn;
				}
				if(noop && rv/* == TRUM1*/)
					rv = TRU1;
				break;
			}

			if(!(op & a_CNDEXP_OP_F_ANDOR)){
				emsg = V_("expected an AND/OR list");
				goto jesyn;
			}

			noop = (((op & a_CNDEXP_OP_F_AND) != 0) ^ (rv == TRU1));
			c = 1;
			continue;
		}

		/* There may be leading unary NOTs */
		for(unanot = c = 0; c < i; ++unanot, ++c){
			op = cecp->cec_toks[c];
			if((op & a_CNDEXP_OP_MASK) != a_CNDEXP_OP_NONE || !(op & a_CNDEXP_OP_F_UNANOT))
				break;
		}
		if(c == i){
			ASSERT(unanot > 0);
			emsg = N_("trailing unary ! operators");
			goto jesyn;
		}

		/* Maybe a group open? */
		if(cecp->cec_toks[c] & a_CNDEXP_OP_F_LBRACK){
			++c;
			cecp->cec_argv += c;
			cecp->cec_argc -= c;
			cecp->cec_toks += c;
			xrv = a_cndexp_expr(cecp, level + 1, noop);
			if(cecp->cec_argc-- == 0){
				emsg = N_("premature end, a ']' is missing or must be quoted");
				goto jesyn;
			}
			++cecp->cec_argv;
			++cecp->cec_toks;
			c = 0;
			goto jstepit;
		}

		/* Must be an operator */
		if(i - c > 1){
			u32 c_save;

			c_save = 0;
jop_redo:
			op = cecp->cec_toks[c];
			op &= a_CNDEXP_OP_MASK;
			if(op >= a_CNDEXP_OP_MIN_UNARY && op <= a_CNDEXP_OP_MAX_UNARY){
				++c;
				xrv = a_cndexp__op_apply(cecp, cecp->cec_toks[c - 1], cecp->cec_argv[c], NIL, noop);
				++c;
				goto jstepit;
			}

			if(i - c > 2){
				u16 opx;

				opx = (cecp->cec_toks[c + 1] & a_CNDEXP_OP_MASK);
				if(opx >= a_CNDEXP_OP_MIN_BINARY && opx <= a_CNDEXP_OP_MAX_BINARY){
					++c;
					xrv = a_cndexp__op_apply(cecp, cecp->cec_toks[c],
							cecp->cec_argv[c - 1], cecp->cec_argv[c + 1], noop);
					c += 2;
					goto jstepit;
				}
			}

			/* Maybe we saw UNANOT? Step left by one and retry */
			if(unanot > 0 && c_save == 0){
				--unanot;
				c_save = c--;
				ASSERT(c_save != 0);
				goto jop_redo;
			}
			if(c_save != 0){
				++unanot;
				c = c_save;
			}
		}

		/* Implicit no-argument operator? */
		if(cecp->cec_toks[c] == a_CNDEXP_OP_NONE){
			char const *cp;

			cp = cecp->cec_argv[c++];
			switch(*cp){
			default:
				switch((xrv = n_boolify(cp, UZ_MAX, TRUM1))){
				case FAL0:
				case TRU1:
					goto jstepit;
				default:
					break;
				}
				break;
			case 'R': case 'r':
				xrv = ((n_psonce & n_PSO_SENDMODE) == 0);
				goto jstepit;
			case 'S': case 's':
				xrv = ((n_psonce & n_PSO_SENDMODE) != 0);
				goto jstepit;

			case 'T': case 't':
				if(!su_cs_cmp_case(cp, "true")) /* Beware! */
					xrv = TRU1;
				else
					xrv = ((n_psonce & n_PSO_INTERACTIVE) != 0);
				goto jstepit;
			}
		}

		/* This is not right */
		emsg = N_("expected an operator");
		goto jesyn;

jstepit:
		if(xrv == TRUM1){
			rv = xrv;
			break;
		}
		if(unanot & 1)
			xrv = !xrv;
		if(!noop)
			rv = xrv;
	}

jleave:
	NYD_OU;
	return rv;

jesyn:
	if(emsg != NIL)
		emsg = V_(emsg);
	a_cndexp_error(cecp, emsg);
	rv = TRUM1;
	goto jleave;
}

static boole
a_cndexp__op_apply(struct a_cndexp_ctx  *cecp, u16 op, char const *lhv, char const *rhv, boole noop){
	struct su_pathinfo pi1, pi2;
	boole act, rv;
	NYD2_IN;

	act = (noop == FAL0);
	if(!act && (n_poption & n_PO_D_V))
		act = TRUM1;
	UNINIT(rv, FAL0);

	if(op & a_CNDEXP_OP_F_ERR){
		lhv = N_("invalid modifier used for operator");
		goto jesyn;
	}

	switch(op & a_CNDEXP_OP_MASK){
	case a_CNDEXP_OP_N:
	case a_CNDEXP_OP_Z:
		if(act){
			lhv = n_var_vlook(lhv, TRU1);
			rv = ((lhv == NIL) == ((op & a_CNDEXP_OP_MASK) == a_CNDEXP_OP_Z));
		}
		break;

	case a_CNDEXP_OP_n: rv = *lhv != '\0'; break;
	case a_CNDEXP_OP_z: rv = *lhv == '\0'; break;

	case a_CNDEXP_OP_FU_b: FALLTHRU
	case a_CNDEXP_OP_FU_c: FALLTHRU
	case a_CNDEXP_OP_FU_d: FALLTHRU
	case a_CNDEXP_OP_FU_e: FALLTHRU
	case a_CNDEXP_OP_FU_f: FALLTHRU
	case a_CNDEXP_OP_FU_G: FALLTHRU
	case a_CNDEXP_OP_FU_g: FALLTHRU
	case a_CNDEXP_OP_FU_p: FALLTHRU
	case a_CNDEXP_OP_FU_k: FALLTHRU
	case a_CNDEXP_OP_FU_O: FALLTHRU
	case a_CNDEXP_OP_FU_s: FALLTHRU
	case a_CNDEXP_OP_FU_S: FALLTHRU
	case a_CNDEXP_OP_FU_u:
		if(act){
			if(!(op & a_CNDEXP_OP_F_SATURATED))
				cecp->cec_have_pi = (*lhv != '\0' && su_pathinfo_stat(&cecp->cec_pi, lhv));
			if((rv = cecp->cec_have_pi)){
				switch(op & a_CNDEXP_OP_MASK){
				case a_CNDEXP_OP_FU_b: rv = su_pathinfo_is_blk(&cecp->cec_pi); break;
				case a_CNDEXP_OP_FU_c: rv = su_pathinfo_is_chr(&cecp->cec_pi); break;
				case a_CNDEXP_OP_FU_d: rv = su_pathinfo_is_dir(&cecp->cec_pi); break;
				/*case a_CNDEXP_OP_FU_e:*/
				case a_CNDEXP_OP_FU_f: rv = su_pathinfo_is_reg(&cecp->cec_pi); break;
				case a_CNDEXP_OP_FU_G: rv = (n_group_eid == cecp->cec_pi.pi_gid); break;
				case a_CNDEXP_OP_FU_g: rv = ((cecp->cec_pi.pi_flags & su_IOPF_SGID) != 0); break;
				case a_CNDEXP_OP_FU_p: rv = su_pathinfo_is_fifo(&cecp->cec_pi); break;
				case a_CNDEXP_OP_FU_k: rv = ((cecp->cec_pi.pi_flags & su_IOPF_SVTX) != 0); break;
				case a_CNDEXP_OP_FU_s: rv = (cecp->cec_pi.pi_size > 0); break;
				case a_CNDEXP_OP_FU_S: rv = su_pathinfo_is_sock(&cecp->cec_pi); break;
				case a_CNDEXP_OP_FU_O: rv = (n_user_eid == cecp->cec_pi.pi_uid); break;
				case a_CNDEXP_OP_FU_u: rv = ((cecp->cec_pi.pi_flags & su_IOPF_SUID) != 0); break;
				}
			}
		}
		break;

	case a_CNDEXP_OP_FU_L:
		if(act){
			if(!(op & a_CNDEXP_OP_F_SATURATED))
				cecp->cec_have_pi = (*lhv != '\0' && su_pathinfo_lstat(&cecp->cec_pi, lhv));
			if((rv = cecp->cec_have_pi))
				rv = su_pathinfo_is_lnk(&cecp->cec_pi);
		}
		break;

	case a_CNDEXP_OP_FU_r:
		if(act)
			rv = (*lhv != '\0' && su_path_access(lhv, su_IOPF_READ));
		break;
	case a_CNDEXP_OP_FU_w:
		if(act)
			rv = (*lhv != '\0' && su_path_access(lhv, su_IOPF_WRITE));
		break;
	case a_CNDEXP_OP_FU_x:
		if(act)
			rv = (*lhv != '\0' && su_path_access(lhv, su_IOPF_EXEC));
		break;

	case a_CNDEXP_OP_FU_t:
		if(act){
			s32 lhvi;

			rv = ((su_idec_s32_cp(&lhvi, lhv, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
					) == su_IDEC_STATE_CONSUMED && lhvi >= 0);
			if(rv)
				rv = su_path_is_a_tty(lhvi);
		}
		break;

	case a_CNDEXP_OP_INT_lt: FALLTHRU
	case a_CNDEXP_OP_INT_le: FALLTHRU
	case a_CNDEXP_OP_INT_eq: FALLTHRU
	case a_CNDEXP_OP_INT_ne: FALLTHRU
	case a_CNDEXP_OP_INT_ge: FALLTHRU
	case a_CNDEXP_OP_INT_gt:
		if(act){
			s64 lhvi, rhvi;
			u32 lf, rf;

			if(*lhv == '\0')
				lhv = n_0;
			if(*rhv == '\0')
				rhv = n_0;

			rf = lf = (((op & a_CNDEXP_OP_F_SATURATED) ? su_IDEC_MODE_LIMIT_NOERROR : su_IDEC_MODE_NONE) |
					su_IDEC_MODE_SIGNED_TYPE);
			lf = su_idec_cp(&lhvi, lhv, 0, lf, NIL);
			rf = su_idec_cp(&rhvi, rhv, 0, rf, NIL);

			if((lf & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)) != su_IDEC_STATE_CONSUMED ||
					(rf & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)) != su_IDEC_STATE_CONSUMED){
				/* TODO if/elif: should support $! and set ERR-OVERFLOW!! */
				lhv = N_("invalid integer number");
				goto jesyn;
			}

			lhvi -= rhvi;
			switch(op & a_CNDEXP_OP_MASK){
			default:
			case a_CNDEXP_OP_INT_lt: rv = (lhvi < 0); break;
			case a_CNDEXP_OP_INT_le: rv = (lhvi <= 0); break;
			case a_CNDEXP_OP_INT_eq: rv = (lhvi == 0); break;
			case a_CNDEXP_OP_INT_ne: rv = (lhvi != 0); break;
			case a_CNDEXP_OP_INT_ge: rv = (lhvi >= 0); break;
			case a_CNDEXP_OP_INT_gt: rv = (lhvi > 0); break;
			}
		}
		break;

	case a_CNDEXP_OP_8B_LT: FALLTHRU
	case a_CNDEXP_OP_8B_LE: FALLTHRU
	case a_CNDEXP_OP_8B_EQ: FALLTHRU
	case a_CNDEXP_OP_8B_NE: FALLTHRU
	case a_CNDEXP_OP_8B_GE: FALLTHRU
	case a_CNDEXP_OP_8B_GT:
		if(act){
			s32 scmp;

			scmp = (op & a_CNDEXP_OP_F_CASE ? su_cs_cmp_case : su_cs_cmp)(lhv, rhv);

			switch(op & a_CNDEXP_OP_MASK){
			default:
			case a_CNDEXP_OP_8B_LT: rv = (scmp < 0); break;
			case a_CNDEXP_OP_8B_LE: rv = (scmp <= 0); break;
			case a_CNDEXP_OP_8B_EQ: rv = (scmp == 0); break;
			case a_CNDEXP_OP_8B_NE: rv = (scmp != 0); break;
			case a_CNDEXP_OP_8B_GE: rv = (scmp >= 0); break;
			case a_CNDEXP_OP_8B_GT: rv = (scmp > 0); break;
			}
		}
		break;

	case a_CNDEXP_OP_8B_SUB: FALLTHRU
	case a_CNDEXP_OP_8B_NSUB:
		if(act)
			rv = (((op & a_CNDEXP_OP_F_CASE ? su_cs_find_case : su_cs_find)(lhv, rhv) != NIL) ^
					((op & a_CNDEXP_OP_MASK) == a_CNDEXP_OP_8B_NSUB));
		break;

#ifdef mx_HAVE_REGEX
	case a_CNDEXP_OP_RE_MATCH: FALLTHRU
	case a_CNDEXP_OP_RE_NMATCH:
		if(act){
			struct su_re re;

			su_re_create(&re);

			if(su_re_setup_cp(&re, rhv, (su_RE_SETUP_EXT |
						((act < FAL0) ? su_RE_SETUP_TEST_ONLY : su_RE_SETUP_NONE) |
						((op & a_CNDEXP_OP_F_CASE) ? su_RE_SETUP_ICASE : su_RE_SETUP_NONE))
					) != su_RE_ERROR_NONE){
				lhv = savecat(_("invalid regular expression: "), su_re_error_doc(&re));
				su_re_gut(&re);
				goto jesyn_ntr;
			}

			if(act > FAL0){
				rv = su_re_eval_cp(&re, lhv, su_RE_EVAL_NONE);
				if(mx_var_re_match_set((rv ? re.re_group_count : 0), lhv, re.re_match) != su_ERR_NONE){
					su_re_gut(&re);
					a_cndexp_error(cecp, _("pattern with too many matches"));
					n_pstate_err_no = su_ERR_OVERFLOW;
					rv = TRUM1;
					goto jleave;
				}
				rv = (!rv ^ ((op & a_CNDEXP_OP_MASK) == a_CNDEXP_OP_RE_MATCH));
			}

			su_re_gut(&re);
		}
		break;
#endif /* mx_HAVE_REGEX */

	case a_CNDEXP_OP_FB_ef: FALLTHRU
	case a_CNDEXP_OP_FB_nt: FALLTHRU
	case a_CNDEXP_OP_FB_ot:
		if(act){
			boole s1, s2;

			s1 = (*lhv != '\0' && su_pathinfo_stat(&pi1, lhv));
			s2 = (*rhv != '\0' && su_pathinfo_stat(&pi2, rhv));

			switch(op & a_CNDEXP_OP_MASK){
			case a_CNDEXP_OP_FB_ef:
				rv = (s1 && s2 && pi1.pi_dev == pi2.pi_dev && pi1.pi_ino == pi2.pi_ino);
				break;
			case a_CNDEXP_OP_FB_nt:
				rv = (s1 > s2 || su_timespec_is_GT(&pi1.pi_mtime, &pi2.pi_mtime));
				break;
			case a_CNDEXP_OP_FB_ot:
				rv = (s1 < s2 || su_timespec_is_LT(&pi1.pi_mtime, &pi2.pi_mtime));
				break;
			}
		}
		break;
	}

	if(act <= FAL0)
		rv = TRU1;
jleave:
	NYD2_OU;
	return rv;

jesyn:
	if(lhv != NIL)
		lhv = V_(lhv);
#ifdef mx_HAVE_REGEX
jesyn_ntr:
#endif
	a_cndexp_error(cecp, lhv);
	rv = TRUM1;
	goto jleave;
}

static void
a_cndexp_error(struct a_cndexp_ctx const *cecp, char const *msg_or_nil){
	struct str e, r;
	NYD2_IN;

	if(cecp->cec_log_on_error){
		str_concat_cpa(&e, cecp->cec_argv_base, " ");

		str_concat_cpa(&r, cecp->cec_argv, " ");

		if(msg_or_nil == NIL)
			msg_or_nil = _("invalid expression syntax");

		n_err(_("Conditional expression: %s: stop during: %s: of: %s\n"), msg_or_nil, r.s, e.s);

		/* v15-compat */
		n_OBSOLETE("conditional expressions exclusively use shell-style notation; compatibility shims were removed!");
	}

	n_pstate_err_no = su_ERR_INVAL;
	NYD2_OU;
}

boole
mx_cndexp_parse(struct mx_cmd_arg_ctx const *cacp, boole log_on_error){
	struct a_cndexp_ctx cec;
	struct mx_cmd_arg *cap;
	u16 *toks, op;
	char const **argv;
	union {void *v; char const **pcc; char **pc; u16 *u;} p;
	boole rv;
	u32 i;
	NYD_IN;

	ASSERT(cacp->cac_no < U32_MAX);
	i = cacp->cac_no + 1;
	if(UZ_MAX / i <= sizeof(*argv) + sizeof(*toks)){
		if(log_on_error)
			n_err(_("conditional expression: overflow: too many arguments\n"));
		rv = TRUM1;
		goto jleave;
	}

	p.v = su_LOFI_ALLOC(i * (sizeof(*argv) + sizeof(*toks)));
	argv = p.pcc;
	p.pc += i;
	toks = p.u;
	p.pc -= i;

	STRUCT_ZERO(struct a_cndexp_ctx, &cec);
	cec.cec_argc = --i;
	cec.cec_argv_base = cec.cec_argv = argv;
	cec.cec_log_on_error = log_on_error;
	cec.cec_toks = toks;

	/* Convert anything we see to op's, with NONE being the default.
	 * Do not try to understand implicit arguments */
	for(cap = cacp->cac_arg; cap != NIL; *toks++ = op, cap = cap->ca_next){
		char *ap, c, c2, c3;

		ASSERT((cap->ca_ent_flags[0] & mx__CMD_ARG_DESC_TYPE_MASK) == mx_CMD_ARG_DESC_SHEXP);

		*argv++ = ap = cap->ca_arg.ca_str.s;
		op = a_CNDEXP_OP_NONE;

		/* Anything that saw changes / quotes cannot be syntax */
		if(cap->ca_arg_flags & n_SHEXP_STATE_CHANGE_MASK)
			continue;

		c = ap[0];
		if((c2 = ap[1]) == '\0'){
			switch(c){
			case '!': op = a_CNDEXP_OP_F_UNANOT; break;
			case '[': op = a_CNDEXP_OP_F_LBRACK; break;
			case ']': op = a_CNDEXP_OP_F_RBRACK; break;
			case '=': op = a_CNDEXP_OP_8B_EQ; break; /* sh(1) compat */
			case '<': op = a_CNDEXP_OP_8B_LT; break;
			case '>': op = a_CNDEXP_OP_8B_GT; break;
			}
		}else if((c3 = ap[2]) == '\0'){
			switch(c){
			case '&': if(c2 == '&') op = a_CNDEXP_OP_F_AND; break;
			case '|': if(c2 == '|') op = a_CNDEXP_OP_F_OR; break;
			case '-':
juna_hyphen_mod:
				switch(c2){
				case 'N': op |= a_CNDEXP_OP_N; break;
				case 'Z': op |= a_CNDEXP_OP_Z; break;
				case 'n': op |= a_CNDEXP_OP_n; break;
				case 'z': op |= a_CNDEXP_OP_z; break;
				case 'b': op |= a_CNDEXP_OP_FU_b; break;
				case 'c': op |= a_CNDEXP_OP_FU_c; break;
				case 'd': op |= a_CNDEXP_OP_FU_d; break;
				case 'e': op |= a_CNDEXP_OP_FU_e; break;
				case 'f': op |= a_CNDEXP_OP_FU_f; break;
				case 'G': op |= a_CNDEXP_OP_FU_G; break;
				case 'g': op |= a_CNDEXP_OP_FU_g; break;
				case 'k': op |= a_CNDEXP_OP_FU_k; break;
				case 'L': op |= a_CNDEXP_OP_FU_L; break;
				case 'O': op |= a_CNDEXP_OP_FU_O; break;
				case 'p': op |= a_CNDEXP_OP_FU_p; break;
				case 'r': op |= a_CNDEXP_OP_FU_r; goto jfX_check;
				case 's': op |= a_CNDEXP_OP_FU_s; break;
				case 'S': op |= a_CNDEXP_OP_FU_S; break;
				case 't': op |= a_CNDEXP_OP_FU_t; goto jfX_check;
				case 'u': op |= a_CNDEXP_OP_FU_u; break;
				case 'w': op |= a_CNDEXP_OP_FU_w; goto jfX_check;
				case 'x': op |= a_CNDEXP_OP_FU_x; goto jfX_check;
				}
				break;
			default:
				if(c2 != '?')
					goto j8b;
				c2 = '\0';
				switch(c){
				case '=': op = a_CNDEXP_OP_8B_EQ | a_CNDEXP_OP_F_CASE; break; /* sh(1) compat */
				case '<': op = a_CNDEXP_OP_8B_LT | a_CNDEXP_OP_F_CASE; break;
				case '>': op = a_CNDEXP_OP_8B_GT | a_CNDEXP_OP_F_CASE; break;
				}
				break;
			}
		}else if(c == '-'){
			/* Unary with modifier? */
			if(c3 == '?'){
				i = 3;
				goto jmod_satu;
			}

			if((c = ap[3]) != '\0'){
				if(c != '?')
					continue;
				i = 4;
jmod_satu:
				op |= a_CNDEXP_OP_F_SATURATED;
				if(ap[i] != '\0' && !su_cs_starts_with_case("saturated", &ap[i]))
					op |= a_CNDEXP_OP_F_ERR;
				if(c3 == '?')
					goto juna_hyphen_mod;
			}

			switch(c2){
			case 'l':
				if(c3 == 't') {op |= a_CNDEXP_OP_INT_lt; break;}
				if(c3 == 'e') {op |= a_CNDEXP_OP_INT_le; break;}
				break;
			case 'e':
				if(c3 == 'q') {op |= a_CNDEXP_OP_INT_eq; break;}
				if(c3 == 'f') {op |= a_CNDEXP_OP_FB_ef; goto jfX_check;}
				break;
			case 'n':
				if(c3 == 'e') {op |= a_CNDEXP_OP_INT_ne; break;}
				if(c3 == 't') {op |= a_CNDEXP_OP_FB_nt; goto jfX_check;}
				break;
			case 'g':
				if(c3 == 'e') {op |= a_CNDEXP_OP_INT_ge; break;}
				if(c3 == 't') {op |= a_CNDEXP_OP_INT_gt; break;}
				break;
			case 'o':
				if(c3 == 't'){
					op |= a_CNDEXP_OP_FB_ot;
jfX_check:
					if(op & a_CNDEXP_OP_F_SATURATED)
						op |= a_CNDEXP_OP_F_ERR;
				}
				break;
			}
		}else if(c3 == '?'){
			op |= a_CNDEXP_OP_F_CASE;
			if(ap[3] != '\0' && !su_cs_starts_with_case("case-insensitive", &ap[3]))
				op |= a_CNDEXP_OP_F_ERR;
j8b:
			switch(c){
			case '<': if(c2 == '=') op |= a_CNDEXP_OP_8B_LE; break;
			case '=':
				switch(c2){
				case '=': op |= a_CNDEXP_OP_8B_EQ; break;
				case '%': op |= a_CNDEXP_OP_8B_SUB; break;
#ifdef mx_HAVE_REGEX
				case '~': op |= a_CNDEXP_OP_RE_MATCH; break;
#endif
				}
				break;
			case '!':
				switch(c2){
				case '=': op |= a_CNDEXP_OP_8B_NE; break;
				case '%': op |= a_CNDEXP_OP_8B_NSUB; break;
#ifdef mx_HAVE_REGEX
				case '~': op |= a_CNDEXP_OP_RE_NMATCH; break;
#endif
				}
				break;
			case '>': if(c2 == '=') op |= a_CNDEXP_OP_8B_GE; break;
			}
		}
	}
	*argv = NIL;
	*toks = a_CNDEXP_OP_NONE;

	rv = a_cndexp_expr(&cec, 0, FAL0);

	su_LOFI_FREE(p.v);

jleave:
	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CNDEXP
/* s-itt-mode */
