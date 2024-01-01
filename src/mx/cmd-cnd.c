/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-cnd.h.
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
#define su_FILE cmd_cnd
#define mx_SOURCE
#define mx_SOURCE_CMD_CND

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/mem.h>

#include "mx/cmd.h"
#include "mx/cndexp.h"
#include "mx/go.h"

#include "mx/cmd-cnd.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#define a_CCND_IF_IS_ACTIVE() (mx_go_data->gdc_ifcond != NIL)
#define a_CCND_IF_IS_SKIP() \
	(a_CCND_IF_IS_ACTIVE() &&\
		((S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond)->cin_flags & a_CCND_IF_F_NOOP) ||\
		 !(S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond)->cin_flags & a_CCND_IF_F_GO)))

enum a_ccnd_if_flags{
	a_CCND_IF_F_ERROR = 1u<<0, /* Bad expression, skip entire if..endif */
	a_CCND_IF_F_NOOP = 1u<<1, /* Outer stack !_GO, entirely no-op */
	a_CCND_IF_F_GO = 1u<<2,
	a_CCND_IF_F_ELSE = 1u<<3 /* In `else' clause */
};

struct a_ccnd_if_node{
	struct a_ccnd_if_node *cin_outer;
	uz cin_flags;
};

/* Shared `if' / `elif' implementation */
static int a_ccnd_if(void *vp, boole iselif);

static int
a_ccnd_if(void *vp, boole iselif){
	int rv;
	struct a_ccnd_if_node *cinp;
	NYD_IN;

	if(!iselif){
		cinp = su_ALLOC(sizeof *cinp);
		cinp->cin_outer = S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond);
	}else{
		cinp = S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond);
		ASSERT(cinp != NIL);
	}
	cinp->cin_flags = (a_CCND_IF_F_GO | (a_CCND_IF_IS_SKIP() ? a_CCND_IF_F_NOOP : 0));
	if(!iselif)
		mx_go_data->gdc_ifcond = cinp;

	if(cinp->cin_flags & a_CCND_IF_F_NOOP){
		rv = su_EX_OK;
		goto jleave;
	}

	/* C99 */{
		boole xrv;

		if((xrv = mx_cndexp_parse(S(struct mx_cmd_arg_ctx const*,vp), TRU1)) >= FAL0){
			ASSERT(cinp->cin_flags & a_CCND_IF_F_GO);
			if(!xrv)
				cinp->cin_flags ^= a_CCND_IF_F_GO;
			rv = su_EX_OK;
		}else{
			cinp->cin_flags |= a_CCND_IF_F_ERROR | a_CCND_IF_F_NOOP;
			rv = su_EX_ERR;
		}
	}

jleave:
	NYD_OU;
	return rv;
}

int
c_if(void *vp){
	int rv;
	NYD_IN;

	rv = a_ccnd_if(vp, FAL0);

	NYD_OU;
	return rv;
}

int
c_elif(void *vp){
	struct a_ccnd_if_node *cinp;
	int rv;
	NYD_IN;

	if((cinp = S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond)) == NIL || (cinp->cin_flags & a_CCND_IF_F_ELSE)){
		n_err(_("elif: no matching `if'\n"));
		rv = 1;
	}else if(!(cinp->cin_flags & a_CCND_IF_F_ERROR)){
		cinp->cin_flags ^= a_CCND_IF_F_GO;
		rv = a_ccnd_if(vp, TRU1);
	}else
		rv = 0;

	NYD_OU;
	return rv;
}

int
c_else(void *vp){
	int rv;
	struct a_ccnd_if_node *cinp;
	NYD_IN;
	UNUSED(vp);

	if((cinp = S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond)) == NIL || (cinp->cin_flags & a_CCND_IF_F_ELSE)){
		n_err(_("else: no matching `if'\n"));
		rv = 1;
	}else{
		cinp->cin_flags ^= a_CCND_IF_F_GO | a_CCND_IF_F_ELSE;
		rv = 0;
	}

	NYD_OU;
	return rv;
}

int
c_endif(void *vp){
	int rv;
	struct a_ccnd_if_node *cinp;
	NYD_IN;
	UNUSED(vp);

	if((cinp = S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond)) == NIL){
		n_err(_("endif: no matching `if'\n"));
		rv = 1;
	}else{
		mx_go_data->gdc_ifcond = cinp->cin_outer;
		su_FREE(cinp);
		rv = 0;
	}

	NYD_OU;
	return rv;
}

boole
mx_cnd_if_exists(struct mx_cmd_desc const *next_cmd_or_nil){
	boole rv;
	NYD2_IN;

	ASSERT(next_cmd_or_nil == NIL || (next_cmd_or_nil->cd_caflags & mx_CMD_ARG_F) != 0);

	if((rv = a_CCND_IF_IS_ACTIVE())){
		boole toggle;

		toggle = (next_cmd_or_nil != NIL &&
				(next_cmd_or_nil->cd_func == &c_elif || next_cmd_or_nil->cd_func == &c_else));

		if(a_CCND_IF_IS_SKIP()){
			if(toggle){
				struct a_ccnd_if_node *cinp;

				cinp = S(struct a_ccnd_if_node*,mx_go_data->gdc_ifcond);

				if((cinp = cinp->cin_outer) != NIL && ((cinp->cin_flags & a_CCND_IF_F_NOOP) ||
						 !(cinp->cin_flags & a_CCND_IF_F_GO)))
					toggle = FAL0;
			}

			rv = toggle ? FAL0 : TRUM1;
		}else if(toggle)
			rv = TRUM1;
	}

	NYD2_OU;
	return rv;
}

void
mx_cnd_if_stack_del(struct mx_go_data_ctx *gdcp){
	struct a_ccnd_if_node *vp, *cinp;
	NYD2_IN;

	vp = S(struct a_ccnd_if_node*,gdcp->gdc_ifcond);
	gdcp->gdc_ifcond = NIL;

	while((cinp = vp) != NIL){
		vp = cinp->cin_outer;
		su_FREE(cinp);
	}

	NYD2_OU;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_CND
/* s-itt-mode */
