# Jonsbo Display

Windows x64 的 Jonsbo 480×480 水冷屏控制程序。C++20 / Direct2D / DirectComposition；
支持系统状态、Home Assistant、Codex 剩余额度、系统媒体与浏览器画中画捕获。
桌面控制面板支持 DPI 缩放、半透明按钮和退出确认。

## 构建与运行

需要 Visual Studio 2022 或更新版本的 C++ Build Tools、Windows SDK（含 C++/WinRT）、
CMake 3.24+。字体使用系统安装的 Noto Sans CJK SC；仓库不附带字体。

在 x64 开发者终端执行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\jonsbo-display.exe
```

USB 输出需要单独安装 JONSBO-AIO 及其驱动，并退出官方程序。
运行时从 `%LOCALAPPDATA%/JONSBO-AIO/dll/x64/` 加载厂商 DLL；仓库不包含它们。
程序默认输出到 USB；`--no-usb` 仅运行桌面面板。Lucide 图标资源编译进 EXE，
运行时不依赖外部 assets 目录。用 `--tab monitor|home|codex|music|video` 切页；
正常退出会记忆当前页。`--reduced-motion` 关闭动画，`--seconds N` 限时运行。
`--check-integrations` 可在不启动界面和 USB 输出的情况下检查本地集成。

## 本地配置

在可执行文件旁创建 `jonsbo-display.ini`（已忽略），或传入 `--config 路径`：

```ini
[home_assistant]
url=https://your-home-assistant.example
temperature=sensor.bedroom_temperature
humidity=sensor.bedroom_humidity
ceiling=light.bedroom_ceiling
spot=light.bedroom_spot
strip=light.bedroom_strip
curtain=cover.bedroom_curtain
sheer=cover.bedroom_sheer
air_conditioner=climate.bedroom_ac
floor_heating=climate.bedroom_heating

[codex]
; 可选；默认依次查找 PATH 和已安装桌面应用的 bundled executable
; executable=C:/path/to/codex.exe
```

HA 只读实体状态；令牌通过 `--ha-token-stdin 主机名` 从标准输入写入 Windows
凭据管理器，不要放进 INI、命令行参数或 Git。Codex 使用本机现有登录读取额度。

CPU/GPU 温度由可选的 C++/CLI 桥接 DLL 调用 `LibreHardwareMonitorLib` 读取；
构建时若能找到 NuGet，会自动恢复依赖。桥接目标为 .NET Framework 4.7.2，
不要求安装 .NET SDK。部分 CPU 传感器需要单独安装 PawnIO 驱动并以管理员权限读取；
缺少桥接 DLL、驱动或传感器权限时显示 `—℃`，不影响其余功能。画中画需先打开
浏览器已有的小窗。
桌面层级适配 Windows 桌面及 Aura 壁纸，不保证兼容所有壁纸软件。
不支持登录前服务模式；项目不会自动修改开机启动项。

## 许可证

本项目原创代码采用 **GPL-3.0-only，附 Jonsbo SDK 链接例外**，见
[LICENSE](LICENSE) 和 [LICENSE-SDK-EXCEPTION](LICENSE-SDK-EXCEPTION)。
例外仅允许与单独取得的厂商运行库组合，不授予厂商 DLL 的再分发权；
厂商许可仍需单独核实。

仅保留实际使用的 [Lucide](https://github.com/lucide-icons/lucide) SVG，
版本 `951813ce76a859d4d8b145366972cbb237147a4e`；
ISC / 部分 Feather 衍生图标 MIT，完整声明见 [assets/lucide/LICENSE](assets/lucide/LICENSE)。
可选温度桥接使用
[LibreHardwareMonitorLib 0.9.6](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)，
遵循 MPL-2.0；其依赖项保留各自许可证。
Noto 字体由用户单独安装；Windows SDK/运行库遵循各自许可。本项目不使用 FFmpeg。
