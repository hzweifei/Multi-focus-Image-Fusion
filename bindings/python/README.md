# Python 绑定

通过 `-DMIF_BUILD_PYTHON=ON` 开启绑定。完整构建后，包会整理到
`outputs/Release/python/mif`；把 `outputs/Release/python` 加入 `PYTHONPATH`
即可导入。运行时需使用与扩展构建版本和架构匹配的 Python。

`src/bind_fusion.cpp` 集中定义模块入口、枚举、参数和融合函数的注册；
`src/array_utils.*` 单独处理 NumPy 与 `cv::Mat` 的数据转换和内存所有权；
`python/mif/__init__.py` 提供用户调用的包装接口，并整理非连续输入数组。

打包入口 `pyproject.toml` 位于仓库根目录，便于源码分发包一起包含算法与第三方
依赖。初始化子模块并准备 OpenCV 开发包后，可在根目录执行
`python -m pip install .`；依赖路径设置见 `docs/build.md`。

构建 `mif_wheel` 目标会把 Release 安装包写入 `outputs/Release/python/wheels`。
Windows 包和 wheel 包含核心库及 OpenCV DLL，导入时自动注册包内 DLL 目录。
运行环境仍需 NumPy 和兼容的 Visual C++ 运行库；Linux/macOS 当前需由环境
提供 OpenCV 共享库。

