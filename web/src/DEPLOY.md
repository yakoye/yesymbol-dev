# YeSymbol 网页版发布说明

## 直接发布

本目录已经是纯静态网站。将目录内全部文件上传到静态托管服务的发布根目录即可。

必须保持以下相对路径：

```text
index.html
assets/app.js
assets/styles.css
data/catalog.generated.json
data/build-info.json
icons/
manifest.webmanifest
sw.js
```

## Cloudflare Pages

- 构建命令：留空（上传现成发布包时），或在源码仓库中设置为 `python tools/build_web.py`；
- 输出目录：`dist-web`；
- 不需要 Node.js；
- `_headers` 会为 HTML、数据和静态资源设置基本缓存策略。

## GitHub Pages

可以把本目录内容推送到 `gh-pages` 分支根目录，或者使用 Actions 将项目的 `dist-web` 目录发布。

## 数据更新

不要直接修改发布目录中的 JSON。回到完整源码项目修改：

```text
data-source/catalog.txt
```

然后执行：

```powershell
.\build.bat web
```

重新上传生成的 `dist-web` 内容。
