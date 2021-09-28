/*@ Implementation of path.h. XXX Nicer single x_posix wrapper?!
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
#define su_FILE su_path
#define su_SOURCE
#define su_SOURCE_PATH

#include "su/code.h"

su_USECASE_CONFIG_CHECKS(su_HAVE_PATHCONF su_HAVE_UTIMENSAT)

#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h> /* XXX use renameat etc. */
#include <unistd.h>

#ifdef su_HAVE_PATHCONF
# include <errno.h>
#endif
#ifdef su_HAVE_UTIMENSAT
# include <fcntl.h> /* For AT_* */
#else
# include <utime.h>
#endif

#include "su/cs.h"
#include "su/mem.h"
#include "su/time.h"

#ifdef su_HAVE_MEM_BAG_LOFI
# include "su/mem-bag.h"
#endif

#include "su/path.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* POSIX 2008/Cor 1-2013 defines a minimum of 14 for _POSIX_NAME_MAX */
#ifndef NAME_MAX
# ifdef _POSIX_NAME_MAX
#  define NAME_MAX _POSIX_NAME_MAX
# else
#  define NAME_MAX 14
# endif
#endif
#if NAME_MAX + 0 < 8
# error NAME_MAX is too small
#endif

/* POSIX 2008/Cor 1-2013 defines for
 * - _POSIX_PATH_MAX a minimum of 256
 * - _XOPEN_PATH_MAX a minimum of 1024
 * NFS RFC 1094 from March 1989 defines a MAXPATHLEN of 1024, so we really
 * should avoid anything smaller than that! */
#ifndef PATH_MAX
# ifdef MAXPATHLEN
#  define PATH_MAX MAXPATHLEN
# else
#  define PATH_MAX 1024
# endif
#endif
#if PATH_MAX + 0 < 1024
# undef PATH_MAX
# define PATH_MAX 1024
#endif

boole
su__path_is(char const *path, enum su_path_type pt, boole check_access){
   struct stat sb;
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(path != NIL, rv = FAL0);

   if(!(rv = (stat(path, &sb) == 0)))
      goto jleave;

   switch(S(u32,pt)){
#ifdef S_ISBLK
   case su_PATH_TYPE_BLOCK_DEV: rv = S_ISBLK(sb.st_mode); break;
#endif
#ifdef S_ISCHR
   case su_PATH_TYPE_CHAR_DEV: rv = S_ISCHR(sb.st_mode); break;
#endif
   case su_PATH_TYPE_DIR: rv = S_ISDIR(sb.st_mode); break;
#ifdef S_ISFIFO
   case su_PATH_TYPE_FIFO: rv = S_ISFIFO(sb.st_mode); break;
#endif
   case su_PATH_TYPE_FILE: rv = S_ISREG(sb.st_mode); break;
   case su_PATH_TYPE_LINK: rv = S_ISLNK(sb.st_mode); break;
#ifdef S_ISSOCK
   case su_PATH_TYPE_SOCKET: rv = S_ISSOCK(sb.st_mode); break;
#endif
   default: rv = FAL0; break;
   }
   if(!rv)
      goto jleave;

   if(check_access){
      int mode;

      mode = R_OK | X_OK;
      if(check_access != TRUM1)
         mode |= W_OK;
      rv = (access(path, mode) == 0);
   }

jleave:
   NYD_OU;
   return rv;
}

char const su_path_dev_null[sizeof su_PATH_DEV_NULL] = su_PATH_DEV_NULL;

uz
su_path_filename_max(char const *path){
   uz rv;
#ifdef su_HAVE_PATHCONF
   long sr;
#endif
   NYD_IN;

   if(path == NIL)
      path = ".";

#ifdef su_HAVE_PATHCONF
   errno = 0;
   if((sr = pathconf(path, _PC_NAME_MAX)) != -1)
      rv = S(uz,sr);
   else if(su_err_no_by_errno() == 0)
      rv = UZ_MAX;
   else
#endif
      rv = NAME_MAX;

   NYD_OU;
   return rv;
}

uz
su_path_pathname_max(char const *path){
   uz rv;
#ifdef su_HAVE_PATHCONF
   long sr;
#endif
   NYD_IN;

   if(path == NIL)
      path = ".";

#ifdef su_HAVE_PATHCONF
   errno = 0;
   if((sr = pathconf(path, _PC_PATH_MAX)) != -1)
      rv = S(uz,sr);
   else if(su_err_no_by_errno() == 0)
      rv = UZ_MAX;
   else
#endif
      rv = PATH_MAX;

   NYD_OU;
   return rv;
}

boole
su_path_mkdir(char const *path, boole recursive){ /* TODO estate argument! */
#undef a_ALLOC
#undef a_FREE
#if defined su_HAVE_MEM_BAG_LOFI && defined su_MEM_BAG_SELF
# define a_ALLOC(SZ) su_MEM_BAG_LOFI_ALLOCATE(su_MEM_BAG_SELF, SZ, 1, 0)
# define a_FREE(P) su_MEM_BAG_SELF_LOFI_FREE(P)
#else
# define a_ALLOC(SZ) su_ALLOC(SZ)
# define a_FREE(P) su_FREE(P)
#endif

   struct stat sb;
   char *buf;
   int e;
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(path != NIL, rv = FAL0);

   e = su_ERR_NONE;
   buf = NIL;
jredo:
   if(mkdir(path, 0777) == 0)
      rv = TRU1;
   else{
      e = su_err_no_by_errno();

      /* Try it recursively? */
      if(recursive && e == su_ERR_NOENT && buf == NIL){
         char *cp;
         uz len;

         len = su_cs_len(path) +1 +1; /* NULNUL terminated */
         if((buf = a_ALLOC(len)) == NIL){
            rv = FAL0;
            goto NYD_OU_LABEL;
         }

         /* Be aware of adjacent and trailing / */
         for(cp = buf;; ++path){
            char c, c2;

            if((c = *path) == '\0')
               break;
            /* Any intermediate level is temporarily NUL terminated.. */
            if(c == '/'){
               if(cp != buf){
                  c = '\0';
                  if((c2 = cp[-1]) == c || c2 == '/')
                     continue;
               }
            }
            *cp++ = c;
         }
         /* ..so NULNUL terminate the sequence as such */
         if(cp == buf || cp[-1] != '\0')
            *cp++ = '\0';
         *cp++ = '\0';

         path = buf;
         goto jredo;
      }

      rv = ((e == su_ERR_EXIST) && !stat(path, &sb) &&
            S_ISDIR(sb.st_mode));
   }

   /* More levels to create? */
   if(rv && recursive && buf != NIL){
      uz l;

      l = su_cs_len(buf);
      if(buf[l + 1] != '\0'){
         buf[l] = '/'; /* XXX magic dirsep */
         goto jredo;
      }
   }

   if(buf != NIL)
      a_FREE(buf);

   if(!rv)
      su_err_set_no(e);

   NYD_OU;
   return rv;

#undef a_ALLOC
#undef a_FREE
}

boole
su_path_rename(char const *dst, char const *src){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(dst != NIL, rv = FAL0);
   ASSERT_NYD_EXEC(src != NIL, rv = FAL0);

   if(!(rv = (rename(src, dst) == 0)))
      su_err_no_by_errno();

   NYD_OU;
   return rv;
}

boole
su_path_rm(char const *path){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(path != NIL, rv = FAL0);

   if(!(rv = (unlink(path) == 0)))
      su_err_no_by_errno();

   NYD_OU;
   return rv;
}

boole
su_path_rmdir(char const *path){
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(path != NIL, rv = FAL0);

   if(!(rv = (rmdir(path) == 0)))
      su_err_no_by_errno();

   NYD_OU;
   return rv;
}

boole
su_path_touch(char const *path, struct su_timespec const *tsp){
   struct su_timespec ts_x;
#ifdef su_HAVE_UTIMENSAT
   struct timespec tsa[2];
#else
   struct stat sb;
   struct utimbuf utb;
#endif
   boole rv;
   NYD_IN;

   if(tsp == NIL || !su_timespec_is_valid(tsp))
      tsp = su_timespec_current(&ts_x);

#ifdef su_HAVE_UTIMENSAT
   tsa[0].tv_sec = tsp->ts_sec + 1;
   tsa[0].tv_nsec = tsp->ts_nano;
   tsa[1].tv_nsec = UTIME_OMIT;
   rv = (utimensat(AT_FDCWD, path, tsa, 0) == 0);
#else
   if((rv = (stat(path, &sb) == 0))){
      utb.actime = tsp->ts_sec;
      utb.modtime = sb.st_mtime;
      rv = (utime(path, &utb) == 0);
   }
#endif

   if(!rv)
      su_err_no_by_errno();

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_PATH
/* s-it-mode */
