"""独立的 GFF 多聚焦图像融合，采用本项目 C++ 版本的流程与默认参数。

依赖：numpy、opencv-contrib-python（需要 cv2.ximgproc.guidedFilter）。
输入图像须预先配准；本文件只负责融合，不执行配准或 AI 推理。

用法：
    # 每张图片为 uint8 灰度图；N 为正奇数，H、W 不限于 544。
    images = np.stack([cv2.imread(p, cv2.IMREAD_GRAYSCALE) for p in paths])[..., None]
    fused = gff_fuse(images)  # (N, H, W, 1) -> (1, H, W, 1)
    # 也可只融合矩形内的区域：左上角 (100, 100)，宽、高各 256 像素。
    fused = gff_fuse(images, roi=(100, 100, 256, 256))
    cv2.imwrite("fused.png", fused[0, ..., 0])
"""

from concurrent.futures import ThreadPoolExecutor

import cv2
import numpy as np


def _normalize_weights(weights):
    """原地归一化：每个像素的各帧权重之和为 1，退化位置平均分配。"""
    total = np.zeros_like(weights[0])
    for weight in weights:
        np.maximum(weight, 0.0, out=weight)
        total += weight
    empty = total <= 1e-12
    total[empty] = 1.0
    for weight in weights:
        weight /= total
        weight[empty] = 1.0 / len(weights)


def _check_integer(name, value):
    """滤波窗口/半径只接受 [1, 255] 内的整数，不接受布尔值。"""
    if (isinstance(value, (bool, np.bool_))
            or not isinstance(value, (int, np.integer))
            or not 1 <= value <= 255):
        raise ValueError(f"{name} 必须是 [1, 255] 内的整数")
    return int(value)


def gff_fuse(
    images,
    *,
    focus_measure="modified_tenengrad",
    focus_window=7,
    base_radius=15,
    detail_radius=3,
    base_epsilon=0.01,
    detail_epsilon=0.0001,
    workers=4,
    roi=None,
):
    """将 (N, H, W, 1) 的 uint8 灰度图融合为 (1, H, W, 1)。

    images：一个四维 NumPy 数组，dtype 必须是 uint8，最后一维固定为 1。
        N 必须为正奇数，H、W 必须为正数，不固定为 544，也无需为正方形。
        N=1 时检查输入及 roi 后直接返回独立副本，不执行融合，不使用融合参数。
        不修改输入，允许只读数组和非连续切片；输出与输入不共享像素存储。
    focus_measure：默认 modified_tenengrad（改进 Tenengrad），也可选择
        modified_laplacian（改进拉普拉斯）或 tenengrad（梯度能量）。
        改进 Tenengrad 先逐点计算 (Gx² + Gy²) / (gray² + 0.001)，再做局部平均；
        gray 是归一化到 [0, 1] 的灰度，分母范围为 [0.001, 1.001]。
    focus_window：清晰度局部平均窗口的边长，默认 7，须为 [1, 255] 内的奇数。
    base_radius：基础层均值滤波半径，也是基础权重的引导滤波半径。
    detail_radius：细节权重的引导滤波半径；两种半径范围均为 [1, 255]。
    base_epsilon/detail_epsilon：引导滤波正则项，基于 [0, 1] 灰度尺度，
        须有限且在 [1e-6, float32 最大值] 内。
    workers：各帧引导滤波的并行数，默认 4，须为正整数；实际不超过图像数量。
        设为 1 可串行执行，适合调用方已并行处理多个批次的情况。
        只使用标准库线程和 NumPy/OpenCV，不修改 OpenCV 的全局线程设置。
    roi：可选矩形 (x, y, width, height)，对应 [y:y+height, x:x+width]。
        坐标从 0 开始，须为整数，宽高须为正，矩形必须完全位于图像内。
        None 表示融合全图；提供矩形时只对各输入的裁剪区域执行融合。
        输出仍为 (1, H, W, 1)，框外直接复制 images[N//2] 的像素：
        5 张取第 3 张；按传入顺序，不额外排序。
        滤波以裁剪区域为边界，因此框边附近可能与全图融合后再裁剪不同。
    """
    # 1. 校验批次格式；保留批次轴和灰度通道轴，不把单张输出压缩成二维。
    if not isinstance(images, np.ndarray):
        raise TypeError("images 必须是形状为 (N, H, W, 1) 的 NumPy 数组")
    if images.ndim != 4 or images.shape[-1] != 1:
        raise ValueError("images 的形状必须为 (N, H, W, 1)，只支持单通道灰度图")
    if images.dtype != np.uint8:
        raise ValueError("images 的数据类型必须为 uint8（8 位灰度图）")
    count, image_height, image_width, _ = images.shape
    if count < 1 or count % 2 == 0:
        raise ValueError("图像数量 N 必须为正奇数，例如 1、3、5")
    if image_height < 1 or image_width < 1:
        raise ValueError("图像的高度 H 和宽度 W 必须为正数")

    region = None
    if roi is not None:
        try:
            rectangle = tuple(roi)
        except TypeError:
            raise ValueError("roi 必须为 (x, y, width, height) 四个整数") from None
        if len(rectangle) != 4 or any(
            isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer))
            for value in rectangle
        ):
            raise ValueError("roi 必须为 (x, y, width, height) 四个整数")
        x, y, width, height = map(int, rectangle)
        if (x < 0 or y < 0 or width <= 0 or height <= 0
                or x + width > image_width or y + height > image_height):
            raise ValueError("roi 宽高必须为正，且矩形必须完全位于图像内")
        region = (slice(y, y + height), slice(x, x + width))

    # 单帧本身就是结果，不需要官方引导滤波或线程池，也不做浮点转换。
    if count == 1:
        return images.copy(order="C")

    if not hasattr(getattr(cv2, "ximgproc", None), "guidedFilter"):
        raise ImportError("缺少官方引导滤波，请在当前环境安装 opencv-contrib-python")
    if focus_measure not in ("modified_laplacian", "tenengrad", "modified_tenengrad"):
        raise ValueError("focus_measure 只能是 modified_laplacian、tenengrad 或 modified_tenengrad")
    focus_window = _check_integer("focus_window", focus_window)
    base_radius = _check_integer("base_radius", base_radius)
    detail_radius = _check_integer("detail_radius", detail_radius)
    if focus_window % 2 == 0:
        raise ValueError("focus_window 必须是奇数")
    if (isinstance(workers, (bool, np.bool_))
            or not isinstance(workers, (int, np.integer)) or workers < 1):
        raise ValueError("workers 必须为正整数")
    for name, value in (("base_epsilon", base_epsilon), ("detail_epsilon", detail_epsilon)):
        if (isinstance(value, (bool, np.bool_))
                or not isinstance(value, (int, float, np.integer, np.floating))
                or not np.isfinite(value)
                or not 1e-6 <= value <= np.finfo(np.float32).max):
            raise ValueError(f"{name} 必须在 [1e-6, float32 最大值] 内且为有限数")

    # 去掉长度为 1 的通道轴只是创建视图；原图在后续归一化时才复制。
    gray_images = images[..., 0]
    output = None
    if region is not None:
        # 先完整复制中间帧，保证框外逐像素不变；仅裁剪区域进入后面的浮点计算。
        output = gray_images[count // 2].copy(order="C")
        gray_images = gray_images[:, region[0], region[1]]

    # 一次完成复制、类型转换与缩放；后续可安全复用这些私有缓冲。
    frames = [np.multiply(image, np.float32(1.0 / 255), dtype=np.float32, order="C")
              for image in gray_images]

    # 2. 灰度图本身就是引导图，在其上评价局部清晰度。
    guides = frames
    kernel = np.array([[-1.0, 2.0, -1.0]], dtype=np.float32)
    scores = []
    for gray in guides:
        if focus_measure == "modified_laplacian":
            dx = cv2.filter2D(gray, cv2.CV_32F, kernel)
            dy = cv2.filter2D(gray, cv2.CV_32F, kernel.T)
            np.abs(dx, out=dx)
            np.abs(dy, out=dy)
            energy = np.add(dx, dy, out=dx)
        else:
            dx = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
            dy = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
            np.square(dx, out=dx)
            np.square(dy, out=dy)
            energy = np.add(dx, dy, out=dx)
            if focus_measure == "modified_tenengrad":
                # 按该点灰度平方加权；0.001 避免黑色像素除零，仍须先除后平均。
                np.square(gray, out=dy)
                dy += np.float32(0.001)
                energy /= dy
        scores.append(cv2.boxFilter(energy, cv2.CV_32F, (focus_window, focus_window)))

    # 3. 每个像素选清晰度最大的帧；近似并列时均分，避免平坦区域偏向首帧。
    maximum = scores[0].copy()
    for score in scores[1:]:
        np.maximum(maximum, score, out=maximum)
    maximum -= np.float32(1e-8)
    # 评分已完成比较，直接将其缓冲改写为 0/1 决策图，保留原来的近似并列规则。
    decisions = [np.greater_equal(score, maximum, out=score) for score in scores]
    _normalize_weights(decisions)
    del scores

    # 4. 原图灰度引导决策图，分别获得平滑的基础权重和保留边缘的细节权重。
    def filter_frame(pair):
        """每帧只读自己的引导图和决策图，两组权重均由官方引导滤波计算。"""
        guide, decision = pair
        weights = []
        for radius, epsilon in (
            (base_radius, base_epsilon), (detail_radius, detail_epsilon),
        ):
            weight = cv2.ximgproc.guidedFilter(
                guide, decision, radius, float(epsilon), dDepth=cv2.CV_32F
            )
            weights.append(np.clip(weight, 0.0, 1.0, out=weight))
        return weights

    # OpenCV 的计算释放 GIL，可并行处理相互独立的帧；map 保持输入顺序。
    if workers == 1:
        pairs = list(map(filter_frame, zip(guides, decisions)))
    else:
        with ThreadPoolExecutor(max_workers=min(int(workers), len(frames))) as executor:
            pairs = list(executor.map(filter_frame, zip(guides, decisions)))
    base_weights, detail_weights = map(list, zip(*pairs))
    _normalize_weights(base_weights)
    _normalize_weights(detail_weights)
    del guides, decisions

    # 5. B = 均值滤波(I)，D = I - B；两层分别加权，再相加重建。
    fused = np.zeros_like(frames[0])
    base = np.empty_like(fused)
    size = 2 * base_radius + 1
    for frame, wb, wd in zip(frames, base_weights, detail_weights):
        cv2.blur(frame, (size, size), dst=base)
        cv2.subtract(frame, base, dst=frame)  # 私有副本改作细节层，不再另建大数组。
        for layer, weight in ((base, wb), (frame, wd)):
            # 直接累加乘积，省去加权层的临时数组；最后几位可能有浮点舍入差异。
            cv2.accumulateProduct(layer, weight, fused)

    # 6. 裁剪可能的重建过冲，恢复 uint8；先舍入，避免直接截断。
    if not np.isfinite(fused).all():
        raise RuntimeError("融合出现非有限值，请检查输入或调整引导滤波正则项")
    np.clip(fused, 0.0, 1.0, out=fused)
    fused *= 255
    np.rint(fused, out=fused)
    fused = fused.astype(np.uint8)
    if output is not None:
        output[region] = fused
        fused = output
    # 单帧结果同样保留批次轴和通道轴，添加这两个轴无需复制像素。
    return fused[None, ..., None]
