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

SUFLVLC=#-std=c2x
SUFLVLCXX=#-std=c++2b
SUFDEVEL=-Dsu_HAVE_DEBUG -Dsu_HAVE_DEVEL -Dsu_NYD_ENABLE
SUFOPT=-O1 -g
#SUFOPT=-DNDEBUG -O2

# standalone: -Dsu_RANDOM_SEED=su_RANDOM_SEED_URANDOM
SUF = $(SUFDEVEL) \
	-Dsu_HAVE_CLOCK_GETTIME \
	-Dsu_HAVE_NANOSLEEP \
	-Dsu_HAVE_PATHCONF \
	-Dsu_HAVE_STAT_BLOCKS -Dsu_HAVE_STAT_TIMESPEC \
	-Dsu_HAVE_UTIMENSAT \

SUFWWW = #-Weverything
SUFWW = -W -Wall -pedantic $(SUFWWW) \
	-Wno-atomic-implicit-seq-cst \
	-Wno-c++98-compat \
	-Wno-documentation-unknown-command \
	-Wno-duplicate-enum \
	-Wno-reserved-identifier \
	-Wno-reserved-macro-identifier \
	-Wno-unused-macros

SUFW = -W -Wall -pedantic

SUFS = -fPIE \
	-fno-common \
	-fstrict-aliasing -fstrict-overflow \
	-fstack-protector-strong \
	-D_FORTIFY_SOURCE=3 \
	\
#	-DHAVE_SANITIZER \
#		-fsanitize=undefined \
#		-fsanitize=address \

CFLAGS += $(SUFLVLC) $(SUF) $(SUFWW) $(SUFS) -D_GNU_SOURCE $(SUFOPT)
CXXFLAGS += $(SUFLVLCXX) $(SUF) $(SUFWW) $(SUFS) $(SUFOPT)

LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,--as-needed \
	-Wl,--enable-new-dtags \
	-fpie

CSRC = atomic.c \
		avopt.c \
	core-code.c \
			core-create.c \
			core-errors.c \
			core-on-gut.c \
		cs-alloc.c \
			cs-ctype.c \
			cs-hash.c \
				cs-hash-strong.c \
			cs-find.c \
				cs-rfind.c \
			cs-tbox.c \
			cs-tools.c \
		cs-dict.c \
	icodec-dec.c \
			icodec-enc.c \
	md.c \
			md-siphash.c \
		mem-alloc.c \
			mem-tools.c \
		mem-bag.c \
		mutex.c \
	path-info.c \
			path-utils.c \
				path-utils-cs.c \
		prime.c \
	random.c \
		re.c \
	sort.c \
		spinlock.c \
	thread.c \
		time-sleep.c \
			time-spec.c \
			time-utils.c \
	utf.c

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

$(CONFIG):
	CC="$(CC)" SRCDIR=`dirname \`pwd\``/ TARGET="$(@)" awk="$(awk)" \
		$(SHELL) ../../mk/su-make-errors.sh compile_time
	echo '#define su_PAGE_SIZE '"`$(getconf) PAGESIZE`" >> $(@)
	xxx="$(SUF)";\
	if [ "$${xxx##*su_HAVE_DEBUG}" != "$$xxx" ]; then \
		printf '#ifndef su_HAVE_DEBUG\n#define su_HAVE_DEBUG\n#endif\n' >> $(@);\
	fi; \
	if [ "$${xxx##*su_HAVE_DEVEL}" != "$$xxx" ]; then \
		printf '#ifndef su_HAVE_DEVEL\n#define su_HAVE_DEVEL\n#endif\n' >> $(@);\
	fi

# s-mk-mode
