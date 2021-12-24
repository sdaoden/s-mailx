/*@ (File-) Path operations.
 *
 * Copyright (c) 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#define su_HEADER
#include <su/code-in.h>
C_DECL_BEGIN

/* Forwards */
struct su_timespec;

/* path {{{ */
/*!
 * \defgroup PATH Operations on (file-) paths
 * \ingroup IO
 * \brief Operations on (file-) paths (\r{su/path.h})
 * @{
 */

/*! \c{/dev/null}. */
#define su_PATH_DEV_NULL "/dev/null"

enum su_path_type{
   su_PATH_TYPE_BLOCK_DEV,
   su_PATH_TYPE_CHAR_DEV,
   su_PATH_TYPE_DIR,
   su_PATH_TYPE_FIFO,
   su_PATH_TYPE_FILE,
   su_PATH_TYPE_LINK,
   su_PATH_TYPE_SOCKET
};

EXPORT boole su__path_is(char const *path, enum su_path_type pt,
      boole check_access);

/*! Compiled in version of \r{su_PATH_DEV_NULL}. */
EXPORT_DATA char const su_path_dev_null[sizeof su_PATH_DEV_NULL];

/**/

/*! Maximum length of a filename in the directory denoted by \a{path}.
 * If \a{path} is \NIL the current directory is used.
 * No error condition: without limits \r{su_UZ_MAX} is returned, the real
 * value upon success, and \c{NAME_MAX} on OS error or without OS support. */
EXPORT uz su_path_filename_max(char const *path);

/*! Maximum length of a pathname in the directory denoted by \a{path}.
 * If \a{path} is \NIL the current directory is used.
 * No error condition: without limits \r{su_UZ_MAX} is returned, the real
 * value upon success, and \c{PATH_MAX} on OS error or without OS support. */
EXPORT uz su_path_pathname_max(char const *path);

/**/

/*! Test if \a{path} is a directory, return \TRU1 if it is.
 * If \a{check_access} is set, we also \c{access(2)}: if it is \TRUM1 only
 * \c{X_OK|R_OK} is tested, otherwise \c{X_OK|R_OK|W_OK}. */
INLINE boole su_path_is_dir(char const *path, boole check_access){
   ASSERT_RET(path != NIL, FAL0);
   return su__path_is(path, su_PATH_TYPE_DIR, check_access);
}

/*! Create directory \a{path}, possibly \a{recursive}ly.
 * A \c{su_ERR_EXIST} error results in success if \a{path} is a directory.
 * In \a{recursive} operation mode heap memory may be needed: errors
 * as via \r{su_state_err_type} can thus occur; \ESTATE. */
EXPORT boole su_path_mkdir(char const *path, boole recursive, u32 estate);

/*! Rename (\c{rename(2)}) \a{src} to \a{dst}. */
EXPORT boole su_path_rename(char const *dst, char const *src);

/*! Delete (\c{unlink(2)}) a file. */
EXPORT boole su_path_rm(char const *path);

/*! Delete (\c{rmdir(2)}) a directory, which must be empty. */
EXPORT boole su_path_rmdir(char const *path);

/*! Change the file timestamp of \a{path} to \a{tsp_or_nil},
 * or to the current time if that is \NIL or not \r{su_timespec_is_valid()}.
 * \remarks{\c{su/time.h} is not included.} */
EXPORT boole su_path_touch(char const *path, struct su_timespec const *tsp);

/*! @} *//* }}} */

C_DECL_END
#include <su/code-ou.h>
#if !su_C_LANG || defined CXX_DOXYGEN
# include <su/time.h>

# define su_CXX_HEADER
# include <su/code-in.h>
NSPC_BEGIN(su)

class path;

/* path {{{ */
/*!
 * \ingroup PATH
 * C++ variant of \r{PATH} (\r{su/path.h})
 *
 * \remarks{In difference, for C++, \c{su/time.h} is included.}
 */
class EXPORT path{
   su_CLASS_NO_COPY(path);
public:
   /*! \copydoc{su_PATH_DEV_NULL} */
   static char const dev_null[sizeof su_PATH_DEV_NULL];

   //

   /*! \copydoc{su_path_filename_max()} */
   static uz filename_max(char const *path=NIL){
      return su_path_filename_max(path);
   }

   /*! \copydoc{su_path_pathname_max()} */
   static uz pathname_max(char const *path=NIL){
      return su_path_pathname_max(path);
   }

   //

   /*! \copydoc{su_path_is_dir()} */
   static boole is_dir(char const *path, boole check_access=FAL0){
      ASSERT_RET(path != NIL, FAL0);
      return su_path_is_dir(path, check_access);
   }

   /*! \copydoc{su_path_mkdir()} */
   static boole mkdir(char const *path, boole recursive=FAL0,
         u32 estate=state::none){
      ASSERT_RET(path != NIL, FAL0);
      return su_path_mkdir(path, recursive, estate);
   }

   /*! \copydoc{su_path_rename()} */
   static boole rename(char const *dst, char const *src){
      ASSERT_RET(dst != NIL, FAL0);
      ASSERT_RET(src != NIL, FAL0);
      return su_path_rename(dst, src);
   }

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
};
/* }}} */

NSPC_END(su)
# include <su/code-ou.h>
#endif /* !C_LANG || @CXX_DOXYGEN */
#endif /* su_PATH_H */
/* s-it-mode */
