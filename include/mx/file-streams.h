/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ File- and pipe streams, as well as temporary file creation.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

#define mx_HEADER
#include <su/code-in.h>

struct mx_fs_tmp_ctx;

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
   /* The name hint given to tmp_open() must be a mandatory member of the
    * result string as a whole.  Also, the random characters are to be placed
    * before the name hint, not after it */
   mx_FS_O_SUFFIX = 1u<<11
};

enum mx_fs_open_state{ /* TODO add mx_fs_open_mode, too */
   /* Lower bits are in fact enum protocol! */
   mx_FS_OPEN_STATE_NONE = 0,
   mx_FS_OPEN_STATE_EXISTS = 1u<<5
};
MCTA(n_PROTO_MASK < mx_FS_OPEN_STATE_EXISTS, "Bit carrier ranges overlap")

/* fs_tmp_open(): tdir_or_nil specials (with _TMP guaranteed to be NIL) */
#define mx_FS_TMP_TDIR_TMP (NIL)
/*#define mx_FS_TMP_TDIR_PWD su_R(char const*,-2) XXX reuse c_cwd() code! */

/* Note: actually publicly visible part of larger internal struct */
struct mx_fs_tmp_ctx{
   char const *fstc_filename;
};

/* */
#ifdef O_CLOEXEC
# define mx_FS_FD_CLOEXEC_SET(FD) (1)
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
EXPORT FILE *mx_fs_open(char const *file, char const *oflags);

/* TODO: Should be Mailbox::create_from_url(URL::from_string(DATA))!
 * Open file according to oflags (& prefix disallowed)m and register it
 * (leading ampersand & to suppress this is disallowed).
 * Handles compressed files, maildir etc.
 * If fs_or_nil is given it will be filled accordingly */
EXPORT FILE *mx_fs_open_any(char const *file, char const *oflags,
      enum mx_fs_open_state *fs_or_nil);

/* Create a temporary file in tdir_or_nil, use namehint_or_nil for its name
 * (prefix unless O_SUFFIX is set in the fs_oflags oflags and
 * *namehint_or_nil!=NUL), and return a stdio FILE pointer with access oflags.
 * tdir_or_nil can be a mx_FS_TMPTDIR_* constant (mx_FS_TMP_TDIR_TMP is NIL).
 * *fstcp_or_nil may only be non-NIL under certain asserted conditions:
 * - if O_REGISTER: it is fully filled in; whether the filename is actually
 *   useful depends on the chosen UNLINK mode.
 * - Else if O_HOLDSIGS: filename filled in, tmp_release() is callable,
 * - else O_UNLINK must not and O_REGISTER_UNLINK could be set (filename is
 *   filled in, tmp_release() is not callable.
 * In the latter two cases autorec memory storage will be created (on success).
 * One of O_WRONLY and O_RDWR must be set.  Implied: 0600,cloexec */
EXPORT FILE *mx_fs_tmp_open(char const *tdir_or_nil,
      char const *namehint_or_nil, u32 oflags,
      struct mx_fs_tmp_ctx **fstcp_or_nil);

/* If (O_REGISTER|)O_HOLDSIGS and a context pointer was set when calling
 * tmp_open(), then sigs_all_*() had not been released yet.
 * Call this to first unlink(2) the temporary file and then release signals */
EXPORT void mx_fs_tmp_release(struct mx_fs_tmp_ctx *fstcp);

/* oflags implied: cloexec (unless nocloexec), O_REGISTER */
EXPORT FILE *mx_fs_fd_open(sz fd, char const *oflags, boole nocloexec);

/* */
EXPORT boole mx_fs_fd_cloexec_set(sz fd);

/* Close and unregister a FILE* opened with any of fs_open(), fs_open_any(),
 * fs_tmp_open() (with O_REGISTER) or fd_open() */
EXPORT boole mx_fs_close(FILE *fp);

/* Create a pair of file descriptors piped together, and ensure the CLOEXEC
 * bit is set in both; no registration is performed */
EXPORT boole mx_fs_pipe_cloexec(sz fd[2]);

/* Create a process to be communicated with via a pipe.
 * mode can be r, W (newfd1 must be set, maybe to CHILD_FD_PASS or
 * CHILD_FD_NULL) or w (newfd1 is implicitly CHILD_FD_PASS).
 * In CHILD_FD_PASS cases pipe_close() must be called with waiting enabled,
 * which is asserted!  Note that child.h is NOT included.
 * env_addon may be NIL, otherwise it is expected to be a NIL terminated
 * array of "K=V" strings to be placed into the children's environment
 * TODO v15 hack: If cmd==(char*)-1 then shell is indeed expected to be a PTF
 * TODO v15 hack: :P that will be called from within the child process */
EXPORT FILE *mx_fs_pipe_open(char const *cmd, char const *mode,
      char const *shell, char const **env_addon, int newfd1);

/* Takes a FILE* returned by pipe_open, and returns <0 if no process can be
 * found, 0 on success, and an errno on kill(2) failure */
EXPORT s32 mx_fs_pipe_signal(FILE *fp, s32 sig);

/* Close fp, which has been opened by fs_pipe_open().
 * With dowait returns true only upon successful program exit.
 * In conjunction with CHILD_FD_PASS dowait is mandatory. */
EXPORT boole mx_fs_pipe_close(FILE *fp, boole dowait);

/* Flush the given or (if NIL) all streams */
EXPORT boole mx_fs_flush(FILE *fp);

/* Close all files and pipes created without O_NOREGISTER */
EXPORT void mx_fs_close_all(void);

/* XXX Temporary (pre v15 I/O) line buffer "pool".
 * (Possibly) Get a line buffer, and release one to the pool, respectively.
 * _book() returns false for integer overflow, or if reallocation survives
 * su_STATE_ERR_NONMEM.
 * The last is driven by the mainloop to perform cleanups */
EXPORT void mx_fs_linepool_aquire(char **dp, uz *dsp);
EXPORT void mx_fs_linepool_release(char *dp, uz ds);
EXPORT boole mx_fs_linepool_book(char **dp, uz *dsp, uz len, uz toadd
      su_DBG_LOC_ARGS_DECL);
EXPORT void mx_fs_linepool_cleanup(boole completely);

#ifdef su_HAVE_DBG_LOC_ARGS
# define mx_fs_linepool_book(A,B,C,D) \
   mx_fs_linepool_book(A, B, C, D  su_DBG_LOC_ARGS_INJ)
#endif

/* TODO The rest below is old-style (will vanish with I/O layer rewrite) */

/* fgets() replacement to handle lines of arbitrary size and with embedded \0
 * characters.
 * line - line buffer.  *line may be NIL.
 * linesize - allocated size of line buffer.
 * count - maximum characters to read.  May be NIL.
 * llen_or_nil - length_of_line(*line); set to 0 on entry if set.
 * fp - input FILE.
 * appendnl - always terminate line with \n, append if necessary.
 * Manages the n_PS_READLINE_NL hack */
EXPORT char *fgetline(char **line, uz *linesize, uz *count, uz *llen_or_nil,
      FILE *fp, int appendnl  su_DBG_LOC_ARGS_DECL);
#ifdef su_HAVE_DBG_LOC_ARGS
# define fgetline(A,B,C,D,E,F)   \
   fgetline(A, B, C, D, E, F  su_DBG_LOC_ARGS_INJ)
#endif

/* Read up a line from the specified input into the linebuffer.
 * Return the number of characters read.  Do not include the newline at EOL.
 * n is the number of characters already read and present in *linebuf; it'll be
 * treated as _the_ line if no more (successful) reads are possible.
 * Manages the n_PS_READLINE_NL hack */
EXPORT int readline_restart(FILE *ibuf, char **linebuf, uz *linesize, uz n
      su_DBG_LOC_ARGS_DECL);
#ifdef su_HAVE_DBG_LOC_ARGS
# define readline_restart(A,B,C,D) \
   readline_restart(A, B, C, D  su_DBG_LOC_ARGS_INJ)
#endif

/* Determine the size of the file possessed by the passed buffer */
EXPORT off_t fsize(FILE *iob);

#include <su/code-ou.h>
#endif /* mx_FILE_STREAMS_H */
/* s-it-mode */
