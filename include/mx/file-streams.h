/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File- and pipe streams, as well as temporary file creation.
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
#ifndef mx_FILE_STREAMS_H
#define mx_FILE_STREAMS_H

#include <mx/nail.h>

#include <su/code-in.h>

enum mx_fs_oflags{
   mx_FS_O_RDONLY = 1u<<0,
   mx_FS_O_WRONLY = 1u<<1,
   mx_FS_O_RDWR = 1u<<2,
   mx_FS_O_APPEND = 1u<<3,
   mx_FS_O_CREATE = 1u<<4,
   mx_FS_O_TRUNC = 1u<<5,
   mx_FS_O_EXCL = 1u<<6,
   mx_FS_O_UNLINK = 1u<<7, /* Only for tmp_open(): unlink(2) after creation */
   /* Register file in our file table, causing its close when we jump away
    * and/or the mainloop ticks otherwise, shall it still exist */
   mx_FS_O_REGISTER = 1u<<8,
   /* tmp_open(): unlink at unregistration: O_REGISTER!, !O_UNLINK */
   mx_FS_O_REGISTER_UNLINK = 1u<<9,
   /* tmp_open(): do not release signals/unlink: !O_UNLINK! */
   mx_FS_O_HOLDSIGS = 1u<<10,
   mx_FS_O_SUFFIX = 1u<<11 /* tmp_open() name hint is mandatory! extension! */
};

enum mx_fs_open_state{ /* TODO add mx_fs_open_mode, too */
   /* Lower bits are in fact enum protocol! */
   mx_FS_OPEN_STATE_NONE = 0,
   mx_FS_OPEN_STATE_EXISTS = 1u<<5
};
MCTA(n_PROTO_MASK < mx_FS_OPEN_STATE_EXISTS, "Bit carrier ranges overlap")

/* Note: actually publically visible part of larger internal struct */
struct mx_fs_tmp_ctx{
   char const *fstc_filename;
};

/* */
#ifdef O_CLOEXEC
# define mx_FS_FD_CLOEXEC_SET(FD) do {;} while(0)
#else
# define mx_FS_FD_CLOEXEC_SET(FD) mx_fs_fd_cloexec_set(FD)
#endif

/* oflags implied: cloexec,O_REGISTER.
 *    {"r", O_RDONLY},
 *    {"w", O_WRONLY | O_CREAT | n_O_NOXY_BITS | O_TRUNC},
 *    {"wx", O_WRONLY | O_CREAT | O_EXCL},
 *    {"a", O_WRONLY | O_APPEND | O_CREAT | mx_O_NOXY_BITS},
 *    {"a+", O_RDWR | O_APPEND | O_CREAT | mx_O_NOXY_BITS},
 *    {"r+", O_RDWR},
 *    {"w+", O_RDWR | O_CREAT | mx_O_NOXY_BITS | O_TRUNC},
 *    {"W+", O_RDWR | O_CREAT | O_EXCL}
 * Prepend (!) an ampersand & ("background") to _not_ include O_REGISTER,
 * in which case the returned file must be closed with normal fclose(3).
 * mx_O_NOXY_BITS come from mx-config.h */
FL FILE *mx_fs_open(char const *file, char const *oflags);

/* TODO: Should be Mailbox::create_from_url(URL::from_string(DATA))!
 * Open file according to oflags (& prefix disallowed)m and register it
 * (leading ampersand & to suppress this is disallowed).
 * Handles compressed files, maildir etc.
 * If fs_or_nil is given it will be filled accordingly */
FL FILE *mx_fs_open_any(char const *file, char const *oflags,
      enum mx_fs_open_state *fs_or_nil);

/* Create a temporary file in $TMPDIR, use namehint for its name (prefix
 * unless O_SUFFIX is set in the fs_oflags oflags, in which case namehint is an
 * extension that MUST be part of aka fit in the resulting filename, otherwise
 * tmp_open() will fail), and return a stdio FILE pointer with access oflags.
 * *fstcp_or_nil may only be non-NIL under certain asserted conditions:
 * - if O_REGISTER: it is fully filled in; whether the filename is actually
 *   useful depends on the chosen UNLINK mode
 * - else if O_HOLDSIGS: filename filled in, tmp_release() is callable,
 * - else O_UNLINK must not and O_REGISTER_UNLINK could be set (filename is
 *   filled in, tmp_release() is not callable.
 * In the latter two cases autorec memory storage will be created (on success).
 * One of O_WRONLY and O_RDWR must be set.  Implied: 0600,cloexec */
FL FILE *mx_fs_tmp_open(char const *namehint, u32 oflags,
      struct mx_fs_tmp_ctx **fstcp_or_nil);

/* If (O_REGISTER|)O_HOLDSIGS and a context pointer was set when calling
 * tmp_open(), then hold_all_sigs() had not been released yet.
 * Call this to first unlink(2) the temporary file and then release signals */
FL void mx_fs_tmp_release(struct mx_fs_tmp_ctx *fstcp);

/* oflags implied: cloexec (unless nocloexec), O_REGISTER */
FL FILE *mx_fs_fd_open(sz fd, char const *oflags, boole nocloexec);

/* */
FL void mx_fs_fd_cloexec_set(sz fd);

/* Close and unregister a FILE* opened with any of fs_open(), fs_open_any(),
 * fs_tmp_open() (with O_REGISTER) or fd_open() */
FL boole mx_fs_close(FILE *fp);

/* Create a pair of file descriptors piped together, and ensure the CLOEXEC
 * bit is set in both; no registration is performed */
FL boole mx_fs_pipe_cloexec(sz fd[2]);

/* Create a process to be communicated with via a pipe.
 * mode can be r, W (newfd1 must be set, maybe to CHILD_FD_PASS or
 * CHILD_FD_NULL) or w (newfd1 is implicitly CHILD_FD_PASS).
 * In CHILD_FD_PASS cases pipe_close() must be called with waiting enabled,
 * whic is asserted!  Note that child.h is NOT included.
 * env_addon may be NIL, otherwise it is expected to be a NIL terminated
 * array of "K=V" strings to be placed into the childs environment
 * TODO v15 hack: If cmd==(char*)-1 then shell is indeed expected to be a PTF
 * TODO v15 hack: :P that will be called from within the child process */
FL FILE *mx_fs_pipe_open(char const *cmd, char const *mode, char const *shell,
      char const **env_addon, int newfd1);

/* Takes a FILE* returned by pipe_open, and returns <0 if no process can be
 * found, 0 on success, and an errno on kill(2) failure */
FL s32 mx_fs_pipe_signal(FILE *fp, s32 sig);

/* Close fp, which has been opened by fs_pipe_open().
 * With dowait returns true only upon successful program exit.
 * In conjunction with CHILD_FD_PASS dowait is mandatory. */
FL boole mx_fs_pipe_close(FILE *fp, boole dowait);

/* Close all _O_REGISTERed files and pipes */
FL void mx_fs_close_all(void);

#include <su/code-ou.h>
#endif /* mx_FILE_STREAMS_H */
/* s-it-mode */
