#@ .makefile, solely for creating the C++ .main.cxx test program
#@ - Does not work with smake(1).

su_USECASE_MX_DISABLED =

awk?=awk
getconf?=getconf
rm?=rm
CC?=cc
CXX?=c++
# Elder BSD make use rl!
ARFLAGS=rv

# c89,c99,c11,c17,c2x
SUFLVLC=-std=c89
# c++98,c++11,c++14,c++17,c++20,c++2b
SUFLVLCXX=-std=c++98
SUFDEVEL=-Dsu_HAVE_DEBUG -Dsu_HAVE_DEVEL -Dsu_NYD_ENABLE
#SUFDEVEL=
SUFOPT=-O1 -g
#SUFOPT=-DNDEBUG -O2
SULDF=-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed,--enable-new-dtags -pie -fPIE
SULDFOPT=
#SULDFOPT=-Wl,-O1,--sort-common
SUSTRIP=
#SUSTRIP=strip

SUF_GEN_CONFIG_LIST = \
	su_RANDOM_SEED\ su_RANDOM_SEED_URANDOM \
	\
	su_HAVE_PATH_RM_AT \
	\
	su__HAVE_CLOCK_GETTIME \
	su__HAVE_NANOSLEEP \
	su__HAVE_PATHCONF \
	su__HAVE_STAT_BLOCKS su__HAVE_STAT_TIMESPEC \
	su__HAVE_UTIMENSAT \

SUF = -D_GNU_SOURCE $(SUFDEVEL)

SUFWW = #-Weverything -Wno-unsafe-buffer-usage -Wno-format-nonliteral
SUFW = -W -Wall -pedantic $(SUFWW) \
	\
	-Wno-atomic-implicit-seq-cst \
	-Wno-c++98-compat \
	-Wno-documentation-unknown-command \
	-Wno-duplicate-enum \
	-Wno-reserved-identifier \
	-Wno-reserved-macro-identifier \
	-Wno-unused-macros \
	\
	-Werror=format-security -Werror=int-conversion \

SUFS = -fPIE \
	-fno-common \
	-fstrict-aliasing -fstrict-overflow \
	-fstack-protector-strong \
	-D_FORTIFY_SOURCE=3 \
	$$(x=$$(uname -m); [ "$${x}" != "$${x\#x86*}" ] && echo -fcf-protection=full) \
	\
#	-DHAVE_SANITIZER \
#		-fsanitize=undefined \
#		-fsanitize=address \

CFLAGS += $(SUFLVLC) $(SUF) $(SUFW) $(SUFS) $(SUFOPT)
CXXFLAGS += $(SUFLVLCXX) $(SUF) $(SUFW) $(SUFS) $(SUFOPT)
LDFLAGS += $(SULDF) $(SULDFOPT)

CSRC = atomic.c \
		avopt.c \
	core-code.c core-create.c core-errors.c core-on-gut.c \
		cs-alloc.c cs-ctype.c cs-hash.c cs-hash-strong.c \
			cs-find.c cs-rfind.c cs-tbox.c cs-tools.c \
		cs-dict.c \
	icodec-dec.c icodec-enc.c \
		imf.c \
	md.c md-siphash.c \
		mem-alloc.c mem-tools.c \
		mem-bag.c \
		mutex.c \
	path-info.c path-utils.c path-utils-cs.c \
		prime.c \
	random.c \
		re.c \
	sort.c \
		spinlock.c \
	thread.c \
		time-sleep.c time-spec.c time-utils.c \
	utf.c \

CXXSRC = cxx-core.cxx

PROGSRC = .main.cxx

## 8< >8

CONFIG = ../../include/su/gen-config.h
COBJ = $(CSRC:.c=.o)
CXXOBJ = $(CXXSRC:.cxx=.o)
PROGOBJ = $(PROGSRC:.cxx=.o)
OBJ = $(COBJ) $(CXXOBJ) $(PROGOBJ)

.SUFFIXES: .o .c .cxx .y # .y for smake
.cxx.o:
	$(CXX) -I../../src -I../../include $(CXXFLAGS) -o $(@) -c $(<)
.c.o:
	$(CC) -I../../src -I../../include $(CFLAGS) -o $(@) -c $(<)
.cxx .c .y: ;

all: .main
clean:
	$(rm) -f $(CONFIG) .main .tmp*  .clib.a .cxxlib.a $(OBJ)

$(COBJ): $(CONFIG) $(CSRC)
.clib.a: $(COBJ)
	$(AR) $(ARFLAGS) $(@) $(COBJ)

$(CXXOBJ): $(CXXSRC)
.cxxlib.a: $(CXXOBJ)
	$(AR) $(ARFLAGS) $(@) $(CXXOBJ)

$(PROGOBJ): $(PROGSRC)
.main: .clib.a .cxxlib.a $(PROGOBJ)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(@) $(PROGOBJ) .cxxlib.a .clib.a
	if [ -n "$(SUSTRIP)" ]; then $(SUSTRIP) -s $(@); fi

test: all
	./.main

$(CONFIG):
	CC="$(CC)" SRCDIR=`dirname \`pwd\``/ TARGET="$(@)" awk="$(awk)" \
		$(SHELL) ../../mk/su-make-errors.sh compile_time
	echo '#define su_PAGE_SIZE '"`$(getconf) PAGESIZE`" >> $(@)
	xxx="$(SUF)";\
	if [ "$${xxx##*su_HAVE_DEBUG}" != "$$xxx" ]; then \
		printf '#ifndef su_HAVE_DEBUG\n# define su_HAVE_DEBUG\n#endif\n' >> $(@);\
	fi;\
	if [ "$${xxx##*su_HAVE_DEVEL}" != "$$xxx" ]; then \
		printf '#ifndef su_HAVE_DEVEL\n# define su_HAVE_DEVEL\n#endif\n' >> $(@);\
	fi;\
	\
	for ce in $(SUF_GEN_CONFIG_LIST); do echo '#define '$$ce >> $(@); done

# s-mk-mode
