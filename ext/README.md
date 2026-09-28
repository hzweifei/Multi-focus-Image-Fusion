# 第三方依赖

`nanobind/` 是固定到 v2.9.2 的 Git 子模块，只在 `MIF_BUILD_PYTHON=ON`
时加入构建。使用以下命令初始化，包括它自己的嵌套依赖：

```sh
git submodule update --init --recursive
```

OpenCV 和 Qt 从已有开发包查找。后续新增的第三方源码依赖放在此目录，并固定
到明确的 Git 提交，保留上游许可证。自有代码的中文注释维护不修改这些第三方
仓库，以便后续正常更新子模块。

OpenFocus 的具体借鉴范围见 `docs/references.md`。

