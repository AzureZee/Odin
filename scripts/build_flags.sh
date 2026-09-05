#!/usr/bin/env sh
set -eu

SUPPORTED_LLVM_VERSIONS="22 21 20 19 18 17"
SUGGESTED_LLVM_VERSION="22"
MINIMUM_LLVM_VERSION="17"

: ${CPPFLAGS=}
: ${CXXFLAGS=}
: ${LDFLAGS=}
: ${LLVM_CONFIG=}

CXXFLAGS="$CXXFLAGS -std=c++14"
DISABLED_WARNINGS="-Wno-switch -Wno-macro-redefined -Wno-unused-value"
LDFLAGS="$LDFLAGS -pthread -lm"
OS_ARCH="$(uname -m)"
OS_NAME="$(uname -s)"

if [ -d ".git" ] && [ -n "$(command -v git)" ]; then
	# Counter the user's git config to show the signature in logs.
	gitnosig="-c log.showSignature=false"
	GIT_SHA=$(git $gitnosig show --pretty='%h' --no-patch --no-notes HEAD)
	GIT_DATE=$(git $gitnosig show "--pretty=%cd" "--date=format:%Y-%m" --no-patch --no-notes HEAD)
	CPPFLAGS="$CPPFLAGS -DGIT_SHA=\"$GIT_SHA\""
else
	GIT_DATE=$(date +"%Y-%m")
fi
CPPFLAGS="$CPPFLAGS -DODIN_VERSION_RAW=\"dev-$GIT_DATE\""

error() {
	printf "ERROR: %s\n" "$1"
	exit 1
}

# Brew advises people not to add llvm to their $PATH, so try and use brew to find it.
if [ -z "$LLVM_CONFIG" ] &&  [ -n "$(command -v brew)" ]; then
	for V in $SUPPORTED_LLVM_VERSIONS; do
		if [ -n "$(command -v $(brew --prefix llvm@$V)/bin/llvm-config)" ]; then
			LLVM_CONFIG="$(brew --prefix llvm@$V)/bin/llvm-config"
			break
		fi
	done
fi

if [ -z "$LLVM_CONFIG" ]; then
	DEFAULT_VERSION=""

	if [ -n "$(command -v llvm-config)" ]; then
		DEFAULT_VERSION=$(llvm-config --version | awk -F. '{print $1}')
	fi

	for V in $SUPPORTED_LLVM_VERSIONS; do
		if [ "$DEFAULT_VERSION" = "$V" ]; then
			LLVM_CONFIG="llvm-config"
			break
		# darwin, linux, openbsd
		elif [ -n "$(command -v "llvm-config-$V")" ]; then
			LLVM_CONFIG="llvm-config-$V"
			break
		# freebsd
		elif [ -n "$(command -v "llvm-config$V")" ]; then
			LLVM_CONFIG="llvm-config$V"
			break
		fi
	done

	if [ -z "$LLVM_CONFIG" ]; then
		if [ -f "/etc/os-release" ]; then
			. /etc/os-release
			case "$ID" in
			ubuntu|debian|linuxmint|pop|zorin|kali|elementary|raspbian)
				echo "ERROR: No supported llvm-config command found. Set LLVM_CONFIG to proceed."
				echo "We suggest installing LLVM $SUGGESTED_LLVM_VERSION from https://apt.llvm.org"
				exit 1
				;;
			fedora|centos|rocky|almalinux|rhel|amzn|ol|centos-stream)
				echo "ERROR: No supported llvm-config command found. Set LLVM_CONFIG to proceed."
				echo "We suggest installing LLVM $SUGGESTED_LLVM_VERSION via COPR, e.g. dnf copr enable fedora-llvm-team/llvm-snapshots"
				exit 1
				;;
			esac
		fi
		error "No supported llvm-config command found. Set LLVM_CONFIG to proceed. We suggest LLVM $SUGGESTED_LLVM_VERSION."
	fi
fi

if [ -x "$(which clang++)" ]; then
	: ${CXX="clang++"}
elif [ -x "$($LLVM_CONFIG --bindir)/clang++" ]; then
	: ${CXX=$($LLVM_CONFIG --bindir)/clang++}
else
	error "No clang++ command found. Set CXX to proceed."
fi

LLVM_VERSION="$($LLVM_CONFIG --version)"
LLVM_VERSION_MAJOR="$(echo $LLVM_VERSION | awk -F. '{print $1}')"
LLVM_VERSION_MINOR="$(echo $LLVM_VERSION | awk -F. '{print $2}')"
LLVM_VERSION_PATCH="$(echo $LLVM_VERSION | awk -F. '{print $3}')"

if [ $LLVM_VERSION_MAJOR -lt $MINIMUM_LLVM_VERSION ]; then
	error "Unsupported LLVM version $LLVM_VERSION: must be 17, 18, 19, 20, 21 or 22"
fi

case "$OS_NAME" in
Darwin)
	darwin_sysroot=
	if [ $(which xcrun) ]; then
		darwin_sysroot="--sysroot $(xcrun --sdk macosx --show-sdk-path)"
	elif [[ -e "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk" ]]; then
		darwin_sysroot="--sysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
	else
		echo "Warning: MacOSX.sdk not found."
	fi

	CXXFLAGS="$CXXFLAGS $($LLVM_CONFIG --cxxflags) ${darwin_sysroot}"
	LDFLAGS="$LDFLAGS $($LLVM_CONFIG --ldflags) -liconv -ldl -framework System -lLLVM"
	;;
FreeBSD)
	CXXFLAGS="$CXXFLAGS $($LLVM_CONFIG --cxxflags)"
	LDFLAGS="$LDFLAGS $($LLVM_CONFIG --ldflags) -lstdc++ $($LLVM_CONFIG --libs core native --system-libs)"
	;;
NetBSD)
	CXXFLAGS="$CXXFLAGS $($LLVM_CONFIG --cxxflags)"
	LDFLAGS="$LDFLAGS $($LLVM_CONFIG --ldflags) -lstdc++ $($LLVM_CONFIG --libs core native --system-libs)"
	;;
Linux)
	CXXFLAGS="$CXXFLAGS $($LLVM_CONFIG --cxxflags)"
	LDFLAGS="$LDFLAGS $($LLVM_CONFIG --ldflags) -lstdc++ -ldl $($LLVM_CONFIG --libs core native passes arm aarch64 x86 webassembly riscv --system-libs --libfiles)"
	# Copy libLLVM*.so into current directory for linking
	# NOTE: This is needed by the Linux release pipeline!
	# cp $(readlink -f $($LLVM_CONFIG --libfiles)) ./
	LDFLAGS="$LDFLAGS -Wl,-rpath=\$ORIGIN"
	;;
OpenBSD)
	CXXFLAGS="$CXXFLAGS -I/usr/local/include $($LLVM_CONFIG --cxxflags)"
	LDFLAGS="$LDFLAGS $($LLVM_CONFIG --ldflags) -lstdc++ -L/usr/local/lib -Wl,-rpath,$($LLVM_CONFIG --libdir) -liconv"
	LDFLAGS="$LDFLAGS $($LLVM_CONFIG --libs core native --system-libs)"
	;;
*)
	error "Platform \"$OS_NAME\" unsupported"
	;;
esac

HAVE_MOLD=0
if command -v mold >/dev/null 2>&1; then
	HAVE_MOLD=1
fi

# 输出 Make 可 include 的变量。注意：
# - llvm-config --ldflags 会多行输出，要把换行折叠成空格，否则 Make 解析失败
# - shell 里的 $ORIGIN 在 Make 里要写成 $$ORIGIN（Make 变量展开规则）
# - Make 会把 " 视为引号字符并剥掉，所以 CPPFLAGS 里的 " 要写成 \"，
#   这样 Make 展开后才保留字面 "，clang 才能正确接收为字符串字面量。
# 每行 VAR = value 形式，Make 的 include 会直接消费。
CPPFLAGS_ESC=$(printf '%s' "$CPPFLAGS" | sed 's/"/\\"/g' | tr '\n' ' ')
CXXFLAGS_BASE_VAL="$(printf '%s' "$CXXFLAGS" | tr '\n' ' ')"
LDFLAGS_BASE_VAL="$(printf '%s' "$LDFLAGS" | sed 's/\$ORIGIN/\$\$ORIGIN/g' | tr '\n' ' ')"

printf '%s\n' \
	"CXX = ${CXX}" \
	"CPPFLAGS = ${CPPFLAGS_ESC}" \
	"CXXFLAGS_BASE = ${CXXFLAGS_BASE_VAL}" \
	"LDFLAGS_BASE = ${LDFLAGS_BASE_VAL}" \
	"DISABLED_WARNINGS = ${DISABLED_WARNINGS}" \
	"LLVM_CONFIG = ${LLVM_CONFIG}" \
	"LLVM_BINDIR = $($LLVM_CONFIG --bindir)" \
	"USE_MOLD = ${HAVE_MOLD}"
