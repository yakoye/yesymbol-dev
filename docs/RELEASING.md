# GitHub 发布检查

## 1. 版本准备

确认版本号同步：

```text
include\yesymbol.h
CMakeLists.txt
README.md
RELEASE_NOTES.md
```

下一候选版建议使用：

```text
1.0.3-rc1
```

当前正式版：

```text
1.0.3-rc1
```

## 2. 数据生成和检查

修改过数据时执行：

```powershell
.\regenerate-data.cmd
```

所有发布都应执行：

```powershell
py -3 tests\static_check.py
```

## 3. 干净构建

```powershell
.\build.bat all
```

确认文件存在：

```text
dist\yesymbol.exe
```

## 4. 实机回归

至少验证：

- Windows 10 x64；
- Windows 11 x64；
- 100%、125% 和 150% 缩放中的一种或多种；
- 单实例；
- 复制；
- 自动插入；
- 中英文搜索；
- Unicode 编码搜索；
- 最近使用；
- 常用收藏、删除和频率排序；
- 自定义添加和删除；
- 托盘隐藏、恢复和退出；
- Explorer 重启后的托盘恢复；
- 复杂 Emoji 和非 BMP 字符；
- 固定窗口尺寸和所有长分类名称；
- EXE、标题栏、任务栏和托盘图标；
- 标题栏图标系统菜单与托盘菜单中的“关于”；
- 邮箱链接和网页版链接。

## 5. 生成校验值

```powershell
Get-FileHash .\dist\yesymbol.exe -Algorithm SHA256 |
    Format-List
```

建议把结果保存为：

```text
yesymbol-v1.0.3-rc1.sha256
```

## 6. GitHub Release 内容

建议上传：

```text
yesymbol.exe                        用户运行文件
yesymbol-v1.0.3-rc1.zip     源码包
yesymbol-v1.0.3-rc1.sha256         校验值
```

Release 说明至少包含：

- 当前版本主要变化；
- 系统要求；
- 下载和运行方式；
- 数据存储位置；
- 已知限制；
- SHA-256。

## 7. 源码包检查

源码仓库不应提交：

```text
build\
dist\
dist-debug\
.vs\
__pycache__\
```

确认 LICENSE、README、第三方声明和生成数据来源均存在。

## 8. 安全提醒

- 未签名 EXE 可能触发 SmartScreen；
- 不要关闭编译器警告来掩盖真实问题；
- 发布前使用 Windows Defender 扫描；
- 有条件时进行代码签名；
- 不要把用户注册表数据打进发布包。
