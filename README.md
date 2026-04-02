# Hyprwarp

[中文文档](README_CN.md)

Hyprwarp is a hint mode tool designed for [Hyprland](https://github.com/hyprwm/Hyprland) ([Wayland](https://wayland.freedesktop.org/)), inspired by [warpd](https://github.com/rvaiya/warpd).

It overlays a grid-based hint system on the screen, allowing users to instantly pinpoint coordinates by typing short character combinations. Through configurable callback commands, it seamlessly integrates with tools like `dotool` to enable **fully keyboard-driven mouse movement, clicking, dragging, and scrolling**.

> **Note**: Hyprwarp focuses on "positioning". It doesn't directly simulate input devices, but instead calls third-party tools (like `dotool`) via callback commands to perform actual mouse actions. This design decouples positioning logic from low-level simulation, making it extremely flexible and avoiding reinventing the wheel.

## Features

<p align="center">
<img src="assets/demo.gif" height="500px"/>
</p>

- **Multi-screen support**: Automatically detects and works across multiple monitors with unique screen prefix characters
- **Grid-based hint tags**: Quickly divides the screen into positionable areas
- **Highly customizable callback system**: Execute arbitrary shell commands after positioning with variable substitution
- **Customizable appearance**: Configure background color (with transparency), text color, font size, and corner radius
- **Smart key support**: Supports deduplicated custom character sets and handles special keys (like `;`, `]`, `,`)
- **Minimal configuration**: Simple key-value configuration with automatic default config generation

## Dependencies

- `wayland-client`
- `wayland-cursor`
- `xkbcommon`
- `cairo`
- `dotool` (**Recommended**: for mouse movement and click simulation)

### Installing dotool (Arch Linux example)

Recommended to install from source for latest features:

```bash
git clone https://git.sr.ht/~geb/dotool
cd dotool
./build.sh && sudo ./build.sh install  # Requires go and scdoc
# Add user to necessary groups
sudo usermod -aG video $USER
sudo usermod -aG input $USER
# Reboot for changes to take effect
```

## Building and Installation

### From Source

```bash
make
sudo make install
```

### Nix / NixOS

```bash
# Run directly
nix run github:bluedeep/hyprwarp

# Install in NixOS flake configuration
# Add to your flake.nix inputs:
inputs = {
  hyprwarp.url = "github:bluedeep/hyprwarp";
  # ...
};

# Then use in your configuration:
environment.systemPackages = [
  inputs.hyprwarp.packages.${pkgs.system}.default
];
```

## Usage

1. Recommended to bind a keyboard shortcut in Hyprland to launch `hyprwarp` ([see configuration example below](#advanced-hyprland-configuration-example))
2. After launching, a grid of hint tags appears on screen
3. **Multi-screen mode**: If you have multiple monitors, each screen has a unique prefix character (first character of `hint_chars` corresponds to the first screen, and so on). Type the prefix to select a screen, then type the hint label.
4. **Single-screen mode**: Simply type characters corresponding to tags (e.g., type `as`) to filter
5. **Function keys**:
   - `ESC`: Cancel and exit
   - `Backspace`: Delete last input character
6. **Trigger logic**: When input uniquely matches a tag:
   - First executes `on_select_cmd` (typically to move mouse pointer)
   - Then executes `on_exit_cmd` (typically to switch submodes or send notifications)

## Configuration

Configuration file path: `~/.config/hyprwarp/config`

If the file doesn't exist on first run, it will be automatically created with these default values:

| Option | Default | Description |
| :--- | :--- | :--- |
| `hint_bgcolor` | `#ff555560` | Hint background color (ARGB hex) |
| `hint_fgcolor` | `#ffffffff` | Hint text color |
| `hint_size` | `18` | Font size in pixels (range 8-64) |
| `hint_radius` | `25` | Corner radius as percentage of height (0-100) |
| `hint_chars` | `asdfghjklqwertzxv` | Character set for hints (determines grid density) |
| `on_select_cmd` | `hyprctl dispatch movecursor {global_x} {global_y}` | Command triggered immediately after selecting a hint |
| `on_exit_cmd` | `hyprctl notify ...` | Command triggered before final exit |

### Command Variable Substitution

In `on_select_cmd` and `on_exit_cmd`, you can use these placeholders:

- `{screen_w}`, `{screen_h}`: Original screen width and height
- `{x}`, `{y}`: Absolute pixel coordinates of selected position (relative to current screen)
- `{scale_x}`, `{scale_y}`: Normalized coordinates (0.0 to 1.0), suitable for `dotool`
- `{global_x}`, `{global_y}`: Global pixel coordinates across all screens (unified coordinate system)
- `{global_scale_x}`, `{global_scale_y}`: Global normalized coordinates (relative to combined screen area)

## Advanced Hyprland Configuration Example

Add the following to your `hyprland.conf` for a complete keyboard-driven mouse experience similar to `warpd`:

```ini
# Start dotool daemon
exec-once = dotoold 

$mainMod = SUPER

# Recommended mouse configuration: auto-hide during operations
cursor {
    hide_on_key_press = true 
    inactive_timeout = 3     
}

bind = $mainMod, semicolon, exec, hyprwarp  # Super + ; to launch hyprwarp hints mode

# Define cursor submap (automatically triggered by hyprwarp's on_exit_cmd)
submap = cursor

# Enter drag mode
bind = , v, exec, echo "buttondown left" | dotoolc  # V: hold left button

# Mouse movement (Vim style)
binde = , j, exec, echo "mousemove 0 30" | dotoolc   # j: move down
binde = , k, exec, echo "mousemove 0 -30" | dotoolc  # k: move up
binde = , l, exec, echo "mousemove 30 0" | dotoolc   # l: move right
binde = , h, exec, echo "mousemove -30 0" | dotoolc  # h: move left

# Fine movement
binde = SHIFT, j, exec, echo "mousemove 0 10" | dotoolc      # shift + j: slow down
binde = SHIFT, k, exec, echo "mousemove 0 -10" | dotoolc      # shift + k: slow up
binde = SHIFT, l, exec, echo "mousemove 10 0" | dotoolc       # shift + l: slow right
binde = SHIFT, h, exec, echo "mousemove -10 0" | dotoolc      # shift + h: slow left

# Click simulation
bind = , space, exec, echo "click left" | dotoolc    # Space: left click
bind = , q, exec, echo "click left" | dotoolc        # Q: left click
bind = , w, exec, echo "click middle" | dotoolc      # W: middle click
bind = , e, exec, echo "click right" | dotoolc       # E: right click

# Scroll wheel
bind = , bracketleft, exec, echo "wheel 20" | dotoolc   # [: scroll up
bind = , bracketright, exec, echo "wheel -20" | dotoolc # ]: scroll down

# Re-enter hyprwarp hints mode
binde = $mainMod, semicolon, exec, echo "buttonup left" | dotoolc; hyprctl keyword cursor:inactive_timeout 3; hyprctl keyword cursor:hide_on_key_press true; hyprctl dispatch submap reset; hyprctl dismissnotify; hyprwarp  # Super + ;: reopen hints mode

# Exit cursor mode
bind = , escape, exec, echo "buttonup left" | dotoolc; hyprctl keyword cursor:inactive_timeout 3; hyprctl keyword cursor:hide_on_key_press true; hyprctl dispatch submap reset; hyprctl dismissnotify  # ESC: restore settings, exit submap, close notification

submap = reset
```

## Acknowledgments

- [Hyprland](https://github.com/hyprwm/Hyprland)
- [warpd](https://github.com/rvaiya/warpd)
- [dotool](https://sr.ht/~geb/dotool)
- [LINUX DO](https://linux.do)

## Support

If you find this project helpful, consider buying me a coffee! ☕

### Donation

**AFDIAN** | **Alipay** | **WeChat Pay**
:---:|:---:|:---:
If you can't use Alipay or WeChat, you can support my work through [AFDIAN](https://afdian.com/a/bluedeep), thank you very much! [https://afdian.com/a/bluedeep](https://afdian.com/a/bluedeep) | ![Alipay QR Code](assets/alipay-qr.png) | ![WeChat Pay QR Code](assets/wechat-qr.png)

## License

Open source under MIT License. See [LICENSE](LICENSE) file for details.
