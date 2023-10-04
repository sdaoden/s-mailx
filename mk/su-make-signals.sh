#!/bin/sh -
#@ Either create src/su/gen-signals.h, or, at compile time, the OS<>SU map.
#
# Public Domain

IN="${SRCDIR}"su/gen-signals.h
XOUT=src/su/gen-signals.h

# We use `csop' for hashing
MAILX='LC_ALL=C s-nail -#:/'

# Acceptable "longest distance" from hash-modulo-index to key
MAXDISTANCE_PENALTY=6

# Generate a more verbose output.  Not for shipout versions.
VERB=1

##

LC_ALL=C
export LC_ALL MAXDISTANCE_PENALTY VERB MAILX IN XOUT

: ${awk:=awk}
# Compile-time only
: ${rm:=rm}
: ${sort:=sort}

# The set of signals we support {{{
SIGNALS="
	NONE='No signal' \
	ABRT='Abort signal' \
	ALRM='Timer signal' \
	BUS='Bus error (bad memory access)' \
	CHLD='Child stopped or terminated' \
	CLD='Child stopped or terminated' \
	CONT='Continue process if stopped' \
	EMT='Emulator trap' \
	FPE='Floating-point exception' \
	HUP='Hangup of controlling terminal, or death of controlling process' \
	ILL='Illegal instruction' \
	INFO='Power failure' \
	INT='Interrupt from keyboard' \
	IO='I/O now possible' \
	IOT='IOT trap / abort signal' \
	KILL='Kill signal' \
	LOST='File lock lost' \
	PIPE='Broken pipe: write to pipe with no readers' \
	POLL='Pollable event' \
	PROF='Profiling timer expired' \
	PWR='Power failure' \
	QUIT='Quit from keyboard' \
	SEGV='Segementation violation: invalid memory reference' \
	STKFLT='Stack fault on coprocessor' \
	STOP='Stop process' \
	TSTP='Stop from terminal' \
	SYS='Bad system call' \
	TERM='Termination signal' \
	TRAP='Trace or breakpoint trap' \
	TTIN='Terminal input for background process' \
	TTOU='Terminal output from background process' \
	UNUSED='Bad system call' \
	URG='Urgent condition on socket' \
	USR1='User-defined signal 1' \
	USR2='User-defined signal 2' \
	VTALRM='Virtual alarm clock' \
	XCPU='CPU time limit exceeded' \
	XFSZ='File size limit exceeded' \
	WINCH='Window resize signal' \
"
export SIGNALS
# }}}

signal_parse() {
	j=\'
	${awk} -v dodoc="${1}" -v incnone="${2}" -v input="${SIGNALS}" '
		BEGIN{
			for(i = 0;;){
				voff = match(input, /[0-9a-zA-Z_]+(='${j}'[^'${j}']+)?/)
				if(voff == 0)
					break
				v = substr(input, voff, RLENGTH)
				input = substr(input, voff + RLENGTH)
				doff = index(v, "=")
				if(doff > 0){
					d = substr(v, doff + 2, length(v) - doff - 1)
					v = substr(v, 1, doff - 1)
				}
				if(!incnone && v == "NONE")
					continue
				print dodoc ? d : v
			}
		}
	'
}

compile_time() { # {{{
	[ -n "${TARGET}" ] || {
		echo >&2 'Invalid usage'
		exit 1
	}
	set -e
	pipefail=
	( set -o pipefail ) >/dev/null 2>&1 && pipefail=y
	[ -n "${pipefail}" ] && set -o pipefail

	{
		printf '#include <signal.h>\nsu_SIGNAL_START\n'
		for n in $(signal_parse 0 0); do
			printf '#ifdef SIG%s\nSIG%s "%s"\n#else\n-1 "%s"\n#endif\n' $n $n $n $n
		done
	} > "${TARGET}".c

	# The problem is that at least (some versions of) gcc mangle output.
	# Ensure we get both arguments on one line.
	# While here sort numerically.
	${CC} -E "${TARGET}".c |
		${awk} '
			function stripsym(sym){
				sym = substr(sym, 2)
				sym = substr(sym, 1, length(sym) - 1)
				return sym
			}
			BEGIN{hot=0; conti=0}
			/^[	 ]*$/{next}
			/^[	 ]*#/{next}
			/^su_SIGNAL_START$/{hot=1; next}
			{
				if(!hot)
					next
				i = conti ? stripsym($1) "\n" : $1 " "
				printf i
				if(conti)
					conti = 0
				else if($2 != "")
					printf "%s\n", stripsym($2)
				else
					conti = 1
			}
		' |
		${sort} -n > "${TARGET}".txt

	# EBCDIC/ASCII: we use \134 for reverse solidus \
	j=\'
	${awk} -v verb="${VERB}" -v input="${SIGNALS}" -v dat="${TARGET}.txt" '
		BEGIN{
			verb = (verb != 0) ? "	" : ""

			# Read in our OS data

			unavail = 0
			max = 0
			oscnt = 0
			while((getline dl < dat) > 0){
				split(dl, ia)

				++oscnt
				osnoa[oscnt] = ia[1]
				osnaa[oscnt] = ia[2]

				if(ia[1] < 0)
					++unavail
				else{
					if(ia[1] > max)
						max = ia[1]
				}
			}
			close(dat)

			# Maximum signal number defines the datatype to use.
			# we warp all non-available errors to numbers too high
			# to be regular errors, counting backwards

			i = max + unavail + 1
			if(i >= 65535){
				t = "u32"
				max = "0xFFFFFFFFu"
			}else if(i >= 255){
				t = "u16"
				max = "0xFFFFu"
			}else{
				t = "u8"
				max = "0xFFu"
			}
			print "#define su__SIG_NUMBER_TYPE " t
			print "#define su__SIG_NUMBER_MAX " max

			# Dump C table

			cnt = 0
			print "#define su__SIG_NUMBER_ENUM_C \134"

			print verb "su_SIG_NONE = 0,\134"
			++cnt

			# Since our final table is searched with binary sort,
			# place the non-available backward counting once last
			unavail = j = k = 0
			for(i = 1; i <= oscnt; ++i){
				if(osnoa[i] >= 0){
					map[osnaa[i]] = osnoa[i]
					print verb "su_SIG_" osnaa[i] " = " osnoa[i] ",\134"
					++cnt
				}else{
					++unavail
					the_unavail[unavail] = "su_SIG_" osnaa[i] " = " "(su__SIG_NUMBER_MAX - " unavail ")"
				}
			}
			for(i = unavail; i >= 1; --i){
				print verb the_unavail[i] ",\134"
				++cnt
			}

			print verb "su__SIG_NUMBER = " cnt

			# The C++ mapping table

			print "#ifdef __cplusplus"
			print "# define su__CXX_SIG_NUMBER_ENUM \134"
			print verb "none = su_SIG_NONE,\134"

			unavail = 0
			for(i = 1; i <= oscnt; ++i){
				cxxn = tolower(osnaa[i])
				if(cxxn !~ "^[_[:alpha:]]")
					cxxn = "s" cxxn
				if(osnoa[i] >= 0)
					print verb cxxn " = su_SIG_" osnaa[i] ",\134"
				else{
					++unavail
					the_unavail[unavail] = cxxn " = su_SIG_" osnaa[i]
				}
			}
			for(i = unavail; i >= 1; --i){
				print verb the_unavail[i] ",\134"
				++cnt
			}
			print verb "s__number = su__SIG_NUMBER"

			print "#endif /* __cplusplus */"

			# And our OS signal -> name map offset table
			# (This "OS" however includes the unavail ones)

			voidoff = 0
			for(mapoff = 0;; ++mapoff){
				voff = match(input, /[0-9a-zA-Z_]+(='${j}'[^'${j}']+)?/)
				if(voff == 0)
					break

				v = substr(input, voff, RLENGTH)
				input = substr(input, voff + RLENGTH)
				doff = index(v, "=")
				if(doff > 0){
					d = substr(v, doff + 2, length(v) - doff - 1)
					v = substr(v, 1, doff - 1)
				}
				mapo[v] = mapoff
				if(v == "UNUSED")
					voidoff = mapoff
			}

			uniq = 0
			print "#define su__SIG_NUMBER_VOIDOFF " voidoff
			print "#define su__SIG_NUMBER_TO_MAPOFF \134"

			print verb "a_X(0, 0) \134"
			++uniq

			unavail = 0
			mapdups[0] = 1
			for(i = 1; i <= oscnt; ++i){
				if(osnoa[i] < 0){
					the_unavail[++unavail] = i
					continue
				}
				if(mapdups[osnoa[i]])
					continue
				mapdups[osnoa[i]] = 1
				print verb "a_X(" osnoa[i] ", " mapo[osnaa[i]] ") \134"
				++uniq
			}

			for(i = unavail; i >= 1; --i){
				print verb "a_X(" "su__SIG_NUMBER_MAX - " i ", " mapo[osnaa[the_unavail[i]]] ")\134"
				++uniq
			}

			print verb "a_X(su__SIG_NUMBER_MAX, su__SIG_NUMBER_VOIDOFF) \134"
			++uniq
			print verb "/* " uniq " unique members */"
		}
	' >> "${TARGET}"

	${rm} "${TARGET}".*
	exit 0
} # }}}

if [ ${#} -ne 0 ]; then
	if [ "${1}" = noverbose ]; then
		shift
		VERB=0
		export VERB
	fi
fi

if [ ${#} -eq 1 ]; then
	[ "${1}" = compile_time ] && compile_time
elif [ ${#} -eq 0 ]; then
	# Now start perl(1) without PERL5OPT set to avoid multibyte sequence errors
	PERL5OPT= PERL5LIB= exec perl -x "${0}"
fi
echo >&2 'Invalid usage'
exit 1

#' PERL {{{
# Thanks to perl(5) and its -x / #! perl / __END__ mechanism!
# Why can env(1) not be used for such easy things in #!?
#!perl

use strict;
use warnings;

use FileHandle;
use IPC::Open2;

use sigtrap qw(handler cleanup normal-signals);

my ($S, @ENTS, $CTOOL, $CTOOL_EXE) = ($ENV{VERB} ? '	' : '');

sub main_fun{
	create_ents();

	create_c_tool();

	hash_em();

	dump_map(); # Starts output file

	reverser(); # Ends output file

	cleanup(undef);
	exit 0
}

sub cleanup{
	die "$CTOOL_EXE: couldn't unlink: $^E" if $CTOOL_EXE && -f $CTOOL_EXE && 1 != unlink $CTOOL_EXE;
	die "$CTOOL: couldn't unlink: $^E" if $CTOOL && -f $CTOOL && 1 != unlink $CTOOL;
	die "Terminating due to signal $_[0]" if $_[0]
}

sub basen{
	my $n = $_[0];
	$n =~ s/^(.*\/)?([^\/]+)$/$2/;
	$n
}

sub create_ents{
	my $input = $ENV{SIGNALS};
	while($input =~ /([[:alnum:]_]+)='([^']+)'(.*)/){
		$input = $3;
		my %vals;
		$vals{name} = $1;
		$vals{doc} = $2;
		push @ENTS, \%vals
	}
}

sub create_c_tool{
	$CTOOL = './tmp-signals-tool-' . $$ . '.c';
	$CTOOL_EXE = $CTOOL . '.exe';

	die "$CTOOL: open: $^E" unless open F, '>', $CTOOL;
	print F '#define MAX_DISTANCE_PENALTY ', $ENV{MAXDISTANCE_PENALTY}, "\n";
# >>>>>>>>>>>>>>>>>>>
	print F <<'_EOT';
#define __CREATE_SIGNALS_SH
#define su_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define su_NELEM(A) (sizeof(A) / sizeof((A)[0]))

#define u32 uint32_t
#define s32 int32_t
#define u16 uint16_t
#define u8 uint8_t

struct a_corsig_map{
	u32 csm_hash; /* Hash of name */
	u32 csm_nameoff; /* Into a_corsig_names[] */
	u32 csm_docoff; /* Into a_corsig_docs[] */
	s32 csm_errno;	 /* OS signal value for this one */
};

_EOT

	print F '#include "', $ENV{XOUT}, "\"\n\n";

	print F <<'_EOT';
static u8 seen_wraparound;
static size_t longest_distance;

static size_t
next_prime(size_t no){ /* blush (brute force) */
jredo:
	++no;
	for(size_t i = 3; i < no; i += 2)
		if(no % i == 0)
			goto jredo;
	return no;
}

static size_t *
reversy(size_t size){
	struct a_corsig_map const *csmp = a_corsig_map, *csmaxp = csmp + su_NELEM(a_corsig_map);
	size_t ldist = 0, *arr;

	arr = (size_t*)malloc(sizeof *arr * size);
	for(size_t i = 0; i < size; ++i)
		arr[i] = su_NELEM(a_corsig_map);

	seen_wraparound = 0;
	longest_distance = 0;

	while(csmp < csmaxp){
		u32 hash = csmp->csm_hash, i = hash % size, l;

		for(l = 0; arr[i] != su_NELEM(a_corsig_map); ++l)
			if(++i == size){
				seen_wraparound = 1;
				i = 0;
			}
		if(l > longest_distance)
			longest_distance = l;
		arr[i] = (size_t)(csmp++ - a_corsig_map);
	}
	return arr;
}

int
main(int argc, char **argv){
	size_t *arr, size = su_NELEM(a_corsig_map);

	fprintf(stderr, "Starting reversy, okeys=%zu\n", size);
	for(;;){
		arr = reversy(size = next_prime(size));
		fprintf(stderr, " - size=%zu longest_distance=%zu seen_wraparound=%d\n",
			size, longest_distance, seen_wraparound);
		if(longest_distance <= MAX_DISTANCE_PENALTY)
			break;
		free(arr);
	}

	printf(
		"#ifdef su_SOURCE /* Lock-out compile-time-tools */\n"
		"# define a_CORSIG_REV_ILL %zuu\n"
		"# define a_CORSIG_REV_PRIME %zuu\n"
		"# define a_CORSIG_REV_LONGEST %zuu\n"
		"# define a_CORSIG_REV_WRAPAROUND %d\n"
		"static %s const a_corsig_revmap[a_CORSIG_REV_PRIME] = {\n%s",
		su_NELEM(a_corsig_map), size, longest_distance, seen_wraparound,
		argv[1], (argc > 2 ? "	" : ""));
	for(size_t i = 0; i < size; ++i)
		printf("%s%zuu", (i == 0 ? "" : (i % 10 == 0 ? (argc > 2 ? ",\n	" : ",\n") : (argc > 2 ? ", " : ","))),
			arr[i]);
	printf("\n};\n#endif /* su_SOURCE */\n");
	return 0;
}
_EOT
# <<<<<<<<<<<<<<<<<<<
	close F
}

sub hash_em{
	die "hash_em: open: $^E" unless my $pid = open2 *RFD, *WFD, $ENV{MAILX};
	foreach my $e (@ENTS){
		print WFD "csop hash32?case $e->{name}\n";
		my $h = <RFD>;
		chomp $h;
		$e->{hash} = $h
	}
	print WFD "x\n";
	waitpid $pid, 0;
}

sub dump_map{
	my ($i, $alen);

	die "$ENV{XOUT}: open: $^E" unless open F, '>', $ENV{XOUT};
	print F '/*@ ', scalar basen($ENV{XOUT}), ', generated by ', scalar basen($0),
		".\n *@ See core-signals.c for more */\n\n";

	($i, $alen) = (0, 0);
	print F '#ifdef su_SOURCE', "\n", 'static char const a_corsig_names[] = {', "\n";
	($i, $alen) = (0, 0);
	foreach my $e (@ENTS){
		$e->{nameoff} = $alen;
		my $k = $e->{name};
		my $l = length $k;
		my $a = join '\',\'', split(//, $k);
		my (@fa);
		print F "${S}/* $i. [$alen]+$l $k */\n" if $ENV{VERB};
		print F "${S}'$a','\\0',\n";
		++$i;
		$alen += $l + 1
	}
	print F '};', "\n\n";

	print F '# undef a_X', "\n", '# define a_X(X)', "\n";
	print F 'static char const a_corsig_docs[] = {', "\n";
	($i, $alen) = (0, 0);
	foreach my $e (@ENTS){
		$e->{docoff} = $alen;
		my $k = $e->{doc};
		my $l = length $k;
		my $a = join '\',\'', split(//, $k);
		my (@fa);
		print F "${S}/* $i. [$alen]+$l $e->{name} */ ", "a_X(N_(\"$e->{doc}\"))\n" if $ENV{VERB};
		print F "${S}'$a','\\0',\n";
		++$i;
		$alen += $l + 1
	}
	print F '};', "\n# undef a_X\n\n";

	print F <<_EOT;
# undef a_X
# ifndef __CREATE_SIGNALS_SH
#  define a_X(X) X
# else
#  define a_X(X) 0
# endif
static struct a_corsig_map const a_corsig_map[] = {
_EOT
	foreach my $e (@ENTS){
		print F "${S}{$e->{hash}u, $e->{nameoff}u, $e->{docoff}u, a_X(su_SIG_$e->{name})},\n"
	}
	print F '};', "\n", '# undef a_X', "\n",
		'#endif /* su_SOURCE */', "\n\n";

	die "$ENV{XOUT}: close: $^E" unless close F
}

sub reverser{
	my $argv2 = $ENV{VERB} ? ' verb' : '';
	system("\$CC -I. -o $CTOOL_EXE $CTOOL");
	my $t = (@ENTS < 0xFF ? 'u8' : (@ENTS < 0xFFFF ? 'u16' : 'u32'));
	`$CTOOL_EXE $t$argv2 >> $ENV{XOUT}`
}

{package main; main_fun()}
# }}}

# s-itt-mode
