# 符号数据维护

YeSymbol 运行时不读取 JSON 或 TXT。符号目录最终会编译到：

```text
src\symbol_data.c
```

最终发布的 `yesymbol.exe` 仍是独立单文件程序。

## 数据源关系

```text
data-source\catalog.txt
        │ 人工修改
        ▼
tools\update_cldr_zh.py ensure
        │ 读取固定提交版本的 CLDR 中文短名称缓存
        ▼
tools\catalog_text.py build
        ▼
data-source\catalog.generated.json
        │ 中间审查文件
        ▼
tools\generate_bilingual_data.py
        ▼
src\symbol_data.c
```

### 人工维护主文件

```text
data-source\catalog.txt
```

这是今后的主要编辑入口，可以修改：

- 分类顺序；
- 分组标题；
- 每行符号；
- 默认常用符号；
- 中文名称；
- 英文名称；
- Emoji 标记。

完整语法见 [CATALOG_TEXT_FORMAT.md](CATALOG_TEXT_FORMAT.md)。

### 生成的 JSON

```text
data-source\catalog.generated.json
```

它由 `catalog.txt` 生成，便于程序和脚本处理。一般不要把它当成人工主文件长期修改，否则下一次执行 `regenerate-data.cmd` 会被 `catalog.txt` 覆盖。

### 旧版文本参考

```text
data-source\符号大全-v0.12.txt
```

它保留早期逐行排版，作为历史参考；当前生成流程不再直接读取它。

### CLDR 中文短名称缓存

```text
data-source\cldr-annotations-zh.tts.json
```

缓存来源固定为 Unicode CLDR 提交 `c9a5503bf238114a1993377b87841fb76031371d` 的 `common/annotations/zh.xml`。转换器读取 `type="tts"` 作为简短中文显示名，并在匹配时忽略 `U+FE0F`。

普通构建使用已有缓存：

```powershell
.\build.bat data
```

联网刷新完整官方缓存，并把适合替换的名称写回 `catalog.txt`：

```powershell
.\build.bat cldr
```

缓存刷新失败时，普通构建继续使用已有缓存或 `catalog.txt` 中的名称；发布后的 `yesymbol.exe` 不读取该文件，也不联网。

### 中文名称覆盖参考

```text
data-source\chinese-name-overrides.json
```

该文件保留历史人工名称资料。当前目录名称以 `catalog.txt` 的 `@symbol` 记录和 CLDR 缓存为准。

## 重新生成

需要 Python 3：

```powershell
.\regenerate-data.cmd
```

执行顺序：

```text
检查或刷新本地 CLDR 中文名称缓存
→ catalog.txt 去重并生成 catalog.generated.json
→ 审计编号、牌类、旗帜和古文字完整性
→ 生成 src\symbol_data.c 和符号哈希索引
```

生成器在 `src\symbol_data.c` 内容完全不变时会跳过写入，避免 Windows 编辑器、安全软件或索引器短暂占用文件时出现 `PermissionError`。

随后执行：

```powershell
py -3 tests\static_check.py
.\build.bat run
```

## 去重边界

当前目录按以下规则处理：

- 符号定义按完整 Unicode 序列全局唯一；
- 同一分组内不重复；
- 同一分类内只显示一次；
- 不同分类可以重复，因为分类是不同查找入口；
- “全部符号”由其他分类自动生成唯一并集；
- 不会因为去重删除唯一符号。

当前归一化统计见：

```text
data-source\catalog-dedupe-report.txt
data-source\catalog-rc13-report.txt
```

## 名称原则

悬浮提示显示：

```text
中文名称
英文 Unicode 名称
```

中文名称处理原则：

1. Emoji 及明显中英文机械混拼的名称，优先采用固定版本的 Unicode CLDR `type="tts"` 中文短名称；
2. 已经人工校订且质量正常的普通符号名称保持不变；
3. CLDR 没有覆盖的字符继续使用人工名称或可靠 Unicode 术语；
4. 无法准确判断时保留可追溯英文名称，不继续逐词拼接。

## Unicode 序列

一个界面项目不一定只有一个码点：

```text
😀             单个非 BMP 码点
👍🏻            Emoji + 肤色修饰
❤️             字符 + Variation Selector
👨‍💻           ZWJ 序列
🇨🇳            两个区域指示符
```

因此符号行使用 CSV 风格的英文逗号分隔；只有空格、逗号、引号等歧义项目才使用双引号。

## 数据回归检查

至少检查：

- 总符号数量没有意外下降；
- 所有符号具有中英文名称；
- 代理对和 ZWJ 序列没有被拆分；
- 同一分类中没有重复显示；
- 不同分类的合理复用仍被保留；
- 每组列数不超过 12；
- `全部符号` 覆盖所有静态符号且自身无重复；
- 搜索中文、英文和 U+ 编码均可命中。

## 分类层级

`catalog.txt` 继续保存真实静态分类。侧边栏展示顺序由 `tools/catalog_text.py` 中的 `UI_MAIN_CATEGORIES` 和 `UI_OTHER_CATEGORIES` 固定，防止“全部符号”或大型历史字符分类意外占据常用位置。

数字编号和装饰字母统一维护在：

```text
# 序号字母
## 字母序号
Ⓐ,Ⓑ,Ⓒ,Ⓓ,Ⓔ,Ⓕ,Ⓖ,Ⓗ,Ⓘ,Ⓙ,Ⓚ,Ⓛ,Ⓜ,Ⓝ,Ⓞ,Ⓟ,Ⓠ,Ⓡ,Ⓢ,Ⓣ,Ⓤ,Ⓥ,Ⓦ,Ⓧ,Ⓨ,Ⓩ
```

日文、韩文、东亚字符、俄文和古文字在数据中仍是一级分类，界面将它们折叠显示在“其他字符”下面。`补充符号`保留为内部数据分类，不显示独立侧边栏入口。

`大篆`和`小篆`是汉字字形风格，不是独立Unicode编码集合。目前只保留说明页；真正展示需要后续提供对应字体或字形资源。

超过12项的长行仍会自动换行。`build.bat run` 会先重新生成数据，解析失败时停止，不会运行旧产物。

## 生成时性能索引

`tools/generate_bilingual_data.py` 除了生成字符串池和分类表，还会生成符号文本哈希表：

```text
完整 UTF-16 序列 → FNV-1a 哈希 → 开放寻址表 → symbol_index
```

运行时最近使用、常用、自定义和肤色变体可以快速找到双语名称，不需要每次扫描全部16,679项。该哈希表同样编译进 EXE，不增加外部运行时文件。
