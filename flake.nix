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
      devShells.${system}.default = pkgs.mkShell {
        packages = [
          # Reads and links IBM's 32-bit COFF objects, and runs the reference
          # binary, which is a PE under Wine because those objects are
          # MSVC-mangled and x86-only.
          pkgs.pkgsCross.mingw32.buildPackages.gcc
          pkgs.pkgsCross.mingw32.buildPackages.binutils
          pkgs.wine

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
          export WINEPREFIX="$PWD/.wine"
          # Nothing under Wine plays audio here; keep it away from the sound
          # devices entirely.
          export WINEDLLOVERRIDES="winealsa.drv=d;winepulse.drv=d;wineoss.drv=d"
          export WINEDEBUG=-all
        '';
      };
    };
}
