# Color emoji artwork

Microsoft Fluent Emoji Flat SVG artwork © Microsoft Corporation, MIT licensed.
Source: https://github.com/microsoft/fluentui-emoji
Pinned source revision: 1ffb34c752ecf5d402f04cfb4b392c77f57c54bc.
All 3,145 Flat variants are rasterized to transparent 64×64 PNGs for WPF.
The complete permission/copyright notice is in Fluent-Emoji-LICENSE.txt.

259 regional flag assets supplement flags absent from Fluent Emoji.
Source: https://github.com/googlefonts/noto-emoji/tree/e20cbc2bbec1926686be9f9bee7d1d2cfa1fea0e/third_party/region-flags
Pinned source revision: e20cbc2bbec1926686be9f9bee7d1d2cfa1fea0e.
The upstream project states these flags are public domain or otherwise exempt
from copyright. Its complete copying/provenance notice is retained in
Regional-Flags-LICENSE.txt. No Noto font software is bundled.

One family sequence absent from Fluent (man, woman, girl, boy) uses Google's
Noto v2.038 color SVG at revision e8073ab740292f8d5f19b5de144087ac58044d06,
rasterized to PNG. Copyright 2013
Google, Inc. All Rights Reserved. Licensed under Apache-2.0; retain both
Noto-Family-LICENSE.txt (upstream attribution) and Apache-2.0-LICENSE.txt (full terms).

The build-only converter scripts/build-emoji-artwork.cjs uses bundled sharp/
libvips SVG rasterization. No converter, SVG engine or downloaded font is shipped.
The archive manifest records source versions, output count and converter versions.
At runtime, trusted embedded PNGs are decoded on demand with a 128-image hot cache.
Unsupported/new sequences retain their exact Unicode text and native-font fallback.
No remote resource lookup or message-content substitution occurs.
