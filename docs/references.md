# 参考来源

- [OpenFocus](https://github.com/Xinzhe99/OpenFocus)：参考其传统融合和配准工作流、
  图像栈导入、原图/结果对照及后台处理交互。
- [OpenFocus GFF](https://github.com/Xinzhe99/OpenFocus/blob/main/fusion_methods/gff.py)：
  参考基础层/细节层分解和分别细化权重的组织方式。本项目使用独立 C++ 模块实现。
- S. Li, X. Kang, J. Hu, *Image fusion with guided filtering*, IEEE Transactions on
  Image Processing, 22(7), 2864–2875, 2013（OpenFocus 引用的算法文献）。
- [OpenCV ECC 文档](https://docs.opencv.org/4.x/dc/d6b/group__video__track.html)：
  配准和逆映射约定。
- [nanobind ndarray 文档](https://nanobind.readthedocs.io/en/latest/ndarray.html)：
  数组转换和 capsule 生命周期。
- [Qt CMake 文档](https://doc.qt.io/qt-6/cmake-get-started.html)：Qt Widgets 构建方式。

参考时间：2026-09-28。OpenFocus 是参考来源，不作为本项目的运行依赖。
没有复制其模型权重或 AI 实现，也不声称复现其所有传统算法。
OpenFocus 的 MIT 许可原文保留于根目录 `THIRD_PARTY_NOTICES.md`；
nanobind 及其 robin-map 子模块保留各自仓库中的许可文件。

