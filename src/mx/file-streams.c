/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of file-streams.h.
 *@ TODO tmp_release() should be removed: tmp_open() should take an optional
 *@ TODO vector of NIL terminated {char const *mode;sz fd_result;} structs,
 *@ TODO and create all desired copies; drop HOLDSIGS, then, too!
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE file_streams
#define mx_SOURCE
#define mx_SOURCE_FILE_STREAMS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <fcntl.h>

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>

#include "mx/child.h"
#include "mx/cmd-filetype.h"
#include "mx/random.h"
#include "mx/sigs.h"
#include "mx/termcap.h"

#include "mx/file-streams.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#ifdef O_CLOEXEC
# define a_FS_O_CLOEXEC O_CLOEXEC
#else
# define a_FS_O_CLOEXEC 0
#endif

#ifdef O_NOCTTY
# define a_FS_O_NOCTTY O_NOCTTY
#else
# define a_FS_O_NOCTTY 0
#endif

#if 0 /*def O_NOFOLLOW*/
# define a_FS_O_NOFOLLOW O_NOFOLLOW
#else
# define a_FS_O_NOFOLLOW 0
#endif

#define a_FS_O_NOXY_BITS (a_FS_O_NOCTTY | a_FS_O_NOFOLLOW)

enum{
	a_FS_PIPE_READ,
	a_FS_PIPE_WRITE
};

enum a_fs_ent_flags{
	a_FS_EF_RAW,
	a_FS_EF_IMAP = 1u<<1,
	a_FS_EF_MAILDIR = 1u<<2,
	a_FS_EF_HOOK = 1u<<3,
	a_FS_EF_PIPE = 1u<<4,
	a_FS_EF_MASK = (1u<<7) - 1,
	/* TODO a_FS_EF_UNLINK: should be in a separate process so that unlinking
	 * TODO the temporary "garbage" is "safe"(r than it is like that) */
	a_FS_EF_UNLINK = 1u<<8,
	/* mx_fs_tmp_release() callable? */
	a_FS_EF_HOLDSIGS = 1u<<9,
	a_FS_EF_FD_PASS_NEEDS_WAIT = 1u<<10
};

/* This struct <-> struct mx_fs_tmp_ctx! */
struct a_fs_ent{
	char *fse_realfile;
	struct a_fs_ent *fse_link;
	BITENUM(u32,a_fs_ent_flags) fse_flags;
	BITENUM(u32,mx_fs_oflags) fse_oflags;
	s64 fse_offset;
	FILE *fse_fp;
	char *fse_save_cmd;
	struct mx_child_ctx fse_cc;
};

struct a_fs_lpool_ent{
	struct a_fs_lpool_ent *fsle_last;
	char *fsle_dat;
	uz fsle_size;
};

static struct a_fs_ent *a_fs_fp_head;

struct a_fs_lpool_ent *a_fs_lpool_free;
struct a_fs_lpool_ent *a_fs_lpool_used;

/* Convert oflags to open(2) flags; if os_or_nil is not NIL, store in there the best approximation to STDIO
 * r[,r+],w,w+,a,a+ for fdopen(3) */
static int a_fs_mx_to_os(BITENUM(u32,mx_fs_oflags) oflags, char const **os_or_nil);

/* Of oflags only mx__FS_O_RWMASK|mx_FS_O_APPEND is used */
static struct a_fs_ent *a_fs_register_file(FILE *fp, BITENUM(u32,mx_fs_oflags) oflags,
		BITENUM(u32,a_fs_ent_flags) flags, struct mx_child_ctx *ccp, char const *realfile,
		s64 offset, char const *save_cmd);
static s32 a_fs_unregister_file(FILE *fp);

/* */
static boole a_fs_file_load(uz flags, int infd, int outfd, char const *load_cmd);
static boole a_fs_file_save(struct a_fs_ent *fpp);

static int
a_fs_mx_to_os(BITENUM(u32,mx_fs_oflags) oflags, char const **os_or_nil){
	char const *os;
	int rv;
	NYD2_IN;

	if(oflags & mx_FS_O_RDONLY){
		rv = O_RDONLY;
		os = "r";
	}else{
		if(oflags & mx_FS_O_WRONLY){
			rv = O_WRONLY;
			os = "w";
		}else{
			if(!(oflags & mx_FS_O_RDWR)){
				DVLDBG(n_panic(_("Implementation error: unknown open flags 0x%X"), oflags);)
			}
			rv = O_RDWR;
			os = "w+";
		}

		if(oflags & mx_FS_O_APPEND){
			os = (rv == O_WRONLY) ? "a" : "a+";
			rv |= O_APPEND;
		}
	}

	if(oflags & mx_FS_O_CREATE)
		rv |= O_CREAT;
	if(oflags & mx_FS_O_TRUNC)
		rv |= O_TRUNC;
	if(oflags & mx_FS_O_EXCL)
		rv |= O_EXCL;

#if a_FS_O_CLOEXEC != 0
	if(!(oflags & mx_FS_O_NOCLOEXEC))
		rv |= a_FS_O_CLOEXEC;
#endif

#ifdef O_NOFOLLOW
	if(oflags & mx_FS_O_NOFOLLOW)
		rv |= O_NOFOLLOW;
#endif

	rv |= a_FS_O_NOXY_BITS;

	if(os_or_nil != NIL)
		*os_or_nil = os;

	NYD2_OU;
	return rv;
}

static struct a_fs_ent *
a_fs_register_file(FILE *fp, BITENUM(u32,mx_fs_oflags) oflags, BITENUM(u32,a_fs_ent_flags) flags,
		struct mx_child_ctx *ccp, char const *realfile, s64 offset, char const *save_cmd){
	struct a_fs_ent *fsep;
	NYD2_IN;

	ASSERT(!(flags & a_FS_EF_UNLINK) || realfile != NIL);

	fsep = su_TCALLOC(struct a_fs_ent, 1);
	if(realfile != NIL)
		fsep->fse_realfile = su_cs_dup(realfile, 0);
	fsep->fse_link = a_fs_fp_head;
	fsep->fse_flags = flags;
	fsep->fse_oflags = oflags/* & (mx__FS_O_RWMASK | mx_FS_O_APPEND)*/;
	fsep->fse_offset = offset;
	fsep->fse_fp = fp;
	if(save_cmd != NIL)
		fsep->fse_save_cmd = su_cs_dup(save_cmd, 0);
	if(ccp != NIL)
		fsep->fse_cc = *ccp;

	a_fs_fp_head = fsep;

	NYD2_OU;
	return fsep;
}

static s32
a_fs_unregister_file(FILE *fp){
	struct a_fs_ent **fsepp, *fsep;
	s32 e;
	NYD2_IN;

	e = su_ERR_NONE;

	for(fsepp = &a_fs_fp_head;; fsepp = &fsep->fse_link){
		if(UNLIKELY((fsep = *fsepp) == NIL)){
			DVLDBGOR(n_panic, n_alert)(_("Invalid file pointer"));
			e = su_ERR_FAULT;
			break;
		}else if(fsep->fse_fp != fp)
			continue;

		*fsepp = fsep->fse_link;

		switch(fsep->fse_flags & a_FS_EF_MASK){
		case a_FS_EF_RAW:
		case a_FS_EF_PIPE:
			break;
		default:
			if(!a_fs_file_save(fsep))
				e = su_ERR_IO; /* xxx */
			break;
		}

		if((fsep->fse_flags & a_FS_EF_UNLINK) && unlink(fsep->fse_realfile))
			e = su_err_by_errno();

		if(fsep->fse_realfile != NIL)
			su_FREE(fsep->fse_realfile);
		if(fsep->fse_save_cmd != NIL)
			su_FREE(fsep->fse_save_cmd);

		su_FREE(fsep);
		break;
	}

	NYD2_OU;
	return e;
}

static boole
a_fs_file_load(uz flags, int infd, int outfd, char const *load_cmd){
	struct mx_child_ctx cc;
	boole rv;
	NYD_IN;

	mx_child_ctx_setup(&cc);
	cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
	cc.cc_fds[mx_CHILD_FD_IN] = infd;
	cc.cc_fds[mx_CHILD_FD_OUT] = outfd;

	switch(flags & a_FS_EF_MASK){
	case a_FS_EF_IMAP:
	case a_FS_EF_MAILDIR:
		rv = TRU1;
		break;
	case a_FS_EF_HOOK:
		mx_child_ctx_set_args_for_sh(&cc, NIL, load_cmd);
		if(0){
		/* FALLTHRU */
	default:
			cc.cc_cmd = mx_FS_FILETYPE_CAT_PROG;
		}

		rv = (mx_child_run(&cc) && cc.cc_exit_status == su_EX_OK);
		break;
	}

	NYD_OU;
	return rv;
}

static boole
a_fs_file_save(struct a_fs_ent *fsep){
	struct mx_child_ctx cc;
	int osiflags;
	boole rv;
	NYD_IN;

	if((rv = ((fsep->fse_oflags & mx__FS_O_RWMASK) == mx_FS_O_RDONLY)))
		goto jleave;

	fflush(fsep->fse_fp);
	clearerr(fsep->fse_fp);

	/* Ensure the I/O library does not optimize the fseek(3) away! */
	if(!n_real_seek(fsep->fse_fp, fsep->fse_offset, SEEK_SET)){
		s32 err;

		err = su_err_by_errno();
		n_err(_("Fatal: cannot restore file position and save %s: %s\n"),
			n_shexp_quote_cp(fsep->fse_realfile, FAL0), su_err_doc(err));
		goto jleave;
	}

#ifdef mx_HAVE_MAILDIR
	if((fsep->fse_flags & a_FS_EF_MASK) == a_FS_EF_MAILDIR){
		rv = maildir_append(fsep->fse_realfile, fsep->fse_fp, fsep->fse_offset,
				((fsep->fse_oflags & mx_FS_O_EXACT_MESSAGE_STATE_REFLECTION) != 0));
		goto jleave;
	}
#endif

#ifdef mx_HAVE_IMAP
	if((fsep->fse_flags & a_FS_EF_MASK) == a_FS_EF_IMAP){
		rv = imap_append(fsep->fse_realfile, fsep->fse_fp, fsep->fse_offset);
		goto jleave;
	}
#endif

	mx_child_ctx_setup(&cc);
	cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
	cc.cc_fds[mx_CHILD_FD_IN] = fileno(fsep->fse_fp);
	osiflags = a_fs_mx_to_os(fsep->fse_oflags, NIL) | O_CREAT | a_FS_O_NOXY_BITS;
	if(!(fsep->fse_oflags & mx_FS_O_APPEND))
		osiflags |= O_TRUNC;
	if((cc.cc_fds[mx_CHILD_FD_OUT] = open(fsep->fse_realfile, osiflags,
			(fsep->fse_oflags & mx_FS_O_CREATE_0600 ? 0600 : 0666))) == -1){
		s32 err;

		err = su_err_by_errno();
		n_err(_("Fatal: cannot create %s: %s\n"), n_shexp_quote_cp(fsep->fse_realfile, FAL0), su_err_doc(err));
		goto jleave;
	}

	switch(fsep->fse_flags & a_FS_EF_MASK){
	case a_FS_EF_HOOK:
		if(n_poption & n_PO_D_V)
			n_err(_("Using `filetype' handler %s to save %s\n"),
				n_shexp_quote_cp(fsep->fse_save_cmd, FAL0), n_shexp_quote_cp(fsep->fse_realfile, FAL0));
		mx_child_ctx_set_args_for_sh(&cc, NIL, fsep->fse_save_cmd);
		break;
	default:
		cc.cc_cmd = mx_FS_FILETYPE_CAT_PROG;
		break;
	}

	rv = (mx_child_run(&cc) && cc.cc_exit_status == su_EX_OK);

	close(cc.cc_fds[mx_CHILD_FD_OUT]); /* XXX no error handling */
jleave:
	NYD_OU;
	return rv;
}

s32
mx_fs_open_fd(char const *file, BITENUM(u32,mx_fs_oflags) oflags, s32 mode){
	s32 fd;
	char const *osflags;
	int osiflags;
	NYD_IN;

	osiflags = a_fs_mx_to_os(oflags, &osflags);

	if((fd = open(file, osiflags, mode)) == -1)
		su_err_by_errno();
#if a_FS_O_CLOEXEC == 0
	else if(!(oflags & mx_FS_O_NOCLOEXEC) && !mx_fs_fd_cloexec_set(fd)){
		su_err_by_errno();
		close(fd);
		fd = -1;
	}
#endif

	NYD_OU;
	return fd;
}

FILE *
mx_fs_open(char const *file, BITENUM(u32,mx_fs_oflags) oflags){
	int osiflags, fd;
	char const *osflags;
	FILE *fp;
	NYD_IN;

	ASSERT(!(oflags & mx_FS_O_CREATE_0600) || (oflags & mx_FS_O_CREATE));

	fp = NIL;

	osiflags = a_fs_mx_to_os(oflags, &osflags);

	if((fd = open(file, osiflags, (oflags & mx_FS_O_CREATE_0600 ? 0600 : 0666))) == -1){
		su_err_by_errno();
		goto jleave;
	}
#if a_FS_O_CLOEXEC == 0
	if(!(oflags & mx_FS_O_NOCLOEXEC) && !mx_fs_fd_cloexec_set(fd)){
		su_err_by_errno();
		close(fd);
		goto jleave;
	}
#endif

	if((fp = fdopen(fd, osflags)) != NIL){
		if(!(oflags & mx_FS_O_NOREGISTER))
			a_fs_register_file(fp, oflags, a_FS_EF_RAW, NIL, NIL, 0L, NIL);
	}else{
		su_err_by_errno();
		close(fd);
	}

jleave:
	NYD_OU;
	return fp;
}

FILE *
mx_fs_open_any(char const *file, BITENUM(u32,mx_fs_oflags) oflags, enum mx_fs_open_state *fs_or_nil){ /* TODO as bits, return state */
	/* TODO Support file locking upon open time */
	s64 offset;
	enum protocol p;
	uz flags;
	BITENUM(u32,mx_fs_oflags) tmpoflags;
	int /*osiflags,*/ omode, infd;
	char const *cload, *csave;
	BITENUM(u32, mx_fs_open_state) fs;
	s32 err;
	FILE *rv;
	NYD_IN;

	ASSERT(!(oflags & mx_FS_O_CREATE_0600) || (oflags & mx_FS_O_CREATE));

	rv = NIL;
	err = su_ERR_NONE;
	fs = mx_FS_OPEN_STATE_NONE;
	cload = csave = NIL;

	/* O_NOREGISTER is disallowed */
	if(oflags & mx_FS_O_NOREGISTER){
		err = su_ERR_INVAL;
		goto jleave;
	}

	/*osiflags =*/ a_fs_mx_to_os(oflags, NIL);
	omode = ((oflags & mx__FS_O_RWMASK) == mx_FS_O_RDONLY) ? R_OK : R_OK | W_OK;
	tmpoflags = (mx_FS_O_RDWR | mx_FS_O_UNLINK | (oflags & mx_FS_O_APPEND) | mx_FS_O_NOREGISTER);
	flags = 0;

	/* v15-compat: NEVER!  We do not want to find mbox.bz2 when doing "copy
	 *  mbox", but only for "file mbox", so do not try hooks when writing */
	p = which_protocol(csave = file, TRU1, ((omode & W_OK) == 0), &file);
	fs = S(enum mx_fs_open_state,p);
	switch(p){
	default:
		err = su_ERR_OPNOTSUPP;
		goto jleave;

	case n_PROTO_IMAP:
#ifdef mx_HAVE_IMAP
		file = csave;
		flags |= a_FS_EF_IMAP;
		/*osiflags = O_RDWR | O_APPEND | O_CREAT | a_FS_O_NOXY_BITS;*/
		infd = -1;
		break;
#else
		err = su_ERR_OPNOTSUPP;
		goto jleave;
#endif

	case n_PROTO_MAILDIR:
#ifdef mx_HAVE_MAILDIR
		if(fs_or_nil != NIL && su_path_access(file, su_IOPF_EXIST))
			fs |= mx_FS_OPEN_STATE_EXISTS;
		flags |= a_FS_EF_MAILDIR;
		/*osiflags = O_RDWR | O_APPEND | O_CREAT | a_FS_O_NOXY_BITS;*/
		infd = -1;
		break;
#else
		err = su_ERR_OPNOTSUPP;
		goto jleave;
#endif

	case n_PROTO_EML:
		if((oflags & mx__FS_O_RWMASK) != mx_FS_O_RDONLY){
			err = su_ERR_OPNOTSUPP;
			goto jleave;
		}
		/* FALLTHRU */
	case n_PROTO_FILE:{
		struct mx_filetype ft;

		if(!(oflags & mx_FS_O_EXCL) && fs_or_nil != NIL && su_path_access(file, su_IOPF_EXIST))
			fs |= mx_FS_OPEN_STATE_EXISTS;

		if(mx_filetype_exists(&ft, file)){/* TODO report real name to outside */
			flags |= a_FS_EF_HOOK;
			cload = ft.ft_load_dat;
			csave = ft.ft_save_dat;
			/* Cause truncation for compressor/hook output files */
			oflags &= ~mx_FS_O_APPEND;
			/*osiflags &= ~O_APPEND;*/
			tmpoflags &= ~mx_FS_O_APPEND;
			if((infd = open(file, (omode & W_OK ? O_RDWR : O_RDONLY))) != -1){
				fs |= mx_FS_OPEN_STATE_EXISTS;
				if(n_poption & n_PO_D_V)
					n_err(_("Using `filetype' handler %s to load %s\n"),
						n_shexp_quote_cp(cload, FAL0), n_shexp_quote_cp(file, FAL0));
			}else{
				err = su_err_by_errno();
				if(!(oflags & mx_FS_O_CREATE) || err != su_ERR_NOENT)
					goto jleave;
			}
		}else{
			/*flags |= a_FS_EF_RAW;*/
			rv = mx_fs_open(file, oflags);
			if(rv == NIL){
				err = su_err();
				if((oflags & mx_FS_O_EXCL) && err == su_ERR_EXIST)
					fs |= mx_FS_OPEN_STATE_EXISTS;
			}
			goto jleave;
		}
		}break;
	}

	/* Note rv is not yet register_file()d, fclose() it in error path! */
	if((rv = mx_fs_tmp_open(NIL, "fopenany", tmpoflags, NIL)) == NIL){
		err = su_err();
		n_perr(_("tmpfile"), err);
		goto jerr;
	}

	if(flags & (a_FS_EF_IMAP | a_FS_EF_MAILDIR)){
	}else if(infd >= 0){
		if(!a_fs_file_load(flags, infd, fileno(rv), cload)){
			err = su_err();
jerr:
			if(rv != NIL)
				fclose(rv); /* Not yet registered */
			rv = NIL;
			if(infd >= 0)
				close(infd);
			goto jleave;
		}
	}else{
		if((infd = creat(file, (oflags & mx_FS_O_CREATE_0600 ? 0600 : 0666))) == -1){
			err = su_err_by_errno();
			fclose(rv);
			rv = NIL;
			goto jleave;
		}
	}

	if(infd >= 0)
		close(infd);
	fflush(rv);

	if(!(oflags & mx_FS_O_APPEND))
		rewind(rv);

	if((offset = ftell(rv)) == -1){
		err = su_err_by_errno();
		mx_fs_close(rv);
		rv = NIL;
		goto jleave;
	}

	a_fs_register_file(rv, oflags, flags, NIL, file, offset, csave);
jleave:
	if(fs_or_nil != NIL)
		*fs_or_nil = fs;
	if(rv == NIL){
		if(err == su_ERR_NONE)
			err = su_ERR_IO;
		su_err_set(err);
	}

	NYD_OU;
	return rv;
}

FILE *
mx_fs_tmp_open(char const *tdir_or_nil, char const *namehint_or_nil, BITENUM(u32,mx_fs_oflags) oflags,
		struct mx_fs_tmp_ctx **fstcp_or_nil){
	/* The 6 is arbitrary but leaves room for an eight character hint (the POSIX minimum path length is 14, though
	 * we do not check that XXX).  6 should be more than sufficient given that we use base64url encoding for our
	 * random string */
	enum {a_RANDCHARS = 6u, a_HINT_MIN = 8u};

	char *cp_base, *cp;
	uz maxname, hintlen, i;
	int osiflags, fd, e;
	boole relesigs;
	FILE *fp;
	NYD_IN;

	ASSERT((oflags & mx_FS_O_WRONLY) || (oflags & mx_FS_O_RDWR));
	ASSERT(!(oflags & mx_FS_O_RDONLY));
	ASSERT(!(oflags & mx_FS_O_REGISTER_UNLINK) || !(oflags & mx_FS_O_NOREGISTER));
	ASSERT(!(oflags & mx_FS_O_HOLDSIGS) || !(oflags & mx_FS_O_UNLINK));
	ASSERT(fstcp_or_nil == NIL || (!(oflags & mx_FS_O_NOREGISTER) || (oflags & mx_FS_O_HOLDSIGS) ||
		!(oflags & mx_FS_O_UNLINK)));

	if(fstcp_or_nil != NIL)
		*fstcp_or_nil = NIL;

	fp = NIL;
	relesigs = FAL0;
	e = 0;

	if(tdir_or_nil == NIL || tdir_or_nil == mx_FS_TMP_TDIR_TMP) /* (EQ) */
		tdir_or_nil = ok_vlook(TMPDIR);

	if((maxname = su_path_max_filename(tdir_or_nil)) < a_RANDCHARS + a_HINT_MIN){
		su_err_set(su_ERR_NAMETOOLONG);
		goto jleave;
	}

	/* We are prepared to ignore the namehint .. but for O_SUFFIX */
	if(namehint_or_nil == NIL){
		namehint_or_nil = su_empty;
		hintlen = 0;
	}else{
		hintlen = su_cs_len(namehint_or_nil);
		if((oflags & mx_FS_O_SUFFIX) && hintlen >= maxname - a_RANDCHARS){
			su_err_set(su_ERR_NAMETOOLONG);
			goto jleave;
		}
	}

	/* Prepare the template string once, then iterate over the random range.
	 * But first ensure we can report the name in O_NOREGISTER cases ("hack") */
	i = su_cs_len(tdir_or_nil) + 1 + maxname +1;

	if((oflags & mx_FS_O_NOREGISTER) && fstcp_or_nil != NIL){
		union {struct a_fs_ent *fse; void *v; struct mx_fs_tmp_ctx *fstc;} p;

		/* Store character data right after the struct */
		p.v = su_AUTO_ALLOC(sizeof(*p.fse) + i);
		su_mem_set(p.fse, 0, sizeof(*p.fse));
		p.fse->fse_realfile = R(char*,&p.fse[1]);
		if(oflags & mx_FS_O_HOLDSIGS) /* Enable tmp_release() nonetheless? */
			p.fse->fse_flags = a_FS_EF_HOLDSIGS;
		*fstcp_or_nil = p.fstc;
	}

	cp_base = cp = su_LOFI_ALLOC(i);
	cp = su_cs_pcopy(cp, tdir_or_nil);
	*cp++ = '/';
	/* C99 */{
		uz j;
		char *xp;

		/* xxx We silently assume VAL_UAGENT easily fits in NAME_MAX-1 */
		xp = su_cs_pcopy(cp, VAL_UAGENT);
		*xp++ = '-';
		if(!(oflags & mx_FS_O_SUFFIX) && hintlen > 0){
			su_mem_copy(xp, namehint_or_nil, hintlen);
			xp += hintlen;
			hintlen = 0; /* Now considered part of base! */
		}

		/* Just cut off again as many as necessary (it suffices!) */
		if((i = P2UZ(xp - cp)) > (j = maxname - hintlen - a_RANDCHARS))
			xp -= i - j;

		if((oflags & mx_FS_O_SUFFIX) && hintlen > 0)
			su_mem_copy(&xp[a_RANDCHARS], namehint_or_nil, hintlen);

		xp[hintlen + a_RANDCHARS] = '\0';
		cp = xp;
	}

	osiflags = O_CREAT | O_EXCL | a_FS_O_CLOEXEC;
	osiflags |= (oflags & mx_FS_O_WRONLY) ? O_WRONLY : O_RDWR;
	if(oflags & mx_FS_O_APPEND)
		osiflags |= O_APPEND;

	for(i = 0;; ++i){
		su_mem_copy(cp, mx_random_create_cp(a_RANDCHARS, NIL), a_RANDCHARS);

		mx_sigs_all_holdx();
		relesigs = TRU1;

		if((fd = open(cp_base, osiflags, 0600)) != -1){
#if a_FS_O_CLOEXEC == 0
			if(!mx_fs_fd_cloexec_set(fd))
				close(fd);
			else
#endif
				break;
		}
		e = su_err_by_errno();

		relesigs = FAL0;
		mx_sigs_all_rele();

		if(i >= mx_FS_TMP_OPEN_TRIES || e == su_ERR_NOENT || e == su_ERR_NOTDIR)
			goto jfree;
	}

	if((fp = fdopen(fd, ((oflags & mx_FS_O_RDWR) ? "w+" : "w"))) != NIL){
		if(!(oflags & mx_FS_O_NOREGISTER)){
			struct a_fs_ent *fsep;

			fsep = a_fs_register_file(fp, oflags, (a_FS_EF_RAW |
						(oflags & mx_FS_O_REGISTER_UNLINK ? a_FS_EF_UNLINK : 0) |
						(oflags & mx_FS_O_HOLDSIGS ? a_FS_EF_HOLDSIGS : 0)), NIL, cp_base,
					0, NIL);

			/* User arg points to registered data in this case */
			if(fstcp_or_nil != NIL){
				union {void *v; struct mx_fs_tmp_ctx *fstc;} p;

				p.v = fsep;
				*fstcp_or_nil = p.fstc;
			}
		}else if(fstcp_or_nil != NIL){
			/* Otherwise copy filename buffer into autorec storage */
			cp = UNCONST(char*,(*fstcp_or_nil)->fstc_filename);
			su_cs_pcopy(cp, cp_base);
		}
	}

	if(fp == NIL || (oflags & mx_FS_O_UNLINK)){
		e = su_err_by_errno();
		su_path_rm(cp_base);
		if(fp == NIL)
			close(fd);
		goto jfree;
	}else if(fp != NIL){
		/* We will succeed and keep the file around for further usage, likely another stream will be opened for
		 * pure reading purposes (this is true at the time of this writing.  A restrictive umask(2) setting may
		 * have turned the path inaccessible, so ensure it may be read at least!
		 * TODO once ok_vlook() can return an integer, look up *umask* first! */
		(void)su_path_fchmod(fd, su_IOPF_WUSR | su_IOPF_RUSR);
	}

	su_LOFI_FREE(cp_base);

jleave:
	if(relesigs && (fp == NIL || !(oflags & mx_FS_O_HOLDSIGS)))
		mx_sigs_all_rele();

	if(fp == NIL){
		if(e == su_ERR_NONE)
			e = su_ERR_IO;
		su_err_set(e);
		if(fstcp_or_nil != NIL)
			*fstcp_or_nil = NIL;
	}

	NYD_OU;
	return fp;

jfree:
	if((cp = cp_base) != NIL)
		su_LOFI_FREE(cp);
	goto jleave;
}

void
mx_fs_tmp_release(struct mx_fs_tmp_ctx *fstcp){
	union {void *vp; struct a_fs_ent *fsep;} u;
	NYD_IN;

	ASSERT(fstcp != NIL);

	u.vp = fstcp;
	ASSERT(u.fsep->fse_flags & a_FS_EF_HOLDSIGS);
	DVLDBG(if(u.fsep->fse_flags & a_FS_EF_UNLINK)
		n_alert("tmp_release(): REGISTER_UNLINK set!");)

	su_path_rm(u.fsep->fse_realfile);

	u.fsep->fse_flags &= ~(a_FS_EF_HOLDSIGS | a_FS_EF_UNLINK);

	mx_sigs_all_rele();

	NYD_OU;
}

FILE *
mx_fs_fd_open(sz fd, BITENUM(u32,mx_fs_oflags) oflags){
	FILE *fp;
	char const *osflags;
	NYD_IN;

	(void)a_fs_mx_to_os(oflags, &osflags);

	if(oflags & mx_FS_O_NOCLOSEFD){
		int m, nfd;

		m = F_DUPFD;
#ifdef F_DUPFD_CLOEXEC
		if(!(oflags & mx_FS_O_NOCLOEXEC))
			m = F_DUPFD_CLOEXEC;
#endif

		nfd = fcntl(fd, m, 0);
		if(nfd == -1){
			su_err_by_errno();
			fp = NIL;
			goto jleave;
		}

#ifndef F_DUPFD_CLOEXEC
		if(!(oflags & mx_FS_O_NOCLOEXEC))
			mx_fs_fd_cloexec_set(nfd);
#endif

		fd = nfd;
	}

	if((fp = fdopen(S(int,fd), osflags)) != NIL){
		if(!(oflags & mx_FS_O_NOREGISTER))
			a_fs_register_file(fp, oflags, a_FS_EF_RAW, NIL, NIL, 0, NIL);
	}else{
		su_err_by_errno();
		if(oflags & mx_FS_O_NOCLOSEFD)
			close(fd);
	}

jleave:
	NYD_OU;
	return fp;
}

boole
mx_fs_fd_cloexec_set(sz fd){
	s32 ifd, ifl;
	boole rv;
	NYD2_IN;

	ifd = S(s32,fd);
	if((rv = ((ifl = fcntl(ifd, F_GETFD)) != -1)) && !(ifl & FD_CLOEXEC))
		rv = (fcntl(ifd, F_SETFD, ifl | FD_CLOEXEC) != -1);

	if(!rv)
		su_err_by_errno();

	NYD2_OU;
	return rv;
}

boole
mx_fs_close(FILE *fp){
	boole rv;
	NYD_IN;

	rv = TRU1;

	if(a_fs_unregister_file(fp) == su_ERR_NONE)
		if(!(rv = (fclose(fp) == 0)))
			su_err_by_errno();

	NYD_OU;
	return rv;
}

boole
mx_fs_pipe_cloexec(sz fd[2]){
	int xfd[2];
	boole rv;
	NYD_IN;

	rv = FAL0;

#if defined mx_HAVE_PIPE2 && a_FS_O_CLOEXEC != 0
	if(pipe2(xfd, a_FS_O_CLOEXEC) != -1){
		fd[0] = xfd[0];
		fd[1] = xfd[1];
		rv = TRU1;
	}else
		su_err_by_errno();

#else
	if(pipe(xfd) != -1){
		fd[0] = xfd[0];
		fd[1] = xfd[1];
		if(!(rv = (mx_fs_fd_cloexec_set(fd[0]) && mx_fs_fd_cloexec_set(fd[1])))){
			su_err_by_errno();
			close(xfd[0]);
			close(xfd[1]);
		}
	}else
		su_err_by_errno();
#endif

	NYD_OU;
	return rv;
}

FILE *
mx_fs_pipe_open(char const *cmd, enum mx_fs_pipe_type fspt, char const *sh, char const **env_addon, sz newfd1){
	struct mx_child_ctx cc;
	sz p[2], myside, hisside, fd0, fd1;
	sigset_t nset;
	char mod[2];
	FILE *rv;
	NYD_IN;

	rv = NIL;
	mod[0] = mod[1] = '\0';

	if(!mx_fs_pipe_cloexec(p))
		goto jleave;

	switch(fspt){
	default:
	case mx_FS_PIPE_READ:
		myside = p[a_FS_PIPE_READ];
		fd0 = mx_CHILD_FD_PASS;
		hisside = fd1 = p[a_FS_PIPE_WRITE];
		mod[0] = 'r';
		break;
	case mx_FS_PIPE_WRITE_CHILD_PASS:
		newfd1 = mx_CHILD_FD_PASS;
		/* FALLTHRU */
	case mx_FS_PIPE_WRITE:
		myside = p[a_FS_PIPE_WRITE];
		hisside = fd0 = p[a_FS_PIPE_READ];
		fd1 = newfd1;
		mod[0] = 'w';
		break;
	}

	sigemptyset(&nset);
	mx_child_ctx_setup(&cc);
	/* Be simple and let's just synchronize completely on that */
	cc.cc_flags = ((cc.cc_cmd = cmd) == R(char*,-1)) ?  mx_CHILD_SPAWN_CONTROL
			: mx_CHILD_SPAWN_CONTROL | mx_CHILD_SPAWN_CONTROL_LINGER;
	cc.cc_mask = &nset;
	cc.cc_fds[mx_CHILD_FD_IN] = fd0;
	cc.cc_fds[mx_CHILD_FD_OUT] = fd1;
	cc.cc_env_addon = env_addon;

	/* Is this a special in-process fork setup? */
	if(cmd == R(char*,-1)){
		if(!mx_child_fork(&cc))
			n_perr(_("fork"), 0);
		else if(cc.cc_pid == 0){
			union {char const *ccp; int (*ptf)(void); int es;} u;

			mx_child_in_child_setup(&cc);
			close(S(int,p[a_FS_PIPE_READ]));
			close(S(int,p[a_FS_PIPE_WRITE]));
			/* TODO close all other open FDs except stds and reset memory */
			/* Standard I/O drives me insane!  All we need is a sync operation that causes n_stdin to
			 * forget about any read buffer it may have.  We cannot use fflush(3), this works with Musl and
			 * Solaris, but not with GlibC.  (For at least pipes.)  We cannot use fdreopen(), because this
			 * function does not exist!  Luckily (!!!) we only use n_stdin not stdin in our child,
			 * otherwise all bets were off!
			 * TODO (Unless we would fiddle around with FILE* directly:
			 * TODO #ifdef __GLIBC__
			 * TODO   n_stdin->_IO_read_ptr = n_stdin->_IO_read_end;
			 * TODO #elif *BSD*
			 * TODO   n_stdin->_r = 0;
			 * TODO #elif su_OS_SOLARIS || su_OS_SUNOS
			 * TODO   n_stdin->_cnt = 0;
			 * TODO #endif
			 * TODO ) which should have additional config test for sure! */
			n_stdin = fdopen(STDIN_FILENO, "r");
			/*n_stdout = fdopen(STDOUT_FILENO, "w");*/
			/*n_stderr = fdopen(STDERR_FILENO, "w");*/
			u.ccp = sh;
			u.es = (*u.ptf)();
			/*fflush(NULL);*/
			for(;;)
				_exit(u.es);
		}
	}else{
		if(sh != NIL)
			mx_child_ctx_set_args_for_sh(&cc, sh, cmd);

		mx_child_run(&cc);
	}

	if(cc.cc_pid < 0){
		close(S(int,p[a_FS_PIPE_READ]));
		close(S(int,p[a_FS_PIPE_WRITE]));
		goto jleave;
	}

	close(S(int,hisside));
	if((rv = fdopen(myside, mod)) != NIL)
		a_fs_register_file(rv, 0, ((fd0 != mx_CHILD_FD_PASS && fd1 != mx_CHILD_FD_PASS)
				? a_FS_EF_PIPE : a_FS_EF_PIPE | a_FS_EF_FD_PASS_NEEDS_WAIT), &cc, NIL, 0L, NIL);
	else{
		su_err_by_errno();
		close(myside);
	}

jleave:
	NYD_OU;
	return rv;
}

s32
mx_fs_pipe_signal(FILE *fp, s32 sig){
	s32 rv;
	struct a_fs_ent *fsep;
	NYD_IN;

	for(fsep = a_fs_fp_head;; fsep = fsep->fse_link)
		if(fsep == NIL){
			rv = -1;
			break;
		}else if(fsep->fse_fp == fp){
			rv = mx_child_signal(&fsep->fse_cc, sig);
			break;
		}

	NYD_OU;
	return rv;
}

boole
mx_fs_pipe_close(FILE *fp, boole dowait){
	n_sighdl_t opipe;
	struct mx_child_ctx cc;
	boole rv;
	struct a_fs_ent *fsep;
	NYD_IN;

	for(fsep = a_fs_fp_head;; fsep = fsep->fse_link)
		if(UNLIKELY(fsep == NIL)){
			DVLDBG(n_alert(_("pipe_close: invalid file pointer"));)
			rv = FAL0;
			goto jleave;
		}else if(fsep->fse_fp == fp)
			break;
	cc = fsep->fse_cc;

	ASSERT(!(fsep->fse_flags & a_FS_EF_FD_PASS_NEEDS_WAIT) || dowait);

	opipe = safe_signal(SIGPIPE, SIG_IGN); /* TODO only because we jump stuff */

	if(a_fs_unregister_file(fp) == su_ERR_NONE)
		fclose(fp);

	safe_signal(SIGPIPE, opipe);

	if(dowait)
		rv = (mx_child_wait(&cc) && cc.cc_exit_status == su_EX_OK);
	else{
		mx_child_forget(&cc);
		rv = TRU1;
	}

jleave:
	NYD_OU;
	return rv;
}

boole
mx_fs_flush(FILE *fp){
	boole rv;
	NYD_IN;

	if(!(rv = (fflush(fp) != EOF)))
		su_err_by_errno();

	NYD_OU;
	return rv;
}

void
mx_fs_close_all(void){
	NYD_IN;

	while(a_fs_fp_head != NIL)
		if((a_fs_fp_head->fse_flags & a_FS_EF_MASK) == a_FS_EF_PIPE)
			mx_fs_pipe_close(a_fs_fp_head->fse_fp, TRU1);
		else
			mx_fs_close(a_fs_fp_head->fse_fp);

	NYD_OU;
}

void
(mx_fs_linepool_aquire)(char **dpp, uz *dsp  su_DVL_LOC_ARGS_DECL){
	struct a_fs_lpool_ent *lpep;
	NYD2_IN;

	if((lpep = a_fs_lpool_free) != NIL)
		a_fs_lpool_free = lpep->fsle_last;
	else
		lpep = su_TCALLOC_LOCOR(struct a_fs_lpool_ent, 1, su_DVL_LOC_ARGS_ORUSE);

	lpep->fsle_last = a_fs_lpool_used;
	a_fs_lpool_used = lpep;
	*dpp = lpep->fsle_dat;
	lpep->fsle_dat = NIL;
	*dsp = lpep->fsle_size;
	lpep->fsle_size = 0;

#ifdef su_HAVE_DVL_LOC_ARGS
	if(*dpp != NIL)
		su_MEM_TOUCH_LOCOR(*dpp, su_DVL_LOC_ARGS_USE_SOLE);
#endif

	NYD2_OU;
}

void
(mx_fs_linepool_release)(char *dp, uz ds  su_DVL_LOC_ARGS_DECL){
	struct a_fs_lpool_ent *lpep;
	NYD2_IN;

	ASSERT(a_fs_lpool_used != NIL);
	lpep = a_fs_lpool_used;
	a_fs_lpool_used = lpep->fsle_last;

#ifdef su_HAVE_DVL_LOC_ARGS
	if(dp != NIL)
		su_MEM_TOUCH_LOCOR(dp,  su_DVL_LOC_ARGS_USE_SOLE);
#endif

	lpep->fsle_last = a_fs_lpool_free;
	a_fs_lpool_free = lpep;
	lpep->fsle_dat = dp;
	lpep->fsle_size = ds;

	NYD2_OU;
}

boole
(mx_fs_linepool_book)(char **dp, uz *dsp, uz len, uz toadd  su_DVL_LOC_ARGS_DECL){
	boole rv;
	NYD2_IN;

	rv = FAL0;

	if(UZ_MAX - 192 <= toadd)
		goto jeoverflow;
	toadd = (toadd + 192) & ~127;

	if(!su_mem_get_can_book(1, len, toadd))
		goto jeoverflow;
	len += toadd;

	if(*dsp < len){
		char *ndp;

		if((ndp = su_MEM_REALLOC_LOCOR(*dp, len, su_DVL_LOC_ARGS_ORUSE)) == NIL)
			goto jleave;
		*dp = ndp;
		*dsp = len;
	}

	rv = TRU1;
jleave:
	NYD2_OU;
	return rv;

jeoverflow:
	su_state_err(su_STATE_ERR_OVERFLOW, (su_STATE_ERR_PASS | su_STATE_ERR_NOERROR),
		_("fs_linepool_book(): buffer size overflow"));
	goto jleave;
}

void
mx_fs_linepool_cleanup(boole completely){
	struct a_fs_lpool_ent *lpep, *tmp, *keep;
	NYD2_IN;

	if(!completely)
		completely = mx_LINEPOOL_QUEUE_MAX;
	else
		completely = FAL0;

	lpep = a_fs_lpool_free;
	a_fs_lpool_free = keep = NIL;
jredo:
	while((tmp = lpep) != NIL){
		void *vp;

		lpep = tmp->fsle_last;

		if((vp = tmp->fsle_dat) != NIL){
			if(completely > 0 && tmp->fsle_size <= MAX(mx_BUFFER_SIZE, mx_LINESIZE)){
				--completely;
				tmp->fsle_last = a_fs_lpool_free;
				a_fs_lpool_free = tmp;
				continue;
			}

			su_FREE(vp);
		}
		su_FREE(tmp);
	}

	/* Only called from go_main_loop(), save to throw the hulls away */
	if((lpep = a_fs_lpool_used) != NIL){
		a_fs_lpool_used = NIL;
		goto jredo;
	}

	NYD2_OU;
}

/* TODO The rest below is old-style v15 */

/* line is a buffer with the result of fgets(). Returns the first newline or
 * the last character read */
static uz _length_of_line(char const *line, uz linesize);

/* Read a line, one character at a time */
static char *a_fs_fgetline_byone(char **line, uz *linesize, uz *llen_or_nil, FILE *fp, int appendnl, uz n
		su_DVL_LOC_ARGS_DECL);

static uz
_length_of_line(char const *line, uz linesize)
{
	uz i;
	NYD2_IN;

	/* Last character is always '\0' and was added by fgets() */
	for (--linesize, i = 0; i < linesize; i++)
		if (line[i] == '\n')
			break;
	i = (i < linesize) ? i + 1 : linesize;

	NYD2_OU;
	return i;
}

static char *
a_fs_fgetline_byone(char **line, uz *linesize, uz *llen_or_nil, FILE *fp, int appendnl, uz n  su_DVL_LOC_ARGS_DECL)
{
	char *rv;
	int c;
	NYD2_IN;

	ASSERT(*linesize == 0 || *line != NULL);

	/* Always leave room for NETNL, not only \n */
	for (rv = *line;;) {
		if (*linesize <= mx_LINESIZE || n >= *linesize - 128) {
			*linesize += ((rv == NULL) ? mx_LINESIZE + n + 2 : 256);
			*line = rv = su_MEM_REALLOC_LOCOR(rv, *linesize, su_DVL_LOC_ARGS_ORUSE);
		}
		c = getc(fp);
		if (c != EOF) {
			rv[n++] = c;
			rv[n] = '\0';
			if (c == '\n') {
				n_pstate |= n_PS_READLINE_NL;
				break;
			}
		} else {
			if (n > 0 && feof(fp)) {
				if (appendnl) {
					rv[n++] = '\n';
					rv[n] = '\0';
				}
				break;
			} else {
				su_err_by_errno();
				rv = NULL;
				goto jleave;
			}
		}
	}

	if(llen_or_nil != NIL)
		*llen_or_nil = n;

jleave:
	NYD2_OU;
	return rv;
}

char *
(fgetline)(char **line, uz *linesize, uz *cnt, uz *llen_or_nil, FILE *fp, int appendnl  su_DVL_LOC_ARGS_DECL)
{
	uz i_llen, size;
	char *rv;
	NYD2_IN;

	n_pstate &= ~n_PS_READLINE_NL;

	if(llen_or_nil != NIL)
		*llen_or_nil = 0;

	if(cnt == NIL){
		/* Without we cannot determine where the chars returned by fgets()
		 * end if there's no newline.  We have to read one character by one */
		rv = a_fs_fgetline_byone(line, linesize, llen_or_nil, fp, appendnl, 0  su_DVL_LOC_ARGS_USE);
		goto jleave;
	}

	if ((rv = *line) == NULL || *linesize < mx_LINESIZE)
		*line = rv = su_MEM_REALLOC_LOCOR(rv, *linesize = mx_LINESIZE, su_DVL_LOC_ARGS_ORUSE);
	size = (*linesize <= *cnt) ? *linesize : *cnt + 1;
	if (size <= 1 || fgets(rv, size, fp) == NULL) {
		/* Leave llen untouched; it is used to determine whether the last line
		 * was \n-terminated in some callers */
		rv = NULL;
		goto jleave;
	}

	i_llen = _length_of_line(rv, size);
	*cnt -= i_llen;
	while (rv[i_llen - 1] != '\n') {
		*line = rv = su_MEM_REALLOC_LOCOR(rv, *linesize += 256, su_DVL_LOC_ARGS_ORUSE);
		size = *linesize - i_llen;
		size = (size <= *cnt) ? size : *cnt + 1;
		if (size <= 1) {
			if (appendnl) {
				rv[i_llen++] = '\n';
				rv[i_llen] = '\0';
			}
			break;
		} else if (fgets(rv + i_llen, size, fp) == NULL) {
			if (!feof(fp)) {
				su_err_by_errno();
				rv = NULL;
				goto jleave;
			}
			if (appendnl) {
				rv[i_llen++] = '\n';
				rv[i_llen] = '\0';
			}
			break;
		}
		size = _length_of_line(rv + i_llen, size);
		i_llen += size;
		*cnt -= size;
	}

	/* Always leave room for NETNL, not only \n */
	if(appendnl && *linesize - i_llen < 3)
		*line = rv = su_MEM_REALLOC_LOCOR(rv, *linesize += 256, su_DVL_LOC_ARGS_ORUSE);

	if(llen_or_nil != NIL)
		*llen_or_nil = i_llen;
jleave:
	NYD2_OU;
	return rv;
}

int
(readline_restart)(FILE *ibuf, char **linebuf, uz *linesize, uz n  su_DVL_LOC_ARGS_DECL)
{
	/* TODO readline_restart(): always *appends* LF just to strip it again;
	 * TODO should be configurable just as for fgetline(); ..or whatever..
	 * TODO intwrap */
	int rv = -1;
	long size;
	NYD2_IN;

	clearerr(ibuf);

	/* Interrupts will cause trouble if we are inside a stdio call. As this is
	 * only relevant if input is from tty, bypass it by read(), then */
	if ((n_psonce & n_PSO_TTYIN) && fileno(ibuf) == 0) {
		ASSERT(*linesize == 0 || *linebuf != NULL);
		n_pstate &= ~n_PS_READLINE_NL;
		for (;;) {
			if (*linesize <= mx_LINESIZE || n >= *linesize - 128) {
				*linesize += ((*linebuf == NULL) ? mx_LINESIZE + n + 1 : 256);
				*linebuf = su_MEM_REALLOC_LOCOR(*linebuf, *linesize, su_DVL_LOC_ARGS_ORUSE);
			}
jagain:
			size = read(0, *linebuf + n, *linesize - n - 1);
			if (size > 0) {
				n += size;
				(*linebuf)[n] = '\0';
				if ((*linebuf)[n - 1] == '\n') {
					n_pstate |= n_PS_READLINE_NL;
					break;
				}
			} else {
				if (size == -1 && su_err_by_errno() == su_ERR_INTR)
					goto jagain;
				/* TODO eh.  what is this?  that now supposed to be a line?!? */
				if (n > 0) {
					if ((*linebuf)[n - 1] != '\n') {
						(*linebuf)[n++] = '\n';
						(*linebuf)[n] = '\0';
					} else
						n_pstate |= n_PS_READLINE_NL;
					break;
				} else
					goto jleave;
			}
		}
	} else {
		/* Not reading from standard input or standard input not a terminal. We
		 * read one char at a time as it is the only way to get lines with
		 * embedded NUL characters in standard stdio */
		if(a_fs_fgetline_byone(linebuf, linesize, &n, ibuf, 1, n su_DVL_LOC_ARGS_USE) == NIL)
			goto jleave;
	}
	if (n > 0 && (*linebuf)[n - 1] == '\n')
		(*linebuf)[--n] = '\0';

	rv = (int)n;
jleave:
	NYD2_OU;
	return rv;
}

off_t
fsize(FILE *iob){
	struct su_pathinfo pi;
	off_t rv;
	NYD_IN;

	rv = (!su_pathinfo_fstat(&pi, fileno(iob)) ? 0 : pi.pi_size);

	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_FILE_STREAMS
/* s-itt-mode */
