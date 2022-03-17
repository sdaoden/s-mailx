#@ .makefile, solely for creating the C++ .main.cxx test program
#@ - Does not work with smake(1).

su_USECASE_MX_DISABLED =

awk?=awk
getconf?=getconf
rm?=rm
CC?=cc
CXX?=c++
# BSD make uses rl!
ARFLAGS=rv

SUFDEVEL=-Dsu_HAVE_DEVEL -Dsu_HAVE_DEBUG
SUF=$(SUFDEVEL) \
	-Dsu_HAVE_CLOCK_GETTIME \
	-Dsu_HAVE_NANOSLEEP \
	-Dsu_HAVE_PATHCONF \
	-Dsu_HAVE_UTIMENSAT
SUFW=-W -Wall -pedantic \
	-Wno-uninitialized -Wno-unused-result -Wno-unused-value

CXXFLAGS+=$(SUF) $(SUFW)
CFLAGS+=$(SUF) $(SUFW) -D_GNU_SOURCE

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
	path.c \
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
	$(CXX) $(LDFLAGS) -o $(@) $(PROGOBJ) .cxxlib.a .clib.a

$(CONFIG):
	CC="$(CC)" SRCDIR=`dirname \`pwd\``/ TARGET="$(@)" awk="$(awk)" \
		$(SHELL) ../../mk/su-make-errors.sh compile_time
	echo '#define su_PAGE_SIZE '"`$(getconf) PAGESIZE`" >> $(@)
	xxx="$(SUF)";\
	if [ "$${xxx##*su_HAVE_DEBUG}" != "$$xxx" ]; then \
	printf '#ifndef su_HAVE_DEBUG\n#define su_HAVE_DEBUG\n#endif\n' \
	>> $(@);\
	fi; \
	if [ "$${xxx##*su_HAVE_DEVEL}" != "$$xxx" ]; then \
	printf '#ifndef su_HAVE_DEVEL\n#define su_HAVE_DEVEL\n#endif\n' \
	>> $(@);\
	fi

# s-mk-mode
