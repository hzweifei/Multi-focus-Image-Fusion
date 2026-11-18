"""生成有清晰参考图的合成数据，验证独立 Python GFF 的融合效果。

在已安装 numpy、opencv-contrib-python、pillow 的环境中运行：
    python examples/python/test_gff_synthetic.py
默认将参考图、输入、结果、对比图和 JSON 报告保存到 outputs/tests/gff_synthetic。
这些数据只模拟空间变化的高斯失焦，不模拟真实镜头、运动或配准误差。
"""

import argparse
import json
from pathlib import Path
from time import perf_counter
from unittest.mock import patch

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

from gff import gff_fuse

FOCUS_MEASURES = ("modified_tenengrad", "modified_laplacian", "tenengrad")
DEFAULT_MEASURE = "modified_tenengrad"


def make_reference():
    """生成 float32 灰度参考图，包含不同方向/频率的细节及平坦区域。"""
    height, width = 512, 768
    y, x = np.mgrid[:height, :width].astype(np.float32)
    background = 0.38 + 0.04 * np.sin(x / 45) + 0.04 * np.cos(y / 31)
    image = background.copy()
    # 六块内容覆盖棋盘、渐变频率条纹、圆环、小字、斜线和随机纹理。
    boxes = [(24, 65, 230, 235), (280, 65, 490, 235), (538, 65, 744, 235),
             (24, 285, 230, 465), (280, 285, 490, 465), (538, 285, 744, 465)]
    rng = np.random.default_rng(20261110)
    for index, (x0, y0, x1, y1) in enumerate(boxes):
        yy, xx = np.mgrid[:y1-y0, :x1-x0].astype(np.float32)
        if index == 0:
            pattern = 0.12 + 0.76 * ((xx // 6 + yy // 6) % 2)
        elif index == 1:
            pattern = 0.5 + 0.4 * np.sin(0.10 * xx + 0.004 * xx * xx)
        elif index == 2:
            radius = np.sqrt((xx - xx.max() / 2) ** 2 + (yy - yy.max() / 2) ** 2)
            pattern = 0.5 + 0.4 * np.cos(radius * 0.9)
        elif index == 3:
            pattern = np.full_like(xx, 0.9)
        elif index == 4:
            pattern = 0.12 + 0.76 * (((xx + yy) // 3) % 2)
        else:
            pattern = 0.15 + 0.7 * rng.random(xx.shape, dtype=np.float32)
        image[y0:y1, x0:x1] = pattern
        cv2.rectangle(image, (x0-2, y0-2), (x1+1, y1+1), 0.96, 2)
    for row, text in enumerate(("GFF 012345", "Focus detail", "ABC abc", "1 2 3 4 5")):
        cv2.putText(image, text, (30, 316 + 39 * row), cv2.FONT_HERSHEY_SIMPLEX,
                    0.55, 0.07, 1, cv2.LINE_AA)
    cv2.putText(image, "MULTI-FOCUS / SYNTHETIC TARGET", (24, 38),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, 0.95, 2)
    cv2.line(image, (12, 258), (755, 258), 0.45, 1)
    cv2.line(image, (12, 268), (755, 268), 0.75, 2)
    cv2.putText(image, "Fine lines / edges / texture / flat regions", (24, 495),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, 0.95, 1)
    return np.clip(image, 0, 1).astype(np.float32)


def make_inputs(reference, kind, noise_sigma=0.0):
    """参考图与模糊图按平滑掩膜混合；掩膜仅用于造数据和评价。"""
    height, width = reference.shape[:2]
    y, x = np.mgrid[:height, :width]
    if kind == "three":
        # 分界设在 1/4 与 3/4 位置，穿过棋盘/文字/圆环/纹理，不只考察平坦间隙。
        owner = (x >= width // 4).astype(np.uint8) + (x >= width * 3 // 4).astype(np.uint8)
    elif kind == "five":
        owner = (x * 5 // width).astype(np.uint8)
    elif kind == "irregular":
        foreground = ((x - width * 0.49) / 230) ** 2 + ((y - height * 0.5) / 180) ** 2 < 1
        owner = (x >= width // 2).astype(np.uint8)
        owner[foreground] = 2  # 左背景、右背景、椭圆前景分别由三帧对焦。
    else:
        raise ValueError("kind 必须为 three、five 或 irregular")
    rng = np.random.default_rng(1042)
    frames, masks = [], []
    for index in range(int(owner.max()) + 1):
        # sigma=2 的过渡带避免突兀的硬接缝，但交界处可能没有完全清晰的输入。
        mask = cv2.GaussianBlur((owner == index).astype(np.float32), (0, 0), 2.0)
        blurred = cv2.GaussianBlur(reference, (0, 0), 3.0 + index)
        frame = mask * reference + (1 - mask) * blurred
        if noise_sigma:
            frame += rng.normal(0, noise_sigma, frame.shape).astype(np.float32)
        frames.append(np.clip(frame, 0, 1))
        masks.append(mask)
    # 标出分区边界附近约 24 像素宽的区域，单独衡量光晕/残留模糊。
    boundary = cv2.morphologyEx(owner, cv2.MORPH_GRADIENT, np.ones((25, 25), np.uint8)) > 0
    return frames, masks, boundary


def to_uint8(image):
    return np.rint(np.clip(image, 0, 1) * 255).astype(np.uint8)


def save_image(path, image):
    """用内存编码支持 Windows 中文路径，并检查保存是否成功。"""
    ok, data = cv2.imencode(".png", image)
    if not ok:
        raise RuntimeError(f"不能编码图片：{path}")
    data.tofile(str(path))


def metrics(image, reference, boundary):
    """PSNR 的峰值为 1；MAE 以 [0, 1] 强度为单位，越小越好。"""
    error = (image.astype(np.float64) - reference.astype(np.float64)) / 255.0
    mse = float(np.mean(error ** 2))
    absolute = np.abs(error)
    return {"psnr_db": float(-10 * np.log10(mse)) if mse else None,
            "mae": float(absolute.mean()),
            "boundary_mae": float(absolute[boundary].mean()),
            "interior_mae": float(absolute[~boundary].mean())}


def montage(path, panels, title, subtitle, font_path, panel_size=(480, 320)):
    """保存带中文说明的对比图；所有面板使用同样的缩放与显示范围。"""
    font = ImageFont.truetype(str(font_path), 19)
    small = ImageFont.truetype(str(font_path), 15)
    width, height = panel_size
    rows = (len(panels) + 1) // 2
    canvas = Image.new("RGB", (width * 2 + 48, 94 + rows * (height + 58)), "#f3f5f8")
    draw = ImageDraw.Draw(canvas)
    draw.text((16, 12), title, font=font, fill="#152536")
    draw.text((16, 46), subtitle, font=small, fill="#45576a")
    for index, (label, bgr) in enumerate(panels):
        left = 16 + (index % 2) * (width + 16)
        top = 88 + (index // 2) * (height + 58)
        draw.text((left, top), label, font=font, fill="#152536")
        rgb = cv2.cvtColor(bgr, cv2.COLOR_GRAY2RGB if bgr.ndim == 2 else cv2.COLOR_BGR2RGB)
        if rgb.shape[1] > width or rgb.shape[0] > height:
            rgb = cv2.resize(rgb, panel_size, interpolation=cv2.INTER_AREA)
        picture = Image.fromarray(rgb).resize(panel_size, Image.Resampling.NEAREST)
        canvas.paste(picture, (left, top + 32))
    canvas.save(path)


def contract_checks(reference):
    """检查 NHWC 灰度批次、奇数帧、单帧直通、输入独立性及线程一致性。"""
    checks = {"valid": [], "invalid": []}
    frames, _, _ = make_inputs(reference[::4, ::4], "five")
    batch = np.stack([to_uint8(frame) for frame in frames])[..., None]
    for count in (1, 3, 5):
        # 非连续、只读的非方形图像也应接受；不要求尺寸为 544。
        images = batch[:count, ::2, ::-2, :]
        original = images.copy()
        images.flags.writeable = False
        for measure in FOCUS_MEASURES:
            fused = gff_fuse(images, focus_measure=measure)
            assert fused.shape == (1, *images.shape[1:]) and fused.dtype == np.uint8
            np.testing.assert_array_equal(images, original)
            assert not np.shares_memory(fused, images)
            if measure == DEFAULT_MEASURE:
                np.testing.assert_array_equal(fused, gff_fuse(images))
            serial = gff_fuse(images, focus_measure=measure, workers=1)
            np.testing.assert_array_equal(fused, serial)
            reversed_result = gff_fuse(images[::-1], focus_measure=measure)
            order_error = int(np.abs(fused.astype(np.int16) - reversed_result).max())
            assert order_error <= 1
            if count == 1:
                np.testing.assert_array_equal(fused, images)
            identical = gff_fuse(np.repeat(images[:1], 3, axis=0), focus_measure=measure)
            np.testing.assert_allclose(identical, images[:1], atol=1, rtol=0)
            checks["valid"].append({"frames": count, "shape": list(images.shape),
                                    "measure": measure, "order_max_error": order_error})

    # 单帧无需 contrib，也不能启动线程池；带 ROI 仍应完整返回原图副本。
    single = batch[:1]
    with patch.object(cv2, "ximgproc", None), patch(
        "gff.ThreadPoolExecutor", side_effect=AssertionError("单帧不应创建线程池")
    ):
        for roi in (None, (3, 4, 12, 10)):
            fused = gff_fuse(single, roi=roi)
            np.testing.assert_array_equal(fused, single)
            assert not np.shares_memory(fused, single)
    checks["single_without_contrib"] = "passed"

    for shape in ((1, 1, 1, 1), (3, 1, 7, 1), (5, 9, 1, 1)):
        images = np.full(shape, 91, dtype=np.uint8)
        np.testing.assert_array_equal(gff_fuse(images), images[:1])
        checks["valid"].append({"shape": list(shape), "case": "tiny_dimension"})

    invalid = [("list", [batch[0, ..., 0]]), ("zero_frames", batch[:0]),
               ("even_frames", batch[:2]), ("two_dimensions", batch[0, ..., 0]),
               ("three_dimensions", batch[..., 0]), ("three_channels", np.repeat(batch, 3, axis=3)),
               ("empty_height", batch[:, :0]), ("empty_width", batch[:, :, :0]),
               ("uint16", batch.astype(np.uint16)), ("float32", batch.astype(np.float32))]
    for name, images in invalid:
        try:
            gff_fuse(images)
        except (TypeError, ValueError):
            checks["invalid"].append(name)
        else:
            raise AssertionError(f"非法输入未被拒绝：{name}")
    return checks


def roi_checks():
    """验证只融合矩形内，框外保留中间帧；坐标采用 (x, y, width, height)。"""
    rng = np.random.default_rng(20261117)
    images = rng.integers(0, 256, (5, 48, 64, 1), dtype=np.uint8)
    checks = {"valid": [], "invalid": []}

    def check(name, frames, roi, middle_index, **options):
        original = frames.copy()
        frames.flags.writeable = False
        x, y, width, height = map(int, roi)
        region = (slice(y, y + height), slice(x, x + width))
        outside = np.ones(frames.shape[1:3], dtype=bool)
        outside[region] = False
        fused = gff_fuse(frames, roi=roi, **options)
        assert fused.shape == (1, *frames.shape[1:]) and fused.dtype == np.uint8
        np.testing.assert_array_equal(frames, original)
        assert not np.shares_memory(fused, frames)
        np.testing.assert_array_equal(fused[0, ..., 0][outside], frames[middle_index, ..., 0][outside])
        # 常规尺寸的 ROI 应与显式裁剪后融合完全相同，验证框外不参与滤波。
        expected = gff_fuse(frames[:, y:y + height, x:x + width, :], **options)
        np.testing.assert_array_equal(fused[:, y:y + height, x:x + width, :], expected)
        checks["valid"].append({"name": name, "roi": [x, y, width, height],
                                "frames": len(frames), "middle_index": middle_index,
                                "dtype": str(fused.dtype)})
        return fused

    roi = (11, 9, 30, 24)
    for measure in FOCUS_MEASURES:
        check(measure, images, roi, 2, focus_measure=measure)
    check("serial_custom_options", images, roi, 2, workers=1, focus_window=3,
          base_radius=5, detail_radius=2, base_epsilon=0.02, detail_epsilon=0.001)
    check("three_frames_middle", images[:3], roi, 1)
    single = check("one_frame_passthrough", images[:1], roi, 0)
    np.testing.assert_array_equal(single, images[:1])
    check("top_left", images, (0, 0, 9, 8), 2)
    check("bottom_right", images, (55, 40, 9, 8), 2)
    full = check("whole_image", images, (0, 0, 64, 48), 2)
    np.testing.assert_array_equal(full, gff_fuse(images))
    np.testing.assert_array_equal(full, gff_fuse(images, roi=None))
    check("numpy_coordinates_noncontiguous", images[:, ::2, ::2, :],
          np.array([3, 4, 12, 10], dtype=np.int64), 2)

    # 单像素/单行/单列也允许：常量输入清晰度并列，框内应接近各帧均值。
    constants = np.stack([np.full((4, 6), value, np.uint8) for value in (12, 25, 80, 170, 238)])[..., None]
    for name, rectangle in (("one_pixel", (5, 3, 1, 1)),
                            ("one_row", (1, 0, 4, 1)), ("one_column", (0, 1, 1, 3))):
        fused = check(name, constants, rectangle, 2)
        x, y, width, height = rectangle
        np.testing.assert_allclose(fused[:, y:y + height, x:x + width, :], 105, atol=1, rtol=0)

    invalid = [(), (1, 2, 3), (1, 2, 3, 4, 5), 3, "1,2,3,4",
               (1.0, 2, 3, 4), (True, 2, 3, 4), (0, 0, np.bool_(True), 2),
               (np.nan, 0, 2, 2), (0, 0, 0, 2), (0, 0, 2, -1),
               (-1, 0, 2, 2), (0, -1, 2, 2), (63, 0, 2, 2),
               (0, 47, 2, 2), (64, 0, 1, 1), (0, 48, 1, 1), (0, 0, 65, 48)]
    for rectangle in invalid:
        try:
            gff_fuse(images, roi=rectangle)
        except ValueError:
            checks["invalid"].append(repr(rectangle))
        else:
            raise AssertionError(f"非法 ROI 未被拒绝：{rectangle!r}")
    return checks


def run(output, font_path):
    output.mkdir(parents=True, exist_ok=True)
    sharp_float = make_reference()
    sharp = to_uint8(sharp_float)
    save_image(output / "reference.png", sharp)
    cases = [("three_focus", "三帧：左中右区域互补（1/4、1/2、1/4）", "three", 0.0),
             ("five_focus", "五帧：五个竖向区域分别对焦", "five", 0.0),
             ("irregular_focus", "三帧：左右背景与椭圆前景", "irregular", 0.0),
             ("noisy_focus", "三帧：不规则边界 + 噪声", "irregular", 0.015)]
    report = {"seed": 20261110, "opencv": cv2.__version__, "numpy": np.__version__,
              "image_size": [768, 512], "opencv_threads": cv2.getNumThreads(),
              "interface": {"input": "uint8 (N,H,W,1), N positive odd", "output": "uint8 (1,H,W,1)"},
              "focus_options": {"focus_measure": DEFAULT_MEASURE, "focus_window": 7, "base_radius": 15, "detail_radius": 3,
                                "base_epsilon": 0.01, "detail_epsilon": 0.0001, "workers": 4},
              "generation": {"transition_sigma": 2.0, "defocus_sigma": "3 + frame_index", "noise_seed": 1042,
                             "three_frame_split_fraction": [0.25, 0.75],
                             "noise_sigma_unit": "normalized [0, 1] intensity"},
              "modified_tenengrad_definition": "先逐点计算 (Gx²+Gy²)/(g²+0.001)，再局部平均；g 为 [0,1] 灰度",
              "metric_note": "PSNR 越大越好；MAE 越小越好。psnr_db=null 表示零误差（无穷大）。",
              "limitation": "平滑分区交界处可能无完全清晰输入；已知掩膜选择仅作参照，不是质量上界。合成测试不能替代实拍测试。",
              "cases": []}
    for name, title, kind, noise in cases:
        folder = output / name
        folder.mkdir(exist_ok=True)
        floats, masks, boundary = make_inputs(sharp_float, kind, noise)
        inputs = np.stack([to_uint8(frame) for frame in floats])
        batch = inputs[..., None]  # 输入保留图像批次和单通道维度。
        for index, (frame, mask) in enumerate(zip(inputs, masks), 1):
            save_image(folder / f"input_{index}.png", frame)
            save_image(folder / f"focus_mask_{index}.png", to_uint8(mask))
        save_image(folder / "boundary_region.png", boundary.astype(np.uint8) * 255)
        average = np.rint(np.mean(inputs, axis=0)).astype(np.uint8)
        save_image(folder / "average.png", average)
        # 已知掩膜选择图使用生成时的最清晰区域，GFF 只接收 inputs，不接收此信息。
        selection = np.argmax(masks, axis=0)
        oracle = np.take_along_axis(inputs, selection[None, ...], axis=0)[0]
        save_image(folder / "known_mask_selection.png", oracle)
        input_metrics = [metrics(frame, sharp, boundary) for frame in inputs]
        record = {"name": name, "title": title, "frames": len(inputs), "noise_sigma": noise,
                  "boundary_fraction": float(boundary.mean()), "inputs": input_metrics,
                  "best_input": max(input_metrics, key=lambda m: m["psnr_db"]),
                  "average": metrics(average, sharp, boundary),
                  "known_mask_selection": metrics(oracle, sharp, boundary), "gff": {}}
        results = {}
        for measure in FOCUS_MEASURES:
            gff_fuse(batch, focus_measure=measure)  # 预热，不计入耗时；不包含文件读写。
            times = []
            for _ in range(3):
                start = perf_counter()
                fused_batch = gff_fuse(batch, focus_measure=measure)
                times.append((perf_counter() - start) * 1000)
            assert fused_batch.shape == (1, *sharp.shape, 1)
            fused = fused_batch[0, ..., 0]  # 保存图片和计算指标时取出二维图像。
            results[measure] = fused
            detail = metrics(fused, sharp, boundary)
            detail["median_ms"] = float(np.median(times))
            record["gff"][measure] = detail
            save_image(folder / f"gff_{measure}.png", fused)
            # 干净场景应优于最佳单帧及直接平均；噪声场景只记录实测，不预设效果。
            if not noise:
                assert detail["psnr_db"] > record["best_input"]["psnr_db"]
                assert detail["psnr_db"] > record["average"]["psnr_db"]
        fused = results[DEFAULT_MEASURE]
        error = np.abs(fused.astype(np.float32) - sharp) / 255.0
        heat = cv2.applyColorMap(to_uint8(error / 0.10), cv2.COLORMAP_INFERNO)
        save_image(folder / "error_heatmap.png", heat)
        panels = [(f"输入 {i+1}：PSNR {input_metrics[i]['psnr_db']:.2f} dB", frame)
                  for i, frame in enumerate(inputs)]
        panels += [("全清晰参考图", sharp),
                   (f"直接平均：{record['average']['psnr_db']:.2f} dB", average),
                   (f"改进 Tenengrad（默认）：{record['gff'][DEFAULT_MEASURE]['psnr_db']:.2f} dB", fused)]
        panels.append(("GFF 误差：黑=0，黄白≥0.10", heat))
        montage(folder / "comparison.png", panels, title,
                "已对齐的合成输入；PSNR 对比同一参考图。显示缩小，原始 PNG 保留完整分辨率。", font_path)
        # 按造数据时的边界位置指定裁剪，不依赖融合输出挑选展示区域。
        x0, y0 = {"five": (70, 92), "three": (96, 92), "irregular": (122, 100)}[kind]
        x1, y1 = x0 + 192, y0 + 120
        record["detail_region_xyxy"] = [x0, y0, x1, y1]
        crop = (slice(y0, y1), slice(x0, x1))
        zoom_panels = [(label, image[crop]) for label, image in panels]
        montage(folder / "detail.png", zoom_panels, title + "：交界细节放大",
                f"裁剪 x={x0}:{x1}、y={y0}:{y1}，最近邻放大；面板标题的 PSNR 仍为全图指标。",
                font_path, panel_size=(480, 300))
        report["cases"].append(record)
        print(f"{name}: best={record['best_input']['psnr_db']:.2f}, "
              f"mean={record['average']['psnr_db']:.2f}, "
              f"GFF={record['gff'][DEFAULT_MEASURE]['psnr_db']:.2f} dB")
    report["contract_checks"] = contract_checks(sharp_float)
    report["roi_checks"] = roi_checks()
    report["status"] = "passed"
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    print(f"PASS: {len(cases)} scenes x {len(FOCUS_MEASURES)} focus measures; "
          f"{len(report['contract_checks']['valid'])} batch/identity/order/thread checks + "
          f"{len(report['contract_checks']['invalid'])} invalid input checks; "
          f"{len(report['roi_checks']['valid'])} ROI cases + {len(report['roi_checks']['invalid'])} invalid ROI checks")
    print(output.resolve())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "outputs/tests/gff_synthetic")
    parser.add_argument("--font", type=Path, default=Path("C:/Windows/Fonts/msyh.ttc"), help="用于对比图的中文字体路径")
    args = parser.parse_args()
    run(args.output, args.font)
