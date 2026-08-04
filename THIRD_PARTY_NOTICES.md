# Third-party notices

## Unicode character data

Unicode character names, properties and Emoji conventions are derived from
Unicode data concepts. Unicode data files are distributed under the Unicode
License. See the Unicode Terms of Use and data-file license at unicode.org.

Copyright © 1991–2026 Unicode, Inc. All rights reserved.

## emoji-regex generated RGI expression

`data-source/emoji-regex-RGI_Emoji.js` comes from the `emoji-regex` npm package
(version 9.2.2), licensed under the MIT License. It is included only as a data
provenance/reference file; the Windows application does not load JavaScript.

Copyright Mathias Bynens and emoji-regex contributors.


## Unicode CLDR Chinese annotations

Build-time Chinese short names are sourced from:

```text
unicode-org/cldr
commit c9a5503bf238114a1993377b87841fb76031371d
common/annotations/zh.xml
Git blob SHA 765c963b2ba18ce5844bc737b1d4570b0b45648e
```

YeSymbol reads `annotation type="tts"` values and normalizes lookup keys by removing U+FE0F, as documented by the source file. The cached data is used only while generating static C data; the Windows application does not load CLDR files or access the network at runtime.

CLDR data is provided under the Unicode License v3 (`Unicode-3.0`).

Copyright © 1991–2026 Unicode, Inc. All rights reserved.
