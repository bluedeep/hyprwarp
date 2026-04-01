{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    wayland
    wayland-protocols
    libxkbcommon
    cairo
    pkg-config
    gcc
    gnumake
  ];

  shellHook = ''
    echo "hyprwarp development environment"
    echo "Build: make"
    echo "Clean: make clean"
    echo "Install: sudo make install"
  '';
}