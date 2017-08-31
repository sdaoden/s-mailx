/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ TLS/SSL functions. TODO this needs an overhaul -- there _are_ stack leaks!?
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE xssl

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE()
#ifdef HAVE_XSSL
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

#ifdef HAVE_XSSL_CONFIG
# include <openssl/conf.h>
#endif

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
# include <dirent.h>
#endif

/*
 * OpenSSL client implementation according to: John Viega, Matt Messier,
 * Pravir Chandra: Network Security with OpenSSL. Sebastopol, CA 2002.
 */

/* Update manual on changes (for all those)! */
#define n_XSSL_DISABLED_PROTOCOLS "-SSLv2"

#ifndef HAVE_XSSL_CONF_CTX
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

  /* SSL_CONF_CTX and _OP_NO_SSL_MASK were both introduced with 1.0.2!?! */
# ifndef SSL_OP_NO_SSL_MASK
#  define SSL_OP_NO_SSL_MASK \
   (SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |\
   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2)
# endif
#endif

#ifdef HAVE_XSSL_STACK_OF
# define n_XSSL_STACKOF(X) STACK_OF(X)
#else
# define n_XSSL_STACKOF(X) /*X*/STACK
#endif

#if OPENSSL_VERSION_NUMBER + 0 >= 0x0090581fL
# define a_XSSL_RAND_LOAD_FILE_MAXBYTES -1
#else
# define a_XSSL_RAND_LOAD_FILE_MAXBYTES 1024
#endif

/* Compatibility sighs (that sigh is _really_ a cute one) */
#if HAVE_XSSL_OPENSSL >= 0x10100
# define a_xssl_X509_get_notBefore X509_get0_notBefore
# define a_xssl_X509_get_notAfter X509_get0_notAfter
#else
# define a_xssl_X509_get_notBefore X509_get_notBefore
# define a_xssl_X509_get_notAfter X509_get_notAfter
#endif

/* X509_STORE_set_flags */
#undef a_XSSL_X509_V_ANY
#ifndef X509_V_FLAG_NO_ALT_CHAINS
# define X509_V_FLAG_NO_ALT_CHAINS -1
#else
# undef a_XSSL_X509_V_ANY
# define a_XSSL_X509_V_ANY
#endif
#ifndef X509_V_FLAG_NO_CHECK_TIME
# define X509_V_FLAG_NO_CHECK_TIME -1
#else
# undef a_XSSL_X509_V_ANY
# define a_XSSL_X509_V_ANY
#endif
#ifndef X509_V_FLAG_PARTIAL_CHAIN
# define X509_V_FLAG_PARTIAL_CHAIN -1
#else
# undef a_XSSL_X509_V_ANY
# define a_XSSL_X509_V_ANY
#endif
#ifndef X509_V_FLAG_X509_STRICT
# define X509_V_FLAG_X509_STRICT -1
#else
# undef a_XSSL_X509_V_ANY
# define a_XSSL_X509_V_ANY
#endif
#ifndef X509_V_FLAG_TRUSTED_FIRST
# define X509_V_FLAG_TRUSTED_FIRST -1
#else
# undef a_XSSL_X509_V_ANY
# define a_XSSL_X509_V_ANY
#endif

enum a_xssl_state{
   a_XSSL_S_INIT = 1u<<0,
   a_XSSL_S_RAND_INIT = 1u<<1,
   a_XSSL_S_CONF_LOAD = 1u<<2,

#if HAVE_XSSL_OPENSSL < 0x10100
   a_XSSL_S_EXIT_HDL = 1u<<8,
   a_XSSL_S_ALGO_LOAD = 1u<<9,
#endif

   a_XSSL_S_VERIFY_ERROR = 1u<<16
};

/* We go for the OpenSSL v1.0.2+ SSL_CONF_CTX if available even if
 * the library does internally what we'd otherwise do ourselfs.
 * But eventually we can drop the direct use cases */
enum a_xssl_conf_type{
   a_XSSL_CT_CERTIFICATE,
   a_XSSL_CT_CIPHER_STRING,
   a_XSSL_CT_CURVES,
   a_XSSL_CT_PRIVATE_KEY,
   a_XSSL_CT_OPTIONS,
   a_XSSL_CT_PROTOCOL
};

struct ssl_method { /* TODO v15 obsolete */
   char const  sm_name[8];
   char const  sm_map[16];
};

#ifndef HAVE_XSSL_CONF_CTX
struct a_xssl_protocol{
   char const *sp_name;
   sl_i sp_flag;
};
#endif

struct a_xssl_smime_cipher{
   char const sc_name[8];
   EVP_CIPHER const *(*sc_fun)(void);
};

struct a_xssl_smime_digest{
   char const sd_name[8];
   EVP_MD const *(*sd_fun)(void);
};

struct a_xssl_x509_v_flags{
   char const xvf_name[20];
   si32_t xvf_flag;
};

/* Supported SSL/TLS methods: update manual on change! */

static struct ssl_method const   _ssl_methods[] = { /* TODO obsolete */
   {"auto",    "ALL,-SSLv2"},
   {"ssl3",    "-ALL,SSLv3"},
   {"tls1",    "-ALL,TLSv1"},
   {"tls1.1",  "-ALL,TLSv1.1"},
   {"tls1.2",  "-ALL,TLSv1.2"}
};

/* Update manual on change! */
#ifndef HAVE_XSSL_CONF_CTX
static struct a_xssl_protocol const a_xssl_protocols[] = {
   {"ALL", SSL_OP_NO_SSL_MASK},
   {"TLSv1.2", SSL_OP_NO_TLSv1_2},
   {"TLSv1.1", SSL_OP_NO_TLSv1_1},
   {"TLSv1", SSL_OP_NO_TLSv1},
   {"SSLv3", SSL_OP_NO_SSLv3},
   {"SSLv2", 0}
};
#endif

/* Supported S/MIME cipher algorithms */
static struct a_xssl_smime_cipher const a_xssl_smime_ciphers[] = { /* Manual! */
#ifndef OPENSSL_NO_AES
# define a_XSSL_SMIME_DEFAULT_CIPHER EVP_aes_128_cbc /* According to RFC 5751 */
   {"aes128", &EVP_aes_128_cbc},
   {"aes256", &EVP_aes_256_cbc},
   {"aes192", &EVP_aes_192_cbc},
#endif
#ifndef OPENSSL_NO_DES
# ifndef a_XSSL_SMIME_DEFAULT_CIPHER
#  define a_XSSL_SMIME_DEFAULT_CIPHER EVP_des_ede3_cbc
# endif
   {"des3", &EVP_des_ede3_cbc},
   {"des", &EVP_des_cbc},
#endif
};
#ifndef a_XSSL_SMIME_DEFAULT_CIPHER
# error Your OpenSSL library does not include the necessary
# error cipher algorithms that are required to support S/MIME
#endif

#ifndef OPENSSL_NO_AES
/* TODO obsolete a_xssl_smime_ciphers_obs */
static struct a_xssl_smime_cipher const a_xssl_smime_ciphers_obs[] = {
   {"aes-128", &EVP_aes_128_cbc},
   {"aes-256", &EVP_aes_256_cbc},
   {"aes-192", &EVP_aes_192_cbc}
};
#endif

/* Supported S/MIME message digest algorithms */
static struct a_xssl_smime_digest const a_xssl_smime_digests[] = { /* Manual! */
#define a_XSSL_SMIME_DEFAULT_DIGEST EVP_sha1 /* According to RFC 5751 */
#define a_XSSL_SMIME_DEFAULT_DIGEST_S  "sha1"
   {"sha1", &EVP_sha1},
   {"sha256", &EVP_sha256},
   {"sha512", &EVP_sha512},
   {"sha384", &EVP_sha384},
   {"sha224", &EVP_sha224},
#ifndef OPENSSL_NO_MD5
   {"md5", &EVP_md5},
#endif
};

/* X509_STORE_set_flags() for *{smime,ssl}-ca-flags* */
static struct a_xssl_x509_v_flags const a_xssl_x509_v_flags[] = { /* Manual! */
   {"no-alt-chains", X509_V_FLAG_NO_ALT_CHAINS},
   {"no-check-time", X509_V_FLAG_NO_CHECK_TIME},
   {"partial-chain", X509_V_FLAG_PARTIAL_CHAIN},
   {"strict", X509_V_FLAG_X509_STRICT},
   {"trusted-first", X509_V_FLAG_TRUSTED_FIRST},
};

static enum a_xssl_state a_xssl_state;
static size_t a_xssl_msgno;

static bool_t a_xssl_rand_init(void);
#if HAVE_XSSL_OPENSSL < 0x10100
# ifdef HAVE_SSL_ALL_ALGORITHMS
static void a_xssl__load_algos(void);
#  define a_xssl_load_algos a_xssl__load_algos
# endif
# if defined HAVE_XSSL_CONFIG || defined HAVE_SSL_ALL_ALGORITHMS
static void a_xssl_atexit(void);
# endif
#endif
#ifndef a_xssl_load_algos
# define a_xssl_load_algos() do{;}while(0)
#endif

static bool_t     _ssl_parse_asn1_time(ASN1_TIME const *atp,
                     char *bdat, size_t blen);
static int        _ssl_verify_cb(int success, X509_STORE_CTX *store);

/* *smime-ca-flags*, *ssl-ca-flags* */
static void a_xssl_ca_flags(X509_STORE *store, char const *flags);

/* SSL_CTX configuration */
static void *     _ssl_conf_setup(SSL_CTX *ctxp);
static bool_t     _ssl_conf(void *confp, enum a_xssl_conf_type ct,
                     char const *value);
static bool_t     _ssl_conf_finish(void **confp, bool_t error);

static bool_t     _ssl_load_verifications(SSL_CTX *ctxp);

static enum okay  ssl_check_host(struct sock *sp, struct url const *urlp);

static int        smime_verify(struct message *m, int n,
                     n_XSSL_STACKOF(X509) *chain, X509_STORE *store);
static EVP_CIPHER const * _smime_cipher(char const *name);
static int        ssl_password_cb(char *buf, int size, int rwflag,
                     void *userdata);
static FILE *     smime_sign_cert(char const *xname, char const *xname2,
                     bool_t dowarn, char const **match);
static char const * _smime_sign_include_certs(char const *name);
static bool_t     _smime_sign_include_chain_creat(n_XSSL_STACKOF(X509) **chain,
                     char const *cfiles, char const *addr);
static EVP_MD const * _smime_sign_digest(char const *name,
                        char const **digname);
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay  load_crl1(X509_STORE *store, char const *name);
#endif
static enum okay  load_crls(X509_STORE *store, enum okeys fok, enum okeys dok);

static bool_t
a_xssl_rand_init(void){
#define a_XSSL_RAND_ENTROPY 32
   char b64buf[a_XSSL_RAND_ENTROPY * 5 +1], *randfile;
   char const *cp, *x;
   bool_t err;
   NYD2_ENTER;

   err = TRU1;
   randfile = NULL;

   /* Shall use some external daemon? */ /* TODO obsolete *ssl_rand_egd* */
   if((cp = ok_vlook(ssl_rand_egd)) != NULL){ /* TODO no one supports it now! */
      n_OBSOLETE(_("all *SSL libraries removed *ssl-rand-egd* support"));
#ifdef HAVE_XSSL_RAND_EGD
      if((x = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) != NULL &&
            RAND_egd(cp = x) != -1){
         err = FAL0;
         goto jleave;
      }
      n_err(_("*ssl_rand_egd* daemon at %s not available\n"),
         n_shexp_quote_cp(cp, FAL0));
#else
      if(n_poption & n_PO_D_VV)
         n_err(_("*ssl_rand_egd* (%s): unsupported by SSL library\n"),
            n_shexp_quote_cp(cp, FAL0));
#endif
   }

   /* Prefer possible user setting */
   if((cp = ok_vlook(ssl_rand_file)) != NULL){
      x = NULL;
      if(*cp != '\0'){
         if((x = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL)
            n_err(_("*ssl-rand-file*: expansion of %s failed "
                  "(using OpenSSL default)\n"),
               n_shexp_quote_cp(cp, FAL0));
      }
      cp = x;
   }
   if(cp == NULL){
      randfile = n_lofi_alloc(PATH_MAX);
      if((cp = RAND_file_name(randfile, PATH_MAX)) == NULL){
         n_err(_("*ssl-rand-file*: no SSL entropy file, can't seed PRNG\n"));
         goto jleave;
      }
   }

   (void)RAND_load_file(cp, a_XSSL_RAND_LOAD_FILE_MAXBYTES);

   /* And feed in some data, then write the updated file.
    * While this rather feeds the PRNG with itself in the n_RANDOM_USE_XSSL
    * case, let us stir the buffer a little bit.
    * Estimate a low but likely still too high number of entropy bytes, use
    * 20%: base64 uses 3 input = 4 output bytes relation, and the base64
    * alphabet is a 6 bit one */
   n_LCTAV(n_RANDOM_USE_XSSL == 0 || n_RANDOM_USE_XSSL == 1);
   for(x = (char*)-1;;){
      RAND_add(n_random_create_buf(b64buf, sizeof(b64buf) -1, NULL),
         sizeof(b64buf) -1, a_XSSL_RAND_ENTROPY);
      if((x = (char*)((uintptr_t)x >> (1 + (n_RANDOM_USE_XSSL * 3)))) == NULL){
         err = (RAND_status() == 0);
         break;
      }
#if !n_RANDOM_USE_XSSL
      if(!(err = (RAND_status() == 0)))
         break;
#endif
   }

   if(!err)
      err = (RAND_write_file(cp) == -1);

jleave:
   if(randfile != NULL)
      n_lofi_free(randfile);
   if(err)
      n_panic(_("Cannot seed the *SSL PseudoRandomNumberGenerator, "
            "RAND_status() is 0!\n"
         "  Please set *ssl-rand-file* to a file with sufficient entropy.\n"
         "  On a machine with entropy: "
            "\"$ dd if=/dev/urandom of=FILE bs=1024 count=1\"\n"));
   NYD2_LEAVE;
   return err;
}

static void
a_xssl_init(void)
{
#ifdef HAVE_XSSL_CONFIG
   char const *cp;
#endif
   NYD_ENTER;

   if(!(a_xssl_state & a_XSSL_S_INIT)){
#if HAVE_XSSL_OPENSSL >= 0x10100
      OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
         OPENSSL_INIT_LOAD_CRYPTO_STRINGS
# ifdef HAVE_SSL_ALL_ALGORITHMS
            | OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS
# endif
         , NULL);

#else /* OPENSSL >= 0x10100 */
      SSL_load_error_strings();
      SSL_library_init();
      a_xssl_load_algos();
#endif
      a_xssl_state |= a_XSSL_S_INIT;
   }

   /* Load openssl.cnf or whatever was given in *ssl-config-file* */
#ifdef HAVE_XSSL_CONFIG
   if(!(a_xssl_state & a_XSSL_S_CONF_LOAD) &&
         (cp = ok_vlook(ssl_config_file)) != NULL){
      ul_i flags = CONF_MFLAGS_IGNORE_MISSING_FILE;

      if(*cp == '\0'){
         cp = NULL;
         flags = 0;
      }
      if(CONF_modules_load_file(cp, n_uagent, flags) == 1){
         a_xssl_state |= a_XSSL_S_CONF_LOAD;
# if HAVE_XSSL_OPENSSL < 0x10100
         if(!(a_xssl_state & a_XSSL_S_EXIT_HDL)){
            a_xssl_state |= a_XSSL_S_EXIT_HDL;
            atexit(&a_xssl_atexit); /* TODO generic program-wide event mech. */
         }
# endif
      }else
         ssl_gen_err(_("Ignoring CONF_modules_load_file() load error"));
   }
#endif

   if(!(a_xssl_state & a_XSSL_S_RAND_INIT) && a_xssl_rand_init())
      a_xssl_state |= a_XSSL_S_RAND_INIT;
   NYD_LEAVE;
}

#if HAVE_XSSL_OPENSSL < 0x10100
# ifdef HAVE_SSL_ALL_ALGORITHMS
static void
a_xssl__load_algos(void){
   NYD2_ENTER;
   if(!(a_xssl_state & a_XSSL_S_ALGO_LOAD)){
      a_xssl_state |= a_XSSL_S_ALGO_LOAD;
      OpenSSL_add_all_algorithms();

      if(!(a_xssl_state & a_XSSL_S_EXIT_HDL)){
         a_xssl_state |= a_XSSL_S_EXIT_HDL;
         atexit(&a_xssl_atexit); /* TODO generic program-wide event mech. */
      }
   }
   NYD2_LEAVE;
}
# endif

# if defined HAVE_XSSL_CONFIG || defined HAVE_SSL_ALL_ALGORITHMS
static void
a_xssl_atexit(void){
   NYD2_ENTER;
#  ifdef HAVE_XSSL_CONFIG
   if(a_xssl_state & a_XSSL_S_CONF_LOAD)
      CONF_modules_free();
#  endif

#  ifdef HAVE_SSL_ALL_ALGORITHMS
   if(a_xssl_state & a_XSSL_S_ALGO_LOAD)
      EVP_cleanup();
#  endif
   NYD2_LEAVE;
}
# endif
#endif /* HAVE_XSSL_OPENSSL < 0x10100 */

static bool_t
_ssl_parse_asn1_time(ASN1_TIME const *atp, char *bdat, size_t blen)
{
   BIO *mbp;
   char *mcp;
   long l;
   NYD_ENTER;

   mbp = BIO_new(BIO_s_mem());

   if (ASN1_TIME_print(mbp, atp) && (l = BIO_get_mem_data(mbp, &mcp)) > 0)
      snprintf(bdat, blen, "%.*s", (int)l, mcp);
   else {
      snprintf(bdat, blen, _("Bogus certificate date: %.*s"),
         /*is (int)*/atp->length, (char const*)atp->data);
      mcp = NULL;
   }

   BIO_free(mbp);
   NYD_LEAVE;
   return (mcp != NULL);
}

static int
_ssl_verify_cb(int success, X509_STORE_CTX *store)
{
   char data[256];
   X509 *cert;
   int rv = TRU1;
   NYD_ENTER;

   if (success && !(n_poption & n_PO_D_V))
      goto jleave;

   if (a_xssl_msgno != 0) {
      n_err(_("Message %lu:\n"), (ul_i)a_xssl_msgno);
      a_xssl_msgno = 0;
   }
   n_err(_(" Certificate depth %d %s\n"),
      X509_STORE_CTX_get_error_depth(store), (success ? n_empty : V_(n_error)));

   if ((cert = X509_STORE_CTX_get_current_cert(store)) != NULL) {
      X509_NAME_oneline(X509_get_subject_name(cert), data, sizeof data);
      n_err(_("  subject = %s\n"), data);

      _ssl_parse_asn1_time(a_xssl_X509_get_notBefore(cert), data, sizeof data);
      n_err(_("  notBefore = %s\n"), data);

      _ssl_parse_asn1_time(a_xssl_X509_get_notAfter(cert), data, sizeof data);
      n_err(_("  notAfter = %s\n"), data);

      X509_NAME_oneline(X509_get_issuer_name(cert), data, sizeof data);
      n_err(_("  issuer = %s\n"), data);
   }

   if (!success) {
      int err = X509_STORE_CTX_get_error(store);

      n_err(_("  err %i: %s\n"), err, X509_verify_cert_error_string(err));
      a_xssl_state |= a_XSSL_S_VERIFY_ERROR;
   }

   if (!success && ssl_verify_decide() != OKAY)
      rv = FAL0;
jleave:
   NYD_LEAVE;
   return rv;
}

static void
a_xssl_ca_flags(X509_STORE *store, char const *flags){
   NYD2_ENTER;
   if(flags != NULL){
      char *iolist, *cp;

      iolist = savestr(flags);
jouter:
      while((cp = n_strsep(&iolist, ',', TRU1)) != NULL){
         struct a_xssl_x509_v_flags const *xvfp;

         for(xvfp = &a_xssl_x509_v_flags[0];
               xvfp < &a_xssl_x509_v_flags[n_NELEM(a_xssl_x509_v_flags)];
               ++xvfp)
            if(!asccasecmp(cp, xvfp->xvf_name)){
               if(xvfp->xvf_flag != -1){
#ifdef a_XSSL_X509_V_ANY
                  X509_STORE_set_flags(store, xvfp->xvf_flag);
#endif
               }else if(n_poption & n_PO_D_V)
                  n_err(_("*{smime,ssl}-ca-flags*: "
                     "directive not supported: %s\n"), cp);
               goto jouter;
            }
         n_err(_("*{smime,ssl}-ca-flags*: invalid directive: %s\n"), cp);
      }
   }
   NYD2_LEAVE;
}

#ifdef HAVE_XSSL_CONF_CTX
static void *
_ssl_conf_setup(SSL_CTX *ctxp)
{
   SSL_CONF_CTX *sccp;
   NYD_ENTER;

   if ((sccp = SSL_CONF_CTX_new()) != NULL) {
      SSL_CONF_CTX_set_flags(sccp,
         SSL_CONF_FLAG_FILE | SSL_CONF_FLAG_CLIENT |
         SSL_CONF_FLAG_CERTIFICATE | SSL_CONF_FLAG_SHOW_ERRORS);

      SSL_CONF_CTX_set_ssl_ctx(sccp, ctxp);
   } else
      ssl_gen_err(_("SSL_CONF_CTX_new() failed"));

   NYD_LEAVE;
   return sccp;
}

static bool_t
_ssl_conf(void *confp, enum a_xssl_conf_type ct, char const *value)
{
   int rv;
   char const *cmsg;
   SSL_CONF_CTX *sccp = (SSL_CONF_CTX*)confp;
   NYD_ENTER;

   switch (ct) {
   case a_XSSL_CT_CERTIFICATE:
      cmsg = "ssl-cert";
      rv = SSL_CONF_cmd(sccp, "Certificate", value);
      break;
   case a_XSSL_CT_CIPHER_STRING:
      cmsg = "ssl-cipher-list";
      rv = SSL_CONF_cmd(sccp, "CipherString", value);
      break;
   case a_XSSL_CT_PRIVATE_KEY:
      cmsg = "ssl-key";
      rv = SSL_CONF_cmd(sccp, "PrivateKey", value);
      break;
   default:
   case a_XSSL_CT_OPTIONS:
      cmsg = "ssl-options";
      rv = SSL_CONF_cmd(sccp, "Options", "Bugs");
      break;
   case a_XSSL_CT_PROTOCOL:
      cmsg = "ssl-protocol";
      rv = SSL_CONF_cmd(sccp, "Protocol", value);
      break;
   case a_XSSL_CT_CURVES:
      cmsg = "ssl-curves";
      rv = SSL_CONF_cmd(sccp, "Curves", value);
      break;
   }

   if (rv == 2)
      rv = 0;
   else {
      if (rv == 0)
         ssl_gen_err(_("SSL_CONF_CTX_cmd() failed for *%s*"), cmsg);
      else
         n_err(_("%s: *%s* implementation error, please report this\n"),
            n_uagent, cmsg);
      rv = 1;
   }

   NYD_LEAVE;
   return (rv == 0);
}

static bool_t
_ssl_conf_finish(void **confp, bool_t error)
{
   SSL_CONF_CTX **sccp = (SSL_CONF_CTX**)confp;
   bool_t rv;
   NYD_ENTER;

   if (!(rv = error))
      rv = (SSL_CONF_CTX_finish(*sccp) != 0);

   SSL_CONF_CTX_free(*sccp);

   *sccp = NULL;
   NYD_LEAVE;
   return rv;
}

#else /* HAVE_XSSL_CONF_CTX */
static void *
_ssl_conf_setup(SSL_CTX* ctxp)
{
   return ctxp;
}

static bool_t
_ssl_conf(void *confp, enum a_xssl_conf_type ct, char const *value){
   SSL_CTX *ctxp;
   NYD2_ENTER;

   ctxp = confp;

   switch(ct){
   case a_XSSL_CT_CERTIFICATE:
      if(SSL_CTX_use_certificate_chain_file(ctxp, value) != 1){
         ssl_gen_err(_("Can't load certificate from file %s\n"),
            n_shexp_quote_cp(value, FAL0));
         confp = NULL;
      }
      break;
   case a_XSSL_CT_CIPHER_STRING:
      if(SSL_CTX_set_cipher_list(ctxp, value) != 1){
         ssl_gen_err(_("Invalid cipher string: %s\n"), value);
         confp = NULL;
      }
      break;
   case a_XSSL_CT_CURVES:
#ifdef SSL_CTRL_SET_CURVES_LIST
      if(SSL_CTX_set1_curves_list(ctxp, value) != 1){
         ssl_gen_err(_("Invalid curves string: %s\n"), value);
         confp = NULL;
      }
#else
      n_err(_("*ssl-curves*: as such not supported\n"));
      confp = NULL;
#endif
      break;
   case a_XSSL_CT_PRIVATE_KEY:
      if(SSL_CTX_use_PrivateKey_file(ctxp, value, SSL_FILETYPE_PEM) != 1){
         ssl_gen_err(_("Can't load private key from file %s\n"),
            n_shexp_quote_cp(value, FAL0));
         confp = NULL;
      }
      break;
   case a_XSSL_CT_OPTIONS:
      /* "Options"="Bugs" TODO *ssl-options* */
      SSL_CTX_set_options(ctxp, SSL_OP_ALL);
      break;
   case a_XSSL_CT_PROTOCOL:{
      char *iolist, *cp, addin;
      size_t i;
      sl_i opts = 0;

      confp = NULL;
      for(iolist = cp = savestr(value);
            (cp = n_strsep(&iolist, ',', FAL0)) != NULL;){
         if(*cp == '\0'){
            n_err(_("*ssl-protocol*: empty arguments are not supported\n"));
            goto jleave;
         }

         addin = TRU1;
         switch(cp[0]){
         case '-': addin = FAL0; /* FALLTHRU */
         case '+': ++cp; /* FALLTHRU */
         default : break;
         }

         for(i = 0;;){
            if(!asccasecmp(cp, a_xssl_protocols[i].sp_name)){
               /* We need to inverse the meaning of the _NO_s */
               if(!addin)
                  opts |= a_xssl_protocols[i].sp_flag;
               else
                  opts &= ~a_xssl_protocols[i].sp_flag;
               break;
            }
            if(++i < n_NELEM(a_xssl_protocols))
               continue;
            n_err(_("*ssl-protocol*: unsupported value: %s\n"), cp);
            goto jleave;
         }
      }
      confp = ctxp;
      SSL_CTX_set_options(ctxp, opts);
   }  break;
   }
jleave:
   NYD2_LEAVE;
   return (confp != NULL);
}

static bool_t
_ssl_conf_finish(void **confp, bool_t error)
{
   n_UNUSED(confp);
   n_UNUSED(error);
   return TRU1;
}
#endif /* !HAVE_XSSL_CONF_CTX */

static bool_t
_ssl_load_verifications(SSL_CTX *ctxp)
{
   char *ca_dir, *ca_file;
   X509_STORE *store;
   bool_t rv = FAL0;
   NYD_ENTER;

   if (ssl_verify_level == SSL_VERIFY_IGNORE) {
      rv = TRU1;
      goto jleave;
   }

   if ((ca_dir = ok_vlook(ssl_ca_dir)) != NULL)
      ca_dir = fexpand(ca_dir, FEXP_LOCAL | FEXP_NOPROTO);
   if ((ca_file = ok_vlook(ssl_ca_file)) != NULL)
      ca_file = fexpand(ca_file, FEXP_LOCAL | FEXP_NOPROTO);

   if ((ca_dir != NULL || ca_file != NULL) &&
         SSL_CTX_load_verify_locations(ctxp, ca_file, ca_dir) != 1) {
      char const *m1, *m2, *m3;

      if (ca_dir != NULL) {
         m1 = ca_dir;
         m2 = (ca_file != NULL) ? _(" or ") : n_empty;
      } else
         m1 = m2 = n_empty;
      m3 = (ca_file != NULL) ? ca_file : n_empty;
      ssl_gen_err(_("Error loading %s%s%s\n"), m1, m2, m3);
      goto jleave;
   }

   /* C99 */{
      bool_t xv15;

      if((xv15 = ok_blook(ssl_no_default_ca)))
         n_OBSOLETE(_("please use *ssl-ca-no-defaults*, "
            "not *ssl-no-default-ca*"));
      if(!ok_blook(ssl_ca_no_defaults) && !xv15 &&
            SSL_CTX_set_default_verify_paths(ctxp) != 1) {
         ssl_gen_err(_("Error loading built-in default CA locations\n"));
         goto jleave;
      }
   }

   a_xssl_state &= ~a_XSSL_S_VERIFY_ERROR;
   a_xssl_msgno = 0;
   SSL_CTX_set_verify(ctxp, SSL_VERIFY_PEER, &_ssl_verify_cb);
   store = SSL_CTX_get_cert_store(ctxp);
   load_crls(store, ok_v_ssl_crl_file, ok_v_ssl_crl_dir);
   a_xssl_ca_flags(store, ok_vlook(ssl_ca_flags));

   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
ssl_check_host(struct sock *sp, struct url const *urlp)
{
   char data[256];
   X509 *cert;
   n_XSSL_STACKOF(GENERAL_NAME) *gens;
   GENERAL_NAME *gen;
   X509_NAME *subj;
   enum okay rv = STOP;
   NYD_ENTER;

   if ((cert = SSL_get_peer_certificate(sp->s_ssl)) == NULL) {
      n_err(_("No certificate from: %s\n"), urlp->url_h_p.s);
      goto jleave;
   }

   gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
   if (gens != NULL) {
      int i;

      for (i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
         gen = sk_GENERAL_NAME_value(gens, i);
         if (gen->type == GEN_DNS) {
            if (n_poption & n_PO_D_V)
               n_err(_("Comparing subject_alt_name: need<%s> is<%s>\n"),
                  urlp->url_host.s, (char*)gen->d.ia5->data);
            rv = rfc2595_hostname_match(urlp->url_host.s,
                  (char*)gen->d.ia5->data);
            if (rv == OKAY)
               goto jdone;
         }
      }
   }

   if ((subj = X509_get_subject_name(cert)) != NULL &&
         X509_NAME_get_text_by_NID(subj, NID_commonName, data, sizeof data)
            > 0) {
      data[sizeof data - 1] = '\0';
      if (n_poption & n_PO_D_V)
         n_err(_("Comparing commonName: need<%s> is<%s>\n"),
            urlp->url_host.s, data);
      rv = rfc2595_hostname_match(urlp->url_host.s, data);
   }

jdone:
   X509_free(cert);
jleave:
   NYD_LEAVE;
   return rv;
}

static int
smime_verify(struct message *m, int n, n_XSSL_STACKOF(X509) *chain,
   X509_STORE *store)
{
   char data[LINESIZE], *sender, *to, *cc, *cnttype;
   int rv, c, i, j;
   struct message *x;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   n_XSSL_STACKOF(X509) *certs;
   n_XSSL_STACKOF(GENERAL_NAME) *gens;
   X509 *cert;
   X509_NAME *subj;
   GENERAL_NAME *gen;
   NYD_ENTER;

   rv = 1;
   fp = NULL;
   fb = pb = NULL;
   pkcs7 = NULL;
   certs = NULL;
   a_xssl_state &= ~a_XSSL_S_VERIFY_ERROR;
   a_xssl_msgno = (size_t)n;

   for (;;) {
      sender = getsender(m);
      to = hfield1("to", m);
      cc = hfield1("cc", m);
      cnttype = hfield1("content-type", m);

#undef _X
#undef _Y
#define _X     (sizeof("application/") -1)
#define _Y(X)  X, sizeof(X) -1
      if (cnttype && is_asccaseprefix("application/", cnttype) &&
            (!ascncasecmp(cnttype + _X, _Y("pkcs7-mime")) ||
             !ascncasecmp(cnttype + _X, _Y("x-pkcs7-mime")))) {
#undef _Y
#undef _X
         if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
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

   if ((fp = Ftmp(NULL, "smimever", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL) {
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
               if (!asccasecmp((char*)gen->d.ia5->data, sender))
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
         if (!asccasecmp(data, sender))
            goto jfound;
      }
   }
   n_err(_("Message %d: certificate does not match <%s>\n"), n, sender);
   goto jleave;
jfound:
   rv = ((a_xssl_state & a_XSSL_S_VERIFY_ERROR) != 0);
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
   if (fp != NULL)
      Fclose(fp);
   NYD_LEAVE;
   return rv;
}

static EVP_CIPHER const *
_smime_cipher(char const *name)
{
   EVP_CIPHER const *cipher;
   char *vn;
   char const *cp;
   size_t i;
   NYD_ENTER;

   vn = ac_alloc(i = strlen(name) + sizeof("smime-cipher-") -1 +1);
   snprintf(vn, (int)i, "smime-cipher-%s", name);
   cp = n_var_vlook(vn, FAL0);
   ac_free(vn);

   if (cp == NULL && (cp = ok_vlook(smime_cipher)) == NULL) {
      cipher = a_XSSL_SMIME_DEFAULT_CIPHER();
      goto jleave;
   }
   cipher = NULL;

   for (i = 0; i < n_NELEM(a_xssl_smime_ciphers); ++i)
      if (!asccasecmp(a_xssl_smime_ciphers[i].sc_name, cp)) {
         cipher = (*a_xssl_smime_ciphers[i].sc_fun)();
         goto jleave;
      }
#ifndef OPENSSL_NO_AES
   for (i = 0; i < n_NELEM(a_xssl_smime_ciphers_obs); ++i) /* TODO obsolete */
      if (!asccasecmp(a_xssl_smime_ciphers_obs[i].sc_name, cp)) {
         n_OBSOLETE2(_("*smime-cipher* names with hyphens will vanish"), cp);
         cipher = (*a_xssl_smime_ciphers_obs[i].sc_fun)();
         goto jleave;
      }
#endif

   /* Not a built-in algorithm, but we may have dynamic support for more */
#ifdef HAVE_SSL_ALL_ALGORITHMS
   if((cipher = EVP_get_cipherbyname(cp)) != NULL)
      goto jleave;
#endif

   n_err(_("Invalid S/MIME cipher(s): %s\n"), cp);
jleave:
   NYD_LEAVE;
   return cipher;
}

static int
ssl_password_cb(char *buf, int size, int rwflag, void *userdata)
{
   char *pass;
   size_t len;
   NYD_ENTER;
   n_UNUSED(rwflag);
   n_UNUSED(userdata);

   /* New-style */
   if(userdata != NULL){
      struct url url;
      struct ccred cred;

      if(url_parse(&url, CPROTO_CCRED, userdata)){
         if(ccred_lookup(&cred, &url)){
            ssize_t slen;

            if((slen = n_strscpy(buf, cred.cc_pass.s, size)) >= 0){
               size = (int)slen;
               goto jleave;
            }
         }
         size = 0;
         goto jleave;
      }
   }

   /* Old-style */
   if ((pass = getpassword("PEM pass phrase:")) != NULL) {
      len = strlen(pass);
      if (UICMP(z, len, >=, size))
         len = size -1;
      memcpy(buf, pass, len);
      buf[len] = '\0';
      size = (int)len;
   } else
      size = 0;
jleave:
   NYD_LEAVE;
   return size;
}

static FILE *
smime_sign_cert(char const *xname, char const *xname2, bool_t dowarn,
   char const **match)
{
   char *vn;
   int vs;
   struct name *np;
   char const *name = xname, *name2 = xname2, *cp;
   FILE *fp = NULL;
   NYD_ENTER;

jloop:
   if (name) {
      np = lextract(name, GTO | GSKIN);
      while (np != NULL) {
         /* This needs to be more intelligent since it will currently take the
          * first name for which a private key is available regardless of
          * whether it is the right one for the message */
         vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-cert-%s", np->n_name);
         cp = n_var_vlook(vn, FAL0);
         ac_free(vn);
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

   if ((cp = ok_vlook(smime_sign_cert)) == NULL)
      goto jerr;
   if(match != NULL)
      *match = NULL;
jopen:
   if ((cp = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL)
      goto jleave;
   if ((fp = Fopen(cp, "r")) == NULL)
      n_perr(cp, 0);
jleave:
   NYD_LEAVE;
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
   NYD_ENTER;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if (name != NULL) {
      struct name *np;

      for (np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink) {
         int vs;
         char *vn;

         vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-include-certs-%s", np->n_name);
         rv = n_var_vlook(vn, FAL0);
         ac_free(vn);
         if (rv != NULL)
            goto jleave;
      }
   }
   rv = ok_vlook(smime_sign_include_certs);
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_smime_sign_include_chain_creat(n_XSSL_STACKOF(X509) **chain,
   char const *cfiles, char const *addr)
{
   X509 *tmp;
   FILE *fp;
   char *nfield, *cfield, *x;
   NYD_ENTER;

   *chain = sk_X509_new_null();

   for (nfield = savestr(cfiles);
         (cfield = n_strsep(&nfield, ',', TRU1)) != NULL;) {
      if ((x = fexpand(cfield, FEXP_LOCAL | FEXP_NOPROTO)) == NULL ||
            (fp = Fopen(cfield = x, "r")) == NULL) {
         n_perr(cfiles, 0);
         goto jerr;
      }
      if ((tmp = PEM_read_X509(fp, NULL, &ssl_password_cb, n_UNCONST(addr))
            ) == NULL) {
         ssl_gen_err(_("Error reading certificate from %s"),
            n_shexp_quote_cp(cfield, FAL0));
         Fclose(fp);
         goto jerr;
      }
      sk_X509_push(*chain, tmp);
      Fclose(fp);
   }

   if (sk_X509_num(*chain) == 0) {
      n_err(_("*smime-sign-include-certs* defined but empty\n"));
      goto jerr;
   }
jleave:
   NYD_LEAVE;
   return (*chain != NULL);
jerr:
   sk_X509_pop_free(*chain, X509_free);
   *chain = NULL;
   goto jleave;
}

static EVP_MD const *
_smime_sign_digest(char const *name, char const **digname)
{
   EVP_MD const *digest;
   char const *cp;
   size_t i;
   NYD_ENTER;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if (name != NULL) {
      struct name *np;

      for (np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink) {
         int vs;
         char *vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-message-digest-%s", np->n_name);
         cp = n_var_vlook(vn, FAL0);
         ac_free(vn);
         if (cp != NULL)
            goto jhave_name;
      }
   }

   if ((cp = ok_vlook(smime_sign_message_digest)) == NULL) {
      digest = a_XSSL_SMIME_DEFAULT_DIGEST();
      *digname = a_XSSL_SMIME_DEFAULT_DIGEST_S;
      goto jleave;
   }

jhave_name:
   i = strlen(cp);
   {  char *x = salloc(i +1);
      i_strcpy(x, cp, i +1);
      cp = x;
   }
   *digname = cp;

   for (i = 0; i < n_NELEM(a_xssl_smime_digests); ++i)
      if (!asccasecmp(a_xssl_smime_digests[i].sd_name, cp)) {
         digest = (*a_xssl_smime_digests[i].sd_fun)();
         goto jleave;
      }

   /* Not a built-in algorithm, but we may have dynamic support for more */
#ifdef HAVE_SSL_ALL_ALGORITHMS
   if((digest = EVP_get_digestbyname(cp)) != NULL)
      goto jleave;
#endif

   n_err(_("Invalid message digest: %s\n"), cp);
   digest = NULL;
jleave:
   NYD_LEAVE;
   return digest;
}

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay
load_crl1(X509_STORE *store, char const *name)
{
   X509_LOOKUP *lookup;
   enum okay rv = STOP;
   NYD_ENTER;

   if (n_poption & n_PO_D_V)
      n_err(_("Loading CRL from %s\n"), n_shexp_quote_cp(name, FAL0));
   if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL) {
      ssl_gen_err(_("Error creating X509 lookup object"));
      goto jleave;
   }
   if (X509_load_crl_file(lookup, name, X509_FILETYPE_PEM) != 1) {
      ssl_gen_err(_("Error loading CRL from %s"), n_shexp_quote_cp(name, FAL0));
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* new OpenSSL */

static enum okay
load_crls(X509_STORE *store, enum okeys fok, enum okeys dok)
{
   char *crl_file, *crl_dir;
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   DIR *dirp;
   struct dirent *dp;
   char *fn = NULL;
   int fs = 0, ds, es;
#endif
   enum okay rv = STOP;
   NYD_ENTER;

   if ((crl_file = n_var_oklook(fok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      if ((crl_file = fexpand(crl_file, FEXP_LOCAL | FEXP_NOPROTO)) == NULL ||
            load_crl1(store, crl_file) != OKAY)
         goto jleave;
#else
      n_err(_("This OpenSSL version is too old to use CRLs\n"));
      goto jleave;
#endif
   }

   if ((crl_dir = n_var_oklook(dok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      char *x;
      if ((x = fexpand(crl_dir, FEXP_LOCAL | FEXP_NOPROTO)) == NULL ||
            (dirp = opendir(crl_dir = x)) == NULL) {
         n_perr(crl_dir, 0);
         goto jleave;
      }

      ds = strlen(crl_dir);
      fn = smalloc(fs = ds + 20);
      memcpy(fn, crl_dir, ds);
      fn[ds] = '/';
      while ((dp = readdir(dirp)) != NULL) {
         if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
               (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
            continue;
         if (dp->d_name[0] == '.')
            continue;
         if (ds + (es = strlen(dp->d_name)) + 2 < fs)
            fn = srealloc(fn, fs = ds + es + 20);
         memcpy(fn + ds + 1, dp->d_name, es + 1);
         if (load_crl1(store, fn) != OKAY) {
            closedir(dirp);
            free(fn);
            goto jleave;
         }
      }
      closedir(dirp);
      free(fn);
#else /* old OpenSSL */
      n_err(_("This OpenSSL version is too old to use CRLs\n"));
      goto jleave;
#endif
   }
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   if (crl_file || crl_dir)
      X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
         X509_V_FLAG_CRL_CHECK_ALL);
#endif
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

#if n_RANDOM_USE_XSSL
FL void
ssl_rand_bytes(void *buf, size_t blen){
   NYD_ENTER;

   if(!(a_xssl_state & a_XSSL_S_RAND_INIT) && a_xssl_rand_init())
      a_xssl_state |= a_XSSL_S_RAND_INIT;

   while(blen > 0){
      si32_t i;

      i = n_MIN(SI32_MAX, blen);
      blen -= i;
      RAND_bytes(buf, i);
      buf = (ui8_t*)buf + i;
   }
   NYD_LEAVE;
}
#endif

FL enum okay
ssl_open(struct url const *urlp, struct sock *sp)
{
   SSL_CTX *ctxp;
   void *confp;
   char const *cp, *cp_base;
   size_t i;
   enum okay rv = STOP;
   NYD_ENTER;

   a_xssl_init();

   ssl_set_verify_level(urlp);

   if ((ctxp = SSL_CTX_new(n_XSSL_CLIENT_METHOD())) == NULL) {
      ssl_gen_err(_("SSL_CTX_new() failed"));
      goto jleave;
   }

   /* Available with OpenSSL 0.9.6 or later */
#ifdef SSL_MODE_AUTO_RETRY
   SSL_CTX_set_mode(ctxp, SSL_MODE_AUTO_RETRY);
#endif

   if ((confp = _ssl_conf_setup(ctxp)) == NULL)
      goto jerr0;

   /* TODO obsolete Check for *ssl-method*, warp to a *ssl-protocol* value */
   if ((cp = xok_vlook(ssl_method, urlp, OXM_ALL)) != NULL) {
      n_OBSOLETE(_("please use *ssl-protocol* instead of *ssl-method*"));
      if (n_poption & n_PO_D_V)
         n_err(_("*ssl-method*: %s\n"), cp);
      for (i = 0;;) {
         if (!asccasecmp(_ssl_methods[i].sm_name, cp)) {
            cp = _ssl_methods[i].sm_map;
            break;
         }
         if (++i == n_NELEM(_ssl_methods)) {
            n_err(_("Unsupported TLS/SSL method: %s\n"), cp);
            goto jerr1;
         }
      }
   }
   /* *ssl-protocol* */
   if ((cp_base = xok_vlook(ssl_protocol, urlp, OXM_ALL)) != NULL) {
      if (n_poption & n_PO_D_V)
         n_err(_("*ssl-protocol*: %s\n"), cp_base);
      cp = cp_base;
   }
   cp = (cp != NULL ? savecatsep(cp, ',', n_XSSL_DISABLED_PROTOCOLS)
         : n_XSSL_DISABLED_PROTOCOLS);
   if (!_ssl_conf(confp, a_XSSL_CT_PROTOCOL, cp))
      goto jerr1;

   /* *ssl-cert* */
   if ((cp = xok_vlook(ssl_cert, urlp, OXM_ALL)) != NULL) {
      if (n_poption & n_PO_D_V)
         n_err(_("*ssl-cert* %s\n"), n_shexp_quote_cp(cp, FAL0));
      if ((cp_base = fexpand(cp, FEXP_LOCAL | FEXP_NOPROTO)) == NULL) {
         n_err(_("*ssl-cert* value expansion failed: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         goto jerr1;
      }
      cp = cp_base;
      if (!_ssl_conf(confp, a_XSSL_CT_CERTIFICATE, cp))
         goto jerr1;

      /* *ssl-key* */
      if ((cp_base = xok_vlook(ssl_key, urlp, OXM_ALL)) != NULL) {
         if (n_poption & n_PO_D_V)
            n_err(_("*ssl-key* %s\n"), n_shexp_quote_cp(cp_base, FAL0));
         if ((cp = fexpand(cp_base, FEXP_LOCAL | FEXP_NOPROTO)) == NULL) {
            n_err(_("*ssl-key* value expansion failed: %s\n"),
               n_shexp_quote_cp(cp_base, FAL0));
            goto jerr1;
         }
      }
      if (!_ssl_conf(confp, a_XSSL_CT_PRIVATE_KEY, cp))
         goto jerr1;
   }

   if ((cp = xok_vlook(ssl_cipher_list, urlp, OXM_ALL)) != NULL &&
         !_ssl_conf(confp, a_XSSL_CT_CIPHER_STRING, cp))
      goto jerr1;
   if ((cp = xok_vlook(ssl_curves, urlp, OXM_ALL)) != NULL &&
         !_ssl_conf(confp, a_XSSL_CT_CURVES, cp))
      goto jerr1;

   if (!_ssl_load_verifications(ctxp))
      goto jerr1;

   if (!_ssl_conf(confp, a_XSSL_CT_OPTIONS, NULL)) /* TODO *ssl-options* */
      goto jerr1;

   /* Done with context setup, create our new per-connection structure */
   if (!_ssl_conf_finish(&confp, FAL0))
      goto jerr0;

   if ((sp->s_ssl = SSL_new(ctxp)) == NULL) {
      ssl_gen_err(_("SSL_new() failed"));
      goto jerr0;
   }

   /* Try establish SNI extension; even though this is a TLS extension the
    * protocol isn't checked once the host name is set, and therefore i've
    * refrained from changing so much code just to check out whether we are
    * using SSLv3, which should become rarer and rarer */
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
   if((urlp->url_flags & n_URL_TLS_MASK) &&
         (urlp->url_flags & n_URL_HOST_IS_NAME)){
      if(!SSL_set_tlsext_host_name(sp->s_ssl, urlp->url_host.s) &&
            (n_poption & n_PO_D_V))
         n_err(_("Hostname cannot be used with ServerNameIndication "
               "TLS extension: %s\n"),
            n_shexp_quote_cp(urlp->url_host.s, FAL0));
   }
#endif

   SSL_set_fd(sp->s_ssl, sp->s_fd);

   if (SSL_connect(sp->s_ssl) < 0) {
      ssl_gen_err(_("could not initiate SSL/TLS connection"));
      goto jerr2;
   }

   if (ssl_verify_level != SSL_VERIFY_IGNORE) {
      if (ssl_check_host(sp, urlp) != OKAY) {
         n_err(_("Host certificate does not match: %s\n"), urlp->url_h_p.s);
         if (ssl_verify_decide() != OKAY)
            goto jerr2;
      }
   }

   /* We're fully setup: since we don't reuse the SSL_CTX (pooh) keep it local
    * and free it right now -- it is reference counted by sp->s_ssl.. */
   SSL_CTX_free(ctxp);
   sp->s_use_ssl = 1;
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
jerr2:
   SSL_free(sp->s_ssl);
   sp->s_ssl = NULL;
jerr1:
   if (confp != NULL)
      _ssl_conf_finish(&confp, TRU1);
jerr0:
   SSL_CTX_free(ctxp);
   goto jleave;
}

FL void
ssl_gen_err(char const *fmt, ...)
{
   va_list ap;
   NYD_ENTER;

   va_start(ap, fmt);
   n_verr(fmt, ap);
   va_end(ap);

   n_err(_(": %s\n"), ERR_error_string(ERR_get_error(), NULL));
   NYD_LEAVE;
}

FL int
c_verify(void *vp)
{
   int *msgvec = vp, *ip, ec = 0, rv = 1;
   X509_STORE *store = NULL;
   char *ca_dir, *ca_file;
   NYD_ENTER;

   a_xssl_init();

   ssl_verify_level = SSL_VERIFY_STRICT;
   if ((store = X509_STORE_new()) == NULL) {
      ssl_gen_err(_("Error creating X509 store"));
      goto jleave;
   }
   X509_STORE_set_verify_cb_func(store, &_ssl_verify_cb);

   if ((ca_dir = ok_vlook(smime_ca_dir)) != NULL)
      ca_dir = fexpand(ca_dir, FEXP_LOCAL | FEXP_NOPROTO);
   if ((ca_file = ok_vlook(smime_ca_file)) != NULL)
      ca_file = fexpand(ca_file, FEXP_LOCAL | FEXP_NOPROTO);

   if (ca_dir != NULL || ca_file != NULL) {
      if (X509_STORE_load_locations(store, ca_file, ca_dir) != 1) {
         ssl_gen_err(_("Error loading %s"),
            (ca_file != NULL) ? ca_file : ca_dir);
         goto jleave;
      }
   }

   /* C99 */{
      bool_t xv15;

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

   a_xssl_ca_flags(store, ok_vlook(smime_ca_flags));

   srelax_hold();
   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp = message + *ip - 1;
      setdot(mp);
      ec |= smime_verify(mp, *ip, NULL, store);
      srelax();
   }
   srelax_rele();

   if ((rv = ec) != 0)
      n_exit_status |= n_EXIT_ERR;
jleave:
   if (store != NULL)
      X509_STORE_free(store);
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_sign(FILE *ip, char const *addr)
{
   FILE *rv, *sp, *fp, *bp, *hp;
   X509 *cert = NULL;
   n_XSSL_STACKOF(X509) *chain = NULL;
   EVP_PKEY *pkey = NULL;
   BIO *bb, *sb;
   PKCS7 *pkcs7;
   EVP_MD const *md;
   char const *name;
   bool_t bail = FAL0;
   NYD_ENTER;

   assert(addr != NULL);
   rv = sp = fp = bp = hp = NULL;

   a_xssl_init();

   if (addr == NULL) {
      n_err(_("No *from* address for signing specified\n"));
      goto jleave;
   }
   if ((fp = smime_sign_cert(addr, NULL, 1, NULL)) == NULL)
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
   Fclose(fp);
   fp = NULL;

   if ((name = _smime_sign_include_certs(addr)) != NULL &&
         !_smime_sign_include_chain_creat(&chain, name,
            savecat(addr, ".smime-include-certs")))
      goto jleave;

   name = NULL;
   if ((md = _smime_sign_digest(addr, &name)) == NULL)
      goto jleave;

   if ((sp = Ftmp(NULL, "smimesign", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL) {
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP)
      goto jleave;

   sb = NULL;
   pkcs7 = NULL;

   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (sb = BIO_new_fp(sp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO signing objects"));
      bail = TRU1;
      goto jerr;
   }

#undef _X
#define _X  PKCS7_DETACHED | PKCS7_PARTIAL
   if ((pkcs7 = PKCS7_sign(NULL, NULL, chain, bb, _X)) == NULL) {
      ssl_gen_err(_("Error creating the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
   if (PKCS7_sign_add_signer(pkcs7, cert, pkey, md, _X) == NULL) {
      ssl_gen_err(_("Error setting PKCS#7 signing object signer"));
      bail = TRU1;
      goto jerr;
   }
   if (!PKCS7_final(pkcs7, bb, _X)) {
      ssl_gen_err(_("Error finalizing the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
#undef _X

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
      fflush_rewind(sp);
      rv = smime_sign_assemble(hp, bp, sp, name);
      hp = bp = sp = NULL;
   }

jleave:
   if (chain != NULL)
      sk_X509_pop_free(chain, X509_free);
   if (cert != NULL)
      X509_free(cert);
   if (pkey != NULL)
      EVP_PKEY_free(pkey);
   if (fp != NULL)
      Fclose(fp);
   if (hp != NULL)
      Fclose(hp);
   if (bp != NULL)
      Fclose(bp);
   if (sp != NULL)
      Fclose(sp);
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_encrypt(FILE *ip, char const *xcertfile, char const *to)
{
   FILE *rv, *yp, *fp, *bp, *hp;
   X509 *cert;
   PKCS7 *pkcs7;
   BIO *bb, *yb;
   n_XSSL_STACKOF(X509) *certs;
   EVP_CIPHER const *cipher;
   char *certfile;
   bool_t bail;
   NYD_ENTER;

   bail = FAL0;
   rv = yp = fp = bp = hp = NULL;

   if ((certfile = fexpand(xcertfile, FEXP_LOCAL | FEXP_NOPROTO)) == NULL)
      goto jleave;

   a_xssl_init();

   if ((cipher = _smime_cipher(to)) == NULL)
      goto jleave;

   if ((fp = Fopen(certfile, "r")) == NULL) {
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
   Fclose(fp);
   fp = NULL;
   bail = FAL0;

   certs = sk_X509_new_null();
   sk_X509_push(certs, cert);

   if ((yp = Ftmp(NULL, "smimeenc", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL) {
      n_perr(_("tempfile"), 0);
      goto jerr1;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP)
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
   if (bb != NULL)
      BIO_free(bb);
   if (yb != NULL)
      BIO_free(yb);
   Fclose(bp);
   bp = NULL;
   if (!bail) {
      fflush_rewind(yp);
      rv = smime_encrypt_assemble(hp, yp);
      hp = yp = NULL;
   }
jerr1:
   sk_X509_pop_free(certs, X509_free);
jleave:
   if(yp != NULL)
      Fclose(yp);
   if(fp != NULL)
      Fclose(fp);
   if(bp != NULL)
      Fclose(bp);
   if(hp != NULL)
      Fclose(hp);
   NYD_LEAVE;
   return rv;
}

FL struct message *
smime_decrypt(struct message *m, char const *to, char const *cc,
   bool_t signcall)
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
   NYD_ENTER;

   pkey = NULL;
   cert = NULL;
   ob = bb = pb = NULL;
   pkcs7 = NULL;
   bp = hp = op = NULL;
   rv = NULL;
   size = m->m_size;

   if((yp = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;

   a_xssl_init();

   if((op = smime_sign_cert(to, cc, 0, &myaddr)) != NULL){
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

      Fclose(op);
      op = NULL;
   }

   if((op = Ftmp(NULL, "smimedec", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   if(smime_split(yp, &hp, &bp, size, 1) == STOP)
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
      if(signcall){
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
   Fclose(bp);
   bp = NULL;

   rv = smime_decrypt_assemble(m, hp, op);
   hp = op = NULL; /* xxx closed by decrypt_assemble */
jleave:
   if(op != NULL)
      Fclose(op);
   if(hp != NULL)
      Fclose(hp);
   if(bp != NULL)
      Fclose(bp);
   if(bb != NULL)
      BIO_free(bb);
   if(ob != NULL)
      BIO_free(ob);
   if(pkcs7 != NULL)
      PKCS7_free(pkcs7);
   if(cert != NULL)
      X509_free(cert);
   if(pkey != NULL)
      EVP_PKEY_free(pkey);
   NYD_LEAVE;
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
   n_XSSL_STACKOF(X509) *certs, *chain = NULL;
   X509 *cert;
   enum okay rv = STOP;
   NYD_ENTER;

   pkcs7 = NULL;

   a_xssl_msgno = (size_t)n;
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
   if (cnttype && is_asccaseprefix("application/", cnttype) &&
         (!ascncasecmp(cnttype + _X, _Y("pkcs7-mime")) ||
          !ascncasecmp(cnttype + _X, _Y("x-pkcs7-mime")))) {
#undef _Y
#undef _X
      if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
         goto jleave;
      if (x != (struct message*)-1) {
         m = x;
         goto jloop;
      }
   }
   size = m->m_size;

   if ((fp = Ftmp(NULL, "smimecert", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL) {
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
      Fclose(fp);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object for message %d"), n);
      BIO_free(fb);
      Fclose(fp);
      goto jleave;
   }
   BIO_free(fb);
   Fclose(fp);

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
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_XSSL */

/* s-it-mode */
