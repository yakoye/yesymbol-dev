#pragma once

/*
 * YeSymbol 界面尺寸集中配置。
 * 修改这里的数值后重新运行 build.bat 即可。
 *
 * 注意：窗口已锁定为固定尺寸，不允许鼠标拖拽调整。
 */

/* 主窗口外框尺寸（像素） */
#define YS_WINDOW_WIDTH 786
#define YS_WINDOW_HEIGHT 650

/* 左侧分类列表宽度 */
#define YS_CATEGORY_WIDTH 162
#define YS_CATEGORY_ITEM_HEIGHT 28

/* 主符号区域每行最大符号数 */
#define YS_MAX_COLUMNS 12

/* 顶部最近使用区域 */
#define YS_RECENT_MAX_VISIBLE 14
#define YS_RECENT_CELL_WIDTH 42
#define YS_RECENT_ROW_HEIGHT 44
#define YS_RECENT_TOGGLE_WIDTH 92
#define YS_RECENT_CLEAR_WIDTH 34

/* 符号格尺寸 */
#define YS_CELL_WIDTH 46
#define YS_CELL_HEIGHT 44

/* 字体高度：负数表示按字符高度创建字体 */
#define YS_GROUP_FONT_HEIGHT (-15)
#define YS_SYMBOL_FONT_HEIGHT (-24)
#define YS_EMOJI_FONT_HEIGHT (-24)

/* 界面间距与区域尺寸 */
#define YS_UI_MARGIN 8
#define YS_PANEL_GAP 8
#define YS_HEADER_ROW_HEIGHT 26
#define YS_TOP_SECTION_GAP 6
#define YS_BOTTOM_BAR_HEIGHT 34
#define YS_SEARCH_CLEAR_WIDTH 24
#define YS_AUTO_INSERT_WIDTH 94
#define YS_TOPMOST_WIDTH 58
#define YS_CUSTOM_PANEL_HEIGHT 62
#define YS_ADD_BUTTON_WIDTH 64
