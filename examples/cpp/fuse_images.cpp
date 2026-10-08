#include <mif/mif.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <iostream>

// C++ 调用示例：mif_example output.png input1.png input2.png [更多输入...]
// 也可使用 mif_example --demo 输出目录，生成互补清晰样例及五种方法的融合结果。
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--demo") {
            // 先生成纹理与文字组成的参考图，再构造左右清晰区域互补的两张输入。
            const std::filesystem::path dir(argv[2]);
            std::filesystem::create_directories(dir);
            cv::Mat sharp(384, 640, CV_8UC3, cv::Scalar(28, 32, 38));
            for (int y = 20; y < sharp.rows - 20; y += 12)
                for (int x = 20; x < sharp.cols - 20; x += 12)
                    cv::rectangle(sharp, {x, y, 6, 6}, cv::Scalar(190, 210, 230), -1);
            cv::putText(sharp, "MULTI FOCUS", {55, 205}, cv::FONT_HERSHEY_SIMPLEX, 2.1,
                        cv::Scalar(70, 230, 170), 5, cv::LINE_AA);
            cv::Mat blurred;
            cv::GaussianBlur(sharp, blurred, {0, 0}, 3.5);
            auto first = blurred.clone(), second = blurred.clone();
            const cv::Rect left(0, 0, sharp.cols / 2, sharp.rows);
            const cv::Rect right(sharp.cols / 2, 0, sharp.cols - sharp.cols / 2, sharp.rows);
            sharp(left).copyTo(first(left)); sharp(right).copyTo(second(right));
            cv::imwrite((dir / "focus_01.png").string(), first);
            cv::imwrite((dir / "focus_02.png").string(), second);
            cv::imwrite((dir / "reference.png").string(), sharp);
            // 参数类型直接决定方法，五种算法共用同一个调用入口。
            const std::vector<cv::Mat> images{first, second};
            mif::GuidedFilterFusionOptions guided;
            guided.focus.window_size = 9;
            cv::imwrite((dir / "fused_guided.png").string(), mif::fuse(images, guided).image);
            cv::imwrite((dir / "fused_pyramid.png").string(), mif::fuse(images, mif::LaplacianPyramidFusionOptions{}).image);
            cv::imwrite((dir / "fused_block_variance.png").string(), mif::fuse(images, mif::BlockVarianceFusionOptions{}).image);
            cv::imwrite((dir / "fused_dtcwt.png").string(), mif::fuse(images, mif::DtcwtFusionOptions{}).image);
            cv::imwrite((dir / "fused_gfg_fgf.png").string(), mif::fuse(images, mif::GfgFgfFusionOptions{}).image);
            std::cout << "Demo written to " << dir << '\n'; return 0;
        }
        if (argc < 4) {
            std::cerr << "Usage: mif_example output.png input1.png input2.png [more...]\n"
                         "       mif_example --demo output-directory\n";
            return 2;
        }
        std::vector<cv::Mat> images;
        // 保留源文件的精度和通道；彩色图像按 OpenCV 约定使用 BGR 排列。
        // 读取失败会产生空 Mat，交给核心统一校验后由下方异常处理报告。
        for (int i = 2; i < argc; ++i) images.push_back(cv::imread(argv[i], cv::IMREAD_UNCHANGED));
        const auto result = mif::fuse(images);
        // 输出格式必须支持当前精度，避免保存时静默丢失 16 位或浮点信息。
        // 浮点 TIFF 使用无压缩存储，以免默认彩色浮点编码改变像素值。
        const auto extension = std::filesystem::path(argv[1]).extension().string();
        if (result.image.depth() != CV_8U && extension != ".png" && extension != ".tif" && extension != ".tiff")
            throw std::runtime_error("Use PNG or TIFF to preserve high bit depth");
        if (result.image.depth() == CV_32F && extension != ".tif" && extension != ".tiff")
            throw std::runtime_error("Use TIFF to preserve float depth");
        if (!cv::imwrite(argv[1], result.image, {cv::IMWRITE_TIFF_COMPRESSION, 1}))
            throw std::runtime_error("Failed to save output");
        std::cout << "Saved " << argv[1] << '\n'; return 0;
    } catch (const std::exception& error) {
        // 输入校验、融合和写出错误统一映射为非零退出码，便于脚本调用。
        std::cerr << error.what() << '\n'; return 1;
    }
}

