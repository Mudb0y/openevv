{
  description = "openevv - a portable Eloquence / Embedded ViaVoice";

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
    in {
      # `nix build' and `nix run'. The ordinary make, which wants a C
      # compiler and nothing else, so this is the plain stdenv and no inputs.
      packages.${system}.default = pkgs.stdenv.mkDerivation {
        name = "openevv";
        src = self;

        # -no-pie is in the Makefile, where it belongs: the machine keeps host
        # addresses in thirty-two bit values, so the program has to sit low
        # enough for one to name it. Nothing here has to switch nixpkgs' PIE
        # hardening off -- this platform does not turn it on, and mkDerivation
        # will not accept the flag. On one that does, it would need
        # hardeningDisable = [ "pie" ].
        enableParallelBuilding = true;
        makeFlags = [ "PREFIX=${placeholder "out"}" ];

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

      devShells.${system}.default = pkgs.mkShell {
        packages = [
          # Reads and links IBM's 32-bit COFF objects, and runs the reference
          # binary, which is a PE under Wine because those objects are
          # MSVC-mangled and x86-only.
          pkgs.pkgsCross.mingw32.buildPackages.gcc
          pkgs.pkgsCross.mingw32.buildPackages.binutils

          # Builds the Windows release: the same engine with src/port_win32.c
          # standing in for the POSIX layer, linked static so what ships is one
          # file.
          pkgs.pkgsCross.mingwW64.buildPackages.gcc
          pkgs.pkgsCross.mingwW64.buildPackages.binutils

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
          pkgs.python3
        ];

        shellHook = ''
          export EVV_ARCHIVE=/mnt/storage/Software/eloquence-archive
          # The cross gcc is built against mcfgthreads but nothing puts it on
          # the link path outside a real cross stdenv. Referenced by path
          # rather than as a package because nixpkgs splicing would otherwise
          # substitute a native build of it.
          export MINGW_LDFLAGS="-L${pkgs.pkgsCross.mingw32.windows.mcfgthreads.outPath}/lib"
          export MINGW64_LDFLAGS="-L${pkgs.pkgsCross.mingwW64.windows.mcfgthreads.outPath}/lib"
          export WINEPREFIX="$PWD/.wine"
          # Nothing under Wine plays audio here; keep it away from the sound
          # devices entirely.
          export WINEDLLOVERRIDES="winealsa.drv=d;winepulse.drv=d;wineoss.drv=d"
          export WINEDEBUG=-all
        '';
      };
    };
}
