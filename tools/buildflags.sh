# Compiler and linker flags for release builds. Source it: `. tools/buildflags.sh`.
# ARCHITECTURE_LEVEL=x86-64 is the v1 baseline (every 64-bit x86 CPU).
export ARCHITECTURE_LEVEL="${ARCHITECTURE_LEVEL:-x86-64}"
export GCC_CFLAGS="-O3 -march=$ARCHITECTURE_LEVEL -mtune=generic -mtls-dialect=gnu2 -fno-semantic-interposition -fno-trapping-math -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -flto=auto -fno-plt -fipa-pta -fno-ident -floop-nest-optimize -fdevirtualize-at-ltrans -fipa-reorder-for-locality -fgcse-sm -fgcse-las -favoid-store-forwarding"
export GCC_CXXFLAGS="$GCC_CFLAGS -fvisibility-inlines-hidden"
export LDFLAGS="-Wl,--gc-sections -Wl,--as-needed -Wl,--build-id=none -Wl,-O2 -Wl,--strip-all -Wl,--sort-common -Wl,--sort-section=alignment -Wl,-z,pack-relative-relocs -Wl,-z,noseparate-code -Wl,--hash-style=gnu"
export CLANG_CFLAGS="-O3 -march=$ARCHITECTURE_LEVEL -mtune=generic -mtls-dialect=gnu2 -fno-pie -fno-semantic-interposition -fno-trapping-math -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections -flto=auto -fno-plt -fno-ident -faddrsig -fveclib=libmvec -Wno-unused-command-line-argument"
export CLANG_CXXFLAGS="$CLANG_CFLAGS -fvisibility-inlines-hidden -fwhole-program-vtables -fstrict-vtable-pointers"
export LLDFLAGS="${LDFLAGS/-Wl,--sort-common/} -fuse-ld=lld -Wl,--no-pie -Wl,-z,keep-text-section-prefix -Wl,--icf=safe -Wl,--lto-O3 -Wl,--lto-CGO3 -Wl,--lto-whole-program-visibility"
export RUSTFLAGS="-Copt-level=3 -Ctarget-cpu=$ARCHITECTURE_LEVEL -Ztune-cpu=generic -Cstrip=symbols -Cforce-unwind-tables=no -Clto=fat -Clinker=clang -Clink-arg=-fuse-ld=lld -Clink-arg=-Wl,--gc-sections -Clink-arg=-Wl,-O2 -Clink-arg=-Wl,--strip-all -Clink-arg=-Wl,--icf=safe -Clink-arg=-Wl,--lto-O3 -Clink-arg=-Wl,--lto-CGO3 -Clink-arg=-Wl,--lto-whole-program-visibility -Clink-arg=-Wl,-z,pack-relative-relocs -Clink-arg=-Wl,--hash-style=gnu -Clink-arg=-Wl,-z,keep-text-section-prefix -Cllvm-args=-enable-dfa-jump-thread -Cpanic=unwind -Cdebuginfo=0 -Cembed-bitcode=yes -Zdylib-lto -Zlocation-detail=none -Zfmt-debug=shallow -Ccodegen-units=1"
export RUSTC_BOOTSTRAP=1

# What cmake gets: the clang set when CC is clang, the GCC set otherwise.
case "$(${CC:-cc} --version 2>/dev/null | head -1)" in
  *clang*) BUILD_C_FLAGS="$CLANG_CFLAGS"; BUILD_CXX_FLAGS="$CLANG_CXXFLAGS"; BUILD_LD_FLAGS="$LLDFLAGS" ;;
  *)       BUILD_C_FLAGS="$GCC_CFLAGS";   BUILD_CXX_FLAGS="$GCC_CXXFLAGS";   BUILD_LD_FLAGS="$LDFLAGS" ;;
esac
export BUILD_C_FLAGS BUILD_CXX_FLAGS BUILD_LD_FLAGS
# The Rust flag set links through clang + lld; without them cargo would fail.
if ! command -v clang >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
  unset RUSTFLAGS
fi
