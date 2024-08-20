/*@ I/O and path flags (IOPF).
 *
 * Copyright (c) 2001 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef su_IO_PATH_FLAGS_H
#define su_IO_PATH_FLAGS_H

/*!
 * \file
 * \ingroup IO
 * \brief I/O and path flags
 */

#include <su/code.h>

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* flags {{{ */
/*!
 * \defgroup IOPF I/O and path flags
 * \ingroup IO
 * \brief I/O and path flags (\r{su/io-path-flags.h})
 * @{
 */

/*! Path access test bits, for example for \r{su_path_access()}. */
enum su_iopf_access{
	su_IOPF_EXIST = 0, /*!< Path exists. */
	su_IOPF_EXEC = 1u<<0, /*!< User may execute/search. */
	su_IOPF_WRITE = 1u<<1, /*!< User may write. */
	su_IOPF_READ = 1u<<4 /*!< User may read. */
};
enum{
	su_IOPF_ACCESS_MASK = su_IOPF_READ | su_IOPF_WRITE | su_IOPF_EXEC /*!< All of \r{su_iopf_access}. */
};

/*! Flags for \c{*at()} function series, for example \r{su_path_rm_at()}. */
enum su_iopf_at{
	su_IOPF_AT_NONE = 0, /*!< \_ */
	su_IOPF_AT_RMDIR = 1u<<0, /*!< Remove a(n empty) directory only, not any other file type. */
	su_IOPF_AT_EACCESS = 1u<<1, /*!< Test with effective, not real user and group IDs. */
	su_IOPF_AT_LNK_FOLLOW = 1u<<2, /*!< Follow symbolic links. */
	su_IOPF_AT_LNK_NOFOLLOW = 1u<<3 /*!< Do not follow symbolic links. */
};

/*! POSIXish path permission bits.
 * These reflect 1:1 the POSIX bits, numerical constants may thus be used. */
enum su_iopf_permission{
	su_IOPF_XOTH = 1u<<0, /*!< Others: execute/search permission. */
	su_IOPF_WOTH = 1u<<1, /*!< Others: write permission. */
	su_IOPF_ROTH = 1u<<2, /*!< Others: read permission. */
	su_IOPF_RWXOTH = su_IOPF_XOTH | su_IOPF_WOTH | su_IOPF_ROTH, /*!< Others: all permissions. */
	su_IOPF_XGRP = 1u<<3, /*!< Group: execute/search permission. */
	su_IOPF_WGRP = 1u<<4, /*!< Group: write permission. */
	su_IOPF_RGRP = 1u<<5, /*!< Group: read permission. */
	su_IOPF_RWXGRP = su_IOPF_XGRP | su_IOPF_WGRP | su_IOPF_RGRP, /*!< Group: all permissions. */
	su_IOPF_XUSR = 1u<<6, /*!< User: execute/search permission. */
	su_IOPF_WUSR = 1u<<7, /*!< User: write permission. */
	su_IOPF_RUSR = 1u<<8, /*!< User: read permission. */
	su_IOPF_RWXUSR = su_IOPF_XUSR | su_IOPF_WUSR | su_IOPF_RUSR /*!< User: all permissions. */
};
enum{
	su_IOPF_PERM_MASK = su_IOPF_RWXOTH | su_IOPF_RWXGRP | su_IOPF_RWXUSR, /*!< All of \r{su_iopf_permission}. */
	su_IOPF_PERM_DEF = su_IOPF_PERM_MASK & ~(su_IOPF_XOTH | su_IOPF_XGRP | su_IOPF_XUSR) /*!< 0666. */
};

/*! POSIXish path protection bits beyond \r{su_iopf_permission}. */
enum su_iopf_protection{
	su_IOPF_SVTX = 1u<<9, /*!< Sticky bit. */
	su_IOPF_SGID = 1u<<10, /*!< Set group ID. */
	su_IOPF_SUID = 1u<<11 /*!< Set user ID. */
};
enum{
	su_IOPF_PROT_MASK = su_IOPF_SVTX | su_IOPF_SGID | su_IOPF_SUID /*!< All of \r{su_iopf_protection}. */
};

/*! POSIXish file types (bits not unique). */
enum su_iopf_type{
	su_IOPF_FIFO = 1u<<12, /*!< FIFO. */
	su_IOPF_CHR = 2u<<12, /*!< Character device. */
	su_IOPF_DIR = 4u<<12, /*!< Directory. */
	su_IOPF_BLK = 6u<<12, /*!< Block device. */
	su_IOPF_REG = 8u<<12, /*!< Regular file. */
	su_IOPF_LNK = 10u<<12, /*!< Symbolic link. */
	su_IOPF_SOCK = 12u<<12, /*!< Socket. */
	su_IOPF_WHT = 14u<<12 /*!< Whiteout. */
};
enum{
	su_IOPF_TYPE_MASK = 15u<<12 /*!< Mask for \r{su_iopf_type}. */
};

/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

/* flags {{{ */
/*!
 * \ingroup IO
 * C++ variant of \r{IOPF} (\r{su/io-path-flags.h})
 * @{
 */

/*! \cd{su_iopf_access} */
enum iopf_access{
	iopf_exist = su_IOPF_EXIST, /*!< \cd{su_IOPF_EXIST} */
	iopf_exec = su_IOPF_EXEC, /*!< \cd{su_IOPF_EXEC} */
	iopf_write = su_IOPF_WRITE, /*!< \cd{su_IOPF_WRITE} */
	iopf_read = su_IOPF_READ /*!< \cd{su_IOPF_READ} */
};
enum{
	iopf_access_mask = su_IOPF_ACCESS_MASK /*!< \cd{su_IOPF_ACCESS_MASK} */
};

/*! \cd{su_iopf_at} */
enum iopf_at{
	iopf_at_none = su_IOPF_AT_NONE, /*!< \cd{su_IOPF_AT_NONE} */
	iopf_at_rmdir = su_IOPF_AT_RMDIR, /*!< \cd{su_IOPF_AT_RMDIR} */
	iopf_at_eaccess = su_IOPF_AT_EACCESS, /*!< \cd{su_IOPF_AT_EACCESS} */
	iopf_at_lnk_follow = su_IOPF_AT_LNK_FOLLOW, /*!< \cd{su_IOPF_AT_LNK_FOLLOW} */
	iopf_at_lnk_nofollow = su_IOPF_AT_LNK_NOFOLLOW/*!< \cd{su_IOPF_AT_LNK_NOFOLLOW} */
};

/*! \cd{su_iopf_permission} */
enum iopf_permission{
	iopf_xoth = su_IOPF_XOTH, /*!< \cd{su_IOPF_XOTH} */
	iopf_woth = su_IOPF_WOTH, /*!< \cd{su_IOPF_WOTH} */
	iopf_roth = su_IOPF_ROTH, /*!< \cd{su_IOPF_ROTH} */
	iopf_rwxoth = su_IOPF_RWXOTH, /*!< \cd{su_IOPF_RWXOTH} */
	iopf_xgrp = su_IOPF_XGRP, /*!< \cd{su_IOPF_XGRP} */
	iopf_wgrp = su_IOPF_WGRP, /*!< \cd{su_IOPF_WGRP} */
	iopf_rgrp = su_IOPF_RGRP, /*!< \cd{su_IOPF_RGRP} */
	iopf_rwxgrp = su_IOPF_RWXGRP, /*!< \cd{su_IOPF_RWXGRP} */
	iopf_xusr = su_IOPF_XUSR, /*!< \cd{su_IOPF_XUSR} */
	iopf_wusr = su_IOPF_WUSR, /*!< \cd{su_IOPF_WUSR} */
	iopf_rusr = su_IOPF_RUSR, /*!< \cd{su_IOPF_RUSR} */
	iopf_rwxusr = su_IOPF_RWXUSR /*!< \cd{su_IOPF_RWXUSR} */
};
enum{
	iopf_perm_mask = su_IOPF_PERM_MASK, /*!< \cd{su_IOPF_PERM_MASK} */
	iopf_perm_def = su_IOPF_PERM_DEF /*!< \cd{su_IOPF_PERM_DEF} */
};

/*! \cd{su_iopf_protection} */
enum iopf_protection{
	iopf_svtx = su_IOPF_SVTX, /*!< \cd{su_IOPF_SVTX} */
	iopf_sgid = su_IOPF_SGID, /*!< \cd{su_IOPF_SGID} */
	iopf_suid = su_IOPF_SUID /*!< \cd{su_IOPF_SUID} */
};
enum{
	iopf_prot_mask = su_IOPF_PROT_MASK /*!< \cd{su_IOPF_PROT_MASK} */
};

/*! \cd{su_iopf_type} */
enum iopf_type{
	iopf_fifo = su_IOPF_FIFO, /*!< \cd{su_IOPF_FIFO} */
	iopf_chr = su_IOPF_CHR, /*!< \cd{su_IOPF_CHR} */
	iopf_dir = su_IOPF_DIR, /*!< \cd{su_IOPF_DIR} */
	iopf_blk = su_IOPF_BLK, /*!< \cd{su_IOPF_BLK} */
	iopf_reg = su_IOPF_REG, /*!< \cd{su_IOPF_REG} */
	iopf_lnk = su_IOPF_LNK, /*!< \cd{su_IOPF_LNK} */
	iopf_sock = su_IOPF_SOCK, /*!< \cd{su_IOPF_SOCK} */
	iopf_wht = su_IOPF_WHT /*!< \cd{su_IOPF_WHT} */
};
enum{
	iopf_type_mask = su_IOPF_TYPE_MASK /*!< \cd{su_IOPF_TYPE_MASK} */
};

/*! @} *//* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_IO_PATH_FLAGS_H */
/* s-itt-mode */
