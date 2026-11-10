{
  description = "OpenEVV - IBM's Embedded ViaVoice, the Eloquence engine, rebuilt as portable C";

  # The channel tarball rather than a github rev, so this shares the binary
  # cache the machine already populated instead of rebuilding gcc.
  inputs.nixpkgs.url = "https://channels.nixos.org/nixpkgs-unstable/nixexprs.tar.xz";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      # The Windows runtime libraries the cross gcc links against are not
      # "supported" on a Linux host, which is exactly what we want them for.
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnsupportedSystem = true;
      };

      # The Windows compilers with gcc's own win32 threads, which is what
      # Ubuntu's mingw has and so what every release is built with, rather
      # than nixpkgs' mcfgthreads. A library that hung or crashed every
      # program ending with an instance alive passed everything here on
      # mcfgthreads, because only win32 threads free a thread's storage before
      # the library's destructors run. Both are in the binary cache.
      winThreads = platform: import nixpkgs {
        inherit system;
        crossSystem = nixpkgs.lib.systems.examples.${platform};
        config.allowUnsupportedSystem = true;
        overlays = [ (final: prev: { threads = { model = "win32"; package = null; }; }) ];
      };
      mingw32 = winThreads "mingw32";
      mingwW64 = winThreads "mingwW64";

      # The Android SDK is unfree and comes with a licence to accept, so it
      # gets a nixpkgs of its own rather than loosening the one everything
      # else is built with.
      androidPkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
        config.android_sdk.accept_license = true;
      };
      # What android/app/build.gradle.kts asks for: compileSdk, the build tools
      # its Android Gradle plugin defaults to, and ndkVersion, which has to be
      # this exact one or Gradle goes looking for it. The emulator and an
      # x86_64 image are what the device tests and the recorded answers run
      # on here; an x86_64 image runs under KVM at the host's own speed.
      androidBuildTools = "35.0.0";
      androidSdk = (androidPkgs.androidenv.composeAndroidPackages {
        platformVersions = [ "36" ];
        buildToolsVersions = [ androidBuildTools ];
        includeNDK = true;
        ndkVersions = [ "27.0.12077973" ];
        includeEmulator = true;
        includeSystemImages = true;
        systemImageTypes = [ "google_apis" ];
        abiVersions = [ "x86_64" ];
      }).androidsdk;
    in {
      # `nix build' and `nix run'. The ordinary make, which wants a C
      # compiler and Python: the rules a build compiles are written out of the
      # text in lang/<tag>/rules rather than kept beside it, and the default
      # build decompiles them into C after that. Nothing else, so the plain
      # stdenv otherwise.
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        name = "openevv";
        src = self;

        nativeBuildInputs = [ pkgs.python3 pkgs.makeWrapper ];

        # -no-pie is in the Makefile, where it belongs: the machine keeps host
        # addresses in thirty-two bit values, so the program has to sit low
        # enough for one to name it. Nothing here has to switch nixpkgs' PIE
        # hardening off -- this platform does not turn it on, and mkDerivation
        # will not accept the flag. On one that does, it would need
        # hardeningDisable = [ "pie" ].
        enableParallelBuilding = true;
        makeFlags = [ "PREFIX=${placeholder "out"}" ];

        postFixup = ''
          wrapProgram $out/bin/openevv-say \
            --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.pipewire pkgs.pulseaudio pkgs.alsa-utils ]}
        '';

        # No meta.license, deliberately. Our own work is MIT, but the language
        # data under lang is IBM's, so the thing this derivation builds is not
        # MIT as a whole. NOTICE says which is which.
        meta = {
          description = "IBM Embedded ViaVoice rebuilt as portable C";
          mainProgram = "evv";
          platforms = [ system ];
        };
      };

      apps.${system}.default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/evv";
      };

      devShells.${system} = {
        default = pkgs.mkShell {
          packages = [
            # Reads and links IBM's 32-bit COFF objects, and runs the reference
            # binary, which is a PE under Wine because those objects are
            # MSVC-mangled and x86-only.
            mingw32.buildPackages.gcc
            mingw32.buildPackages.binutils

            # Builds the Windows release: the same engine with src/port/port_win32.c
            # standing in for the POSIX layer, linked static so what ships is one
            # file.
            mingwW64.buildPackages.gcc
            mingwW64.buildPackages.binutils

            # Wow64, because there are now two kinds of PE to run: the 32-bit
            # reference the tests compare against, and our own 64-bit build.
            # The 32-bit-only wine answers "Bad EXE format" to the second.
            pkgs.wineWow64Packages.stable

            # The thirty-two bit build, which is a check rather than a target:
            # a difference between the word sizes is a layout mistake, and this
            # is what makes one show up early.
            pkgs.pkgsCross.gnu32.buildPackages.gcc
            pkgs.pkgsCross.gnu32.buildPackages.binutils

            pkgs.llvm
            pkgs.gcc
            pkgs.gnumake
            # The CMU Pronouncing Dictionary is what tools/measure/referee.py
            # settles a disputed English word by, and what test/words.sh counts
            # agreement with when it records.
            (pkgs.python3.withPackages (ps: [ ps.cmudict ]))

            # The rules as C are written again whole when anything they are
            # made from changes, and most of the files come out as they were;
            # this hands back the object for one it has compiled before.
            pkgs.ccache

            # The Speech Dispatcher output module compiles against Speech
            # Dispatcher's own headers and links its out-of-tree module helper.
            # Headers and a library, nothing else: the module hands its samples
            # back to the server rather than opening a device, so building it
            # here neither needs a running server nor touches one.
            pkgs.speechd
            pkgs.pkg-config
            # speech-dispatcher.pc requires glib-2.0, so pkg-config cannot
            # answer for it without glib's own .pc beside it.
            pkgs.glib.dev
          ];

          shellHook = ''
            export EVV_ARCHIVE=/mnt/storage/Software/speech/eloquence-archive
            export WINEPREFIX="$PWD/.wine"
            # Nothing under Wine plays audio here; keep it away from the sound
            # devices entirely.
            export WINEDLLOVERRIDES="winealsa.drv=d;winepulse.drv=d;wineoss.drv=d"
            export WINEDEBUG=-all
          '';
        };

        # `nix develop .#android': the Android voice in android/, built by its
        # Gradle wrapper. The engine half is the ordinary make with the NDK's
        # clang, so Python and make are here as they are above.
        android = pkgs.mkShell {
          packages = [
            androidSdk
            pkgs.jdk17
            pkgs.gnumake
            pkgs.python3
            pkgs.ccache
            pkgs.git
          ];

          shellHook = ''
            export ANDROID_HOME=${androidSdk}/libexec/android-sdk
            export ANDROID_SDK_ROOT=$ANDROID_HOME
            export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/27.0.12077973
            export JAVA_HOME=${pkgs.jdk17.home}
            # The Gradle plugin fetches aapt2 from Maven as a prebuilt Linux
            # binary, which has no loader to run under here. The SDK's own has
            # been patched to run, and this tells the plugin to use it.
            export GRADLE_OPTS="-Dorg.gradle.project.android.aapt2FromMavenOverride=$ANDROID_HOME/build-tools/${androidBuildTools}/aapt2"
          '';
        };
      };
    };
}
