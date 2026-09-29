# 参考来源

- [OpenFocus](https://github.com/Xinzhe99/OpenFocus)：参考其传统融合和配准工作流、
  图像栈导入、原图/结果对照及后台处理交互。
- [OpenFocus GFF](https://github.com/Xinzhe99/OpenFocus/blob/main/fusion_methods/gff.py)：
  参考基础层/细节层分解和分别细化权重的组织方式。本项目使用独立 C++ 模块实现。
- [OpenFocus DCT](https://github.com/Xinzhe99/OpenFocus/blob/bf3a3a15c1c508fbba117f6e98a64e49a087434e/fusion_methods/dct.py)：
  参考空间域块方差选帧与两次中值一致性检查。本项目保留完整奇数边缘、int32 输入索引、
  原始位深和并列等权处理，不复制上游裁剪或 8 位重建的限制。
- [OpenFocus DTCWT](https://github.com/Xinzhe99/OpenFocus/blob/bf3a3a15c1c508fbba117f6e98a64e49a087434e/fusion_methods/dtcwt.py)：
  参考低频均值、六方向局部幅值与多数一致性选系数规则，保留多帧顺序两两合并的约定。
  双树变换引擎使用 C++ 独立实现，数学和滤波器数据出处见
  [DTCWT 滤波器说明](../algorithms/src/fusion/dtcwt/FILTERS.md)。
- [OpenFocus GFG-FGF](https://github.com/Xinzhe99/OpenFocus/blob/bf3a3a15c1c508fbba117f6e98a64e49a087434e/fusion_methods/gfg_fgf.py)：
  参考梯度筛帧、局部差异、两阶段官方引导滤波路径；本项目补齐灰度、高位深、小图、
  无纹理等权和筛选后原始索引映射。本项目的拉普拉斯金字塔融合为额外实现。
- [OpenFocus 配准](https://github.com/Xinzhe99/OpenFocus/blob/455e0f0217e93e5df45fbaeb7a9bb94c9649f22a/core/registration.py)：
  参考 SIFT 匹配 + RANSAC 和 ECC 两种求解路线。本项目分别放在独立 C++ 文件中，
  直接配准到第一张、共享掩码裁剪，并明确拒绝失败结果；尚未实现上游的组合模式。
- S. Li, X. Kang, J. Hu, *Image fusion with guided filtering*, IEEE Transactions on
  Image Processing, 22(7), 2864–2875, 2013（OpenFocus 引用的算法文献）。
- [OpenCV ECC 文档](https://docs.opencv.org/4.x/dc/d6b/group__video__track.html)：
  配准和逆映射约定。
- [OpenCV 4.12 引导滤波](https://docs.opencv.org/4.12.0/da/d17/group__ximgproc__filters.html)：
  `cv::ximgproc::guidedFilter`，完整分辨率的灰度引导滤波；由 vcpkg `opencv4[contrib]` 提供。
- [nanobind ndarray 文档](https://nanobind.readthedocs.io/en/latest/ndarray.html)：
  数组转换和 capsule 生命周期。
- [Qt CMake 文档](https://doc.qt.io/qt-6/cmake-get-started.html)：Qt Widgets 构建方式。

融合方法核对时间：2026-09-29，OpenFocus 提交 `bf3a3a15c1c508fbba117f6e98a64e49a087434e`。
OpenFocus 是参考来源，不作为运行依赖；未复制模型权重或 AI 实现。
当前已实现四条传统融合路线，参数、边界、并列与精度处理的明确差异见算法说明，
不承诺与上游 Python 输出逐像素相同。
OpenFocus 的 MIT 许可原文保留于根目录 `THIRD_PARTY_NOTICES.md`；
nanobind 及其 robin-map 子模块保留各自仓库中的许可文件。

