/*@ (File-) Path (-descriptor) operations, path information.
 *@ TODO get_device_part, get_dir_part(), get_home(), get_tmp, is_valid, split_components, convert_separators,
 *@ TODO expand_long_name, readlink, canonicalize and/or realpath, and and and
 *
 * Copyright (c) 2021 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_PATH_H
#define su_PATH_H

/*!
 * \file
 * \ingroup PATH
 * \brief \r{PATH}
 */

#include <su/code.h>

#include <su/io-path-flags.h>
#include <su/time.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

struct su_pathinfo;

/* path {{{ */
/*!
 * \defgroup PATH Operations on (file-) paths
 * \ingroup IO
 * \brief Operations on (file-) paths (\r{su/path.h})
 *
 * Contains two groups of functions, one works on strings, the other performs physical access.
 * @{
 */

/* pathinfo {{{ */
/*!
 * \defgroup PATHINFO Path status info
 * \ingroup PATH
 * \brief Path status info (\r{su/path.h})
 * @{
 */

/*!
 * For \r{su_timespec} fields the nanosecond part may be 0, and time
 * resolution in general is filesystem-specific. */
struct su_pathinfo{
	u32 pi_flags; /*!< \r{su_iopf_permission}, \r{su_iopf_protection} and \r{su_iopf_type} \r{IOPF}. */
	su_64( u8 pi__pad[4]; )
	uz pi_nlink; /*!< Hard links or 1. */
	u64 pi_ino; /*!< Inode or 0. */
	u64 pi_dev; /*!< pi_ino's or 0. */
	u64 pi_rdev; /*!< Device type or 0. */
	uz pi_uid; /*!< (SU) User ID. */
	uz pi_gid; /*!< (SU) Group ID. */
	u64 pi_blocks; /*!< Number of 512-byte blocks, maybe 0. */
	uz pi_blksize; /*!< Preferred I/O size or 512. */
	u64 pi_size; /*!< Size in bytes */
	struct su_timespec pi_atime; /*!< Access time. */
	struct su_timespec pi_mtime; /*!< Modification time. */
	struct su_timespec pi_ctime; /*!< Change time or all-0. */
};

INLINE boole su__pathinfo_is(struct su_pathinfo const *pip, u32 t){
	return ((pip->pi_flags & su_IOPF_TYPE_MASK) == t);
}

/*! \_ */
EXPORT boole su_pathinfo_stat(struct su_pathinfo *self, char const *path);

/*! \_ */
EXPORT boole su_pathinfo_lstat(struct su_pathinfo *self, char const *path);

/*! \_ */
EXPORT boole su_pathinfo_fstat(struct su_pathinfo *self, sz fd);

/*! Get "descriptive character" for the type, or \NUL for regular file. */
EXPORT char su_pathinfo_descriptive_char(struct su_pathinfo const *self);

/*! \_ */
INLINE boole su_pathinfo_is_blk(struct su_pathinfo const *pip){
	ASSERT_RET(pip != NIL, FAL0);
	return su__pathinfo_is(pip, su_IOPF_BLK);
}

/*! \_ */
INLINE boole su_pathinfo_is_chr(struct su_pathinfo const *pip){
	ASSERT_RET(pip != NIL, FAL0);
	return su__pathinfo_is(pip, su_IOPF_CHR);
}

/*! \_ */
INLINE boole su_pathinfo_is_dir(struct su_pathinfo const *pip){
	ASSERT_RET(pip != NIL, FAL0);
	return su__pathinfo_is(pip, su_IOPF_DIR);
}

/*! \_ */
INLINE boole su_pathinfo_is_fifo(struct su_pathinfo const *pip){
	ASSERT_RET(pip != NIL, FAL0);
	return su__pathinfo_is(pip, su_IOPF_FIFO);
}

/*! \_ */
INLINE boole su_pathinfo_is_lnk(struct su_pathinfo const *pip){
	ASSERT_RET(pip != NIL, FAL0);
	return su__pathinfo_is(pip, su_IOPF_LNK);
}

/*! \_ */
INLINE boole su_pathinfo_is_reg(struct su_pathinfo const *pip){
	ASSERT_RET(pip != NIL, FAL0);
	return su__pathinfo_is(pip, su_IOPF_REG);
}

/*! \_ */
INLINE boole su_pathinfo_is_sock(struct su_pathinfo const *pip){
	ASSERT_RET(pip != NIL, FAL0);
	return su__pathinfo_is(pip, su_IOPF_SOCK);
}

/*! @} *//* }}} */

/* path utils {{{ */

#if !su_OS_POSIX || defined DOXYGEN
# define su_PATH_IS_DEV_STYLE 1 /*!< Does the operating system use paths with device parts like \c{c:\\}? */

# define su_PATH_SEP_C '\\' /*!< Directory separator byte (character). */
# define su_PATH_LIST_SEP_C ';' /*!< Path separator byte (character) in lists (\c{$PATH} etc). */

# define su_PATH_CURRENT "." /*!< "Some notion" of a the current directory. */
# define su_PATH_NULL "NUL" /*!< Literal \c{/dev/null} / \c{null} etc. */
# define su_PATH_ROOT "c:\\" /*!< "Some notion" of a root device / directory / path. */
#else
# define su_PATH_IS_DEV_STYLE 0
# define su_PATH_SEP_C '/'
# define su_PATH_LIST_SEP_C ':'
# define su_PATH_CURRENT "."
# define su_PATH_NULL "/dev/null"
# define su_PATH_ROOT "/"
#endif

#define su_PATH_AT_FDCWD -2l /*!< Pseudo file-descriptor referring to "current directory". */

/*! Compiled-in \r{CS} version of \r{su_PATH_SEP_C}. */
EXPORT_DATA char const su_path_sep[2];

/*! Compiled-in \r{CS} version of \r{su_PATH_LIST_SEP_C}. */
EXPORT_DATA char const su_path_list_sep[2];

/*! Compiled-in version of \r{su_PATH_CURRENT}. */
EXPORT_DATA char const su_path_current[sizeof su_PATH_CURRENT];

/*! Compiled-in version of \r{su_PATH_NULL}. */
EXPORT_DATA char const su_path_null[sizeof su_PATH_NULL];

/*! Compiled-in version of \r{su_PATH_ROOT}. */
EXPORT_DATA char const su_path_root[sizeof su_PATH_ROOT];

/* string */

/*! Scan \a{path} to describe the last component of a pathname.
 * \pb{For pathnames containing at least one filename: the final, or only, filename in the pathname.
 * For pathnames consisting only of \r{su_PATH_SEP_C} characters: \r{su_PATH_ROOT}
 * (on \r{su_OS_POSIX}: or \c{//} if the pathname consists of exactly two \c{//} characters).}
 * \remarks{May modify \a{path}, or return a pointer to internal constant storage.} */
EXPORT char const *su_path_basename(char *path);

/*! Scan \a{path} to report the parent directory component of a pathname.
 * \remarks{May modify \a{path}, or return a pointer to internal constant storage.} */
EXPORT char const *su_path_dirname(char *path);

/*! Does \a{path} specify an absolute path?
 * The empty path is not an absolute path.
 * \remarks{This may mean it starts with device description on some platforms.} */
INLINE boole su_path_is_absolute(char const *path){
	ASSERT_RET(path != NIL, FAL0);
#if su_PATH_IS_DEV_STYLE
# error path_is_absolute
#else
	return *path == '/';
#endif
}

/* physical */

/*! Test bitmix of \r{su_iopf_access}. */
EXPORT boole su_path_access(char const *path, BITENUM(u32,su_iopf_access) mode);

/*! Change current working directory. */
EXPORT boole su_path_chdir(char const *path);

/*! Change mode.
 * \a{permprot} is a bitmix of \r{su_iopf_permission}, \r{su_iopf_protection}.
 * The \ERR{INTR} error is handled internally! */
EXPORT boole su_path_fchmod(sz fd, u32 permprot);

/*! \_ */
EXPORT boole su_path_is_a_tty(sz fd);

/*! Link (\c{link(2)}) \a{src} to \a{dst}. */
EXPORT boole su_path_link(char const *dst, char const *src);

/*! Maximum length of a filename in the directory denoted by \a{path}.
 * If \a{path} is \NIL the current directory is used.
 * No error condition: without limits \r{su_UZ_MAX} is returned, the real value upon success,
 * and \c{NAME_MAX} on OS error or without OS support. */
EXPORT uz su_path_max_filename(char const *path);

/*! Maximum length of a pathname in the (filesystem of the) directory denoted by \a{path}.
 * If \a{path} is \NIL the current directory is used.
 * No error condition: without limits \r{su_UZ_MAX} is returned, the real value upon success,
 * and \c{PATH_MAX} on OS error or without OS support. */
EXPORT uz su_path_max_pathname(char const *path);

/*! Create directory \a{path}, possibly \a{recursive}ly, with \r{su_iopf_permission} \a{mode}.
 * A \c{su_ERR_EXIST} error results in success if \a{path} is a directory.
 * In \a{recursive} operation mode heap memory may be needed:
 * errors as via \r{su_state_err_type} can thus occur; \ESTATE. */
EXPORT boole su_path_mkdir(char const *path, BITENUM(u32,su_iopf_permission) mode, boole recursive, u32 estate);

/*! Rename (\c{rename(2)}) \a{src} to \a{dst}. */
EXPORT boole su_path_rename(char const *dst, char const *src);

#if defined su_HAVE_PATH_RM_AT || defined DOXYGEN
/*! Delete a file or an (empty) directory.
 * Dependent upon whether \a{flags} contains \r{su_IOPF_AT_RMDIR} this removes a directory or a file,
 * therefore acting like \r{su_path_rm()} or \r{su_path_rmdir()}.
 * \a{dirfd} can either be \r{su_PATH_AT_FDCWD}, or must be a file descriptor opened on a directory.
 * \remarks{Only available if \c{su_HAVE_PATH_RM_AT} is defined.} */
EXPORT boole su_path_rm_at(sz dirfd, char const *path, BITENUM(u32,su_iopf_at) flags);
#endif

/*! Delete (\c{unlink(2)}) a file. */
EXPORT boole su_path_rm(char const *path);

/*! Delete (\c{rmdir(2)}) a directory, which must be empty. */
EXPORT boole su_path_rmdir(char const *path);

/*! Change the file timestamp of \a{path} to \a{tsp_or_nil},
 * or to the current time if that is \NIL or not \r{su_timespec_is_valid()}. */
EXPORT boole su_path_touch(char const *path, struct su_timespec const *tsp_or_nil);

/*! Set the file-mode creation mask, and return the former one. *//* XXX POSIX */
EXPORT u32 su_path_umask(BITENUM(u32,su_iopf_permission) perm);
/* }}} */

/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class path;
//class path::info;

/* path {{{ */
/*!
 * \ingroup PATH
 * C++ variant of \r{PATH} (\r{su/path.h})
 */
class EXPORT path{
	// friend of time::spec
	su_CLASS_NO_COPY(path);
public:
	class info;

	/* info {{{ */
	/*! \copydoc{PATHINFO} */
	class info : private su_pathinfo{
		// friend of time::spec
	public:
		/*! \_ */
		info(void) {}

		/*! \_ */
		info(info const &t) {*S(su_pathinfo*,this) = *S(su_pathinfo const*,&t);}

		/*! \_ */
		~info(void) {}

		/*! \_ */
		info &assign(info const &t){
			*S(su_pathinfo*,this) = *S(su_pathinfo const*,&t);
			return *this;
		}

		/*! \_ */
		info &operator=(info const &t) {return assign(t);}

		/*! \copydoc{su_pathinfo_stat()} */
		boole stat(char const *path) {return su_pathinfo_stat(this, path);}

		/*! \copydoc{su_pathinfo_lstat()} */
		boole lstat(char const *path) {return su_pathinfo_lstat(this, path);}

		/*! \copydoc{su_pathinfo_fstat()} */
		boole fstat(sz fd) {return su_pathinfo_fstat(this, fd);}

		/*! \copydoc{su_pathinfo_descriptive_char()} */
		char descriptive_char(void) const {return su_pathinfo_descriptive_char(this);}

		/*! \copydoc{su_pathinfo::pi_flags}.
		 * In C++ you better use specific checks like \r{is_dir()}. */
		u32 flags(void) const {return pi_flags;}

		boole is_blk(void) const {return su_pathinfo_is_blk(this);} /*!< \copydoc{su_pathinfo_is_blk()} */
		boole is_chr(void) const {return su_pathinfo_is_chr(this);} /*!< \copydoc{su_pathinfo_is_chr()} */
		boole is_dir(void) const {return su_pathinfo_is_dir(this);} /*!< \copydoc{su_pathinfo_is_dir()} */
		boole is_fifo(void) const {return su_pathinfo_is_fifo(this);} /*!< \copydoc{su_pathinfo_is_fifo()} */
		boole is_lnk(void) const {return su_pathinfo_is_lnk(this);} /*!< \copydoc{su_pathinfo_is_lnk()} */
		boole is_reg(void) const {return su_pathinfo_is_reg(this);} /*!< \copydoc{su_pathinfo_is_reg()} */
		boole is_sock(void) const {return su_pathinfo_is_sock(this);} /*!< \copydoc{su_pathinfo_is_sock()} */

		boole is_sticky(void) const {return (pi_flags & iopf_svtx) != 0;} /*!< Has sticky bit set? */
		boole is_setgid(void) const {return (pi_flags & iopf_sgid) != 0;} /*!< Is SETGID? */
		boole is_setuid(void) const {return (pi_flags & iopf_suid) != 0;} /*!< Is SETUID? */

		uz nlink(void) const {return pi_nlink;} /*!< \copydoc{su_pathinfo::pi_nlink} */
		u64 ino(void) const {return pi_ino;} /*!< \copydoc{su_pathinfo::pi_ino} */
		u64 dev(void) const {return pi_dev;} /*!< \copydoc{su_pathinfo::pi_dev} */
		u64 rdev(void) const {return pi_rdev;} /*!< \copydoc{su_pathinfo::pi_rdev} */
		uz uid(void) const {return pi_uid;} /*!< \copydoc{su_pathinfo::pi_uid} */
		uz gid(void) const {return pi_gid;} /*!< \copydoc{su_pathinfo::pi_gid} */
		u64 blocks(void) const {return pi_blocks;} /*!< \copydoc{su_pathinfo::pi_blocks} */
		uz blksize(void) const {return pi_blksize;} /*!< \copydoc{su_pathinfo::pi_blksize} */
		u64 size(void) const {return pi_size;} /*!< \copydoc{su_pathinfo::pi_size} */

		/*! \copydoc{su_pathinfo::pi_atime} */
		time::spec const &atime(void) const {return S(time::spec const&,*&pi_atime);}

		/*! \copydoc{su_pathinfo::pi_mtime} */
		time::spec const &mtime(void) const {return S(time::spec const&,*&pi_mtime);}

		/*! \copydoc{su_pathinfo::pi_ctime} */
		time::spec const &ctime(void) const {return S(time::spec const&,*&pi_ctime);}
	};
	/* }}} */

	static boole const is_dev_style = su_PATH_IS_DEV_STYLE; /*!< \copydoc{su_PATH_IS_DEV_STYLE} */

	static char const sep_c = su_PATH_SEP_C; /*!< \copydoc{su_PATH_SEP_C} */
	static char const list_sep_c = su_PATH_LIST_SEP_C; /*!< \copydoc{su_PATH_LIST_SEP_C} */

	static char const sep[sizeof su_path_sep]; /*!< \copydoc{su_path_sep} */
	static char const list_sep[sizeof su_path_list_sep]; /*!< \copydoc{su_path_list_sep} */

	static char const current[sizeof su_PATH_CURRENT]; /*!< \copydoc{su_path_current} */
	static char const null[sizeof su_PATH_NULL]; /*!< \copydoc{su_path_null} */
	static char const root[sizeof su_PATH_ROOT]; /*!< \copydoc{su_path_root} */

	// string

	/*! \copydoc{su_path_basename()} */
	static char const *basename(char *path){
		ASSERT_RET(path != NIL, NIL);
		return su_path_basename(path);
	}

	/*! \copydoc{su_path_dirname()} */
	static char const *dirname(char *path){
		ASSERT_RET(path != NIL, NIL);
		return su_path_dirname(path);
	}

	/*! \copydoc{su_path_is_absolute()} */
	static boole is_absolute(char const *path){
		ASSERT_RET(path != NIL, FAL0);
		return su_path_is_absolute(path);
	}

	// physical

	/*! \copydoc{su_path_access()} */
	static boole access(char const *path, BITENUM(u32,iopf_access) mode){
		ASSERT_RET(path != NIL, FAL0);
		return su_path_access(path, mode);
	}

	/*! \copydoc{su_path_fchmod()} */
	static boole fchmod(sz fd, u32 permprot) {return su_path_fchmod(fd, permprot);}

	/*! \copydoc{su_path_is_a_tty()} */
	static boole is_a_tty(sz fd) {return su_path_is_a_tty(fd);}

	/*! \copydoc{su_path_link()} */
	static boole link(char const *dst, char const *src){
		ASSERT_RET(dst != NIL, FAL0);
		ASSERT_RET(src != NIL, FAL0);
		return su_path_link(dst, src);
	}

	/*! \copydoc{su_path_max_filename()} */
	static uz max_filename(char const *path=NIL) {return su_path_max_filename(path);}

	/*! \copydoc{su_path_max_pathname()} */
	static uz max_pathname(char const *path=NIL) {return su_path_max_pathname(path);}

	/*! \copydoc{su_path_mkdir()} */
	static boole mkdir(char const *path, BITENUM(u32,iopf_permission) perm=iopf_perm_mask,
			boole recursive=FAL0, u32 estate=state::none){
		ASSERT_RET(path != NIL, FAL0);
		return su_path_mkdir(path, perm, recursive, estate);
	}

	/*! \copydoc{su_path_rename()} */
	static boole rename(char const *dst, char const *src){
		ASSERT_RET(dst != NIL, FAL0);
		ASSERT_RET(src != NIL, FAL0);
		return su_path_rename(dst, src);
	}

#if defined su_HAVE_PATH_RM_AT || defined DOXYGEN
	/*! \copydoc{su_path_rm_at()} */
	static boole rm_at(sz dirfd, char const *path, BITENUM(u32,iopf_at) flags=iopf_at_none){
		ASSERT_RET(path != NIL, FAL0);
		return su_path_rm_at(dirfd, path, flags);
	}
#endif

	/*! \copydoc{su_path_rmdir()} */
	static boole rmdir(char const *path){
		ASSERT_RET(path != NIL, FAL0);
		return su_path_rmdir(path);
	}

	/*! \copydoc{su_path_touch()} */
	static boole touch(char const *path, time::spec const *tsp){
		ASSERT_RET(path != NIL, FAL0);
		return su_path_touch(path, S(struct su_timespec const*,tsp));
	}

	/*! \copydoc{su_path_umask()} */
	static u32 umask(BITENUM(u32,iopf_permission) perm) {return su_path_umask(perm);}
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_PATH_H */
/* s-itt-mode */
