#!/usr/bin/env bash

versions="8 7 6.0 5.0 4.3 4.2 4.1 4.0"
scan_build=

for i in $versions; do
	if [ ! -z "`which scan-build-$i`" ]; then
		scan_build=scan-build-$i
		break;
	fi
done

if [ -z "$scan_build" ]; then

cat << EOF
No appropriate "scan-build" utility found.
Make sure the clang package of one of the
following versions is installed:
$versions
EOF

	exit -1
fi

clang=

for i in $versions; do
	if [ ! -z "`which clang++-$i`" ]; then
		clang=`which clang++-$i`
		break;
	fi
done

if [ -z "$clang" ]; then

cat << EOF
No appropriate clang compiler found.
Make sure the clang package of one of the
following versions is installed:
$versions
EOF

	exit -1
fi

usage=`$scan_build 2> /dev/null`
ncpu=$((2 * `cat /proc/cpuinfo 2>/dev/null | grep processor | wc -l` + 1))
preferred_checkers=`
cat << EOF
alpha.core.BoolAssignment
alpha.core.CallAndMessageUnInitRefArg
alpha.core.Conversion
alpha.core.DynamicTypeChecker
alpha.core.IdenticalExpr
alpha.core.PointerArithm
alpha.core.PointerSub
alpha.core.SizeofPtr
alpha.core.TestAfterDivZero
alpha.security.ArrayBound
alpha.security.ArrayBoundV2
alpha.security.MallocOverflow
alpha.security.ReturnPtrRange
alpha.security.taint.TaintPropagation
alpha.unix.BlockInCriticalSection
alpha.unix.MallocWithAnnotations
alpha.unix.PthreadLock
alpha.unix.SimpleStream
alpha.unix.Stream
alpha.unix.cstring.BufferOverlap
alpha.unix.cstring.NotNullTerminated
alpha.unix.cstring.OutOfBounds
alpha.valist.CopyToSelf
alpha.valist.Uninitialized
alpha.valist.Unterminated
core.CallAndMessage
core.DivideZero
core.DynamicTypePropagation
core.NonNullParamChecker
core.NullDereference
core.StackAddressEscape
core.UndefinedBinaryOperatorResult
core.VLASize
core.builtin.BuiltinFunctions
core.builtin.NoReturnFunctions
core.uninitialized.ArraySubscript
core.uninitialized.Assign
core.uninitialized.Branch
core.uninitialized.CapturedBlockVariable
core.uninitialized.UndefReturn
cplusplus.NewDelete
cplusplus.NewDeleteLeaks
cplusplus.SelfAssignment
nullability.NullPassedToNonnull
nullability.NullReturnedFromNonnull
nullability.NullableDereferenced
nullability.NullablePassedToNonnull
nullability.NullableReturnedFromNonnull
deadcode.DeadStores
security.FloatLoopCounter
security.insecureAPI.UncheckedReturn
security.insecureAPI.getpw
security.insecureAPI.gets
security.insecureAPI.mkstemp
security.insecureAPI.mktemp
security.insecureAPI.rand
security.insecureAPI.strcpy
security.insecureAPI.vfork
unix.API
unix.Malloc
unix.MallocSizeof
unix.MismatchedDeallocator
unix.Vfork
unix.cstring.BadSizeArg
unix.cstring.NullArg
EOF`

checker_args=

for c in $preferred_checkers; do
	if [ ! -z "`echo $usage | grep $c`" ]; then
		checker_args+=" -enable-checker $c"
	fi
done

LC_ALL=C USE_CLANG_OPTS=1 $scan_build \
	-analyze-headers \
	-maxloop 16 \
	--use-c++=$clang \
	--use-analyzer=$clang \
	-o ./scan-reports $checker_args \
	make -j$ncpu
