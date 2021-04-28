/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-fop.h.
 *@ TODO - better commandline parser that can dive into subcommands could
 *@ TODO   get rid of a lot of ERR_SYNOPSIS cruft.
 *@ TODO - use su_path_info instead of stat(2)
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
#define su_FILE cmd_fop
#define mx_SOURCE
#define mx_SOURCE_CMD_FOP

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_CMD_FOP

#include <sys/types.h> /* TODO su_path_info */
#include <sys/stat.h> /* TODO su_path_info */

#include <unistd.h> /* TODO su_path_info */

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/path.h>
#include <su/time.h>

#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"

#include "mx/cmd-fop.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

enum a_fop_err{
   a_FOP_ERR_NONE,
   a_FOP_ERR_SYNOPSIS,
   a_FOP_ERR_SUBCMD,
   a_FOP_ERR_STR_OVERFLOW,
   a_FOP_ERR_STR_NODATA,
   a_FOP_ERR_STR_GENERIC
};
enum {a_FOP_ERR__MAX = a_FOP_ERR_STR_GENERIC};

CTA(S(uz,a_FOP_ERR__MAX) <= 0x7Fu, "Bit range excess");

enum a_fop_flags{
   a_FOP_NONE,
   a_FOP_ERR = 1u<<0, /* There was an error */
   a_FOP_USE_ESTAT = 1u<<1, /* fop_ctx.fc_estat shall be returned */

   /* fop_subcmd.fs_flags: command has a NOFOLLOW variant
    * fop_ctx.fc_flags: NOFOLLOW variant _was_ actually used */
   a_FOP_MOD_NOFOLLOW = 1u<<2,
   a_FOP_MOD_FLOCK = 1u<<3, /* Only fop_subcmd.fs_flags: indeed flock lock */
   a_FOP_MOD_MASK = a_FOP_MOD_NOFOLLOW | a_FOP_MOD_FLOCK
};

struct a_fop_ctx{
   struct a_fop_subcmd const *fc_subcmd;
   u32 fc_flags;
   u8 fc_cmderr; /* On input, a_fop_cmd, on output (maybe) a_fop_err */
   boole fc_cm_local; /* `local' command modifier */
   u8 fc__pad[2];
   char const **fc_argv;
   char const *fc_varname; /* `vput' command modifier */
   char const *fc_varres;
   char const *fc_arg; /* The current arg (_ERR: which caused failure) */
   s64 fc_lhv;
   s32 fc_estat; /* To be used as `fop' exit status With FOP_USE_ESTAT */
   char fc_iencbuf[2+1/* BASE# prefix*/ + su_IENC_BUFFER_SIZE + 1];
};

struct a_fop_subcmd{
   void (*fs_cmd)(struct a_fop_ctx *fcp);
   u32 fs_flags;
   char fs_name[12];
};

struct a_fop_ofd{
   struct a_fop_ofd *fof_next;
   FILE *fof_fp;
   boole fof_silence;
   u8 fof__pad[7];
};

struct a_fop_ofd *a_fop_ofds;

/* idec and find an open fd, or NIL.
 * std_or_nil must be a pointer to storage if used, .fof_next will be NIL. */
static struct a_fop_ofd *a_fop_fd(char const *arg,
      struct a_fop_ofd *std_or_nil, struct a_fop_ofd ***fofppp_or_nil);

/* Subcmds */
static void a_fop__close(struct a_fop_ctx *fcp);
static void a_fop__expand(struct a_fop_ctx *fcp);
static void a_fop__ftruncate(struct a_fop_ctx *fcp);
static void a_fop__lock(struct a_fop_ctx *fcp);
static void a_fop__mkdir(struct a_fop_ctx *fcp);
static void a_fop__mktmp(struct a_fop_ctx *fcp);
static void a_fop__open(struct a_fop_ctx *fcp);
static void a_fop__pass(struct a_fop_ctx *fcp);
static void a_fop__rename(struct a_fop_ctx *fcp);
static void a_fop__rewind(struct a_fop_ctx *fcp);
static void a_fop__rm(struct a_fop_ctx *fcp);
static void a_fop__rmdir(struct a_fop_ctx *fcp);
static void a_fop__stat(struct a_fop_ctx *fcp);
static void a_fop__touch(struct a_fop_ctx *fcp);

static struct a_fop_subcmd const a_fop_subcmds[] = {
   {&a_fop__close, 0, "close"},
   {&a_fop__expand, 0, "expand\0"},
   {&a_fop__ftruncate, 0, "ftruncate\0"},
#ifdef mx_HAVE_FLOCK
   {&a_fop__lock, a_FOP_MOD_NOFOLLOW | a_FOP_MOD_FLOCK, "flock\0"},
#endif
      {&a_fop__lock, a_FOP_MOD_NOFOLLOW, "lock"},
   {&a_fop__mkdir, 0, "mkdir"},
   {&a_fop__mktmp, 0, "mktmp"},
   {&a_fop__open, 0, "open"},
   {&a_fop__pass, 0, "pass"},
   {&a_fop__rename, 0, "rename\0"},
   {&a_fop__rewind, 0, "rewind\0"},
   {&a_fop__rm, 0, "rm"},
   {&a_fop__rmdir, 0, "rmdir"},
   {&a_fop__stat, a_FOP_MOD_NOFOLLOW, "stat"},
   {&a_fop__touch, a_FOP_MOD_NOFOLLOW, "touch"}
};

static struct a_fop_ofd *
a_fop_fd(char const *arg, struct a_fop_ofd *std_or_nil,
      struct a_fop_ofd ***fofppp_or_nil){
   struct a_fop_ofd **fofdpp, *fofdp;
   s64 fd;
   NYD2_IN;

   if((su_idec_s64_cp(&fd, arg, 0, NIL
            ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED || fd < 0 || fd >= S32_MAX)
      fofdp = NIL;
   else{
      for(fofdpp = &a_fop_ofds;; fofdpp = &fofdp->fof_next){
         if(UNLIKELY((fofdp = *fofdpp) == NIL)){
            if(std_or_nil != NIL){
               std_or_nil->fof_next = NIL;
               if(fd == STDIN_FILENO)
                  (fofdp = std_or_nil)->fof_fp = n_stdin;
               else if(fd == STDOUT_FILENO)
                  (fofdp = std_or_nil)->fof_fp = n_stdout;
            }
            break;
         }else if(fd == fileno(fofdp->fof_fp)){
            if(fofppp_or_nil != NIL)
               *fofppp_or_nil = fofdpp;
            break;
         }
      }
   }

   NYD2_OU;
   return fofdp;
}

static void
a_fop__close(struct a_fop_ctx *fcp){
   struct a_fop_ofd **fofdpp, *fofdp;
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
   }else if((fofdp = a_fop_fd(fcp->fc_arg = fcp->fc_argv[0], NIL, &fofdpp)
         ) != NIL){
      boole silence;

      silence = fofdp->fof_silence;
      *fofdpp = fofdp->fof_next;
      fclose(fofdp->fof_fp);
      su_FREE(fofdp);
      fcp->fc_varres = silence ? NIL : fcp->fc_arg;
   }else{
      n_pstate_err_no = su_ERR_INVAL;
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }

   NYD2_OU;
}

static void
a_fop__expand(struct a_fop_ctx *fcp){
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
   }else{
      fcp->fc_arg = fcp->fc_argv[0];

      if((fcp->fc_varres = fexpand(fcp->fc_arg, FEXP_NVAR | FEXP_NOPROTO)
            ) == NIL){
         fcp->fc_flags |= a_FOP_ERR;
         fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      }
   }

   NYD2_OU;
}

static void
a_fop__ftruncate(struct a_fop_ctx *fcp){
   struct a_fop_ofd *fofdp, fofstd;
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
   }else if((fofdp = a_fop_fd(fcp->fc_arg = fcp->fc_argv[0], &fofstd, NIL)
         ) != NIL){
      ftrunc(fofdp->fof_fp);
      fcp->fc_varres = fofdp->fof_silence ? NIL : fcp->fc_arg;
   }else{
      n_pstate_err_no = su_ERR_INVAL;
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }

   NYD2_OU;
}

static void
a_fop__lock(struct a_fop_ctx *fcp){
   FILE *fp;
   boole silence;
   BITENUM_IS(u32,mx_fs_oflags) oflags;
   char const *mode;
   BITENUM_IS(u32,mx_file_lock_mode) flm;
   NYD2_IN;

#ifdef mx_HAVE_FLOCK
   if(fcp->fc_subcmd->fs_flags & a_FOP_MOD_FLOCK)
      flm = mx_FILE_LOCK_MODE_IFLOCK;
   else
#endif
      flm = mx_FILE_LOCK_MODE_IDEFAULT;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] == NIL){
jflesyn:
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }else if(flm == mx_FILE_LOCK_MODE_IDEFAULT){
      if(fcp->fc_argv[2] != NIL)
         goto jflesyn;
   }else if(fcp->fc_argv[2] != NIL && fcp->fc_argv[3] != NIL)
      goto jflesyn;

   fcp->fc_arg = fcp->fc_argv[0];
   if((fcp->fc_varres = fexpand(fcp->fc_arg, (/*FEXP_NOPROTO |*/
         FEXP_LOCAL | FEXP_NVAR))) == NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   mode = fcp->fc_arg = fcp->fc_argv[1];
   oflags = mx_FS_O_NONE;
   silence = FAL0;

   for(; *mode != '\0'; ++mode){
      switch(*mode){
      default: goto jflesyn;
      case '0': oflags |= mx_FS_O_TRUNC; break;
      case '^': silence = TRU1; break;
      case 'R':
         flm |= mx_FILE_LOCK_MODE_LOG;
         /* FALLTHRU */
      case 'r':
         if(flm & mx_FILE_LOCK_MODE_TMASK)
            goto jflesyn;
         flm |= mx_FILE_LOCK_MODE_TSHARE;
         break;
      case 'A': /* FALLTHRU */
      case 'W':
         flm |= mx_FILE_LOCK_MODE_LOG;
         /* FALLTHRU */
      case 'a':
         oflags |= mx_FS_O_APPEND;
         /* FALLTHRU */
      case 'w':
         if(flm & mx_FILE_LOCK_MODE_TMASK)
            goto jflesyn;
         flm |= mx_FILE_LOCK_MODE_TEXCL;
         break;
      }
   }
   if(!(flm & mx_FILE_LOCK_MODE_TMASK))
      goto jflesyn;

   if(mx_FILE_LOCK_MODE_IS_TSHARE(flm)){
      if(oflags != mx_FS_O_NONE)
         goto jflesyn;
      oflags = mx_FS_O_RDONLY;
   }else
      oflags |= mx_FS_O_RDWR | mx_FS_O_CREATE;

   if(fcp->fc_argv[2] == NIL)
      oflags |= mx_FS_O_NOREGISTER;
   else if(silence)
      goto jflesyn;
   if(fcp->fc_flags & a_FOP_MOD_NOFOLLOW)
      oflags |= mx_FS_O_NOFOLLOW;

   if((fp = mx_fs_open(fcp->fc_varres, oflags)) == NIL){
      n_pstate_err_no = su_err_no();
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
      goto jleave;
   }

   flm |= mx_FILE_LOCK_MODE_RETRY;
   if(!mx_file_lock(fileno(fp), flm)){
      n_pstate_err_no = su_err_no();
      if(oflags & mx_FS_O_NOREGISTER)
         fclose(fp);
      else
         mx_fs_close(fp);
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
      goto jleave;
   }

   if(oflags & mx_FS_O_NOREGISTER){
      struct a_fop_ofd *fofdp;

      fofdp = su_TALLOC(struct a_fop_ofd, 1);
      fofdp->fof_next = a_fop_ofds;
      a_fop_ofds = fofdp;
      fofdp->fof_fp = fp;
      fofdp->fof_silence = silence;
      fcp->fc_varres = su_ienc_s32(fcp->fc_iencbuf, fileno(fp), 10);
   }else{
      struct mx_child_ctx cc;

      mx_child_ctx_setup(&cc);
      cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
      cc.cc_cmd = ok_vlook(SHELL);
      cc.cc_args[0] = "-c";
      cc.cc_args[1] = fcp->fc_argv[2];
      cc.cc_fds[mx_CHILD_FD_IN] = fileno(fp);
      if(!mx_FILE_LOCK_MODE_IS_TSHARE(flm))
         cc.cc_fds[mx_CHILD_FD_OUT] = fileno(fp);

      if(mx_child_run(&cc)){
         fcp->fc_estat = cc.cc_exit_status;
         fcp->fc_varres = (fcp->fc_varname == NIL) ? NIL
               : su_ienc_s32(fcp->fc_iencbuf, fcp->fc_estat, 10);
         fcp->fc_flags |= a_FOP_USE_ESTAT;
      }else{
         fcp->fc_flags |= a_FOP_ERR;
         fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
      }

      mx_fs_close(fp);

      n_pstate_err_no = cc.cc_error;
   }

jleave:
   NYD2_OU;
}

static void
a_fop__mkdir(struct a_fop_ctx *fcp){
   char const *a1;
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL ||
         ((a1 = fcp->fc_argv[1]) != NIL && fcp->fc_argv[2] != NIL)){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }
   fcp->fc_arg = fcp->fc_argv[0];
   if(a1 == NIL)
      a1 = su_empty;

   if((fcp->fc_varres = fexpand(fcp->fc_arg, (/*FEXP_NOPROTO |*/
         FEXP_LOCAL | FEXP_NVAR))) == NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   if(!su_path_mkdir(fcp->fc_varres, n_boolify(a1, UZ_MAX, FAL0))){
      n_pstate_err_no = su_err_no();
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }

jleave:
   NYD2_OU;
}

static void
a_fop__mktmp(struct a_fop_ctx *fcp){
   struct mx_fs_tmp_ctx *fstcp;
   FILE *fp;
   char const *tdir;
   NYD2_IN;

   tdir = mx_FS_TMP_TDIR_TMP; /* (NIL) */
   if((fcp->fc_arg = fcp->fc_argv[0]) == NIL)
      fcp->fc_arg = su_empty;
   else if((tdir = fcp->fc_argv[1]) != NIL && fcp->fc_argv[2] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }

   if((fp = mx_fs_tmp_open(tdir, fcp->fc_arg, (mx_FS_O_RDWR |
         ((*fcp->fc_arg != '\0') ? mx_FS_O_SUFFIX : 0)), &fstcp)) != NIL){
      fclose(fp);
      fcp->fc_varres = fstcp->fstc_filename;
   }else{
      n_pstate_err_no = su_err_no();
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }

jleave:
   NYD2_OU;
}

static void
a_fop__open(struct a_fop_ctx *fcp){
   FILE *fp;
   boole silence;
   BITENUM_IS(u32,mx_fs_oflags) oflags;
   char const *mode;
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] == NIL ||
         fcp->fc_argv[2] != NIL){
jesyn:
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }

   fcp->fc_arg = fcp->fc_argv[0];
   if((fcp->fc_varres = fexpand(fcp->fc_arg, (/*FEXP_NOPROTO |*/
         FEXP_LOCAL | FEXP_NVAR))) == NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   mode = fcp->fc_arg = fcp->fc_argv[1];
   oflags = mx_FS_O_NOREGISTER | mx_FS_O_NOCLOEXEC;
   silence = FAL0;

   for(; *mode != '\0'; ++mode){
      switch(*mode){
      default: goto jesyn;
      case '0': oflags |= mx_FS_O_TRUNC; break;
      case '^': silence = TRU1; break;
      case 'r':
         if(oflags & mx__FS_O_RWMASK)
            goto jesyn;
         oflags |= mx_FS_O_RDONLY;
         break;
      case 'A':
         oflags |= mx_FS_O_EXCL;
         /* FALLTHRU */
      case 'a':
         if(oflags & mx__FS_O_RWMASK)
            goto jesyn;
         oflags |= mx_FS_O_RDWR | mx_FS_O_APPEND | mx_FS_O_CREATE;
         break;
      case 'W':
         oflags |= mx_FS_O_EXCL;
         /* FALLTHRU */
      case 'w':
         if(oflags & mx__FS_O_RWMASK)
            goto jesyn;
         oflags |= mx_FS_O_RDWR | mx_FS_O_CREATE;
         break;
      }
   }
   if(!(oflags & mx__FS_O_RWMASK))
      goto jesyn;

   if((fp = mx_fs_open(fcp->fc_varres, oflags)) == NIL){
      n_pstate_err_no = su_err_no();
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
      goto jleave;
   }

   /* C99 */{
      struct a_fop_ofd *fofdp;

      fofdp = su_TALLOC(struct a_fop_ofd, 1);
      fofdp->fof_next = a_fop_ofds;
      a_fop_ofds = fofdp;
      fofdp->fof_fp = fp;
      fofdp->fof_silence = silence;
      fcp->fc_varres = su_ienc_s32(fcp->fc_iencbuf, fileno(fp), 10);
   }

jleave:
   NYD2_OU;
}

static void
a_fop__pass(struct a_fop_ctx *fcp){
   struct mx_child_ctx cc;
   struct a_fop_ofd *fofdp;
   uz i;
   NYD2_IN;

   mx_child_ctx_setup(&cc);

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] == NIL ||
         fcp->fc_argv[2] == NIL || fcp->fc_argv[3] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }

   /* STDIN first */
   i = 0;
jredo:
   fcp->fc_arg = fcp->fc_argv[i];
   if((fcp->fc_arg[0] == '-' || fcp->fc_arg[0] == '@') &&
         fcp->fc_arg[1] == '\0'){
      cc.cc_fds[(i == 0) ? mx_CHILD_FD_IN : mx_CHILD_FD_OUT
         ] = (fcp->fc_arg[0] == '-') ? mx_CHILD_FD_PASS : mx_CHILD_FD_NULL;
   }else if((fofdp = a_fop_fd(fcp->fc_arg, NIL, NIL)) != NIL)
      cc.cc_fds[(i == 0) ? mx_CHILD_FD_IN : mx_CHILD_FD_OUT
         ] = fileno(fofdp->fof_fp);
   else{
      n_pstate_err_no = su_ERR_INVAL;
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
      goto jleave;
   }
   /* Then STDOUT */
   if(i++ == 0)
      goto jredo;

   cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
   cc.cc_cmd = ok_vlook(SHELL);
   cc.cc_args[0] = "-c";
   cc.cc_args[1] = fcp->fc_argv[i];

   if(mx_child_run(&cc)){
      fcp->fc_estat = cc.cc_exit_status;
      fcp->fc_varres = (fcp->fc_varname == NIL) ? NIL
            : su_ienc_s32(fcp->fc_iencbuf, fcp->fc_estat, 10);
      fcp->fc_flags |= a_FOP_USE_ESTAT;
   }else{
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }
   n_pstate_err_no = cc.cc_error;

jleave:
   NYD2_OU;
}

static void
a_fop__rename(struct a_fop_ctx *fcp){
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] == NIL ||
         fcp->fc_argv[2] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
   }else{
      char const *src;

      fcp->fc_arg = fcp->fc_argv[0];

      if((fcp->fc_varres = fexpand(fcp->fc_arg, FEXP_NVAR | FEXP_NOPROTO)
               ) == NIL ||
            (src = fexpand(fcp->fc_argv[1], FEXP_NVAR | FEXP_NOPROTO)) == NIL){
         fcp->fc_flags |= a_FOP_ERR;
         fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      }else if(!su_path_rename(fcp->fc_varres, src)){
         n_pstate_err_no = su_err_no();
         fcp->fc_flags |= a_FOP_ERR;
         fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
      }
   }

   NYD2_OU;
}

static void
a_fop__rewind(struct a_fop_ctx *fcp){
   struct a_fop_ofd *fofdp, fofstd;
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
   }else if((fofdp = a_fop_fd(fcp->fc_arg = fcp->fc_argv[0], &fofstd, NIL)
         ) != NIL){
      really_rewind(fofdp->fof_fp);
      fcp->fc_varres = fofdp->fof_silence ? NIL : fcp->fc_arg;
   }else{
      n_pstate_err_no = su_ERR_INVAL;
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }

   NYD2_OU;
}

static void
a_fop__rm(struct a_fop_ctx *fcp){
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }
   fcp->fc_arg = fcp->fc_argv[0];

   if((fcp->fc_varres = fexpand(fcp->fc_arg, (/*FEXP_NOPROTO |*/
         FEXP_LOCAL | FEXP_NVAR))) == NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   if(!su_path_rm(fcp->fc_varres)){
      n_pstate_err_no = su_err_no();
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }

jleave:
   NYD2_OU;
}

static void
a_fop__rmdir(struct a_fop_ctx *fcp){
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }
   fcp->fc_arg = fcp->fc_argv[0];

   if((fcp->fc_varres = fexpand(fcp->fc_arg, (/*FEXP_NOPROTO |*/
         FEXP_LOCAL | FEXP_NVAR))) == NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   if(!su_path_rmdir(fcp->fc_varres)){
      n_pstate_err_no = su_err_no();
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
   }

jleave:
   NYD2_OU;
}

static void
a_fop__stat(struct a_fop_ctx *fcp){
   struct stat st;
   struct n_string s_b, *s;
   char c;
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }
   fcp->fc_arg = fcp->fc_argv[0];

   if((fcp->fc_varres = fexpand(fcp->fc_arg, (/*FEXP_NOPROTO |*/
         FEXP_LOCAL | FEXP_NVAR))) == NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   if(((fcp->fc_flags & a_FOP_MOD_NOFOLLOW) ? lstat : stat
         )(fcp->fc_varres, &st) != 0){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   s = n_string_book(n_string_creat_auto(&s_b), 250);
   s = n_string_push_cp(s, "st_file=");
   s = n_string_push_cp(s, n_shexp_quote_cp(fcp->fc_varres, FAL0));
   s = n_string_push_c(s, ' ');

   s = n_string_push_cp(s, "st_type=");
   if(S_ISDIR(st.st_mode)) c = '/';
   else if(S_ISLNK(st.st_mode)) c = '@';
#ifdef S_ISBLK
   else if(S_ISBLK(st.st_mode)) c = '#';
#endif
#ifdef S_ISCHR
   else if(S_ISCHR(st.st_mode)) c = '%';
#endif
#ifdef S_ISFIFO
   else if(S_ISFIFO(st.st_mode)) c = '|';
#endif
#ifdef S_ISSOCK
   else if(S_ISSOCK(st.st_mode)) c = '=';
#endif
   else c = '.';
   s = n_string_push_c(s, c);
   s = n_string_push_c(s, ' ');

   s = n_string_push_cp(s, "st_nlink=");
   s = n_string_push_cp(s, su_ienc_s64(fcp->fc_iencbuf, st.st_nlink, 10));
   s = n_string_push_c(s, ' ');

   s = n_string_push_cp(s, "st_size=");
   s = n_string_push_cp(s, su_ienc_u64(fcp->fc_iencbuf, st.st_size, 10));
   s = n_string_push_c(s, ' ');

   s = n_string_push_cp(s, "st_mode=");
   s = n_string_push_cp(s, su_ienc_s32(fcp->fc_iencbuf,
         st.st_mode & 07777, 8));
   s = n_string_push_c(s, ' ');

   s = n_string_push_cp(s, "st_uid=");
   s = n_string_push_cp(s, su_ienc_s64(fcp->fc_iencbuf, st.st_uid, 10));
   s = n_string_push_c(s, ' ');

   s = n_string_push_cp(s, "st_gid=");
   s = n_string_push_cp(s, su_ienc_s64(fcp->fc_iencbuf, st.st_gid, 10));
   s = n_string_push_c(s, ' ');

   fcp->fc_varres = n_string_cp(s);
   /* n_string_gut(n_string_drop_ownership(s)); */

jleave:
   NYD2_OU;
}

static void
a_fop__touch(struct a_fop_ctx *fcp){
   FILE *fp;
   NYD2_IN;

   if(fcp->fc_argv[0] == NIL || fcp->fc_argv[1] != NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_SYNOPSIS;
      goto jleave;
   }
   fcp->fc_arg = fcp->fc_argv[0];

   if((fcp->fc_varres = fexpand(fcp->fc_arg, (/*FEXP_NOPROTO |*/
         FEXP_LOCAL | FEXP_NVAR))) == NIL){
      fcp->fc_flags |= a_FOP_ERR;
      fcp->fc_cmderr = a_FOP_ERR_STR_NODATA;
      goto jleave;
   }

   if(!su_path_touch(fcp->fc_varres, n_time_now(TRU1))){
      if((fp = mx_fs_open(fcp->fc_varres, (mx_FS_O_WRONLY | mx_FS_O_CREATE |
               ((fcp->fc_flags & a_FOP_MOD_NOFOLLOW) ? mx_FS_O_NOFOLLOW : 0)))
               ) != NIL)
         mx_fs_close(fp);
      else{
         n_pstate_err_no = su_err_no();
         fcp->fc_flags |= a_FOP_ERR;
         fcp->fc_cmderr = a_FOP_ERR_STR_GENERIC;
      }
   }

jleave:
   NYD2_OU;
}

int
c_fop(void *vp){
   struct a_fop_ctx fc;
   boole is_l, was_l;
   uz i, j;
   u32 f;
   NYD_IN;

   /*DVL(*/ su_mem_set(&fc, 0xAA, sizeof fc); /*)*/
   fc.fc_flags = a_FOP_ERR;
   fc.fc_cmderr = a_FOP_ERR_SUBCMD;
   fc.fc_cm_local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);
   fc.fc_argv = S(char const**,vp);
   fc.fc_varname = (n_pstate & n_PS_ARGMOD_VPUT) ? *fc.fc_argv++ : NIL;
   fc.fc_varres = su_empty;
   fc.fc_arg = *fc.fc_argv++;

   f = a_FOP_NONE;
   j = su_cs_len(fc.fc_arg);
   is_l = (j > 0 && su_cs_to_lower(fc.fc_arg[0]) == 'l');

   for(i = 0; i < NELEM(a_fop_subcmds); ++i){
      was_l = FAL0;
      if(su_cs_starts_with_case_n(a_fop_subcmds[i].fs_name, fc.fc_arg, j) ||
            (is_l && (a_fop_subcmds[i].fs_flags & a_FOP_MOD_NOFOLLOW) &&
             (was_l = su_cs_starts_with_case_n(a_fop_subcmds[i].fs_name,
                  &fc.fc_arg[1], j - 1)))){
         fc.fc_arg = (fc.fc_subcmd = &a_fop_subcmds[i])->fs_name;
         fc.fc_flags = f | (was_l ? a_FOP_MOD_NOFOLLOW : 0);

         (*fc.fc_subcmd->fs_cmd)(&fc);
         break;
      }
   }
   f = fc.fc_flags;

   if(LIKELY(!(f & a_FOP_ERR))){
      n_pstate_err_no = su_ERR_NONE;
   }else switch(fc.fc_cmderr){
   case a_FOP_ERR_NONE:
      ASSERT(0);
      break;
   case a_FOP_ERR_SYNOPSIS:
      mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("fop"), NIL);
      n_pstate_err_no = su_ERR_INVAL;
      goto jestr;
   case a_FOP_ERR_SUBCMD:
      n_err(_("fop: invalid subcommand: %s\n"),
         n_shexp_quote_cp(fc.fc_arg, FAL0));
      n_pstate_err_no = su_ERR_INVAL;
      goto jestr;
   case a_FOP_ERR_STR_OVERFLOW:
      n_err(_("fop: string length or offset overflows datatype\n"));
      n_pstate_err_no = su_ERR_OVERFLOW;
      goto jestr;
   case a_FOP_ERR_STR_NODATA:
      n_pstate_err_no = su_ERR_NODATA;
      /* FALLTHRU*/
   case a_FOP_ERR_STR_GENERIC:
jestr:
      fc.fc_varres = su_empty;
      f = a_FOP_ERR;
      break;
   }

   if(fc.fc_varname == NIL){
      if(fc.fc_varres != NIL && fprintf(n_stdout, "%s\n", fc.fc_varres) < 0){
         n_pstate_err_no = su_err_no();
         f |= a_FOP_ERR;
      }
   }else if(!n_var_vset(fc.fc_varname, R(up,fc.fc_varres), fc.fc_cm_local)){
      n_pstate_err_no = su_ERR_NOTSUP;
      f |= a_FOP_ERR;
   }

   NYD_OU;
   return ((f & a_FOP_USE_ESTAT) ? fc.fc_estat : ((f & a_FOP_ERR) ? 1 : 0));
}

#include "su/code-ou.h"
#endif /* mx_HAVE_CMD_FOP */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_FOP
/* s-it-mode */
