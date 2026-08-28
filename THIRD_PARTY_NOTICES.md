# Third-party notices

## AES67_macos_Driver

Architecture and selected AudioServerPlugIn structure were studied and adapted
from `maxajbarlow/AES67_macos_Driver`, audited at commit
`a2bba6220a8c74880e2599bdc224e97ab54f7b0e` (2026-04-09).

That revision is distributed under GNU GPL version 3. The upstream README still
mentions MIT in one section, but the repository `LICENSE`, GitHub license
classification and latest commit all state GPL-3.0. AES Bridge therefore uses
`GPL-3.0-only` for the complete derivative.

Upstream: https://github.com/maxajbarlow/AES67_macos_Driver

## libASPL

libASPL by Victor Gaydov and contributors is fetched from
`gavv/libASPL` at commit `633e0f70203edd87d320fc5a3cae901e1363aac5`.
It is distributed under the MIT License. Its copyright and permission notice
must remain with source and binary distributions.

Upstream: https://github.com/gavv/libASPL

Copyright (c) Victor Gaydov and contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Tauri

AES Bridge Control uses Tauri 2 and its Rust ecosystem. Tauri is dual-licensed
under MIT or Apache-2.0; this derivative uses it under the MIT terms below.

Upstream: https://github.com/tauri-apps/tauri

MIT License

Copyright (c) 2017 - Present Tauri Apps Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Frontend build tools

Vite is MIT-licensed, copyright 2019-present VoidZero Inc. and Vite
contributors. TypeScript is Apache-2.0-licensed. They are development tools and
are not embedded as runtime frameworks in the shipped WebView assets. Exact
versions and the complete dependency graphs are recorded in
`ControlApp/package-lock.json` and `ControlApp/src-tauri/Cargo.lock`.
