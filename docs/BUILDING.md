# 编译说明

YeSymbol 使用 CMake 构建，Windows 发布版本默认使用 Visual Studio x64 和静态 CRT。

## 环境要求

- Windows 10 或 Windows 11；
- Visual Studio 或 Visual Studio Build Tools；
- 安装“使用 C++ 的桌面开发”；
- Windows SDK；
- CMake 3.20 或更高版本。

确认 CMake：

```powershell
cmake --version
```

## 统一构建入口

项目只保留一个 `build.bat`：

```powershell
.\build.bat
.\build.bat build
```

执行增量 Release 编译，产物为：

```text
dist\yesymbol.exe
```

只清理：

```powershell
.\build.bat clean
```

清理后完整编译：

```powershell
.\build.bat all
```

结束旧程序、清理、完整编译并启动：

```powershell
.\build.bat run
```

该命令会自动重新生成 `catalog.txt` 对应的 JSON/C 数据；目录语法有误时构建立即停止。

只重新生成目录数据：

```powershell
.\build.bat data
```

联网刷新固定版本的 Unicode CLDR 中文短名称并重新生成数据：

```powershell
.\build.bat cldr
```

查看帮助：

```powershell
.\build.bat help
```


## DirectWrite 编译单元

Windows SDK 10.0.26100 的 `dwrite.h` 含有 C++ 专用语法。项目主体仍以 C11 编译，但 CMake 会把 `src/emoji_renderer.c` 单独设置为 `LANGUAGE CXX`。该文件定义 `CINTERFACE`、`COBJMACROS` 并通过 `extern "C"` 暴露接口，不使用 STL 或 RTTI。不要把该文件强制改回 `/TC`，否则会再次出现 `dwrite.h` 的 `C2059` 和 `static_cast` 错误。

## CMake 缓存处理

脚本在 `build\CMakeCache.txt` 已存在时，不会强制改变生成器平台，而是先复用原来的生成器和平台。

如果缓存确实不兼容，脚本会自动删除 `build`，然后使用 Visual Studio x64 重新配置。因此不需要手工删除 `CMakeCache.txt`。

典型错误：

```text
generator platform: x64
Does not match the platform used previously
```

这个错误通常由旧脚本在已有缓存上突然加入 `-A x64` 引起。rc8 已修复。

## Manifest

`src/resource.rc` 已手工内嵌 manifest，因此 MSVC 链接选项使用：

```text
/MANIFEST:NO
```

不要删除这一配置，否则可能出现 `CVT1100` 和 `LNK1123` 资源重复错误。

## 常见问题

### dist 无法删除

通常是 `yesymbol.exe` 仍在运行。使用：

```powershell
.\build.bat run
```

该命令会先结束旧进程。


### `symbol_data.c` PermissionError

连续执行：

```powershell
.\regenerate-data.cmd
.\build.bat run
```

旧版可能在第二次生成时报：

```text
PermissionError: [Errno 13] Permission denied: src\symbol_data.c
```

rc13 会先比较目标文件内容。数据没有变化时直接跳过写入；有变化时解除只读属性，使用临时文件原子替换并自动重试。正常情况下不需要关闭编辑器或手动删除该文件。

### 找不到 cmake.exe

安装 CMake，或在 Visual Studio Installer 中启用对应 CMake 工具。

### 运行了旧程序

使用 `build.bat run`，它会删除旧的 `build` 和 `dist`，确保启动新产物。


## 资源与图标

`src/resource.rc` 会嵌入：

- `assets/yesymbol.ico`：EXE、标题栏、任务栏和托盘图标；
- `src/yesymbol.manifest`：Windows Common Controls v6 和 DPI 配置；
- 版本资源。

MSVC 链接参数继续保留 `/MANIFEST:NO`，防止链接器默认 manifest 与 `resource.rc` 中的手工 manifest 重复。

## DirectWrite 彩色 Emoji

v1.0.1 起，彩色 Emoji 使用 Windows SDK 自带的 DirectWrite 和 Direct2D：

```text
src\emoji_renderer.c
include\emoji_renderer.h
```

CMake 必须链接：

```text
dwrite.lib
d2d1.lib
```

它们是 Windows 系统组件，不需要随 EXE 分发额外 DLL。颜色字体绘制失败时，程序会退回 GDI 单色路径。
