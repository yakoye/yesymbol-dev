# YeSymbol（符号大全）

<p align="center">
  <img src="assets/yesymbol-512.png" width="104" height="104" alt="YeSymbol icon">
</p>

> 面向 Windows 的原生符号面板：查找、收藏、复制和快速输入 Unicode 符号与 Emoji。

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4)
![Language](https://img.shields.io/badge/language-C11-00599C)
![UI](https://img.shields.io/badge/UI-Win32%20API-5C2D91)
![License](https://img.shields.io/badge/license-MIT-green)
![Version](https://img.shields.io/badge/version-1.0.0--rc15-orange)

YeSymbol 使用纯 C 和 Win32 API 编写，不依赖 .NET、Qt、Electron，也不需要安装。构建完成后，只需运行一个 `yesymbol.exe`。

当前符号目录包含 **16,679 个去重符号或 Unicode 序列**，包括常用符号、箭头、数学符号、单位、音标、日文、韩文、东亚字符、古文字以及分类 Emoji。符号名称同时提供中文和英文说明。

## 功能特点

- **快速启动**：原生 Win32 程序，符号数据编译进 EXE，运行时不解析 JSON；托盘和悬浮提示延后初始化。
- **快速符号索引**：构建阶段生成 UTF-16 哈希索引，最近使用、常用和自定义名称查找不再逐项扫描全部符号。
- **大分类虚拟绘制**：大于等于480项的分类只计算和绘制当前可见行；“全部符号”、象形文字和古埃及文字不再一次建立全部格子。
- **搜索节流**：连续输入时延迟90毫秒合并搜索，避免每个按键都完整扫描并重排界面。
- **非阻塞记录保存**：最近使用、常用频次和自定义变更延迟合并写入注册表，不阻塞自动插入。
- **中英文搜索**：可搜索符号本身、中文名称、英文名称、分类以及 Unicode 编码。
- **悬浮说明**：显示中文名称、英文名称、所属分类和 Unicode 编码。
- **顶部最近使用**：固定显示最近复制的14个符号；完整记录最多保留64个，可折叠或一键清空。
- **常用符号**：可以右键收藏；按实际使用次数自动排序。
- **自定义符号**：进入“自定义”分类后在主界面添加，并可从右键菜单删除。
- **完整 Unicode 序列**：支持代理对、Emoji、肤色修饰、旗帜和 ZWJ 组合序列。
- **Emoji 分类优化**：Emoji 分类已按主题拆分为更小的分组，浏览更容易。
- **肤色变体入口**：Emoji 分类默认优先显示基础/黄色皮肤版本；右键可切换其他肤色并直接复制。
- **12 列上限**：所有分组每一行最多显示 12 个符号，避免过宽难找。
- **自动插入**：可在复制后切回之前的窗口并发送粘贴命令；勾选状态会被记住。
- **系统托盘**：关闭窗口后隐藏到托盘；从托盘菜单真正退出。
- **关于窗口**：标题栏图标菜单和托盘菜单均可打开，显示版本、开发日期、开发目的、联系邮箱和网页版入口。
- **单实例运行**：重复启动时只恢复已有窗口。
- **运行时离线和本地存储**：发布后的 EXE 不联网；用户数据保存在当前用户注册表。
- **文本目录源**：`catalog.txt` 使用接近 Markdown 的语法，便于直接编辑分类、分组、符号和中英文名称。
- **自动去重**：同组和同分类重复项自动整理；“全部符号”自动生成唯一并集，不会重复维护。
- **完整编号和字符系列**：补齐带圈数字 21–50、七套 A–Z、麻将牌、多米诺骨牌、扑克牌和国际象棋扩展符号，并按 Unicode 顺序排列。
- **古文字分类**：新增“象形文字”和“古埃及文字”，古埃及圣书体及格式控制共 1,110 项。
- **完整旗帜数据**：收录 259 个国家和地区旗帜序列，以及英格兰、苏格兰、威尔士3个地区旗帜序列。
- **CLDR 中文短名称**：构建阶段可使用固定提交版本的 Unicode CLDR `type="tts"` 中文名称修复机械混译。
- **界面可定制**：窗口、分类栏、符号格和字体尺寸集中在一个头文件中。

## 界面结构

```text
┌──────────────────────────────────────────────────────────┐
│ 最近使用 ▼  [搜索符号、名称或 Unicode...       ×]  ☑ 自动插入 ☑ 置顶 │
│ [符号][符号][符号]……[最多一行14个]                  [🗑] │
├──────────────┬───────────────────────────────────────────┤
│ 常用符号     │                                           │
│ 特殊符号     │              当前分类符号                 │
│ 标点符号     │                                           │
│ 序号字母     │                                           │
│ ...          │                                           │
│ 全部符号     │                                           │
│ 自定义       │                                           │
├──────────────┴───────────────────────────────────────────┤
│                       ✔  勾号  U+2714  CHECK MARK  特殊符号 │
└──────────────────────────────────────────────────────────┘
```

## 快速开始

### 直接运行

从 GitHub Releases 下载 `yesymbol.exe`，双击运行即可。程序无需安装。

关闭主窗口时，程序会隐藏到系统托盘：

```text
双击托盘图标                 恢复窗口
托盘右键 → 打开符号大全      恢复窗口
托盘右键 → 退出              真正结束程序
```

### 从源码编译

构建环境：

- Windows 10 或 Windows 11；
- Visual Studio / Build Tools，安装“使用 C++ 的桌面开发”；
- Windows SDK；
- CMake 3.20 或更高版本。

在项目根目录运行：

```powershell
.\build.bat
```

生成文件：

```text
dist\yesymbol.exe
```

需要清理旧缓存并完整重编译：

```powershell
.\build.bat all
```

完整编译说明和故障排查见 [docs/BUILDING.md](docs/BUILDING.md)。

统一构建入口：

```powershell
.\build.bat         # 增量编译 Release
.\build.bat clean   # 只清理 build 和 dist
.\build.bat data    # 重新生成 JSON 和 C 数据
.\build.bat cldr    # 刷新官方 CLDR 中文名称并重新生成数据
.\build.bat all     # 生成数据、清理并完整编译
.\build.bat run     # 结束旧进程、生成数据、清理、编译并启动
```

`build.bat` 会复用已有 CMake 生成器；只有缓存不兼容时才自动清理并重新生成 x64 工程。

### 一键清理、编译并运行

开发调试时可以直接运行：

```powershell
.\build.bat run
```

执行流程：

```text
结束当前 yesymbol.exe
→ 清理 build 与 dist
→ 重新生成并编译 Release
→ 检查 dist\yesymbol.exe
→ 启动最新程序
```

`build.bat run` 会先结束旧进程，再重新生成目录数据、清理并编译。目录解析、数据审计或 C 数据生成任一步失败，流程都会停止，不会启动旧程序。

## 使用方法

### 复制符号

1. 在左侧选择分类；
2. 单击一个符号；
3. 符号会写入 Windows 剪贴板；
4. 在目标程序中按 `Ctrl + V`。

对于支持肤色变体的 Emoji：

- 默认列表优先显示基础/黄色皮肤版本；
- 右键该 Emoji，可在“切换肤色”子菜单中选择其他肤色；
- 选择后会立即复制对应变体。

### 搜索

搜索框支持：

```text
→                    直接搜索符号
箭头                 中文名称
arrow                英文名称
2192                 十六进制编码
U+2192               Unicode 编码
金牛座 / Taurus      中英文名称
```

### 最近使用、常用和自定义

- **最近使用**：每次复制后自动更新；右键可删除单项。
- **常用符号**：右键任意符号选择“添加到常用符号”；使用次数越多，排序越靠前。
- **自定义**：选择左侧“自定义”后，在主界面输入符号并点击“添加”；右键可删除。

详细使用说明见 [docs/USAGE.md](docs/USAGE.md)。

## 符号分类

顶部动态区域：

```text
最近使用（最多显示14个）
```

左侧分类：

```text
常用符号
特殊符号
标点符号
序号字母
数学/单位
希腊/拉丁
拼音/注音
中文字符
英文音标
制表符
Emoji·表情与人物
Emoji·动物与自然
Emoji·食物与活动
Emoji·旅行与物品
Emoji·符号与旗帜
其他字符：
    日文字符
    韩文字符
    东亚字符
    大篆
    小篆
    俄文字符
    古埃及文字
    象形文字
全部符号
自定义
```

## 界面参数调整

常用尺寸集中在：

```text
include\ui_config.h
```

主要参数：

```c
#define YS_WINDOW_WIDTH 770          /* 窗口宽度 */
#define YS_WINDOW_HEIGHT 650         /* 窗口高度 */
#define YS_CATEGORY_WIDTH 142        /* 左侧分类宽度 */
#define YS_CELL_WIDTH 46             /* 符号格宽度 */
#define YS_CELL_HEIGHT 44            /* 符号格高度 */
#define YS_SYMBOL_FONT_HEIGHT (-24)  /* 普通符号字体 */
#define YS_EMOJI_FONT_HEIGHT (-24)   /* Emoji 字体 */
```

修改后执行：

```powershell
.\build.bat all
```

更多参数和字体修改位置见 [docs/CUSTOMIZATION.md](docs/CUSTOMIZATION.md)。


## 关于与联系方式

“关于 YeSymbol”可以从两个入口打开：

```text
点击窗口左上角图标 → 关于 YeSymbol...
托盘图标右键 → 关于 YeSymbol
```

关于窗口显示：

- 当前版本；
- 开发日期；
- 开发目的；
- 联系邮箱：`yuxiang_163com@163.com`；
- 网页版入口。

联系邮箱可以直接点击并唤起默认邮件程序。网页版尚未发布时显示“开发中”；发布后只需要修改：

```text
include\about_config.h
```

中的 `YESYMBOL_WEB_URL`，重新编译后链接即可生效。

## 用户数据和隐私

发布后的 `yesymbol.exe` 不联网，也不上传任何内容。只有源码维护命令 `build.bat cldr` 会在构建阶段访问 Unicode CLDR，用于更新本地中文名称缓存。用户数据保存在：

```text
HKEY_CURRENT_USER\Software\YeTools\YeSymbol
```

其中包括：

- 最近使用；
- 常用符号和使用次数；
- 自定义符号。

因此 `yesymbol.exe` 可以随意移动，但个人记录会继续保留在当前 Windows 用户的注册表中。

清空全部用户数据前，先退出 YeSymbol，然后执行：

```powershell
reg delete "HKCU\Software\YeTools\YeSymbol" /f
```

该命令会永久删除最近使用、常用和自定义记录。

## 数据维护

今后的人工主文件是：

```text
data-source\catalog.txt
```

它使用接近 Markdown 的语法，例如：

```text
# 序号字母
@desc 数字、分数、编号以及装饰字母。

## 黑底带圈数字
❶,❷,❸,❹,❺,❻,❼,❽,❾,❿
⓫,⓬,⓭,⓮,⓯,⓰,⓱,⓲,⓳,⓴
```

重新生成：

```powershell
.\regenerate-data.cmd
```

刷新官方 CLDR 中文短名称并把适合替换的名称写回 `catalog.txt`：

```powershell
.\build.bat cldr
```

执行流程：

```text
CLDR 中文短名称缓存
→ catalog.txt
→ catalog.generated.json
→ 数据完整性审计
→ src\symbol_data.c
```


TXT 中不需要维护 `@cols`、`@row` 或普通符号外层的双引号。每一行的项目数和行号由转换工具自动计算；只有空格、逗号等特殊项目才使用双引号。

文本语法、名称标注和去重规则见 [docs/CATALOG_TEXT_FORMAT.md](docs/CATALOG_TEXT_FORMAT.md)。

## 性能设计

YeSymbol 仍然把全部目录编译进单个 EXE，不在运行时读取或拆分 JSON。rc15 的“懒加载”发生在视图层：

```text
静态目录常驻只读数据
→ 小分类直接布局
→ 大分类只保存分组、行数和滚动范围
→ 根据当前滚动位置绘制可见行
```

自动插入的关键路径调整为：

```text
写入剪贴板
→ 恢复此前目标窗口和焦点控件
→ 立即发送 Ctrl+V
→ 再更新最近使用和使用次数
→ 350ms 内合并写入注册表
```

这样保持单 EXE、离线和快速启动，同时减少大目录浏览与自动插入的等待。

实现与数据规模说明见 [data-source/performance-rc15-report.md](data-source/performance-rc15-report.md)。

## 项目结构

```text
.
├── CMakeLists.txt
├── build.bat
├── regenerate-data.cmd
├── include\                 公共头文件和界面参数
├── src\                     C/Win32 源码及生成后的符号数据
├── data-source\             符号目录、双语名称和生成审查数据
├── tools\                   数据生成脚本
├── tests\                   静态检查
└── docs\                    编译、使用、定制和发布文档
```

## 技术说明

- 语言标准：C11；
- GUI：Win32 API / GDI；
- 构建系统：CMake；
- MSVC Release 使用 `/O2` 和静态 CRT；
- 源码统一按 UTF-8 编译；
- `resource.rc` 手工嵌入 manifest，MSVC 链接阶段使用 `/MANIFEST:NO` 避免重复资源；
- 普通符号使用 `Segoe UI Symbol`，Emoji 使用 `Segoe UI Emoji`；
- 数据由静态 C 数组和字符串池直接提供。

## 已知限制

- 某些罕见字符能否正确显示，取决于当前 Windows 中是否安装了包含该字形的字体。
- 不同应用对复杂 Emoji、组合字符和自动插入的支持可能不同。
- 自动插入跨越不同权限级别的程序时，可能被 Windows 安全机制阻止。
- 当前窗口尺寸为编译期固定值，修改参数后需要重新编译。
- 当前为候选版本，发布前建议在 Windows 10 和 Windows 11 上分别完成实机回归。

## 参与开发

提交代码或符号数据前，请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。发布检查见 [docs/RELEASING.md](docs/RELEASING.md)。

## 许可证

项目源码使用 [MIT License](LICENSE)。第三方数据和术语来源见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
