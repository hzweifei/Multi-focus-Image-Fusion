# Third-party notices

## OpenFocus — traditional fusion and workflow reference

Source: https://github.com/Xinzhe99/OpenFocus

The traditional workflow, guided-filter layer decomposition, block-variance
selection, DTCWT coefficient fusion rules, and GFG-FGF stages informed this
implementation. The fusion reference revision is
`bf3a3a15c1c508fbba117f6e98a64e49a087434e`. OpenFocus is not bundled as a runtime
dependency. Its notice is retained below. No neural models or pretrained weights
are included.

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

## Dual-tree complex wavelet filter data

The C++ transform is independently implemented from filter-bank mathematics.
It uses the published near_sym_a (5/7-tap) and qshift_a (10-tap) numerical filter
coefficients associated with Nick Kingsbury's dual-tree transform. Mathematical
references, data-source links, phase conventions and reconstruction equations
are documented in `algorithms/src/fusion/dtcwt/FILTERS.md`.

The Python dtcwt package was used only as a development-time numerical reference.
Its program code and the original research-restricted MATLAB toolbox are not
included, translated, or required by this project.

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

