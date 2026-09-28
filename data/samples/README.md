# 示例图片

运行构建出的 `mif_example --demo outputs/demo`，生成一组可复现的合成图片：

- `focus_01.png`：左半边清晰，右半边散焦。
- `focus_02.png`：右半边清晰，左半边散焦。
- `reference.png`：全清晰参考图。
- `fused_guided.png`、`fused_pyramid.png`：两种方法的融合结果。

在桌面程序中只导入两张 `focus_*.png` 作为输入。不要把参考图或融合结果混入
输入栈。这里不包含第三方图像数据；实际采集图片可按场景自行添加。
