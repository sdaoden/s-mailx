/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of file-streams.h.
 *@ TODO tmp_release() should be removed: tmp_open() should take an optional
 *@ TODO vector of NIL terminated {char const *mode;sz fd_result;} structs,
 *@ TODO and create all desired copies; drop HOLDSIGS, then, too!
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE file_streams
#define mx_SOURCE
#define mx_SOURCE_FILE_STREAMS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <fcntl.h>

#include <su/cs.h>
#include <su/mem.h>

#include "mx/child.h"
#include "mx/filetype.h"
#include "mx/random.h"
#include "mx/termcap.h"

#include "mx/file-streams.h"
#include "su/code-in.h"

#ifdef O_CLOEXEC
# define a_FS__O_CLOEXEC O_CLOEXEC
#else
# define a_FS__O_CLOEXEC 0
#endif

enum{
   a_FS_PIPE_READ = 0,
   a_FS_PIPE_WRITE = 1
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

/* This struct is what backs struct mx_fs_tmp_ctx! */
struct a_fs_ent{
   char *fse_realfile;
   struct a_fs_ent *fse_link;
   u32 fse_flags;
   int fse_omode;
   long fse_offset; /* TODO SU I/O 64-bit */
   FILE *fse_fp;
   char *fse_save_cmd;
   struct mx_child_ctx fse_cc;
};

static struct a_fs_ent *a_fs_fp_head;

/* Scan file open mode, and turn it to flags.  If & was prepended, set *mode
 * to NIL to indicate O_REGISTER shall not be set */
static boole a_fs_scan_mode(char const **mode, int *omode);

/* */
static struct a_fs_ent *a_fs_register_file(FILE *fp, int omode,
      struct mx_child_ctx *ccp,
      u32 flags, char const *realfile, long offset, char const *save_cmd);
static boole a_fs_unregister_file(FILE *fp);

/* */
static boole a_fs_file_load(uz flags, int infd, int outfd,
      char const *load_cmd);
static boole a_fs_file_save(struct a_fs_ent *fpp);

static boole
a_fs_scan_mode(char const **mode, int *omode){
   static struct{
      char const mode[4];
      int omode;
   }const maps[] = {
      {"r", O_RDONLY},
      {"w", O_WRONLY | O_CREAT | mx_O_NOXY_BITS | O_TRUNC},
      {"wx", O_WRONLY | O_CREAT | O_EXCL},
      {"a", O_WRONLY | O_APPEND | O_CREAT | mx_O_NOXY_BITS},
      {"a+", O_RDWR | O_APPEND | O_CREAT | mx_O_NOXY_BITS},
      {"r+", O_RDWR},
      {"w+", O_RDWR | O_CREAT | mx_O_NOXY_BITS | O_TRUNC},
      {"W+", O_RDWR | O_CREAT | O_EXCL}
   };
   boole rv;
   uz i;
   char const *xmode;
   NYD2_IN;

   if((xmode = *mode)[0] == '&'){
      *mode = NIL;
      ++xmode;
   }

   for(i = 0; i < NELEM(maps); ++i)
      if(!su_cs_cmp(maps[i].mode, xmode)){
         *omode = maps[i].omode;
         rv = TRU1;
         goto jleave;
      }

   su_DBG( n_alert(_("Internal error: bad stdio open mode %s"), xmode); )
   su_NDBG( su_err_set_no(su_ERR_INVAL); )
   *omode = 0; /* (silence CC) */
   rv = FAL0;
jleave:
   NYD2_OU;
   return rv;
}

static struct a_fs_ent *
a_fs_register_file(FILE *fp, int omode, struct mx_child_ctx *ccp, u32 flags,
      char const *realfile, long offset, char const *save_cmd){
   struct a_fs_ent *fsep;
   NYD_IN;

   ASSERT(!(flags & a_FS_EF_UNLINK) || realfile != NIL);

   fsep = su_TCALLOC(struct a_fs_ent, 1);
   if(realfile != NIL)
      fsep->fse_realfile = su_cs_dup(realfile, 0);
   fsep->fse_link = a_fs_fp_head;
   fsep->fse_flags = flags;
   fsep->fse_omode = omode;
   fsep->fse_offset = offset;
   fsep->fse_fp = fp;
   if(save_cmd != NIL)
      fsep->fse_save_cmd = su_cs_dup(save_cmd, 0);
   if(ccp != NIL)
      fsep->fse_cc = *ccp;

   a_fs_fp_head = fsep;
   NYD_OU;
   return fsep;
}

static boole
a_fs_unregister_file(FILE *fp){
   struct a_fs_ent **fsepp, *fsep;
   boole rv;
   NYD_IN;

   rv = TRU1;

   for(fsepp = &a_fs_fp_head;; fsepp = &fsep->fse_link){
      if(UNLIKELY((fsep = *fsepp) == NIL)){
         su_DBGOR(n_panic, n_alert)(_("Invalid file pointer"));
         rv = FAL0;
         break;
      }else if(fsep->fse_fp != fp)
         continue;

      switch(fsep->fse_flags & a_FS_EF_MASK){
      case a_FS_EF_RAW:
      case a_FS_EF_PIPE:
         break;
      default:
         rv = a_fs_file_save(fsep);
         break;
      }

      if((fsep->fse_flags & a_FS_EF_UNLINK) && unlink(fsep->fse_realfile))
         rv = FAL0;

      *fsepp = fsep->fse_link;
      if(fsep->fse_realfile != NIL)
         su_FREE(fsep->fse_realfile);
      if(fsep->fse_save_cmd != NIL)
         su_FREE(fsep->fse_save_cmd);

      su_FREE(fsep);
      break;
   }

   NYD_OU;
   return rv;
}

static boole
a_fs_file_load(uz flags, int infd, int outfd, char const *load_cmd){
   struct mx_child_ctx cc;
   boole rv;
   NYD2_IN;

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
      cc.cc_cmd = ok_vlook(SHELL);
      cc.cc_args[0] = "-c";
      cc.cc_args[1] = load_cmd;
      if(0){
      /* FALLTHRU */
   default:
         cc.cc_cmd = mx_FS_FILETYPE_CAT_PROG;
      }

      rv = (mx_child_run(&cc) && cc.cc_exit_status == 0);
      break;
   }

   NYD2_OU;
   return rv;
}

static boole
a_fs_file_save(struct a_fs_ent *fsep){
   struct mx_child_ctx cc;
   boole rv;
   NYD_IN;

   if(fsep->fse_omode == O_RDONLY){
      rv = TRU1;
      goto jleave;
   }
   rv = FAL0;

   fflush(fsep->fse_fp);
   clearerr(fsep->fse_fp);

   /* Ensure the I/O library does not optimize the fseek(3) away! */
   if(!n_real_seek(fsep->fse_fp, fsep->fse_offset, SEEK_SET)){
      s32 err;

      err = su_err_no();
      n_err(_("Fatal: cannot restore file position and save %s: %s\n"),
         n_shexp_quote_cp(fsep->fse_realfile, FAL0), su_err_doc(err));
      goto jleave;
   }

#ifdef mx_HAVE_MAILDIR
   if((fsep->fse_flags & a_FS_EF_MASK) == a_FS_EF_MAILDIR){
      rv = maildir_append(fsep->fse_realfile, fsep->fse_fp, fsep->fse_offset);
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
   if((cc.cc_fds[mx_CHILD_FD_OUT] = open(fsep->fse_realfile,
         ((fsep->fse_omode | O_CREAT |
          (fsep->fse_omode & O_APPEND ? 0 : O_TRUNC) | mx_O_NOXY_BITS
         ) & ~O_EXCL), 0666)) == -1){
      s32 err;

      err = su_err_no();
      n_err(_("Fatal: cannot create %s: %s\n"),
         n_shexp_quote_cp(fsep->fse_realfile, FAL0), su_err_doc(err));
      goto jleave;
   }

   switch(fsep->fse_flags & a_FS_EF_MASK){
   case a_FS_EF_HOOK:
      if(n_poption & n_PO_D_V)
         n_err(_("Using `filetype' handler %s to save %s\n"),
            n_shexp_quote_cp(fsep->fse_save_cmd, FAL0),
            n_shexp_quote_cp(fsep->fse_realfile, FAL0));
      cc.cc_cmd = ok_vlook(SHELL);
      cc.cc_args[0] = "-c";
      cc.cc_args[1] = fsep->fse_save_cmd;
      break;
   default:
      cc.cc_cmd = mx_FS_FILETYPE_CAT_PROG;
      break;
   }

   rv = (mx_child_run(&cc) && cc.cc_exit_status == 0);

   close(cc.cc_fds[mx_CHILD_FD_OUT]); /* XXX no error handling */
jleave:
   NYD_OU;
   return rv;
}

FILE *
mx_fs_open(char const *file, char const *oflags){
   int osflags, fd;
   char const *moflags;
   FILE *fp;
   NYD_IN;

   fp = NIL;

   moflags = oflags;
   if(!a_fs_scan_mode(&moflags, &osflags))
      goto jleave;
   osflags |= a_FS__O_CLOEXEC;
   if(moflags == NIL)
      ++oflags;

   if((fd = open(file, osflags, 0666)) == -1)
      goto jleave;
   mx_FS_FD_CLOEXEC_SET(fd);

   if((fp = fdopen(fd, oflags)) != NIL && moflags != NIL)
      a_fs_register_file(fp, osflags, 0, a_FS_EF_RAW, NIL, 0L, NIL);

jleave:
   NYD_OU;
   return fp;
}

FILE *
mx_fs_open_any(char const *file, char const *oflags, /* TODO take flags */
      enum mx_fs_open_state *fs_or_nil){ /* TODO as bits, return state */
   /* TODO Support file locking upon open time */
   long offset;
   enum protocol p;
   enum mx_fs_oflags rof;
   uz flags;
   int osflags, omode, infd;
   char const *cload, *csave;
   enum mx_fs_open_state fs;
   s32 err;
   FILE *rv;
   NYD_IN;

   rv = NIL;
   err = su_ERR_NONE;
   fs = mx_FS_OPEN_STATE_NONE;
   cload = csave = NIL;

   /* !O_REGISTER is disallowed */
   if(!a_fs_scan_mode(&oflags, &osflags) || oflags == NIL){
      err = su_ERR_INVAL;
      goto jleave;
   }

   flags = 0;
   rof = mx_FS_O_RDWR | mx_FS_O_UNLINK;
   if(osflags & O_APPEND)
      rof |= mx_FS_O_APPEND;
   omode = (osflags == O_RDONLY) ? R_OK : R_OK | W_OK;

   /* We don't want to find mbox.bz2 when doing "copy * mbox", but only for
    * "file mbox", so don't try hooks when writing */
   p = which_protocol(csave = file, TRU1, ((omode & W_OK) == 0), &file);
   fs = S(enum mx_fs_open_state,p);
   switch(p){
   default:
      goto jleave;
   case n_PROTO_IMAP:
#ifdef mx_HAVE_IMAP
      file = csave;
      flags |= a_FS_EF_IMAP;
      osflags = O_RDWR | O_APPEND | O_CREAT | mx_O_NOXY_BITS;
      infd = -1;
      break;
#else
      err = su_ERR_OPNOTSUPP;
      goto jleave;
#endif
   case n_PROTO_MAILDIR:
#ifdef mx_HAVE_MAILDIR
      if(fs_or_nil != NIL && !access(file, F_OK))
         fs |= mx_FS_OPEN_STATE_EXISTS;
      flags |= a_FS_EF_MAILDIR;
      osflags = O_RDWR | O_APPEND | O_CREAT | mx_O_NOXY_BITS;
      infd = -1;
      break;
#else
      err = su_ERR_OPNOTSUPP;
      goto jleave;
#endif
   case n_PROTO_FILE:{
      struct mx_filetype ft;

      if(!(osflags & O_EXCL) && fs_or_nil != NIL && !access(file, F_OK))
         fs |= mx_FS_OPEN_STATE_EXISTS;

      if(mx_filetype_exists(&ft, file)){
         flags |= a_FS_EF_HOOK;
         cload = ft.ft_load_dat;
         csave = ft.ft_save_dat;
         /* Cause truncation for compressor/hook output files */
         osflags &= ~O_APPEND;
         rof &= ~mx_FS_O_APPEND;
         if((infd = open(file, (omode & W_OK ? O_RDWR : O_RDONLY))) != -1){
            fs |= mx_FS_OPEN_STATE_EXISTS;
            if(n_poption & n_PO_D_V)
               n_err(_("Using `filetype' handler %s to load %s\n"),
                  n_shexp_quote_cp(cload, FAL0), n_shexp_quote_cp(file, FAL0));
         }else{
            err = su_err_no();
            if(!(osflags & O_CREAT) || err != su_ERR_NOENT)
               goto jleave;
         }
      }else{
         /*flags |= a_FS_EF_RAW;*/
         rv = mx_fs_open(file, oflags);
         if((osflags & O_EXCL) && rv == NIL)
            fs |= mx_FS_OPEN_STATE_EXISTS;
         goto jleave;
      }
      }break;
   }

   /* Note rv is not yet register_file()d, fclose() it in error path! */
   if((rv = mx_fs_tmp_open("fopenany", rof, NIL)) == NIL){
      n_perr(_("tmpfile"), err = su_err_no());
      goto Jerr;
   }

   if(flags & (a_FS_EF_IMAP | a_FS_EF_MAILDIR))
      ;
   else if(infd >= 0){
      if(!a_fs_file_load(flags, infd, fileno(rv), cload)) Jerr:{
         err = su_err_no();
         if(rv != NIL)
            mx_fs_close(rv);
         rv = NIL;
         if(infd >= 0)
            close(infd);
         goto jleave;
      }
   }else{
      if((infd = creat(file, 0666)) == -1){
         err = su_err_no();
         fclose(rv);
         rv = NIL;
         goto jleave;
      }
   }

   if(infd >= 0)
      close(infd);
   fflush(rv);

   if(!(osflags & O_APPEND))
      rewind(rv);

   if((offset = ftell(rv)) == -1){
      err = su_err_no();
      mx_fs_close(rv);
      rv = NIL;
      goto jleave;
   }

   a_fs_register_file(rv, osflags, 0, flags, file, offset, csave);
jleave:
   if(fs_or_nil != NIL)
      *fs_or_nil = fs;
   if(rv == NIL && err != su_ERR_NONE)
      su_err_set_no(err);
   NYD_OU;
   return rv;
}

FILE *
mx_fs_tmp_open(char const *namehint, u32 oflags,
      struct mx_fs_tmp_ctx **fstcp_or_nil){
   /* The 8 is arbitrary but leaves room for a six character suffix (the
    * POSIX minimum path length is 14, though we don't check that XXX).
    * 8 should be more than sufficient given that we use base64url encoding
    * for our random string */
   enum {a_RANDCHARS = 8u};

   char *cp_base, *cp;
   uz maxname, xlen, i;
   char const *tmpdir;
   int osoflags, fd, e;
   boole relesigs;
   FILE *fp;
   NYD_IN;

   ASSERT(namehint != NIL);
   ASSERT((oflags & mx_FS_O_WRONLY) || (oflags & mx_FS_O_RDWR));
   ASSERT(!(oflags & mx_FS_O_RDONLY));
   ASSERT(!(oflags & mx_FS_O_REGISTER_UNLINK) || (oflags & mx_FS_O_REGISTER));
   ASSERT(!(oflags & mx_FS_O_HOLDSIGS) || !(oflags & mx_FS_O_UNLINK));
   ASSERT(fstcp_or_nil == NIL || ((oflags & mx_FS_O_REGISTER) ||
      (oflags & mx_FS_O_HOLDSIGS) || !(oflags & mx_FS_O_UNLINK)));

   if(fstcp_or_nil != NIL)
      *fstcp_or_nil = NIL;

   fp = NIL;
   relesigs = FAL0;
   e = 0;
   tmpdir = ok_vlook(TMPDIR);
   maxname = NAME_MAX;

#ifdef mx_HAVE_PATHCONF
   /* C99 */{
      long pc;

      if((pc = pathconf(tmpdir, _PC_NAME_MAX)) != -1){
         maxname = S(uz,pc);
         if(maxname <= a_RANDCHARS + 1){
            su_err_set_no(su_ERR_NAMETOOLONG);
            goto jleave;
         }
      }
   }
#endif

   if((oflags & mx_FS_O_SUFFIX) && *namehint != '\0'){
      if((xlen = su_cs_len(namehint)) > maxname - a_RANDCHARS){
         su_err_set_no(su_ERR_NAMETOOLONG);
         goto jleave;
      }
   }else
      xlen = 0;

   /* Prepare the template string once, then iterate over the random range.
    * But first ensure we can report the name in !O_REGISTER cases ("hack") */
   i = su_cs_len(tmpdir) + 1 + maxname +1;

   if(!(oflags & mx_FS_O_REGISTER) && fstcp_or_nil != NIL){
      union {struct a_fs_ent *fse; void *v; struct mx_fs_tmp_ctx *fstc;} p;

      p.v = n_autorec_alloc(sizeof(*p.fse) + i);
      su_mem_set(p.fse, 0, sizeof(*p.fse));
      p.fse->fse_realfile = R(char*,&p.fse[1]);
      if(oflags & mx_FS_O_HOLDSIGS) /* Enable tmp_release() nonetheless? */
         p.fse->fse_flags = a_FS_EF_HOLDSIGS;
      *fstcp_or_nil = p.fstc;
   }

   cp_base =
   cp = n_lofi_alloc(i);
   cp = su_cs_pcopy(cp, tmpdir);
   *cp++ = '/';
   /* C99 */{
      char *x;

      x = su_cs_pcopy(cp, VAL_UAGENT);
      *x++ = '-';
      if(!(oflags & mx_FS_O_SUFFIX))
         x = su_cs_pcopy(x, namehint);

      i = P2UZ(x - cp);
      if(i > maxname - xlen - a_RANDCHARS){
         uz j;

         j = maxname - xlen - a_RANDCHARS;
         x -= i - j;
      }

      if((oflags & mx_FS_O_SUFFIX) && xlen > 0)
         su_mem_copy(x + a_RANDCHARS, namehint, xlen);

      x[xlen + a_RANDCHARS] = '\0';
      cp = x;
   }

   osoflags = O_CREAT | O_EXCL | a_FS__O_CLOEXEC;
   osoflags |= (oflags & mx_FS_O_WRONLY) ? O_WRONLY : O_RDWR;
   if(oflags & mx_FS_O_APPEND)
      osoflags |= O_APPEND;

   for(relesigs = TRU1, i = 0;; ++i){
      su_mem_copy(cp, mx_random_create_cp(a_RANDCHARS, NIL), a_RANDCHARS);

      hold_all_sigs();

      if((fd = open(cp_base, osoflags, 0600)) != -1){
         mx_FS_FD_CLOEXEC_SET(fd);
         break;
      }
      if(i >= mx_FS_TMP_OPEN_TRIES){
         e = su_err_no();
         goto jfree;
      }

      rele_all_sigs();
   }

   /* C99 */{
      char const *osflags;

      osflags = (oflags & mx_FS_O_RDWR ? "w+" : "w");

      if((fp = fdopen(fd, osflags)) != NIL){
         if(oflags & mx_FS_O_REGISTER){
            struct a_fs_ent *fsep;
            int osflagbits;

            a_fs_scan_mode(&osflags, &osflagbits); /* TODO osoflags&xy ?!!? */
            fsep = a_fs_register_file(fp, osflagbits | a_FS__O_CLOEXEC, 0,
                  (a_FS_EF_RAW |
                   (oflags & mx_FS_O_REGISTER_UNLINK ? a_FS_EF_UNLINK : 0) |
                   (oflags & mx_FS_O_HOLDSIGS ? a_FS_EF_HOLDSIGS : 0)),
               cp_base, 0L, NIL);

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
   }

   if(fp == NIL || (oflags & mx_FS_O_UNLINK)){
      e = su_err_no();
      unlink(cp_base);
      goto jfree;
   }else if(fp != NIL){
      /* We will succeed and keep the file around for further usage, likely
       * another stream will be opened for pure reading purposes (this is true
       * at the time of this writing.  A restrictive umask(2) settings may have
       * turned the path inaccessible, so ensure it may be read at least!
       * TODO once ok_vlook() can return an integer, look up *umask* first! */
      (void)fchmod(fd, S_IWUSR | S_IRUSR);
   }

   n_lofi_free(cp_base);

jleave:
   if(relesigs && (fp == NIL || !(oflags & mx_FS_O_HOLDSIGS)))
      rele_all_sigs();
   if(fp == NIL){
      su_err_set_no(e);
      if(fstcp_or_nil != NIL)
         *fstcp_or_nil = NIL;
   }
   NYD_OU;
   return fp;

jfree:
   if((cp = cp_base) != NIL)
      n_lofi_free(cp);
   goto jleave;
}

void
mx_fs_tmp_release(struct mx_fs_tmp_ctx *fstcp){
   union {void *vp; struct a_fs_ent *fsep;} u;
   NYD_IN;

   ASSERT(fstcp != NIL);

   u.vp = fstcp;
   ASSERT(u.fsep->fse_flags & a_FS_EF_HOLDSIGS);
   DBG( if(u.fsep->fse_flags & a_FS_EF_UNLINK)
      n_alert("tmp_release(): REGISTER_UNLINK set!"); )

   unlink(u.fsep->fse_realfile);

   u.fsep->fse_flags &= ~(a_FS_EF_HOLDSIGS | a_FS_EF_UNLINK);
   rele_all_sigs();
   NYD_OU;
}

FILE *
mx_fs_fd_open(sz fd, char const *oflags, boole nocloexec){
   FILE *fp;
   int osflags;
   NYD_IN;

   a_fs_scan_mode(&oflags, &osflags);
   if(!nocloexec)
      osflags |= a_FS__O_CLOEXEC; /* Ensured by caller as documented! */

   if((fp = fdopen(S(int,fd), oflags)) != NIL && oflags != NIL)
      a_fs_register_file(fp, osflags, 0, a_FS_EF_RAW, NIL, 0L, NIL);
   NYD_OU;
   return fp;
}

void
mx_fs_fd_cloexec_set(sz fd){
   s32 ifd;
   NYD2_IN;

   ifd = S(s32,fd);
   /*if((a__fl = fcntl(ifd, F_GETFD)) != -1 && !(ifd & FD_CLOEXEC))*/
      (void)fcntl(ifd, F_SETFD, FD_CLOEXEC);
   NYD2_OU;
}

boole
mx_fs_close(FILE *fp){
   boole rv;
   NYD_IN;

   rv = (a_fs_unregister_file(fp) && fclose(fp) == 0);
   NYD_OU;
   return rv;
}

boole
mx_fs_pipe_cloexec(sz fd[2]){
   int xfd[2];
   boole rv;
   NYD_IN;

   rv = FAL0;

#ifdef mx_HAVE_PIPE2
   if(pipe2(xfd, O_CLOEXEC) != -1){
      fd[0] = xfd[0];
      fd[1] = xfd[1];
      rv = TRU1;
   }
#else
   if(pipe(xfd) != -1){
      fd[0] = xfd[0];
      fd[1] = xfd[1];
      mx_fs_fd_cloexec_set(fd[0]);
      mx_fs_fd_cloexec_set(fd[1]);
      rv = TRU1;
   }
#endif

   NYD_OU;
   return rv;
}

FILE *
mx_fs_pipe_open(char const *cmd, char const *mode, char const *sh,
      char const **env_addon, int newfd1){
   struct mx_child_ctx cc;
   sz p[2], myside, hisside, fd0, fd1;
   sigset_t nset;
   char mod[2];
   FILE *rv;
   NYD_IN;

   rv = NIL;
   mod[0] = '0', mod[1] = '\0';

   if(!mx_fs_pipe_cloexec(p))
      goto jleave;

   if(*mode == 'r'){
      myside = p[a_FS_PIPE_READ];
      fd0 = mx_CHILD_FD_PASS;
      hisside = fd1 = p[a_FS_PIPE_WRITE];
      mod[0] = *mode;
   }else if(*mode == 'W'){
      myside = p[a_FS_PIPE_WRITE];
      hisside = fd0 = p[a_FS_PIPE_READ];
      fd1 = newfd1;
      mod[0] = 'w';
   }else{
      myside = p[a_FS_PIPE_WRITE];
      hisside = fd0 = p[a_FS_PIPE_READ];
      fd1 = mx_CHILD_FD_PASS;
      mod[0] = 'w';
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
         /* Standard I/O drives me insane!  All we need is a sync operation
          * that causes n_stdin to forget about any read buffer it may have.
          * We cannot use fflush(3), this works with Musl and Solaris, but not
          * with GlibC.  (For at least pipes.)  We cannot use fdreopen(),
          * because this function does not exist!  Luckily (!!!) we only use
          * n_stdin not stdin in our child, otherwise all bets were off!
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
      if(sh != NIL){
         cc.cc_cmd = sh;
         cc.cc_args[0] = "-c";
         cc.cc_args[1] = cmd;
      }

      mx_child_run(&cc);
   }

   if(cc.cc_pid < 0){
      close(S(int,p[a_FS_PIPE_READ]));
      close(S(int,p[a_FS_PIPE_WRITE]));
      goto jleave;
   }

   close(S(int,hisside));
   if((rv = fdopen(myside, mod)) != NIL)
      a_fs_register_file(rv, 0, &cc,
         ((fd0 != mx_CHILD_FD_PASS && fd1 != mx_CHILD_FD_PASS)
            ? a_FS_EF_PIPE : a_FS_EF_PIPE | a_FS_EF_FD_PASS_NEEDS_WAIT),
         NIL, 0L, NIL);
   else
      close(myside);

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
mx_fs_pipe_close(FILE *ptr, boole dowait){
   n_sighdl_t opipe;
   struct mx_child_ctx cc;
   boole rv;
   struct a_fs_ent *fsep;
   NYD_IN;

   for(fsep = a_fs_fp_head;; fsep = fsep->fse_link)
      if(UNLIKELY(fsep == NIL)){
         DBG( n_alert(_("pipe_close: invalid file pointer")); )
         rv = FAL0;
         goto jleave;
      }else if(fsep->fse_fp == ptr)
         break;
   cc = fsep->fse_cc;

   ASSERT(!(fsep->fse_flags & a_FS_EF_FD_PASS_NEEDS_WAIT) || dowait);

   a_fs_unregister_file(ptr);

   opipe = safe_signal(SIGPIPE, SIG_IGN); /* TODO only because we jump stuff */
   fclose(ptr);
   safe_signal(SIGPIPE, opipe);

   if(dowait)
      rv = (mx_child_wait(&cc) && cc.cc_exit_status == 0);
   else{
      mx_child_forget(&cc);
      rv = TRU1;
   }

jleave:
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

#include "su/code-ou.h"
/* s-it-mode */
