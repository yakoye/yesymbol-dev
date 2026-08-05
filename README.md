# YeSymbol（符号大全）

<p align="center">
  <img src="assets/yesymbol-512.png" width="104" height="104" alt="YeSymbol icon">
</p>

> 面向 Windows 10/11 的原生 Unicode 符号与彩色 Emoji 选择器。

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4)
![Language](https://img.shields.io/badge/language-C11%20%2B%20DirectWrite%20bridge-00599C)
![UI](https://img.shields.io/badge/UI-Win32%20%2B%20DirectWrite-5C2D91)
![License](https://img.shields.io/badge/license-MIT-green)
![Version](https://img.shields.io/badge/version-1.1.0-blue)

仓库：<https://github.com/yakoye/yesymbol-dev>

## 项目介绍

YeSymbol 是一个以 C11 为主体、使用 Win32 API、DirectWrite 和 Direct2D 开发的 Windows 符号工具。它用于快速浏览、搜索、收藏、复制和插入 Unicode 符号与 Emoji。

程序发布后只有一个便携的 `yesymbol.exe`：

- 无需安装；
- 不依赖 .NET、Qt、Electron；
- 不需要安装特定输入法；
- 运行时不需要 Python；
- 运行时不读取 TXT 或 JSON；
- 不携带或分发字体文件。

当前目录内置约 16,679 个去重符号或 Unicode 序列，覆盖标点、编号字母、数学、单位、拼音、音标、东亚字符、历史文字、Emoji、国家和地区旗帜等内容。

## 开发目的

Windows 自带的 `Win + .` 面板以及输入法附带的符号面板，可能存在启动速度、分类方式、内容覆盖和输入法依赖方面的限制。

YeSymbol 的开发目标是：

- 提供一个独立、轻量、启动迅速的符号工具；
- 让常用符号、专业字符和 Emoji 可以统一搜索；
- 使用普通 TXT 文件维护符号数据，降低人工整理成本；
- 保持单 EXE 发布，不把 JSON 解析放到运行时；
- 使用 DirectWrite 和 Direct2D 正确呈现 Windows 彩色 Emoji 字体；
- 让符号目录可以持续增加、删除、分组和重排。

## 功能特点

- 中文名称、英文名称、符号文本、分类名称和 Unicode 编码搜索；
- 搜索结果显示原始分类、行号和所在位置；
- 搜索结果右键可“跳到所在位置”；
- 搜索框使用 `↑`、`↓` 回显历史搜索；
- 顶部显示最近使用，最多一行 17 个，并按可用宽度自动铺满；
- 常用符号固定每行显示 12 个，顺序由用户维护，不再按次数自动重排；
- 支持自定义符号和完整 Unicode 序列；
- 支持 Emoji 肤色、ZWJ 组合和区域指示符序列；
- Emoji 优先使用构建期嵌入的 Twemoji 图片，以 36×36 px 显示；点击时仍复制或插入原始 Unicode 序列；
- DirectWrite 初始化失败时自动退回 GDI 单色绘制，不影响复制；
- 可选“自动插入”，并记住用户设置；
- 支持窗口置顶和系统托盘；
- 大分类使用虚拟布局，只绘制当前可见区域；
- 符号查找使用构建期生成的哈希索引；
- 用户数据保存在当前用户注册表，不写入程序目录；常用符号采用固定顺序模型，并支持旧版数据迁移。

## 快速开始

### 直接运行

1. 获取 `yesymbol.exe`；
2. 放到任意可写或只读目录；
3. 双击运行。

关闭主窗口时，程序默认隐藏到系统托盘。真正退出需要：

```text
托盘图标右键 → 退出
```

“关于 YeSymbol”中包含可点击仓库和 Unicode List 链接：

```text
GitHub：yesymbol-dev
Unicode List：emoji-list.html
```

### 从源码编译运行

#### 编译环境要求

- Windows 10 或 Windows 11；
- Visual Studio 2022、Visual Studio 2026 或对应 Build Tools；
- Visual Studio 的“使用 C++ 的桌面开发”工作负载；
- Windows 10/11 SDK；
- CMake 3.20 或更高版本；
- Python 3.10 或更高版本，仅用于构建期数据生成。

程序使用以下 Windows 系统组件：

```text
Win32 API
GDI
DirectWrite
Direct2D
Common Controls
Shell API
```

Python不是 `yesymbol.exe` 的运行依赖。

#### 一键编译并运行

在项目根目录打开 PowerShell：

```powershell
.\build.bat run
```

该命令依次执行：

```text
结束正在运行的 yesymbol.exe
→ CMake 配置
→ Release 编译
→ 运行 dist\yesymbol.exe
```

其他命令：

```powershell
.\build.bat          # 增量 Release 编译
.\build.bat build    # 增量 Release 编译
.\build.bat clean    # 删除 build 和 dist
.\build.bat data     # TXT → JSON → C，不编译 EXE
.\build.bat cldr     # 刷新固定版本 CLDR 中文名称并重新生成数据
.\build.bat all      # 生成数据、清理并完整编译
.\build.bat run      # 增量编译并运行（不重新生成数据）
.\build.bat help     # 查看帮助
```

编译结果：

```text
dist\yesymbol.exe
```

CMake 中必须保留：

```text
/MANIFEST:NO
```

`src/resource.rc` 已手工嵌入 manifest。重复生成 manifest 可能导致 `CVT1100` 或 `LNK1123`。

## 使用方法

### 复制符号

- 单击符号：复制到剪贴板；
- 勾选“自动插入”：单击图片后向此前窗口直接发送对应 Unicode 文本；
- 鼠标悬浮：查看中英文名称、分类、位置信息和编码；
- 右键符号：加入常用、移出常用、删除记录、切换肤色或跳转原位置。

底部信息栏显示：

```text
符号  中文名称  Unicode编码  英文名称  分类
```

### 搜索

搜索框支持：

```text
→             符号本身
箭头          中文名称
arrow         英文名称
U+2192        Unicode编码
2192          十六进制编码
Emoji·动物    分类名称
```

快捷键：

```text
↑      更早的搜索记录
↓      更新的搜索记录，最后恢复当前草稿
Enter  确认并保存当前搜索
Esc    清空搜索
```

搜索结果悬浮信息格式：

```text
符号：
中文名称：
英文名称：
分类：
信息：第 N 行，第 M 个
编码：
```

右键选择“跳到所在位置”，程序会清空搜索、展开需要的分类并滚动到原始符号。

### 最近使用、常用和自定义

#### 最近使用

顶部最近使用区域最多显示 17 个符号。17个槽位会按当前可用宽度平均分配，使整行铺满；垃圾桶按钮可以清空记录。

#### 常用符号

常用符号位于左侧第一项：

- 每行固定显示 12 个；
- 常用符号顺序固定，不会因使用次数变化而重新排序；
- 在“常用符号”页面按住符号拖动，可以调整顺序；
- 右键任意符号可以加入常用或从常用删除；
- 非常用符号累计使用达到 5 次后，会自动追加到常用符号末尾；
- 手工加入、自动加入、拖动后的顺序和删除结果都会持久化保存。
- 初始内容来自 `catalog.txt` 中可编辑的 `# 常用符号`；重新生成后会合并新默认项，同时保留用户追加、排序和删除结果。

#### 自定义

选择左侧“自定义”，在右侧输入自定义符号或完整 Unicode 序列并添加。

用户数据位于：

```text
HKEY_CURRENT_USER\Software\YeTools\YeSymbol
```

包括：

```text
最近使用
常用符号固定顺序、使用次数和用户修改结果
非常用符号的自动加入计数
自定义符号
搜索历史
自动插入设置
```

其中常用符号使用 `CommonV3` 保存固定顺序、来源和默认项删除记录，非当前常用符号的累计次数使用 `UsageV1` 保存。旧版 `CommonV1`/`CommonV2` 会自动迁移，已有用户项目不会丢失。

自动加入阈值可在 `include\ui_config.h` 中修改：

```c
#define YS_COMMON_AUTO_ADD_THRESHOLD 5u
```

清空全部用户数据前先退出程序，然后执行：

```powershell
reg delete "HKCU\Software\YeTools\YeSymbol" /f
```

## 高级功能

### 1. 界面参数调整

界面尺寸集中在：

```text
include\ui_config.h
```

当前主要参数：

```c
#define YS_WINDOW_WIDTH 786
#define YS_WINDOW_HEIGHT 650

#define YS_CATEGORY_WIDTH 162
#define YS_CATEGORY_ITEM_HEIGHT 28

#define YS_MAX_COLUMNS 12
#define YS_RECENT_MAX_VISIBLE 17
#define YS_COMMON_AUTO_ADD_THRESHOLD 5u
```

修改后重新执行：

```powershell
.\build.bat run
```

窗口使用固定尺寸，不允许拖拽缩放。

### 2. 数据维护（可以增加、删除、重排符号）

人工主数据文件：

```text
data-source\catalog.txt
```

数据生成链路：

```text
catalog.txt
→ catalog.generated.json
→ src\symbol_data.c
→ yesymbol.exe
```

`catalog.generated.json` 是构建中间文件，不建议直接长期维护。运行时不会打开或解析JSON。

#### 2.1 刷新官方 CLDR 中文短名称并把适合替换的名称写回 catalog.txt

执行：

```powershell
.\build.bat cldr
```

该流程会：

```text
下载固定提交的 Unicode CLDR zh.xml
→ 校验固定 Git Blob SHA
→ 提取 type="tts" 中文短名称
→ 更新本地缓存
→ 将适合替换的中文名称写回 catalog.txt
→ 重新生成 JSON 和 C 数据
```

当前固定数据源：

```text
仓库：unicode-org/cldr
提交：c9a5503bf238114a1993377b87841fb76031371d
文件：common/annotations/zh.xml
```

CLDR缓存位于：

```text
data-source\cldr-annotations-zh.tts.json
```

普通 `build.bat run` 可以离线使用已经缓存的数据。

#### 2.2 人工修改 `data-source\catalog.txt`

##### 修改 TXT

分类：

```text
# 特殊符号
```

分组：

```text
## 箭头
```

`#` 和 `##` 后面的文字都是显示名称，可以自由修改。转换器只根据行首标记识别层级，不会根据“大篆”“扑克牌”等标题文字猜测分类含义。完全同名的一级或二级标题会按首次出现位置自动合并，但为了便于人工维护，仍建议主动整理重复标题。

侧边栏位置使用独立结构指令控制：

```text
@ui-section main     普通侧边栏分类
@ui-section other    折叠到“其他符号”下面
@ui-section hidden   不显示在侧边栏，但参与搜索和全部符号
```

该指令会影响它后面的一级分类，直到出现新的 `@ui-section`。修改分类名称、分组名称和 `@desc` 不需要同步修改程序源码。

符号行使用逗号分隔：

```text
←,↑,→,↓,↔,↕
```

通常不需要双引号。以下情况需要引号：

```text
" "       半角空格
"　"      全角空格
","       逗号本身
"a,b"     内容内部包含逗号
```

无需手工填写：

```text
@cols
@row
```

转换工具会根据物理行自动计算行号、列数和顺序。详细语法参见：

```text
docs\CATALOG_TEXT_FORMAT.md
```

##### 如何生成 JSON

只生成和检查数据：

```powershell
.\build.bat data
```

或者直接执行：

```powershell
python tools\catalog_text.py check
python tools\catalog_text.py build
python tools\audit_catalog.py
python tools\generate_bilingual_data.py
python tools\build_emoji_images.py all
```

生成文件：

```text
data-source\catalog.generated.json
src\symbol_data.c
src\emoji_image_data.c
```

##### 如何再次编译生成并运行

完成 TXT 修改后先生成数据，再编译运行：

```powershell
.\build.bat data
.\build.bat run
```

数据格式、重复项、分类完整性、图片下载或 C 数据生成失败时，`build.bat data` 会停止。

## 技术说明

### 原生架构

```text
语言：C11 主体；DirectWrite 渲染桥接单元使用 C++ 编译器模式
界面：Win32 API
普通符号：GDI
彩色 Emoji：内嵌 Twemoji PNG + WIC + GDI AlphaBlend
构建：CMake + MSVC
数据生成：Python，仅构建期使用
用户数据：HKCU 注册表
发布形式：单 EXE
```

### 彩色 Emoji 绘制

Emoji 网格和顶部最近使用区域使用同一套底层绘制流程：

```text
GDI绘制背景、边框和普通符号
→ 按需从 EXE 内嵌数据解码 36×36 Emoji 图片
→ 保存到最多 384 项的 LRU 位图缓存
→ AlphaBlend 绘制当前可见图片
→ 缺图时使用 DirectWrite/Direct2D 字体回退
→ BitBlt显示到窗口
```

图片只参与显示。单击后查找对应记录，剪贴板和自动插入始终使用原始 Unicode 文本。缺少嵌入图片或图片解码失败时，程序使用系统 `Segoe UI Emoji` 与 DirectWrite/Direct2D 回退；所有渲染失败都不影响搜索和复制。

### 数据为什么经过 JSON

TXT适合人工维护，JSON适合构建工具校验和转换，C数组适合快速运行：

```text
TXT：可读、可手改、可重排
JSON：结构化中间数据、方便审计
C：编译进EXE、启动时无需解析
```

因此，`TXT → JSON → C` 没有给运行时增加JSON加载开销。

### 性能设计

- 大分类使用虚拟布局；
- 只绘制当前可见行；
- 搜索输入使用短延迟合并；
- 符号文本使用构建期哈希索引；
- 网格复用GDI双缓冲；
- Emoji按当前可见区域批量交给DirectWrite绘制；
- 最近、常用和自定义记录延迟合并写入注册表。

### 开发维护检查

修改代码或数据后建议执行：

```powershell
python tests\static_check.py
.\build.bat data
.\build.bat run
```

发布前至少检查：

```text
TXT、JSON、C符号数量一致
同组重复为0
同分类重复为0
搜索结果可跳转
常用符号每行12个
Emoji彩色绘制
自动插入
托盘与关于窗口
README本地链接
```


## 网页版

项目同时提供纯静态 HTML 版本。网页版和 Windows 桌面版共用同一个构建期数据文件：

```text
data-source\catalog.txt
        ↓
data-source\catalog.generated.json
        ├─→ src\symbol_data.c      Windows 单 EXE
        └─→ dist-web\data\catalog.generated.json  网页发布目录
```

修改 `catalog.txt` 后构建网页版：

```powershell
.\build.bat web
```

本地预览：

```powershell
.\build.bat web-serve
```

将 `dist-web` 中的全部内容上传到 GitHub Pages、Cloudflare Pages、Netlify 或普通静态服务器即可发布。网页版的最近使用、常用、自定义和搜索历史保存在浏览器 `localStorage`，不会修改共享目录数据。详细说明见 [web/README.md](web/README.md)。

## 已知限制

- 未包含在 Twemoji v17.0.3 中的字符会回退到 Windows 系统字体，显示效果取决于系统版本和字体覆盖；
- “自动插入”使用 Windows Unicode 输入事件；管理员权限隔离、远程桌面、沙箱或特殊编辑器仍可能阻止注入；
- 大篆和小篆属于字形风格，Unicode没有分别编码一整套独立字符；显示真实篆体需要用户系统已安装相应字体，本项目不分发字体；
- 古文字是否显示取决于用户系统字体覆盖范围；缺少字体时可能出现方框；
- 原生桌面程序主要面向 Windows 10/11；其他桌面和移动平台可以使用静态网页版，但触控布局仍以符号浏览和复制为主。

## 许可证

项目源码使用 [MIT License](LICENSE)。

Unicode CLDR 数据遵循 Unicode License v3，相关说明见：

```text
THIRD_PARTY_NOTICES.md
```


### DirectWrite 编译说明

YeSymbol 的主程序、界面、数据、存储和剪贴板模块仍使用 C11。Windows SDK 10.0.26100 的 `dwrite.h` 包含 C++ 专用语法，因此 `src\emoji_renderer.c` 作为隔离的 DirectWrite 桥接单元由 C++ 编译器模式编译，并通过 `extern "C"` 向其余 C 模块暴露稳定的 C ABI。该桥接不使用 STL、异常或 RTTI，不改变单 EXE 发布方式。
