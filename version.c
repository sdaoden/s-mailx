/*	$Id: version.c,v 1.2 2000/03/21 03:12:24 gunnar Exp $
/*	OpenBSD: version.c,v 1.4 1996/06/08 19:48:46 christos Exp 	*/
/*	NetBSD: version.c,v 1.4 1996/06/08 19:48:46 christos Exp 	*/

/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef lint
#if 0
static char sccsid[] __attribute__ ((unused)) = "@(#)version.c	8.1 (Berkeley) 6/6/93";
#elif 0
static char rcsid[] __attribute__ ((unused)) = "OpenBSD: version.c,v 1.4 1996/06/08 19:48:46 christos Exp";
#else
static char rcsid[] __attribute__ ((unused)) = "@(#)$Id: version.c,v 1.2 2000/03/21 03:12:24 gunnar Exp $";
#endif
#endif /* not lint */

/*
 * Just keep track of the date/sid of this version of Mail.
 * Load this file first to get a "total" Mail version.
 */
#if 0
char	*version = "8.1 6/6/93";
#else
const char *version = "nail 9.0 2000-03-20";
#endif
