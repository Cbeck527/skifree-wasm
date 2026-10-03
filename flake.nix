{
  description = "SkiFree reverse-engineering workspace (decompile ski32.exe, later port to WASM)";

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
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
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
          ];

          # Not included: wine (to actually run the .exe) — nixpkgs' wine is not
          # available on aarch64-darwin. Options there are CrossOver, Whisky, or
          # a Homebrew wine build; or skip it and go straight to a native port.

          shellHook = ''
            echo "skifree RE shell: ghidra, ghidra-analyzeHeadless, r2, imhex, llvm-objdump, wrestool, python (pefile/capstone/lief/r2pipe)"
          '';
        };
      });
    };
}
