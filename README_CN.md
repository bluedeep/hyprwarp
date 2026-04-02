# Hyprwarp 

[English Documentation](README.md)

Hyprwarp 是一款专为 [Hyprland](https://github.com/hyprwm/Hyprland) ([Wayland](https://wayland.freedesktop.org/)) 打造的提示定位（Hint Mode）工具，灵感来源于 [warpd](https://github.com/rvaiya/warpd)。

它通过在屏幕上覆盖一层基于网格的提示标签，让用户可以通过输入简短的字符组合，瞬间精准定位坐标。通过执行灵活的可配置回调命令，它可以与 `dotool` 等工具无缝集成，从而实现**完全由键盘驱动的鼠标移动、点击、拖拽和滚动操作**。

> **注意**：Hyprwarp 核心专注于“定位”。它本身并不直接模拟输入设备，而是通过配置文件中的回调命令调用第三方工具（如 `dotool`）来完成实际的鼠标动作。这种设计解耦了定位逻辑与底层模拟，使得功能极其灵活且不重复造轮子。

## 功能特性

<p align="center">
<img src="assets/demo.gif" height="500px"/>
</p>

- **多屏幕支持**：自动检测并支持多显示器，每个显示器有独特的前缀字符
- **基于网格的提示标签**：快速将屏幕划分为可定位的区域。
- **高度可定制的回调系统**：定位后可执行任意 Shell 命令，支持变量替换。
- **自定义外观**：支持配置背景色（透明度）、文字颜色、字体大小及圆角。
- **智能按键支持**：支持去重后的自定义字符集，并完美兼容特殊按键（如 `;`, `]`, `,` 等）。
- **极简配置**：简单的键值对配置，支持自动生成默认配置文件。

## 项目依赖

- `wayland-client`
- `wayland-cursor`
- `xkbcommon`
- `cairo`
- `dotool`（**推荐**：用于执行鼠标移动和点击模拟）

### 安装 dotool (Arch Linux 示例)

建议从源码安装以获取最新功能：
```bash
git clone https://git.sr.ht/~geb/dotool
cd dotool
./build.sh && sudo ./build.sh install # 需要 go 和 scdoc
# 将用户添加到必要的用户组
sudo usermod -aG video $USER
sudo usermod -aG input $USER
# 重启后生效
```

## 构建与安装

### 从源码构建

```bash
make
sudo make install
```

### Nix / NixOS

```bash
# 直接运行
nix run github:bluedeep/hyprwarp

# NixOS flake 配置中安装
# 在你的 flake.nix 中添加 input:
inputs = {
  hyprwarp.url = "github:bluedeep/hyprwarp";
  # ...
};

# 然后在配置中使用:
environment.systemPackages = [
  inputs.hyprwarp.packages.${pkgs.system}.default
];
```

## 使用方法

1. 建议在 Hyprland 中绑定快捷键启动 `hyprwarp`（见下文[配置示例](#hyprland-%E8%BF%9B%E9%98%B6%E9%85%8D%E7%BD%AE%E7%A4%BA%E4%BE%8B)）。
2. 启动后，屏幕会显示提示标签网格。
3. **多屏幕模式**：如果有多个显示器，每个屏幕会有一个唯一的前缀字符（`hint_chars` 的第一个字符对应第一个屏幕，以此类推）。先输入前缀选择屏幕，然后输入标签字符。
4. **单屏幕模式**：直接输入标签对应的字符（如输入 `as`）进行过滤。
5. **功能按键**：
   - `ESC`：取消操作并退出。
   - `Backspace`：删除上一个输入的字符。
6. **触发逻辑**：当输入唯一匹配某个标签时：
   - 首先执行 `on_select_cmd`（通常用于移动鼠标指针）。
   - 随后执行 `on_exit_cmd`（通常用于切换子模式或发送通知）。

## 配置文件

配置文件路径：`~/.config/hyprwarp/config`

首次运行时若文件不存在，将自动创建并填充如下默认值：

| 选项 | 默认值 | 描述 |
| :--- | :--- | :--- |
| `hint_bgcolor` | `#ff555560` | 标签背景色 (ARGB 十六进制) |
| `hint_fgcolor` | `#ffffffff` | 标签文字颜色 |
| `hint_size` | `18` | 字体大小 (像素, 范围 8-64) |
| `hint_radius` | `25` | 圆角半径 (占高度的百分比, 0-100) |
| `hint_chars` | `asdfghjklqwertzxv` | 标签字符集 (决定网格密度) |
| `on_select_cmd` | `hyprctl dispatch movecursor {global_x} {global_y}` | 选中标签后立即触发的命令 |
| `on_exit_cmd` | `hyprctl notify ...` | 最终退出前触发的命令 |

### 命令变量替换

在 `on_select_cmd` 和 `on_exit_cmd` 中，你可以使用以下占位符：

- `{screen_w}`, `{screen_h}`：屏幕原始宽高。
- `{x}`, `{y}`：选中位置的绝对像素坐标（相对于当前屏幕）。
- `{scale_x}`, `{scale_y}`：选中位置的归一化坐标 (0.0 到 1.0)，适合 `dotool` 使用。
- `{global_x}`, `{global_y}`：全局像素坐标（所有屏幕的统一坐标系）。
- `{global_scale_x}`, `{global_scale_y}`：全局归一化坐标（相对于组合屏幕区域）。

## Hyprland 进阶配置示例

将以下配置加入你的 `hyprland.conf`，即可实现类似 `warpd` 的完整键盘控鼠体验：

```ini
# 启动 dotool 服务端
exec-once = dotoold 

$mainMod = SUPER

# 鼠标建议配置：操作时自动隐藏
cursor {
    hide_on_key_press = true 
    inactive_timeout = 3     
}

bind = $mainMod, semicolon, exec, hyprwarp # Super + ; 启动 hyprwarp 进入hints模式

# 定义名为 cursor 的子模式（由 hyprwarp 的 on_exit_cmd 自动触发进入）
submap = cursor

# 进入选择/拖拽模式
bind = , v, exec, echo "buttondown left" | dotoolc  # V 模拟左键持续按住

# 移动鼠标（使用 Vim 风格按键）
binde = , j, exec, echo "mousemove 0 30" | dotoolc  # j 鼠标下移
binde = , k, exec, echo "mousemove 0 -30" | dotoolc # k 鼠标上移
binde = , l, exec, echo "mousemove 30 0" | dotoolc  # l 鼠标右移
binde = , h, exec, echo "mousemove -30 0" | dotoolc # h 鼠标左移

# 精细移动
binde = SHIFT, j, exec, echo "mousemove 0 10" | dotoolc     # shift + j 鼠标缓慢下移
binde = SHIFT, k, exec, echo "mousemove 0 -10" | dotoolc    # shift + k 鼠标缓慢上移
binde = SHIFT, l, exec, echo "mousemove 10 0" | dotoolc     # shift + l 鼠标缓慢右移
binde = SHIFT, h, exec, echo "mousemove -10 0" | dotoolc    # shift + h 鼠标缓慢左移

# 鼠标点击
bind = , space, exec, echo "click left" | dotoolc   # 空格: 左键点击
bind = , q, exec, echo "click left" | dotoolc       # Q 键: 左键点击
bind = , w, exec, echo "click middle" | dotoolc     # W 键：中键点击
bind = , e, exec, echo "click right" | dotoolc      # E 键：右键点击

# 模拟滚轮
bind = , bracketleft, exec, echo "wheel 20" | dotoolc   # [ 向上滚动
bind = , bracketright, exec, echo "wheel -20" | dotoolc # ] 向下滚动

# 再次进入hyprwarp hints模式 
binde = $mainMod, semicolon, exec, echo "buttonup left" | dotoolc; hyprctl keyword cursor:inactive_timeout 3; hyprctl keyword cursor:hide_on_key_press true; hyprctl dispatch submap reset; hyprctl dismissnotify; hyprwarp # Super + ; 重新打开 hints 模式


# 退出鼠标模式
bind = , escape, exec, echo "buttonup left" | dotoolc; hyprctl keyword cursor:inactive_timeout 3; hyprctl keyword cursor:hide_on_key_press true; hyprctl dispatch submap reset; hyprctl dismissnotify # esc 恢复鼠标显示设置，退出子模式，并关闭 Hyprland 通知

submap = reset
```

## 致谢

- [Hyprland](https://github.com/hyprwm/Hyprland)
- [warpd](https://github.com/rvaiya/warpd)
- [dotool](https://sr.ht/~geb/dotool)
- [LINUX DO](https://linux.do)

## 支持项目

如果您觉得这个项目对您有帮助，欢迎请我喝杯咖啡！☕

### 捐助支持

**支付宝** | **微信**
:---:|:---:
![支付宝二维码](assets/alipay-qr.png) | ![微信二维码](assets/wechat-qr.png)


## 许可证

基于 MIT 许可证开源。详见 [LICENSE](LICENSE) 文件。
