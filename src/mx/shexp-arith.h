/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Signed 64-bit sh(1)ell-style $(( ARITH ))metic expression evaluator.
 *@ POW2 bases are parsed as unsigned, operation overflow is not handled,
 *@ saturated mode is not supported, division by zero is handled via error.
 *@ The maximum expression length is ~100.000.000 on 32-bit, U32_MAX otherwise.
 *@ After reading on Dijkstra's two stack algorithm; found in the internet:
 *@
 *@   We can use Dijkstra's two stack algorithm to solve an equation.
 *@   You need two stacks, a value stack (operands), and an operator stack.
 *@   Numbers will be double values, operators will be char values.
 *@   The whole of the expression is made up of tokens, ignoring whitespace.
 *@
 *@   While there are still tokens to read
 *@      Get the next item
 *@         If the item is:
 *@            A number: push it onto the value stack.
 *@            A left parenthesis: push it onto the operator stack.
 *@            A right parenthesis:
 *@               While the top of the operator stack is not a left parenthesis
 *@                  Pop the operator from the operator stack.
 *@                  Pop the value stack twice, getting two operands.
 *@                  Apply the operator to the operands, in the correct order.
 *@                  Push the result onto the value stack.
 *@                  Pop the left parenthesis from the operator stack
 *@            An operator op:
 *@               While the operator stack is not empty, and the top of the
 *@               operator stack has the same or greater precedence as op,
 *@                  Pop the operator from the operator stack.
 *@                  Pop the value stack twice, getting two operands.
 *@                  Apply the operator to the operands, in the correct order.
 *@                  Push the result onto the value stack.
 *@                  Push op onto the operator stack.
 *@   While the operator stack is not empty... [less push op]
 *@
 *@   At this point the operator stack should be empty, and the value stack
 *@   should have only one value in it, which is the final result.
 *@
 *@ as well as bash:expr.c, a bit.  Most heavily inspired by busybox.
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
   a_SHEXP_ARITH_ERR_NO_OPERAND, /* Expected an argument here */
   a_SHEXP_ARITH_ERR_NAME_LOOP, /* Variable self-reference loop */
   a_SHEXP_ARITH_ERR_OP_INVALID /* Unknown operator */
};

/* Operators and precedences in increasing precedence order.
 * (The operator stack as such is u16: (OP<<8) | PREC.)
 * bash's expr.c says:
 *    Sub-expressions within parentheses have a precedence level greater than
 *    all of these levels and are evaluated first.  Within a single precedence
 *    group, evaluation is left-to-right, except for arithmetic assignment,
 *    which is evaluated right-to-left (as in C).  [**, too] */
enum a_shexp_arith_ops{
#undef a_X
#define a_X(N,P,O) \
   CONCAT(a_SHEXP_ARITH_PREC_,N) = CONCAT(P,u),\
   CONCAT(a_SHEXP_ARITH_OP_,N) =\
         (CONCAT(O,u) << 8) | CONCAT(a_SHEXP_ARITH_PREC_,N)

   a_X(PAREN_LEFT, 0, 0),
   a_X(COMMA, 1, 0),
   a_X(ASSIGN, 2, 0),
      a_X(ASSIGN_BIT_OR, 2, 1), a_X(ASSIGN_BIT_XOR, 2, 2),
         a_X(ASSIGN_BIT_AND, 2, 3),
      a_X(ASSIGN_SHIFT_LEFT, 2, 4), a_X(ASSIGN_SHIFT_RIGHT, 2, 5),
         a_X(ASSIGN_SHIFT_RIGHTU, 2, 6),
      a_X(ASSIGN_ADD, 2, 7), a_X(ASSIGN_SUB, 2, 8),
      a_X(ASSIGN_MUL, 2, 9), a_X(ASSIGN_DIV, 2, 10), a_X(ASSIGN_MOD, 2, 11),
      a_X(ASSIGN_EXP, 2, 12),
   a_X(OR, 3, 0), a_X(AND, 4, 0),
   a_X(BIT_OR, 5, 0), a_X(BIT_XOR, 6, 0), a_X(BIT_AND, 7, 0),
   a_X(EQ, 8, 0), a_X(NE, 8, 1),
   a_X(LE, 9, 0), a_X(GE, 9, 1), a_X(LT, 9, 2), a_X(GT, 9, 3),
   a_X(SHIFT_LEFT, 10, 0), a_X(SHIFT_RIGHT, 10, 1), a_X(SHIFT_RIGHTU, 10, 2),
   a_X(ADD, 11, 0), a_X(SUB, 11, 1),
   a_X(MUL, 12, 0), a_X(DIV, 12, 1), a_X(MOD, 12, 2),
   a_X(EXP, 13, 0),

   /* Further operators are unary, pre- or postfix */
   a_SHEXP_ARITH_PREC_UNARY = 14,
   a_SHEXP_ARITH_PREC_PREFIX = 15,
   a_SHEXP_ARITH_PREC_POSTFIX = 17,

   a_X(UNARY_NOT, 14, 0), a_X(UNARY_BIT_NOT, 14, 1),
   a_X(PREFIX_INC, 15, 0), a_X(PREFIX_DEC, 15, 1),
   a_X(UNARY_PLUS, 16, 1), a_X(UNARY_MINUS, 16, 0),
   a_X(POSTFIX_INC, 17, 0), a_X(POSTFIX_DEC, 17, 1),

   /* Beyond operator profanity; the first "is a number" */
   a_SHEXP_ARITH_PREC_SKY = 18,
   a_X(NUM, 18, 0), a_X(PAREN_RIGHT, 18, 1),

#undef a_X
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
   u16 *sas_ops;
   u16 *sas_opstop;
   struct a_shexp_arith_val *sas_nums;
   struct a_shexp_arith_val *sas_numstop;
};

struct a_shexp_arith_ctx{
   enum a_shexp_arith_error sac_error;
   su_64( u8 sac__pad[4]; )
   s64 sac_rv;
   struct a_shexp_arith_stack *sac_stack;
   char const *sac_ifs_ws; /* IFS whitespace or NIL */
   struct a_shexp_arith_name_stack *sac_name_stack;
};

/* Sort by ~expected usage -- however, longest first if ambiguous!
 * Follow busybox, save space by compressing data in char[] not struct[] */
static char const a_shexp_arith_op_toks[] = {
#undef a_X
#define a_X(X) \
   S(char,(CONCAT(a_SHEXP_ARITH_OP_,X) & 0xFF00u) >> 8),\
   S(char,CONCAT(a_SHEXP_ARITH_PREC_,X))

   '-','-','\0', a_X(POSTFIX_DEC),
   '+','+','\0', a_X(POSTFIX_INC),
   '+','=','\0', a_X(ASSIGN_ADD),
   '-','=','\0', a_X(ASSIGN_SUB),
   '*','=','\0', a_X(ASSIGN_MUL),
   '/','=','\0', a_X(ASSIGN_DIV),
   '%','=','\0', a_X(ASSIGN_MOD),
   '|','=','\0', a_X(ASSIGN_BIT_OR),
   '^','=','\0', a_X(ASSIGN_BIT_XOR),
   '&','=','\0', a_X(ASSIGN_BIT_AND),
   '<','<','=',0, a_X(ASSIGN_SHIFT_LEFT),
   '>','>','>','=',0, a_X(ASSIGN_SHIFT_RIGHTU),
   '>','>','=',0, a_X(ASSIGN_SHIFT_RIGHT),
   '*','*','=','\0', a_X(ASSIGN_EXP),
   '*','*','\0', a_X(EXP),

   '|','|','\0', a_X(OR),
   '&','&','\0', a_X(AND),
   '!','=','\0', a_X(NE),

   '+','\0', a_X(ADD),
   '-','\0', a_X(SUB),
   '*','\0', a_X(MUL),
   '/','\0', a_X(DIV),
   '%','\0', a_X(MOD),
   '|','\0', a_X(BIT_OR),
   '^','\0', a_X(BIT_XOR),
   '&','\0', a_X(BIT_AND),
   '<','<','\0', a_X(SHIFT_LEFT),
   '>','>','>','\0', a_X(SHIFT_RIGHTU),
   '>','>','\0', a_X(SHIFT_RIGHT),
   '~','\0', a_X(UNARY_BIT_NOT),
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
   '\0'
#undef a_X
};

/* Our "public" entry point.  exp_buf can be NIL if exp_len is 0, it need not
 * be NUL terminated (stop for NUL or out of length). */
static enum a_shexp_arith_error a_shexp_arith_eval(s64 *resp,
      char const *exp_buf, uz exp_len);

static void a_shexp__arith_eval(struct a_shexp_arith_ctx *self,
      char const *exp_buf, uz exp_len);

/* Count non-WS as well as normalized WS ([:"space":]+ -> ' ') in exp_buf,
 * return count.  If store!=NIL, also copy normalization.
 * An all-WS exp_buf returns 0 */
static uz a_shexp__arith_ws_squeeze(struct a_shexp_arith_ctx *self,
      char const *exp_buf, uz exp_len, char *store_or_nil);

/* Resolve and evaluate the "self-contained string" savp->sav_var.
 * Take care to avoid name lookup loops */
static boole a_shexp__arith_val_eval(struct a_shexp_arith_ctx *self,
      struct a_shexp_arith_val *savp);

/* Work top of the stack, which may pop & push etc */
static boole a_shexp__arith_op_apply(struct a_shexp_arith_ctx *self);

#if a_SHEXP_ARITH_DBG
static void a_shexp__arith_log(char const *fmt, ...);
#endif

static enum a_shexp_arith_error
a_shexp_arith_eval(s64 *resp, char const *exp_buf, uz exp_len){
   struct a_shexp_arith_stack sas_stack;
   struct a_shexp_arith_ctx self;
   NYD_IN;

   a_SHEXP_ARITH_L(("> arith_eval %zu <%.*s>\n",
      exp_len, S(int,exp_len!=UZ_MAX?exp_len:su_cs_len(exp_buf)), exp_buf));

   STRUCT_ZERO(struct a_shexp_arith_ctx, &self);

   ASSERT_NYD_EXEC(resp != NIL,
      self.sac_error = a_SHEXP_ARITH_ERR_NO_OPERAND);
   DBG( *resp = 0; )
   ASSERT_NYD_EXEC(exp_len == 0 || exp_buf != NIL,
      self.sac_error = a_SHEXP_ARITH_ERR_NO_OPERAND);

   self.sac_ifs_ws = ok_vlook(ifs_ws);
   self.sac_stack = &sas_stack;
   a_shexp__arith_eval(&self, exp_buf, exp_len);
   *resp = self.sac_rv;

   a_SHEXP_ARITH_L(("< arith_eval %zu <%.*s> -> <%lld> ERR<%d>\n",
      exp_len, S(int,exp_len!=UZ_MAX?exp_len:su_cs_len(exp_buf)), exp_buf,
      self.sac_rv, self.sac_error));

   NYD_OU;
   return self.sac_error;
}

static void
a_shexp__arith_eval(struct a_shexp_arith_ctx *self,
      char const *exp_buf, uz exp_len){
   char *ep, *cp, c;
   u16 lop;
   struct a_shexp_arith_stack *sasp;
   void *mem_p;
   NYD2_IN;

   a_SHEXP_ARITH_L((" > _arith_eval %zu <%.*s>\n",
      exp_len, (int)(exp_len!=UZ_MAX?exp_len:su_cs_len(exp_buf)), exp_buf));

   mem_p = NIL;

   sasp = self->sac_stack;

   /* Create single contiguous allocation for anything.
    * Since we need to keep pointers to variable names along the way, simply
    * NUL terminate in this large buffer (move backward by one first) */
   /* C99 */{
      union {void *v; char *c;} p;
      uz i, j, a;

      /* Done for empty expression */
      if((i = a_shexp__arith_ws_squeeze(self, exp_buf, exp_len, NIL)) == 0)
         goto jleave;

      /* Overflow check: since arithmetic expressions are rarely long enough
       * to come near this limit, laxe & fuzzy, not exact; max U32_MAX! */
      if(su_64( i > U32_MAX || )
            i >= ((UZ_MAX - i) / (su_ALIGNOF(*sasp->sas_nums) +
                  sizeof(*sasp->sas_ops) + 1))){
         self->sac_error = a_SHEXP_ARITH_ERR_NOMEM;
         goto jleave;
      }

/* FIXME due to initial LPAREN (drop that!!) +1 is missing?! */
      j = su_ALIGNOF(*sasp->sas_nums) * ((i + 1) >> 1);
      a = j + (sizeof(*sasp->sas_ops) * i) + i + 1 +1;
      mem_p = p.v = su_LOFI_ALLOC(a);
      if(p.v == NIL){
         /* (For MX LOFI has _MUSTFAIL set though) */
         self->sac_error = a_SHEXP_ARITH_ERR_NOMEM;
         goto jleave;
      }
      sasp->sas_nums = sasp->sas_numstop = S(struct a_shexp_arith_val*,p.v);
      p.c += j;
      sasp->sas_ops = sasp->sas_opstop = S(u16*,p.v);
      p.c += sizeof(*sasp->sas_ops) * i;
      /* (room for moving back vars by one, to NUL terminate 'em) */
      a_shexp__arith_ws_squeeze(self, exp_buf, exp_len, ep = ++p.c);

      a_SHEXP_ARITH_L((" ! _arith_eval ALLOC <%lu> numstop=%p <%lu> opstop=%p "
         "%lu <%s>\n", S(ul,a), sasp->sas_numstop,
         S(ul,j / su_ALIGNOF(*sasp->sas_nums)), sasp->sas_opstop, S(ul,i),ep));
   }

   /* Start with a left paren */
/* FIXME get rid if outer () */
   *sasp->sas_opstop++ = lop = a_SHEXP_ARITH_OP_PAREN_LEFT;

   for(;;) Jouter:{
      u16 op;

      a_SHEXP_ARITH_L((" = _arith_eval TICK LOP <0x%02X %u> "
         "numstop=%p opstop=%p %lu <%s>\n",
         lop, lop & 0xFF, sasp->sas_numstop, sasp->sas_opstop,
         S(ul,su_cs_len(ep)), ep));

      if(*ep == '\0'){
         /* At the end of the expression pop anything left.
          * Assume we have read PAREN_RIGHT */
         if(exp_buf != NIL){
            exp_buf = NIL;
            op = a_SHEXP_ARITH_OP_PAREN_RIGHT;
            goto jtok_go;
         }

         /* After PAREN_RIGHT, we must be finished */
         if(sasp->sas_numstop != &sasp->sas_nums[1])
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

         is = su_idec_cp(&sasp->sas_numstop->sav_val, ep, 0,
               a_SHEXP_ARITH_IDEC_MODE, S(char const**,&ep));
         if((is &= su_IDEC_STATE_EMASK) && is != su_IDEC_STATE_EBASE)
            sasp->sas_numstop->sav_val = 0;
         sasp->sas_numstop->sav_var = NIL;

         ++sasp->sas_numstop;
         lop = a_SHEXP_ARITH_OP_NUM;
         a_SHEXP_ARITH_L((" + _arith_eval NUM <%lld> numstop=%p opstop=%p\n",
            sasp->sas_numstop[-1].sav_val, sasp->sas_numstop,
            sasp->sas_opstop));
         continue;
      }

      /* Is it a variable name? */
      for(cp = ep; (c = *cp, a_SHEXP_ISVARC(c)); ++cp)
         if(cp == ep && a_SHEXP_ISVARC_BAD1ST(c))
            break;

      if(cp != ep){
         for(;;){
            c = cp[-1];
            if(!a_SHEXP_ISVARC_BADNST(c))
               break;
            if(--cp == ep){
               self->sac_error = a_SHEXP_ARITH_ERR_SYNTAX;
               goto jleave;
            }
         }

         /* We reserved one byte at the front, so we can simply move back
          * the variable name by one, and then NUL terminate it.
          * (Unfortunately we do _have_ to copy; even more weird: move!) */
         su_mem_move(sasp->sas_numstop->sav_var = &ep[-1], ep, P2UZ(cp - ep));
         cp[-1] = '\0';
         ep = cp;

         ++sasp->sas_numstop;
         lop = a_SHEXP_ARITH_OP_NUM;
         a_SHEXP_ARITH_L((" + _arith_eval VAR <%s> numstop=%p opstop=%p "
            "%lu\n", sasp->sas_numstop[-1].sav_var, sasp->sas_numstop,
            sasp->sas_opstop));
         continue;
      }

      /* An operator.
       * We turn prefix operators to multiple unary plus/minus if
       * not attached to a variable name (++10 -> + + 10).
       * (We adjust postfix to prefix below) */
      if((ep[0] == '+' || ep[0] == '-') && (ep[1] == ep[0])){
         if(sasp->sas_numstop == sasp->sas_nums ||
               sasp->sas_numstop[-1].sav_var == NIL){
            if((c = ep[2]) == ' ')
               c = ep[3];

            if(c != '\0' && (!a_SHEXP_ISVARC(c) || a_SHEXP_ISVARC_BAD1ST(c))){
               op = (ep[0] == '+') ? a_SHEXP_ARITH_OP_ADD
                     : a_SHEXP_ARITH_OP_SUB;
               ++ep;
               a_SHEXP_ARITH_L((" + _arith_eval OP PREFIX INC/DEC SPLIT "
                  "<%c%c> -> <%c>\n", ep[0], ep[0], ep[0]));
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
      a_SHEXP_ARITH_L((" + _arith_eval OP <0x%02X %u> LAST <0x%02X %u> "
         "numstop=%p opstop=%p %lu <%s>\n", op, prec, lop, lop & 0xFF,
         sasp->sas_numstop, sasp->sas_opstop, S(ul,su_cs_len(ep)), ep));

      if(op == a_SHEXP_ARITH_OP_UNARY_PLUS){
         a_SHEXP_ARITH_L((" + _arith_eval IGNORE UNARY PLUS\n"));
         continue;
      }

      /* Post grammar: a++ reduce to num */
      if((lop & 0xFF) == a_SHEXP_ARITH_PREC_POSTFIX){
         lop = a_SHEXP_ARITH_OP_NUM;
         a_SHEXP_ARITH_L((" + _arith_eval REDUCE POSTFIX LOP to NUM\n"));
      }
      /* Correct our understanding of some binary/postfix operators */
      else if(lop != a_SHEXP_ARITH_OP_NUM){
         switch(op){
         case a_SHEXP_ARITH_OP_ADD:
            a_SHEXP_ARITH_L((" + _arith_eval ADJUST OP: IGNORE UNARY PLUS\n"));
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
            a_SHEXP_ARITH_L((" + _arith_eval ADJUST OP TO UNARY/PREFIX\n"));
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
            a_SHEXP_ARITH_L((" + _arith_eval OP POSTFIX INC/DEC SPLIT "
               "<%c%c> -> <%c>\n", c, c, c));
         }
      }

      if((prec > a_SHEXP_ARITH_PREC_PAREN_LEFT &&
               prec < a_SHEXP_ARITH_PREC_UNARY) ||
            prec >= a_SHEXP_ARITH_PREC_SKY){
         if(lop != a_SHEXP_ARITH_OP_NUM){
            self->sac_error = a_SHEXP_ARITH_ERR_NO_OPERAND;
            goto jleave;
         }

         /* Pop as much as possible */
         while(sasp->sas_opstop != sasp->sas_ops){
            u16 prev_op;

            prev_op = *--sasp->sas_opstop;

            if(op == a_SHEXP_ARITH_OP_PAREN_RIGHT){
               if(prev_op == a_SHEXP_ARITH_OP_PAREN_LEFT){
                  if(sasp->sas_numstop[-1].sav_var != NIL){
                     if(!a_shexp__arith_val_eval(self,
                           &sasp->sas_numstop[-1]))
                        goto jleave;
                     sasp->sas_numstop[-1].sav_var = NIL;
                  }
                  /* Resolved to number */
                  a_SHEXP_ARITH_L((" + _arith_eval OP ( ) RESOLVED <%lld> "
                     "numstop=%p opstop=%p\n",
                     sasp->sas_numstop[-1].sav_val, sasp->sas_numstop,
                     sasp->sas_opstop));
                  lop = a_SHEXP_ARITH_OP_NUM;
                  goto Jouter;
               }
            }
            /* Is this a right-associative operation? */
            else{
               u8 prev_prec;

               prev_prec = prev_op & 0xFF;

               if(prev_prec < prec){
                  ++sasp->sas_opstop;
                  a_SHEXP_ARITH_L((" + _arith_eval OP DELAY PRECEDENCE "
                     "numstop=%p opstop=%p\n",
                     sasp->sas_numstop, sasp->sas_opstop));
                  break;
               }else if(prev_prec == prec &&
                     prec == a_SHEXP_ARITH_PREC_ASSIGN){
                  ++sasp->sas_opstop;
                  a_SHEXP_ARITH_L((" + _arith_eval OP DELAY RIGHT ASSOC "
                     "numstop=%p opstop=%p\n",
                     sasp->sas_numstop, sasp->sas_opstop));
                  break;
               }
            }

            if(!a_shexp__arith_op_apply(self))
               goto jleave;
         }

         if(op == a_SHEXP_ARITH_OP_PAREN_RIGHT){
            self->sac_error = a_SHEXP_ARITH_ERR_SYNTAX;
            goto jleave;
         }
      }

      /* Push this operator to the stack and remember it */
      *sasp->sas_opstop++ = lop = op;
      a_SHEXP_ARITH_L((" + _arith_eval OP PUSH <0x%02X %u> numstop=%p "
         "opstop=%p\n", op, (op & 0xFF), sasp->sas_numstop, sasp->sas_opstop));
   }
   }

   self->sac_rv = sasp->sas_nums->sav_val;

jleave:
   if(mem_p != NIL)
      su_LOFI_FREE(mem_p);

   a_SHEXP_ARITH_L((" < _arith_eval <%lld> ERR<%d>\n",
      self->sac_rv, self->sac_error));

   NYD2_OU;
}

static uz
a_shexp__arith_ws_squeeze(struct a_shexp_arith_ctx *self,
      char const *exp_buf, uz exp_len, char *store_or_nil){
   char c;
   boole last_ws, ws;
   char const *ifs_ws;
   uz rv;
   NYD2_IN;

   rv = 0;
   ifs_ws = self->sac_ifs_ws;

   for(;; ++exp_buf, --exp_len){
      if(UNLIKELY(exp_len == 0) || UNLIKELY((c = *exp_buf) == '\0'))
         goto jleave;
      if(!(su_cs_is_space(c) ||
            (/*ifs_ws != NIL &&*/ su_cs_find_c(ifs_ws, c) != NIL)))
         break;
   }

   for(last_ws = FAL0;; ++exp_buf, --exp_len){
      if(UNLIKELY(exp_len == 0) || UNLIKELY((c = *exp_buf) == '\0'))
         break;

      ws = (su_cs_is_space(c) ||
            (/*ifs_ws != NIL &&*/ su_cs_find_c(ifs_ws, c) != NIL));
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
a_shexp__arith_val_eval(struct a_shexp_arith_ctx *self,
      struct a_shexp_arith_val *savp){
   struct a_shexp_arith_name_stack sans_stack, *sansp;
   struct a_shexp_arith_stack sas_stack, *sasp;
   char const *cp;
   NYD_IN;
   ASSERT(savp->sav_var != NIL);

   a_SHEXP_ARITH_L(("> _arith_val_eval %p <%s>\n",
      savp, savp->sav_var));

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
    * However, in most cases it solely consists of an integer, shortcut it! */
   if(su_cs_is_digit(*cp) && (su_idec_cp(&savp->sav_val, cp, 0,
            a_SHEXP_ARITH_IDEC_MODE, NIL) & su_IDEC_STATE_CONSUMED)){
      a_SHEXP_ARITH_L((" + _arith_val_eval NUM FAST <%lld>\n", savp->sav_val));
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
      savp, savp->sav_var, savp->sav_val,
      (cp == NIL && self->sac_error == a_SHEXP_ARITH_ERR_NONE)));

   NYD_OU;
   return (cp == NIL && self->sac_error == a_SHEXP_ARITH_ERR_NONE);
}

static boole
a_shexp__arith_op_apply(struct a_shexp_arith_ctx *self){
   struct a_shexp_arith_val *numstop;
   u8 prec;
   u16 op;
   struct a_shexp_arith_stack *sasp;
   s64 val;
   boole rv;
   NYD_IN;

   rv = FAL0;
   val = 0;
   sasp = self->sac_stack;
   op = *sasp->sas_opstop;


   /* At least one argument is always needed */
   if((numstop = sasp->sas_numstop) == sasp->sas_nums){
      self->sac_error = a_SHEXP_ARITH_ERR_NO_OPERAND;
      goto jleave;
   }
   --numstop;

   /* Resolve name to value as necessary */
   if(numstop->sav_var != NIL && !a_shexp__arith_val_eval(self, numstop))
      goto jleave;

   val = numstop->sav_val;
   prec = op & 0xFF;

   /* Not a binary operator? */
   if(prec >= a_SHEXP_ARITH_PREC_UNARY && prec < a_SHEXP_ARITH_PREC_SKY){
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
   }else{
      /* Binaries need two numbers: one is popped, the other replaced */
      s64 rval;

      if(numstop == sasp->sas_nums){
         self->sac_error = a_SHEXP_ARITH_ERR_NO_OPERAND;
         goto jleave;
      }
      sasp->sas_numstop = numstop--;

      /* Resolve LHV as necessary */
      if(op != a_SHEXP_ARITH_OP_COMMA && op != a_SHEXP_ARITH_OP_ASSIGN &&
            numstop->sav_var != NIL && !a_shexp__arith_val_eval(self, numstop))
         goto jleave;

      rval = val;
      val = numstop->sav_val; /* (may be bogus for assign, fixed soon) */

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
       *    Fixed a bug that caused floating-point exceptions and
       *    overflow errors for the / and % arithmetic operators when
       *    using INTMAX_MIN and -1. */
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
   if(prec == a_SHEXP_ARITH_PREC_ASSIGN || prec == a_SHEXP_ARITH_PREC_PREFIX ||
         prec == a_SHEXP_ARITH_PREC_POSTFIX){
      char buf[su_IENC_BUFFER_SIZE], *bp;

      if(numstop->sav_var == NIL){
         self->sac_error = a_SHEXP_ARITH_ERR_ASSIGN_NO_VAR;
         goto jleave;
      }

      bp = su_ienc_s64(buf, val, 10);
      n_var_vset(numstop->sav_var, S(up,bp), FAL0);

      /* And restore the stack value again for postfix operators */
      if(op == a_SHEXP_ARITH_OP_POSTFIX_INC)
         --val;
      else if(op == a_SHEXP_ARITH_OP_POSTFIX_DEC)
         ++val;

      a_SHEXP_ARITH_L(("  + _arith_op_apply VAR <%s> SET <%s> VAL <%lld>\n",
         numstop->sav_var, bp, val));
   }

   numstop->sav_val = val;
   numstop->sav_var = NIL;

   rv = TRU1;
jleave:
   a_SHEXP_ARITH_L(("  < _arith_op_apply RV %d <0x%02X %u> RES<%lld> ERR<%d> "
         "numstop=%p (%lu) opstop=%p (%lu)\n",
      rv, op, op & 0xFF, val, self->sac_error,
      sasp->sas_numstop, S(ul,sasp->sas_numstop - sasp->sas_nums),
      sasp->sas_opstop, S(ul,sasp->sas_opstop - sasp->sas_ops)));

   NYD_OU;
   return rv;
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

#undef a_SHEXP_ARITH_DBG
#undef a_SHEXP_ARITH_L
#undef a_SHEXP_ARITH_IDEC_MODE

/* s-it-mode */
