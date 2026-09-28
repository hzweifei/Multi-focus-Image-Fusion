# 验证记录

验证日期：2026-09-28。

## 已验证环境

Windows x64，Visual Studio 2022 / MSVC 19.44，CMake 4.0.2，
OpenCV 4.12.0，Qt 5.15.2，Python 3.12.4，NumPy 2.2.6，nanobind 2.9.2。
Qt 6、Linux 和 macOS 暂未实际编译验证。

## 检查结果

- Release 完整构建通过：核心库、Qt 桌面程序、C++ 示例、nanobind 扩展。
- CTest 10/10 通过：清晰度指标、合成融合效果、原图不变与位深、异常输入、
  权重归一化、进度和取消、ECC 配准、257 张来源索引、桌面流程、Python 绑定。
- Qt 原生 Windows 窗口测试通过，使用程序目录中的运行库，检查中文路径、
  预览、后台融合、导出、16 位 PNG 和 float32 TIFF 无损往返。
- Python wheel 构建成功，安装到独立测试目录后，5 个 unittest 用例全部通过。
  没有将包安装到用户的全局 Python 环境。
- 已生成 `outputs/demo/` 合成图像和 `outputs/desktop-preview.png` 界面截图。

合成测试中，输入相对于全清晰参考图的平均绝对误差约 12.44，融合后约
0.28–0.33（8 位范围）。这是固定合成纹理的回归检查，不代表真实显微图像性能。

## 待真实数据验证

当前没有用户提供的实际焦点图像栈。噪声、倍率变化、视差、曝光差异、微小结构
与反光区域的融合效果仍需进一步评估；当前不对实际检测精度作保证。

## 交付目录验证

构建规则已升级：核心库默认动态链接，按配置整理 `outputs/<配置>/app`、
`python`、`sdk` 与 `examples`，编译中间文件保留在 `build/`。

- 改为 DLL 后，CTest 10/10 通过，包括跨 DLL 的融合回调和取消异常。
- 将 SDK 复制并改名，外部 `tests/sdk_consumer` 项目通过 `find_package(Mif)`
  编译链接成功，使用 SDK 的运行库执行成功。
- 清除开发依赖 PATH 后，`outputs/Release/app` 中的程序启动且响应正常。
- 不设置外部 OpenCV DLL 路径时，从 `outputs/Release/python` 导入包并通过 5 项测试。
- 新 wheel 包含 21 个 Windows 运行 DLL；安装到独立目录后，在未设置外部
  OpenCV 路径的环境中通过全部 5 项 Python 测试。

## 中文注释与结构精简后的复验

自有算法、Qt、Python 绑定、示例、测试及构建脚本已补充中文注释。
本轮合并了 Python 扩展入口，拆分了主窗口初始化方法，并统一运行库安装函数。

- Release 完整构建通过，`outputs/Release/` 中的程序、Python 包和 SDK 已更新。
- CTest 10/10 通过，覆盖主窗口调整后的桌面流程及合并入口后的 Python 导入。
- wheel 重新构建成功，安装到 `build/comments-wheel-smoke/`；清除开发依赖 PATH、
  不设置 `MIF_TEST_DLL_DIRS` 后，5 项 Python 测试全部通过。

注释范围和保留的模块边界见 [项目结构](architecture.md)。
