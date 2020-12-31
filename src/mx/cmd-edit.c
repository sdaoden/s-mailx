/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Perform message editing functions.
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
#define su_FILE cmd_edit
#define mx_SOURCE
#define mx_SOURCE_CMD_EDIT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/path.h>
#include <su/time.h>

#include "mx/child.h"
#include "mx/file-streams.h"
#include "mx/sigs.h"
#include "mx/tty.h"

#include "mx/cmd-edit.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

static void a_edit_it(int *msgvec, int viored);

static void
a_edit_it(int *msgvec, int viored){
	char prompt[80], *piins, iencbuf[su_IENC_BUFFER_SIZE];
	FILE *fp;
	n_sighdl_t sigint;
	struct message *mp;
	uz i;
	boole wb;
	NYD_IN;

	wb = ok_blook(writebackedited);

	for(piins = NIL, i = 0; msgvec[i] != 0 && UCMP(z, i, <, msgCount); ++i){
		if(i > 0){
			if(piins == NIL)
				piins = su_cs_pcopy(prompt, _("Edit message "));

			su_cs_pcopy(piins, su_ienc_s32(iencbuf, msgvec[i], 10));

			if(!mx_tty_yesorno(prompt, TRU1))
				continue;
		}

		mp = &message[msgvec[i] - 1];
		setdot(mp, TRU1);
		touch(mp);

		sigint = safe_signal(SIGINT, SIG_IGN);

		--mp->m_size; /* Strip final NL.. TODO MAILVFS->MESSAGE->length() */
		fp = mx_run_editor(viored, ((mb.mb_perm & MB_EDIT) == 0 || !wb), (wb ? SEND_MBOX : SEND_TODISP_ALL),
				sigint, NIL, -1, NIL, mp, NIL);
		++mp->m_size; /* And re-add it TODO */

		if(fp != NIL){
			int c;
			uz size;
			long lines;
			boole lastnl;
			off_t end_off;

			if(fseek(mb.mb_otf, 0L, SEEK_END) == -1)
				goto jeotf;
			if((end_off = ftell(mb.mb_otf)) == -1)
				goto jeotf;

			rewind(fp);
			for(lastnl = FAL0, lines = 0, size = 0; (c = getc(fp)) != EOF; ++size){
				if((lastnl = (c == '\n')))
					++lines;
				if(putc(c, mb.mb_otf) == EOF)
					goto jeotf;
			}
			if(!lastnl){
				if(putc('\n', mb.mb_otf) == EOF)
					goto jeotf;
				++size;
			}
			if(putc('\n', mb.mb_otf) == EOF)
				goto jeotf;
			++size;

			if(fflush(mb.mb_otf) != EOF && !ferror(mb.mb_otf)){
				mp->m_flag |= MODIFY;
				mp->m_block = mailx_blockof(end_off);
				mp->m_offset = mailx_offsetof(end_off);
				mp->m_size = size;
				mp->m_lines = lines;
			}else{
jeotf:
				n_perr(_("/tmp"), su_err_no_by_errno());
			}

			mx_fs_close(fp);
		}

		safe_signal(SIGINT, sigint);
	}

	NYD_OU;
}

int
c_edit(void *vp){
	NYD_IN;

	a_edit_it(vp, 'e');

	NYD_OU;
	return su_EX_OK; /* XXX */
}

int
c_visual(void *vp){
	NYD_IN;

	a_edit_it(vp, 'v');

	NYD_OU;
	return su_EX_OK; /* XXX */
}

FILE *
mx_run_editor(int viored, boole rdonly, enum sendaction action, n_sighdl_t oldint,
		FILE *fp_or_nil, s64 cnt, struct header *hp_or_nil, struct message *mp_or_nil,
		char const *pipecmd_or_nil){
	struct mx_child_ctx cc;
	sigset_t cset;
	struct su_pathinfo pi;
	struct su_timespec modtime;
	struct mx_fs_tmp_ctx *fstcp;
	u64 modsize;
	FILE *nf, *nf_pipetmp, *nf_tmp;
	NYD_IN;

	nf = nf_pipetmp = NIL;
	STRUCT_ZERO(struct su_timespec, &modtime);
	modsize = 0;

	if((nf_tmp = mx_fs_tmp_open(NIL, "edbase", ((viored == '|' ? mx_FS_O_RDWR : mx_FS_O_WRONLY) |
				mx_FS_O_REGISTER_UNLINK), &fstcp)) == NIL)
		goto jperr;

	if(mp_or_nil != NIL){
		ASSERT(fp_or_nil == NIL);
		ASSERT(hp_or_nil == NIL);
		if(sendmp(mp_or_nil, nf_tmp, NIL, NIL, action, NIL) < 0)
			goto jperr;
	}else{
		int c;
		ASSERT(fp_or_nil != NIL);

		if(hp_or_nil != NIL && !n_header_put4compose(nf_tmp, hp_or_nil))
			goto jleave;

		if(cnt >= 0){
			while(--cnt >= 0 && (c = getc(fp_or_nil)) != EOF)
				if(putc(c, nf_tmp) == EOF)
					goto jperros;
		}else while((c = getc(fp_or_nil)) != EOF)
			if(putc(c, nf_tmp) == EOF)
				goto jperros;
	}

	if(fflush(nf_tmp) == EOF || (fp_or_nil != NIL && ferror(fp_or_nil)))
		goto jperros;
	else{
		int x;

		really_rewind(nf_tmp, x);
		if(x != 0)
			goto jperros;
	}

	if(viored != '|'){
		if(rdonly){
			if(!su_path_fchmod(fileno(nf_tmp), su_IOPF_RUSR))
				goto jperr;
		}else if(su_pathinfo_fstat(&pi, fileno(nf_tmp))){
			modtime = pi.pi_mtime;
			modsize = pi.pi_size;
		}
	}

	mx_child_ctx_setup(&cc);
	cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;

	if(viored == '|'){
		ASSERT(pipecmd_or_nil != NIL);

		nf_pipetmp = mx_fs_tmp_open(NIL, "edpipe", (mx_FS_O_WRONLY | mx_FS_O_REGISTER_UNLINK), &fstcp);
		if(nf_pipetmp == NIL)
			goto jperr;

		nf = nf_tmp, nf_tmp = nf_pipetmp, nf_pipetmp = nf, nf = NIL;

		cc.cc_fds[mx_CHILD_FD_IN] = fileno(nf_pipetmp);
		cc.cc_fds[mx_CHILD_FD_OUT] = fileno(nf_tmp);
		mx_child_ctx_set_args_for_sh(&cc, NIL, pipecmd_or_nil);
	}else{
		cc.cc_cmd = (viored == 'e') ? ok_vlook(EDITOR) : ok_vlook(VISUAL);
		if(oldint != SIG_IGN){
			sigemptyset(&cset);
			cc.cc_mask = &cset;
		}
		cc.cc_args[0] = fstcp->fstc_filename;
	}

	if(!mx_child_run(&cc) || cc.cc_exit_status != su_EX_OK)
		goto jleave;

	/* If rdonly or file unchanged, remove the temporary and return.  Otherwise switch to new file */
	if(viored != '|'){
		if(rdonly)
			goto jleave;
		if(!su_pathinfo_stat(&pi, fstcp->fstc_filename))
			goto jperr;
		if(su_timespec_is_EQ(&modtime, &pi.pi_mtime) && modsize == pi.pi_size)
			goto jleave;
	}

	if((nf = mx_fs_open(fstcp->fstc_filename, mx_FS_O_RDWR)) == NIL)
		goto jperr;

jleave:
	if(nf_pipetmp != NIL)
		mx_fs_close(nf_pipetmp);

	if(nf_tmp != NIL && !mx_fs_close(nf_tmp)){
		n_perr(_("closing of temporary mail edit file"), 0);
		if(nf != NIL){
			mx_fs_close(nf);
			nf = NIL;
		}
	}

	NYD_OU;
	return nf;
jperros:
	viored = su_err_no_by_errno();
	goto jperrx;
jperr:
	viored = 0;
jperrx:
	n_perr(_("Failed to prepare editable message"), viored);
	goto jleave;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_EDIT
/* s-itt-mode */
