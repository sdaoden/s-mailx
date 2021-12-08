/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ OpenSSL client implementation according to: John Viega, Matt Messier,
 *@ Pravir Chandra: Network Security with OpenSSL. Sebastopol, CA 2002.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-4-Clause TODO ISC
 */
/*
 * Copyright (c) 2002
 * Gunnar Ritter.  All rights reserved.
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
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#define su_FILE xtls
#define mx_SOURCE
#define mx_SOURCE_XTLS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_XTLS /* Shorthand for mx_HAVE_TLS==mx_TLS_IMPL{...} */
#include <sys/socket.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>

#ifdef mx_XTLS_HAVE_CONFIG
# include <openssl/conf.h>
#endif
#ifdef mx_XTLS_HAVE_SET_RESEED_DEFAULTS
# include <openssl/rand_drbg.h>
#endif

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
# include <dirent.h>
#endif

#include <su/cs.h>
#include <su/mem.h>

#if mx_HAVE_TLS != mx_TLS_IMPL_RESSL && !defined mx_XTLS_HAVE_RAND_FILE
# include <su/time.h>
#endif

#include "mx/compat.h"
#include "mx/cred-auth.h"
#include "mx/file-streams.h"
#include "mx/names.h"
#include "mx/net-socket.h"
#include "mx/random.h"
#include "mx/tty.h"
#include "mx/url.h"

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Compatibility shims which assume 0/-1 cannot really happen */
/* Always for _protocols #ifndef mx_XTLS_HAVE_CONF_CTX */
# ifndef SSL_OP_NO_SSLv2
#  define SSL_OP_NO_SSLv2 0
# endif
# ifndef SSL_OP_NO_SSLv3
#  define SSL_OP_NO_SSLv3 0
# endif
# ifndef SSL_OP_NO_TLSv1
#  define SSL_OP_NO_TLSv1 0
# endif
# ifndef SSL_OP_NO_TLSv1_1
#  define SSL_OP_NO_TLSv1_1 0
# endif
# ifndef SSL_OP_NO_TLSv1_2
#  define SSL_OP_NO_TLSv1_2 0
# endif
# ifndef SSL_OP_NO_TLSv1_3
#  define SSL_OP_NO_TLSv1_3 0
# endif
  /* SSL_CONF_CTX and _OP_NO_SSL_MASK were both introduced with 1.0.2!?! */
# ifndef SSL_OP_NO_SSL_MASK
#  define SSL_OP_NO_SSL_MASK \
   (SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |\
   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2 |\
   SSL_OP_NO_TLSv1_3)
# endif

# ifndef SSL2_VERSION
#  define SSL2_VERSION 0
# endif
# ifndef SSL3_VERSION
#  define SSL3_VERSION 0
# endif
# ifndef TLS1_VERSION
#  define TLS1_VERSION 0
# endif
# ifndef TLS1_1_VERSION
#  define TLS1_1_VERSION 0
# endif
# ifndef TLS1_2_VERSION
#  define TLS1_2_VERSION 0
# endif
# ifndef TLS1_3_VERSION
#  define TLS1_3_VERSION 0
# endif
/* #endif */

#ifdef mx_XTLS_HAVE_STACK_OF
# define a_XTLS_STACKOF(X) STACK_OF(X)
#else
# define a_XTLS_STACKOF(X) /*X*/STACK
#endif

#ifdef mx_XTLS_HAVE_RAND_FILE
# if OPENSSL_VERSION_NUMBER + 0 >= 0x0090581fL
#  define a_XTLS_RAND_LOAD_FILE_MAXBYTES -1
# else
#  define a_XTLS_RAND_LOAD_FILE_MAXBYTES 1024
# endif
#endif

/* More cute compatibility sighs */
#if mx_HAVE_XTLS >= 0x10100
# define a_xtls_X509_get_notBefore X509_get0_notBefore
# define a_xtls_X509_get_notAfter X509_get0_notAfter
# define a_xtls_SSL_get_verified_chain SSL_get0_verified_chain
#else
# define a_xtls_X509_get_notBefore X509_get_notBefore
# define a_xtls_X509_get_notAfter X509_get_notAfter
# define a_xtls_SSL_get_verified_chain SSL_get_peer_cert_chain
#endif

#if mx_HAVE_XTLS >= 0x30000
# define a_xtls_SSL_CTX_load_verify_file(CTXP,FILE) \
   SSL_CTX_load_verify_file(CTXP, FILE)
# define a_xtls_SSL_CTX_load_verify_dir(CTXP,DIR) \
   SSL_CTX_load_verify_dir(ctxp, ca_dir)

# define a_xtls_X509_STORE_load_file(STORE,FILE) \
   X509_STORE_load_file(STORE, FILE)
# define a_xtls_X509_STORE_load_path(STORE,PATH) \
   X509_STORE_load_path(STORE, PATH)

# define a_xtls_SSL_get_peer_certificate(TLSP) \
   SSL_get0_peer_certificate(TLSP)
# define a_xtls_SSL_get_peer_certificate__FREE(CERT)

#else
# define a_xtls_SSL_CTX_load_verify_file(CTXP,FILE) \
   SSL_CTX_load_verify_locations(CTXP, FILE, NIL)
# define a_xtls_SSL_CTX_load_verify_dir(CTXP,DIR) \
   SSL_CTX_load_verify_locations(CTXP, NIL, DIR)

# define a_xtls_X509_STORE_load_file(STORE,FILE) \
   X509_STORE_load_locations(STORE, FILE, NIL)
# define a_xtls_X509_STORE_load_path(STORE,PATH) \
   X509_STORE_load_locations(STORE, NIL, PATH)

# define a_xtls_SSL_get_peer_certificate(TLSP) \
   SSL_get_peer_certificate(TLSP)
# define a_xtls_SSL_get_peer_certificate__FREE(CERT) \
   X509_free(CERT)
#endif /* mx_HAVE_XTLS >= 0x30000 */

/* X509_STORE_set_flags */
#undef a_XTLS_X509_V_ANY
#ifndef X509_V_FLAG_NO_ALT_CHAINS
# define X509_V_FLAG_NO_ALT_CHAINS -1
#else
# undef a_XTLS_X509_V_ANY
# define a_XTLS_X509_V_ANY
#endif
#ifndef X509_V_FLAG_NO_CHECK_TIME
# define X509_V_FLAG_NO_CHECK_TIME -1
#else
# undef a_XTLS_X509_V_ANY
# define a_XTLS_X509_V_ANY
#endif
#ifndef X509_V_FLAG_PARTIAL_CHAIN
# define X509_V_FLAG_PARTIAL_CHAIN -1
#else
# undef a_XTLS_X509_V_ANY
# define a_XTLS_X509_V_ANY
#endif
#ifndef X509_V_FLAG_X509_STRICT
# define X509_V_FLAG_X509_STRICT -1
#else
# undef a_XTLS_X509_V_ANY
# define a_XTLS_X509_V_ANY
#endif
#ifndef X509_V_FLAG_TRUSTED_FIRST
# define X509_V_FLAG_TRUSTED_FIRST -1
#else
# undef a_XTLS_X509_V_ANY
# define a_XTLS_X509_V_ANY
#endif

enum a_xtls_state{
   a_XTLS_S_INIT = 1u<<0,
   a_XTLS_S_RAND_DRBG_INIT = 1u<<1,
   a_XTLS_S_RAND_INIT = 1u<<2,
   a_XTLS_S_CONF_LOAD = 1u<<3,

#if mx_HAVE_XTLS < 0x10100
   a_XTLS_S_EXIT_HDL = 1u<<8,
   a_XTLS_S_ALGO_LOAD = 1u<<9,
#endif

   a_XTLS_S_VERIFY_ERROR = 1u<<16
};

struct ssl_method { /* TODO v15 obsolete */
   char const  sm_name[8];
   char const  sm_map[16];
};

struct a_xtls_protocol{
   char const xp_name[8];
   sl xp_op_no; /* SSL_OP_NO_* bit */
   u16 xp_version; /* *_VERSION number */
   boole xp_ok_minmaxproto; /* Valid for {Min,Max}Protocol= */
   boole xp_ok_proto; /* Valid for Protocol= */
   boole xp_last;
   boole xp_is_all; /* The special "ALL" */
   u8 xp__dummy[2];
};

struct a_xtls_cipher{
   char const xc_name[8];
   EVP_CIPHER const *(*xc_fun)(void);
};

struct a_xtls_digest{
   char const xd_name[16];
   EVP_MD const *(*xd_fun)(void);
};

struct a_xtls_x509_v_flags{
   char const xxvf_name[20];
   s32 xxvf_flag;
};

/* Supported SSL/TLS methods: update manual on change! */
static struct ssl_method const _ssl_methods[] = { /* TODO obsolete */
   {"auto",    "ALL,-SSLv2"},
   {"ssl3",    "-ALL,SSLv3"},
   {"tls1",    "-ALL,TLSv1"},
   {"tls1.1",  "-ALL,TLSv1.1"},
   {"tls1.2",  "-ALL,TLSv1.2"}
};

/* Update manual on change!
 * Ensure array size by adding \0 to longest entry.
 * Strictly to be sorted new/up to old/down, [0]=ALL, [x-1]=None! */
static struct a_xtls_protocol const a_xtls_protocols[] = {
   {"ALL", SSL_OP_NO_SSL_MASK, 0, FAL0, TRU1, FAL0, TRU1, {0}},
   {"TLSv1.3\0", SSL_OP_NO_TLSv1_3, TLS1_3_VERSION, TRU1,TRU1,FAL0,FAL0,{0}},
   {"TLSv1.2", SSL_OP_NO_TLSv1_2, TLS1_2_VERSION, TRU1, TRU1, FAL0, FAL0, {0}},
   {"TLSv1.1", SSL_OP_NO_TLSv1_1, TLS1_1_VERSION, TRU1, TRU1, FAL0, FAL0, {0}},
   {"TLSv1", SSL_OP_NO_TLSv1, TLS1_VERSION, TRU1, TRU1, FAL0, FAL0, {0}},
   {"SSLv3", SSL_OP_NO_SSLv3, SSL3_VERSION, TRU1, TRU1, FAL0, FAL0, {0}},
   {"SSLv2", SSL_OP_NO_SSLv2, SSL2_VERSION, TRU1, TRU1, FAL0, FAL0, {0}},
   {"None", SSL_OP_NO_SSL_MASK, 0, TRU1, FAL0, TRU1, FAL0, {0}}
};

/* Supported S/MIME cipher algorithms */
static struct a_xtls_cipher const a_xtls_ciphers[] = { /*Manual!*/
#ifndef OPENSSL_NO_AES
# define a_XTLS_SMIME_DEFAULT_CIPHER EVP_aes_128_cbc /* According RFC 5751 */
   {"AES128", &EVP_aes_128_cbc},
   {"AES256", &EVP_aes_256_cbc},
   {"AES192", &EVP_aes_192_cbc},
#endif
#ifndef OPENSSL_NO_DES
# ifndef a_XTLS_SMIME_DEFAULT_CIPHER
#  define a_XTLS_SMIME_DEFAULT_CIPHER EVP_des_ede3_cbc
# endif
   {"DES3", &EVP_des_ede3_cbc},
   {"DES", &EVP_des_cbc},
#endif
};
#ifndef a_XTLS_SMIME_DEFAULT_CIPHER
# error Your OpenSSL library does not include the necessary
# error cipher algorithms that are required to support S/MIME
#endif

#ifndef OPENSSL_NO_AES
/* TODO obsolete a_xtls_smime_ciphers_obs */
static struct a_xtls_cipher const a_xtls_smime_ciphers_obs[] = {
   {"AES-128", &EVP_aes_128_cbc},
   {"AES-256", &EVP_aes_256_cbc},
   {"AES-192", &EVP_aes_192_cbc}
};
#endif

/* Supported S/MIME message digest algorithms.
 * Update manual on default changes! */
static struct a_xtls_digest const a_xtls_digests[] = { /*Manual!*/
#ifdef mx_XTLS_HAVE_BLAKE2
   {"BLAKE2b512\0", &EVP_blake2b512},
   {"BLAKE2s256", &EVP_blake2s256},
# ifndef a_XTLS_FINGERPRINT_DEFAULT_DIGEST
#  define a_XTLS_FINGERPRINT_DEFAULT_DIGEST EVP_blake2s256
#  define a_XTLS_FINGERPRINT_DEFAULT_DIGEST_S "BLAKE2s256"
# endif
#endif

#ifdef mx_XTLS_HAVE_SHA3
   {"SHA3-512\0", &EVP_sha3_512},
   {"SHA3-384", &EVP_sha3_384},
   {"SHA3-256", &EVP_sha3_256},
   {"SHA3-224", &EVP_sha3_224},
#endif

#ifndef OPENSSL_NO_SHA512
   {"SHA512\0", &EVP_sha512},
   {"SHA384", &EVP_sha384},
# ifndef a_XTLS_SMIME_DEFAULT_DIGEST
#  define a_XTLS_SMIME_DEFAULT_DIGEST EVP_sha512
#  define a_XTLS_SMIME_DEFAULT_DIGEST_S "SHA512"
# endif
#endif

#ifndef OPENSSL_NO_SHA256
   {"SHA256\0", &EVP_sha256},
   {"SHA224", &EVP_sha224},
# ifndef a_XTLS_SMIME_DEFAULT_DIGEST
#  define a_XTLS_SMIME_DEFAULT_DIGEST EVP_sha256
#  define a_XTLS_SMIME_DEFAULT_DIGEST_S "SHA256"
# endif
# ifndef a_XTLS_FINGERPRINT_DEFAULT_DIGEST
#  define a_XTLS_FINGERPRINT_DEFAULT_DIGEST EVP_sha256
#  define a_XTLS_FINGERPRINT_DEFAULT_DIGEST_S "SHA256"
# endif
#endif

#ifndef OPENSSL_NO_SHA
   {"SHA1\0", &EVP_sha1},
# ifndef a_XTLS_SMIME_DEFAULT_DIGEST
#  define a_XTLS_SMIME_DEFAULT_DIGEST EVP_sha1
#  define a_XTLS_SMIME_DEFAULT_DIGEST_S "SHA1"
# endif
# ifndef a_XTLS_FINGERPRINT_DEFAULT_DIGEST
#  define a_XTLS_FINGERPRINT_DEFAULT_DIGEST EVP_sha1
#  define a_XTLS_FINGERPRINT_DEFAULT_DIGEST_S "SHA1"
# endif
#endif

#ifndef OPENSSL_NO_MD5
   {"MD5\0", &EVP_md5},
#endif
};

#if !defined a_XTLS_SMIME_DEFAULT_DIGEST || \
      !defined a_XTLS_FINGERPRINT_DEFAULT_DIGEST
# error Not enough supported message digest algorithms available
#endif

/* X509_STORE_set_flags() for *{smime,ssl}-ca-flags* */
static struct a_xtls_x509_v_flags const a_xtls_x509_v_flags[] = { /* Manual! */
   {"no-alt-chains", X509_V_FLAG_NO_ALT_CHAINS},
   {"no-check-time", X509_V_FLAG_NO_CHECK_TIME},
   {"partial-chain", X509_V_FLAG_PARTIAL_CHAIN},
   {"strict", X509_V_FLAG_X509_STRICT},
   {"trusted-first", X509_V_FLAG_TRUSTED_FIRST},
};

static uz a_xtls_state;
static uz a_xtls_msgno;

/* Special pre-PRNG PRNG init */
#ifdef mx_XTLS_HAVE_SET_RESEED_DEFAULTS
SINLINE void a_xtls_rand_drbg_init(void);
#else
# define a_xtls_rand_drbg_init() \
   do {a_xtls_state |= a_XTLS_S_RAND_DRBG_INIT;} while(0)
#endif

/* PRNG init */
#ifdef mx_XTLS_HAVE_RAND_FILE
static void a_xtls_rand_init(void);
#else
# define a_xtls_rand_init() \
   do {a_xtls_state |= a_XTLS_S_RAND_INIT;} while(0)
#endif

/* Library init */
static void a_xtls_init(void);

#if mx_HAVE_XTLS < 0x10100
# ifdef mx_HAVE_TLS_ALL_ALGORITHMS
static void a_xtls__load_algos(void);
#  define a_xtls_load_algos a_xtls__load_algos
# endif
# if defined mx_XTLS_HAVE_CONFIG || defined mx_HAVE_TLS_ALL_ALGORITHMS
static void a_xtls__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags);
# endif
#endif
#ifndef a_xtls_load_algos
# define a_xtls_load_algos() do{;}while(0)
#endif

static boole a_xtls_parse_asn1_time(ASN1_TIME const *atp,
               char *bdat, uz blen);
static int a_xtls_verify_cb(int success, X509_STORE_CTX *store);

static boole a_xtls_digest_find(char const *name, EVP_MD const **mdp,
               char const **normalized_name_or_null);

/* *smime-ca-flags*, *tls-ca-flags* */
static void a_xtls_ca_flags(X509_STORE *store, char const *flags);

/* SSL_CTX configuration; the latter always NULLs *confp */
static void *a_xtls_conf_setup(SSL_CTX *ctxp, struct mx_url const *urlp);
static boole a_xtls_conf(void *confp, char const *cmd, char const *value);
static boole a_xtls_conf_finish(void **confp, boole error);

static boole a_xtls_obsolete_conf_vars(void *confp, struct mx_url const *urlp);
static boole a_xtls_config_pairs(void *confp, struct mx_url const *urlp);
static boole a_xtls_load_verifications(SSL_CTX *ctxp,
      struct mx_url const *urlp);

static boole a_xtls_check_host(struct mx_socket *sop, X509 *peercert,
      struct mx_url const *urlp);

static int        smime_verify(struct message *m, int n,
                     a_XTLS_STACKOF(X509) *chain, X509_STORE *store);
static EVP_CIPHER const * _smime_cipher(char const *name);
static int        ssl_password_cb(char *buf, int size, int rwflag,
                     void *userdata);
static FILE *     smime_sign_cert(char const *xname, char const *xname2,
                     boole dowarn, char const **match, boole fallback_from);
static char const * _smime_sign_include_certs(char const *name);
static boole     _smime_sign_include_chain_creat(a_XTLS_STACKOF(X509) **chain,
                     char const *cfiles, char const *addr);
static EVP_MD const *a_xtls_smime_sign_digest(char const *name,
                        char const **digname);
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay  load_crl1(X509_STORE *store, char const *name);
#endif
static enum okay  load_crls(X509_STORE *store, enum okeys fok, enum okeys dok);

#ifdef mx_XTLS_HAVE_SET_RESEED_DEFAULTS
SINLINE void
a_xtls_rand_drbg_init(void){
   (void)RAND_DRBG_set_reseed_defaults(0, 0, 0, 0); /* (does not fail here) */
   a_xtls_state |= a_XTLS_S_RAND_DRBG_INIT;
}
#endif

#ifdef mx_XTLS_HAVE_RAND_FILE
static void
a_xtls_rand_init(void){
# define a_XTLS_RAND_ENTROPY 32
   char b64buf[a_XTLS_RAND_ENTROPY * 5 +1], *randfile;
   char const *cp, *x;
   boole err;
   NYD2_IN;

   a_xtls_rand_drbg_init();
   a_xtls_state |= a_XTLS_S_RAND_INIT;

# ifdef mx_XTLS_HAVE_CONFIG
   if(!(a_xtls_state & a_XTLS_S_INIT))
      a_xtls_init();
# endif

   err = TRU1;
   randfile = NIL;

   /* Prefer possible user setting */
   if((cp = ok_vlook(tls_rand_file)) != NIL ||
         (cp = ok_vlook(ssl_rand_file)) != NIL){
      x = NIL;
      if(*cp != '\0'){
         if((x = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
               ) == NIL)
            n_err(_("*tls-rand-file*: expansion of %s failed "
                  "(using default)\n"),
               n_shexp_quote_cp(cp, FAL0));
      }
      cp = x;
   }

   if(cp == NIL){
      randfile = su_LOFI_ALLOC(PATH_MAX);
      if((cp = RAND_file_name(randfile, PATH_MAX)) == NIL){
         n_err(_("*tls-rand-file*: no TLS entropy file, cannot seed PRNG\n"));
         goto jleave;
      }
   }

   (void)RAND_load_file(cp, a_XTLS_RAND_LOAD_FILE_MAXBYTES);

   /* And feed in some data, then write the updated file.
    * While this feeds the PRNG with itself, let's stir the buffer a bit.
    * Estimate a low but likely still too high number of entropy bytes, use
    * 20%: base64 uses 3 input = 4 output bytes relation, and the base64
    * alphabet is a 6 bit one */
   for(x = R(char*,-1);;){
      RAND_add(mx_random_create_buf(b64buf, sizeof(b64buf) -1, NIL),
         sizeof(b64buf) -1, a_XTLS_RAND_ENTROPY);
      if((x = R(char*,R(up,x) >> 3)) == NIL){
         err = (RAND_status() == 0);
         break;
      }
      if(!(err = (RAND_status() == 0)))
         break;
   }

   if(!err)
      err = (RAND_write_file(cp) == -1);

jleave:
   if(randfile != NIL)
      su_LOFI_FREE(randfile);
   if(err)
      n_panic(_("Cannot seed the PseudoRandomNumberGenerator, "
            "RAND_status() is 0!\n"
         "  Please set *tls-rand-file* to a file with sufficient entropy.\n"
         "  On a machine with entropy: "
            "\"$ dd if=/dev/urandom of=FILE bs=1024 count=1\"\n"));
   NYD2_OU;
}
#endif /* mx_XTLS_HAVE_RAND_FILE */

static void
a_xtls_init(void){
#ifdef mx_XTLS_HAVE_CONFIG
   char const *cp;
#endif
   NYD2_IN;

   if(a_xtls_state & a_XTLS_S_INIT)
      goto jleave;

#if mx_HAVE_XTLS >= 0x10100
   OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
      OPENSSL_INIT_LOAD_CRYPTO_STRINGS
# ifdef mx_HAVE_TLS_ALL_ALGORITHMS
         | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS
# endif
      , NULL);
#else
   SSL_load_error_strings();
   SSL_library_init();
   a_xtls_load_algos();
#endif
   a_xtls_state |= a_XTLS_S_INIT;

   a_xtls_rand_drbg_init();

   /* Load openssl.cnf or whatever was given in *tls-config-file* */
#ifdef mx_XTLS_HAVE_CONFIG
   if((cp = ok_vlook(tls_config_file)) != NULL ||
         (cp = ok_vlook(ssl_config_file)) != NULL){
      char const *msg;
      ul flags;

      if(*cp == '\0'){
         msg = "[default]";
         cp = NULL;
         flags = CONF_MFLAGS_IGNORE_MISSING_FILE;
      }else if((msg = cp, cp = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL))) != NIL)
         flags = 0;
      else{
         n_err(_("*tls-config-file*: file expansion failed: %s\n"),
            n_shexp_quote_cp(msg, FAL0));
         goto jefile;
      }

      if(CONF_modules_load_file(cp, n_uagent, flags) == 1){
         a_xtls_state |= a_XTLS_S_CONF_LOAD;
# if mx_HAVE_XTLS < 0x10100
         if(!(a_xtls_state & a_XTLS_S_EXIT_HDL)){
            a_xtls_state |= a_XTLS_S_EXIT_HDL;
            su_state_on_gut_install(&a_xtls__on_gut, FAL0, su_STATE_ERR_PASS);
         }
# endif
         if(n_poption & n_PO_D_V)
            n_err(_("Loaded TLS configuration for %s from %s\n"), n_uagent,
               n_shexp_quote_cp(msg, FAL0));
jefile:;
      }else
         ssl_gen_err(_("TLS CONF_modules_load_file() load error"));
   }
#endif /* mx_XTLS_HAVE_CONFIG */

   if(!(a_xtls_state & a_XTLS_S_RAND_INIT))
      a_xtls_rand_init();

jleave:
   NYD2_OU;
}

#if mx_HAVE_XTLS < 0x10100
# ifdef mx_HAVE_TLS_ALL_ALGORITHMS
static void
a_xtls__load_algos(void){
   NYD2_IN;
   if(!(a_xtls_state & a_XTLS_S_ALGO_LOAD)){
      a_xtls_state |= a_XTLS_S_ALGO_LOAD;
      OpenSSL_add_all_algorithms();

      if(!(a_xtls_state & a_XTLS_S_EXIT_HDL)){
         a_xtls_state |= a_XTLS_S_EXIT_HDL;
         su_state_on_gut_install(&a_xtls__on_gut, FAL0, su_STATE_ERR_PASS);
      }
   }
   NYD2_OU;
}
# endif

# if defined mx_XTLS_HAVE_CONFIG || defined mx_HAVE_TLS_ALL_ALGORITHMS
static void
a_xtls__on_gut(BITENUM_IS(u32,su_state_gut_flags) flags){
   NYD2_IN;

   if((flags & su_STATE_GUT_ACT_MASK) == su_STATE_GUT_ACT_NORM){
#  ifdef mx_XTLS_HAVE_CONFIG
      if(a_xtls_state & a_XTLS_S_CONF_LOAD)
         CONF_modules_free();
#  endif

#  ifdef mx_HAVE_TLS_ALL_ALGORITHMS
      if(a_xtls_state & a_XTLS_S_ALGO_LOAD)
         EVP_cleanup();
#  endif
   }

   a_xtls_state = 0;

   NYD2_OU;
}
# endif
#endif /* mx_HAVE_XTLS < 0x10100 */

static boole
a_xtls_parse_asn1_time(ASN1_TIME const *atp, char *bdat, uz blen)
{
   BIO *mbp;
   char *mcp;
   long l;
   NYD_IN;

   mbp = BIO_new(BIO_s_mem());

   if (ASN1_TIME_print(mbp, C(ASN1_TIME*,atp)) &&
         (l = BIO_get_mem_data(mbp, &mcp)) > 0)
      snprintf(bdat, blen, "%.*s", (int)l, mcp);
   else {
      snprintf(bdat, blen, _("Bogus certificate date: %.*s"),
         /*is (int)*/atp->length, (char const*)atp->data);
      mcp = NULL;
   }

   BIO_free(mbp);
   NYD_OU;
   return (mcp != NULL);
}

static int
a_xtls_verify_cb(int success, X509_STORE_CTX *store)
{
   char data[256];
   X509 *cert;
   int rv = TRU1;
   NYD_IN;

   if (success && !(n_poption & n_PO_D_V))
      goto jleave;

   if (a_xtls_msgno != 0) {
      n_err(_("Message %lu:\n"), (ul)a_xtls_msgno);
      a_xtls_msgno = 0;
   }
   n_err(_(" Certificate depth %d %s\n"),
      X509_STORE_CTX_get_error_depth(store),
      (success ? su_empty : V_(n_error)));

   if ((cert = X509_STORE_CTX_get_current_cert(store)) != NULL) {
      X509_NAME_oneline(X509_get_subject_name(cert), data, sizeof data);
      n_err(_("  subject = %s\n"), data);

      a_xtls_parse_asn1_time(a_xtls_X509_get_notBefore(cert),
         data, sizeof data);
      n_err(_("  notBefore = %s\n"), data);

      a_xtls_parse_asn1_time(a_xtls_X509_get_notAfter(cert),
         data, sizeof data);
      n_err(_("  notAfter = %s\n"), data);

      X509_NAME_oneline(X509_get_issuer_name(cert), data, sizeof data);
      n_err(_("  issuer = %s\n"), data);
   }

   if (!success) {
      int err = X509_STORE_CTX_get_error(store);

      n_err(_("  err %i: %s\n"), err, X509_verify_cert_error_string(err));
      a_xtls_state |= a_XTLS_S_VERIFY_ERROR;
   }

   if(!success)
      rv = n_tls_verify_decide();
jleave:
   NYD_OU;
   return rv;
}

static boole
a_xtls_digest_find(char const *name,
      EVP_MD const **mdp, char const **normalized_name_or_null){
   uz i;
   char *nn;
   NYD2_IN;

   /* C99 */{
      char *cp, c;

      i = su_cs_len(name);
      nn = cp = n_lofi_alloc(i +1);
      while((c = *name++) != '\0')
         *cp++ = su_cs_to_upper(c);
      *cp = '\0';

      if(normalized_name_or_null != NULL)
         *normalized_name_or_null = savestrbuf(nn, P2UZ(cp - nn));
   }

   for(i = 0; i < NELEM(a_xtls_digests); ++i)
      if(!su_cs_cmp(a_xtls_digests[i].xd_name, nn)){
         *mdp = (*a_xtls_digests[i].xd_fun)();
         goto jleave;
      }

   /* Not a built-in algorithm, but we may have dynamic support for more */
#ifdef mx_HAVE_TLS_ALL_ALGORITHMS
   if((*mdp = EVP_get_digestbyname(nn)) != NULL)
      goto jleave;
#endif

   n_err(_("Invalid message digest: %s\n"), n_shexp_quote_cp(nn, FAL0));
   *mdp = NULL;
jleave:
   n_lofi_free(nn);

   NYD2_OU;
   return (*mdp != NULL);
}

static void
a_xtls_ca_flags(X509_STORE *store, char const *flags){
   NYD2_IN;
   if(flags != NULL){
      char *iolist, *cp;

      iolist = savestr(flags);
jouter:
      while((cp = su_cs_sep_c(&iolist, ',', TRU1)) != NULL){
         struct a_xtls_x509_v_flags const *xvfp;

         for(xvfp = &a_xtls_x509_v_flags[0];
               xvfp < &a_xtls_x509_v_flags[NELEM(a_xtls_x509_v_flags)];
               ++xvfp)
            if(!su_cs_cmp_case(cp, xvfp->xxvf_name)){
               if(xvfp->xxvf_flag != -1){
#ifdef a_XTLS_X509_V_ANY
                  X509_STORE_set_flags(store, xvfp->xxvf_flag);
#endif
               }else if(n_poption & n_PO_D_V)
                  n_err(_("*{smime,tls}-ca-flags*: "
                     "directive not supported: %s\n"), cp);
               goto jouter;
            }
         n_err(_("*{smime,tls}-ca-flags*: invalid directive: %s\n"), cp);
      }
   }
   NYD2_OU;
}

#ifdef mx_XTLS_HAVE_CONF_CTX
static void *
a_xtls_conf_setup(SSL_CTX *ctxp, struct mx_url const *urlp){
   char const *cp;
   SSL_CONF_CTX *sccp;
   NYD2_IN;

   sccp = NULL;

   if((cp = xok_vlook(tls_config_module, urlp, OXM_ALL)) != NULL ||
         (cp = xok_vlook(ssl_config_module, urlp, OXM_ALL)) != NULL){
# ifdef mx_XTLS_HAVE_CTX_CONFIG
      if(!(a_xtls_state & a_XTLS_S_CONF_LOAD)){
         n_err(_("*tls-config-module*: no *tls-config-file* loaded: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         goto jleave;
      }else if(!SSL_CTX_config(ctxp, cp)){
         ssl_gen_err(_("*tls-config-module*: load error for %s, section [%s]"),
               n_uagent, n_shexp_quote_cp(cp, FAL0));
         goto jleave;
      }
# else
      n_err(_("*tls-config-module*: set but not supported: %s\n"),
         n_shexp_quote_cp(cp, FAL0));
      goto jleave;
# endif
   }

   if((sccp = SSL_CONF_CTX_new()) != NULL){
      SSL_CONF_CTX_set_flags(sccp,
         SSL_CONF_FLAG_FILE | SSL_CONF_FLAG_CLIENT |
         SSL_CONF_FLAG_CERTIFICATE | SSL_CONF_FLAG_SHOW_ERRORS);

      SSL_CONF_CTX_set_ssl_ctx(sccp, ctxp);
   }else
      ssl_gen_err(_("SSL_CONF_CTX_new() failed"));
jleave:
   NYD2_OU;
   return sccp;
}

static boole
a_xtls_conf(void *confp, char const *cmd, char const *value){
   int rv;
   SSL_CONF_CTX *sccp;
   NYD2_IN;

   if(n_poption & n_PO_D_V)
      n_err(_("TLS: applying config: %s = %s\n"),
            n_shexp_quote_cp(cmd, FAL0), n_shexp_quote_cp(value, FAL0));

   rv = SSL_CONF_cmd(sccp = confp, cmd, value);
   if(rv == 2)
      rv = 0;
   else{
      cmd = n_shexp_quote_cp(cmd, FAL0);
      value = n_shexp_quote_cp(value, FAL0);
      if(rv == 0)
         ssl_gen_err(_("TLS: config failure: %s = %s"), cmd, value);
      else{
         char const *err;

         switch(rv){
         case -2: err = N_("TLS: config command not recognized"); break;
         case -3: err = N_("TLS: missing required config argument"); break;
         default: err = N_("TLS: unspecified config error"); break;
         }
         err = V_(err);
         n_err(_("%s (%d): %s = %s\n"), err, rv, cmd, value);
      }
      rv = 1;
   }
   NYD2_OU;
   return (rv == 0);
}

static boole
a_xtls_conf_finish(void **confp, boole error){
   SSL_CONF_CTX *sccp;
   boole rv;
   NYD2_IN;

   sccp = (SSL_CONF_CTX*)*confp;
   *confp = NULL;

   if(!(rv = error))
      rv = (SSL_CONF_CTX_finish(sccp) != 0);

   SSL_CONF_CTX_free(sccp);
   NYD2_OU;
   return rv;
}

#else /* mx_XTLS_HAVE_CONF_CTX */
# ifdef mx_XTLS_HAVE_CTX_CONFIG
#  error SSL_CTX_config(3) support unexpected without SSL_CONF_CTX support
# endif

static void *
a_xtls_conf_setup(SSL_CTX* ctxp, struct mx_url const *urlp){
   char const *cp;
   NYD2_IN;

   if((cp = xok_vlook(tls_config_module, urlp, OXM_ALL)) != NULL ||
         (cp = xok_vlook(ssl_config_module, urlp, OXM_ALL)) != NULL){
      n_err(_("*tls-config-module*: set but not supported: %s\n"),
         n_shexp_quote_cp(cp, FAL0));
      ctxp = NULL;
   }
   NYD2_OU;
   return ctxp;
}

static boole
a_xtls_conf(void *confp, char const *cmd, char const *value){
   char const *xcmd, *emsg;
   SSL_CTX *ctxp;
   NYD2_IN;

   if(n_poption & n_PO_D_V)
      n_err(_("TLS: applying config: %s = %s\n"),
            n_shexp_quote_cp(cmd, FAL0), n_shexp_quote_cp(value, FAL0));

   ctxp = confp;

   if(!su_cs_cmp_case(cmd, xcmd = "Certificate")){
      if(SSL_CTX_use_certificate_chain_file(ctxp, value) != 1){
         emsg = N_("TLS: %s: cannot load from file %s\n");
         goto jerr;
      }
   }else if(!su_cs_cmp_case(cmd, xcmd = "CipherString") ||
         !su_cs_cmp_case(cmd, xcmd = "CipherList")/* XXX bad bug in past! */){
      if(SSL_CTX_set_cipher_list(ctxp, value) != 1){
         emsg = N_("TLS: %s: invalid: %s\n");
         goto jerr;
      }
   }else if(!su_cs_cmp_case(cmd, xcmd = "Ciphersuites")){
# ifdef mx_XTLS_HAVE_SET_CIPHERSUITES
      if(SSL_CTX_set_ciphersuites(ctxp, value) != 1){
         emsg = N_("TLS: %s: invalid: %s\n");
         goto jerr;
      }
# else
      value = NULL;
      emsg = N_("TLS: %s: directive not supported\n");
      goto jxerr;
# endif
   }else if(!su_cs_cmp_case(cmd, xcmd = "Curves")){
# ifdef SSL_CTRL_SET_CURVES_LIST
      if(SSL_CTX_set1_curves_list(ctxp, n_UNCONST(value)) != 1){
         emsg = N_("TLS: %s: invalid: %s\n");
         goto jerr;
      }
# else
      value = NULL;
      emsg = N_("TLS: %s: directive not supported\n");
      goto jxerr;
# endif
   }else if((emsg = NULL, !su_cs_cmp_case(cmd, xcmd = "MaxProtocol")) ||
         (emsg = (char*)-1, !su_cs_cmp_case(cmd, xcmd = "MinProtocol"))){
# ifndef mx_XTLS_HAVE_SET_MIN_PROTO_VERSION
      value = NULL;
      emsg = N_("TLS: %s: directive not supported\n");
      goto jxerr;
# else
      struct a_xtls_protocol const *xpp;

      for(xpp = &a_xtls_protocols[1] /* [0] == ALL */;;)
         if(xpp->xp_ok_minmaxproto && !su_cs_cmp_case(value, xpp->xp_name)){
            if(xpp->xp_op_no == 0 || xpp->xp_version == 0)
               goto jenoproto;
            break;
         }else if((++xpp)->xp_last)
            goto jenoproto;

      if((emsg == NULL ? SSL_CTX_set_max_proto_version(ctxp, xpp->xp_version)
            : SSL_CTX_set_min_proto_version(ctxp, xpp->xp_version)) != 1){
         emsg = N_("TLS: %s: cannot set protocol version: %s\n");
         goto jerr;
      }
# endif /* !mx_XTLS_HAVE_SET_MIN_PROTO_VERSION */
   }else if(!su_cs_cmp_case(cmd, xcmd = "Options")){
      if(su_cs_cmp_case(value, "Bugs")){
         emsg = N_("TLS: %s: fallback only supports value \"Bugs\": %s\n");
         goto jxerr;
      }
      SSL_CTX_set_options(ctxp, SSL_OP_ALL);
   }else if(!su_cs_cmp_case(cmd, xcmd = "PrivateKey")){
      if(SSL_CTX_use_PrivateKey_file(ctxp, value, SSL_FILETYPE_PEM) != 1){
         emsg = N_("%s: cannot load from file %s\n");
         goto jerr;
      }
   }else if(!su_cs_cmp_case(cmd, xcmd = "Protocol")){
      struct a_xtls_protocol const *xpp;
      char *iolist, *cp, addin;
      sl opts;

      opts = 0;

      for(iolist = cp = savestr(value);
            (cp = su_cs_sep_c(&iolist, ',', FAL0)) != NULL;){
         if(*cp == '\0'){
            value = NULL;
            emsg = N_("TLS: %s: empty elements are not supported\n");
            goto jxerr;
         }

         addin = TRU1;
         switch(cp[0]){
         case '-': addin = FAL0; /* FALLTHRU */
         case '+': ++cp; /* FALLTHRU */
         default : break;
         }

         for(xpp = &a_xtls_protocols[0];;){
            if(xpp->xp_ok_proto && !su_cs_cmp_case(cp, xpp->xp_name)){
               if((xpp->xp_op_no == 0 || xpp->xp_version == 0) &&
                     !xpp->xp_is_all)
                  goto jenoproto;
               /* We need to inverse the meaning of the _NO_s */
               if(!addin)
                  opts |= xpp->xp_op_no;
               else
                  opts &= ~xpp->xp_op_no;
               break;
            }else if((++xpp)->xp_last){
jenoproto:
               emsg = N_("TLS: %s: unknown or unsupported protocol: %s\n");
               goto jxerr;
            }
         }
      }

      SSL_CTX_clear_options(ctxp, SSL_OP_NO_SSL_MASK);
      SSL_CTX_set_options(ctxp, opts);
   }else{
      xcmd = n_shexp_quote_cp(cmd, FAL0);
      emsg = N_("TLS: unsupported directive: %s: value: %s\n");
      goto jxerr;
   }

jleave:
   NYD2_OU;
   return (confp != NULL);
jerr:
   ssl_gen_err(V_(emsg), xcmd, n_shexp_quote_cp(value, FAL0));
   confp = NULL;
   goto jleave;
jxerr:
   if(value != NULL)
      value = n_shexp_quote_cp(value, FAL0);
   n_err(V_(emsg), xcmd, value);
   confp = NULL;
   goto jleave;
}

static boole
a_xtls_conf_finish(void **confp, boole error){
   UNUSED(confp);
   UNUSED(error);
   *confp = NULL;
   return TRU1;
}
#endif /* !mx_XTLS_HAVE_CONF_CTX */

static boole
a_xtls_obsolete_conf_vars(void *confp, struct mx_url const *urlp){
   char const *cp, *cp_base, *certchain;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   /* Certificate via ssl-cert */
   if((certchain = cp = xok_vlook(ssl_cert, urlp, OXM_ALL)) != NULL){
      n_OBSOLETE(_("please use *tls-config-pairs* instead of *ssl-cert*"));
      if((cp_base = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
            ) == NIL){
         n_err(_("*ssl-cert* value expansion failed: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         goto jleave;
      }
      if(!a_xtls_conf(confp, "Certificate", certchain = cp_base))
         goto jleave;
   }

   /* CipherString via ssl-ciper-list */
   if((cp = xok_vlook(ssl_cipher_list, urlp, OXM_ALL)) != NULL){
      n_OBSOLETE(_("please use *tls-config-pairs* instead of "
         "*ssl-cipher-list*"));
      if(!a_xtls_conf(confp, "CipherString", cp))
         goto jleave;
   }

   /* Curves via ssl-curves */
   if((cp = xok_vlook(ssl_curves, urlp, OXM_ALL)) != NULL){
      n_OBSOLETE(_("please use *tls-config-pairs* instead of *ssl-curves*"));
      if(!a_xtls_conf(confp, "Curves", cp))
         goto jleave;
   }

   /* PrivateKey via ssl-key */
   if((cp = xok_vlook(ssl_key, urlp, OXM_ALL)) != NULL){
      n_OBSOLETE(_("please use *tls-config-pairs* instead of *ssl-key*"));
      if((cp_base = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
            ) == NIL){
         n_err(_("*ssl-key* value expansion failed: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         goto jleave;
      }
      cp = cp_base;
      if(certchain == NULL){
         n_err(_("*ssl-key* can only be used together with *ssl-cert*! "
            "And use *ssl-config-pairs*!\n"));
         goto jleave;
      }
   }
   if((cp != NULL || (cp = certchain) != NULL) &&
         !a_xtls_conf(confp, "PrivateKey", cp))
      goto jleave;

   /* Protocol via ssl-method or ssl-protocol */
   if((cp = xok_vlook(ssl_method, urlp, OXM_ALL)) != NULL){
      uz i;

      n_OBSOLETE(_("please use *tls-config-pairs* instead of *ssl-method*"));
      for(i = 0;;){
         if(!su_cs_cmp_case(_ssl_methods[i].sm_name, cp)){
            cp = _ssl_methods[i].sm_map;
            break;
         }
         if(++i == NELEM(_ssl_methods)){
            n_err(_("Unsupported SSL method: %s\n"), cp);
            goto jleave;
         }
      }
   }
   if((cp_base = xok_vlook(ssl_protocol, urlp, OXM_ALL)) != NULL){
      n_OBSOLETE(_("please use *tls-config-pairs* instead of *ssl-protocol*"));
      if(cp != NULL && (n_poption & n_PO_D_V))
         n_err(_("*ssl-protocol* overrides *ssl-method*! "
            "And please use *tls-config-pairs* instead!\n"));
      cp = cp_base;
   }
   if(cp != NULL && !a_xtls_conf(confp, "Protocol", cp))
      goto jleave;

   rv = TRU1;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_xtls_config_pairs(void *confp, struct mx_url const *urlp){
   /* Due to interdependencies some commands have to be delayed a bit */
   static char const cmdcert[] = "Certificate", cmdprivkey[] = "PrivateKey";
   char const *valcert, *valprivkey;
   char *pairs, *cp, *cmd, *val;
   NYD2_IN;

   if((pairs = n_UNCONST(xok_vlook(tls_config_pairs, urlp, OXM_ALL))
         ) == NULL &&
         (pairs = n_UNCONST(xok_vlook(ssl_config_pairs, urlp, OXM_ALL))
         ) == NULL)
      goto jleave;
   pairs = savestr(pairs);

   valcert = valprivkey = NULL;

   while((cp = su_cs_sep_escable_c(&pairs, ',', FAL0)) != NULL){
      char c;
      enum{
         a_NONE,
         a_EXPAND = 1u<<0,
         a_CERT = 1u<<1,
         a_PRIVKEY = 1u<<2,
         a_EXPAND_MASK = a_EXPAND | a_CERT | a_PRIVKEY
      } f;

      /* Directive, space trimmed */
      if((cmd = su_cs_find_c(cp, '=')) == NULL){
jenocmd:
         if(pairs == NULL)
            pairs = n_UNCONST(n_empty);
         n_err(_("*tls-config-pairs*: missing directive: %s; rest: %s\n"),
            n_shexp_quote_cp(cp, FAL0), n_shexp_quote_cp(pairs, FAL0));
         goto jleave;
      }
      val = &cmd[1];

      if((cmd > cp && cmd[-1] == '*')){
         --cmd;
         f = a_EXPAND;
      }else
         f = a_NONE;
      while(cmd > cp && (c = cmd[-1], su_cs_is_space(c)))
         --cmd;
      if(cmd == cp)
         goto jenocmd;
      *cmd = '\0';
      cmd = cp;

      /* Command with special treatment? */
      if(!su_cs_cmp_case(cmd, cmdcert))
         f |= a_CERT;
      else if(!su_cs_cmp_case(cmd, cmdprivkey))
         f |= a_PRIVKEY;

      /* Value, space trimmed */
      while((c = *val) != '\0' && su_cs_is_space(c))
         ++val;
      cp = &val[su_cs_len(val)];
      while(cp > val && (c = cp[-1], su_cs_is_space(c)))
         --cp;
      *cp = '\0';
      if(cp == val){
         if(pairs == NULL)
            pairs = n_UNCONST(n_empty);
         n_err(_("*tls-config-pairs*: missing value: %s; rest: %s\n"),
            n_shexp_quote_cp(cmd, FAL0), n_shexp_quote_cp(pairs, FAL0));
         goto jleave;
      }

      /* Filename transformations to be applied? */
      if(f & a_EXPAND_MASK){
         if((cp = fexpand(val, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
               ) == NIL){
            if(pairs == NULL)
               pairs = n_UNCONST(n_empty);
            n_err(_("*tls-config-pairs*: value expansion failed: %s: %s; "
                  "rest: %s\n"),
               n_shexp_quote_cp(cmd, FAL0), n_shexp_quote_cp(val, FAL0),
               n_shexp_quote_cp(pairs, FAL0));
            goto jleave;
         }
         val = cp;
      }

      /* Some things have to be delayed */
      if(f & a_CERT)
         valcert = val;
      else if(f & a_PRIVKEY)
         valprivkey = val;
      else if(!a_xtls_conf(confp, cmd, val)){
         pairs = n_UNCONST(n_empty);
         goto jleave;
      }
   }

   /* Work the delayed ones */
   if((valcert != NULL && !a_xtls_conf(confp, cmdcert, valcert)) ||
         ((valprivkey != NULL || (valprivkey = valcert) != NULL) &&
          !a_xtls_conf(confp, cmdprivkey, valprivkey)))
      pairs = n_UNCONST(n_empty);

jleave:
   NYD2_OU;
   return (pairs == NULL);
}

static boole
a_xtls_load_verifications(SSL_CTX *ctxp, struct mx_url const *urlp){
   char *ca_dir, *ca_file;
   X509_STORE *store;
   boole rv;
   NYD2_IN;

   if(n_tls_verify_level == n_TLS_VERIFY_IGNORE){
      rv = TRU1;
      goto jleave;
   }
   rv = FAL0;

   if((ca_dir = xok_vlook(tls_ca_dir, urlp, OXM_ALL)) != NULL ||
         (ca_dir = xok_vlook(ssl_ca_dir, urlp, OXM_ALL)) != NULL)
      ca_dir = fexpand(ca_dir, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL));
   if((ca_file = xok_vlook(tls_ca_file, urlp, OXM_ALL)) != NULL ||
         (ca_file = xok_vlook(ssl_ca_file, urlp, OXM_ALL)) != NULL)
      ca_file = fexpand(ca_file, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL));

   if(ca_file != NIL && a_xtls_SSL_CTX_load_verify_file(ctxp, ca_file) != 1){
      ssl_gen_err(_("Error loading %s\n"), n_shexp_quote_cp(ca_file, FAL0));
      goto jleave;
   }

   if(ca_dir != NIL && a_xtls_SSL_CTX_load_verify_dir(ctxp, ca_dir) != 1){
      ssl_gen_err(_("Error loading %s\n"), n_shexp_quote_cp(ca_dir, FAL0));
      goto jleave;
   }

   /* C99 */{
      boole xv15;

      if((xv15 = ok_blook(ssl_no_default_ca)))
         n_OBSOLETE(_("please use *tls-ca-no-defaults*, "
            "not *ssl-no-default-ca*"));
      if(!xok_blook(tls_ca_no_defaults, urlp, OXM_ALL) &&
            !xok_blook(ssl_ca_no_defaults, urlp, OXM_ALL) && !xv15 &&
            SSL_CTX_set_default_verify_paths(ctxp) != 1) {
         ssl_gen_err(_("Error loading built-in default CA locations\n"));
         goto jleave;
      }
   }

   a_xtls_state &= ~a_XTLS_S_VERIFY_ERROR;
   a_xtls_msgno = 0;
   SSL_CTX_set_verify(ctxp, SSL_VERIFY_PEER, &a_xtls_verify_cb);
   store = SSL_CTX_get_cert_store(ctxp);
   load_crls(store, ok_v_tls_crl_file, ok_v_tls_crl_dir);
   a_xtls_ca_flags(store, xok_vlook(tls_ca_flags, urlp, OXM_ALL));
      a_xtls_ca_flags(store, xok_vlook(ssl_ca_flags, urlp, OXM_ALL));

   rv = TRU1;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_xtls_check_host(struct mx_socket *sop, X509 *peercert,
      struct mx_url const *urlp){
   char data[256];
   a_XTLS_STACKOF(GENERAL_NAME) *gens;
   GENERAL_NAME *gen;
   X509_NAME *subj;
   boole rv;
   NYD_IN;
   UNUSED(sop);

   rv = FAL0;

   if((gens = X509_get_ext_d2i(peercert, NID_subject_alt_name, NULL, NULL)
         ) != NULL){
      int i;

      for(i = 0; i < sk_GENERAL_NAME_num(gens); ++i){
         gen = sk_GENERAL_NAME_value(gens, i);
         if(gen->type == GEN_DNS){
            if(n_poption & n_PO_D_V)
               n_err(_("Comparing subject_alt_name: need<%s> is<%s>\n"),
                  urlp->url_host.s, (char*)gen->d.ia5->data);
            if((rv = n_tls_rfc2595_hostname_match(urlp->url_host.s,
                  (char*)gen->d.ia5->data)))
               goto jleave;
         }
      }
   }

   if((subj = X509_get_subject_name(peercert)) != NULL &&
         X509_NAME_get_text_by_NID(subj, NID_commonName, data, sizeof data
            ) > 0){
      data[sizeof data - 1] = '\0';
      if(n_poption & n_PO_D_V)
         n_err(_("Comparing commonName: need<%s> is<%s>\n"),
            urlp->url_host.s, data);
      rv = n_tls_rfc2595_hostname_match(urlp->url_host.s, data);
   }
jleave:
   NYD_OU;
   return rv;
}

static int
smime_verify(struct message *m, int n, a_XTLS_STACKOF(X509) *chain,
   X509_STORE *store)
{
   char data[mx_LINESIZE], *sender, *to, *cc, *cnttype;
   int rv, c, i, j;
   struct message *x;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   a_XTLS_STACKOF(X509) *certs;
   a_XTLS_STACKOF(GENERAL_NAME) *gens;
   X509 *cert;
   X509_NAME *subj;
   GENERAL_NAME *gen;
   NYD_IN;

   rv = 1;
   fp = NULL;
   fb = pb = NULL;
   pkcs7 = NULL;
   certs = NULL;
   a_xtls_state &= ~a_XTLS_S_VERIFY_ERROR;
   a_xtls_msgno = (uz)n;

   for (;;) {
      sender = getsender(m);
      to = hfield1("to", m);
      cc = hfield1("cc", m);
      cnttype = hfield1("content-type", m);

#undef _X
#undef _Y
#define _X     (sizeof("application/") -1)
#define _Y(X)  X, sizeof(X) -1
      if (cnttype && su_cs_starts_with_case(cnttype, "application/") &&
            (!su_cs_cmp_case_n(cnttype + _X, _Y("pkcs7-mime")) ||
             !su_cs_cmp_case_n(cnttype + _X, _Y("x-pkcs7-mime")))) {
#undef _Y
#undef _X
         if ((x = smime_decrypt(m, to, cc, TRU1)) == NULL)
            goto jleave;
         if (x != (struct message*)-1) {
            m = x;
            continue;
         }
      }

      if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
         goto jleave;
      size = m->m_size;
      break;
   }

   if((fp = mx_fs_tmp_open(NIL, "smimever", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL){
      n_perr(_("tempfile"), 0);
      goto jleave;
   }
   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush_rewind(fp);

   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_(
         "Error creating BIO verification object for message %d"), n);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object for message %d"), n);
      goto jleave;
   }
   if (PKCS7_verify(pkcs7, chain, store, pb, NULL, 0) != 1) {
      ssl_gen_err(_("Error verifying message %d"), n);
      goto jleave;
   }

   if (sender == NULL) {
      n_err(_("Warning: Message %d has no sender\n"), n);
      rv = 0;
      goto jleave;
   }

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      n_err(_("No certificates found in message %d\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
      if (gens != NULL) {
         for (j = 0; j < sk_GENERAL_NAME_num(gens); ++j) {
            gen = sk_GENERAL_NAME_value(gens, j);
            if (gen->type == GEN_EMAIL) {
               if (n_poption & n_PO_D_V)
                  n_err(_("Comparing subject_alt_name: need<%s> is<%s>)\n"),
                     sender, (char*)gen->d.ia5->data);
               if (!su_cs_cmp_case((char*)gen->d.ia5->data, sender))
                  goto jfound;
            }
         }
      }

      if ((subj = X509_get_subject_name(cert)) != NULL &&
            X509_NAME_get_text_by_NID(subj, NID_pkcs9_emailAddress,
               data, sizeof data) > 0) {
         data[sizeof data -1] = '\0';
         if (n_poption & n_PO_D_V)
            n_err(_("Comparing emailAddress: need<%s> is<%s>\n"),
               sender, data);
         if (!su_cs_cmp_case(data, sender))
            goto jfound;
      }
   }
   n_err(_("Message %d: certificate does not match <%s>\n"), n, sender);
   goto jleave;
jfound:
   rv = ((a_xtls_state & a_XTLS_S_VERIFY_ERROR) != 0);
   if (!rv)
      fprintf(n_stdout, _("Message %d was verified successfully\n"), n);
jleave:
   if (certs != NULL)
      sk_X509_free(certs);
   if (pb != NULL)
      BIO_free(pb);
   if (fb != NULL)
      BIO_free(fb);
   if (pkcs7 != NULL)
      PKCS7_free(pkcs7);
   if(fp != NIL)
      mx_fs_close(fp);
   NYD_OU;
   return rv;
}

static EVP_CIPHER const *
_smime_cipher(char const *name)
{
   EVP_CIPHER const *cipher;
   char *vn;
   char const *cp;
   uz i;
   NYD_IN;

   vn = n_lofi_alloc(i = su_cs_len(name) + sizeof("smime-cipher-") -1 +1);
   snprintf(vn, (int)i, "smime-cipher-%s", name);
   cp = n_var_vlook(vn, FAL0);
   n_lofi_free(vn);

   if (cp == NULL && (cp = ok_vlook(smime_cipher)) == NULL) {
      cipher = a_XTLS_SMIME_DEFAULT_CIPHER();
      goto jleave;
   }
   cipher = NULL;

   for(i = 0; i < NELEM(a_xtls_ciphers); ++i)
      if(!su_cs_cmp_case(a_xtls_ciphers[i].xc_name, cp)){
         cipher = (*a_xtls_ciphers[i].xc_fun)();
         goto jleave;
      }
#ifndef OPENSSL_NO_AES
   for (i = 0; i < NELEM(a_xtls_smime_ciphers_obs); ++i) /* TODO obsolete */
      if (!su_cs_cmp_case(a_xtls_smime_ciphers_obs[i].xc_name, cp)) {
         n_OBSOLETE2(_("*smime-cipher* names with hyphens will vanish"), cp);
         cipher = (*a_xtls_smime_ciphers_obs[i].xc_fun)();
         goto jleave;
      }
#endif

   /* Not a built-in algorithm, but we may have dynamic support for more */
#ifdef mx_HAVE_TLS_ALL_ALGORITHMS
   if((cipher = EVP_get_cipherbyname(cp)) != NULL)
      goto jleave;
#endif

   n_err(_("Invalid S/MIME cipher(s): %s\n"), cp);
jleave:
   NYD_OU;
   return cipher;
}

static int
ssl_password_cb(char *buf, int size, int rwflag, void *userdata)
{
   char *pass;
   uz len;
   NYD_IN;
   UNUSED(rwflag);
   UNUSED(userdata);

   /* New-style */
   if(userdata != NULL){
      struct mx_url url;
      struct mx_cred_ctx cred;

      if(mx_url_parse(&url, CPROTO_CCRED, userdata)){
         if(mx_cred_auth_lookup(&cred, &url)){
            char *end;

            if((end = su_cs_pcopy_n(buf, cred.cc_pass.s, size)) != NULL){
               size = (int)P2UZ(end - buf);
               goto jleave;
            }
         }
         size = 0;
         goto jleave;
      }
   }

   /* Old-style */
   if((pass = mx_tty_getpass("PEM pass phrase:")) != NIL){
      len = su_cs_len(pass);
      if (UCMP(z, len, >=, size))
         len = size -1;
      su_mem_copy(buf, pass, len);
      buf[len] = '\0';
      size = (int)len;
   } else
      size = 0;
jleave:
   NYD_OU;
   return size;
}

static FILE *
smime_sign_cert(char const *xname, char const *xname2, boole dowarn,
   char const **match, boole fallback_from)
{
   char *vn;
   int vs;
   struct mx_name *np;
   char const *name = xname, *name2 = xname2, *cp;
   FILE *fp = NULL;
   NYD_IN;

jloop:
   if (name) {
      np = lextract(name, GTO | GSKIN);
      while (np != NULL) {
         /* This needs to be more intelligent since it will currently take the
          * first name for which a private key is available regardless of
          * whether it is the right one for the message */
         vn = n_lofi_alloc(vs = su_cs_len(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-cert-%s", np->n_name);
         cp = n_var_vlook(vn, FAL0);
         n_lofi_free(vn);
         if (cp != NULL) {
            if (match != NULL)
               *match = np->n_name;
            goto jopen;
         }
         np = np->n_flink;
      }
      if (name2 != NULL) {
         name = name2;
         name2 = NULL;
         goto jloop;
      }
   }

   /* It is the default *smime-sign-cert* / *from* pair */
   if((cp = ok_vlook(smime_sign_cert)) == NIL)
      goto jerr;

   if(match != NIL){
      name = fallback_from ? myorigin(NIL) : NIL;
      *match = (name == NIL) ? NIL : savestr(name);
   }

jopen:
   if((cp = fexpand(cp, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
         ) == NIL)
      goto jleave;
   if((fp = mx_fs_open(cp, mx_FS_O_RDONLY)) == NIL)
      n_perr(cp, 0);

jleave:
   NYD_OU;
   return fp;

jerr:
   if (dowarn)
      n_err(_("Could not find a certificate for %s%s%s\n"),
         xname, (xname2 != NULL ? _("or ") : n_empty),
         (xname2 != NULL ? xname2 : n_empty));
   goto jleave;
}

static char const *
_smime_sign_include_certs(char const *name)
{
   char const *rv;
   NYD_IN;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if (name != NULL) {
      struct mx_name *np;

      for (np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink) {
         int vs;
         char *vn;

         vn = n_lofi_alloc(vs = su_cs_len(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-include-certs-%s", np->n_name);
         rv = n_var_vlook(vn, FAL0);
         n_lofi_free(vn);
         if (rv != NULL)
            goto jleave;
      }
   }
   rv = ok_vlook(smime_sign_include_certs);
jleave:
   NYD_OU;
   return rv;
}

static boole
_smime_sign_include_chain_creat(a_XTLS_STACKOF(X509) **chain,
   char const *cfiles, char const *addr)
{
   X509 *tmp;
   FILE *fp;
   char *nfield, *cfield, *x;
   NYD_IN;

   *chain = sk_X509_new_null();

   for (nfield = savestr(cfiles);
         (cfield = su_cs_sep_c(&nfield, ',', TRU1)) != NULL;) {
      if((x = fexpand(cfield, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
            ) == NIL || (fp = mx_fs_open(cfield = x, mx_FS_O_RDONLY)) == NIL){
         n_perr(cfiles, 0);
         goto jerr;
      }
      if ((tmp = PEM_read_X509(fp, NULL, &ssl_password_cb, n_UNCONST(addr))
            ) == NULL) {
         ssl_gen_err(_("Error reading certificate from %s"),
            n_shexp_quote_cp(cfield, FAL0));
         mx_fs_close(fp);
         goto jerr;
      }
      sk_X509_push(*chain, tmp);
      mx_fs_close(fp);
   }

   if (sk_X509_num(*chain) == 0) {
      n_err(_("*smime-sign-include-certs* defined but empty\n"));
      goto jerr;
   }
jleave:
   NYD_OU;
   return (*chain != NULL);
jerr:
   sk_X509_pop_free(*chain, X509_free);
   *chain = NULL;
   goto jleave;
}

static EVP_MD const *
a_xtls_smime_sign_digest(char const *name, char const **digname){
   EVP_MD const *digest;
   char const *cp;
   NYD2_IN;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if(name != NULL){
      struct mx_name *np;

      for(np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink){
         int vs;
         char *vn;

         vn = n_lofi_alloc(vs = su_cs_len(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-digest-%s", np->n_name);
         if((cp = n_var_vlook(vn, FAL0)) == NULL){
            snprintf(vn, vs, "smime-sign-message-digest-%s",np->n_name);/*v15*/
            cp = n_var_vlook(vn, FAL0);
         }
         n_lofi_free(vn);
         if(cp != NULL)
            goto jhave_name;
      }
   }

   if((cp = ok_vlook(smime_sign_digest)) != NULL ||
         (cp = ok_vlook(smime_sign_message_digest)/* v15 */) != NULL)
jhave_name:
      if(a_xtls_digest_find(cp, &digest, digname)){
#ifndef PKCS7_PARTIAL
         n_err(_("WARNING: old OpenSSL version, *smime-sign-digest*=%s "
            "ignored!\n"), digname);
#endif
         goto jleave;
      }

   digest = a_XTLS_SMIME_DEFAULT_DIGEST();
   *digname = a_XTLS_SMIME_DEFAULT_DIGEST_S;
jleave:
   NYD2_OU;
   return digest;
}

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay
load_crl1(X509_STORE *store, char const *name)
{
   X509_LOOKUP *lookup;
   enum okay rv = STOP;
   NYD_IN;

   if (n_poption & n_PO_D_V)
      n_err(_("Loading CRL from %s\n"), n_shexp_quote_cp(name, FAL0));
   if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL) {
      ssl_gen_err(_("Error creating X509 lookup object"));
      goto jleave;
   }
   if (X509_load_crl_file(lookup, name, X509_FILETYPE_PEM) != 1) {
      ssl_gen_err(_("Error loading CRL from %s"),
         n_shexp_quote_cp(name, FAL0));
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_OU;
   return rv;
}
#endif /* new OpenSSL */

static enum okay
load_crls(X509_STORE *store, enum okeys fok, enum okeys dok)/*TODO nevertried*/
{
   char *crl_file, *crl_dir;
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   DIR *dirp;
   struct dirent *dp;
   char *fn = NULL;
   int fs = 0, ds, es;
#endif
   boole any;
   enum okay rv;
   NYD_IN;

   rv = STOP;
   any = FAL0;

jredo_v15:
   if ((crl_file = n_var_oklook(fok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      if((crl_file = fexpand(crl_file, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
            FEXP_NSHELL))) == NIL || load_crl1(store, crl_file) != OKAY)
         goto jleave;
      any = TRU1;
#else
      n_err(_("This OpenSSL version is too old to use CRLs\n"));
      goto jleave;
#endif
   }

   if ((crl_dir = n_var_oklook(dok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      char *x;

      if((x = fexpand(crl_dir, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL))
            ) == NIL || (dirp = opendir(crl_dir = x)) == NIL){
         n_perr(crl_dir, 0);
         goto jleave;
      }

      ds = su_cs_len(crl_dir);
      fn = n_alloc(fs = ds + 20);
      su_mem_copy(fn, crl_dir, ds);
      fn[ds] = '/';
      while ((dp = readdir(dirp)) != NULL) {
         if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
               (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
            continue;
         if (dp->d_name[0] == '.')
            continue;
         if (ds + (es = su_cs_len(dp->d_name)) + 2 < fs)
            fn = n_realloc(fn, fs = ds + es + 20);
         su_mem_copy(fn + ds + 1, dp->d_name, es + 1);
         if (load_crl1(store, fn) != OKAY) {
            closedir(dirp);
            n_free(fn);
            goto jleave;
         }
         any = TRU1;
      }
      closedir(dirp);
      n_free(fn);
#else /* old OpenSSL */
      n_err(_("This OpenSSL version is too old to use CRLs\n"));
      goto jleave;
#endif
   }

   if(fok == ok_v_tls_crl_file){
      fok = ok_v_ssl_crl_file;
      dok = ok_v_ssl_crl_dir;
      goto jredo_v15;
   }
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   if(any)
      X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
         X509_V_FLAG_CRL_CHECK_ALL);
#endif
   rv = OKAY;
jleave:
   NYD_OU;
   return rv;
}

#if su_RANDOM_SEED == su_RANDOM_SEED_HOOK && mx_RANDOM_SEED_HOOK == 3
FL boole
mx_random_hook(void **cookie, void *buf, uz len){
   NYD2_IN;
   ASSERT(len > 0);
   ASSERT(cookie != NIL && *cookie == NIL);
   UNUSED(cookie);

   if(!(a_xtls_state & a_XTLS_S_RAND_INIT))
      a_xtls_rand_init();

   for(;;){
      s32 i;

      switch(RAND_bytes(buf, i = MIN(S32_MAX, len))){
      default:
         /* LibreSSL always succeeds, i think it aborts otherwise.
          * With elder OpenSSL we ensure via RAND_status() in
          * a_xtls_rand_init() that the PRNG is seeded, so it does not fail.
          *
          * With newer OpenSSL we disable automatic reseeding, but do not
          * ASSERT RAND_status() ("Since you always have to check RAND_bytes's
          * return value now, RAND_status is mostly useless.",
          * 20190104180735.GA25041@roeckx.be), so we have not that many options
          * on what to do.  Since OSs will try hard to serve, a simple sleep
          * may be it, so do that */
# if mx_HAVE_TLS != mx_TLS_IMPL_RESSL && !defined mx_XTLS_HAVE_RAND_FILE
         n_err(_("TLS RAND_bytes(3ssl) failed (missing entropy?), "
            "waiting a bit\n"));
         /* Around ~Y2K+1 anything <= was a busy loop iirc, so give pad */
         su_time_msleep(250, FAL0);
         continue;
# endif
         /* FALLTHRU */
      case 1:
         break;
      }

      if((len -= i) == 0)
         break;
      buf = &S(u8*,buf)[i];
   }

   NYD2_OU;
   return TRU1;
}
#endif /* su_RANDOM_SEED == su_RANDOM_SEED_HOOK && mx_RANDOM_SEED_HOOK == 3 */

FL boole
n_tls_open(struct mx_url *urlp, struct mx_socket *sop){ /* TODO split */
   void *confp;
   SSL_CTX *ctxp;
   const EVP_MD *fprnt_mdp;
   char const *fprnt, *fprnt_namep;
   NYD_IN;

   a_xtls_init();
   n_tls_set_verify_level(urlp); /* TODO should come in via URL! */

   sop->s_tls = NULL;
   if(urlp->url_cproto != CPROTO_CERTINFO)
      fprnt = xok_vlook(tls_fingerprint, urlp, OXM_ALL);
   else
      fprnt = NULL;
   fprnt_namep = NULL;
   fprnt_mdp = NULL;

   if(fprnt != NULL || urlp->url_cproto == CPROTO_CERTINFO ||
         (n_poption & n_PO_D_V)){
      if((fprnt_namep = xok_vlook(tls_fingerprint_digest, urlp,
            OXM_ALL)) == NULL ||
            !a_xtls_digest_find(fprnt_namep, &fprnt_mdp, &fprnt_namep)){
         fprnt_mdp = a_XTLS_FINGERPRINT_DEFAULT_DIGEST();
         fprnt_namep = a_XTLS_FINGERPRINT_DEFAULT_DIGEST_S;
      }
   }

   if((ctxp = SSL_CTX_new(mx_XTLS_CLIENT_METHOD())) == NULL){
      ssl_gen_err(_("SSL_CTX_new() failed"));
      goto j_leave;
   }

   /* Available with OpenSSL 0.9.6 or later */
#ifdef SSL_MODE_AUTO_RETRY
   SSL_CTX_set_mode(ctxp, SSL_MODE_AUTO_RETRY);
#endif

   if((confp = a_xtls_conf_setup(ctxp, urlp)) == NULL)
      goto jleave;

   if(!a_xtls_obsolete_conf_vars(confp, urlp))
      goto jerr1;
   if(!a_xtls_config_pairs(confp, urlp))
      goto jerr1;
   if((fprnt == NULL || urlp->url_cproto == CPROTO_CERTINFO) &&
         !a_xtls_load_verifications(ctxp, urlp))
      goto jerr1;

   /* Done with context setup, create our new per-connection structure */
   if(!a_xtls_conf_finish(&confp, FAL0))
      goto jleave;
   ASSERT(confp == NULL);

   if((sop->s_tls = SSL_new(ctxp)) == NULL){
      ssl_gen_err(_("SSL_new() failed"));
      goto jleave;
   }

   /* Try establish SNI extension; even though this is a TLS extension the
    * protocol isn't checked by OpenSSL once the host name is set, and
    * therefore i refrained from changing so much code just to check out
    * whether we are using SSLv3, which should become more and more rare */
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
   if((urlp->url_flags & mx_URL_TLS_MASK) &&
         (urlp->url_flags & mx_URL_HOST_IS_NAME)){
      if(!SSL_set_tlsext_host_name(sop->s_tls, urlp->url_host.s) &&
            (n_poption & n_PO_D_V))
         n_err(_("Hostname cannot be used with ServerNameIndication "
               "TLS extension: %s\n"),
            n_shexp_quote_cp(urlp->url_host.s, FAL0));
   }
#endif

   SSL_set_fd(sop->s_tls, sop->s_fd);
   mx_socket_reset_io_buf(sop);

   if(SSL_connect(sop->s_tls) < 0){
      ssl_gen_err(_("could not initiate TLS connection"));
      goto jerr2;
   }

   if(fprnt != NULL || urlp->url_cproto == CPROTO_CERTINFO ||
         n_tls_verify_level != n_TLS_VERIFY_IGNORE){
      boole stay;
      X509 *peercert;

      if((peercert = a_xtls_SSL_get_peer_certificate(sop->s_tls)) == NIL){
         n_err(_("TLS: no certificate from peer: %s\n"), urlp->url_h_p.s);
         goto jerr2;
      }

      stay = FAL0;

      if(fprnt == NULL){
         if(!a_xtls_check_host(sop, peercert, urlp)){
            n_err(_("TLS certificate does not match: %s\n"), urlp->url_h_p.s);
            stay = n_tls_verify_decide();
         }else{
            if(n_poption & n_PO_D_V)
               n_err(_("TLS certificate ok\n"));
            stay = TRU1;
         }
      }

      if(fprnt != NULL || urlp->url_cproto == CPROTO_CERTINFO ||
            (n_poption & n_PO_D_V)){
         char fpmdhexbuf[EVP_MAX_MD_SIZE * 3], *cp;
         unsigned char fpmdbuf[EVP_MAX_MD_SIZE], *ucp;
         unsigned int fpmdlen;

         if(!X509_digest(peercert, fprnt_mdp, fpmdbuf, &fpmdlen)){
            ssl_gen_err(_("TLS %s fingerprint creation failed"), fprnt_namep);
            goto jpeer_leave;
         }
         ASSERT(fpmdlen <= EVP_MAX_MD_SIZE);

         for(cp = fpmdhexbuf, ucp = fpmdbuf; fpmdlen > 0; --fpmdlen){
            n_c_to_hex_base16(cp, (char)*ucp++);
            cp[2] = ':';
            cp += 3;
         }
         cp[-1] = '\0';

         if(n_poption & n_PO_D_V)
            n_err(_("TLS %s fingerprint: %s\n"), fprnt_namep, fpmdhexbuf);
         if(fprnt != NULL){
            if(!(stay = !su_cs_cmp_case(fprnt, fpmdhexbuf))){
               n_err(_("TLS fingerprint mismatch: %s\n"
                     "  Expected: %s\n  Detected: %s\n"),
                  urlp->url_h_p.s, fprnt, fpmdhexbuf);
               stay = n_tls_verify_decide();
            }else if(n_poption & n_PO_D_V)
               n_err(_("TLS fingerprint ok\n"));
            goto jpeer_leave;
         }else if(urlp->url_cproto == CPROTO_CERTINFO){
            char *xcp;
            long i;
            BIO *biop;
            a_XTLS_STACKOF(X509) *certs;

            sop->s_tls_finger = savestrbuf(fpmdhexbuf,
                  P2UZ(cp - fpmdhexbuf));

            /* For the sake of `tls cert(chain|ificate)', this too */

            /*if((certs = SSL_get_peer_cert_chain(sop->s_tls)) != NIL){*/
            if((certs = a_xtls_SSL_get_verified_chain(sop->s_tls)) != NIL){
               if((biop = BIO_new(BIO_s_mem())) != NIL){
                  xcp = NIL;
                  peercert = NIL;

                  for(i = 0; i < sk_X509_num(certs); ++i){
                     peercert = sk_X509_value(certs, i);
                     if(((n_poption & n_PO_D_V) &&
                              X509_print(biop, peercert) == 0) ||
                           PEM_write_bio_X509(biop, peercert) == 0){
                        ssl_gen_err(_("Error storing certificate %d from %s"),
                           i, urlp->url_h_p.s);
                        peercert = NIL;
                        break;
                     }

                     if(i == 0){
                        i = BIO_get_mem_data(biop, &xcp);
                        if(i > 0)
                           sop->s_tls_certificate = savestrbuf(xcp, i);
                        i = 0;
                     }
                  }

                  if(peercert != NIL){
                     i = BIO_get_mem_data(biop, &xcp);
                     if(i > 0)
                        sop->s_tls_certchain = savestrbuf(xcp, i);
                  }

                  BIO_free(biop);
               }
            }
         }
      }

jpeer_leave:
      a_xtls_SSL_get_peer_certificate__FREE(peercert);
      if(!stay)
         goto jerr2;
   }

   if(n_poption & n_PO_D_V){
      struct a_xtls_protocol const *xpp;
      int ver;

      ver = SSL_version(sop->s_tls);
      for(xpp = &a_xtls_protocols[1] /* [0] == ALL */;; ++xpp)
         if(xpp->xp_version == ver || xpp->xp_last){
            n_err(_("TLS connection using %s / %s\n"),
               (xpp->xp_last ? n_qm : xpp->xp_name),
               SSL_get_cipher(sop->s_tls));
            break;
         }
   }

   sop->s_use_tls = 1;
jleave:
   /* We're fully setup: since we don't reuse the SSL_CTX (pooh) keep it local
    * and free it right now -- it is reference counted by sp->s_tls.. */
   SSL_CTX_free(ctxp);
j_leave:
   NYD_OU;
   return (sop->s_tls != NULL);
jerr2:
   SSL_free(sop->s_tls);
   sop->s_tls = NULL;
jerr1:
   if(confp != NULL)
      a_xtls_conf_finish(&confp, TRU1);
   goto jleave;
}

FL void
ssl_gen_err(char const *fmt, ...)
{
   va_list ap;
   NYD_IN;

   va_start(ap, fmt);
   n_verr(fmt, ap);
   va_end(ap);

   n_err(_(": %s\n"), ERR_error_string(ERR_get_error(), NULL));
   NYD_OU;
}

FL int
c_verify(void *vp)
{
   int *msgvec = vp, *ip, ec = 0, rv = 1;
   X509_STORE *store = NULL;
   char *ca_dir, *ca_file;
   NYD_IN;

   a_xtls_init();

   n_tls_verify_level = n_TLS_VERIFY_STRICT;
   if ((store = X509_STORE_new()) == NULL) {
      ssl_gen_err(_("Error creating X509 store"));
      goto jleave;
   }
   X509_STORE_set_verify_cb_func(store, &a_xtls_verify_cb);

   if((ca_dir = ok_vlook(smime_ca_dir)) != NIL)
      ca_dir = fexpand(ca_dir, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL));
   if((ca_file = ok_vlook(smime_ca_file)) != NIL)
      ca_file = fexpand(ca_file, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
         FEXP_NSHELL));

   if(ca_file != NIL && a_xtls_X509_STORE_load_file(store, ca_file) != 1){
      ssl_gen_err(_("Error loading %s\n"), n_shexp_quote_cp(ca_file, FAL0));
      goto jleave;
   }

   if(ca_dir != NIL && a_xtls_X509_STORE_load_path(store, ca_dir) != 1){
      ssl_gen_err(_("Error loading %s\n"), n_shexp_quote_cp(ca_dir, FAL0));
      goto jleave;
   }

   /* C99 */{
      boole xv15;

      if((xv15 = ok_blook(smime_no_default_ca)))
         n_OBSOLETE(_("please use *smime-ca-no-defaults*, "
            "not *smime-no-default-ca*"));
      if(!ok_blook(smime_ca_no_defaults) && !xv15 &&
            X509_STORE_set_default_paths(store) != 1) {
         ssl_gen_err(_("Error loading built-in default CA locations\n"));
         goto jleave;
      }
   }

   if (load_crls(store, ok_v_smime_crl_file, ok_v_smime_crl_dir) != OKAY)
      goto jleave;

   a_xtls_ca_flags(store, ok_vlook(smime_ca_flags));

   srelax_hold();
   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp;

      mp = &message[*ip - 1];
      setdot(mp, FAL0);
      ec |= smime_verify(mp, *ip, NULL, store);
      srelax();
   }
   srelax_rele();

   if ((rv = ec) != 0)
      n_exit_status |= su_EX_ERR;
jleave:
   if (store != NULL)
      X509_STORE_free(store);
   NYD_OU;
   return rv;
}

FL FILE *
smime_sign(FILE *ip, char const *addr)
{
   FILE *rv, *sfp, *fp, *bp, *hp;
   X509 *cert = NULL;
   a_XTLS_STACKOF(X509) *chain = NIL;
   EVP_PKEY *pkey = NULL;
   BIO *bb, *sb;
   PKCS7 *pkcs7;
   EVP_MD const *md;
   char const *name;
   boole bail = FAL0;
   NYD_IN;

   /* TODO smime_sign(): addr should vanish, it is either *from* aka *sender*
    * TODO or what we parsed as From:/Sender: from a template.  This latter
    * TODO should set *from* / *sender* in a scope, we should use *sender*:
    * TODO *sender* should be set to the real *from*! */
   ASSERT(addr != NULL);
   rv = sfp = fp = bp = hp = NULL;

   a_xtls_init();

   if ((fp = smime_sign_cert(addr, NIL, 1, NIL, FAL0)) == NIL)
      goto jleave;

   if ((pkey = PEM_read_PrivateKey(fp, NULL, &ssl_password_cb,
         savecat(addr, ".smime-cert-key"))) == NULL) {
      ssl_gen_err(_("Error reading private key from"));
      goto jleave;
   }

   rewind(fp);
   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb,
         savecat(addr, ".smime-cert-cert"))) == NULL) {
      ssl_gen_err(_("Error reading signer certificate from"));
      goto jleave;
   }
   mx_fs_close(fp);
   fp = NULL;

   if ((name = _smime_sign_include_certs(addr)) != NULL &&
         !_smime_sign_include_chain_creat(&chain, name,
            savecat(addr, ".smime-include-certs")))
      goto jleave;

   name = NULL;
   if ((md = a_xtls_smime_sign_digest(addr, &name)) == NULL)
      goto jleave;

   if((sfp = mx_fs_tmp_open(NIL, "smimesign", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL){
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   rewind(ip);
   if(!mx_smime_split(ip, &hp, &bp, -1, FAL0))
      goto jleave;

   sb = NULL;
   pkcs7 = NULL;

   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (sb = BIO_new_fp(sfp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO signing objects"));
      bail = TRU1;
      goto jerr;
   }

#ifdef PKCS7_PARTIAL
   if((pkcs7 = PKCS7_sign(NULL, NULL, chain, bb,
         (PKCS7_DETACHED | PKCS7_PARTIAL))) == NIL){
      ssl_gen_err(_("Error creating the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
   if(PKCS7_sign_add_signer(pkcs7, cert, pkey, md,
         (PKCS7_DETACHED | PKCS7_PARTIAL)) == NIL){
      ssl_gen_err(_("Error setting PKCS#7 signing object signer"));
      bail = TRU1;
      goto jerr;
   }
   if(!PKCS7_final(pkcs7, bb, (PKCS7_DETACHED | PKCS7_PARTIAL))){
      ssl_gen_err(_("Error finalizing the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
#else
   if((pkcs7 = PKCS7_sign(cert, pkey, chain, bb, PKCS7_DETACHED)) == NIL){
      ssl_gen_err(_("Error creating the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
#endif /* !PKCS7_PARTIAL */

   if (PEM_write_bio_PKCS7(sb, pkcs7) == 0) {
      ssl_gen_err(_("Error writing signed S/MIME data"));
      bail = TRU1;
      /*goto jerr*/
   }
jerr:
   if (pkcs7 != NULL)
      PKCS7_free(pkcs7);
   if (sb != NULL)
      BIO_free(sb);
   if (bb != NULL)
      BIO_free(bb);
   if (!bail) {
      rewind(bp);
      fflush_rewind(sfp);
      rv = smime_sign_assemble(hp, bp, sfp, name);
      hp = bp = sfp = NULL;
   }

jleave:
   if (chain != NULL)
      sk_X509_pop_free(chain, X509_free);
   if (cert != NULL)
      X509_free(cert);
   if (pkey != NULL)
      EVP_PKEY_free(pkey);
   if(fp != NIL)
      mx_fs_close(fp);
   if(hp != NIL)
      mx_fs_close(hp);
   if(bp != NIL)
      mx_fs_close(bp);
   if(sfp != NIL)
      mx_fs_close(sfp);
   NYD_OU;
   return rv;
}

FL FILE *
smime_encrypt(FILE *ip, char const *xcertfile, char const *to)
{
   FILE *rv, *yp, *fp, *bp, *hp;
   X509 *cert;
   PKCS7 *pkcs7;
   BIO *bb, *yb;
   a_XTLS_STACKOF(X509) *certs;
   EVP_CIPHER const *cipher;
   char *certfile;
   boole bail;
   NYD_IN;

   bail = FAL0;
   rv = yp = fp = bp = hp = NULL;

   if((certfile = fexpand(xcertfile, (FEXP_NOPROTO | FEXP_LOCAL_FILE |
         FEXP_NSHELL))) == NIL)
      goto jleave;

   a_xtls_init();

   if ((cipher = _smime_cipher(to)) == NULL)
      goto jleave;

   if((fp = mx_fs_open(certfile, mx_FS_O_RDONLY)) == NIL){
      n_perr(certfile, 0);
      goto jleave;
   }
   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(_("Error reading encryption certificate from %s"),
         n_shexp_quote_cp(certfile, FAL0));
      bail = TRU1;
   }
   if (bail)
      goto jleave;
   mx_fs_close(fp);
   fp = NULL;
   bail = FAL0;

   certs = sk_X509_new_null();
   sk_X509_push(certs, cert);

   if((yp = mx_fs_tmp_open(NIL, "smimeenc", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL){
      n_perr(_("tempfile"), 0);
      goto jerr1;
   }

   rewind(ip);
   if(!mx_smime_split(ip, &hp, &bp, -1, FAL0))
      goto jerr1;

   yb = NULL;
   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (yb = BIO_new_fp(yp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO encryption objects"));
      bail = TRU1;
      goto jerr2;
   }
   if ((pkcs7 = PKCS7_encrypt(certs, bb, cipher, 0)) == NULL) {
      ssl_gen_err(_("Error creating the PKCS#7 encryption object"));
      bail = TRU1;
      goto jerr2;
   }
   if (PEM_write_bio_PKCS7(yb, pkcs7) == 0) {
      ssl_gen_err(_("Error writing encrypted S/MIME data"));
      bail = TRU1;
      /* goto jerr2 */
   }
   PKCS7_free(pkcs7);

jerr2:
   if(bb != NIL)
      BIO_free(bb);
   if(yb != NIL)
      BIO_free(yb);
   mx_fs_close(bp);
   bp = NIL;
   if(!bail){
      fflush_rewind(yp);
      rv = smime_encrypt_assemble(hp, yp);
      hp = yp = NIL;
   }
jerr1:
   sk_X509_pop_free(certs, X509_free);

jleave:
   if(yp != NIL)
      mx_fs_close(yp);
   if(fp != NIL)
      mx_fs_close(fp);
   if(bp != NIL)
      mx_fs_close(bp);
   if(hp != NIL)
      mx_fs_close(hp);
   NYD_OU;
   return rv;
}

FL struct message *
smime_decrypt(struct message *m, char const *to, char const *cc,
   boole is_a_verify_call)
{
   char const *myaddr;
   long size;
   struct message *rv;
   FILE *bp, *hp, *op;
   PKCS7 *pkcs7;
   BIO *ob, *bb, *pb;
   X509 *cert;
   EVP_PKEY *pkey;
   FILE *yp;
   NYD_IN;

   pkey = NULL;
   cert = NULL;
   ob = bb = pb = NULL;
   pkcs7 = NULL;
   bp = hp = op = NULL;
   rv = NULL;
   size = m->m_size;

   if((yp = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;

   a_xtls_init();

   if((op = smime_sign_cert(to, cc, 0, &myaddr, TRU1)) != NULL){
      pkey = PEM_read_PrivateKey(op, NULL, &ssl_password_cb,
            savecat(myaddr, ".smime-cert-key"));
      if(pkey == NULL){
         ssl_gen_err(_("Error reading private key"));
         goto jleave;
      }

      rewind(op);
      if((cert = PEM_read_X509(op, NULL, &ssl_password_cb,
            savecat(myaddr, ".smime-cert-cert"))) == NULL){
         ssl_gen_err(_("Error reading decryption certificate"));
         goto jleave;
      }

      mx_fs_close(op);
      op = NULL;
   }

   if((op = mx_fs_tmp_open(NIL, "smimed", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL){
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   if(!mx_smime_split(yp, &hp, &bp, size, TRU1))
      goto jleave;

   if((ob = BIO_new_fp(op, BIO_NOCLOSE)) == NULL ||
         (bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL){
      ssl_gen_err(_("Error creating BIO decryption objects"));
      goto jleave;
   }

   if((pkcs7 = SMIME_read_PKCS7(bb, &pb)) == NULL){
      ssl_gen_err(_("Error reading PKCS#7 object"));
      goto jleave;
   }

   if(PKCS7_type_is_signed(pkcs7)){
      if(!is_a_verify_call){
         setinput(&mb, m, NEED_BODY);
         rv = (struct message*)-1;
         goto jleave;
      }
      if(PKCS7_verify(pkcs7, NULL, NULL, NULL, ob,
            PKCS7_NOVERIFY | PKCS7_NOSIGS) != 1)
         goto jerr;
      fseek(hp, 0L, SEEK_END);
      fprintf(hp, "X-Encryption-Cipher: none\n");
      fflush_rewind(hp);
   }else if(pkey == NULL){
      n_err(_("No appropriate private key found\n"));
      goto jleave;
   }else if(cert == NULL){
      n_err(_("No appropriate certificate found\n"));
      goto jleave;
   }else if(PKCS7_decrypt(pkcs7, pkey, cert, ob, 0) != 1){
jerr:
      ssl_gen_err(_("Error decrypting PKCS#7 object"));
      goto jleave;
   }
   fflush_rewind(op);

   mx_fs_close(bp);
   bp = NIL;

   if((rv = mx_smime_decrypt_assemble(m, hp, op)) == NIL)
      n_err(_("I/O error while creating decrypted message\n"));
jleave:
   if(op != NIL)
      mx_fs_close(op);
   if(hp != NIL)
      mx_fs_close(hp);
   if(bp != NIL)
      mx_fs_close(bp);
   if(bb != NIL)
      BIO_free(bb);
   if(ob != NIL)
      BIO_free(ob);
   if(pkcs7 != NIL)
      PKCS7_free(pkcs7);
   if(cert != NIL)
      X509_free(cert);
   if(pkey != NIL)
      EVP_PKEY_free(pkey);

   NYD_OU;
   return rv;
}

FL enum okay
smime_certsave(struct message *m, int n, FILE *op)
{
   struct message *x;
   char *to, *cc, *cnttype;
   int c, i;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   a_XTLS_STACKOF(X509) *certs, *chain = NIL;
   X509 *cert;
   enum okay rv = STOP;
   NYD_IN;

   pkcs7 = NULL;

   a_xtls_msgno = (uz)n;
jloop:
   to = hfield1("to", m);
   cc = hfield1("cc", m);
   cnttype = hfield1("content-type", m);

   if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;

#undef _X
#undef _Y
#define _X     (sizeof("application/") -1)
#define _Y(X)  X, sizeof(X) -1
   if (cnttype && su_cs_starts_with_case(cnttype, "application/") &&
         (!su_cs_cmp_case_n(cnttype + _X, _Y("pkcs7-mime")) ||
          !su_cs_cmp_case_n(cnttype + _X, _Y("x-pkcs7-mime")))) {
#undef _Y
#undef _X
      if ((x = smime_decrypt(m, to, cc, TRU1)) == NULL)
         goto jleave;
      if (x != (struct message*)-1) {
         m = x;
         goto jloop;
      }
   }
   size = m->m_size;

   if((fp = mx_fs_tmp_open(NIL, "smimecert", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
            NIL)) == NIL){
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush(fp);

   rewind(fp);
   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err("Error creating BIO object for message %d", n);
      mx_fs_close(fp);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object for message %d"), n);
      BIO_free(fb);
      mx_fs_close(fp);
      goto jleave;
   }
   BIO_free(fb);
   mx_fs_close(fp);

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      n_err(_("No certificates found in message %d\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      if (X509_print_fp(op, cert) == 0 || PEM_write_X509(op, cert) == 0) {
         ssl_gen_err(_("Error writing certificate %d from message %d"),
            i, n);
         goto jleave;
      }
   }
   rv = OKAY;
jleave:
   if(pkcs7 != NULL)
      PKCS7_free(pkcs7);
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_XTLS */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_XTLS
/* s-it-mode */
