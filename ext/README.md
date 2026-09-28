# Third-party dependencies

`nanobind/` is a Git submodule pinned to v2.9.2. It is built only when
`MIF_BUILD_PYTHON=ON`. Initialize it with:

```sh
git submodule update --init --recursive
```

OpenCV and Qt are discovered from installed development packages. New source
dependencies should be added here as submodules pinned to reviewed commits.
Keep their original license files. OpenFocus is a design reference, not a runtime
dependency; see `docs/references.md`.

