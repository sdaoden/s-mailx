/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File locking, also via so-called dotlock files.
 *
 * Copyright (c) 2015 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_FILE_LOCKS_H
#define mx_FILE_LOCKS_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_file_dotlock_info;

enum mx_file_lock_mode{
   mx_FILE_LOCK_MODE_IDEFAULT, /* This is 0 */
   mx_FILE_LOCK_MODE_IFCNTL = 1u<<0, /* fcntl(2) interface */
#ifdef mx_HAVE_FLOCK
   mx_FILE_LOCK_MODE_IFLOCK = 2u<<0, /* flock(2) interface (optional) */
#endif
   mx_FILE_LOCK_MODE_IMASK = 7u<<0,
   mx_FILE_LOCK_MODE_TSHARE = 1u<<3, /* Shared (read) lock */
   mx_FILE_LOCK_MODE_TEXCL = 2u<<3, /* Exclusive (write) lock */
   mx_FILE_LOCK_MODE_TMASK = 7u<<3,
   mx_FILE_LOCK_MODE_RETRY = 1u<<6, /* Retry FILE_LOCK_TRIES times */
   mx_FILE_LOCK_MODE_LOG = 1u<<7 /* Log shall we try several times */
};

#ifdef mx_HAVE_DOTLOCK
enum mx_file_dotlock_state{
   mx_FILE_DOTLOCK_STATE_NONE,
   mx_FILE_DOTLOCK_STATE_CANT_CHDIR, /* Failed to chdir(2) into desired path */
   mx_FILE_DOTLOCK_STATE_NAMETOOLONG, /* Lock file name would be too long */
   mx_FILE_DOTLOCK_STATE_ROFS, /* Read-only filesys (no error, mailbox RO) */
   mx_FILE_DOTLOCK_STATE_NOPERM, /* No permission to create lock file */
   mx_FILE_DOTLOCK_STATE_NOEXEC, /* Privilege separated dotlocker not found */
   mx_FILE_DOTLOCK_STATE_PRIVFAILED, /* Rising privileges failed in privsep */
   mx_FILE_DOTLOCK_STATE_EXIST, /* Lock file already exists, stale lock? */
   mx_FILE_DOTLOCK_STATE_FISHY, /* Something makes us think bad of situation */
   mx_FILE_DOTLOCK_STATE_DUNNO, /* Catch-all error */
   mx_FILE_DOTLOCK_STATE_PING, /* Not an error, but have to wait for lock */
   /* ORd to any but _NONE: give up, do not retry */
   mx_FILE_DOTLOCK_STATE_ABANDON = 1u<<7
};
#endif

/**/
#define mx_FILE_LOCK_MODE_IS_TSHARE(FLM) \
   (((FLM) & mx_FILE_LOCK_MODE_TMASK) == mx_FILE_LOCK_MODE_TSHARE)

#ifdef mx_HAVE_DOTLOCK
struct mx_file_dotlock_info{
   char const *fdi_file_name; /* Mailbox to lock */
   char const *fdi_lock_name; /* .fdi_file_name + .lock */
   char const *fdi_hostname; /* ..filled in parent (due resolver delays) */
   char const *fdi_randstr; /* ..ditto, random string */
   char const *fdi_retry;  /* Empty if not wanted */
   struct stat *fdi_stb;
};
#endif

/* */
EXPORT boole mx_file_lock(int fd, BITENUM_IS(u32,mx_file_lock_mode) flm);

/* Acquire a flt mx_file_lock().
 * If *dotlock-disable* is set (FILE*)-1 is returned if flt could be acquired,
 * NIL if not, with err_no being usable.
 * Otherwise a dotlock file is created, and a registered control-pipe FILE* is
 * returned upon success which keeps the link in between us and the
 * lock-holding fork(2)ed subprocess (which conditionally replaced itself via
 * execv(2) with the privilege-separated dotlock helper program): the lock file
 * will be removed once the control pipe is closed via pipe_close().
 * If *dotlock_ignore_error* is set (FILE*)-1 will be returned if at least the
 * normal file lock could be established, otherwise err_no() is usable */
EXPORT FILE *mx_file_dotlock(char const *fname, int fd,
      BITENUM_IS(u32,mx_file_lock_mode) flm);

#include <su/code-ou.h>
#endif /* mx_FILE_LOCKS_H */
/* s-it-mode */
