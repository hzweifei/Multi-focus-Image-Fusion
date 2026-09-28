# 项目结构

```text
algorithms/       C++ 算法库，include/mif 为公开接口，src 为内部实现
apps/desktop/     Qt Widgets 桌面程序、图片读写、预览和后台任务
bindings/python/  nanobind 扩展、Python 包和接口测试
ext/              固定版本的第三方 Git 子模块
cmake/            第三方依赖查找
tests/            C++ 算法与桌面程序回归测试
examples/         C++ 与 Python 调用示例
data/samples/     测试图片说明
docs/             架构、算法、构建和参考来源
```

依赖方向：`mif_desktop → mif_desktop_ui → mif_core → OpenCV`；
`Python mif → _mif → mif_core`。核心库不依赖 Qt、Python 或模型推理框架。

## 核心接口

```cpp
#include <mif/fusion.hpp>

mif::FusionOptions options;
options.method = mif::FusionMethod::GuidedFilter;
options.alignment = mif::Alignment::None;
auto result = mif::fuse(images, options);
```

输入是至少两张同尺寸、同类型的 `cv::Mat`，灰度或 BGR 三通道。
支持 `CV_8U`、`CV_16U`、`CV_32F`。浮点输入必须位于 `[0, 1]` 且不含
NaN/Inf。不自动缩放不同尺寸的输入，也不自动丢弃透明通道。
调用期间调用者不能并发修改输入；核心函数自身不修改输入。

`FusionResult` 包含：

- `image`：融合结果，保留输入位深和通道数。
- `focus_indices`：每个像素的主导细节权重来源，零起始 `int32` 索引。
  这是融合诊断图，不能当作经过标定的物理深度图。
- `crop`：输出在第一张原图坐标系中的矩形范围。
- `transforms`：参考图到各输入图的 2×3 仿射矩阵，应用时使用逆映射。
- `weights`：开启 `keep_weight_maps` 时返回的归一化细节权重。

进度回调在调用线程执行，返回 `false` 会抛出 `mif::Cancelled`。
取消检查发生在图像和处理阶段之间；单次 OpenCV 算子及 ECC 求解不能中途打断。
错误通过异常返回，不静默跳过失败图片或配准失败。

## 桌面程序

Qt Designer 的 `main_window.ui` 定义主窗口外框；控件、信号和布局由
`main_window.cpp` 组织。图片可通过文件对话框、文件夹或拖放导入，按自然顺序
排列并去重，列表第一张作为配准参考。目录导入不递归。

`FusionWorker` 在后台读取整组图片和执行算法，通过排队信号更新主线程中的
进度和结果。关闭窗口时先请求取消，线程结束后关闭。选中单张图片的预览解码
目前在界面线程进行，特别大的图片可能造成短暂停顿。

`ImageView` 支持滚轮缩放、拖动和双击适应窗口。预览转换成 8 位显示，导出使用
原始融合结果；16 位 PNG/TIFF 与浮点 TIFF 的保存不会使用预览数据。
`QFile`/`QSaveFile` 配合 OpenCV 内存编解码，支持 Windows 中文路径和原子保存。

## Python

绑定前复制输入，融合计算期间释放 GIL。返回 NumPy 数组通过 capsule 持有
`cv::Mat`，其生命周期与返回数组一致。包装层支持只读和非连续数组，校验 dtype，
颜色顺序为 BGR。Python 包构建入口放在仓库根目录，方便同时打包算法与子模块。

## 第三方依赖

`ext/nanobind` 固定版本，只有开启 Python 绑定时才加入构建。Qt 和 OpenCV 由
已有开发环境提供。`mif_core` 构建成静态库，可从父项目通过 `add_subdirectory`
使用 `mif::core` 目标；当前未提供安装后的 CMake package 配置。

