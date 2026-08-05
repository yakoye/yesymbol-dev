# 数据与技术参考

## Unicode Emoji

- [Unicode Emoji List](https://unicode.org/emoji/charts/emoji-list.html)：Emoji 字符与序列总表。
- [Unicode Emoji Sequences](https://unicode.org/emoji/charts/emoji-sequences.html)：旗帜、键帽等标准序列。
- [CLDR CJK annotations](https://unicode.org/cldr/charts/49/annotations/cjk.html)：多语言名称对照。

## 中文名称

- [Unicode CLDR 固定提交的中文 annotations](https://github.com/unicode-org/cldr/blob/c9a5503bf238114a1993377b87841fb76031371d/common/annotations/zh.xml)
- [Android 镜像中的 CLDR annotations](https://android.googlesource.com/platform/external/cldr/%2B/refs/heads/cldr-release-32-0-1/common/annotations/zh.xml)

构建工具固定使用仓库中记录的 CLDR 提交，避免上游变化导致同一源码生成不同数据。

## Emoji 图片

- [jdecked/twemoji v17.0.3](https://github.com/jdecked/twemoji/tree/v17.0.3/assets/72x72)：构建期下载并嵌入的 Emoji PNG 来源。
- [Twemoji CC-BY 4.0 许可](https://creativecommons.org/licenses/by/4.0/)

完整第三方声明见仓库根目录的 `THIRD_PARTY_NOTICES.md`。

## 相关研究

- [Unicode-aware text processing reference](https://arxiv.org/abs/1911.06154)
