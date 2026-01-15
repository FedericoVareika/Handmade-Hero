{
    description = "Handmade Hero Development Environment";

    inputs = {
        nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    };

    outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        # The packages you need available in the shell
        packages = with pkgs; [
          gcc
          SDL2
          SDL2.dev
        ];

        # Optional: Print a message when entering the shell
        shellHook = ''
          echo "Handmade Hero Dev Environment Loaded!"
          echo "Compiler: $(gcc --version | head -n 1)"
        '';
      };
    };
}
