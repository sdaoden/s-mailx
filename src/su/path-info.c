/*@ Implementation of path.h, info.
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
#define su_FILE su_path_info
#define su_SOURCE
#define su_SOURCE_PATH_INFO

#include "su/code.h"

su_USECASE_CONFIG_CHECKS(
	su_HAVE_STAT_BLOCKS
	su_HAVE_STAT_TIMESPEC su_HAVE_STAT_TIMENSEC
	)

#include <sys/stat.h>

#include "su/time.h"

#include "su/path.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Port to native MS-Windows and to ancient UNIX */
#if !defined S_ISDIR && defined S_IFDIR && defined S_IFMT
# define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

NSPC_USE(su)

static void a_pathinfo_copy(struct su_pathinfo *self, struct stat *sp);

static void
a_pathinfo_copy(struct su_pathinfo *self, struct stat *sp){
	u32 x, f;
	NYD2_IN;

	x = S(u32,sp->st_mode);

	/* */
	if(su_IOPF_XOTH == S_IXOTH && su_IOPF_WOTH == S_IWOTH && su_IOPF_ROTH == S_IROTH &&
				su_IOPF_XGRP == S_IXGRP && su_IOPF_WGRP == S_IWGRP && su_IOPF_RGRP == S_IRGRP &&
				su_IOPF_XUSR == S_IXUSR && su_IOPF_WUSR == S_IWUSR && su_IOPF_RUSR == S_IRUSR &&
#ifdef S_ISVTX
			su_IOPF_SVTX == S_ISVTX &&
#endif
				su_IOPF_SGID == S_ISGID && su_IOPF_SUID == S_ISUID){
		f = (x & (S_IXOTH | S_IWOTH | S_IROTH | S_IXGRP | S_IWGRP | S_IRGRP | S_IXUSR | S_IWUSR | S_IRUSR |
#ifdef S_ISVTX
				S_ISVTX |
#endif
					S_ISGID | S_ISUID));
	}else{
		f = 0;
		if(x & S_IXOTH) f |= su_IOPF_XOTH;
		if(x & S_IWOTH) f |= su_IOPF_WOTH;
		if(x & S_IROTH) f |= su_IOPF_ROTH;
		if(x & S_IXGRP) f |= su_IOPF_XGRP;
		if(x & S_IWGRP) f |= su_IOPF_WGRP;
		if(x & S_IRGRP) f |= su_IOPF_RGRP;
		if(x & S_IXUSR) f |= su_IOPF_XUSR;
		if(x & S_IWUSR) f |= su_IOPF_WUSR;
		if(x & S_IRUSR) f |= su_IOPF_RUSR;
#ifdef S_ISVTX
		if(x & S_ISVTX) f |= su_IOPF_SVTX;
#endif
		if(x & S_ISGID) f |= su_IOPF_SGID;
		if(x & S_ISUID) f |= su_IOPF_SUID;
	}

	/* */
#if defined S_IFCHR && defined S_IFDIR && defined S_IFBLK && defined S_IFREG
	if(
# ifdef S_IFIFO
			su_IOPF_FIFO == S_IFIFO &&
# endif
			su_IOPF_CHR == S_IFCHR && su_IOPF_DIR == S_IFDIR && su_IOPF_BLK == S_IFBLK &&
			su_IOPF_REG == S_IFREG
# ifdef S_IFLNK
			&& su_IOPF_LNK == S_IFLNK
# endif
# ifdef S_IFSOCK
			&& su_IOPF_SOCK == S_IFSOCK
# endif
# ifdef S_IFWHT
			&& su_IOPF_WHT == S_IFWHT
# endif
			){
		f |= x & su_IOPF_TYPE_MASK;
	}else
#endif
	{
		if(S_ISCHR(x)) f |= su_IOPF_CHR;
		else if(S_ISDIR(x)) f |= su_IOPF_DIR;
		else if(S_ISBLK(x)) f |= su_IOPF_BLK;
		/*else if(S_ISREG(x)) f |= su_IOPF_REG; */
#ifdef S_ISIFO
		else if(S_ISFIFO(x)) f |= su_IOPF_FIFO;
#endif
#ifdef S_IFLNK
		else if(S_ISLNK(x)) f |= su_IOPF_LNK;
#endif
#ifdef S_IFSOCK
		else if(S_ISSOCK(x)) f |= su_IOPF_SOCK;
#endif
#ifdef S_IFWHT
		else if(S_ISWHT(x)) f |= su_IOPF_WHT;
#endif
		else
			f |= su_IOPF_REG;
	}

	self->pi_flags = f;
	self->pi_nlink = sp->st_nlink;
	self->pi_ino = S(u64,sp->st_ino);
	self->pi_dev = S(u64,sp->st_dev);
	self->pi_rdev = S(u64,sp->st_rdev);
	self->pi_uid = S(uz,sp->st_uid);
	self->pi_gid = S(uz,sp->st_gid);
	self->pi_blocks =
#ifdef su_HAVE_STAT_BLOCKS
			sp->st_blocks
#else
			((S(u64,sp->st_size) + 511) & ~511) >> 9
#endif
			;
	self->pi_blksize = sp->st_blksize;
	self->pi_size = sp->st_size;
#ifdef su_HAVE_STAT_TIMESPEC
	self->pi_atime.ts_sec = S(s64,sp->st_atim.tv_sec);
	self->pi_atime.ts_nano = S(sz,sp->st_atim.tv_nsec);
	self->pi_mtime.ts_sec = S(s64,sp->st_mtim.tv_sec);
	self->pi_mtime.ts_nano = S(sz,sp->st_mtim.tv_nsec);
	self->pi_ctime.ts_sec = S(s64,sp->st_ctim.tv_sec);
	self->pi_ctime.ts_nano = S(sz,sp->st_ctim.tv_nsec);
#elif defined su_HAVE_STAT_TIMENSEC
	self->pi_atime.ts_sec = S(s64,sp->st_atime);
	self->pi_atime.ts_nano = S(sz,sp->st_atimensec);
	self->pi_mtime.ts_sec = S(s64,sp->st_mtime);
	self->pi_mtime.ts_nano = S(sz,sp->st_mtimensec);
	self->pi_ctime.ts_sec = S(s64,sp->st_ctime);
	self->pi_ctime.ts_nano = S(sz,sp->st_ctimensec);
#else
	self->pi_atime.ts_sec = S(s64,sp->st_atime);
	self->pi_atime.ts_nano = 0;
	self->pi_mtime.ts_sec = S(s64,sp->st_mtime);
	self->pi_mtime.ts_nano = 0;
	self->pi_ctime.ts_sec = S(s64,sp->st_ctime);
	self->pi_ctime.ts_nano = 0;
#endif

	NYD2_OU;
}

boole
su_pathinfo_stat(struct su_pathinfo *self, char const *path){
	struct stat sb;
	boole rv;
	NYD_IN;
	ASSERT(self);
	ASSERT_NYD_EXEC(path != NIL, rv = FAL0);

	if((rv = (stat(path, &sb) == 0)))
		a_pathinfo_copy(self, &sb);
	else
		su_err_no_by_errno();

	NYD_OU;
	return rv;
}

boole
su_pathinfo_lstat(struct su_pathinfo *self, char const *path){
	struct stat sb;
	boole rv;
	NYD_IN;
	ASSERT(self);
	ASSERT_NYD_EXEC(path != NIL, rv = FAL0);

	if((rv = (lstat(path, &sb) == 0)))
		a_pathinfo_copy(self, &sb);
	else
		su_err_no_by_errno();

	NYD_OU;
	return rv;
}

boole
su_pathinfo_fstat(struct su_pathinfo *self, sz fd){
	struct stat sb;
	boole rv;
	NYD_IN;
	ASSERT(self);

	if((rv = (fstat(fd, &sb) == 0)))
		a_pathinfo_copy(self, &sb);
	else
		su_err_no_by_errno();

	NYD_OU;
	return rv;
}

char
su_pathinfo_descriptive_char(struct su_pathinfo const *self){
	char rv;
	NYD_IN;
	ASSERT(self);

	if(su_pathinfo_is_blk(self))
		rv = '#';
	else if(su_pathinfo_is_chr(self))
		rv = '%';
	else if(su_pathinfo_is_dir(self))
		rv = '/';
	else if(su_pathinfo_is_fifo(self))
		rv = '|';
	else if(su_pathinfo_is_lnk(self))
		rv = '@';
	/*else if(su_pathinfo_is_reg(self))*/
	else if(su_pathinfo_is_sock(self))
		rv = '=';
	else
		rv = '\0';

	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_PATH_INFO
/* s-itt-mode */
