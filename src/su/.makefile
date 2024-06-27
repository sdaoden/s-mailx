#@ .makefile, solely for creating the C++ .main.cc test program
#@ With CC=tcc, AR=tcc ARFLAGS=-ar!

su_USECASE_MX_DISABLED =

awk?=awk
getconf?=getconf

SUF=-Dsu_HAVE_DEVEL -Dsu_HAVE_DEBUG
CXXFLAGS+=-Wall -pedantic $(SUF)
CFLAGS+=-Wall -pedantic $(SUF)

CSRC = avopt.c core-code.c core-errors.c \
	cs-alloc.c cs-ctype.c cs-dict.c cs-find.c cs-misc.c \
		cs-rfind.c cs-tbox.c cs-tools.c \
	icodec-dec.c icodec-enc.c \
	mem-alloc.c mem-bag.c mem-tools.c \
	prime.c re.c sort.c utf.c
CXXSRC = cxx-core.cc \
	.main.cc

## 8< >8

.SUFFIXES: .o .c .cc .y
.cc.o:
	$(CXX) -Dsu_USECASE_SU -I../../src -I../../include \
		$(CXXFLAGS) -o $(@) -c $(<)
.c.o:
	$(CC) -Dsu_USECASE_SU -I../../src -I../../include \
		$(CFLAGS) -o $(@) -c $(<)
.cc .c .y: ;

COBJ = $(CSRC:.c=.o)
CXXOBJ = $(CXXSRC:.cc=.o)
OBJ = $(COBJ) $(CXXOBJ)

all: .main
clean:
	rm -f ../../include/su/gen-config.h .main .tmp* .clib.a $(OBJ)

$(COBJ): $(CSRC) ../../include/su/gen-config.h
.clib.a: $(COBJ)
	$(AR) $(ARFLAGS) $(@) $(COBJ)
$(CXXOBJ): $(CLIB) ../../include/su/gen-config.h
.main: $(CXXOBJ) .clib.a
	$(CXX) $(LDFLAGS) -o $(@) $(CXXOBJ) .clib.a

../../include/su/gen-config.h:
	CC="$(CC)" SRCDIR=`dirname \`pwd\``/ TARGET="$(@)" awk="$(awk)" \
		$(SHELL) ../../mk/su-make-errors.sh compile_time &&\
	echo '#define su_PAGE_SIZE '"`$(getconf) PAGESIZE`" >> $(@)

# s-mk-mode
