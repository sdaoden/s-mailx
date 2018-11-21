/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ n_dotlock(): creation of an exclusive "dotlock" file.
 *
 * Copyright (c) 2016 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE dotlock
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#ifdef mx_HAVE_DOTLOCK
# include <su/cs.h>

# include "mx/dotlock.h"
#endif

/* XXX Our Popen() main() takes void, temporary global data store */
#ifdef mx_HAVE_DOTLOCK
static enum n_file_lock_type a_dotlock_flt;
static int a_dotlock_fd;
struct n_dotlock_info *a_dotlock_dip;
#endif

/* main() of fork(2)ed dot file locker */
#ifdef mx_HAVE_DOTLOCK
static int a_dotlock_main(void);
#endif

#ifdef mx_HAVE_DOTLOCK
static int
a_dotlock_main(void){
   /* Use PATH_MAX not NAME_MAX to catch those "we proclaim the minimum value"
    * problems (SunOS), since the pathconf(3) value comes too late! */
   char name[PATH_MAX +1];
   struct n_dotlock_info di;
   struct stat stb, fdstb;
   enum n_dotlock_state dls;
   char const *cp;
   int fd;
   enum n_file_lock_type flt;
   n_NYD_IN;

   /* Ignore SIGPIPE, we will see su_ERR_PIPE and "fall through" */
   safe_signal(SIGPIPE, SIG_IGN);

   /* Get the arguments "passed to us" */
   flt = a_dotlock_flt;
   fd = a_dotlock_fd;
   n_UNUSED(fd);
   di = *a_dotlock_dip;

   /* chdir(2)? */
jislink:
   dls = n_DLS_CANT_CHDIR | n_DLS_ABANDON;

   if((cp = su_cs_rfind_c(di.di_file_name, '/')) != NULL){
      char const *fname = cp + 1;

      while(PTRCMP(cp - 1, >, di.di_file_name) && cp[-1] == '/')
         --cp;
      cp = savestrbuf(di.di_file_name, PTR2SIZE(cp - di.di_file_name));
      if(chdir(cp))
         goto jmsg;

      di.di_file_name = fname;
   }

   /* So we are here, but then again the file can be a symbolic link!
    * This is however only true if we do not have realpath(3) available since
    * that will have resolved the path already otherwise; nonetheless, let
    * readlink(2) be a precondition for dotlocking and keep this code */
   if(lstat(cp = di.di_file_name, &stb) == -1)
      goto jmsg;
   if(S_ISLNK(stb.st_mode)){
      /* Use n_autorec_alloc() and hope we stay in built-in buffer.. */
      char *x;
      size_t i;
      ssize_t sr;

      for(x = NULL, i = PATH_MAX;; i += PATH_MAX){
         x = n_autorec_alloc(i +1);
         sr = readlink(cp, x, i);
         if(sr <= 0){
            dls = n_DLS_FISHY | n_DLS_ABANDON;
            goto jmsg;
         }
         if(UICMP(z, sr, <, i)){
            x[sr] = '\0';
            break;
         }
      }
      di.di_file_name = x;
      goto jislink;
   }

   dls = n_DLS_FISHY | n_DLS_ABANDON;

   /* Bail out if the file has changed its identity in the meanwhile */
   if(fstat(fd, &fdstb) == -1 ||
         fdstb.st_dev != stb.st_dev || fdstb.st_ino != stb.st_ino ||
         fdstb.st_uid != stb.st_uid || fdstb.st_gid != stb.st_gid ||
         fdstb.st_mode != stb.st_mode)
      goto jmsg;

   /* Be aware, even if the error is false!  Note the shared code in dotlock.h
    * *requires* that it is possible to create a filename at least one byte
    * longer than di_lock_name! */
   do/* while(0) breaker */{
# ifdef mx_HAVE_PATHCONF
      long pc;
# endif
      int i;

      i = snprintf(name, sizeof name, "%s.lock", di.di_file_name);
      if(i < 0 || UICMP(32, i, >=, sizeof name)){
jenametool:
         dls = n_DLS_NAMETOOLONG | n_DLS_ABANDON;
         goto jmsg;
      }

      /* fd is a file, not portable to use for _PC_NAME_MAX */
# ifdef mx_HAVE_PATHCONF
      su_err_set_no(su_ERR_NONE);
      if((pc = pathconf(".", _PC_NAME_MAX)) == -1){
         /* su_err_no() unchanged: no limit */
         if(su_err_no() == 0)
            break;
# endif
         if(UICMP(z, NAME_MAX - 1, <, i))
            goto jenametool;
# ifdef mx_HAVE_PATHCONF
      }else if(pc - 1 >= i)
         break;
      else
         goto jenametool;
# endif
   }while(0);

   di.di_lock_name = name;

   /* We are in the directory of the mailbox for which we have to create
    * a dotlock file for.  Any symbolic links have been resolved.
    * We do not know whether we have realpath(3) available,and manually
    * resolving the path is due especially given that S-nail supports the
    * special "%:" syntax to warp any file into a "system mailbox"; there may
    * also be multiple system mailbox directories...
    * So what we do is that we fstat(2) the mailbox and check its UID and
    * GID against that of our own process: if any of those mismatch we must
    * either assume a directory we are not allowed to write in, or that we run
    * via -u/$USER/%USER as someone else, in which case we favour our
    * privilege-separated dotlock process */
   assert(cp != NULL); /* Ugly: avoid a useless var and reuse that one */
   if(access(".", W_OK)){
      /* This may however also indicate a read-only filesystem, which is not
       * really an error from our point of view since the mailbox will degrade
       * to a readonly one for which no dotlock is needed, then, and errors
       * may arise only due to actions which require box modifications */
      if(su_err_no() == su_ERR_ROFS){
         dls = n_DLS_ROFS | n_DLS_ABANDON;
         goto jmsg;
      }
      cp = NULL;
   }
   if(cp == NULL || stb.st_uid != n_user_id || stb.st_gid != n_group_id){
      char itoabuf[64];
      char const *args[13];

      snprintf(itoabuf, sizeof itoabuf, "%" PRIuZ, di.di_pollmsecs);
      args[ 0] = VAL_DOTLOCK_PS;
      args[ 1] = (flt == FLT_READ ? "rdotlock" : "wdotlock");
      args[ 2] = "mailbox";   args[ 3] = di.di_file_name;
      args[ 4] = "name";      args[ 5] = di.di_lock_name;
      args[ 6] = "hostname";  args[ 7] = di.di_hostname;
      args[ 8] = "randstr";   args[ 9] = di.di_randstr;
      args[10] = "pollmsecs"; args[11] = itoabuf;
      args[12] = NULL;
      execv(VAL_LIBEXECDIR "/" VAL_UAGENT "-dotlock", n_UNCONST(args));

      dls = n_DLS_NOEXEC;
      write(STDOUT_FILENO, &dls, sizeof dls);
      /* But fall through and try it with normal privileges! */
   }

   /* So let's try and call it ourselfs!  Note we do not block signals just
    * like our privsep child does, the user will anyway be able to remove his
    * file again, and if we are in -u/$USER mode then we are allowed to access
    * the user's box: shall we leave behind a stale dotlock then at least we
    * start a friendly human conversation.  Since we cannot handle SIGKILL and
    * SIGSTOP malicious things could happen whatever we do */
   safe_signal(SIGHUP, SIG_IGN);
   safe_signal(SIGINT, SIG_IGN);
   safe_signal(SIGQUIT, SIG_IGN);
   safe_signal(SIGTERM, SIG_IGN);

   n_NYD;
   dls = a_dotlock_create(&di);
   n_NYD;

   /* Finally: notify our parent about the actual lock state.. */
jmsg:
   write(STDOUT_FILENO, &dls, sizeof dls);
   close(STDOUT_FILENO);

   /* ..then eventually wait until we shall remove the lock again, which will
    * be notified via the read returning */
   if(dls == n_DLS_NONE){
      read(STDIN_FILENO, &dls, sizeof dls);

      unlink(name);
   }
   n_NYD_OU;
   return n_EXIT_OK;
}
#endif /* mx_HAVE_DOTLOCK */

FL FILE *
n_dotlock(char const *fname, int fd, enum n_file_lock_type flt,
      off_t off, off_t len, size_t pollmsecs){
#undef _DOMSG
#define _DOMSG() \
   n_err(_("Creating file lock for %s "), n_shexp_quote_cp(fname, FAL0))

#ifdef mx_HAVE_DOTLOCK
   int cpipe[2];
   struct n_dotlock_info di;
   enum n_dotlock_state dls;
   char const *emsg;
#endif
   int serr;
   union {size_t tries; int (*ptf)(void); char const *sh; ssize_t r;} u;
   bool_t flocked, didmsg;
   FILE *rv;
   n_NYD_IN;

   if(pollmsecs == UIZ_MAX)
      pollmsecs = FILE_LOCK_MILLIS;

   rv = NULL;
   didmsg = FAL0;
   n_UNINIT(serr, 0);
#ifdef mx_HAVE_DOTLOCK
   emsg = NULL;
#endif

   if(n_poption & n_PO_D_VV){
      _DOMSG();
      didmsg = TRUM1;
   }

   flocked = FAL0;
   for(u.tries = 0; !n_file_lock(fd, flt, off, len, 0);)
      switch((serr = su_err_no())){
      case su_ERR_ACCES:
      case su_ERR_AGAIN:
      case su_ERR_NOLCK:
         if(pollmsecs > 0 && ++u.tries < FILE_LOCK_TRIES){
            if(!didmsg)
               _DOMSG();
            n_err(".");
            didmsg = TRUM1;
            n_msleep(pollmsecs, FAL0);
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
   n_NYD_OU;
   return rv;

#else
   if(ok_blook(dotlock_disable)){
      rv = (FILE*)-1;
      goto jleave;
   }

   /* Create control-pipe for our dot file locker process, which will remove
    * the lock and terminate once the pipe is closed, for whatever reason */
   if(pipe_cloexec(cpipe) == -1){
      serr = su_err_no();
      emsg = N_("  Cannot create dotlock file control pipe\n");
      goto jemsg;
   }

   /* And the locker process itself; it will be a (rather cheap) thread only
    * unless the lock has to be placed in the system spool and we have our
    * privilege-separated dotlock program available, in which case that will be
    * executed and do "it" */
   di.di_file_name = fname;
   di.di_pollmsecs = pollmsecs;
   /* Initialize some more stuff; query the two strings in the parent in order
    * to cache the result of the former and anyway minimalize child page-ins.
    * Especially uname(3) may hang for multiple seconds when it is called the
    * first time! */
   di.di_hostname = n_nodename(FAL0);
   di.di_randstr = n_random_create_cp(16, NULL);
   a_dotlock_flt = flt;
   a_dotlock_fd = fd;
   a_dotlock_dip = &di;

   u.ptf = &a_dotlock_main;
   rv = Popen((char*)-1, "W", u.sh, NULL, cpipe[1]);
   serr = su_err_no();

   close(cpipe[1]);
   if(rv == NULL){
      close(cpipe[0]);
      emsg = N_("  Cannot create file lock process\n");
      goto jemsg;
   }

   /* Let's check whether we were able to create the dotlock file */
   for(;;){
      u.r = read(cpipe[0], &dls, sizeof dls);
      if(UICMP(z, u.r, !=, sizeof dls)){
         serr = (u.r != -1) ? su_ERR_AGAIN : su_err_no();
         dls = n_DLS_DUNNO | n_DLS_ABANDON;
      }else
         serr = su_ERR_NONE;

      if(dls == n_DLS_NONE || (dls & n_DLS_ABANDON))
         close(cpipe[0]);

      switch(dls & ~n_DLS_ABANDON){
      case n_DLS_NONE:
         goto jleave;
      case n_DLS_CANT_CHDIR:
         if(n_poption & n_PO_D_V)
            emsg = N_("  Cannot change directory, please check permissions\n");
         serr = su_ERR_ACCES;
         break;
      case n_DLS_NAMETOOLONG:
         emsg = N_("Resulting dotlock filename would be too long\n");
         serr = su_ERR_ACCES;
         break;
      case n_DLS_ROFS:
         assert(dls & n_DLS_ABANDON);
         if(n_poption & n_PO_D_V)
            emsg = N_("  Read-only filesystem, not creating lock file\n");
         serr = su_ERR_ROFS;
         break;
      case n_DLS_NOPERM:
         if((n_psonce & n_PSO_INTERACTIVE) || (n_poption & n_PO_D_V))
            emsg = N_("  Cannot create a dotlock file, "
                  "please check permissions\n"
                  "  (Or set *dotlock-disable*, then try again)\n");
         serr = su_ERR_ACCES;
         break;
      case n_DLS_NOEXEC:
         if((n_psonce & (n_PSO_INTERACTIVE | n_PSO_DOTLOCK_PS_NOTED)
               ) == n_PSO_INTERACTIVE || (n_poption & n_PO_D_V)){
            n_psonce |= n_PSO_DOTLOCK_PS_NOTED;
            emsg = N_("  Cannot find privilege-separated dotlock program\n");
         }
         serr = su_ERR_NOENT;
         break;
      case n_DLS_PRIVFAILED:
         emsg = N_("  Privilege-separated dotlock program cannot change "
               "privileges\n");
         serr = su_ERR_PERM;
         break;
      case n_DLS_EXIST:
         emsg = N_("  It seems there is a stale dotlock file?\n"
               "  Please remove the lock file manually, then retry\n");
         serr = su_ERR_EXIST;
         break;
      case n_DLS_FISHY:
         emsg = N_("  Fishy!  Is someone trying to \"steal\" foreign files?\n"
               "  Please check the mailbox file etc. manually, then retry\n");
         serr = su_ERR_AGAIN; /* ? Hack to ignore *dotlock-ignore-error* xxx */
         break;
      default:
      case n_DLS_DUNNO:
         emsg = N_("  Unspecified dotlock file control process error.\n"
               "  Like broken I/O pipe; this one is unlikely to happen\n");
         if(serr != su_ERR_AGAIN)
            serr = su_ERR_INVAL;
         break;
      case n_DLS_PING:
         if(!didmsg)
            _DOMSG();
         n_err(".");
         didmsg = TRUM1;
         continue;
      }

      if(emsg != NULL){
         if(!didmsg){
            _DOMSG();
            didmsg = TRUM1;
         }
         if(didmsg == TRUM1)
            n_err(_(". failed\n"));
         didmsg = TRU1;
         n_err(V_(emsg));
         emsg = NULL;
      }

      if(dls & n_DLS_ABANDON){
         Pclose(rv, FAL0);
         rv = NULL;
         break;
      }
   }

jleave:
   if(didmsg == TRUM1)
      n_err(". %s\n", (rv != NULL ? _("ok") : _("failed")));
   if(rv == NULL) {
      if(flocked){
         if(serr == su_ERR_ROFS)
            rv = (FILE*)-1;
         else if(serr != su_ERR_AGAIN && serr != su_ERR_EXIST &&
               ok_blook(dotlock_ignore_error)){
            n_OBSOLETE(_("*dotlock-ignore-error*: please use "
               "*dotlock-disable* instead"));
            if(n_poption & n_PO_D_V)
               n_err(_("  *dotlock-ignore-error* set: continuing\n"));
            rv = (FILE*)-1;
         }else
            goto jserr;
      }else
jserr:
         su_err_set_no(serr);
   }
   n_NYD_OU;
   return rv;
jemsg:
   if(!didmsg)
      _DOMSG();
   n_err("\n");
   didmsg = TRU1;
   n_err(V_(emsg));
   goto jleave;
#endif /* mx_HAVE_DOTLOCK */
#undef _DOMSG
}

/* s-it-mode */
