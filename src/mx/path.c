/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Path and directory related operations.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
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
#define su_FILE path
#define mx_SOURCE
#define mx_SOURCE_PATH

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#ifdef mx_HAVE_FCHDIR
FL enum okay
cwget(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if ((cw->cw_fd = open(".", O_RDONLY)) == -1)
      goto jleave;
   if (fchdir(cw->cw_fd) == -1) {
      close(cw->cw_fd);
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_OU;
   return rv;
}

FL enum okay
cwret(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if (!fchdir(cw->cw_fd))
      rv = OKAY;
   NYD_OU;
   return rv;
}

FL void
cwrelse(struct cw *cw)
{
   NYD_IN;
   close(cw->cw_fd);
   NYD_OU;
}

#else /* !mx_HAVE_FCHDIR */
FL enum okay
cwget(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if (getcwd(cw->cw_wd, sizeof cw->cw_wd) != NULL && !chdir(cw->cw_wd))
      rv = OKAY;
   NYD_OU;
   return rv;
}

FL enum okay
cwret(struct cw *cw)
{
   enum okay rv = STOP;
   NYD_IN;

   if (!chdir(cw->cw_wd))
      rv = OKAY;
   NYD_OU;
   return rv;
}

FL void
cwrelse(struct cw *cw)
{
   NYD_IN;
   UNUSED(cw);
   NYD_OU;
}
#endif /* !mx_HAVE_FCHDIR */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_PATH
/* s-it-mode */
