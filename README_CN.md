# Hyprwarp 

[English Documentation](README.md)

Hyprwarp 是一款专为 [Hyprland](https://github.com/hyprwm/Hyprland) ([Wayland](https://wayland.freedesktop.org/)) 打造的提示定位（Hint Mode）工具，灵感来源于 [warpd](https://github.com/rvaiya/warpd)。

它通过在屏幕上覆盖一层基于网格的提示标签，让用户可以通过输入简短的字符组合，瞬间精准定位坐标。通过执行灵活的可配置回调命令，它可以与 `dotool` 等工具无缝集成，从而实现**完全由键盘驱动的鼠标移动、点击、拖拽和滚动操作**。

> **注意**：Hyprwarp 核心专注于“定位”。它本身并不直接模拟输入设备，而是通过配置文件中的回调命令调用第三方工具（如 `dotool`）来完成实际的鼠标动作。这种设计解耦了定位逻辑与底层模拟，使得功能极其灵活且不重复造轮子。

## 功能特性

<p align="center">
<img src="assets/demo.gif" height="500px"/>
</p>

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

```bash
# 构建
make

# 安装 (默认到 /usr/local/bin)
sudo make install

# 或者安装到指定路径
sudo make install PREFIX=/usr
```

## 使用方法

1. 建议在 Hyprland 中绑定快捷键启动 `hyprwarp`（见下文[配置示例](#hyprland-%E8%BF%9B%E9%98%B6%E9%85%8D%E7%BD%AE%E7%A4%BA%E4%BE%8B)）。
2. 启动后，屏幕会显示提示标签网格。
3. 输入标签对应的字符（如输入 `as`）进行过滤。
4. **功能按键**：
   - `ESC`：取消操作并退出。
   - `Backspace`：删除上一个输入的字符。
5. **触发逻辑**：当输入唯一匹配某个标签时：
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
| `on_select_cmd` | `echo mouseto {scale_x} {scale_y} \| dotool` | 选中标签后立即触发的命令 |
| `on_exit_cmd` | `hyprctl notify ...` | 最终退出前触发的命令 |

### 命令变量替换

在 `on_select_cmd` 和 `on_exit_cmd` 中，你可以使用以下占位符：

- `{screen_w}`, `{screen_h}`：屏幕原始宽高。
- `{x}`, `{y}`：选中位置的绝对像素坐标。
- `{scale_x}`, `{scale_y}`：选中位置的归一化坐标 (0.0 到 1.0)，适合 `dotool` 使用。

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

# 1. 绑定启动快捷键 (Win + ;)
bind = $mainMod, semicolon, exec, hyprwarp

# 2. 定义名为 cursor 的子模式（由 hyprwarp 的 on_exit_cmd 自动触发进入）
submap = cursor

# V 键：模拟左键按住（进入拖拽模式）
bind = , v, exec, echo "buttondown left" | dotoolc 

# 基础移动 (Vim 风格: hjkl)
binde = , j, exec, echo "mousemove 0 5" | dotoolc
binde = , k, exec, echo "mousemove 0 -5" | dotoolc
binde = , l, exec, echo "mousemove 5 0" | dotoolc
binde = , h, exec, echo "mousemove -5 0" | dotoolc

# 快速移动 (Shift + hjkl)
binde = SHIFT, j, exec, echo "mousemove 0 30" | dotoolc
binde = SHIFT, k, exec, echo "mousemove 0 -30" | dotoolc
binde = SHIFT, l, exec, echo "mousemove 30 0" | dotoolc
binde = SHIFT, h, exec, echo "mousemove -30 0" | dotoolc

# 点击模拟
bind = , space, exec, echo "click left" | dotoolc    # 空格：左键
bind = , f,     exec, echo "click right" | dotoolc   # F 键：右键
bind = , m,     exec, echo "click middle" | dotoolc  # M 键：中键

# 滚轮模拟
binde = , bracketleft,  exec, echo "wheel 20" | dotoolc  # [ : 向上滚
binde = , bracketright, exec, echo "wheel -20" | dotoolc # ] : 向下滚

# 3. 退出鼠标模式 (ESC)
# 恢复鼠标显示设置，退出子模式，并关闭 Hyprland 通知
bind = , escape, exec, echo "buttonup left" | dotoolc; hyprctl keyword cursor:inactive_timeout 3; hyprctl keyword cursor:hide_on_key_press true; hyprctl dispatch submap reset; hyprctl dismissnotify

submap = reset
```

## 支持项目

如果您觉得这个项目对您有帮助，欢迎请我喝杯咖啡！☕

### 捐助支持

**支付宝** | **微信**
:---:|:---:
![支付宝二维码](assets/alipay-qr.png) | ![微信二维码](assets/wechat-qr.png)


## 许可证

基于 MIT 许可证开源。详见 [LICENSE](LICENSE) 文件。
