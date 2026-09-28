# Third-party notices

## OpenFocus — design and guided-fusion reference

Source: https://github.com/Xinzhe99/OpenFocus

The traditional workflow and guided-filter fusion decomposition informed this
implementation. OpenFocus is not bundled as a runtime dependency. Its notice is
retained below. No neural models or pretrained weights are included.

MIT License

Copyright (c) 2025 OpenFocus Contributors

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

## nanobind and robin-map

nanobind is pinned at v2.9.2, commit
`116e098cfa96effca2a54e32e0ce5b93abe25393` (see the submodule gitlink for the
authoritative full revision). Its BSD license is in `ext/nanobind/LICENSE`.
The nested robin-map dependency retains its MIT license in
`ext/nanobind/ext/robin_map/LICENSE`.

## OpenCV and Qt

These are external development/runtime dependencies supplied by the build
environment. Retain their applicable notices and follow their distribution
terms when packaging the desktop program or Python extension.

