# Emoji 分类整理审核说明

> 状态：TSV 分类已经同步到 `catalog.txt`，数据生成、编译和回归验证均已通过。
> 工作目录：`C:\Users\color\Downloads\yesymbol\yesymbol-dev`
> 修改日期：2026-08-20

## 一、数据位置

| 内容 | 位置 | 作用 |
|---|---|---|
| 原始人工目录 | `data-source/catalog.txt` | 当前程序的数据主表 |
| 完整 Emoji 数据 | `data-source/catalog.generated.json` | 3826 条 Emoji 记录 |
| 用户确认的分类表 | `data-source/review/emoji-image-classification.tsv` | 本次分类实现的唯一依据 |
| 主网格预览 | `data-source/review/emoji-categories-proposed.md` | 14 类、1914 个主网格 Emoji |
| PNG 缓存 | `data-source/emoji-images/` | 3619 个实体 PNG，呈现别名共用图片 |

## 二、目标界面

```text
左侧栏
└─ Emoji
   ├─ 笑脸
   ├─ 手势
   ├─ 人物
   ├─ 人物活动
   ├─ 家庭
   ├─ 情绪
   ├─ 植物
   ├─ 动物
   ├─ 食物
   ├─ 活动
   ├─ 旅行
   ├─ 物体
   ├─ 符号
   └─ 旗帜
```

左侧只保留一个 `Emoji` 父级入口；14 个细分类作为缩进的二级侧栏项。点击父级展开并打开“笑脸”，再次点击折叠；滚轮可按侧栏顺序连续浏览全部 14 类并衔接到“其他符号”。

## 三、最终分类规则

- “情绪”同时包含爱心和 Unicode `emotion` 子组，避免再增加独立分类。
- “人物活动”包含人物运动、休息和人物活动。
- “家庭”包含 Unicode `family` 子组，并额外包含 `🫂` 和 `👪`。
- 肤色变体继续用于右键选择，不占主网格。
- 带/不带 `U+FE0F` 的呈现别名不在主网格重复显示。

## 四、数量核对

| 分类 | 主网格 | 全部记录 |
|---|---:|---:|
| 笑脸 | 129 | 132 |
| 手势 | 61 | 261 |
| 人物 | 188 | 1035 |
| 人物活动 | 81 | 451 |
| 家庭 | 39 | 339 |
| 情绪 | 40 | 45 |
| 植物 | 29 | 31 |
| 动物 | 129 | 133 |
| 食物 | 129 | 131 |
| 活动 | 85 | 96 |
| 旅行 | 219 | 268 |
| 物体 | 265 | 314 |
| 符号 | 224 | 293 |
| 旗帜 | 296 | 297 |
| **合计** | **1914** | **3826** |

展示方式合计：主网格 1914，肤色变体 1705，呈现别名 207。

## 五、TSV 完整性

- 序号已经按当前行顺序重新编为 1–3826。
- Emoji 精确重复：0。
- 相对 `catalog.generated.json` 缺失或多余：0。
- 图片缺失：0。
- 分类为空或未知：0。

## 六、实现与验证结果

- `catalog.txt` 按 TSV 顺序生成 14 个真实 Emoji 数据分类和 1914 个主网格项，并用 `@ui-section emoji` 统一归入侧栏 `Emoji` 父级。
- 点击 `Emoji` 自动展示第一个“笑脸”分类；滚轮到达边界后自动进入相邻分类。
- `catalog.generated.json` 仍包含 3826 条 Emoji 记录，肤色变体和呈现别名没有丢失。
- `build.bat data` 通过，3826 张 Emoji 图片全部嵌入，图片缺失 0。
- Release 编译通过，输出为 `dist/yesymbol.exe`。
- 静态检查、Emoji 图片检查、常用符号检查和快速连续输入检查均通过。

## 参考

- Unicode Emoji 17.0：https://www.unicode.org/Public/17.0.0/emoji/emoji-test.txt
- Unicode Emoji 技术标准：https://www.unicode.org/reports/tr51/
