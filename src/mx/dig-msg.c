/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Dig message objects. TODO Very very restricted (especially non-compose)
 *@ Protocol change: adjust mx-config.h:mx_DIG_MSG_PLUMBING_VERSION + `~^' man.
 *@ TODO - Turn X-SERIES in something regular and documented
 *@ XXX - Multiple objects per message could be possible (a_dmsg_find()),
 *@ XXX   except in compose mode
 *
 * Copyright (c) 2016 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE dig_msg
#define mx_SOURCE
#define mx_SOURCE_DIG_MSG

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/attachments.h"
#include "mx/cmd.h"
#include "mx/cmd-fop.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/ignore.h"
#include "mx/mime.h"
#include "mx/mime-parse.h"
#include "mx/mime-type.h"
#include "mx/names.h"
/* TODO ARGH! sigman!! */
#include "mx/sigs.h"

#include "mx/dig-msg.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/**/
CTAV(HF_CMD_TO_OFF(HF_CMD_forward) == 0);
CTAV(HF_CMD_TO_OFF(HF_CMD_mail) == 1);
CTAV(HF_CMD_TO_OFF(HF_CMD_Lreply) == 2);
CTAV(HF_CMD_TO_OFF(HF_CMD_Reply) == 3);
CTAV(HF_CMD_TO_OFF(HF_CMD_reply) == 4);
CTAV(HF_CMD_TO_OFF(HF_CMD_resend) == 5);
CTAV(HF_CMD_TO_OFF(HF_CMD_MASK) == 6);

struct a_dmsg_sl{
	struct a_dmsg_sl *dmsl_next;
	u32 dmsl_status_or_new_ent; /* First node: status; else: is new result record (new line before) */
	u32 dmsl_len; /* May be 0 TODO some "real" uz->u32 trunc checks please */
	char const *dmsl_dat;
};

static char const a_dmsg_hf_cmd[7][8] = {"forward\0", "mail", "Lreply", "Reply", "reply", "resend", ""};
static char const a_dmsg_subj[] = "Subject";

struct mx_dig_msg_ctx *mx_dig_msg_read_overlay; /* XXX HACK */
struct mx_dig_msg_ctx *mx_dig_msg_compose_ctx; /* Or NIL XXX HACK*/

/* Try to convert cp into an unsigned number that corresponds to an existing message (or ERR_INVAL), search for an
 * existing object (ERR_EXIST if oexcl and exists; ERR_NOENT if not oexcl and does not exist).  On oexcl success *dmcp
 * will be ALLOC()ated with .dmc_msgno and .dmc_mp etc. set; but not linked into mb.mb_digmsg and .dmc_fp not created */
static s32 a_dmsg_find(char const *cp, struct mx_dig_msg_ctx **dmcpp, boole oexcl);

/* Subcommand drivers */
static boole a_dmsg_cmd(FILE *fp, struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *cmd, struct mx_cmd_arg *args);

static struct a_dmsg_sl *a_dmsg__help(void);
static struct a_dmsg_sl *a_dmsg__header(struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *args,
		struct a_dmsg_sl *dmslp, struct n_string *sp);
static struct a_dmsg_sl *a_dmsg__attach(struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *args,
		struct a_dmsg_sl *dmslp, struct n_string *sp);
static struct a_dmsg_sl *a_dmsg__part(struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *args,
		struct a_dmsg_sl *dmslp, struct n_string *sp);

static struct a_dmsg_sl *a_dmsg___line_tuple(char const *name, boole ndup, char const *value, boole vdup);

/* a_dmsg_find {{{ */
static s32
a_dmsg_find(char const *cp, struct mx_dig_msg_ctx **dmcpp, boole oexcl){
	struct mx_dig_msg_ctx *dmcp;
	s32 rv;
	u32 msgno;
	NYD2_IN;

	if(cp[0] == '-' && cp[1] == '\0'){
		if((dmcp = mx_dig_msg_compose_ctx) != NIL){
			*dmcpp = dmcp;
			if(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE_DIGGED)
				rv = oexcl ? su_ERR_EXIST : su_ERR_NONE;
			else
				rv = /*oexcl ?*/ su_ERR_NONE /*: su_ERR_NOENT*/;
		}else
			rv = su_ERR_INVAL;
		goto jleave;
	}

	if((su_idec_u32_cp(&msgno, cp, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
			) != su_IDEC_STATE_CONSUMED || msgno == 0 || UCMP(z, msgno, >, msgCount)){
		rv = su_ERR_INVAL;
		goto jleave;
	}

	for(dmcp = mb.mb_digmsg; dmcp != NIL; dmcp = dmcp->dmc_next)
		if(dmcp->dmc_msgno == msgno){
			*dmcpp = dmcp;
			rv = oexcl ? su_ERR_EXIST : su_ERR_NONE;
			goto jleave;
		}
	if(!oexcl){
		rv = su_ERR_NOENT;
		goto jleave;
	}

	*dmcpp = dmcp = su_CALLOC(ALIGN_Z(sizeof *dmcp) + sizeof(struct header));
	dmcp->dmc_mp = &message[msgno - 1];
	dmcp->dmc_flags = mx__DIG_MSG_OWN_MEMBAG | (!(mb.mb_perm & MB_DELE) ? mx__DIG_MSG_RDONLY : mx__DIG_MSG_NONE);
	dmcp->dmc_msgno = msgno;
	dmcp->dmc_hp = R(struct header*,ALIGN_Z(P2UZ(&dmcp[1])));
	dmcp->dmc_membag = su_mem_bag_create(&dmcp->dmc__membag_buf[0], 0);
	/* Rest done by caller */
	rv = su_ERR_NONE;
jleave:
	NYD2_OU;
	return rv;
}
/* }}} */

/* a_dmsg_cmd {{{ */
static boole
a_dmsg_cmd(FILE *fp, struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *cmd, struct mx_cmd_arg *args){
	char cb_i[su_IENC_BUFFER_SIZE], cb_s[8], *cps;
	struct n_string s;
	struct a_dmsg_sl *dmslp, dmsl_s[1];
	struct str *sp;
	void *lofi_snap;
	NYD2_IN;

	ASSERT(su_mem_bag_top(su_MEM_BAG_SELF) == dmcp->dmc_membag);
	lofi_snap = su_mem_bag_lofi_snap_create(su_MEM_BAG_SELF);

	n_string_reserve(n_string_creat(&s), 128);

	STRUCT_ZERO(struct a_dmsg_sl, dmslp = &dmsl_s[0]);
	UNINIT(dmslp->dmsl_len, 0);

	if(cmd == NIL)
		goto Jecmd;

	sp = &cmd->ca_arg.ca_str;
	if(su_cs_starts_with_case_n("header", sp->s, sp->l))
		dmslp = a_dmsg__header(dmcp, args, dmslp, &s);
	else if(su_cs_starts_with_case_n("attachment", sp->s, sp->l)){
		if(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE)
			dmslp = a_dmsg__attach(dmcp, args, dmslp, &s);
		else{
			static char const ca[] = "digmsg: attachment: only in compose mode (yet)";/* TODO v15-compat */

			dmslp->dmsl_status_or_new_ent = 505;
			dmslp->dmsl_len = sizeof(ca) - 1;
			dmslp->dmsl_dat = ca;
		}
	}else if(su_cs_starts_with_case_n("x-part", sp->s, sp->l)){ /* X-SERIES */
		if(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE){
			static char const ca[] = "digmsg: part: not in compose mode";

			dmslp->dmsl_status_or_new_ent = 505;
			dmslp->dmsl_len = sizeof(ca) - 1;
			dmslp->dmsl_dat = ca;
		}else
			dmslp = a_dmsg__part(dmcp, args, dmslp, &s);
	}else if(su_cs_starts_with_case_n("epoch", sp->s, sp->l)){
		if(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE){
			static char const ca[] = "digmsg: epoch: not in compose mode";

			dmslp->dmsl_status_or_new_ent = 505;
			dmslp->dmsl_len = sizeof(ca) - 1;
			dmslp->dmsl_dat = ca;
		}else if(args != NIL)
			goto Jecmd;
		else{
			s64 t;
			char const *cp;

			if((cp = hfield1("date", dmcp->dmc_mp)) == NIL || (t = mx_header_rfctime(cp)) == 0){
				static char const ca[] = "digmsg: epoch: invalid date";/* XXX .m_date! */

				dmslp->dmsl_status_or_new_ent = 501;
				dmslp->dmsl_len = sizeof(ca) - 1;
				dmslp->dmsl_dat = ca;
			}else{
				dmslp->dmsl_status_or_new_ent = 210;
				dmslp->dmsl_len = su_cs_len(dmslp->dmsl_dat = su_ienc_u64(cb_i, t, 10));
			}
		}
	}else if(su_cs_starts_with_case_n("version", sp->s, sp->l)){
		static char const ca[] = mx_DIG_MSG_PLUMBING_VERSION;

		if(args != NIL)
			goto Jecmd;
		dmslp->dmsl_status_or_new_ent = 210;
		dmslp->dmsl_len = sizeof(ca) - 1;
		dmslp->dmsl_dat = ca;
	}else if((sp->l == 1 && sp->s[0] == '?') || su_cs_starts_with_case_n("help", sp->s, sp->l)){
		if(args != NIL)
			goto Jecmd;
		dmslp = a_dmsg__help();
	}else Jecmd:{
		dmslp->dmsl_status_or_new_ent = 500;
		ASSERT(dmslp->dmsl_len == 0);
	}

	/* Visualize status xxx make it char* directly? */
	/* C99 */{
		u32 i;

		cps = &cb_s[NELEM(cb_s)];
		*--cps = '\0';
		i = dmslp->dmsl_status_or_new_ent;
		do
			*--cps = "0123456789"[i % 10]; /* xxx inline atoi */
		while((i /= 10) != 0);
	}

	/* Create output */
	if(dmcp->dmc_flags & mx__DIG_MSG_MODE_CARET){
		char const **cpp;
		struct a_dmsg_sl *x;
		u32 i;

		for(i = 0, x = dmslp; x != NIL; ++i, x = x->dmsl_next){
		}

		cpp = su_LOFI_TALLOC(char const*, i +1);

		for(i = 0, x = dmslp; x != NIL; x = x->dmsl_next)
			cpp[i++] = UNCONST(char*,(x->dmsl_len > 0) ? x->dmsl_dat : su_empty);
		cpp[i] = NIL;

		if(mx_var_caret_array_set(NIL, cps, i, NIL, cpp) != su_ERR_NONE)
			dmslp = NIL;
	}else{
		struct str s_b;
		struct a_dmsg_sl *x;

		if(fputs(cps, fp) == EOF)
			dmslp = NIL;
		else for(x = dmslp; x != NIL; x = x->dmsl_next){
			boole neednl;

			neednl = (x != dmslp && x->dmsl_status_or_new_ent != FAL0);

			if(x->dmsl_len > 0){
				if(putc((neednl ? '\n' : ' '), fp) == EOF){
					dmslp = NIL;
					break;
				}

				s_b.s = UNCONST(char*,x->dmsl_dat);
				s_b.l = x->dmsl_len;
				ASSERT(s_b.s != NIL);
				n_shexp_quote(n_string_trunc(&s, 0), &s_b, FAL0);

				if(fwrite(s.s_dat, sizeof(*s.s_dat), s.s_len, fp) != s.s_len){
					dmslp = NIL;
					break;
				}
				neednl = TRU1;
			}else if(neednl && putc('\n', fp) == EOF){
				dmslp = NIL;
				break;
			}
		}

		if(dmslp != NIL){
			if(putc('\n', fp) == EOF)
				dmslp = NIL;
			/* Multi-line output needs empty last line */
			else if(*cps != '5' && dmslp->dmsl_status_or_new_ent != 210 && putc('\n', fp) == EOF)
				dmslp = NIL;
		}

		if(fflush(fp) == EOF)
			dmslp = NIL;
	}

	n_string_gut(&s);

	ASSERT(su_mem_bag_top(su_MEM_BAG_SELF) == dmcp->dmc_membag);
	su_mem_bag_lofi_snap_unroll(su_MEM_BAG_SELF, lofi_snap);

	NYD2_OU;
	return (dmslp != NIL);
}
/* }}} */

/* a_dmsg__help {{{ */
static struct a_dmsg_sl *
a_dmsg__help(void){
	struct{char const *d; uz l;} const h[] = {
#undef a_X
#define a_X(S) {S, sizeof(S) -1}
		a_X("attachment [CMD (default: list)]"),
		a_X("  attribute ATTACHMENT (212; 501)"),
		a_X("  attribute-at NUMBER"),
		a_X("  attribute-set ATTACHMENT NAME VALUE (210; 505/501)"),
		a_X("  attribute-set-at NUMBER NAME VALUE"),
		a_X("  insert FILE[=input-charset[#output-charset]] (210; 501/505/506)"),
		a_X("  insert #MSG-NUMBER"),
		a_X("  list (212; 501)"),
		a_X("  remove ATTACHMENT (210; 501/506)"),
		a_X("  remove-at NUMBER (210; 501/505)"),
		a_X("header [CMD (default: list)]"),
		a_X("  insert HEADER VALUE (210; 501/505/506)"),
		a_X("  headerpick CTX (210; 501/505)"),
		a_X("  list [HEADER] (210; [501])"),
		a_X("  remove HEADER (210; 501/505)"),
		a_X("  remove-at HEADER NUMBER (210; 501/505)"),
		a_X("  show HEADER (211/212; 501)"),
		a_X("epoch (210; 501[/505])"),
		a_X("help (212)"),
		a_X("version (210)")
#undef a_X
	};
	static char const ca[] = "(Arguments undergo shell-style evaluation)";

	uz i;
	struct a_dmsg_sl *dmslp, **x;
	NYD2_IN;

	dmslp = su_LOFI_TALLOC(struct a_dmsg_sl, 1);
	dmslp->dmsl_status_or_new_ent = 212;
	dmslp->dmsl_len = sizeof(ca) - 1;
	dmslp->dmsl_dat = ca;

	x = &dmslp->dmsl_next;
	for(i = 0; i < NELEM(h); ++i){
		*x = su_LOFI_TALLOC(struct a_dmsg_sl, 1);
		(*x)->dmsl_status_or_new_ent = TRU1;
		(*x)->dmsl_dat = h[i].d;
		(*x)->dmsl_len = S(u32,h[i].l);
		x = &(*x)->dmsl_next;
	}
	*x = NIL;

	NYD2_OU;
	return dmslp;
} /* }}} */

/* a_dmsg__header {{{ */
static struct a_dmsg_sl *
a_dmsg__header(struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *args, struct a_dmsg_sl *dmslp, struct n_string *sp){
	char ienc_b[su_IENC_BUFFER_SIZE];
	struct str sin, sou;
	struct n_header_field *hfp;
	struct mx_name *np, **npp;
	struct a_dmsg_sl *x;
	uz i;
	struct mx_cmd_arg *a3p;
	char const *cp;
	struct header *hp;
	NYD2_IN;

	hp = dmcp->dmc_hp;
	UNINIT(a3p, NIL);

	if(args == NIL){
		cp = su_empty;
		goto jdefault;
	}

	cp = args->ca_arg.ca_str.s;
	args = args->ca_next;

	/* Strip the optional colon from header names */
	if((a3p = args) != NIL){
		char *xp;

		a3p = a3p->ca_next;

		for(xp = args->ca_arg.ca_str.s; *xp != '\0'; ++xp)
			if(*xp == ':')
				/* Do not loose possible modifier! */
				su_cs_pcopy(xp, &xp[1]);
	}

	/* TODO ERR_2BIG should happen on the cmd_arg parser side */
	if(a3p != NIL && a3p->ca_next != NIL){
		/* TODO until argparser can deal with subcmds: unroll convenience (CMD_ARG_DESC_GREEDY_JOIN arg3) */
		if(!su_cs_starts_with_case("insert", cp))
			goto jecmd;
		a3p->ca_arg.ca_str.s = savecatsep(a3p->ca_arg.ca_str.s, ' ', a3p->ca_next->ca_arg.ca_str.s);
		a3p->ca_arg.ca_str.l = su_cs_len(a3p->ca_arg.ca_str.s);
		goto jcmd_insert;
	}

	if(su_cs_starts_with_case("insert", cp))
		goto jcmd_insert;
	if(su_cs_starts_with_case("headerpick", cp))
		goto jcmd_headerpick;
	if(su_cs_starts_with_case("list", cp))
		goto jcmd_list;
	if(su_cs_starts_with_case("remove", cp))
		goto jcmd_remove;
	if(su_cs_starts_with_case("remove-at", cp))
		goto jcmd_remove_at;
	if(su_cs_starts_with_case("show", cp))
		goto jcmd_show;
	if(su_cs_starts_with_case("x-decode", cp)) /* X-SERIES */
		goto jcmd_x_decode;

jecmd:
	dmslp->dmsl_status_or_new_ent = 500;
jleave:
	NYD2_OU;
	return dmslp;

j505r:
	dmslp->dmsl_status_or_new_ent = 505;
	sp = n_string_assign_buf(sp, "read-only: ", sizeof("read-only: ") -1);
	sp = n_string_push_cp(sp, cp);
	dmslp->dmsl_len = sp->s_len;
	dmslp->dmsl_dat = su_LOFI_ALLOC(sp->s_len +1);
	su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), n_string_cp(sp), sp->s_len +1);
	goto jleave;
j501cp:
	dmslp->dmsl_status_or_new_ent = 501;
	dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
	goto jleave;

jcmd_insert:{ /* {{{ */
	/* TODO LOGIC BELONGS head.c
	 * TODO That is: Header::factory(string) -> object (blahblah).
	 * TODO I.e., as long as we don't have regular RFC compliant parsers
	 * TODO which differentiate in between structured and unstructured
	 * TODO header fields etc., a little workaround */
	struct mx_name *xnp;
	s8 aerr;
	char const *mod_suff;
	enum expand_addr_check_mode eacm;
	enum gfield ntype;
	boole mult_ok;

	if(args == NIL || a3p == NIL)
		goto jecmd;
	if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
		goto j505r;

	/* Strip [\r\n] which would render a body invalid XXX all controls? */
	/* C99 */{
		char c;

		for(cp = a3p->ca_arg.ca_str.s; (c = *cp) != '\0'; ++cp)
			if(c == '\n' || c == '\r')
				*UNCONST(char*,cp) = ' ';
	}

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, a_dmsg_subj)){
		if(a3p->ca_arg.ca_str.l == 0)
			goto j501cp;

		if(hp->h_subject != NIL)
			hp->h_subject = savecatsep(hp->h_subject, ' ', a3p->ca_arg.ca_str.s);
		else
			hp->h_subject = savestr(a3p->ca_arg.ca_str.s);

		dmslp->dmsl_status_or_new_ent = 210;
		dmslp->dmsl_len = sizeof(a_dmsg_subj) -1;
		dmslp->dmsl_dat = a_dmsg_subj;

		dmslp->dmsl_next = x = su_LOFI_TCALLOC(struct a_dmsg_sl, 1);
		x->dmsl_len = 1;
		x->dmsl_dat = n_1;
		goto jleave;
	}

	mult_ok = TRU1;
	ntype = GEXTRA | GFULL | GFULLEXTRA;
	eacm = EACM_STRICT;
	mod_suff = NIL;

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
		npp = &hp->h_from;
jins:
		aerr = 0;
		/* todo As said above, this should be table driven etc., but.. */
		if(ntype & GBCC_IS_FCC){
			np = nalloc_fcc(a3p->ca_arg.ca_str.s);
			if(is_addr_invalid(np, eacm))
				goto jins_505;
		}else{
			np = ((mult_ok > FAL0) ? lextract : n_extract_single)(a3p->ca_arg.ca_str.s, (ntype | GNULL_OK));
			if(np == NIL)
				goto j501cp;

			if((np = checkaddrs(np, eacm, &aerr), aerr != 0)){
jins_505:
				dmslp->dmsl_status_or_new_ent = 505;
				dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
				goto jleave;
			}
		}

		/* Go to the end of the list, track whether it contains any non-deleted entries */
		i = 0;
		if((xnp = *npp) != NIL)
			for(;; xnp = xnp->n_flink){
				if(!(xnp->n_type & GDEL))
					++i;
				if(xnp->n_flink == NIL)
					break;
			}

		if(!mult_ok && (i != 0 || np->n_flink != NIL)){
			dmslp->dmsl_status_or_new_ent = 506;
			dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
		}else{
			if(xnp == NIL)
				*npp = np;
			else
				xnp->n_flink = np;
			np->n_blink = xnp;

			dmslp->dmsl_status_or_new_ent = 210;
			dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));

			i = su_cs_len(cp = su_ienc_uz(ienc_b, ++i, 10));
			dmslp->dmsl_next = x = su_LOFI_ALLOC(sizeof(struct a_dmsg_sl) + i +1);
			x->dmsl_next = NIL;
			x->dmsl_status_or_new_ent = FAL0;
			x->dmsl_len = S(u32,i);
			x->dmsl_dat = S(char*,&x[1]);
			su_mem_copy(UNCONST(char*,x->dmsl_dat), cp, i +1);
		}
		goto jleave;
	}

#undef a_X
#define a_X(F,H,INS) \
	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = F)) {npp = &hp->H; INS; goto jins;}

	/* Same as From:*/
	a_X("Author", h_author, UNUSED(0));

	/* Modifier? */
	if((cp = su_cs_find_c(args->ca_arg.ca_str.s, '?')) != NIL){
		mod_suff = cp;
		args->ca_arg.ca_str.s[P2UZ(cp - args->ca_arg.ca_str.s)] = '\0';
		if(*++cp != '\0' && !su_cs_starts_with_case("single", cp)){
			cp = mod_suff;
			goto j501cp;
		}
		mult_ok = TRUM1;
	}

	/* Just like with ~t,~c,~b, immediately test *expandaddr* compliance */
	a_X("To", h_to, ntype = GTO|GFULL su_COMMA eacm = EACM_NORMAL);
	a_X("Cc", h_cc, ntype = GCC|GFULL su_COMMA eacm = EACM_NORMAL);
	a_X("Bcc", h_bcc, ntype = GBCC|GFULL su_COMMA eacm = EACM_NORMAL);

	if((cp = mod_suff) != NIL)
		goto j501cp;

	/* Not | EAF_FILE, depend on *expandaddr*! */
	a_X("Fcc", h_fcc, ntype = GBCC|GBCC_IS_FCC su_COMMA eacm = EACM_NORMAL);
	a_X("Sender", h_sender, mult_ok = FAL0);
	a_X("Reply-To", h_reply_to, eacm = EACM_NONAME);
	a_X("Mail-Followup-To", h_mft, eacm = EACM_NONAME);
	a_X("Message-ID", h_message_id, mult_ok = FAL0 su_COMMA ntype = GREF su_COMMA eacm = EACM_NONAME);
	a_X("References", h_ref, ntype = GREF su_COMMA eacm = EACM_NONAME);
	a_X("In-Reply-To", h_in_reply_to, ntype = GREF su_COMMA eacm = EACM_NONAME);
#undef a_X

	if((cp = n_header_is_known(args->ca_arg.ca_str.s, UZ_MAX)) != NIL)
		goto j505r;

	/* Free-form header fields */
	/* C99 */{
		uz nl, bl;
		struct n_header_field **hfpp;

		for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
			if(!fieldnamechar(*cp)){
				cp = args->ca_arg.ca_str.s;
				goto j501cp;
			}

		for(i = 0, hfpp = &hp->h_user_headers; *hfpp != NIL; ++i)
			hfpp = &(*hfpp)->hf_next;

		nl = su_cs_len(cp = args->ca_arg.ca_str.s) +1;
		bl = su_cs_len(a3p->ca_arg.ca_str.s) +1;
		*hfpp = hfp = su_AUTO_ALLOC(VSTRUCT_SIZEOF(struct n_header_field,hf_dat) + nl + bl);
		hfp->hf_next = NIL;
		hfp->hf_nl = nl - 1;
		hfp->hf_bl = bl - 1;
		su_mem_copy(&hfp->hf_dat[0], cp, nl);
		su_mem_copy(&hfp->hf_dat[nl], a3p->ca_arg.ca_str.s, bl);

		dmslp->dmsl_status_or_new_ent = 210;
		dmslp->dmsl_dat = &hfp->hf_dat[0];
		dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat));

		cp = su_ienc_uz(ienc_b, ++i, 10);
		i = su_cs_len(cp);
		dmslp->dmsl_next = x = su_LOFI_ALLOC(sizeof(struct a_dmsg_sl) + i +1);
		x->dmsl_next = NIL;
		x->dmsl_status_or_new_ent = FAL0;
		x->dmsl_len = S(u32,i);
		x->dmsl_dat = S(char*,&x[1]);
		su_mem_copy(UNCONST(char*,x->dmsl_dat), cp, i +1);
	}

	goto jleave;
	} /* }}} */

jcmd_headerpick:{ /* TODO v15-compat: oooooh: an iterator! {{{ */
	struct n_header_field **hfpp;
	struct mx_ignore *ip;

	if(args == NIL)
		goto jecmd;
	if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
		goto j505r;

	cp = args->ca_arg.ca_str.s;
	if((ip = mx_ignore_by_name(cp)) == NIL)
		goto j501cp;

#undef a_X
#define a_X(F,S) \
	if(su_CONCAT(hp->h_, F) != NIL && mx_ignore_is_ign(ip, su_STRING(S)))\
		su_CONCAT(hp->h_, F) = NIL;

	a_X(subject, Subject);
	a_X(author, Author);
	a_X(from, From);
	a_X(sender, Sender);
	a_X(to, To);
	a_X(cc, Cc);
	a_X(bcc, Bcc);
	a_X(fcc, Fcc);
	a_X(reply_to, Reply-To);
	a_X(mft, Mail-Followup-To);
	a_X(message_id, Message-ID);
	a_X(ref, References);
	a_X(in_reply_to, In-Reply-To);

#undef a_X

	for(hfpp = &hp->h_user_headers; *hfpp != NIL;){
		if(mx_ignore_is_ign(ip, (*hfpp)->hf_dat))
			*hfpp = (*hfpp)->hf_next;
		else
			hfpp = &(*hfpp)->hf_next;
	}

	dmslp->dmsl_status_or_new_ent = 210;
	goto jleave;
	} /* }}} */

jcmd_list: jdefault:{ /* {{{ */
	if(args == NIL){
		n_string_trunc(sp, 0);

#undef a_X
#define a_X(F,S) \
		if(su_CONCAT(hp->h_, F) != NIL)\
			n_string_push_buf((sp->s_len > 0 ? n_string_push_c(sp, ' ') : sp),\
				su_STRING(S), sizeof(su_STRING(S)) -1);\

		a_X(subject, Subject);
		a_X(author, Author);
		a_X(from, From);
		a_X(sender, Sender);
		a_X(to, To);
		a_X(cc, Cc);
		a_X(bcc, Bcc);
		a_X(fcc, Fcc);
		a_X(reply_to, Reply-To);
		a_X(mft, Mail-Followup-To);
		a_X(message_id, Message-ID);
		a_X(ref, References);
		a_X(in_reply_to, In-Reply-To);

		a_X(mailx_raw_to, Mailx-Raw-To);
		a_X(mailx_raw_cc, Mailx-Raw-Cc);
		a_X(mailx_raw_bcc, Mailx-Raw-Bcc);
		a_X(mailx_orig_sender, Mailx-Orig-Sender);
		a_X(mailx_orig_from, Mailx-Orig-From);
		a_X(mailx_orig_to, Mailx-Orig-To);
		a_X(mailx_orig_cc, Mailx-Orig-Cc);
		a_X(mailx_orig_bcc, Mailx-Orig-Bcc)
#undef a_X

		if((hp->h_flags & HF_CMD_MASK) != HF_NONE){
			static char const ca[] = "Mailx-Command";

			n_string_push_buf((sp->s_len > 0 ? n_string_push_c(sp, ' ') : sp), ca, sizeof(ca) -1);
		}

		/* Print only one instance of each free-form header */
		for(hfp = hp->h_user_headers; hfp != NIL; hfp = hfp->hf_next){
			struct n_header_field *hfpx;

			for(hfpx = hp->h_user_headers;; hfpx = hfpx->hf_next){
				if(hfpx == hfp){
					n_string_push_cp((sp->s_len > 0 ? n_string_push_c(sp, ' ') : sp),
						&hfp->hf_dat[0]);
					break;
				}else if(!su_cs_cmp_case(&hfpx->hf_dat[0], &hfp->hf_dat[0]))
					break;
			}
		}

		dmslp->dmsl_status_or_new_ent = 210;
		if((dmslp->dmsl_len = sp->s_len) > 0){
			dmslp->dmsl_dat = su_LOFI_ALLOC(sp->s_len +1);
			su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), n_string_cp(sp), sp->s_len +1);
		}
	}else{
		if(a3p != NIL)
			goto jecmd;

		if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = a_dmsg_subj)){
			np = (hp->h_subject != NIL) ? R(struct mx_name*,-1) : NIL;
			goto jlist;
		}
		if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
			np = hp->h_from;
jlist:
			dmslp->dmsl_status_or_new_ent = (np == NIL) ? 501 : 210;
			dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
			goto jleave;
		}

#undef a_X
#define a_X(F,H) \
		if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
			np = hp->su_CONCAT(h_,H);\
			goto jlist;\
		}

		a_X(Author, author);
		a_X(Sender, sender);
		a_X(To, to);
		a_X(Cc, cc);
		a_X(Bcc, bcc);
		a_X(Fcc, fcc);
		a_X(Reply-To, reply_to);
		a_X(Mail-Followup-To, mft);
		a_X(Message-ID, message_id);
		a_X(References, ref);
		a_X(In-Reply-To, in_reply_to);

		a_X(Mailx-Raw-To, mailx_raw_to);
		a_X(Mailx-Raw-Cc, mailx_raw_cc);
		a_X(Mailx-Raw-Bcc, mailx_raw_bcc);
		a_X(Mailx-Orig-Sender, mailx_orig_sender);
		a_X(Mailx-Orig-From, mailx_orig_from);
		a_X(Mailx-Orig-To, mailx_orig_to);
		a_X(Mailx-Orig-Cc, mailx_orig_cc);
		a_X(Mailx-Orig-Bcc, mailx_orig_bcc);
#undef a_X

		if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Mailx-Command")){
			np = ((hp->h_flags & HF_CMD_MASK) != HF_NONE) ? R(struct mx_name*,-1) : NIL;
			goto jlist;
		}

		/* Free-form header fields */
		for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
			if(!fieldnamechar(*cp)){
				cp = args->ca_arg.ca_str.s;
				goto j501cp;
			}

		cp = args->ca_arg.ca_str.s;
		for(hfp = hp->h_user_headers;; hfp = hfp->hf_next){
			if(hfp == NIL)
				goto j501cp;
			else if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
				dmslp->dmsl_status_or_new_ent = 210;
				dmslp->dmsl_dat = &hfp->hf_dat[0];
				dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat));
				break;
			}
		}
	}

	goto jleave;
	} /* }}} */

jcmd_remove:{ /* {{{ */
	if(args == NIL || a3p != NIL)
		goto jecmd;
	if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
		goto j505r;

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, a_dmsg_subj)){
		if(hp->h_subject == NIL)
			goto j501cp;
		hp->h_subject = NIL;
		dmslp->dmsl_status_or_new_ent = 210;
		dmslp->dmsl_dat = a_dmsg_subj;
		dmslp->dmsl_len = sizeof(a_dmsg_subj) -1;
		goto jleave;
	}

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
		npp = &hp->h_from;
jrem:
		if(*npp != NIL){
			*npp = NIL;
			dmslp->dmsl_status_or_new_ent = 210;
			dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
			goto jleave;
		}else
			goto j501cp;
	}

#undef a_X
#define a_X(F,H) \
	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
		npp = &hp->su_CONCAT(h_,H);\
		goto jrem;\
	}

	a_X(Author, author);
	a_X(Sender, sender);
	a_X(To, to);
	a_X(Cc, cc);
	a_X(Bcc, bcc);
	a_X(Fcc, fcc);
	a_X(Reply-To, reply_to);
	a_X(Mail-Followup-To, mft);
	a_X(Message-ID, message_id);
	a_X(References, ref);
	a_X(In-Reply-To, in_reply_to);
#undef a_X

	if((cp = n_header_is_known(args->ca_arg.ca_str.s, UZ_MAX)) != NIL)
		goto j505r;

	/* Free-form header fields (note j501cp may print non-normalized name) */
	/* C99 */{
		struct n_header_field **hfpp;
		boole any;

		for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
			if(!fieldnamechar(*cp)){
				cp = args->ca_arg.ca_str.s;
				goto j501cp;
			}
		cp = args->ca_arg.ca_str.s;

		for(any = FAL0, hfpp = &hp->h_user_headers; (hfp = *hfpp) != NIL;){
			if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
				*hfpp = hfp->hf_next;
				if(!any){
					dmslp->dmsl_status_or_new_ent = 210;
					dmslp->dmsl_dat = &hfp->hf_dat[0];
					dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat));
				}
				any = TRU1;
			}else
				hfpp = &hfp->hf_next;
		}
		if(!any)
			goto j501cp;
	}

	goto jleave;
	} /* }}} */

jcmd_remove_at:{ /* {{{ */
	if(args == NIL || a3p == NIL)
		goto jecmd;
	if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
		goto j505r;

	if((su_idec_uz_cp(&i, a3p->ca_arg.ca_str.s, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
			) != su_IDEC_STATE_CONSUMED || i == 0){
		dmslp->dmsl_status_or_new_ent = 505;
		sp = n_string_trunc(sp, 0);
		sp = n_string_push_buf(sp, "invalid position: ", sizeof("invalid position: ") -1);
		sp = n_string_push_buf(sp, a3p->ca_arg.ca_str.s, a3p->ca_arg.ca_str.l);
		dmslp->dmsl_len = sp->s_len;
		dmslp->dmsl_dat = su_LOFI_ALLOC(sp->s_len +1);
		su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), n_string_cp(sp), sp->s_len +1);
		goto jleave;
	}

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, a_dmsg_subj)){
		if(hp->h_subject != NIL && i == 1){
			hp->h_subject = NIL;
			dmslp->dmsl_status_or_new_ent = 210;
			dmslp->dmsl_len = sizeof(a_dmsg_subj) -1;
			dmslp->dmsl_dat = a_dmsg_subj;

			dmslp->dmsl_next = x = su_LOFI_TCALLOC(struct a_dmsg_sl, 1);
			x->dmsl_len = 1;
			x->dmsl_dat = n_1;
			goto jleave;
		}else
			goto j501cp;
	}

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
		npp = &hp->h_from;
jremat:
		if((np = *npp) == NIL)
			goto j501cp;
		while(--i != 0 && np != NIL)
			np = np->n_flink;
		if(np == NIL)
			goto j501cp;

		if(np->n_blink != NIL)
			np->n_blink->n_flink = np->n_flink;
		else
			*npp = np->n_flink;
		if(np->n_flink != NIL)
			np->n_flink->n_blink = np->n_blink;

		dmslp->dmsl_status_or_new_ent = 210;
		dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
		goto jleave;
	}

#undef a_X
#define a_X(F,H) \
	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
		npp = &hp->su_CONCAT(h_,H);\
		goto jremat;\
	}

	a_X(Author, author);
	a_X(Sender, sender);
	a_X(To, to);
	a_X(Cc, cc);
	a_X(Bcc, bcc);
	a_X(Fcc, fcc);
	a_X(Reply-To, reply_to);
	a_X(Mail-Followup-To, mft);
	a_X(Message-ID, message_id);
	a_X(References, ref);
	a_X(In-Reply-To, in_reply_to);
#undef a_X

	if((cp = n_header_is_known(args->ca_arg.ca_str.s, UZ_MAX)) != NIL)
		goto j505r;

	/* Free-form header fields */
	/* C99 */{
		struct n_header_field **hfpp;

		for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
			if(!fieldnamechar(*cp)){
				cp = args->ca_arg.ca_str.s;
				goto j501cp;
			}
		cp = args->ca_arg.ca_str.s;

		for(hfpp = &hp->h_user_headers; (hfp = *hfpp) != NIL;){
			if(--i == 0){
				*hfpp = hfp->hf_next;

				dmslp->dmsl_status_or_new_ent = 210;
				dmslp->dmsl_dat = &hfp->hf_dat[0];
				dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat));

				i = su_cs_len(cp = su_ienc_uz(ienc_b, i, 10));
				dmslp->dmsl_next = x = su_LOFI_ALLOC(sizeof(struct a_dmsg_sl) + i +1);
				x->dmsl_next = NIL;
				x->dmsl_status_or_new_ent = FAL0;
				x->dmsl_len = S(u32,i);
				x->dmsl_dat = S(char*,&x[1]);
				su_mem_copy(UNCONST(char*,x->dmsl_dat), cp, i +1);
				break;
			}else
				hfpp = &hfp->hf_next;
		}
		if(hfp == NIL)
			goto j501cp;
	}

	goto jleave;
	} /* }}} */

jcmd_show:{ /* {{{ */
	if(args == NIL || a3p != NIL)
		goto jecmd;

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = a_dmsg_subj)){
		if((sin.s = hp->h_subject) == NIL)
			goto j501cp;
		sin.l = su_cs_len(sin.s);

		if(!mx_mime_display_from_header(&sin, &sou, mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT))
			goto j501cp;

		dmslp->dmsl_status_or_new_ent = 212;
		dmslp->dmsl_len = sizeof(a_dmsg_subj) -1;
		dmslp->dmsl_dat = a_dmsg_subj;

		dmslp->dmsl_next = x = su_LOFI_ALLOC(sizeof(struct a_dmsg_sl) + sou.l +1);
		x->dmsl_next = NIL;
		x->dmsl_status_or_new_ent = TRU1;
		x->dmsl_len = S(u32,sou.l);
		x->dmsl_dat = S(char*,&x[1]);
		su_mem_copy(UNCONST(char*,x->dmsl_dat), sou.s, sou.l +1);

		su_FREE(sou.s);
		goto jleave;
	}

	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "From")){
		np = hp->h_from;
jshow:
		if(np == NIL)
			goto j501cp;

		dmslp->dmsl_status_or_new_ent = 211;
		dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));

		x = NIL;
		for(x = NIL; np != NIL; np = np->n_flink){
			char const *intro, *a;

			switch(np->n_flags & mx_NAME_ADDRSPEC_ISMASK){
			case mx_NAME_ADDRSPEC_ISFILE: intro = n_hy; break;
			case mx_NAME_ADDRSPEC_ISPIPE: intro = "|"; break;
			case mx_NAME_ADDRSPEC_ISNAME: intro = n_ns; break;
			default: intro = np->n_name; break;
			}

			if((a = mx_mime_fromaddr(np->n_fullname)) != NIL){ /* XXX error?? */
				struct a_dmsg_sl *y;

				y = su_LOFI_TALLOC(struct a_dmsg_sl, 2);
				if(x != NIL)
					x->dmsl_next = y;
				else
					dmslp->dmsl_next = y;
				x = y;
				x->dmsl_status_or_new_ent = TRU1;
				x->dmsl_len = S(u32,su_cs_len(x->dmsl_dat = intro));

				y = &y[1];
				x->dmsl_next = y;
				x = y;
				x->dmsl_status_or_new_ent = FAL0;
				x->dmsl_next = NIL;
				x->dmsl_len = S(u32,su_cs_len(x->dmsl_dat = a));
			}
		}
		goto jleave;
	}

#undef a_X
#define a_X(F,H) \
	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = su_STRING(F))){\
		np = hp->su_CONCAT(h_,H);\
		goto jshow;\
	}

	a_X(Author, author);
	a_X(Sender, sender);
	a_X(To, to);
	a_X(Cc, cc);
	a_X(Bcc, bcc);
	a_X(Fcc, fcc);
	a_X(Reply-To, reply_to);
	a_X(Mail-Followup-To, mft);
	a_X(Message-ID, message_id);
	a_X(References, ref);
	a_X(In-Reply-To, in_reply_to);

	a_X(Mailx-Raw-To, mailx_raw_to);
	a_X(Mailx-Raw-Cc, mailx_raw_cc);
	a_X(Mailx-Raw-Bcc, mailx_raw_bcc);
	a_X(Mailx-Orig-Sender, mailx_orig_sender);
	a_X(Mailx-Orig-From, mailx_orig_from);
	a_X(Mailx-Orig-To, mailx_orig_to);
	a_X(Mailx-Orig-Cc, mailx_orig_cc);
	a_X(Mailx-Orig-Bcc, mailx_orig_bcc);
#undef a_X

	x = 0;
	if(!su_cs_cmp_case(args->ca_arg.ca_str.s, cp = "Mailx-Command")){
		if((i = hp->h_flags & HF_CMD_MASK) == HF_NONE)
			goto j501cp;

		dmslp->dmsl_status_or_new_ent = 212;
		dmslp->dmsl_len = sizeof("Mailx-Command") -1;
		dmslp->dmsl_dat = cp;

		dmslp->dmsl_next = x = su_LOFI_TCALLOC(struct a_dmsg_sl, 1);
		x->dmsl_status_or_new_ent = TRU1;
		x->dmsl_dat = a_dmsg_hf_cmd[HF_CMD_TO_OFF(i)];
		x->dmsl_len = S(u32,su_cs_len(x->dmsl_dat));
		goto jleave;
	}

	/* Free-form header fields */
	/* C99 */{
		boole any;

		for(cp = args->ca_arg.ca_str.s; *cp != '\0'; ++cp)
			if(!fieldnamechar(*cp)){
				cp = args->ca_arg.ca_str.s;
				goto j501cp;
			}
		cp = args->ca_arg.ca_str.s;

		for(any = FAL0, hfp = hp->h_user_headers; hfp != NIL; hfp = hfp->hf_next){
			if(!su_cs_cmp_case(cp, &hfp->hf_dat[0])){
				struct a_dmsg_sl *y;

				sin.s = &hfp->hf_dat[hfp->hf_nl +1];
				sin.l = su_cs_len(sin.s);
				mx_mime_display_from_header(&sin, &sou, mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT);

				y = su_LOFI_ALLOC(sizeof(struct a_dmsg_sl) + sou.l +1);

				if(any)
					x->dmsl_next = y;
				else{
					any = TRU1;
					dmslp->dmsl_status_or_new_ent = 212;
					dmslp->dmsl_dat = &hfp->hf_dat[0];
					dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat));
					dmslp->dmsl_next = y;
				}
				x = y;

				x->dmsl_next = NIL;
				x->dmsl_status_or_new_ent = TRU1;
				x->dmsl_len = S(u32,sou.l);
				x->dmsl_dat = S(char*,&x[1]);
				su_mem_copy(UNCONST(char*,x->dmsl_dat), sou.s, sou.l +1);

				su_FREE(sou.s);
			}
		}
		if(!any)
			goto j501cp;
	}

	goto jleave;
	} /* }}} */

jcmd_x_decode:{ /* TODO v15-compat: not at all needed! {{{ */
	struct mx_name **npp_a[12];
	struct n_header_field **hfpp, *hfpo;

	if(args != NIL)
		goto jecmd;
	if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
		goto j505r;

	if((sin.s = hp->h_subject) != NIL){
		sin.l = su_cs_len(sin.s);
		if(mx_mime_display_from_header(&sin, &sou, mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT)){
			hp->h_subject = savestrbuf(sou.s, sou.l);
			su_FREE(sou.s);
		}else
			hp->h_subject = NIL; /* XXX error */
	}

	npp_a[0] = &hp->h_author;
	npp_a[1] = &hp->h_from;
	npp_a[2] = &hp->h_sender;
	npp_a[3] = &hp->h_to;
	npp_a[4] = &hp->h_cc;
	npp_a[5] = &hp->h_bcc;
	npp_a[6] = &hp->h_fcc;
	npp_a[7] = &hp->h_reply_to;
	npp_a[8] = &hp->h_mft;
	npp_a[9] = &hp->h_message_id;
	npp_a[10] = &hp->h_ref;
	npp_a[11] = &hp->h_in_reply_to;

	for(i = 0; i < NELEM(npp_a); ++i){
		npp = npp_a[i];
		while((np = *npp) != NIL){
			if((np->n_name = mx_mime_fromaddr(np->n_name)) == NIL ||
					(np->n_fullname = mx_mime_fromaddr(np->n_fullname)) == NIL){
				/* XXX error */
				if((*npp = np->n_flink) != NIL)
					(*npp)->n_blink = np->n_blink;
			}else
				npp = &np->n_flink;
		}
	}

	hfpo = *(hfpp = &hp->h_user_headers);
	*hfpp = NIL;

	for(; hfpo != NIL; hfpo = hfpo->hf_next){
		sin.s = &hfpo->hf_dat[hfpo->hf_nl +1];
		sin.l = hfpo->hf_bl;
		if(mx_mime_display_from_header(&sin, &sou, mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT)){
			hfp = su_AUTO_ALLOC(VSTRUCT_SIZEOF(struct n_header_field,hf_dat) + hfpo->hf_nl +1 + sou.l +1);
			*hfpp = hfp;
				hfpp = &hfp->hf_next;
			hfp->hf_next = NIL;
			hfp->hf_nl = hfpo->hf_nl;
			hfp->hf_bl = S(u32,sou.l);
			su_mem_copy(hfp->hf_dat, hfpo->hf_dat, hfpo->hf_nl +1);
				su_mem_copy(&hfp->hf_dat[hfpo->hf_nl +1], sou.s, sou.l +1);
			su_FREE(sou.s);
		} /* XXX else error */
	}

	dmslp->dmsl_status_or_new_ent = 210;
	goto jleave;
	} /* }}} */
}
/* }}} */

/* a_dmsg__attach {{{ */
static struct a_dmsg_sl *
a_dmsg__attach(struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg *args, struct a_dmsg_sl *dmslp, struct n_string *sp){
	char ienc_b[su_IENC_BUFFER_SIZE];
	boole status;
	struct mx_attachment *ap;
	struct a_dmsg_sl *x;
	uz i;
	char const *cp;
	struct header *hp;
	NYD2_IN;

	hp = dmcp->dmc_hp;

	if(args == NIL){
		cp = su_empty; /* xxx not NIL anyway */
		goto jdefault;
	}

	cp = args->ca_arg.ca_str.s;
	args = args->ca_next;

	if(su_cs_starts_with_case("attribute", cp)){
		if(args == NIL || args->ca_next != NIL)
			goto jecmd;

		cp = args->ca_arg.ca_str.s;
		if((ap = mx_attachments_find(hp->h_attach, cp, NIL)) == NIL)
			goto j501;

jatt_att:
		dmslp->dmsl_status_or_new_ent = 212;
		dmslp->dmsl_len = S(u32,su_cs_len(cp));
		dmslp->dmsl_dat = cp;

		if(ap->a_msgno > 0)
			dmslp->dmsl_next = a_dmsg___line_tuple("message-number", FAL0,
					su_ienc_uz(ienc_b, ap->a_msgno, 10), TRU1);
		else{
			dmslp->dmsl_next = x = a_dmsg___line_tuple("creation-name", FAL0, ap->a_path_user, FAL0);
			x = x->dmsl_next;

			x->dmsl_next = a_dmsg___line_tuple("open-path", FAL0, ap->a_path, FAL0);
			x = x->dmsl_next->dmsl_next;

			x->dmsl_next = a_dmsg___line_tuple("filename", FAL0, ap->a_name, FAL0);
			x = x->dmsl_next->dmsl_next;

			if((cp = ap->a_content_description) != NIL){
				x->dmsl_next = a_dmsg___line_tuple("content-description", FAL0, cp, FAL0);
				x = x->dmsl_next->dmsl_next;
			}

			if(ap->a_content_id != NIL){
				x->dmsl_next = a_dmsg___line_tuple("content-id", FAL0, ap->a_content_id->n_name, FAL0);
				x = x->dmsl_next->dmsl_next;
			}

			if((cp = ap->a_content_type) != NIL){
				x->dmsl_next = a_dmsg___line_tuple("content-type", FAL0, cp, FAL0);
				x = x->dmsl_next->dmsl_next;
			}

			if((cp = ap->a_content_disposition) != NIL){
				x->dmsl_next = a_dmsg___line_tuple("content-disposition", FAL0, cp, FAL0);
				x = x->dmsl_next->dmsl_next;
			}
		}
	}else if(su_cs_starts_with_case("attribute-at", cp)){
		if(args == NIL || args->ca_next != NIL)
			goto jecmd;

		cp = args->ca_arg.ca_str.s;
		if((su_idec_uz_cp(&i, cp, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
				) != su_IDEC_STATE_CONSUMED || i == 0)
			goto j505invpos;

		for(ap = hp->h_attach; ap != NIL && --i != 0; ap = ap->a_flink){
		}
		if(ap != NIL)
			goto jatt_att;
		goto j501;
	}else if(su_cs_starts_with_case("attribute-set", cp)){
		/* ATT-ID KEYWORD VALUE */
		if(args == NIL)
			goto jecmd;

		cp = args->ca_arg.ca_str.s;
		args = args->ca_next;

		if(args == NIL || args->ca_next == NIL || args->ca_next->ca_next != NIL)
			goto jecmd;
		if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
			goto j505r;

		if((ap = mx_attachments_find(hp->h_attach, cp, NIL)) == NIL)
			goto j501;

jatt_attset:
		if(ap->a_msgno > 0){
			static char const ca[] = "RFC822 message attachment: ";

			dmslp->dmsl_status_or_new_ent = 505;
			sp = n_string_trunc(sp, 0);
			sp = n_string_push_buf(sp, ca, sizeof(ca) -1);
			sp = n_string_push_cp(sp, cp);
			dmslp->dmsl_len = sp->s_len;
			dmslp->dmsl_dat = su_LOFI_ALLOC(sp->s_len +1);
			su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), n_string_cp(sp), sp->s_len);
		}else{
			char c;
			char const *keyw, *xcp;

			keyw = args->ca_arg.ca_str.s;
			cp = args->ca_next->ca_arg.ca_str.s;

			for(xcp = cp; (c = *xcp) != '\0'; ++xcp)
				if(su_cs_is_cntrl(c))
					goto j505;
			c = *cp;

			if(!su_cs_cmp_case(keyw, "filename"))
				ap->a_name = (c == '\0') ? ap->a_path_bname : savestr(cp);
			else if(!su_cs_cmp_case(keyw, "content-description"))
				ap->a_content_description = (c == '\0') ? NIL : savestr(cp);
			else if(!su_cs_cmp_case(keyw, "content-id")){
				ap->a_content_id = NIL;

				if(c != '\0'){
					struct mx_name *np;

					/* XXX lextract->extract_single() */
					np = checkaddrs(lextract(cp, GREF),
							/*EACM_STRICT | TODO '/' valid!! */ EACM_NOLOG | EACM_NONAME,
							NIL);
					if(np != NIL && np->n_flink == NIL)
						ap->a_content_id = np;
					else
						cp = NIL;
				}
			}else if(!su_cs_cmp_case(keyw, "content-type")){
				if((ap->a_content_type = (c == '\0') ? NIL : (cp = savestr(cp))) != NIL){
					char *cp2;

					for(cp2 = UNCONST(char*,cp); (c = *cp++) != '\0';)
						*cp2++ = su_cs_to_lower(c);

					if(!mx_mime_type_is_valid(ap->a_content_type, TRU1, FAL0)){
						ap->a_content_type = NIL;
						goto j505;
					}
				}
			}else if(!su_cs_cmp_case(keyw, "content-disposition"))
				ap->a_content_disposition = (c == '\0') ? NIL : savestr(cp);
			else
				cp = NIL;

			if(cp != NIL){
				for(i = 0; ap != NIL; ++i, ap = ap->a_blink){
				}
				dmslp->dmsl_status_or_new_ent = 210;
				i = su_cs_len(cp = su_ienc_uz(ienc_b, i, 10));
				dmslp->dmsl_len = S(u32,i);
				dmslp->dmsl_dat = su_LOFI_ALLOC(i +1);
				su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), cp, i +1);
			}else{
				cp = xcp;
				goto j505; /* xxx jecmd; */
			}
		}
	}else if(su_cs_starts_with_case("attribute-set-at", cp)){
		cp = args->ca_arg.ca_str.s;
		args = args->ca_next;

		if(args == NIL || args->ca_next == NIL || args->ca_next->ca_next != NIL)
			goto jecmd;
		if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
			goto j505r;

		if((su_idec_uz_cp(&i, cp, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
				) != su_IDEC_STATE_CONSUMED || i == 0)
			goto j505invpos;

		for(ap = hp->h_attach; ap != NIL && --i != 0; ap = ap->a_flink){
		}
		if(ap != NIL)
			goto jatt_attset;
		goto j501;
	}else if(su_cs_starts_with_case("insert", cp)){
		BITENUM(u32,mx_attach_error) aerr;

		if(args == NIL || args->ca_next != NIL)
			goto jecmd;
		if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
			goto j505r;

		hp->h_attach = mx_attachments_append(hp->h_attach, args->ca_arg.ca_str.s, &aerr, &ap);
		switch(aerr){
		case mx_ATTACHMENTS_ERR_FILE_OPEN: i = 505; goto jatt__ins;
		case mx_ATTACHMENTS_ERR_ICONV_FAILED: i = 506; goto jatt__ins;
		case mx_ATTACHMENTS_ERR_ICONV_NAVAIL: /* FALLTHRU */
		case mx_ATTACHMENTS_ERR_OTHER: /* FALLTHRU */
		default:
			i = 501;
jatt__ins:
			dmslp->dmsl_status_or_new_ent = S(u32,i);
			dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = args->ca_arg.ca_str.s));
			break;
		case mx_ATTACHMENTS_ERR_NONE:
			for(i = 0; ap != NIL; ++i, ap = ap->a_blink){
			}
			dmslp->dmsl_status_or_new_ent = 210;
			i = su_cs_len(cp = su_ienc_uz(ienc_b, i, 10));
			dmslp->dmsl_len = S(u32,i);
			dmslp->dmsl_dat = su_LOFI_ALLOC(i +1);
			su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), cp, i +1);
			break;
		}
	}else if(su_cs_starts_with_case("list", cp)){
jdefault:
		if(args != NIL)
			goto jecmd;

		if((ap = hp->h_attach) == NIL)
			goto j501;

		dmslp->dmsl_status_or_new_ent = 212;
		x = dmslp;
		do{
			x->dmsl_next = su_LOFI_TALLOC(struct a_dmsg_sl, 1);
			x = x->dmsl_next;
			x->dmsl_next = NIL;
			x->dmsl_status_or_new_ent = TRU1;
			x->dmsl_len = S(u32,su_cs_len(x->dmsl_dat = ap->a_path_user));
		}while((ap = ap->a_flink) != NIL);
	}else if(su_cs_starts_with_case("remove", cp)){
		if(args == NIL || args->ca_next != NIL)
			goto jecmd;
		if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
			goto j505r;

		cp = args->ca_arg.ca_str.s;
		if((ap = mx_attachments_find(hp->h_attach, cp, &status)) == NIL)
			goto j501;
		if(status == TRUM1)
			goto j506;

		hp->h_attach = mx_attachments_remove(hp->h_attach, ap);
		dmslp->dmsl_status_or_new_ent = 210;
		dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
	}else if(su_cs_starts_with_case("remove-at", cp)){
		if(args == NIL || args->ca_next != NIL)
			goto jecmd;
		if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
			goto j505r;

		cp = args->ca_arg.ca_str.s;
		if((su_idec_uz_cp(&i, cp, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
				) != su_IDEC_STATE_CONSUMED || i == 0)
			goto j505invpos;

		for(ap = hp->h_attach; ap != NIL && --i != 0; ap = ap->a_flink){
		}
		if(ap != NIL){
			hp->h_attach = mx_attachments_remove(hp->h_attach, ap);
			dmslp->dmsl_status_or_new_ent = 210;
			dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
		}else
			goto j501;
	}else
		goto jecmd;

jleave:
	NYD2_OU;
	return dmslp;

jecmd:
	dmslp->dmsl_status_or_new_ent = 500;
	goto jleave;
j501:
	dmslp->dmsl_status_or_new_ent = 501;
	goto jleave;
j505:
	dmslp->dmsl_status_or_new_ent = 505;
	goto jleave;
j505r:
	dmslp->dmsl_status_or_new_ent = 505;
	sp = n_string_assign_buf(sp, "read-only: ", sizeof("read-only: ") -1);
	goto jeapp;
j505invpos:
	dmslp->dmsl_status_or_new_ent = 505;
	sp = n_string_assign_buf(sp, "invalid position: ", sizeof("invalid position: ") -1);
jeapp:
	sp = n_string_push_cp(sp, cp);
	dmslp->dmsl_len = sp->s_len;
	dmslp->dmsl_dat = su_LOFI_ALLOC(sp->s_len +1);
	su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), n_string_cp(sp), sp->s_len +1);
	goto jleave;
j506:
	dmslp->dmsl_status_or_new_ent = 506;
	goto jleave;
}
/* }}} */

/* a_dmsg__part X-SERIES {{{ */
static struct a_dmsg_sl *
a_dmsg__part(struct mx_dig_msg_ctx *dmcp, struct mx_cmd_arg * volatile args, struct a_dmsg_sl *dmslp, struct n_string *sp){
	/* TODO `part': all of this is a gross hack to be able to write a simple
	 * TODO mailing-list manager; after the MIME rewrite we have a tree of
	 * TODO objects which can be created/deleted at will, and which know how to
	 * TODO dump_to_{wire,user} themselves.  Until then we cut out bytes */
	struct a_dmsg_sl *x;
	struct mimepart *mp, *xmp, **mpp;
	char const * volatile cp;
	NYD2_IN;

	if(args == NIL){
		cp = su_empty; /* xxx not NIL anyway */
		goto jdefault;
	}

	cp = args->ca_arg.ca_str.s;
	args = args->ca_next;

#ifdef mx_HAVE_REGEX
	if(su_cs_starts_with_case("x-dump", cp))
		goto jdump;
#endif
	if(su_cs_starts_with_case("list", cp)){
jdefault:
		if(args != NIL)
			goto jecmd;

		if((mp = dmcp->dmc_mime) == NIL)
			goto j501;

		dmslp->dmsl_status_or_new_ent = 212;
		x = dmslp;
		while(mp != NIL){
			if(mp->m_mime_type != mx_MIME_TYPE_DISCARD){
				x->dmsl_next = su_LOFI_TCALLOC(struct a_dmsg_sl, 1);
				x = x->dmsl_next;
				x->dmsl_status_or_new_ent = TRU1;
				x->dmsl_len = S(u32,su_cs_len(x->dmsl_dat = mp->m_ct_type_plain));
			}

			if(mp->m_multipart != NIL)
				mp = mp->m_multipart;

			while(mp->m_nextpart == NIL){
				if(mp->m_parent == NIL)
					break;
				mp = mp->m_parent;
			}
			mp = mp->m_nextpart;
		}
	}else if(su_cs_starts_with_case("remove-at", cp)){
		uz i;

		if(args == NIL || args->ca_next != NIL)
			goto jecmd;
		if(dmcp->dmc_flags & mx__DIG_MSG_RDONLY)
			goto j505r;

		cp = args->ca_arg.ca_str.s;
		if((su_idec_uz_cp(&i, cp, 0, NIL) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
				) != su_IDEC_STATE_CONSUMED || i == 0)
			goto j505invpos;

		if((mp = dmcp->dmc_mime) == NIL)
			goto j501;

		while(mp != NIL){
			if(mp->m_mime_type != mx_MIME_TYPE_DISCARD && --i == 0)
				break;

			if(mp->m_multipart != NIL)
				mp = mp->m_multipart;

			while(mp->m_nextpart == NIL){
				if(mp->m_parent == NIL)
					break;
				mp = mp->m_parent;
			}
			mp = mp->m_nextpart;
		}

		if(mp == NIL)
			goto j501;
		/* Following: EVERYTHING is a HACK: should be mp->delete(). */
		/* Cannot delete main part (for now) */
		if(mp == dmcp->dmc_mime)
			goto j505invpos;

		if((xmp = mp->m_parent) == NIL)
			xmp = dmcp->dmc_mime;
		if(mp == xmp->m_multipart){
			/* Cannot cut out sole part, since the first boundary is included in the content of m_parent,
			 * therefore after that the closing boundary would immediately follow */
			if(mp->m_nextpart == NIL || mp->m_nextpart->m_nextpart == NIL)
				goto j501;
			mpp = &xmp->m_multipart;
		}else{
			struct mimepart *xok;

			for(xmp = xok = xmp->m_multipart; xmp->m_nextpart != mp; xmp = xmp->m_nextpart)
				if(xmp->m_mime_type != mx_MIME_TYPE_DISCARD)
					xok = xmp;
			mpp = &xok->m_nextpart;
		}

		mp = mp->m_nextpart;
		if(mp->m_mime_type != mx_MIME_TYPE_DISCARD)
			goto j501;
		if(mp->m_nextpart != NIL)
			mp = mp->m_nextpart;
		*mpp = mp;

		dmslp->dmsl_status_or_new_ent = 210;
		dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat = cp));
	}else
		goto jecmd;

jleave:
	NYD2_OU;
	return dmslp;

jecmd:
	dmslp->dmsl_status_or_new_ent = 500;
	goto jleave;
j501:
	dmslp->dmsl_status_or_new_ent = 501;
	goto jleave;
#ifdef mx_HAVE_REGEX
j505:
	dmslp->dmsl_status_or_new_ent = 505;
	goto jleave;
#endif
j505r:
	dmslp->dmsl_status_or_new_ent = 505;
	sp = n_string_assign_buf(sp, "read-only: ", sizeof("read-only: ") -1);
	goto jeapp;
j505invpos:
	dmslp->dmsl_status_or_new_ent = 505;
	sp = n_string_assign_buf(sp, "invalid position: ", sizeof("invalid position: ") -1);
jeapp:
	sp = n_string_push_cp(sp, cp);
	dmslp->dmsl_len = sp->s_len;
	dmslp->dmsl_dat = su_LOFI_ALLOC(sp->s_len +1);
	su_mem_copy(UNCONST(char*,dmslp->dmsl_dat), n_string_cp(sp), sp->s_len +1);
	goto jleave;
/*
j506:
	dmslp->dmsl_status_or_new_ent = 506;
	goto jleave;
*/

#ifdef mx_HAVE_REGEX
jdump:{ /* X-SERIES */
	struct n_sigman sm;
	FILE * volatile ofp;

	if(args == NIL || args->ca_next != NIL)
		goto jecmd;
	if(!su_cs_cmp(args->ca_arg.ca_str.s, n_hy))
		ofp = n_stdout;
	else if((ofp = mx_fop_get_file_by_cp(args->ca_arg.ca_str.s)) == NIL)
		goto j505;

	if((mp = dmcp->dmc_mime) == NIL)
		goto j501;

	n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
	case 0:
		su_mem_bag_pop(su_MEM_BAG_SELF, dmcp->dmc_membag);
		break;
	default:
jedump_506:
		su_mem_bag_push(su_MEM_BAG_SELF, dmcp->dmc_membag);

		dmslp->dmsl_status_or_new_ent = 506;

		n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
		goto jleave;
	}

	if(!mx_sendout_temporary_digdump(ofp, mp, dmcp->dmc_hp, FAL0))
		goto jedump_506; /* TODO document that partial output may exist! */

	while(mp != NIL){
		if(mp->m_multipart != NIL)
			mp = mp->m_multipart;

		if(!mx_sendout_temporary_digdump(ofp, mp, NIL, (mp == dmcp->dmc_mime)))
			goto jedump_506; /* TODO document that partial output may exist! */

		while(mp->m_nextpart == NIL){
			if(mp->m_parent == NIL)
				break;
			mp = mp->m_parent;
		}
		mp = mp->m_nextpart;
	}

	n_sigman_cleanup_ping(&sm);

	su_mem_bag_push(su_MEM_BAG_SELF, dmcp->dmc_membag);

	n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);

	dmslp->dmsl_status_or_new_ent = 210;
	dmslp->dmsl_dat = (args == NIL) ? n_hy : args->ca_arg.ca_str.s;
	dmslp->dmsl_len = S(u32,su_cs_len(dmslp->dmsl_dat));
	}goto jleave;
#endif /* mx_HAVE_REGEX */
}
/* }}} */

/* a_dmsg___line_tuple {{{ */
static struct a_dmsg_sl *
a_dmsg___line_tuple(char const *name, boole ndup, char const *value, boole vdup){
	char *cp;
	struct a_dmsg_sl *rv, *x;
	uz l1, l2;
	NYD2_IN;

	l1 = su_cs_len(name);
	l2 = su_cs_len(value);

	rv = su_LOFI_ALLOC((sizeof(struct a_dmsg_sl) * 2) + (ndup ? l1 +1 : 0) + (vdup ? l2 +1 : 0));
	cp = S(char*,&rv[2]);

	x = &rv[0];
	x->dmsl_next = &rv[1];
	x->dmsl_status_or_new_ent = TRU1;
	x->dmsl_len = S(u32,l1);
	if(!ndup)
		x->dmsl_dat = name;
	else{
		x->dmsl_dat = cp;
		su_mem_copy(cp, name, ++l1);
		cp += l1;
	}

	x = &rv[1];
	x->dmsl_next = NIL;
	x->dmsl_status_or_new_ent = FAL0;
	x->dmsl_len = S(u32,l2);
	if(!vdup)
		x->dmsl_dat = value;
	else{
		x->dmsl_dat = cp;
		su_mem_copy(cp, value, ++l2);
	}

	NYD2_OU;
	return rv;
}
/* }}} */

void
mx_dig_msg_on_mailbox_close(struct mailbox *mbp){ /* XXX HACK <- event! */
	struct mx_dig_msg_ctx *dmcp;
	NYD_IN;

	while((dmcp = mbp->mb_digmsg) != NIL){
		ASSERT(!(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE));
		mbp->mb_digmsg = dmcp->dmc_next;
		if(dmcp->dmc_flags & mx__DIG_MSG_OWN_FD)
			fclose(dmcp->dmc_fp);
		if(dmcp->dmc_flags & mx__DIG_MSG_OWN_MEMBAG)
			su_mem_bag_gut(dmcp->dmc_membag);
		su_FREE(dmcp);
	}

	NYD_OU;
}

/* c_digmsg {{{ */
int
c_digmsg(void * volatile vp){
	struct n_sigman sm;
	char const *cp, *emsg;
	struct mx_dig_msg_ctx *dmcp;
	struct mx_cmd_arg * volatile cap;
	struct mx_cmd_arg_ctx *cacp;
	boole volatile have_sm;
	NYD_IN;

	n_pstate_err_no = su_ERR_NONE;
	have_sm = FAL0;
	cacp = vp;
	cap = cacp->cac_arg;

	if(su_cs_starts_with_case("create", cp = cap->ca_arg.ca_str.s) || su_cs_starts_with("x-create"/*X-SERIES*/, cp)){
		if(cacp->cac_no < 2 || cacp->cac_no > 3) /* XXX argparse is stupid */
			goto jesynopsis;
		cap = cap->ca_next;

		/* Request to use STDOUT? */
		if(cacp->cac_no == 3){
			cp = cap->ca_next->ca_arg.ca_str.s;
			if(cp[1] != '\0' || (cp[0] != '-' && cp[0] != '^')){
				emsg = N_("digmsg: create: invalid I/O channel: %s\n");
				goto jeinval_quote;
			}
		}

		/* First of all, our context object */
		switch(a_dmsg_find(cp = cap->ca_arg.ca_str.s, &dmcp, TRU1)){
		case su_ERR_INVAL:
			emsg = N_("digmsg: create: message number invalid: %s\n");
			goto jeinval_quote;
		case su_ERR_EXIST:
			emsg = N_("digmsg: create: message object already exists: %s\n");
			goto jeinval_quote;
		default:
			break;
		}

		if(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE)
			dmcp->dmc_flags = mx__DIG_MSG_COMPOSE | mx__DIG_MSG_COMPOSE_DIGGED;
		else{
			FILE *fp;

			if((fp = setinput(&mb, dmcp->dmc_mp,
					 (*cacp->cac_arg->ca_arg.ca_str.s == 'x'/*X-SERIES*/ ? NEED_BODY : NEED_HEADER))
					) == NIL){
				/* XXX Should have panicked before.. */
				su_FREE(dmcp);
				emsg = N_("digmsg: create: mailbox I/O error for message: %s\n");
				goto jeinval_quote;
			}

			have_sm = TRU1;
			n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
			case 0:
				su_mem_bag_push(su_MEM_BAG_SELF, dmcp->dmc_membag);
				break;
			default:
				su_mem_bag_pop(su_MEM_BAG_SELF, dmcp->dmc_membag);
				vp = NIL;
				goto jeremove;
			}

			/* XXX n_header_extract error!! */
			n_header_extract((n_HEADER_EXTRACT_FULL | n_HEADER_EXTRACT_PREFILL_RECIPIENTS |
					n_HEADER_EXTRACT_IGNORE_FROM_), fp, dmcp->dmc_hp, NIL);

			/* X-SERIES */
			if(*cacp->cac_arg->ca_arg.ca_str.s == 'x'){
				if((dmcp->dmc_mime = mx_mime_parse_msg(dmcp->dmc_mp, mx_MIME_PARSE_PARTS)) == NIL){
					su_mem_bag_pop(su_MEM_BAG_SELF, dmcp->dmc_membag);
					n_err(_("digmsg: create: cannot parse MIME\n"));
					vp = NIL;
					goto jeremove;
				}

				/* TODO for now x-create strips RDONLY; we need a regular approach
				 * TODO though, the question is how?  Strip on request?  Keep in
				 * TODO mind original state nonetheless? Allow saving/dumping?? */
				dmcp->dmc_flags &= ~mx__DIG_MSG_RDONLY;
			}

			n_sigman_cleanup_ping(&sm);

			su_mem_bag_pop(su_MEM_BAG_SELF, dmcp->dmc_membag);
		}

		if(cacp->cac_no == 3){
			cap = cap->ca_next;
			if(cap->ca_arg.ca_str.s[1] != '\0')
				goto jesynopsis;
			switch(cap->ca_arg.ca_str.s[0]){
			case '^':
				dmcp->dmc_flags |= mx__DIG_MSG_MODE_CARET;
				break;
			case '-':
				dmcp->dmc_flags |= mx__DIG_MSG_MODE_FP;
				dmcp->dmc_fp = n_stdout;
				break;
			default:
				goto jesynopsis;
			}
		}
		/* We must set mx_FS_O_NOREGISTER to avoid cleanup on main loop tick!
		 * For *the* compose mode object (_DIGGED prevents multi-instance opens) do not mind, but to avoid too
		 * many stale descriptors for multiple create/remove, special-case their removals */
		else if((dmcp->dmc_fp = mx_fs_tmp_open(NIL, "digmsg",
				(mx_FS_O_RDWR | mx_FS_O_UNLINK |
				 ((dmcp->dmc_flags & mx__DIG_MSG_COMPOSE) ? 0 : mx_FS_O_NOREGISTER)), NIL)) != NIL)
			dmcp->dmc_flags |= mx__DIG_MSG_MODE_FP | mx__DIG_MSG_OWN_FD;
		else{
			n_err(_("digmsg: create: cannot create temporary file: %s\n"),
				su_err_doc(n_pstate_err_no = su_err()));
			vp = NIL;
			goto jeremove;
		}

		if(!(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE)){
			dmcp->dmc_last = NIL;
			if((dmcp->dmc_next = mb.mb_digmsg) != NIL)
				dmcp->dmc_next->dmc_last = dmcp;
			mb.mb_digmsg = dmcp;
		}
	}else if(su_cs_starts_with_case("remove", cp)){
		if(cacp->cac_no != 2)
			goto jesynopsis;
		cap = cap->ca_next;

		switch(a_dmsg_find(cp = cap->ca_arg.ca_str.s, &dmcp, FAL0)){
		case su_ERR_INVAL:
			emsg = N_("digmsg: remove: message number invalid: %s\n");
			goto jeinval_quote;
		default:
			if(!(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE) || (dmcp->dmc_flags & mx__DIG_MSG_COMPOSE_DIGGED))
				break;
			/* FALLTHRU */
		case su_ERR_NOENT:
			emsg = N_("digmsg: remove: no such message object: %s\n");
			goto jeinval_quote;
		}

		if(!(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE)){
			if(dmcp->dmc_last != NIL)
				dmcp->dmc_last->dmc_next = dmcp->dmc_next;
			else{
				ASSERT(dmcp == mb.mb_digmsg);
				mb.mb_digmsg = dmcp->dmc_next;
			}
			if(dmcp->dmc_next != NIL)
				dmcp->dmc_next->dmc_last = dmcp->dmc_last;
		}

		if(dmcp->dmc_flags & mx__DIG_MSG_MODE_FP){
			if(mx_dig_msg_read_overlay == dmcp)
				mx_dig_msg_read_overlay = NIL;

			if(dmcp->dmc_flags & mx__DIG_MSG_OWN_FD){
				if(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE)
					mx_fs_close(dmcp->dmc_fp);
				else
					fclose(dmcp->dmc_fp);
			}
		}
jeremove:
		if(dmcp->dmc_flags & mx__DIG_MSG_OWN_MEMBAG){
			ASSERT(!(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE));
			su_mem_bag_gut(dmcp->dmc_membag);
		}

		/* For compose mode default object simply restore defaults */
		if(dmcp->dmc_flags & mx__DIG_MSG_COMPOSE){
			dmcp->dmc_flags = mx__DIG_MSG_COMPOSE_FLAGS;
			dmcp->dmc_fp = n_stdout;
		}else
			su_FREE(dmcp);
	}else{
		switch(a_dmsg_find(cp, &dmcp, FAL0)){
		case su_ERR_INVAL:
			emsg = N_("digmsg: message number invalid: %s\n");
			goto jeinval_quote;
		case su_ERR_NOENT:
			emsg = N_("digmsg: no such message object: %s\n");
			goto jeinval_quote;
		default:
			break;
		}
		cap = cap->ca_next;

		if(dmcp->dmc_flags & mx__DIG_MSG_MODE_FP){
			if(dmcp->dmc_flags & mx__DIG_MSG_OWN_FD){
				rewind(dmcp->dmc_fp);
				ftruncate(fileno(dmcp->dmc_fp), 0);
			}else
				clearerr(dmcp->dmc_fp);
		}

		have_sm = TRU1;
		n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
		case 0:
			su_mem_bag_push(su_MEM_BAG_SELF, dmcp->dmc_membag);
			ASSERT(su_mem_bag_top(su_MEM_BAG_SELF) == dmcp->dmc_membag);

			if(!a_dmsg_cmd(dmcp->dmc_fp, dmcp, cap, ((cap != NIL) ? cap->ca_next : NIL)))
				vp = NIL;
			break;
		default:
			vp = NIL;
			break;
		}

		n_sigman_cleanup_ping(&sm);

		su_mem_bag_pop(su_MEM_BAG_SELF, dmcp->dmc_membag);

		if(dmcp->dmc_flags & mx__DIG_MSG_MODE_FP){
			if(dmcp->dmc_flags & mx__DIG_MSG_OWN_FD)
				rewind(dmcp->dmc_fp);
			else
				clearerr(dmcp->dmc_fp);
			/* This will be reset by go_input() _if_ we read to EOF */
			mx_dig_msg_read_overlay = dmcp;
		}
	}

jleave:
	if(have_sm)
		n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);

	NYD_OU;
	return (vp != NIL ? su_EX_OK : su_EX_ERR);

jesynopsis:
	mx_cmd_print_synopsis(mx_cmd_by_arg_desc(cacp->cac_desc), NIL);
	goto jeinval;
jeinval_quote:
	emsg = V_(emsg);
	n_err(emsg, n_shexp_quote_cp(cp, FAL0));
jeinval:
	n_pstate_err_no = su_ERR_INVAL;
	vp = NIL;
	goto jleave;
}
/* }}} */

/* mx_dig_msg_caret {{{ */
boole
mx_dig_msg_caret(enum mx_scope scope, boole force_mode_caret, char const *cmd){
	/* Identical to (subset of) c_digmsg() cmd-tab */
	mx_CMD_ARG_DESC_SUBCLASS_DEF_NAME(dm, "digmsg", 5, pseudo_cad){
		{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_HONOUR_STOP,
			n_SHEXP_PARSE_IGN_EMPTY | n_SHEXP_PARSE_TRIM_IFSSPACE},
		{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
			n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg1 */
		{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
			n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg2 */
		{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP,
			n_SHEXP_PARSE_TRIM_IFSSPACE}, /* arg3 */
		{mx_CMD_ARG_DESC_SHEXP | mx_CMD_ARG_DESC_OPTION | mx_CMD_ARG_DESC_HONOUR_STOP |
				mx_CMD_ARG_DESC_GREEDY | mx_CMD_ARG_DESC_GREEDY_JOIN,
			n_SHEXP_PARSE_TRIM_IFSSPACE} /* arg4 */
	}mx_CMD_ARG_DESC_SUBCLASS_DEF_END;

	struct mx_cmd_arg_ctx cac;
	struct su_mem_bag membag;
	boole rv;
	struct mx_dig_msg_ctx *dmcp;
	NYD_IN;

	dmcp = mx_dig_msg_compose_ctx;

	/*ASSERT(su_mem_bag_top(su_MEM_BAG_SELF) == dmcp->dmc_membag);*/
	su_mem_bag_push(su_MEM_BAG_SELF, su_mem_bag_create(&membag, 0));

	cac.cac_desc = mx_CMD_ARG_DESC_SUBCLASS_CAST(&pseudo_cad);
	cac.cac_indat = cmd;
	cac.cac_inlen = UZ_MAX;
	cac.cac_msgflag = cac.cac_msgmask = 0;

	rv = mx_cmd_arg_parse(&cac, scope, FAL0);

	if(rv){
		BITENUM(u32,mx_dig_msg_flags) f;

		su_mem_bag_push(su_MEM_BAG_SELF, dmcp->dmc_membag);
		ASSERT(su_mem_bag_top(su_MEM_BAG_SELF) == dmcp->dmc_membag);

		f = dmcp->dmc_flags;
		dmcp->dmc_flags = ((f & ~mx__DIG_MSG_MODE_MASK) |
				(force_mode_caret ? mx__DIG_MSG_MODE_CARET : mx__DIG_MSG_MODE_FP));
		rv = a_dmsg_cmd(n_stdout, dmcp, cac.cac_arg, cac.cac_arg->ca_next);
		dmcp->dmc_flags = f;

		su_mem_bag_pop(su_MEM_BAG_SELF, dmcp->dmc_membag);

		/*if(!rv)
		 *	clearerr(n_stdout); XXX no: yet done in caller */
	}

	su_mem_bag_pop(su_MEM_BAG_SELF, &membag);
	su_mem_bag_gut(&membag);

	NYD_OU;
	return rv;
}
/* }}} */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_DIG_MSG
/* s-itt-mode */
