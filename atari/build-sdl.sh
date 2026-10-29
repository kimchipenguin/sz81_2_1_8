#!/bin/zsh
# Build SDL-1.2 for the Atari build of sz81 with the m68k-atari-mint
# toolchain: static libraries, one per CPU, installed under
# atari/sdl-install/<cpu> (sdl-config and pkg-config files included).
#
# usage: atari/build-sdl.sh [cpu ...]     default: m68000 m68020-60
#
# then:  make -f Makefile.atari SDL_PREFIX=atari/sdl-install
#        make -f Makefile.atari SDL_PREFIX=atari/sdl-install CPU=-m68020-60
#
# The source is atari/SDL-1.2; when missing, upstream SDL-1.2 is cloned at
# the commit sz81 was tested with (set SDL_COMMIT to use another one). Its
# September 2026 GEM driver reads the keyboard properly.
#
# SDL's Falcon/XBIOS audio driver (the one ARAnyM needs) is only built when
# Miro Kropacek's header-only uSound library is found; it is fetched into
# atari/usound at a fixed commit as well (USOUND_COMMIT to change it).
#
# The patches in atari/sdl-patches are applied to the checkout first. They
# are for sz81's own builds only and are not meant for upstream SDL:
#   gem-message-hook.patch  the GEM driver passes on the AES messages it
#                           does not handle (menu bar)
#
# TOS-compatible configuration as SDL's README.MiNT suggests: no threads
# (SDL then times with the single SDL_SetTimer alarm), no OpenGL/LDG.

set -e
here=${0:A:h}
src=$here/SDL-1.2
repo=https://github.com/libsdl-org/SDL-1.2.git
commit=${SDL_COMMIT:-81490298a02fe6806c05ac7b5afde7d88fcc7bf9}
usound=$here/usound
usound_repo=https://github.com/mikrosk/usound.git
usound_commit=${USOUND_COMMIT:-abd8e213d16d50e492faf2e05ecdae099a500d0a}
cpus=("$@"); (( $#cpus )) || cpus=(m68000 m68020-60)

if [[ ! -d $src ]]; then
	git clone $repo $src
	git -C $src checkout -q $commit
fi

if [[ ! -d $usound ]]; then
	git clone $usound_repo $usound
	git -C $usound checkout -q $usound_commit
fi

for patch in $here/sdl-patches/*.patch(N); do
	if git -C $src apply --reverse --check $patch 2>/dev/null; then
		echo "already applied: ${patch:t}"
	else
		git -C $src apply $patch
		echo "applied: ${patch:t}"
	fi
done

for cpu in $cpus; do
	build=$here/sdl-build/$cpu
	prefix=$here/sdl-install/$cpu
	mkdir -p $build
	cd $build
	CFLAGS="-$cpu -O2 -fomit-frame-pointer" CPPFLAGS="-I$usound" \
	$src/configure --host=m68k-atari-mint --prefix=$prefix \
		--disable-shared --enable-static \
		--disable-threads --disable-pthreads --disable-pth \
		--disable-video-opengl --disable-loadso --disable-atari-ldg \
		> configure.log 2>&1 || { tail -30 configure.log; exit 1; }
	make -j8 > make.log 2>&1 || { grep -E "error" make.log | head -30; exit 1; }
	make install > install.log 2>&1
	echo "$cpu: $(ls -la $prefix/lib/libSDL.a)"
done
