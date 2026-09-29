# 双树复小波滤波器与实现约定

本目录的 C++ 变换引擎根据滤波器组数学关系独立实现，不包含 Python `dtcwt` 或原 MATLAB 工具箱的程序代码。Python 包仅用于开发时比较变换系数和重建输出，不是构建或运行依赖。

## 数学及数值来源

- N. Kingsbury, *Complex Wavelets for Shift Invariant Analysis and Filtering of Signals*, Applied and Computational Harmonic Analysis, 10(3), 2001, 234–253，DOI：[10.1006/acha.2000.0343](https://doi.org/10.1006/acha.2000.0343)。采用文中双树、四分之一采样偏移及二维方向组合的设计。
- 首层 `near_sym_a` 使用 5/7 抽头近似对称双正交滤波器；后续 `qshift_a` 使用 10 抽头 Q-shift 滤波器。常数对应公开滤波器数据 [near_sym_a.npz](https://github.com/rjw57/dtcwt/blob/master/dtcwt/data/near_sym_a.npz) 和 [qshift_a.npz](https://github.com/rjw57/dtcwt/blob/master/dtcwt/data/qshift_a.npz)。代码只保存一棵树的低通数值，其余 Q-shift 系数由时间反转及交替符号的正交镜像关系生成。
- 融合采用 OpenFocus 的低频均值、高频模长最大活动度和多数一致性规则。对应论文：J. J. Lewis 等，*Pixel- and region-based image fusion with complex wavelets*, Information Fusion 8(2), 2007, 119–130。OpenFocus 的 MIT 许可声明由项目根目录 `THIRD_PARTY_NOTICES.md` 统一保留。

## 可逆变换

首层不直接丢弃偶数或奇数位置；四种行列相位交错存放，代表二维的四棵实树。后续一维分析使用直接采样求和

`y[2j+t'] = sum_k h_t[k] * x_reflect[4j+10+t-2k]`。

低通 `t'=t`，高通 `t'=1-t`。长度取四的倍数、使用端点重复的对称延拓时，低通与高通共同构成正交矩阵 `A`。合成直接计算转置 `Aᵀ`，即将每个系数按分析时的索引和权重加回原位置；无需引入另一套边界插值算法。

每个实高频平面的四相位 `a,b,c,d` 通过正交组合形成两个复子带：

`((a-d)+i(b+c))/sqrt(2)` 与 `((a+d)+i(b-c))/sqrt(2)`。

对三组实高频分别执行，得到每层六个方向。逆变换先逆向组合，再执行滤波器组的合成。首层双正交合成使用相应的对偶滤波器。

奇数原图仅在底部、右侧重复一个采样；后续不满足四倍数的低频在相应维两端各补一个采样。每层记录补边前尺寸，重建时逐层撤销补边。内部双精度减少重建误差，方法出口转换为项目统一的 `CV_32F`。

## 参考程序许可边界

Python `dtcwt` 的许可对其新增代码给出 BSD 条款，同时指向原 MATLAB 工具箱的研究用途限制；因此它未作为本项目依赖引入，也未复制其程序实现。上述引用用于说明数学设计与滤波器数值出处，不把参考库宣称为无条件 BSD 或 MIT 软件。
