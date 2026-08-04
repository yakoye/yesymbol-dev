# History


## 1.0.3 修复记录

编译问题(已修复并验证通过全新 clean build,零警告零错误)

根因:你机器上的 Windows SDK (10.0.26100.0) 里,dwrite.h 和 d2d1.h 在这个版本上其实都没有可用的 C 语言接口路径——dwrite.h 从来就没有(全文件搜不到一处 __cplusplus/CINTERFACE 判断,interface X : public IUnknown 是无条件的 C++ 写法);d2d1.h 虽然有 D2D_USE_C_DEFINITIONS 开关,但在这个 SDK 里那条分支只做了空的前向声明,没有任何方法、没有 Vtbl 结构体。所以 emoji_renderer.c 里原来那套 IDWriteFactory_CreateTextFormat(...)、ID2D1Factory_CreateDCRenderTarget(...) 这类 COBJMACROS 宏调用,在这份 SDK 下无论 C 还是 C++ 模式都不存在。

修复方式:让 emoji_renderer.c 这一个文件按 C++ 编译(其余所有 .c 文件保持纯 C11 不变),内部改成真正的 C++ COM 方法调用语法(obj->Method(...)),顺带修了一处潜在的 IID_IDWriteFactory 未声明符号问题(改用 __uuidof(IDWriteFactory))。CMakeLists.txt 相应加了 CXX 语言和针对该文件的 LANGUAGE CXX 覆盖。emoji_renderer.h 本来就带了 extern "C",所以 ui.c 等其余代码完全不受影响。

性能问题(已定位并修复,但需要你实机验证)

翻了 ui.c 的点击处理链路——命中测试是二分查找/坐标计算(O(log n)),重绘只画可见行,常用符号/最近使用/自定义的注册表写入本来就是 350ms 防抖延迟合并写入,这些都没问题。

真正的坑在 clipboard.c 的 ys_clipboard_set:每次点击复制符号时,如果 OpenClipboard 第一次没抢到(Windows 剪贴板历史/云剪贴板同步等系统功能经常会在你刚写入剪贴板后瞬间打开它去读取),原代码会 Sleep(5) 重试最多 8 次——而这个函数是在鼠标点击的 UI 线程里同步跑的,Sleep() 会顶到系统调度粒度(常见约 15ms),最坏情况一次点击能卡住 UI 线程 100ms 以上,这正好就是"连续点击不跟手、有漏掉"的表现。已经改成优先用 SwitchToThread()(近乎零成本的让出时间片)重试,只有持续抢不到才退化到 Sleep(1)。

这个改动我没法用手头的工具做真实鼠标连点测试(没有原生 Win32 GUI 自动化工具),建议你 .\build.bat run 之后照你原来的方式连续点击①②③...⑳验证一下手感。如果还是不够跟手,把具体现象告诉我(比如是不是开着"自动插入",这个功能本身要切换前台窗口,开销和剪贴板问题是两回事)。
