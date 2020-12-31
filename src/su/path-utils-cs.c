/*@ Implementation of path.h, string utils. XXX Nicer single x_posix wrapper?!
 *
 * Copyright (c) 2021 - 2023 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE su_path_utils_cs
#define su_SOURCE
#define su_SOURCE_PATH_UTILS_CS

#include "su/code.h"

#include "su/cs.h"

#include "su/path.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

NSPC_USE(su)

#if su_OS_POSIX
static char const a_pucs_root2[3] = "//";
#endif

char const su_path_sep[2] = {su_PATH_SEP_C, '\0'};
char const su_path_list_sep[2] = {su_PATH_LIST_SEP_C, '\0'};

char const su_path_current[sizeof su_PATH_CURRENT] = su_PATH_CURRENT;
char const su_path_null[sizeof su_PATH_NULL] = su_PATH_NULL;
char const su_path_root[sizeof su_PATH_ROOT] = su_PATH_ROOT;

char const *
su_path_basename(char *path){
	char const *rv;
	NYD_IN;
	ASSERT_NYD_EXEC(path != NIL, rv = NIL);

	for(rv = path; *rv != '\0'; ++rv){
	}

	if(rv == path){
		rv = su_path_current;
		goto jleave;
	}

	while(*--rv == su_PATH_SEP_C){
		if(rv == path){
#if su_OS_POSIX
			if(rv[2] == '\0')
				rv = a_pucs_root2;
			else
#endif
				rv = su_path_root;
			goto jleave;
		}
	}

	UNCONST(char*,rv)[1] = '\0';

	for(;; --rv){
		if(*rv == su_PATH_SEP_C){
			++rv;
			break;
		}
		if(rv == path)
			goto jleave;
	}

jleave:
	NYD_OU;
	return rv;
}

char const *
su_path_dirname(char *path){
	char const *rv;
	NYD_IN;
	ASSERT_NYD_EXEC(path != NIL, rv = NIL);

	for(rv = path; *rv != '\0'; ++rv){
	}

	if(rv == path){
		rv = su_path_current;
		goto jleave;
	}

	while(*--rv == su_PATH_SEP_C){
		if(rv == path){
#if su_OS_POSIX
			if(rv[2] == '\0')
				rv = a_pucs_root2;
			else
#endif
				rv = su_path_root;
			goto jleave;
		}
	}

	for(;;){
		if(rv == path){
			rv = su_path_current;
			goto jleave;
		}
		if(*--rv == su_PATH_SEP_C)
			break;
	}

	for(;;){
		if(rv == path){
			rv = su_path_root;
			goto jleave;
		}
		if(*--rv != su_PATH_SEP_C)
			break;
	}

	UNCONST(char*,rv)[1] = '\0';
	rv = path;
jleave:
	NYD_OU;
	return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef su_SOURCE
#undef su_SOURCE_PATH_UTILS_CS
/* s-itt-mode */
