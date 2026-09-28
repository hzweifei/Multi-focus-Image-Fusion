#include <mif/fusion.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--demo") {
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
            auto options = mif::FusionOptions{};
            cv::imwrite((dir / "fused_guided.png").string(), mif::fuse({first, second}, options).image);
            options.method = mif::FusionMethod::LaplacianPyramid;
            cv::imwrite((dir / "fused_pyramid.png").string(), mif::fuse({first, second}, options).image);
            std::cout << "Demo written to " << dir << '\n'; return 0;
        }
        if (argc < 4) {
            std::cerr << "Usage: mif_example output.png input1.png input2.png [more...]\n"
                         "       mif_example --demo output-directory\n";
            return 2;
        }
        std::vector<cv::Mat> images;
        for (int i = 2; i < argc; ++i) images.push_back(cv::imread(argv[i], cv::IMREAD_UNCHANGED));
        const auto result = mif::fuse(images);
        const auto extension = std::filesystem::path(argv[1]).extension().string();
        if (result.image.depth() != CV_8U && extension != ".png" && extension != ".tif" && extension != ".tiff")
            throw std::runtime_error("Use PNG or TIFF to preserve high bit depth");
        if (result.image.depth() == CV_32F && extension != ".tif" && extension != ".tiff")
            throw std::runtime_error("Use TIFF to preserve float depth");
        if (!cv::imwrite(argv[1], result.image, {cv::IMWRITE_TIFF_COMPRESSION, 1}))
            throw std::runtime_error("Failed to save output");
        std::cout << "Saved " << argv[1] << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

