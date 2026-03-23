{ pkgs ? import <nixpkgs> {} }:

pkgs.stdenv.mkDerivation {
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
    mainProgram = "hyprwarp";
  };
}