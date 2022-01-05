/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Socket operations. TODO enum okay -> boole
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef mx_NET_SOCKET_H
#define mx_NET_SOCKET_H

#include <mx/nail.h>
#ifdef mx_HAVE_NET

#include <mx/url.h>

#define mx_HEADER
#include <su/code-in.h>
#endif

struct mx_socket;

#ifdef mx_HAVE_NET
/* Data associated with a socket */
struct mx_socket{
   int s_fd; /* file descriptor */
#ifdef mx_HAVE_TLS
   int s_use_tls; /* TLS is used */
# ifdef mx_HAVE_XTLS
   void *s_tls;  /* TLS object */
# endif
   char *s_tls_finger; /* Set to autorec! store for CPROTO_CERTINFO */
   char *s_tls_certificate; /* Ditto */
   char *s_tls_certchain; /* Ditto */
#endif
   char *s_wbuf; /* for buffered writes */
   int s_wbufsize; /* allocated size of s_buf */
   int s_wbufpos; /* position of first empty data byte */
   char *s_rbufptr; /* read pointer to s_rbuf */
   int s_rsz; /* size of last read in s_rbuf */
   char const *s_desc; /* description of error messages */
   void (*s_onclose)(void); /* execute on close */
   char s_rbuf[mx_LINESIZE + 1]; /* for buffered reads */
};

/* Note: immediately closes the socket again for CPROTO_CERTINFO */
EXPORT boole mx_socket_open(struct mx_socket *sp, struct mx_url *urlp);

/* */
EXPORT int mx_socket_close(struct mx_socket *sp);

/* Drop I/O buffers */
INLINE struct mx_socket *mx_socket_reset_read_buf(struct mx_socket *self){
   self->s_rbufptr = NIL;
   self->s_rsz = 0;
   return self;
}

INLINE struct mx_socket *mx_socket_reset_write_buf(struct mx_socket *self){
   self->s_wbufpos = 0;
   return self;
}

INLINE struct mx_socket *mx_socket_reset_io_buf(struct mx_socket *self){
   self = mx_socket_reset_read_buf(self);
   self = mx_socket_reset_write_buf(self);
   return self;
}

/* */
EXPORT enum okay mx_socket_write(struct mx_socket *sp, char const *data);
EXPORT enum okay mx_socket_write1(struct mx_socket *sp, char const *data,
      int sz, int use_buffer);

/* */
EXPORT int mx_socket_getline(char **line, uz *linesize, uz *linelen,
      struct mx_socket *sp  su_DVL_LOC_ARGS_DECL);
#ifdef su_HAVE_DVL_LOC_ARGS
# define mx_socket_getline(A,B,C,D) \
   mx_socket_getline(A, B, C, D  su_DVL_LOC_ARGS_INJ)
#endif

#include <su/code-ou.h>
#endif /* mx_HAVE_NET */
#endif /* mx_NET_SOCKET_H */
/* s-it-mode */
