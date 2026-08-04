# YeSymbol（符号大全）

<p align="center">
  <img src="assets/yesymbol-512.png" width="104" height="104" alt="YeSymbol icon">
</p>

> 面向 Windows 的原生 Unicode 符号选择器，用于快速搜索、浏览、收藏、复制和插入符号与 Emoji。

![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011-0078D4)
![Language](https://img.shields.io/badge/language-C11-00599C)
![UI](https://img.shields.io/badge/UI-Win32%20API-5C2D91)
![License](https://img.shields.io/badge/license-MIT-green)
![Version](https://img.shields.io/badge/version-1.0.0--rc16-orange)

## 项目介绍

YeSymbol 使用纯 C 和 Win32 API 开发，不依赖 .NET、Qt、Electron 或 Python 运行时。发布产物是一个便携的 `yesymbol.exe`，无需安装，也不需要随程序携带 JSON、字体或其他数据文件。

当前内置 **16,679 个去重符号或 Unicode 序列**，包括常用符号、标点、数字与字母序号、数学符号、单位、音标、东亚字符、历史文字、Emoji、国家与地区旗帜等。每条内置记录包含：

```text
符号文本
中文名称
英文名称
Unicode 编码
原始分类
原始分类中的行号和位置
```

主要功能：

- 中文、英文、符号本身、分类名和 `U+编码` 搜索；
- 搜索框使用 `↑`、`↓` 回显最近搜索记录；
- 搜索结果悬浮显示原始分类、原始行号和该行中的位置；
- 搜索结果右键可选择“跳到所在位置”；
- 顶部固定显示最近使用的14个符号；
- 常用符号按使用次数排序；
- 支持自定义符号、Emoji肤色变体、旗帜和ZWJ组合序列；
- 可选“自动插入”，复制后恢复此前窗口并发送 `Ctrl+V`；
- 关闭窗口后隐藏到系统托盘；
- 运行时完全离线，个人记录保存在当前用户注册表。

## 开发目的

Windows 自带的 `Win + .` 面板启动和搜索速度不稳定，符号内容也与输入法绑定；不同输入法的符号面板在分类、搜索和专业字符覆盖方面差异很大。YeSymbol 的目标是提供一个：

- 启动快、浏览快、复制快的原生工具；
- 不依赖输入法、账号、网络和大型运行时的独立工具；
- 可由普通文本持续维护符号目录的开放工具；
- 能覆盖专业符号、语言字符、古文字和完整 Unicode 序列的长期目录。

项目坚持“单 EXE、纯 C、数据构建期固化、运行时不解析 JSON”的方向。

## 界面与分类

```text
┌──────────────────────────────────────────────────────────┐
│ 最近使用▼ [搜索符号、名称或Unicode...       ×] ☑自动插入 ☑置顶 │
│ [最近使用符号，最多14个]                            [🗑] │
├────────────────┬─────────────────────────────────────────┤
│ 常用符号       │                                         │
│ 特殊符号       │              当前分类符号               │
│ 标点符号       │                                         │
│ 序号字母       │                                         │
│ ……             │                                         │
│ 其他符号⯈     │                                         │
│ 全部符号       │                                         │
│ 自定义         │                                         │
├────────────────┴─────────────────────────────────────────┤
│                         ✔ 勾号 U+2714 CHECK MARK 特殊符号 │
└──────────────────────────────────────────────────────────┘
```

左侧顺序：

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
其他符号⯈
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

“其他符号”只负责展开或折叠子分类。点击后，选择状态停留在“其他符号”上，不会跳回“常用符号”。“补充符号”仍作为内部数据分类参与搜索和“全部符号”生成，但不单独占用侧边栏入口。

## 安装与使用

### 直接运行

1. 下载 `yesymbol.exe`；
2. 将它放到任意目录；
3. 双击运行。

程序无需安装。需要开机启动时，可自行给 `yesymbol.exe` 创建快捷方式并放入 Windows 启动目录。

关闭主窗口时程序进入系统托盘：

```text
双击托盘图标              恢复窗口
托盘右键 → 打开符号大全   恢复窗口
托盘右键 → 关于 YeSymbol  查看版本信息
托盘右键 → 退出           真正结束程序
```

### 查找和复制

- 单击符号：复制到剪贴板；
- 勾选“自动插入”：复制后尝试回到此前的输入位置并执行粘贴；
- 右键符号：加入/移出常用、删除最近记录、切换肤色或跳到原始位置；
- 悬浮符号：查看中英文名称、原始分类、行号、位置和Unicode编码。

搜索示例：

```text
→             搜索符号本身
箭头          搜索中文名称
arrow         搜索英文名称
2192          搜索十六进制编码
U+2192        搜索Unicode编码
金牛座        搜索中文名
Taurus        搜索英文名
```

输入过的非空搜索会记入历史。搜索框获得焦点后：

```text
↑  上一条搜索
↓  下一条搜索或回到当前草稿
Esc 清空搜索
Enter 确认搜索并记入历史
```

用户数据位于：

```text
HKEY_CURRENT_USER\Software\YeTools\YeSymbol
```

包括最近使用、常用符号及次数、自定义符号、搜索历史和自动插入设置。删除全部用户数据前先退出程序，再运行：

```powershell
reg delete "HKCU\Software\YeTools\YeSymbol" /f
```

## 编译与运行

### 环境要求

- Windows 10 或 Windows 11；
- Visual Studio 2022/2026 或 Visual Studio Build Tools；
- “使用 C++ 的桌面开发”工作负载；
- Windows SDK；
- CMake 3.20或更高版本；
- Python 3.10或更高版本，仅用于构建期生成数据。

Python不是 `yesymbol.exe` 的运行依赖。

### 构建命令

在项目根目录打开 PowerShell：

```powershell
.\build.bat
```

生成：

```text
dist\yesymbol.exe
```

统一命令：

```powershell
.\build.bat          # 增量编译Release
.\build.bat build    # 与上面相同
.\build.bat clean    # 删除build和dist
.\build.bat data     # TXT → JSON → C，不编译EXE
.\build.bat cldr     # 刷新固定版本的CLDR中文名称并重新生成数据
.\build.bat all      # 生成数据、清理、完整编译
.\build.bat run      # 结束旧进程、生成数据、清理、编译并运行
```

开发时推荐：

```powershell
.\build.bat run
```

数据解析、目录审计或C数据生成失败时，脚本会立即停止，不会继续启动旧程序。

CMake 中必须保留：

```text
/MANIFEST:NO
```

`src/resource.rc` 已经手工嵌入 manifest；再次让链接器生成 manifest 会引发 `CVT1100` 或 `LNK1123`。

## 修改 TXT 并生成 JSON

### 数据流

人工只维护：

```text
data-source\catalog.txt
```

完整生成关系：

```text
catalog.txt
    ↓ tools\catalog_text.py build
catalog.generated.json
    ↓ tools\generate_bilingual_data.py
src\symbol_data.c
    ↓ MSVC/CMake
yesymbol.exe
```

这里的“JSON让C识别加载”发生在**构建阶段**：Python生成器读取JSON并生成静态C数组。程序运行时不会打开或解析JSON，因此不会影响启动和浏览速度，也不会破坏单EXE发布方式。

不要手工维护以下生成文件：

```text
data-source\catalog.generated.json
src\symbol_data.c
```

### TXT格式

分类、分组和符号行：

```text
# 序号字母
@desc 数字、分数、编号和装饰字母。

## 黑底带圈数字
❶,❷,❸,❹,❺,❻,❼,❽,❾,❿
⓫,⓬,⓭,⓮,⓯,⓰,⓱,⓲,⓳,⓴
```

普通符号直接用逗号分隔，不写 `@cols`、`@row`，也不需要双引号。转换工具自动计算每行列数和行号；一行超过12项会自动换行。

只有内容本身包含逗号、空格或需要表达空白字符时才使用引号，例如：

```text
","," ","　",a,b,👨‍💻
```

名称记录：

```text
#@symbols
@symbol ♉|金牛座|TAURUS|false
@symbol 😀|嘿嘿|GRINNING FACE|emoji
```

四个字段依次为：符号、中文名称、英文名称、是否优先使用Emoji字体。

### 检查与生成

只检查TXT格式和重复项：

```powershell
python tools\catalog_text.py check
```

TXT生成JSON：

```powershell
python tools\catalog_text.py build
```

执行完整数据链路：

```powershell
.\regenerate-data.cmd
```

它依次执行：

```text
检查固定CLDR中文缓存
→ catalog.txt生成catalog.generated.json
→ 审计数量、顺序和完整性
→ catalog.generated.json生成src\symbol_data.c
```

反向将JSON导出为TXT：

```powershell
python tools\catalog_text.py export
```

`export` 会覆盖 `catalog.txt`，只用于旧数据迁移或恢复，不应在已经人工修改TXT后随意执行。

## 开发与维护

### 目录结构

```text
include\                 公共头文件和界面参数
src\                     Win32程序、存储、资源和生成的符号数据
data-source\             人工目录、生成JSON、CLDR缓存和审计清单
tools\                   TXT/JSON/C转换及数据检查工具
tests\                   静态回归检查
docs\                    编译、使用、数据维护和发布文档
assets\                  图标和README图片
```

### 界面参数

集中在：

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
#define YS_RECENT_MAX_VISIBLE 14
```

### 修改代码后的检查

```powershell
python tools\catalog_text.py check
python tools\audit_catalog.py
python tests\static_check.py
.\build.bat run
```

提交前至少验证：

- 软件可以快速启动且只运行一个实例；
- 搜索、上下键历史、悬浮说明和右键定位正常；
- 普通分类与大分类都能滚动、悬浮、复制；
- 自动插入在记事本、浏览器输入框和Office类程序中可用；
- 常用、最近、自定义和搜索历史重启后仍能恢复；
- 修改 `catalog.txt` 后能完整生成JSON和C数据；
- 同组、同分类没有重复，符号数量和完整系列没有回退。

### 版本规则

发现问题后先发布候选版本：

```text
1.0.0-rc16
1.0.0-rc17
……
```

只有候选版本通过实际Windows验收后，才发布对应的 `final`，避免未验证修改直接作为正式版。

更多资料：

- [编译说明](docs/BUILDING.md)
- [使用说明](docs/USAGE.md)
- [目录文本格式](docs/CATALOG_TEXT_FORMAT.md)
- [数据维护](docs/DATA_MAINTENANCE.md)
- [界面定制](docs/CUSTOMIZATION.md)
- [贡献指南](CONTRIBUTING.md)
- [版本说明](RELEASE_NOTES.md)

## 许可证与第三方数据

项目源代码采用 [MIT License](LICENSE)。Unicode CLDR名称数据的来源和许可说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
