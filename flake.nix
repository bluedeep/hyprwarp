{
  description = "A hint mode tool for Hyprland (Wayland)";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "hyprwarp";
          version = "1.0.1";
          src = ./.;
          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            libxkbcommon
            cairo
          ];
          nativeBuildInputs = with pkgs; [
            pkg-config
          ];
          makeFlags = [ "PREFIX=$(out)" ];
          buildPhase = ''
            runHook preBuild
            make
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            make install PREFIX=$out
            runHook postInstall
          '';
          meta = with pkgs.lib; {
            description = "A hint mode tool for Hyprland inspired by warpd";
            homepage = "https://github.com/bluedeep/hyprwarp";
            license = licenses.mit;
            platforms = platforms.linux;
            maintainers = [ ];
            mainProgram = "hyprwarp";
          };
        };
        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            libxkbcommon
            cairo
            pkg-config
            gcc
          ];
        };
      }
    );
}