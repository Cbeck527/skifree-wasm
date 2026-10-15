{
  description = "SkiFree reverse-engineering workspace (decompile ski32.exe, port to native and WASM)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs: let
        # Both shells consume the same locked npm graph, including the Railway SDK.
        npmDeps = pkgs.importNpmLock.buildNodeModules {
          npmRoot = ./.;
          inherit (pkgs) nodejs;
        };
        # The linker preserves npm-managed directories; never silently use those.
        checkNpmDeps = pkgs.writeShellScript "check-skifree-node-deps" ''
          ${pkgs.nodejs}/bin/node <<'JS'
          const fs = require('node:fs');
          const pkg = require('${./package.json}');
          for (const name of Object.keys({ ...pkg.dependencies, ...pkg.devDependencies })) {
            let matches = false;
            try {
              matches = fs.realpathSync('node_modules/' + name) ===
                fs.realpathSync('${npmDeps}/node_modules/' + name);
            } catch {}
            if (!matches) {
              console.error('Nix dependency mismatch: node_modules/' + name +
                '. Move the npm-managed node_modules aside and re-enter nix develop. No files were deleted.');
              process.exitCode = 1;
            }
          }
          JS
        '';
      in {
        # Container builds don't need Ghidra, disassemblers, or native SDL tools.
        build = pkgs.mkShell {
          inherit npmDeps checkNpmDeps;
          packages = with pkgs; [
            emscripten
            nodejs
            gnumake
            curl
            cacert
            (python3.withPackages (ps: [ ps.pefile ]))
            importNpmLock.hooks.linkNodeModulesHook
          ];
          shellHook = ''
            linkNodeModulesHook
            ${checkNpmDeps} || exit 1
          '';
        };

        default = pkgs.mkShell {
          # Build the locked server libraries (including ws) in the Nix store.
          inherit npmDeps checkNpmDeps;

          packages = with pkgs; [
            # Decompiler / disassembler. ghidra-bin is the official release build
            # (includes the native decompiler for Apple Silicon); provides `ghidra`
            # (GUI) and `ghidra-analyzeHeadless` (scriptable batch analysis).
            ghidra-bin

            # CLI disassembler / analysis.
            radare2

            # Hex editor with PE pattern support.
            imhex
            hexyl

            # PE/COFF inspection: llvm-objdump -d --x86-asm-syntax=intel,
            # llvm-readobj --file-headers --sections --coff-imports, etc.
            # (GNU/macOS objdump won't parse PE on darwin.)
            llvmPackages.llvm

            # Extract icons/bitmaps from the .rsrc section (wrestool, icotool).
            # SkiFree's sprites live there.
            icoutils

            # Check for packers / embedded data.
            upx
            binwalk

            # Scripting against the binary.
            (python3.withPackages (ps: [
              ps.pefile
              ps.capstone
              ps.lief
              ps.r2pipe
              ps.pillow
            ]))

            # Native port (src/): SDL2 frontend, built with the stdenv C compiler.
            SDL2
            pkg-config
            # make downloads ski32.exe, which isn't in the repo.
            curl

            # Browser/server WASM builds and the authoritative Node server.
            emscripten
            nodejs
            importNpmLock.hooks.linkNodeModulesHook

            # Railway project setup, deployment and operational tooling.
            railway
          ];

          # Not included: wine (to actually run the .exe) — nixpkgs' wine is not
          # available on aarch64-darwin. Options there are CrossOver, Whisky, or
          # a Homebrew wine build; or skip it and go straight to a native port.

          shellHook = ''
            linkNodeModulesHook
            ${checkNpmDeps} || exit 1
            echo "skifree shell: make (native port), ghidra, r2, imhex, llvm-objdump, wrestool, python (pefile/capstone/lief/r2pipe/pillow)"
          '';
        };
      });
    };
}
