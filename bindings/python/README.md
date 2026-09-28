# Python bindings

Build with `-DMIF_BUILD_PYTHON=ON`. For a local CMake build, put
`<build-directory>/python` on `PYTHONPATH`. The Python package is `mif`.

The packaging entry point `pyproject.toml` is at the repository root so source
distributions can contain algorithms and third-party source dependencies together.
Run `python -m pip install .` from the root after initializing submodules and
installing OpenCV development libraries. See `docs/build.md` for dependency paths.

Local wheels depend on the installed OpenCV runtime; they are not yet portable
binary distributions. On Windows, add the OpenCV DLL directory with
`os.add_dll_directory(...)` **before** importing `mif`, keeping the returned handle
alive. Linux/macOS must likewise be able to locate the OpenCV shared libraries.

