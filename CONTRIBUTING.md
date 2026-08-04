# 参与贡献

感谢参与 YeSymbol。

## 提交类型

欢迎：

- 修复 Win32 界面、托盘、剪贴板或存储问题；
- 补充或校正符号中文名称；
- 改善分类和视觉排列；
- 改善高 DPI 和字体回退；
- 增加自动化检查；
- 完善文档。

## 开发环境

请先阅读：

- `docs/BUILDING.md`
- `docs/DATA_MAINTENANCE.md`
- `docs/CATALOG_TEXT_FORMAT.md`

## 提交前检查

```powershell
python tools\catalog_text.py check
.\regenerate-data.cmd
py -3 tests\static_check.py
.\build.bat all
```

然后在 Windows 实机验证：

- 程序启动和单实例；
- 复制及自动插入；
- 搜索；
- 最近使用、常用和自定义；
- 托盘关闭、恢复和退出；
- 悬浮提示；
- 复杂 Unicode 序列。

## 代码约束

- 保持 C11 和 Win32 API；
- 不引入 .NET、Qt、Electron 等运行时依赖；
- 注意栈空间，不要在栈上声明超大结构体或大路径数组；
- 大型持久化结构优先使用进程堆；
- Windows 路径和界面文本使用宽字符 API；
- 源文件保存为 UTF-8；
- 不要删除 `/MANIFEST:NO`，除非同时重构 manifest 资源方案；
- 不要在 `WM_PAINT` 中反复创建可缓存的 GDI 对象；
- 所有 GDI、菜单、句柄和堆内存必须成对释放。

## 符号数据约束

- 按完整 Unicode 序列处理和去重；
- 不要把 Emoji 或代理对拆成单个 `wchar_t`；
- 中文名称必须可追溯，不确定时保留英文名称；
- 以 `data-source/catalog.txt` 为人工主表；
- 不要直接长期修改生成的 `catalog.generated.json`；
- 保留人工主表的分类、分组和逐行视觉结构；
- 数据变化后重新生成并执行静态检查。

## Pull Request 建议

PR 描述中写明：

- 修改目的；
- 修改范围；
- 编译环境；
- 实机测试结果；
- 是否修改符号数量或分类；
- 界面变化截图（如果适用）。
