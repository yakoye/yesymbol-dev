# 界面与源码定制

## 1. 集中界面参数

所有常用尺寸位于：

```text
include\ui_config.h
```

修改后必须重新编译。

## 2. 窗口大小

```c
#define YS_WINDOW_WIDTH 786
#define YS_WINDOW_HEIGHT 650
```

这两个值传给 `CreateWindowExW`，表示主窗口外框尺寸。

窗口当前使用固定样式：

```text
WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX
```

因此：

- 不能拖动边框调整大小；
- 没有最大化按钮；
- 修改尺寸需要重新编译。

## 3. 左侧分类宽度

```c
#define YS_CATEGORY_WIDTH 162
```

增大后：

- 分类名称显示空间更宽；
- 右侧符号区域相应变窄。

建议每次只调整 8～20 像素，再观察长分类名称是否完整。

## 4. 符号格大小

```c
#define YS_CELL_WIDTH 46
#define YS_CELL_HEIGHT 44
```

- `YS_CELL_WIDTH` 控制横向密度；
- `YS_CELL_HEIGHT` 控制纵向密度；
- 增大后同屏显示数量减少；
- 缩小后要同步检查字体是否拥挤。

## 5. 字体大小

```c
#define YS_GROUP_FONT_HEIGHT (-15)
#define YS_SYMBOL_FONT_HEIGHT (-24)
#define YS_EMOJI_FONT_HEIGHT (-24)
```

Win32 `CreateFontW` 使用负高度时，绝对值越大，字体越大：

```text
-20    较小
-24    当前默认
-28    较大
```

## 6. 字体名称

字体名称目前位于：

```text
src\ui.c
```

搜索：

```c
L"Segoe UI"
L"Segoe UI Symbol"
L"Segoe UI Emoji"
```

当前用途：

```text
Segoe UI          分组标题和普通 UI
Segoe UI Symbol   普通符号
Segoe UI Emoji    Emoji
```

替换字体前请确认目标字体已经安装，并覆盖所需 Unicode 范围。

## 7. 间距和控件尺寸

```c
#define YS_UI_MARGIN 8
#define YS_PANEL_GAP 8
#define YS_HEADER_ROW_HEIGHT 26
#define YS_TOP_SECTION_GAP 6
#define YS_BOTTOM_BAR_HEIGHT 34
#define YS_AUTO_INSERT_WIDTH 94
#define YS_TOPMOST_WIDTH 58
#define YS_SEARCH_CLEAR_WIDTH 24
#define YS_ADD_BUTTON_WIDTH 64
```

顶部最近使用区域：

```c
#define YS_RECENT_MAX_VISIBLE 17
#define YS_RECENT_ROW_HEIGHT 44
#define YS_RECENT_TOGGLE_WIDTH 92
#define YS_RECENT_CLEAR_WIDTH 34
```

左侧分类行高：

```c
#define YS_CATEGORY_ITEM_HEIGHT 28
```

自定义分类顶部输入区域高度：

```c
#define YS_CUSTOM_PANEL_HEIGHT 62
```

顶部或底部控件被遮挡时，应同时检查窗口宽度、最近使用单元格宽度以及控制项宽度之和。

## 8. 修改窗口标题和版本号

位于：

```text
include\yesymbol.h
```

主要定义：

```c
#define YESYMBOL_PRODUCT_NAME L"符号大全"
#define YESYMBOL_VERSION L"1.1.0"
```

窗口标题在 `src/ui.c` 的 `CreateWindowExW` 调用处设置。

发布新版本时应同时检查：

- `include/yesymbol.h`；
- `CMakeLists.txt` 中的项目版本；
- `README.md`；
- `RELEASE_NOTES.md`。

## 9. 修改注册表位置

位于：

```text
src\storage.c
```

当前键：

```c
L"Software\\YeTools\\YeSymbol"
```

修改该路径会使程序看不到旧版本保存的数据。

## 10. 修改分类和符号

不要直接手改巨大的 `src/symbol_data.c`。

优先修改：

```text
data-source\catalog.txt
```

然后运行：

```powershell
.\regenerate-data.cmd
py -3 tests\static_check.py
.\build.bat all
```

详细说明见 `DATA_MAINTENANCE.md` 和 `CATALOG_TEXT_FORMAT.md`。

## 11. 推荐的参数调整流程

```text
1. 修改 include\ui_config.h
2. 运行 build.bat all
3. 打开 dist\yesymbol.exe
4. 依次检查所有长分类名
5. 检查普通符号、Emoji、日文和韩文
6. 检查 100%、125%、150% DPI
7. 记录最终参数
```


## 关于窗口内容

文件：

```text
include\about_config.h
```

可修改：

```c
#define YESYMBOL_AUTHOR_EMAIL L"yuxiang_163com@163.com"
#define YESYMBOL_DEVELOPMENT_DATE L"2026-08-04"
#define YESYMBOL_WEB_URL L""
#define YESYMBOL_WEB_LABEL L"打开网页版"
```

网页版发布后，将 `YESYMBOL_WEB_URL` 填写为完整 HTTPS 地址并重新编译。URL 为空时，关于窗口显示“网页版：开发中，敬请期待”。

程序图标文件：

```text
assets\yesymbol.ico
```

图标同时用于 EXE、窗口标题栏、任务栏和系统托盘。替换图标后执行 `build.bat run` 完整重编译。


## 常用符号自动加入阈值

```c
#define YS_COMMON_AUTO_ADD_THRESHOLD 5u
```

达到该使用次数的非常用符号会追加到常用符号末尾。常用符号顺序不会按次数自动重排。
