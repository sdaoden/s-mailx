/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Creation of an exclusive "dotlock" file.  This is (potentially) shared
 *@ in between n_dotlock() and the privilege-separated "dotlocker"..
 *@ (Which is why it does not use NYD or other utilities.)
 *@ The code assumes it has been chdir(2)d into the target directory and
 *@ that SIGPIPE is ignored (we react upon ERR_PIPE).
 *@ It furtherly assumes that it can create a filename that is at least one
 *@ byte longer than the dotlock file's name!
 *
 * Copyright (c) 2012 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-2-Clause
 */
/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Jump in */
static enum mx_file_dotlock_state a_file_lock_dotlock_create(struct mx_file_dotlock_info *fdip);

/* Create a unique file. O_EXCL does not really work over NFS so we follow
 * the following trick (inspired by S.R. van den Berg):
 * - make a mostly unique filename and try to create it
 * - link the unique filename to our target
 * - get the link count of the target
 * - unlink the mostly unique filename
 * - if the link count was 2, then we are ok; else we've failed */
static enum mx_file_dotlock_state a_file_lock_dotlock__create_excl(struct mx_file_dotlock_info *fdip, char const *lname);

static enum mx_file_dotlock_state
a_file_lock_dotlock_create(struct mx_file_dotlock_info *fdip){
	/* Use PATH_MAX not NAME_MAX to catch those "we proclaim the minimum value"
	 * problems (SunOS), since the pathconf(3) value came too late! */
	char lname[PATH_MAX +1];
	sigset_t nset, oset;
	uz tries;
	sz w;
	enum mx_file_dotlock_state rv, xrv;

	/* The callee ensured this does not end up as plain .fdi_lock_name.
	 * However, when called via dotlock helper, this may not be true in malicious
	 * cases, so add another check, then */
	snprintf(lname, sizeof lname, "%s%s%s", fdip->fdi_lock_name, fdip->fdi_randstr, fdip->fdi_hostname);
#ifdef mx_SOURCE_PS_DOTLOCK_MAIN
	if(!strcmp(lname, fdip->fdi_lock_name)){
		rv = mx_FILE_DOTLOCK_STATE_FISHY | mx_FILE_DOTLOCK_STATE_ABANDON;
		goto jleave;
	}
#endif

	sigfillset(&nset);

	for(tries = 0;; ++tries){
		sigprocmask(SIG_BLOCK, &nset, &oset);
		rv = a_file_lock_dotlock__create_excl(fdip, lname);
		sigprocmask(SIG_SETMASK, &oset, NULL);

		if(rv == mx_FILE_DOTLOCK_STATE_NONE || (rv & mx_FILE_DOTLOCK_STATE_ABANDON))
			break;
		if(fdip->fdi_retry[0] == '\0' || tries >= mx_DOTLOCK_TRIES){
			rv |= mx_FILE_DOTLOCK_STATE_ABANDON;
			break;
		}

		xrv = mx_FILE_DOTLOCK_STATE_PING;
		w = write(STDOUT_FILENO, &xrv, sizeof xrv);
		if(w == -1 && su_err_by_errno() == su_ERR_PIPE){
			rv = mx_FILE_DOTLOCK_STATE_DUNNO | mx_FILE_DOTLOCK_STATE_ABANDON;
			break;
		}
		su_time_msleep(mx_FILE_LOCK_MILLIS, FAL0);
	}

#ifdef mx_SOURCE_PS_DOTLOCK_MAIN
jleave:
#endif
	return rv;
}

static enum mx_file_dotlock_state
a_file_lock_dotlock__create_excl(struct mx_file_dotlock_info *fdip, char const *lname){
#ifdef mx_SOURCE_PS_DOTLOCK_MAIN
	struct stat pi;
#else
	struct su_pathinfo pi;
#endif
	int fd, e;
	uz tries;
	enum mx_file_dotlock_state rv;

	rv = mx_FILE_DOTLOCK_STATE_NONE;

	/* We try to create the unique filename */
	for(tries = 0;; ++tries){
		fd = open(lname,
#ifdef O_SYNC
					(O_WRONLY | O_CREAT | O_EXCL | O_SYNC),
#else
					(O_WRONLY | O_CREAT | O_EXCL),
#endif
				S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
		if(fd != -1){
#ifdef mx_SOURCE_PS_DOTLOCK_MAIN
			if(fchown(fd, fdip->fdi_uid, fdip->fdi_gid)){
				e = su_err_by_errno();
				close(fd);
				goto jbados;
			}
#endif
			close(fd);
			break;
		}else if((e = su_err_by_errno()) != su_ERR_EXIST){
			rv = ((e == su_ERR_ROFS)
					? mx_FILE_DOTLOCK_STATE_ROFS | mx_FILE_DOTLOCK_STATE_ABANDON
					: mx_FILE_DOTLOCK_STATE_NOPERM);
			goto jleave;
		}else if(tries >= mx_DOTLOCK_TRIES){
			rv = mx_FILE_DOTLOCK_STATE_EXIST;
			goto jleave;
		}
	}

	/* We link the name to the fname */
	if(!su_path_link(fdip->fdi_lock_name, lname)){
		e = su_err();
		goto jbados;
	}

	/* Note that we stat our own exclusively created name, not the
	 * destination, since the destination can be affected by others */
	if(!su_pathinfo_stat(&pi, lname)){
		e = su_err();
		goto jbados;
	}

	su_path_rm(lname);

	/* If the number of links was two (one for the unique file and one for
	 * the lock), we have won the race; also for one: encfs etc create unique */
	if(pi.
#ifdef mx_SOURCE_PS_DOTLOCK_MAIN
			st_nlink
#else
			pi_nlink
#endif
			> 2)
		rv = mx_FILE_DOTLOCK_STATE_EXIST;

jleave:
	return rv;
jbados:
	rv = ((e == su_ERR_EXIST) ? mx_FILE_DOTLOCK_STATE_EXIST
			: mx_FILE_DOTLOCK_STATE_NOPERM | mx_FILE_DOTLOCK_STATE_ABANDON);
	su_path_rm(lname);
	goto jleave;
}

/* s-itt-mode */
