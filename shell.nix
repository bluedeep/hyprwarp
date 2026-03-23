{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [
    wayland
    wayland-protocols
    libxkbcommon
    cairo
    pkg-config
    gcc
  ];
}