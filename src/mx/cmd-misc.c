/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Miscellaneous user commands, like `echo', `pwd', etc.
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
#define su_FILE cmd_misc
#define mx_SOURCE
#define mx_SOURCE_CMD_MISC

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/utsname.h>

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/sort.h>

#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/sigs.h"

#include "mx/cmd-misc.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Expand the shell escape by expanding unescaped !'s into the last issued command where possible */
static char const *a_cmisc_bangexp(char const *cp);

/* c_n?echo(), c_n?echoerr() */
static int a_cmisc_echo(void *vp, FILE *fp, boole donl);

/* c_read(), c_readsh() */
static int a_cmisc_read(void *vp, boole atifs);
static boole a_cmisc_read_set(char const *cp, char const *value, enum mx_scope scope);

/* c_version() */
static sz a_cmisc_version_cmp(void const *s1, void const *s2);

static char const *
a_cmisc_bangexp(char const *cp){
	/* SysV 10 and POSIX.1-2023 compatible */
	struct n_string bang_i, *bang;
	char c;
	char const *obang;
	boole doit, bangit, changed;
	NYD_IN;

	bang = n_string_reserve(n_string_creat_auto(&bang_i), 110);
	doit = !(n_pstate & n_PS_ROBOT);
	bangit = (doit && ok_blook(bang));
	obang = bangit ? ok_vlook(bang_data) : NIL;
	changed = FAL0;

	for(; (c = *cp++) != '\0';){
		if(bangit && c == '!'){
			changed = TRU1;
			if(obang != NIL && *obang != '\0')
				bang = n_string_push_cp(bang, obang);
		}else{
			if(c == '\\' && *cp == '!'){
				changed = TRU1;
				++cp;
				c = '!';
			}
			bang = n_string_push_c(bang, c);
		}
	}

	cp = n_string_cp(bang);

	if(doit){
		n_PS_ROOT_BLOCK(ok_vset(bang_data, cp));

		if(changed && (n_psonce & n_PSO_INTERACTIVE))
			fprintf(n_stdout, "!%s\n", cp);
	}

	/*n_string_gut(bang);*/

	NYD_OU;
	return cp;
}

static int
a_cmisc_echo(void *vp, FILE *fp, boole donl){/* TODO -t=enable FEXP!! */
	struct n_string s_b, *s;
	int rv;
	char const *cp;
	struct mx_cmd_arg *cap;
	boole doerr;
	struct mx_cmd_arg_ctx *cacp;
	NYD2_IN;

	s = n_string_reserve(n_string_creat_auto(&s_b), 121/* XXX */);
	cacp = vp;
	doerr = (fp == n_stderr);

	for(cap = cacp->cac_arg; cap != NIL; cap = cap->ca_next){
		if(cap != cacp->cac_arg)
			s = n_string_push_c(s, ' ');
		/* TODO -t/-T en/disable if((cp = fexpand(*ap, FEXP_NVAR)) == NIL)*/
		s = n_string_push_buf(s, cap->ca_arg.ca_str.s, cap->ca_arg.ca_str.l);
	}
	if(donl)
		s = n_string_push_c(s, '\n');
	cp = n_string_cp(s);

	if(cacp->cac_vput == NIL){
		s32 e;

		e = su_ERR_NONE;
		if(doerr){
			/* xxx Ensure *log-prefix* will be placed by n_err() for next msg */
			if(donl)
				cp = n_string_cp(n_string_trunc(s, s->s_len - 1));
			n_errx(TRU1, (donl ? "%s\n" : "%s"), cp);
		}else if(fputs(cp, fp) == EOF)
			e = su_err_by_errno();
		if((rv = (fflush(fp) == EOF)))
			e = su_err_by_errno();
		rv |= ferror(fp) ? 1 : 0; /* FIXME stupid! */
		n_pstate_err_no = e;
	}else if(!n_var_vset(cacp->cac_vput, R(up,cp), cacp->cac_scope_vput)){
		n_pstate_err_no = su_ERR_NOTSUP;
		rv = -1;
	}else{
		n_pstate_err_no = su_ERR_NONE;
		rv = S(int,s->s_len);
	}

	NYD2_OU;
	return rv;
}

static int
a_cmisc_read(void * volatile vp, boole atifs){
	struct n_sigman sm;
	struct n_string s_b, *s;
	int rv;
	boole rset;
	struct mx_cmd_arg *cap;
	struct mx_cmd_arg_ctx *cacp;
	char *linebuf;
	uz linesize, ncnt;
	NYD2_IN;

	s = n_string_creat_auto(&s_b);
	mx_fs_linepool_aquire(&linebuf, &linesize);
	
	cacp = vp;
	cap = cacp->cac_arg;
	rset = (cacp->cac_no == 1 && cap->ca_arg.ca_str.l == 1 && *cap->ca_arg.ca_str.s == '^');
	if(rset)
		cap = NIL;
	ncnt = 0;

	n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
	case 0:
		break;
	default:
		n_pstate_err_no = su_ERR_INTR;
		rv = -1;
		goto jleave;
	}

	n_pstate_err_no = su_ERR_NONE;
	rv = mx_go_input((((n_pstate & n_PS_COMPOSE_MODE) ? mx_GO_INPUT_CTX_COMPOSE : mx_GO_INPUT_CTX_DEFAULT) |
			mx_GO_INPUT_FORCE_STDIN | mx_GO_INPUT_NL_ESC | mx_GO_INPUT_PROMPT_NONE/* XXX POSIX: PS2: yes*/),
			NIL, &linebuf, &linesize, NIL, NIL);
	if(UNLIKELY(rv <= 0)){
		boole x;

		x = mx_go_input_is_eof();

		if(rv < 0){
			if(!x)
				n_pstate_err_no = su_ERR_BADF;
			goto jleave;
		}
		if(x){
			rv = -1;
			goto jleave;
		}
	}else{
		struct str trim;
		struct mx_cmd_arg **npp;
		char const *ifs;

		ifs = ok_vlook(ifs); /* (ifs has default value) */
		if(rset)
			npp = &cap;
		else
			s = n_string_reserve(s, 64 -1);
		trim.s = linebuf;
		trim.l = rv;

		for(;;){
			if(trim.l == 0)
				break;

			s = n_string_trunc(s, 0);

			if(atifs){
				uz i;
				char const *cp;

				if(n_str_trim_ifs(&trim, n_STR_TRIM_FRONT, FAL0)->l == 0)
					break;

				for(cp = trim.s, i = 1;; ++cp, ++i){
					if(su_cs_find_c(ifs, *cp) != NIL){
						/* POSIX says for read(1) (with -r):
						 * If there are fewer vars than fields, the last var shall be set to
						 * a value comprising the following elements:
						 *   . The field that corresponds to the last var in the normal
						 *     assignment sequence described above
						 *   . The delimiter(s) that follow the field corresponding to last var
						 *   . The remaining fields and their delimiters, with trailing IFS
						 *     white space ignored */
						s = n_string_push_buf(s, trim.s, i - 1);
						trim.s += i;
						trim.l -= i;

						if(!rset && cap->ca_next == NIL && trim.l > 0){
							/* If it was _only_ the delimiter, done */
							n_str_trim_ifs(&trim, n_STR_TRIM_END, FAL0);
							if(trim.l == 0)
								break;
							++trim.l;
							--trim.s;
							goto jitall;
						}
						break;
					}else if(i == trim.l){
jitall:
						s = n_string_push_buf(s, trim.s, trim.l);
						trim.l = 0;
						break;
					}
				}
			}else{
jsh_redo:
				if(n_shexp_parse_token((n_SHEXP_PARSE_LOG | n_SHEXP_PARSE_IFS_VAR |
							n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_TRIM_IFSSPACE |
							n_SHEXP_PARSE_IGN_COMMENT | n_SHEXP_PARSE_IGN_SUBST_ALL),
						cacp->cac_scope_pp, s, &trim, NIL) & n_SHEXP_STATE_STOP)
					trim.l = 0;
				else if(!rset && cap->ca_next == NIL){
					s = n_string_push_c(s, *ifs);
					goto jsh_redo;
				}
			}

			if(rset){
				*npp = su_AUTO_TCALLOC(struct mx_cmd_arg, 1);
				(*npp)->ca_arg.ca_str.s = n_string_cp(s);
				(*npp)->ca_arg.ca_str.l = s->s_len;
				npp = &(*npp)->ca_next;
				s = n_string_creat_auto(s);
				++ncnt;
			}else{
				if(!a_cmisc_read_set(cap->ca_arg.ca_str.s, n_string_cp(s), cacp->cac_scope)){
					n_pstate_err_no = su_ERR_NOTSUP;
					rv = -1;
					break;
				}

				if((cap = cap->ca_next) == NIL)
					break;
			}
		}
	}

	/* Set $^0, or the remains to the empty string */
	if(rset){
		s = n_string_resize(s, su_IENC_BUFFER_SIZE);
		if((n_pstate_err_no = mx_var_result_set_set(NIL, su_ienc_s32(s->s_dat, rv, 10), ncnt, cap, NIL)
				) != su_ERR_NONE)
			rv = -1;
	}else for(; cap != NIL; cap = cap->ca_next)
		if(!a_cmisc_read_set(cap->ca_arg.ca_str.s, su_empty, cacp->cac_scope)){
			n_pstate_err_no = su_ERR_NOTSUP;
			rv = -1;
			break;
		}

	n_sigman_cleanup_ping(&sm);
jleave:
	mx_fs_linepool_release(linebuf, linesize);

	NYD2_OU;
	n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
	return rv;
}

static boole
a_cmisc_read_set(char const *cp, char const *value, enum mx_scope scope){
	boole rv;
	NYD2_IN;

	if(!n_shexp_is_valid_varname(cp, FAL0))
		value = N_("not a valid variable name");
	else if(!n_var_is_user_writable(cp))
		value = N_("variable is read-only");
	else if(!n_var_vset(cp, S(up,value), scope))
		value = N_("failed to update variable value");
	else{
		rv = TRU1;
		goto jleave;
	}

	n_err("read{,sh}: %s: %s\n", V_(value), n_shexp_quote_cp(cp, FAL0));
	rv = FAL0;

jleave:
	NYD2_OU;
	return rv;
}

static su_sz
a_cmisc_version_cmp(void const *s1, void const *s2){
	su_sz rv;
	char const *cp1, *cp2;
	NYD2_IN;

	cp1 = s1;
	cp2 = s2;
	rv = su_cs_cmp(&cp1[1], &cp2[1]);

	NYD2_OU;
	return rv;
}

int
mx_shell_cmd(char const *cmd, char const *vputvar_or_nil, enum mx_scope scope){
	struct mx_child_ctx cc;
	sigset_t mask;
	int rv;
	FILE *fp;
	char const *varres;
	NYD_IN;

	n_pstate_err_no = su_ERR_NONE;
	varres = su_empty;
	fp = NIL;

	if(vputvar_or_nil != NIL && (fp = mx_fs_tmp_open(NIL, "shell", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)) == NIL){
		n_pstate_err_no = su_ERR_CANCELED;
		rv = -1;
	}else{
		sigemptyset(&mask);
		mx_child_ctx_setup(&cc);
		cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
		cc.cc_mask = &mask;
		if(fp != NIL)
			cc.cc_fds[mx_CHILD_FD_OUT] = fileno(fp);
		mx_child_ctx_set_args_for_sh(&cc, NIL, a_cmisc_bangexp(cmd));

		if(!mx_child_run(&cc) || (rv = cc.cc_exit_status) < su_EX_OK){
			n_pstate_err_no = cc.cc_error;
			rv = -1;
		}
	}

	if(fp != NIL){
		if(rv != -1){
			int c;
			char *x;
			off_t l;

			fflush_rewind(fp);
			l = fsize(fp);
			if(UCMP(64, l, >=, UZ_MAX -42)){
				n_pstate_err_no = su_ERR_NOMEM;
				varres = su_empty;
			}else if(l > 0){
				varres = x = su_AUTO_ALLOC(l +1);

				for(; l > 0 && (c = getc(fp)) != EOF; --l)
					*x++ = c;
				*x++ = '\0';
				if(l != 0){
					n_pstate_err_no = su_err_by_errno();
					varres = su_empty; /* xxx hmmm */
				}
			}
		}

		mx_fs_close(fp);
	}

	if(vputvar_or_nil != NIL){
		if(!n_var_vset(vputvar_or_nil, R(up,varres), scope)){
			n_pstate_err_no = su_ERR_NOTSUP;
			rv = -1;
		}
	}else if(rv >= 0 && (n_psonce & n_PSO_INTERACTIVE)){
		fprintf(n_stdout, "!\n");
		/* Line buffered fflush(n_stdout); */
	}

	NYD_OU;
	return rv;
}

int
c_shell(void *vp){
	int rv;
	struct mx_cmd_arg_ctx *cacp;
	NYD_IN;

	cacp = vp;
	rv = mx_shell_cmd(cacp->cac_arg->ca_arg.ca_str.s, cacp->cac_vput, cacp->cac_scope_vput);

	NYD_OU;
	return rv;
}

int
c_dosh(void *vp){
	struct mx_child_ctx cc;
	int rv;
	NYD_IN;
	UNUSED(vp);

	mx_child_ctx_setup(&cc);
	cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
	cc.cc_cmd = ok_vlook(SHELL);

	if(mx_child_run(&cc) && (rv = cc.cc_exit_status) >= su_EX_OK){
		putc('\n', n_stdout);
		/* Line buffered fflush(n_stdout); */
		n_pstate_err_no = su_ERR_NONE;
	}else{
		n_pstate_err_no = cc.cc_error;
		rv = -1;
	}

	NYD_OU;
	return rv;
}

int
c_cwd(void *vp){
	struct n_string s_b, *s;
	uz l;
	struct mx_cmd_arg_ctx *cacp;
	NYD_IN;

	s = n_string_creat_auto(&s_b);
	cacp = vp;
	l = PATH_MAX;

	for(;; l += PATH_MAX){
		s = n_string_resize(n_string_trunc(s, 0), l);

		if(getcwd(s->s_dat, s->s_len) == NIL){
			int e;

			e = su_err_by_errno();
			if(e == su_ERR_RANGE)
				continue;
			n_perr(_("Failed to getcwd(3)"), e);
			vp = NIL;
			break;
		}

		if(cacp->cac_vput != NIL){
			if(!n_var_vset(cacp->cac_vput, R(up,s->s_dat), cacp->cac_scope_vput))
				vp = NIL;
		}else{
			l = su_cs_len(s->s_dat);
			s = n_string_trunc(s, l);
			if(fwrite(s->s_dat, 1, s->s_len, n_stdout) == s->s_len && putc('\n', n_stdout) == EOF)
				vp = NIL;
		}
		break;
	}

	NYD_OU;
	return (vp == NIL ? su_EX_ERR : su_EX_OK);
}

int
c_chdir(void *vp){
	char **arglist;
	char const *cp;
	NYD_IN;

	if(*(arglist = vp) == NIL)
		cp = ok_vlook(HOME);
	else if((cp = fexpand(*arglist, (FEXP_LOCAL | FEXP_DEF_FOLDER))) == NIL)
		goto jleave;

	if(chdir(cp) == -1){
		n_perr(cp, su_err_by_errno());
		cp = NIL;
	}

jleave:
	NYD_OU;
	return (cp == NIL ? su_EX_ERR : su_EX_OK);
}

int
c_echo(void *vp){
	int rv;
	NYD_IN;

	rv = a_cmisc_echo(vp, n_stdout, TRU1);

	NYD_OU;
	return rv;
}

int
c_echoerr(void *vp){
	int rv;
	NYD_IN;

	rv = a_cmisc_echo(vp, n_stderr, TRU1);

	NYD_OU;
	return rv;
}

int
c_echon(void *vp){
	int rv;
	NYD_IN;

	rv = a_cmisc_echo(vp, n_stdout, FAL0);

	NYD_OU;
	return rv;
}

int
c_echoerrn(void *vp){
	int rv;
	NYD_IN;

	rv = a_cmisc_echo(vp, n_stderr, FAL0);

	NYD_OU;
	return rv;
}

int
c_read(void *vp){
	int rv;
	NYD2_IN;

	rv = a_cmisc_read(vp, TRU1);

	NYD2_OU;
	return rv;
}

int
c_readsh(void *vp){
	int rv;
	NYD2_IN;

	rv = a_cmisc_read(vp, FAL0);

	NYD2_OU;
	return rv;
}

int
c_readall(void *vp){ /* TODO 64-bit retval */
	struct n_sigman sm;
	struct n_string s_b, *s;
	char *linebuf;
	uz linesize;
	int rv;
	struct mx_cmd_arg_ctx *cacp;
	NYD2_IN;

	s = n_string_creat_auto(&s_b);
	s = n_string_reserve(s, 64 -1);

	linesize = 0;
	linebuf = NIL;
	cacp = vp;

	n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
	case 0:
		break;
	default:
		n_pstate_err_no = su_ERR_INTR;
		rv = -1;
		goto jleave;
	}

	n_pstate_err_no = su_ERR_NONE;

	for(;;){
		rv = mx_go_input(((n_pstate & n_PS_COMPOSE_MODE ? mx_GO_INPUT_CTX_COMPOSE : mx_GO_INPUT_CTX_DEFAULT) |
					mx_GO_INPUT_FORCE_STDIN | /*mx_GO_INPUT_NL_ESC |*/ mx_GO_INPUT_PROMPT_NONE),
				NIL, &linebuf, &linesize, NIL, NIL);
		if(rv < 0){
			if(!mx_go_input_is_eof()){
				n_pstate_err_no = su_ERR_BADF;
				goto jleave;
			}
			if(s->s_len == 0)
				goto jleave;
			break;
		}

		if(n_pstate & n_PS_READLINE_NL)
			linebuf[rv++] = '\n'; /* Replace NUL with it */

		if(UNLIKELY(rv == 0)){ /* xxx will not get*/
			if(mx_go_input_is_eof()){
				if(s->s_len == 0){
					rv = -1;
					goto jleave;
				}
				break;
			}
		}else if(LIKELY(UCMP(32, S32_MAX - s->s_len, >, rv)))
			s = n_string_push_buf(s, linebuf, rv);
		else{
			n_pstate_err_no = su_ERR_OVERFLOW;
			rv = -1;
			goto jleave;
		}
	}

	if(!a_cmisc_read_set(cacp->cac_arg->ca_arg.ca_str.s, n_string_cp(s), cacp->cac_scope)){
		n_pstate_err_no = su_ERR_NOTSUP;
		rv = -1;
		goto jleave;
	}
	rv = s->s_len;

	n_sigman_cleanup_ping(&sm);
jleave:
	if(linebuf != NIL)
		su_FREE(linebuf);

	NYD2_OU;
	n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
	return rv;
}

int
c_version(void *vp){
	struct utsname ut;
	struct n_string s_b, *s;
	int rv;
	struct mx_cmd_arg_ctx *cacp;
	char *iop;
	char const *cp, **arr;
	uz i, lnlen, j;
	NYD_IN;

	s = n_string_creat_auto(&s_b);
	s = n_string_book(s, 1024);

	/* First two lines */
	s = mx_version(s);
	s = n_string_push_cp(s, _("Features included (+) or not (-):\n"));

	/* Some lines with the features.
	 * *features* starts with dummy byte to avoid + -> *folder* expansions */
	i = su_cs_len(cp = &ok_vlook(features)[1]) +1;
	iop = su_AUTO_ALLOC(i);
	su_mem_copy(iop, cp, i);

	arr = su_AUTO_ALLOC(sizeof(cp) * VAL_FEATURES_CNT);
	for(i = 0; (cp = su_cs_sep_c(&iop, ',', TRU1)) != NIL; ++i)
		arr[i] = cp;
	su_sort_shell_vpp(su_S(void const**,arr), i, &a_cmisc_version_cmp);

	for(lnlen = 0; i-- > 0;){
		cp = *(arr++);
		j = su_cs_len(cp);

		if((lnlen += j + 1) > 72){
			s = n_string_push_c(s, '\n');
			lnlen = j + 1;
		}
		s = n_string_push_c(s, ' ');
		s = n_string_push_buf(s, cp, j);
	}
	s = n_string_push_c(s, '\n');

	/* */
	if(n_poption & n_PO_V){
		s = n_string_push_cp(s, "Compile: ");
		s = n_string_push_cp(s, ok_vlook(build_cc));
		s = n_string_push_cp(s, "\nLink: ");
		s = n_string_push_cp(s, ok_vlook(build_ld));
		if(*(cp = ok_vlook(build_rest)) != '\0'){
			s = n_string_push_cp(s, "\nRest: ");
			s = n_string_push_cp(s, cp);
		}
		s = n_string_push_c(s, '\n');

		/* A trailing line with info of the running machine */
		uname(&ut);
		s = n_string_push_c(s, '@');
		s = n_string_push_cp(s, ut.sysname);
		s = n_string_push_c(s, ' ');
		s = n_string_push_cp(s, ut.release);
		s = n_string_push_c(s, ' ');
		s = n_string_push_cp(s, ut.version);
		s = n_string_push_c(s, ' ');
		s = n_string_push_cp(s, ut.machine);
		s = n_string_push_c(s, '\n');
	}

	/* Done */
	cp = n_string_cp(s);

	cacp = vp;

	if(cacp->cac_vput != NIL)
		rv = n_var_vset(cacp->cac_vput, R(up,cp), cacp->cac_scope_vput) ? su_EX_OK : -1;
	else{
		if(fputs(cp, n_stdout) != EOF)
			rv = su_EX_OK;
		else{
			clearerr(n_stdout);
			rv = su_EX_ERR;
		}
	}

	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_MISC
/* s-itt-mode */
