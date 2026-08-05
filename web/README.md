# YeSymbol 网页版

这是 YeSymbol 的纯静态网页版本，不依赖前端框架或服务器端程序。页面使用与 Windows 桌面版相同的符号主数据：

```text
data-source/catalog.txt
        ↓ regenerate-data.cmd
data-source/catalog.generated.json
        ↓ tools/build_web.py
dist-web/data/catalog.generated.json
```

不要直接编辑 `dist-web/data/catalog.generated.json`。人工数据源仍然是根目录下的 `data-source/catalog.txt`。

## 构建网页版

在项目根目录运行：

```powershell
.\build.bat web
```

该命令会先重新生成 JSON 和 C 数据，再把同一个 JSON 原样复制到网页发布目录：

```text
dist-web/
```

本地预览：

```powershell
.\build.bat web-serve
```

浏览器访问：

```text
http://127.0.0.1:8080/
```

网页使用 `fetch()` 加载 JSON，因此不要直接双击 `index.html` 以 `file://` 方式打开。

## 发布

将 `dist-web` 目录中的全部文件上传到任意静态网站服务即可，例如：

- GitHub Pages；
- Cloudflare Pages；
- Netlify；
- Nginx、Apache 或对象存储静态网站。

发布包根目录必须直接包含：

```text
index.html
assets/
icons/
data/
manifest.webmanifest
sw.js
```

## 用户数据

网页版的最近使用、常用符号、自定义符号、搜索历史和界面设置保存在当前浏览器的 `localStorage` 中，不会写回项目数据文件，也不会上传到服务器。

浏览器清理站点数据或更换浏览器后，这些用户数据会消失。符号目录本身始终来自 `catalog.generated.json`。
