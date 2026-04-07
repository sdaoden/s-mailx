/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of file-locks.h.
 *
 * Copyright (c) 2015 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE file_locks
#define mx_SOURCE
#define mx_SOURCE_FILE_LOCKS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <fcntl.h>
#include <unistd.h>

#ifdef mx_HAVE_FLOCK
# include mx_HAVE_FLOCK
#endif

#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/path.h>
#include <su/time.h>

#ifdef mx_HAVE_DOTLOCK
# include <su/cs.h>
#endif

#include "mx/file-streams.h"
#include "mx/random.h"
#include "mx/sigs.h"

#include "mx/file-locks.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* XXX Our pipe_open() main() takes void, temporary global data store */
#ifdef mx_HAVE_DOTLOCK
static enum mx_file_lock_mode a_filo_flm;
static int a_filo_fd;
struct mx_file_dotlock_info *a_filo_fdip;
#endif

/* Workhorse */
static boole a_filo_lock(int fd, BITENUM_IS(u32,mx_file_lock_mode) flm);

/* main() of fork(2)ed dot file locker */
#ifdef mx_HAVE_DOTLOCK
static int a_filo_main(void);
#endif

#ifdef mx_HAVE_DOTLOCK
# include "mx/file-dotlock.h" /* $(MX_SRCDIR) */
#endif

static boole
a_filo_lock(int fd, BITENUM_IS(u32,mx_file_lock_mode) flm){
   boole rv;
   NYD2_IN;

#ifdef mx_HAVE_FLOCK
   if((flm & mx_FILE_LOCK_MODE_IMASK) == mx_FILE_LOCK_MODE_IFLOCK){
      int op;

      switch(flm & mx_FILE_LOCK_MODE_TMASK){
      default:
      case mx_FILE_LOCK_MODE_TSHARE: op = LOCK_SH; break;
      case mx_FILE_LOCK_MODE_TEXCL: op = LOCK_EX; break;
      }
      op |= LOCK_NB;

      rv = (flock(fd, op) != -1);
   }else
#endif
       {
      struct flock flp;

      su_mem_set(&flp, 0, sizeof flp);

      switch(flm & mx_FILE_LOCK_MODE_TMASK){
      default:
      case mx_FILE_LOCK_MODE_TSHARE: flp.l_type = F_RDLCK; break;
      case mx_FILE_LOCK_MODE_TEXCL: flp.l_type = F_WRLCK; break;
      }
      flp.l_start = 0;
      flp.l_whence = SEEK_SET;
      flp.l_len = 0;

      rv = (fcntl(fd,
#ifdef F_OFD_SETLK
            F_OFD_SETLK,
#else
            F_SETLK,
#endif
            &flp) != -1);
   }

   if(!rv)
      switch(su_err_no_by_errno()){
      case su_ERR_BADF:
      case su_ERR_INVAL:
         rv = TRUM1;
         break;
      }

   NYD2_OU;
   return rv;
}

#ifdef mx_HAVE_DOTLOCK
static int
a_filo_main(void){
   /* Use PATH_MAX not NAME_MAX to catch those "we proclaim the minimum value"
    * problems (SunOS), since the pathconf(3) value comes too late! */
   char name[PATH_MAX +1];
   struct mx_file_dotlock_info fdi;
   struct stat stb, fdstb;
   enum mx_file_dotlock_state fdls;
   char const *cp;
   int fd;
   BITENUM_IS(u32,mx_file_lock_mode) flm;
   NYD_IN;

   /* Ignore SIGPIPE, we will see ERR_PIPE and "fall through" */
   safe_signal(SIGPIPE, SIG_IGN);

   /* Get the arguments "passed to us" */
   flm = a_filo_flm;
   fd = a_filo_fd;
   UNUSED(fd);
   fdi = *a_filo_fdip;

   /* chdir(2)? */
jislink:
   fdls = mx_FILE_DOTLOCK_STATE_CANT_CHDIR | mx_FILE_DOTLOCK_STATE_ABANDON;

   if((cp = su_cs_rfind_c(fdi.fdi_file_name, '/')) != NIL){
      char const *fname = cp + 1;

      while(PCMP(cp - 1, >, fdi.fdi_file_name) && cp[-1] == '/')
         --cp;
      cp = savestrbuf(fdi.fdi_file_name, P2UZ(cp - fdi.fdi_file_name));
      if(chdir(cp))
         goto jmsg;

      fdi.fdi_file_name = fname;
   }

   /* So we are here, but then again the file can be a symbolic link!
    * This is however only true if we do not have realpath(3) available since
    * that will have resolved the path already otherwise; nonetheless, let
    * readlink(2) be a precondition for dotlocking and keep this code */
   if(lstat(cp = fdi.fdi_file_name, &stb) == -1)
      goto jmsg;
   if(S_ISLNK(stb.st_mode)){
      /* Use AUTO_ALLOC() and hope we stay in built-in buffer.. */
      char *x;
      uz i;
      sz sr;

      for(x = NIL, i = PATH_MAX;; i += PATH_MAX){
         x = su_AUTO_ALLOC(i +1);
         sr = readlink(cp, x, i);
         if(sr <= 0){
            fdls = mx_FILE_DOTLOCK_STATE_FISHY | mx_FILE_DOTLOCK_STATE_ABANDON;
            goto jmsg;
         }
         if(UCMP(z, sr, <, i)){
            x[sr] = '\0';
            break;
         }
      }
      fdi.fdi_file_name = x;
      goto jislink;
   }

   fdls = mx_FILE_DOTLOCK_STATE_FISHY | mx_FILE_DOTLOCK_STATE_ABANDON;

   /* Bail out if the file has changed its identity in the meanwhile */
   if(fstat(fd, &fdstb) == -1 ||
         fdstb.st_dev != stb.st_dev || fdstb.st_ino != stb.st_ino ||
         fdstb.st_uid != stb.st_uid || fdstb.st_gid != stb.st_gid ||
         fdstb.st_mode != stb.st_mode)
      goto jmsg;

   /* Be aware, even if the error is false!  Note the shared code in
    * file-dotlock.h *requires* that it is possible to create a filename
    * at least one byte longer than di_lock_name! */
   /* C99 */{
      uz pc;
      int i;

      i = snprintf(name, sizeof name, "%s.lock", fdi.fdi_file_name);
      if(i < 0 || UCMP(32, i, >=, sizeof name)){
jenametool:
         fdls = mx_FILE_DOTLOCK_STATE_NAMETOOLONG |
               mx_FILE_DOTLOCK_STATE_ABANDON;
         goto jmsg;
      }

      /* fd is a file, not portable to use for _PC_NAME_MAX */
      if((pc = su_path_filename_max(NIL)) - 1 < S(uz,i))
         goto jenametool;
   }

   fdi.fdi_lock_name = name;

   /* We are in the directory of the mailbox for which we have to create
    * a dotlock file for.  Any symbolic links have been resolved.
    * We do not know whether we have realpath(3) available,and manually
    * resolving the path is due especially given that we support the special
    * "%:" syntax to warp any file into a "system mailbox"; there may also be
    * multiple system mailbox directories...
    * So what we do is that we fstat(2) the mailbox and check its UID and
    * GID against that of our own process: if any of those mismatch we must
    * either assume a directory we are not allowed to write in, or that we run
    * via -u/$USER/%USER as someone else, in which case we favour our
    * privilege-separated dotlock process */
   ASSERT(cp != NIL); /* Ugly: avoid a useless var and reuse that one */
   if(access(".", W_OK)){
      /* This may however also indicate a read-only filesystem, which is not
       * really an error from our point of view since the mailbox will degrade
       * to a readonly one for which no dotlock is needed, then, and errors
       * may arise only due to actions which require box modifications */
      if(su_err_no_by_errno() == su_ERR_ROFS){
         fdls = mx_FILE_DOTLOCK_STATE_ROFS | mx_FILE_DOTLOCK_STATE_ABANDON;
         goto jmsg;
      }
      cp = NIL;
   }

   if(cp == NIL || stb.st_uid != n_user_id || stb.st_gid != n_group_id){
      char const *args[13];

      args[ 0] = VAL_PS_DOTLOCK;
      args[ 1] = mx_FILE_LOCK_MODE_IS_TSHARE(flm) ? "rdotlock" : "wdotlock";
      args[ 2] = "mailbox"; args[ 3] = fdi.fdi_file_name;
      args[ 4] = "name"; args[ 5] = fdi.fdi_lock_name;
      args[ 6] = "hostname"; args[ 7] = fdi.fdi_hostname;
      args[ 8] = "randstr"; args[ 9] = fdi.fdi_randstr;
      args[10] = "retry"; args[11] = fdi.fdi_retry;
      args[12] = NIL;
      execv(VAL_LIBEXECDIR "/" VAL_UAGENT "-dotlock", n_UNCONST(args));

      fdls = mx_FILE_DOTLOCK_STATE_NOEXEC;
      write(STDOUT_FILENO, &fdls, sizeof fdls);
      /* But fall through and try it with normal privileges! */
   }

   /* So let's try and call it ourselves!  Note we do not block signals just
    * like our privsep child does, the user will anyway be able to remove his
    * file again, and if we are in -u/$USER mode then we are allowed to access
    * the user's box: shall we leave behind a stale dotlock then at least we
    * start a friendly human conversation.  Since we cannot handle SIGKILL and
    * SIGSTOP malicious things could happen whatever we do */
   safe_signal(SIGHUP, SIG_IGN);
   safe_signal(SIGINT, SIG_IGN);
   safe_signal(SIGQUIT, SIG_IGN);
   safe_signal(SIGTERM, SIG_IGN);

   NYD;
   fdls = a_file_lock_dotlock_create(&fdi);
   NYD;

   /* Finally: notify our parent about the actual lock state.. */
jmsg:
   write(STDOUT_FILENO, &fdls, sizeof fdls);
   close(STDOUT_FILENO);

   /* ..then eventually wait until we shall remove the lock again, which will
    * be notified via the read returning */
   if(fdls == mx_FILE_DOTLOCK_STATE_NONE){
      read(STDIN_FILENO, &fdls, sizeof fdls);

      su_path_rm(name);
   }
   NYD_OU;
   return su_EX_OK;
}
#endif /* mx_HAVE_DOTLOCK */

boole
mx_file_lock(int fd, BITENUM_IS(u32,mx_file_lock_mode) flm){
   uz tries;
   boole didmsg, rv;
   NYD2_IN;

   UNINIT(rv, 0);
   for(didmsg = FAL0, tries = 0; tries <= mx_FILE_LOCK_TRIES; ++tries){
      rv = a_filo_lock(fd, flm);

      if(rv == TRUM1){
         rv = FAL0;
         break;
      }
      if(rv || !(flm & mx_FILE_LOCK_MODE_RETRY))
         break;
      else{
         if(flm & mx_FILE_LOCK_MODE_LOG){
            if(!didmsg){
               n_err(_("Failed to lock file, waiting %lu milliseconds "),
                  S(ul,mx_FILE_LOCK_MILLIS));
               didmsg = TRU1;
            }else
               n_err(".");
         }
         su_time_msleep(mx_FILE_LOCK_MILLIS, FAL0);
      }
   }

   if(didmsg)
      n_err(" %s\n", (rv ? _("ok") : _("failure")));

   NYD2_OU;
   return rv;
}

FILE *
mx_file_dotlock(char const *fname, int fd,
      BITENUM_IS(u32,mx_file_lock_mode) flm){
#undef a_DOMSG
#define a_DOMSG() \
   do if(!didmsg){\
      didmsg = TRUM1;\
      n_err(dmsg, dmsg_name);\
}while(0)

#ifdef mx_HAVE_DOTLOCK
   sz cpipe[2];
   struct mx_file_dotlock_info fdi;
   enum mx_file_dotlock_state fdls;
   char const *emsg;
#endif
   char const *dmsg, *dmsg_name;
   int serr;
   union {uz tries; int (*ptf)(void); char const *sh; sz r;} u;
   boole flocked, didmsg;
   FILE *rv;
   NYD_IN;

   flm |= mx_FILE_LOCK_MODE_LOG;

   rv = NIL;
   didmsg = FAL0;
   UNINIT(serr, 0);
#ifdef mx_HAVE_DOTLOCK
   emsg = NIL;
#endif
   dmsg = _("Creating file (dot) lock for %s ");
   dmsg_name = n_shexp_quote_cp(fname, FAL0);

   if(n_poption & n_PO_D_VV)
      a_DOMSG();

   flocked = FAL0;
   for(u.tries = 0; !mx_file_lock(fd, flm);)
      switch((serr = su_err_no())){
      case su_ERR_ACCES:
      case su_ERR_AGAIN:
      case su_ERR_NOLCK:
         if((flm & mx_FILE_LOCK_MODE_RETRY) && ++u.tries < mx_FILE_LOCK_TRIES){
            a_DOMSG();
            n_err(".");
            su_time_msleep(mx_FILE_LOCK_MILLIS, FAL0);
            continue;
         }
         /* FALLTHRU */
      default:
         goto jleave;
      }
   flocked = TRU1;

#ifndef mx_HAVE_DOTLOCK
jleave:
   if(didmsg == TRUM1)
      n_err("\n");
   if(flocked)
      rv = (FILE*)-1;
   else
      su_err_set_no(serr);

   NYD_OU;
   return rv;

#else
   if(ok_blook(dotlock_disable)){
      rv = R(FILE*,-1);
      goto jleave;
   }

   /* Create control-pipe for our dot file locker process, which will remove
    * the lock and terminate once the pipe is closed, for whatever reason */
   if(!mx_fs_pipe_cloexec(cpipe)){
      serr = su_err_no();
      emsg = N_("  Cannot create dotlock file control pipe\n");
      goto jemsg;
   }

   /* And the locker process itself; it will be a (rather cheap) thread only
    * unless the lock has to be placed in the system spool and we have our
    * privilege-separated dotlock program available, in which case that will be
    * executed and do "it" */
   fdi.fdi_file_name = fname;
   fdi.fdi_retry = (flm & mx_FILE_LOCK_MODE_RETRY) ? n_1 : su_empty;
   /* Initialize some more stuff; query the two strings in the parent in order
    * to cache the result of the former and anyway minimalize child page-ins.
    * Especially uname(3) may hang for multiple seconds when it is called the
    * first time! */
   fdi.fdi_hostname = n_nodename(FAL0);
   fdi.fdi_randstr = mx_random_create_cp(16, NIL);
   a_filo_flm = flm;
   a_filo_fd = fd;
   a_filo_fdip = &fdi;

   u.ptf = &a_filo_main;
   rv = mx_fs_pipe_open(R(char*,-1), mx_FS_PIPE_WRITE, u.sh, NIL, cpipe[1]);
   serr = su_err_no();

   close(S(int,cpipe[1]));
   if(rv == NIL){
      close(S(int,cpipe[0]));
      emsg = N_("  Cannot create file lock process\n");
      goto jemsg;
   }

   /* Let's check whether we were able to create the dotlock file */
   for(;;){
      u.r = read(S(int,cpipe[0]), &fdls, sizeof fdls);
      if(UCMP(z, u.r, !=, sizeof fdls)){
         serr = (u.r != -1) ? su_ERR_AGAIN : su_err_no_by_errno();
         fdls = mx_FILE_DOTLOCK_STATE_DUNNO | mx_FILE_DOTLOCK_STATE_ABANDON;
      }else
         serr = su_ERR_NONE;

      if(fdls == mx_FILE_DOTLOCK_STATE_NONE ||
            (fdls & mx_FILE_DOTLOCK_STATE_ABANDON))
         close(S(int,cpipe[0]));

      switch(fdls & ~mx_FILE_DOTLOCK_STATE_ABANDON){
      case mx_FILE_DOTLOCK_STATE_NONE:
         goto jleave;
      case mx_FILE_DOTLOCK_STATE_CANT_CHDIR:
         if(n_poption & n_PO_D_V)
            emsg = N_("  Cannot change directory, please check permissions\n");
         serr = su_ERR_ACCES;
         break;
      case mx_FILE_DOTLOCK_STATE_NAMETOOLONG:
         emsg = N_("Resulting dotlock filename would be too long\n");
         serr = su_ERR_ACCES;
         break;
      case mx_FILE_DOTLOCK_STATE_ROFS:
         ASSERT(fdls & mx_FILE_DOTLOCK_STATE_ABANDON);
         if(n_poption & n_PO_D_V)
            emsg = N_("  Read-only filesystem, not creating lock file\n");
         serr = su_ERR_ROFS;
         break;
      case mx_FILE_DOTLOCK_STATE_NOPERM:
         if((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_D_V))
            emsg = N_("  Cannot create a dotlock file, "
                  "please check permissions\n");
         serr = su_ERR_ACCES;
         break;
      case mx_FILE_DOTLOCK_STATE_NOEXEC:
         if((n_psonce & (n_PSO_INTERACTIVE | n_PSO_PS_DOTLOCK_NOTED)
               ) == n_PSO_INTERACTIVE || (n_poption & n_PO_D_V)){
            n_psonce |= n_PSO_PS_DOTLOCK_NOTED;
            emsg = N_("  Cannot find privilege-separated dotlock program\n");
         }
         serr = su_ERR_NOENT;
         break;
      case mx_FILE_DOTLOCK_STATE_PRIVFAILED:
         emsg = N_("  Privilege-separated dotlock program cannot change "
               "privileges\n");
         serr = su_ERR_PERM;
         break;
      case mx_FILE_DOTLOCK_STATE_EXIST:
         emsg = N_("  It seems there is a stale dotlock file?\n"
               "  Please remove the lock file manually, then retry\n");
         serr = su_ERR_EXIST;
         break;
      case mx_FILE_DOTLOCK_STATE_FISHY:
         emsg = N_("  Fishy!  Is someone trying to \"steal\" foreign files?\n"
               "  Please check the mailbox file etc. manually, then retry\n");
         serr = su_ERR_AGAIN; /* ? Hack to ignore *dotlock-ignore-error* xxx */
         break;
      default:
      case mx_FILE_DOTLOCK_STATE_DUNNO:
         emsg = N_("  Unspecified dotlock file control process error.\n"
               "  Like broken I/O pipe; this one is unlikely to happen\n");
         if(serr != su_ERR_AGAIN)
            serr = su_ERR_INVAL;
         break;
      case mx_FILE_DOTLOCK_STATE_PING:
         a_DOMSG();
         n_err(".");
         continue;
      }

      if(emsg != NIL){
         boole b;

         a_DOMSG();
         b = ((fdls & mx_FILE_DOTLOCK_STATE_ABANDON) != 0);
         n_err(_(". failed\n%s%s"), V_(emsg),
            (b ? su_empty : _("Trying different policy ")));
         if(b)
            didmsg = TRU1;
         emsg = NIL;
      }

      if(fdls & mx_FILE_DOTLOCK_STATE_ABANDON){
         mx_fs_pipe_close(rv, FAL0);
         rv = NIL;
         break;
      }
   }

jleave:
   if(didmsg == TRUM1)
      n_err(". %s\n", (rv != NIL ? _("ok") : _("failed")));
   if(rv == NIL){
      if(flocked){
         if(serr == su_ERR_ROFS)
            rv = R(FILE*,-1);
         else if(serr != su_ERR_AGAIN && serr != su_ERR_EXIST &&
               ok_blook(dotlock_ignore_error)){
            n_OBSOLETE(_("*dotlock-ignore-error*: please use "
               "*dotlock-disable* instead"));
            if(n_poption & n_PO_D_V)
               n_err(_("  *dotlock-ignore-error* set: continuing\n"));
            rv = R(FILE*,-1);
         }else{
            n_err(_("  (Set *dotlock-disable* to bypass dotlocking)\n"));
            goto jserr;
         }
      }else
jserr:
         su_err_set_no(serr);
   }

   NYD_OU;
   return rv;
jemsg:
   a_DOMSG();
   n_err("\n");
   n_err(V_(emsg));
   didmsg = TRU1;
   goto jleave;
#endif /* mx_HAVE_DOTLOCK */
#undef a_DOMSG
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_FILE_LOCKS
/* s-it-mode */
