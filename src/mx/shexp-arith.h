/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Signed 64-bit sh(1)ell-style $(( ARITH ))metic expression evaluator.
 *@ POW2 bases are parsed as unsigned, operation overflow -> limit constant(s),
 *@ saturated mode is not supported, division by zero is handled via error.
 *@ The expression length limit is ~100.000.000 on 32-bit, U32_MAX otherwise.
 *@ After reading on Dijkstra's two stack algorithm, as well as bash:expr.c.
 *@ Most heavily inspired by busybox -- conclusion: the Dijkstra algorithm
 *@ scales very badly to ternary as are used to implement conditionals and
 *@ their ignored sub-expressions.
 *@
 *@ #define's:
 *@ - a_SHEXP_ARITH_COMPAT_SHIMS: for inclusion in other code bases, setting
 *@  this defines most necessary compat macros.
 *@  We still need s64, u64, S64_MIN, savestr(CP) <> strdup(3) that does not
 *@  return NIL (only with _ERROR_TRACK).  Plus stdint.h, ctype.h, string.h.
 *@  We need su_idec_cp(), su_ienc_s64(), n_var_vlook() and n_var_vset().
 *@  We need su_IDEC_STATE_EMASK (= 1) and su_IDEC_STATE_CONSUMED (= 2), e.g.:
 *@    errno = 0;
 *@    res = strto_arith_t(cbuf, (char**)endptr_or_nil);
 *@    rv = 0;
 *@    if(errno == 0){
 *@      if(**endptr_or_nil == '\0')
 *@        rv = su_IDEC_STATE_CONSUMED;
 *@    }else{
 *@      rv = su_IDEC_STATE_EMASK;
 *@      res = 0;
 *@    }
 *@    *S(s64*,resp) = res;
 *@ - a_SHEXP_ARITH_COOKIE: adds struct a_shexp_arith_ctx:sac_cookie, and
 *@   a cookie arg to a_shexp_arith_eval().
 *@ - a_SHEXP_ARITH_ERROR_TRACK: add "char **error_track_or_nil" to
 *@   a_shexp_arith_eval(), and according error stack handling, so that users
 *@   can be given hint where an error occurred.  ("Three stack algorithm.")
 *
 * Copyright (c) 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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

/* Tracking of error location in input */
#ifdef a_SHEXP_ARITH_ERROR_TRACK
# undef a_SHEXP_ARITH_ERROR_TRACK
# define a_SHEXP_ARITH_ERROR_TRACK(X) X
#else
# define a_SHEXP_ARITH_ERROR_TRACK(X)
#endif

/* IFS whitespace removal */
#undef a_SHEXP_ARITH_IFS
#ifdef mx_SOURCE
# define a_SHEXP_ARITH_IFS(X) X
#else
# define a_SHEXP_ARITH_IFS(X)
#endif

/* (Most necessary) Compat shims */
#ifdef a_SHEXP_ARITH_COMPAT_SHIMS
# define boole bool
#  define FAL0 false
#  define TRU1 true
# define u8 uint8_t
# define u16 uint16_t
# define u32 uint32_t
# define U32_MAX UINT32_MAX
# define ul unsigned long
# define up uintptr_t
# define UZ_MAX SIZE_MAX
# define uz size_t
# define a_SHEXP_ISVARC(C) ((C) == '_' || isalnum(S(unsigned char,C)))
# define a_SHEXP_ISVARC_BAD1ST(C) su_cs_is_digit(C)
# define a_SHEXP_ISVARC_BADNST(C) FAL0
# define ASSERT(X)
# define ASSERT_NYD_EXEC(X,Y)
# define BITENUM_IS(X,Y) X
# define CONCAT(S1,S2) su__CONCAT_1(S1, S2)
#  define su__CONCAT_1(S1,S2) su__CONCAT_2(S1, S2)
#  define su__CONCAT_2(S1,S2) S1 ## S2
# define DBGX(X)
# define FALLTHRU
# define N_(X) X
# define NIL NULL
# define NYD_IN S(void,0)
# define NYD2_IN S(void,0)
# define NYD_OU S(void,0)
# define NYD2_OU S(void,0)
# define P2UZ(X) S(size_t,X)
# define S(X,Y) ((X)(Y))
# define ALIGN_Z(X) ((sizeof(X) + 7) & ~7)
# define su_COMMA ,
# define su_cs_cmp(X,Y) strcmp(X, Y)
# define su_cs_is_digit(X) isdigit(S(unsigned char,X))
# define su_cs_is_space(X) isspace(S(unsigned char,X))
# define su_empty ""
# define su_IDEC_STATE_EBASE 0 /* (could cause $CC optimiz.) */
# define su_IENC_BUFFER_SIZE 80u
# define su_LOFI_ALLOC(X) alloca(X)
# define su_LOFI_FREE(X)
# define su_mem_move(X,Y,Z) memmove(X, Y, Z)
# define STRUCT_ZERO(X,Y) memset(Y, 0, sizeof(X))
# define UNLIKELY(X) X
# define UNUSED(X) S(void,X)
# if LONG_MAX - 1 > 0x7FFFFFFFl - 1
#  define su_64(X) X
# else
#  define su_64(X)
# endif
#endif /* a_SHEXP_ARITH_COMPAT_SHIMS */

/* -- >8 -- 8< -- */

#if 1
# define a_SHEXP_ARITH_DBG 0
# define a_SHEXP_ARITH_L(X)
#else
# define a_SHEXP_ARITH_DBG 1
# define a_SHEXP_ARITH_L(X) a_shexp__arith_log X
#endif

/* We parse with base 0: set _RESCAN to allow "I='  -10';$((10#$I))" */
#define a_SHEXP_ARITH_IDEC_MODE (su_IDEC_MODE_SIGNED_TYPE |\
	su_IDEC_MODE_POW2BASE_UNSIGNED | su_IDEC_MODE_LIMIT_NOERROR |\
	su_IDEC_MODE_BASE0_NUMBER_SIGN_RESCAN)

enum a_shexp_arith_error{
	a_SHEXP_ARITH_ERR_NONE,
	a_SHEXP_ARITH_ERR_NOMEM, /* Out of memory */
	a_SHEXP_ARITH_ERR_SYNTAX, /* General syntax error */
	a_SHEXP_ARITH_ERR_ASSIGN_NO_VAR, /* Assignment without variable */
	a_SHEXP_ARITH_ERR_DIV_BY_ZERO,
	a_SHEXP_ARITH_ERR_EXP_INVALID, /* Invalid exponent */
	a_SHEXP_ARITH_ERR_NO_OP, /* Expected an argument here */
	a_SHEXP_ARITH_ERR_COND_NO_COLON, /* Incomplete ?: condition */
	a_SHEXP_ARITH_ERR_COND_PREC_INVALID, /* 1 ? VAR1 : VAR2 = 3 */
	a_SHEXP_ARITH_ERR_NAME_LOOP, /* Variable self-reference loop */
	a_SHEXP_ARITH_ERR_OP_INVALID /* Unknown operator */
};

/* Operators and precedences in increasing precedence order.
 * (The operator stack as such is u16: [OP_FLAGS |] (OP<<8) | PREC.) */
enum a_shexp_arith_ops{
#undef a_X
#define a_X(N,P,O) \
	CONCAT(a_SHEXP_ARITH_PREC_,N) = CONCAT(P,u),\
	CONCAT(a_SHEXP_ARITH_OP_,N) = (CONCAT(O,u) << 8) | CONCAT(a_SHEXP_ARITH_PREC_,N)

	a_X(PAREN_LEFT, 0, 0),

	a_X(COMMA, 1, 0),

	a_X(ASSIGN, 2, 0),
		a_X(ASSIGN_BIT_OR, 2, 1), a_X(ASSIGN_BIT_XOR, 2, 2), a_X(ASSIGN_BIT_AND, 2, 3),
		a_X(ASSIGN_SHIFT_LEFT, 2, 4), a_X(ASSIGN_SHIFT_RIGHT, 2, 5), a_X(ASSIGN_SHIFT_RIGHTU, 2, 6),
		a_X(ASSIGN_ADD, 2, 7), a_X(ASSIGN_SUB, 2, 8),
		a_X(ASSIGN_MUL, 2, 9), a_X(ASSIGN_DIV, 2, 10), a_X(ASSIGN_MOD, 2, 11),
		a_X(ASSIGN_EXP, 2, 12),

	a_X(COND, 3, 0), a_X(COND_COLON, 3, 1),

	a_X(OR, 4, 0),
	a_X(AND, 5, 0),
	a_X(BIT_OR, 6, 0),
	a_X(BIT_XOR, 7, 0),
	a_X(BIT_AND, 8, 0),
	a_X(EQ, 9, 0), a_X(NE, 9, 1),
	a_X(LE, 10, 0), a_X(GE, 10, 1), a_X(LT, 10, 2), a_X(GT, 10, 3),
	a_X(SHIFT_LEFT, 11, 0), a_X(SHIFT_RIGHT, 11, 1), a_X(SHIFT_RIGHTU, 11, 2),
	a_X(ADD, 12, 0), a_X(SUB, 12, 1),
	a_X(MUL, 13, 0), a_X(DIV, 13, 1), a_X(MOD, 13, 2),
	a_X(EXP, 14, 0),

	/* Further operators are unary, pre- or postfix */
	a_SHEXP_ARITH_PREC_UNARY = 15,
	a_SHEXP_ARITH_PREC_PREFIX = 16,
	a_SHEXP_ARITH_PREC_POSTFIX = 18,

	a_X(UNARY_NOT, 15, 0), a_X(UNARY_BIT_NOT, 15, 1),
	a_X(PREFIX_INC, 16, 0), a_X(PREFIX_DEC, 16, 1),
	a_X(UNARY_PLUS, 17, 1), a_X(UNARY_MINUS, 17, 0),
	a_X(POSTFIX_INC, 18, 0), a_X(POSTFIX_DEC, 18, 1),

	/* Beyond operator profanity; the first "is a number" */
	a_SHEXP_ARITH_PREC_SKY = 19,
	a_X(NUM, 19, 0), a_X(PAREN_RIGHT, 19, 1),

#undef a_X
};

enum arith_op_flags{
	/* Mask off operator and precision */
	a_SHEXP_ARITH_OP_MASK = 0x1FFF,
	a_SHEXP_ARITH_OP_FLAG_COND_SAW_COLON = 1u<<13,
	a_SHEXP_ARITH_OP_FLAG_OUTER_WHITEOUT = 1u<<14,
	a_SHEXP_ARITH_OP_FLAG_WHITEOUT = 1u<<15,
	a_SHEXP_ARITH_OP_FLAG_WHITE_MASK = a_SHEXP_ARITH_OP_FLAG_OUTER_WHITEOUT | a_SHEXP_ARITH_OP_FLAG_WHITEOUT,
	a_SHEXP_ARITH_OP_FLAG_MASK = a_SHEXP_ARITH_OP_FLAG_COND_SAW_COLON | a_SHEXP_ARITH_OP_FLAG_WHITE_MASK
};

struct a_shexp_arith_name_stack{
	struct a_shexp_arith_name_stack *sans_last;
	char const *sans_var;
};

struct a_shexp_arith_val{
	s64 sav_val;
	char *sav_var; /* Named variable or NIL */
};

struct a_shexp_arith_stack{
	struct a_shexp_arith_val *sas_nums;
	struct a_shexp_arith_val *sas_nums_top;
	u16 *sas_ops;
	u16 *sas_ops_top;
	a_SHEXP_ARITH_ERROR_TRACK(
		char **sas_error_track;
		char **sas_error_track_top;
	)
};

struct a_shexp_arith_ctx{
	u32 sac_error;
	boole sac_have_error_track;
	u8 sac__pad[3];
	s64 sac_rv;
	struct a_shexp_arith_stack *sac_stack;
	struct a_shexp_arith_name_stack *sac_name_stack;
	a_SHEXP_ARITH_ERROR_TRACK( char **sac_error_track_or_nil; )
	a_SHEXP_ARITH_IFS( char const *sac_ifs_ws; )
#ifdef a_SHEXP_ARITH_COOKIE
	a_SHEXP_ARITH_COOKIE sac_cookie;
#endif
};

/* Sort by ~expected usage -- however, longest first if ambiguous!
 * Follow busybox, save space by compressing data in char[] not struct[]!
 * (XXX Instead use 1-st byte jump table like for commands) */
static char const a_shexp_arith_op_toks[] = {
#undef a_X
#define a_X(X) S(char,(CONCAT(a_SHEXP_ARITH_OP_,X) & 0xFF00u) >> 8), S(char,CONCAT(a_SHEXP_ARITH_PREC_,X))

		'+','+','\0', a_X(POSTFIX_INC),
		'+','=','\0', a_X(ASSIGN_ADD),
	'+','\0', a_X(ADD),
		'-','-','\0', a_X(POSTFIX_DEC),
		'-','=','\0', a_X(ASSIGN_SUB),
	'-','\0', a_X(SUB),
			'*','*','=','\0', a_X(ASSIGN_EXP),
		'*','*','\0', a_X(EXP),
		'*','=','\0', a_X(ASSIGN_MUL),
	'*','\0', a_X(MUL),
		'/','=','\0', a_X(ASSIGN_DIV),
	'/','\0', a_X(DIV),
		'%','=','\0', a_X(ASSIGN_MOD),
	'%','\0', a_X(MOD),
		'|','|','\0', a_X(OR),
		'|','=','\0', a_X(ASSIGN_BIT_OR),
	'|','\0', a_X(BIT_OR),
		'^','=','\0', a_X(ASSIGN_BIT_XOR),
	'^','\0', a_X(BIT_XOR),
		'&','&','\0', a_X(AND),
		'&','=','\0', a_X(ASSIGN_BIT_AND),
	'&','\0', a_X(BIT_AND),
		'<','<','=',0, a_X(ASSIGN_SHIFT_LEFT),
	'<','<','\0', a_X(SHIFT_LEFT),
		'>','>','>','=',0, a_X(ASSIGN_SHIFT_RIGHTU),
	'>','>','>','\0', a_X(SHIFT_RIGHTU),
		'>','>','=',0, a_X(ASSIGN_SHIFT_RIGHT),
	'>','>','\0', a_X(SHIFT_RIGHT),

	'~','\0', a_X(UNARY_BIT_NOT),
		'!','=','\0', a_X(NE),
	'!','\0', a_X(UNARY_NOT),

	')','\0', a_X(PAREN_RIGHT),
	'(','\0', a_X(PAREN_LEFT),
	',','\0', a_X(COMMA),

	'<','=','\0', a_X(LE),
	'>','=','\0', a_X(GE),
	'=','=','\0', a_X(EQ),
	'<','\0', a_X(LT),
	'>','\0', a_X(GT),
	'=','\0', a_X(ASSIGN),

	'?','\0', a_X(COND),
	':','\0', a_X(COND_COLON),

	'\0'
#undef a_X
};

/* Our "public" entry point.  exp_buf can be NIL if exp_len is 0, it need not
 * be NUL terminated (stop for NUL or out of length).
 * Upon error *error_track_or_nil is set to a "newly allocated" string that
 * points to where parse stopped, or NIL upon initial setup failure. */
static enum a_shexp_arith_error a_shexp_arith_eval(
#ifdef a_SHEXP_ARITH_COOKIE
		a_SHEXP_ARITH_COOKIE cookie,
#endif
		s64 *resp, char const *exp_buf, uz exp_len
		a_SHEXP_ARITH_ERROR_TRACK( su_COMMA char **error_track_or_nil ));

static void a_shexp__arith_eval(struct a_shexp_arith_ctx *self, char const *exp_buf, uz exp_len);

/* Count non-WS as well as normalized WS ([:"space":]+ -> ' ') in exp_buf, return count.
 * If store!=NIL, also copy normalization.  An all-WS exp_buf returns 0 */
static uz a_shexp__arith_ws_squeeze(struct a_shexp_arith_ctx *self, char const *exp_buf, uz exp_len, char *store_or_nil);

/* Resolve and evaluate the "self-contained string" savp->sav_var.  Take care to avoid name lookup loops */
static boole a_shexp__arith_val_eval(struct a_shexp_arith_ctx *self, struct a_shexp_arith_val *savp);

/* Work top of the stack, which may pop & push etc */
static boole a_shexp__arith_op_apply(struct a_shexp_arith_ctx *self);

static boole a_shexp__arith_op_apply_colons(struct a_shexp_arith_ctx *self);

#if a_SHEXP_ARITH_DBG
static void a_shexp__arith_log(char const *fmt, ...);
#endif

static enum a_shexp_arith_error
a_shexp_arith_eval(
#ifdef a_SHEXP_ARITH_COOKIE
		a_SHEXP_ARITH_COOKIE cookie,
#endif
		s64 *resp, char const *exp_buf, uz exp_len
		a_SHEXP_ARITH_ERROR_TRACK( su_COMMA char **error_track_or_nil )){
	struct a_shexp_arith_stack sas_stack;
	struct a_shexp_arith_ctx self;
	NYD_IN;

	a_SHEXP_ARITH_L(("> arith_eval %zu <%.*s>\n",
		exp_len, S(int,exp_len != UZ_MAX ? exp_len : su_cs_len(exp_buf)), exp_buf));

	a_SHEXP_ARITH_ERROR_TRACK(DBGX(
		if(error_track_or_nil != NIL)
			*error_track_or_nil = NIL;
	));

	STRUCT_ZERO(struct a_shexp_arith_ctx, &self);
#ifdef a_SHEXP_ARITH_COOKIE
	self.sac_cookie = cookie;
#endif
	a_SHEXP_ARITH_ERROR_TRACK(
		if((self.sac_error_track_or_nil = error_track_or_nil) != NIL)
			self.sac_have_error_track = TRU1;
	)

	ASSERT_NYD_EXEC(resp != NIL, self.sac_error = a_SHEXP_ARITH_ERR_NO_OP);
	DBGX( *resp = 0; )
	ASSERT_NYD_EXEC(exp_len == 0 || exp_buf != NIL, self.sac_error = a_SHEXP_ARITH_ERR_NO_OP);

	a_SHEXP_ARITH_IFS( self.sac_ifs_ws = ok_vlook(ifs_ws); )
	self.sac_stack = &sas_stack;
	a_shexp__arith_eval(&self, exp_buf, exp_len);
	*resp = self.sac_rv;

	a_SHEXP_ARITH_L(("< arith_eval %zu <%.*s> -> <%lld> ERR<%u>\n",
		exp_len, S(int,exp_len != UZ_MAX ? exp_len : su_cs_len(exp_buf)),
		exp_buf, self.sac_rv, self.sac_error));

	NYD_OU;
	return S(enum a_shexp_arith_error,self.sac_error);
}

static void
a_shexp__arith_eval(struct a_shexp_arith_ctx *self, char const *exp_buf, uz exp_len){
	char *ep, *varp, *cp, c;
	u16 lop;
	struct a_shexp_arith_stack *sasp;
	void *mem_p;
	NYD2_IN;

	a_SHEXP_ARITH_L((" > _arith_eval %zu <%.*s>\n",
		exp_len, S(int,exp_len != UZ_MAX ? exp_len : su_cs_len(exp_buf)), exp_buf));

	mem_p = NIL;
	sasp = self->sac_stack;

	/* Create a single continuous allocation for anything */
	/* C99 */{
		union {void *v; char *c;} p;
		uz i, j, o, a;

		/* Done for empty expression */
		if((i = a_shexp__arith_ws_squeeze(self, exp_buf, exp_len, NIL)) == 0)
			goto jleave;

		/* Overflow check: since arithmetic expressions are rarely long enough
		 * to come near this limit, xxx laxe & fuzzy, not exact; max U32_MAX! */
		if(su_64( i >= U32_MAX || ) i++ >= UZ_MAX / 2)
			goto jenomem;
		if(i >= ((UZ_MAX - (i a_SHEXP_ARITH_ERROR_TRACK( * 2 ))) /
					((ALIGN_Z(sizeof(*sasp->sas_nums)) + ALIGN_Z(sizeof(*sasp->sas_ops) * 2))
					  a_SHEXP_ARITH_ERROR_TRACK( + sizeof(*sasp->sas_error_track) * 2 ))
				)){
jenomem:
			self->sac_error = a_SHEXP_ARITH_ERR_NOMEM;
			goto jleave;
		}

		++i;
		j = ALIGN_Z(sizeof(*sasp->sas_nums) * (i >> 1));
		o = ALIGN_Z(sizeof(*sasp->sas_ops) * i);
		a = j + o + a_SHEXP_ARITH_ERROR_TRACK( (sizeof(*sasp->sas_error_track) * i) + )
				1 + (i a_SHEXP_ARITH_ERROR_TRACK( * 2 ));
		mem_p = p.v = su_LOFI_ALLOC(a);
		if(p.v == NIL){
			/* (For MX LOFI has _MUSTFAIL set though) */
			self->sac_error = a_SHEXP_ARITH_ERR_NOMEM;
			goto jleave;
		}
		sasp->sas_nums = sasp->sas_nums_top = S(struct a_shexp_arith_val*,p.v);
		p.c += j;
		sasp->sas_ops = sasp->sas_ops_top = S(u16*,p.v);
		p.c += o;
		a_SHEXP_ARITH_ERROR_TRACK(
			sasp->sas_error_track_top = sasp->sas_error_track = S(char**,p.v);
			p.c += sizeof(*sasp->sas_error_track) * i;
		)

		ep = ++p.c; /* ++ to copy varnames in !_ARITH_ERROR cases */
		i = a_shexp__arith_ws_squeeze(self, exp_buf, exp_len, ep);
		varp = &ep[
#if 0 a_SHEXP_ARITH_ERROR_TRACK( + 1 )
				i + 1
#else
				-1
#endif
			];

		a_SHEXP_ARITH_L((" ! _arith_eval ALLOC <%lu> nums=%p (%lu) ops=%p varp=%p %lu <%s>\n",
			S(ul,a), sasp->sas_nums, S(ul,j / sizeof(*sasp->sas_nums)),
			sasp->sas_ops, varp, S(ul,i), ep));
	}

	/* Start with a left paren */
	a_SHEXP_ARITH_ERROR_TRACK( *sasp->sas_error_track_top++ = ep; )
	*sasp->sas_ops_top++ = lop = a_SHEXP_ARITH_OP_PAREN_LEFT;

	for(;;) Jouter:{
		u16 op;

		a_SHEXP_ARITH_L((" = _arith_eval TICK LOP <0x%02X %u> nums=%lu ops=%lu DATA %lu <%s>\n",
			lop, lop & 0xFF, S(ul,sasp->sas_nums_top - sasp->sas_nums),
			S(ul,sasp->sas_ops_top - sasp->sas_ops), S(ul,su_cs_len(ep)), ep));

		if(*ep == '\0'){
			/* At the end of the expression pop anything left.
			 * Assume we have read PAREN_RIGHT */
			if(exp_buf != NIL){
				exp_buf = NIL;
				op = a_SHEXP_ARITH_OP_PAREN_RIGHT;
				/* Could fail for "1)" (how could that enter at all?)
				 * ASSERT(sasp->sas_ops_top > sasp->sas_ops);
				 * Can only be a syntax error! */
				if(sasp->sas_ops_top == sasp->sas_ops){
					self->sac_error = a_SHEXP_ARITH_ERR_SYNTAX;
					break;
				}
				goto jtok_go;
			}

			/* After PAREN_RIGHT, we must be finished */
			if(sasp->sas_nums_top != &sasp->sas_nums[1])
				self->sac_error = a_SHEXP_ARITH_ERR_SYNTAX;
			break;
		}

		/* Skip (normalized) WS now */
		if(*ep == ' ')
			++ep;
		ASSERT(!su_cs_is_space(*ep));

		/* A number? */
		if(su_cs_is_digit(*ep)){
			BITENUM_IS(u32,su_idec_state) is;

			is = su_idec_cp(&sasp->sas_nums_top->sav_val, ep, 0, a_SHEXP_ARITH_IDEC_MODE, S(char const**,&ep));
			if((is &= su_IDEC_STATE_EMASK) && is != su_IDEC_STATE_EBASE)
				sasp->sas_nums_top->sav_val = 0;
			sasp->sas_nums_top->sav_var = NIL;

			++sasp->sas_nums_top;
			lop = a_SHEXP_ARITH_OP_NUM;
			a_SHEXP_ARITH_L((" + _arith_eval NUM <%lld>\n", sasp->sas_nums_top[-1].sav_val));
			continue;
		}

		/* Is it a variable name? */
		for(cp = ep; (c = *cp, a_SHEXP_ISVARC(c)); ++cp)
			if(cp == ep && a_SHEXP_ISVARC_BAD1ST(c))
				break;

		if(cp != ep){
			/* Could be an invalid name */
			for(;;){
				c = cp[-1];
				/* (For example, hyphen-minus as a sh(1) extension!) */
				if(!a_SHEXP_ISVARC_BADNST(c))
					break;
				if(--cp == ep){
					self->sac_error = a_SHEXP_ARITH_ERR_SYNTAX;
					goto jleave;
				}
			}

			/* Copy over to pre-allocated var storage */
			/* C99 */{
				uz i;

				i = P2UZ(cp - ep);
				/* (move not copy for !_ARITH_ERROR cases (says ISO C?)) */
				su_mem_move(sasp->sas_nums_top->sav_var = varp, ep, i);
				varp += i;
				*varp++ = '\0';
			}
			ep = cp;

			++sasp->sas_nums_top;
			lop = a_SHEXP_ARITH_OP_NUM;

			a_SHEXP_ARITH_L((" + _arith_eval VAR <%s>\n", sasp->sas_nums_top[-1].sav_var));
			continue;
		}

		/* An operator.
		 * We turn prefix operators to multiple unary plus/minus if
		 * not pre- or post-attached to a variable name (++10 -> + + 10).
		 * (We adjust postfix to prefix below) */
		if((ep[0] == '+' || ep[0] == '-') && (ep[1] == ep[0])){
			if(sasp->sas_nums_top == sasp->sas_nums || sasp->sas_nums_top[-1].sav_var == NIL){
				if((c = ep[2]) == ' ')
					c = ep[3];

				if(c != '\0' && (!a_SHEXP_ISVARC(c) || a_SHEXP_ISVARC_BAD1ST(c))){
					op = (ep[0] == '+') ? a_SHEXP_ARITH_OP_ADD : a_SHEXP_ARITH_OP_SUB;
					++ep;
					a_SHEXP_ARITH_L((" + _arith_eval OP PREFIX INC/DEC SPLIT <%c%c> -> <%c>\n",
						ep[0], ep[0], ep[0]));
					goto jtok_go;
				}
			}
		}

		/* Operator search */
		/* C99 */{
			char const *tokp;

			/* 3=NUL+OP+PREC */
			for(tokp = a_shexp_arith_op_toks; *tokp != '\0'; tokp += 3){
				for(cp = ep;; ++tokp, ++cp){
					if(*tokp == '\0'){
						ep = cp;
						op = (S(u16,tokp[1]) << 8) | S(u8,tokp[2]);
						goto jtok_go;
					}else if(*tokp != *cp)
						break;
				}

				while(*tokp != '\0')
					++tokp;
			}
			self->sac_error = a_SHEXP_ARITH_ERR_OP_INVALID;
			goto jleave;
		}

jtok_go:/* C99 */{
		u8 prec;

		prec = op & 0xFF;
		a_SHEXP_ARITH_L((" + _arith_eval OP <0x%02X %u> LOP <0x%02X %u> nums=%lu ops=%lu %lu <%s>\n",
			op, prec, lop, lop & 0xFF, S(ul,sasp->sas_nums_top - sasp->sas_nums),
			S(ul,sasp->sas_ops_top - sasp->sas_ops), S(ul,su_cs_len(ep)), ep));

		if(op == a_SHEXP_ARITH_OP_UNARY_PLUS){
			a_SHEXP_ARITH_L((" + _arith_eval IGNORE UNARY PLUS\n"));
			continue;
		}

		/* Correct our understanding of what there is.
		 * Post grammar: VAR++ reduces to num */
		if((lop & 0xFF) == a_SHEXP_ARITH_PREC_POSTFIX){
			lop = a_SHEXP_ARITH_OP_NUM;
			a_SHEXP_ARITH_L((" + _arith_eval LOP POSTFIX REDUCED to NUM\n"));
		}
		/* Adjust some binary/postfix operators to make them flow */
		else if(lop != a_SHEXP_ARITH_OP_NUM){
			switch(op){
			case a_SHEXP_ARITH_OP_ADD:
				a_SHEXP_ARITH_L((" + _arith_eval OP ADJUST: IGNORE UNARY PLUS\n"));
				continue;
			case a_SHEXP_ARITH_OP_SUB:
				op = a_SHEXP_ARITH_OP_UNARY_MINUS;
				goto junapre;
			case a_SHEXP_ARITH_OP_POSTFIX_INC:
				op = a_SHEXP_ARITH_OP_PREFIX_INC;
				goto junapre;
			case a_SHEXP_ARITH_OP_POSTFIX_DEC:
				op = a_SHEXP_ARITH_OP_PREFIX_DEC;
junapre:
				prec = a_SHEXP_ARITH_PREC_PREFIX;
				a_SHEXP_ARITH_L((" + _arith_eval OP ADJUST TO UNARY/PREFIX\n"));
				break;
			}
		}
		/* Special: +10++VAR -> +10 + +VAR.  (Since we do handle +10++11
		 * correctly via "prefix split", we should also handle this) */
		else if(prec == a_SHEXP_ARITH_PREC_POSTFIX){
			ASSERT(lop == a_SHEXP_ARITH_OP_NUM);
			if((c = ep[0]) == ' ')
				c = ep[1];
			if(c != '\0' && (a_SHEXP_ISVARC(c) && !a_SHEXP_ISVARC_BAD1ST(c))){
				c = *--ep;
				op = (c == '+') ? a_SHEXP_ARITH_OP_ADD : a_SHEXP_ARITH_OP_SUB;
				prec = op & 0xFF;
				a_SHEXP_ARITH_L((" + _arith_eval OP POSTFIX INC/DEC SPLIT <%c%c> -> <%c>\n", c, c, c));
			}
		}

		/* Check whether we can work it a bit */
		if((prec > a_SHEXP_ARITH_PREC_PAREN_LEFT && prec < a_SHEXP_ARITH_PREC_UNARY) ||
				prec >= a_SHEXP_ARITH_PREC_SKY){
			if(lop != a_SHEXP_ARITH_OP_NUM){
				self->sac_error = a_SHEXP_ARITH_ERR_NO_OP;
				goto jleave;
			}

			/* Pop as much as possible */
			while(sasp->sas_ops_top != sasp->sas_ops){
				a_SHEXP_ARITH_ERROR_TRACK( --sasp->sas_error_track_top; )
				lop = *--sasp->sas_ops_top & a_SHEXP_ARITH_OP_MASK;

				a_SHEXP_ARITH_L((" + _arith_eval TRY POP - OP "
						"<0x%02X %u>, NEW LOP <0x%02X %u 0x%X> nums=%lu ops=%lu\n",
					op, op & 0xFF, lop, lop & 0xFF,
					(*sasp->sas_ops_top & a_SHEXP_ARITH_OP_FLAG_MASK),
					S(ul,sasp->sas_nums_top - sasp->sas_nums),
					S(ul,sasp->sas_ops_top - sasp->sas_ops)));

				/* Special-case parenthesis groups */
				if(op == a_SHEXP_ARITH_OP_PAREN_RIGHT){
					if(lop == a_SHEXP_ARITH_OP_PAREN_LEFT){
						ASSERT(sasp->sas_nums_top > sasp->sas_nums);
						/* Resolve VAR to NUM */
						if(sasp->sas_nums_top[-1].sav_var != NIL){
							ASSERT(!(*sasp->sas_ops_top & a_SHEXP_ARITH_OP_FLAG_WHITE_MASK));
							if(!a_shexp__arith_val_eval(self, &sasp->sas_nums_top[-1]))
								goto jleave;
						}
						sasp->sas_nums_top[-1].sav_var = NIL;
						a_SHEXP_ARITH_L((" + _arith_eval OP () RESOLVED <%lld>\n",
							sasp->sas_nums_top[-1].sav_val));
						lop = a_SHEXP_ARITH_OP_NUM;
						goto Jouter;
					}
				}else{
					u8 lprec;

					lprec = lop & 0xFF;

					/* */
					if(op == a_SHEXP_ARITH_OP_COND){
						u16 x;

						x = *sasp->sas_ops_top & a_SHEXP_ARITH_OP_FLAG_MASK;
						if(x & a_SHEXP_ARITH_OP_FLAG_WHITEOUT){
							x ^= a_SHEXP_ARITH_OP_FLAG_WHITEOUT;
							x |= a_SHEXP_ARITH_OP_FLAG_OUTER_WHITEOUT;
						}
						op |= x;

						/* Resolve as resolve can, need to assert our condition! */
						while(lprec > a_SHEXP_ARITH_PREC_COND){
							if(!a_shexp__arith_op_apply(self))
								goto jleave;
							a_SHEXP_ARITH_ERROR_TRACK( --sasp->sas_error_track_top; )
							lop = *--sasp->sas_ops_top & a_SHEXP_ARITH_OP_MASK;
							lprec = lop & 0xFF;
						}

						/* Evaluate condition assertion */
						ASSERT(sasp->sas_nums_top > sasp->sas_nums);
						--sasp->sas_nums_top;

						if(sasp->sas_nums_top->sav_var != NIL){
							if(!(op & a_SHEXP_ARITH_OP_FLAG_WHITE_MASK) &&
									!a_shexp__arith_val_eval(self, sasp->sas_nums_top))
								goto jleave;
							sasp->sas_nums_top->sav_var = NIL;
						}

						if(sasp->sas_nums_top->sav_val == 0)
							op |= a_SHEXP_ARITH_OP_FLAG_WHITEOUT;
						op |= *sasp->sas_ops_top & a_SHEXP_ARITH_OP_FLAG_MASK;

						/* Delay ternary: this ? op will last until we can resolve
						 * the entire condition, its number stack position is used
						 * as storage for the actual condition result */
						a_SHEXP_ARITH_ERROR_TRACK( ++sasp->sas_error_track_top; )
						++sasp->sas_ops_top;
						break;
					}else if(op == a_SHEXP_ARITH_OP_COND_COLON){
						uz recur;
						u16 *opsp, x;
						boole delay;

						delay = TRU1;

						/* Find our counterpart ? so we can toggle whiteout */
						opsp = sasp->sas_ops_top;
						for(recur = 1;; --opsp){
							if(opsp == sasp->sas_ops){
								self->sac_error = a_SHEXP_ARITH_ERR_SYNTAX;
								goto jleave;
							}

							x = *opsp & a_SHEXP_ARITH_OP_MASK;
							if(x == a_SHEXP_ARITH_OP_COND_COLON)
								++recur;
							else if(x == a_SHEXP_ARITH_OP_COND && --recur == 0){
								*opsp |= a_SHEXP_ARITH_OP_FLAG_COND_SAW_COLON;
								break;
							}
						}
						op |= *opsp & a_SHEXP_ARITH_OP_FLAG_MASK;
						op ^= a_SHEXP_ARITH_OP_FLAG_WHITEOUT;

						/* Resolve innermost condition asap.
						 * In "1?0?5:6:3", resolve innermost upon :3 */
						while(lprec > a_SHEXP_ARITH_PREC_PAREN_LEFT &&
								lprec != a_SHEXP_ARITH_PREC_COND){
							if(!a_shexp__arith_op_apply(self))
								goto jleave;
							a_SHEXP_ARITH_ERROR_TRACK( --sasp->sas_error_track_top; )
							lop = *--sasp->sas_ops_top & a_SHEXP_ARITH_OP_MASK;
							lprec = lop & 0xFF;
						}

						/* If at a COLON we have to resolve further, otherwise syntax
						 * error would happen for 1?2?3:6:7 (due to how Dijkstra's
						 * algorithm applies, and our squeezing of ?: constructs) */
						if(lop == a_SHEXP_ARITH_OP_COND_COLON){
							delay = FAL0;
							if(!a_shexp__arith_op_apply_colons(self))
								goto jleave;
							lop = *sasp->sas_ops_top & a_SHEXP_ARITH_OP_MASK;
						}

						if(lop != a_SHEXP_ARITH_OP_COND){
							self->sac_error = a_SHEXP_ARITH_ERR_SYNTAX;
							goto jleave;
						}

						if(delay){
							a_SHEXP_ARITH_ERROR_TRACK( ++sasp->sas_error_track_top; )
							++sasp->sas_ops_top;
						}
						a_SHEXP_ARITH_L((" + _arith_eval %sTERNARY ?:%s\n",
							(delay ? "DELAY " : su_empty),
							((op & a_SHEXP_ARITH_OP_FLAG_WHITE_MASK)
							 ? " WHITEOUT" : su_empty)));
						break;
					}
					/* Is this a right-associative operation? */
					else{
						boole doit;

						doit = FAL0;
						if(lprec < prec){
							doit = TRU1;
							a_SHEXP_ARITH_L((" + _arith_eval DELAY PRECEDENCE\n"));
						}else if(lprec == prec && prec == a_SHEXP_ARITH_PREC_ASSIGN){
							doit = TRU1;
							a_SHEXP_ARITH_L((" + _arith_eval DELAY RIGHT ASSOC\n"));
						}else if(lprec == a_SHEXP_ARITH_PREC_COND){
							if(lop == a_SHEXP_ARITH_OP_COND){
								doit = TRU1;
								a_SHEXP_ARITH_L((" + _arith_eval DELAY CONDITION\n"));
							}
							/* Without massive rewrite this is the location to detect
							 * in-whiteout precedence bugs as in
							 *   $((0?I1=10:(1?I3:I2=12)))
							 * which would be parsed like (1?I3:I2)=12 without error
							 * (different to 0?I3:I2=12) otherwise */
							else if(op != a_SHEXP_ARITH_OP_COMMA){
								self->sac_error = a_SHEXP_ARITH_ERR_COND_PREC_INVALID;
								goto jleave;
							}
						}

						if(doit){
							/* If we are about to delay and LHV is a VAR, expand that
							 * immediately to expand in correct order things like
							 *   I1=I2=10 I2=3; echo $((I1,I2))
							 *   I1=I2=10 I2=3; echo $((I1+=I2)) */
							if(sasp->sas_nums_top[-1].sav_var != NIL){
								if(op != a_SHEXP_ARITH_OP_ASSIGN &&
										!(*sasp->sas_ops_top & a_SHEXP_ARITH_OP_FLAG_WHITE_MASK) &&
										!a_shexp__arith_val_eval(self, &sasp->sas_nums_top[-1]))
									goto jleave;
								if(prec != a_SHEXP_ARITH_PREC_ASSIGN)
									sasp->sas_nums_top[-1].sav_var = NIL;
							}

							a_SHEXP_ARITH_ERROR_TRACK( ++sasp->sas_error_track_top; )
							++sasp->sas_ops_top;
							break;
						}
					}
				}

				/* */
				if(!a_shexp__arith_op_apply(self))
					goto jleave;

				if(lop == a_SHEXP_ARITH_OP_COND_COLON){
					ASSERT(sasp->sas_ops_top > sasp->sas_ops && &sasp->sas_ops_top[-1] > sasp->sas_ops);
					ASSERT((sasp->sas_ops_top[-1] & a_SHEXP_ARITH_OP_MASK) == a_SHEXP_ARITH_OP_COND);
					a_SHEXP_ARITH_ERROR_TRACK( --sasp->sas_error_track_top; )
					--sasp->sas_ops_top;
				}
			}

			/* Should have been catched in *ep==\0,exp_buf!=NIL case */
			ASSERT(op != a_SHEXP_ARITH_OP_PAREN_RIGHT);
		}

		/* Push this operator to the stack and remember it */
		a_SHEXP_ARITH_ERROR_TRACK( *sasp->sas_error_track_top++ = ep; )
		if(sasp->sas_ops_top > sasp->sas_ops && (op & 0xFF) != a_SHEXP_ARITH_PREC_COND)
			op |= sasp->sas_ops_top[-1] & a_SHEXP_ARITH_OP_FLAG_MASK;
		*sasp->sas_ops_top++ = op;
		lop = op & a_SHEXP_ARITH_OP_MASK;
		a_SHEXP_ARITH_L((" + _arith_eval OP PUSH <0x%02X %u> nums=%lu ops=%lu\n",
			op, (op & 0xFF), S(ul,sasp->sas_nums_top - sasp->sas_nums),
			S(ul,sasp->sas_ops_top - sasp->sas_ops)));
	}
	}

	self->sac_rv = sasp->sas_nums->sav_val;

jleave:
#if 0 a_SHEXP_ARITH_ERROR_TRACK( + 1 )
	if(self->sac_error != a_SHEXP_ARITH_ERR_NONE && mem_p != NIL && self->sac_have_error_track){
		if(sasp->sas_error_track_top > sasp->sas_error_track)
			--sasp->sas_error_track_top;
		*self->sac_error_track_or_nil = savestr(*sasp->sas_error_track_top);
	}
#endif

	if(mem_p != NIL)
		su_LOFI_FREE(mem_p);

	a_SHEXP_ARITH_L((" < _arith_eval <%lld> ERR<%d>\n", self->sac_rv, self->sac_error));
	NYD2_OU;
}

static uz
a_shexp__arith_ws_squeeze(struct a_shexp_arith_ctx *self, char const *exp_buf, uz exp_len, char *store_or_nil){
	a_SHEXP_ARITH_IFS( char const *ifs_ws; )
	char c;
	boole last_ws, ws;
	uz rv;
	NYD2_IN;
	UNUSED(self);

	rv = 0;
	a_SHEXP_ARITH_IFS( ifs_ws = self->sac_ifs_ws; )

	for(;; ++exp_buf, --exp_len){
		if(UNLIKELY(exp_len == 0) || UNLIKELY((c = *exp_buf) == '\0'))
			goto jleave;
		if(!(su_cs_is_space(c) a_SHEXP_ARITH_IFS( || su_cs_find_c(ifs_ws, c) != NIL )))
			break;
	}

	for(last_ws = FAL0;; ++exp_buf, --exp_len){
		if(UNLIKELY(exp_len == 0) || UNLIKELY((c = *exp_buf) == '\0'))
			break;

		ws = (su_cs_is_space(c) a_SHEXP_ARITH_IFS( || su_cs_find_c(ifs_ws, c) != NIL ));
		if(ws){
			if(last_ws)
				continue;
			c = ' ';
		}
		last_ws = ws;

		++rv;
		if(store_or_nil != NIL)
			*store_or_nil++ = c;
	}

	if(last_ws){
		--rv;
		if(store_or_nil != NIL)
			--store_or_nil;
	}

jleave:
	if(store_or_nil != NIL)
		*store_or_nil = '\0';

	NYD2_OU;
	return rv;
}

static boole
a_shexp__arith_val_eval(struct a_shexp_arith_ctx *self, struct a_shexp_arith_val *savp){
	struct a_shexp_arith_name_stack sans_stack, *sansp;
	struct a_shexp_arith_stack sas_stack, *sasp;
	char const *cp;
	NYD_IN;
	ASSERT(savp->sav_var != NIL);

	a_SHEXP_ARITH_L(("> _arith_val_eval %p <%s>\n", savp, savp->sav_var));

	savp->sav_val = 0;

	/* Also look in program environment XXX configurable? */
	cp = n_var_vlook(savp->sav_var, TRU1);
	if(cp == NIL)
		goto jleave;

	for(sansp = self->sac_name_stack; sansp != NIL; sansp = sansp->sans_last){
		if(!su_cs_cmp(sansp->sans_var, savp->sav_var)){
			self->sac_error = a_SHEXP_ARITH_ERR_NAME_LOOP;
			goto jleave;
		}
	}

	/* cp must be a self-contained expression.
	 * However, in most cases it solely consists of an integer, shortcut that */
	if(su_idec_cp(&savp->sav_val, cp, 0, a_SHEXP_ARITH_IDEC_MODE, NIL) & su_IDEC_STATE_CONSUMED){
		a_SHEXP_ARITH_L((" + _arith_val_eval NUM DIRECT <%lld>\n", savp->sav_val));
	}else{
		sasp = self->sac_stack;
		self->sac_stack = &sas_stack;

		sans_stack.sans_last = sansp = self->sac_name_stack;
		sans_stack.sans_var = savp->sav_var;
		self->sac_name_stack = &sans_stack;

		a_shexp__arith_eval(self, cp, UZ_MAX);
		savp->sav_val = self->sac_rv;
		/* .sav_var may be needed further on for updating purposes */

		self->sac_stack = sasp;
		self->sac_name_stack = sansp;
	}

	cp = NIL;
jleave:
	a_SHEXP_ARITH_L(("< _arith_val_eval %p <%s> <%lld> -> OK <%d>\n",
		savp, savp->sav_var, savp->sav_val, (cp == NIL && self->sac_error == a_SHEXP_ARITH_ERR_NONE)));

	NYD_OU;
	return (cp == NIL && self->sac_error == a_SHEXP_ARITH_ERR_NONE);
}

static boole
a_shexp__arith_op_apply(struct a_shexp_arith_ctx *self){
	struct a_shexp_arith_val *nums_top;
	u8 prec;
	u16 op;
	struct a_shexp_arith_stack *sasp;
	s64 val;
	boole rv, ign;
	NYD_IN;

	rv = FAL0;
	val = 0;
	sasp = self->sac_stack;
	op = *sasp->sas_ops_top & a_SHEXP_ARITH_OP_MASK;
	ign = ((*sasp->sas_ops_top & a_SHEXP_ARITH_OP_FLAG_WHITE_MASK) != 0);

	a_SHEXP_ARITH_L(("  > _arith_op_apply %s<0x%02X %u> nums_top=%p (%lu) ops_top=%p (%lu)\n",
		(ign ? "WHITEOUT " : su_empty), op, (op & 0xFF), sasp->sas_nums_top,
		S(ul,sasp->sas_nums_top - sasp->sas_nums),
		sasp->sas_ops_top, S(ul,sasp->sas_ops_top - sasp->sas_ops)));

	/* At least one argument is always needed */
	if((nums_top = sasp->sas_nums_top) == sasp->sas_nums){
		self->sac_error = a_SHEXP_ARITH_ERR_NO_OP;
		goto jleave;
	}
	--nums_top;

	/* Resolve name ([R]VAL) to value as necessary */
	if(!ign && nums_top->sav_var != NIL && !a_shexp__arith_val_eval(self, nums_top))
		goto jleave;

	val = nums_top->sav_val;
	prec = op & 0xFF;

	/* Not a binary operator? */
	if(prec >= a_SHEXP_ARITH_PREC_UNARY && prec < a_SHEXP_ARITH_PREC_SKY){
		if(ign)
			goto jquick;

		switch(op){
		default: break;
		case a_SHEXP_ARITH_OP_UNARY_NOT: val = !val; break;
		case a_SHEXP_ARITH_OP_UNARY_BIT_NOT: val = ~val; break;
		case a_SHEXP_ARITH_OP_UNARY_MINUS: val = -val; break;
		case a_SHEXP_ARITH_OP_PREFIX_INC: FALLTHRU
		case a_SHEXP_ARITH_OP_POSTFIX_INC: ++val; break;
		case a_SHEXP_ARITH_OP_PREFIX_DEC: FALLTHRU
		case a_SHEXP_ARITH_OP_POSTFIX_DEC: --val; break;
		}
	}else if(op == a_SHEXP_ARITH_OP_COND){
		if(!(*sasp->sas_ops_top & a_SHEXP_ARITH_OP_FLAG_COND_SAW_COLON)){
			self->sac_error = a_SHEXP_ARITH_ERR_COND_NO_COLON;
			goto jleave;
		}
		goto jquick;
	}else if(op == a_SHEXP_ARITH_OP_COND_COLON){
		ASSERT(sasp->sas_ops_top > sasp->sas_ops);
		ASSERT(nums_top > sasp->sas_nums);

		if(!ign){
			/* Move the ternary value over to LHV where we find it as a result,
			 * and ensure LHV's name is forgotten so not to evaluate it (for
			 * example in 0?I1:I2 I1 would be evaluated when resolving the virtual
			 * outer group, because it still exists on number stack) */
			nums_top[-1].sav_val = nums_top[0].sav_val;
			nums_top[-1].sav_var = NIL;
		}
		DBGX( else val = -1; )

		sasp->sas_nums_top = nums_top;

		if((sasp->sas_ops_top[-1] & a_SHEXP_ARITH_OP_MASK) == a_SHEXP_ARITH_OP_COND_COLON){
			a_SHEXP_ARITH_ERROR_TRACK( --sasp->sas_error_track_top; )
			--sasp->sas_ops_top;
			if(!a_shexp__arith_op_apply_colons(self))
				goto jleave;
			ASSERT(sasp->sas_nums_top > sasp->sas_nums);
			if(!ign)
				sasp->sas_nums_top[-1].sav_val = val;
		}
	}else{
		/* Binaries need two numbers: one is popped, the other replaced */
		s64 rval;

		if(nums_top == sasp->sas_nums){
			self->sac_error = a_SHEXP_ARITH_ERR_NO_OP;
			goto jleave;
		}
		sasp->sas_nums_top = nums_top--;

		if(ign)
			goto jquick;

		/* Resolve LHV as necessary */
		if(op != a_SHEXP_ARITH_OP_ASSIGN && nums_top->sav_var != NIL && !a_shexp__arith_val_eval(self, nums_top))
			goto jleave;

		rval = val;
		val = nums_top->sav_val; /* (may be bogus for assign, fixed soon) */

		/* In precedence order (excluding assignments) */
		switch(op){
		default: break;
		case a_SHEXP_ARITH_OP_COMMA: FALLTHRU

		case a_SHEXP_ARITH_OP_ASSIGN: val = rval; break;

		case a_SHEXP_ARITH_OP_OR: val = (val != 0 || rval != 0); break;
		case a_SHEXP_ARITH_OP_AND: val = (val != 0 && rval != 0); break;

		case a_SHEXP_ARITH_OP_BIT_OR: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_BIT_OR: val |= rval; break;
		case a_SHEXP_ARITH_OP_BIT_XOR: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_BIT_XOR: val ^= rval; break;
		case a_SHEXP_ARITH_OP_BIT_AND: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_BIT_AND: val &= rval; break;

		case a_SHEXP_ARITH_OP_EQ: val = (val == rval); break;
		case a_SHEXP_ARITH_OP_NE: val = (val != rval); break;

		case a_SHEXP_ARITH_OP_LE: val = (val <= rval); break;
		case a_SHEXP_ARITH_OP_GE: val = (val >= rval); break;
		case a_SHEXP_ARITH_OP_LT: val = (val < rval); break;
		case a_SHEXP_ARITH_OP_GT: val = (val > rval); break;

		case a_SHEXP_ARITH_OP_SHIFT_LEFT: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_SHIFT_LEFT: val <<= rval; break;

		case a_SHEXP_ARITH_OP_SHIFT_RIGHT: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_SHIFT_RIGHT: val >>= rval; break;

		case a_SHEXP_ARITH_OP_SHIFT_RIGHTU: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_SHIFT_RIGHTU:
			val = S(s64,S(u64,val) >> rval);
			break;

		case a_SHEXP_ARITH_OP_ADD: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_ADD: val += rval; break;
		case a_SHEXP_ARITH_OP_SUB: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_SUB: val -= rval; break;

		case a_SHEXP_ARITH_OP_MUL: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_MUL: val *= rval; break;
		/* For /,%, avoid lvh=S64_MIN, rhv=-1:
		 * CHANGES, bash 4.3 [ac50fbac377e32b98d2de396f016ea81e8ee9961]:
		 *   Fixed a bug that caused floating-point exceptions and
		 *   overflow errors for the / and % arithmetic operators when
		 *   using INTMAX_MIN and -1. */
		case a_SHEXP_ARITH_OP_DIV: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_DIV:
			if(rval == 0){
				self->sac_error = a_SHEXP_ARITH_ERR_DIV_BY_ZERO;
				goto jleave;
			}else if(val != S64_MIN || rval != -1)
				val /= rval;
			break;
		case a_SHEXP_ARITH_OP_MOD: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_MOD:
			if(rval == 0){
				self->sac_error = a_SHEXP_ARITH_ERR_DIV_BY_ZERO;
				goto jleave;
			}else if(val == S64_MIN && rval == -1)
				val = 0;
			else
				val %= rval;
			break;

		case a_SHEXP_ARITH_OP_EXP: FALLTHRU
		case a_SHEXP_ARITH_OP_ASSIGN_EXP:
			if(rval < 0){
				self->sac_error = a_SHEXP_ARITH_ERR_EXP_INVALID;
				goto jleave;
			}else{
				s64 i;

				for(i = 1; rval > 0; --rval)
					i *= val;
				val = i;
			}
			break;
		}
	}

	/* Assignment updates a variable, which must exist.
	 * For prefix and postfix operators, too: we already turned them into
	 * multiple unary plus/minus unless we had seen a variable name */
jquick:
	if(prec == a_SHEXP_ARITH_PREC_ASSIGN || prec == a_SHEXP_ARITH_PREC_PREFIX || prec == a_SHEXP_ARITH_PREC_POSTFIX){
		char buf[su_IENC_BUFFER_SIZE], *bp;

		if(nums_top->sav_var == NIL){
			self->sac_error = a_SHEXP_ARITH_ERR_ASSIGN_NO_VAR;
			goto jleave;
		}

		if(!ign){
			bp = su_ienc_s64(buf, val, 10);
			n_var_vset(nums_top->sav_var, S(up,bp), ((n_pstate & n_PS_ARGMOD_LOCAL) != 0));
		}

		/* And restore the stack value again for postfix operators */
		if(op == a_SHEXP_ARITH_OP_POSTFIX_INC)
			--val;
		else if(op == a_SHEXP_ARITH_OP_POSTFIX_DEC)
			++val;

		if(!ign){
			a_SHEXP_ARITH_L(("  + _arith_op_apply VAR <%s> SET <%s> VAL <%lld>\n",
				nums_top->sav_var, bp, val));
		}
	}

	nums_top->sav_val = val;
	nums_top->sav_var = NIL;

	rv = TRU1;
jleave:
	a_SHEXP_ARITH_L(("  < _arith_op_apply RV %d <0x%02X %u> RES<%lld> ERR<%d> nums=%lu ops=%lu\n",
		rv, op, op & 0xFF, val, self->sac_error, S(ul,sasp->sas_nums_top - sasp->sas_nums),
		S(ul,sasp->sas_ops_top - sasp->sas_ops)));

	NYD_OU;
	return rv;
}

static boole
a_shexp__arith_op_apply_colons(struct a_shexp_arith_ctx *self){
	u16 lop, lprec;
	boole next_stop;
	NYD_IN;

	for(next_stop = FAL0;;){
		if(!a_shexp__arith_op_apply(self)){
			next_stop = FAL0;
			break;
		}
		if(next_stop)
			break;
		a_SHEXP_ARITH_ERROR_TRACK( --self->sac_stack->sas_error_track_top; )
		lop = *--self->sac_stack->sas_ops_top & a_SHEXP_ARITH_OP_MASK;
		lprec = lop & 0xFF;
		next_stop = (lprec == a_SHEXP_ARITH_PREC_PAREN_LEFT || lop == a_SHEXP_ARITH_OP_COND);
	}

	NYD_OU;
	return next_stop;
}

#if a_SHEXP_ARITH_DBG
static void
a_shexp__arith_log(char const *fmt, ...){
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
#endif

#undef a_SHEXP_ARITH_ERROR_TRACK
#undef a_SHEXP_ARITH_IFS
#undef a_SHEXP_ARITH_DBG
#undef a_SHEXP_ARITH_L
#undef a_SHEXP_ARITH_IDEC_MODE

/* s-itt-mode */
