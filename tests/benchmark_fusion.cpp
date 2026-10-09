// 单进程只测一个批次和一种算法。解码、输入检查、结果落盘均在计时区间之外。
// 示例：mif_benchmark "D:/images/1" dtcwt "outputs/performance/baseline/1-dtcwt-t8" 5 8
#include <mif/mif.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct MemorySample {
    bool available = false;
    double working_set_bytes = 0;
    double peak_working_set_bytes = 0;
};

struct TimedRun {
    double seconds = 0;
    MemorySample before;
    MemorySample after;
};

/// Windows 峰值是进程自启动以来的最高工作集，不能当成单次调用的额外分配量。
MemorySample sampleMemory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        throw std::runtime_error("Cannot query process working set");
    return {true, static_cast<double>(counters.WorkingSetSize), static_cast<double>(counters.PeakWorkingSetSize)};
#else
    return {};
#endif
}

void writeMemory(cv::FileStorage& json, const std::string& key, const MemorySample& memory) {
    json << key << "{" << "available" << static_cast<int>(memory.available)
         << "working_set_bytes" << memory.working_set_bytes
         << "peak_working_set_bytes" << memory.peak_working_set_bytes << "}";
}

/// OpenCV 对单个 JSON 字符串有长度限制，编译选项的一行也可能超过此限制。
/// 每块最多 512 个 UTF-8 字节且不拆开多字节字符；直接连接各块即可还原全文。
void writeTextChunks(cv::FileStorage& json, const std::string& value) {
    json << "[";
    for (std::size_t offset = 0; offset < value.size();) {
        auto end = std::min(offset + std::size_t{512}, value.size());
        while (end < value.size() && end > offset &&
               (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
        if (end == offset) throw std::runtime_error("Invalid UTF-8 text in benchmark metadata");
        json << value.substr(offset, end - offset);
        offset = end;
    }
    json << "]";
}

/// 普通路径仍保存为字符串；极长路径等动态文本改用无损分块数组。
void writeText(cv::FileStorage& json, const std::string& key, const std::string& value) {
    json << key;
    if (value.size() <= 512) json << value;
    else writeTextChunks(json, value);
}

/// 记录实际加载的核心 DLL，便于核对同一个基准程序是否确实运行了冻结的旧核心。
std::string coreLibraryPath() {
#ifdef _WIN32
    HMODULE module = GetModuleHandleW(L"mif_core.dll");
    if (!module) module = GetModuleHandleW(L"mif_cored.dll");
    if (!module) return "static-or-unavailable";
    std::wstring buffer(32768, L'\0');
    const DWORD count = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) throw std::runtime_error("Cannot determine the loaded core DLL path");
    buffer.resize(count);
    return fs::path(buffer).u8string();
#else
    return "unavailable-on-this-platform";
#endif
}

unsigned char asciiLower(unsigned char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

bool digit(unsigned char value) { return value >= '0' && value <= '9'; }

/// 数字段按整数大小比较，避免 10 排在 2 前面；相同数字用长度及原名稳定消歧。
/// 负号作为文件名中的普通字符处理，实际输入顺序完整写入 JSON，保证新旧运行一致。
bool naturalLess(const fs::path& left, const fs::path& right) {
    const auto a = left.filename().u8string(), b = right.filename().u8string();
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(static_cast<unsigned char>(a[i])) && digit(static_cast<unsigned char>(b[j]))) {
            const auto begin_a = i, begin_b = j;
            while (i < a.size() && digit(static_cast<unsigned char>(a[i]))) ++i;
            while (j < b.size() && digit(static_cast<unsigned char>(b[j]))) ++j;
            auto significant_a = begin_a, significant_b = begin_b;
            while (significant_a < i && a[significant_a] == '0') ++significant_a;
            while (significant_b < j && b[significant_b] == '0') ++significant_b;
            if (i - significant_a != j - significant_b) return i - significant_a < j - significant_b;
            const int comparison = a.compare(significant_a, i - significant_a, b, significant_b, j - significant_b);
            if (comparison != 0) return comparison < 0;
            if (i - begin_a != j - begin_b) return i - begin_a < j - begin_b;
        } else {
            const auto ca = asciiLower(static_cast<unsigned char>(a[i]));
            const auto cb = asciiLower(static_cast<unsigned char>(b[j]));
            if (ca != cb) return ca < cb;
            ++i;
            ++j;
        }
    }
    if (i != a.size() || j != b.size()) return i == a.size();
    return a < b;
}

std::vector<fs::path> listImages(const fs::path& directory) {
    if (!fs::is_directory(directory)) throw std::invalid_argument("Input must be one image batch directory");
    const std::vector<std::string> extensions{ ".png", ".jpg", ".jpeg", ".tif", ".tiff", ".bmp", ".webp", ".jp2", ".ppm", ".pgm" };
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        auto extension = entry.path().extension().u8string();
        for (char& value : extension) value = static_cast<char>(asciiLower(static_cast<unsigned char>(value)));
        if (std::find(extensions.begin(), extensions.end(), extension) != extensions.end())
            files.push_back(fs::canonical(entry.path()));
    }
    std::sort(files.begin(), files.end(), naturalLess);
    if (files.size() < 2 || files.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("A batch must contain at least two supported image files");
    return files;
}

/// 用 filesystem 路径读取字节后原样解码，兼容 Windows 中文路径且不降低精度或分辨率。
cv::Mat readImage(const fs::path& path) {
    const auto size = fs::file_size(path);
    if (size == 0 || size > static_cast<std::uintmax_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Empty image file or encoded image exceeds the supported buffer size");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read image: " + path.u8string());
    auto image = cv::imdecode(bytes, cv::IMREAD_UNCHANGED);
    if (image.empty()) throw std::runtime_error("Cannot decode image: " + path.u8string());
    return image;
}

std::string depthName(int depth) {
    switch (depth) {
    case CV_8U: return "uint8";
    case CV_16U: return "uint16";
    case CV_32F: return "float32";
    case CV_32S: return "int32";
    default: return "unsupported";
    }
}

/// 轻量字节指纹仅用于发现运行中输入被修改，不作为密码学文件校验值。
std::string fingerprint(const cv::Mat& image) {
    std::uint64_t value = UINT64_C(14695981039346656037);
    const auto row_bytes = static_cast<std::size_t>(image.cols) * image.elemSize();
    for (int y = 0; y < image.rows; ++y) {
        const auto* row = image.ptr<unsigned char>(y);
        for (std::size_t x = 0; x < row_bytes; ++x) {
            value ^= row[x];
            value *= UINT64_C(1099511628211);
        }
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0') << std::setw(16) << value;
    return text.str();
}

std::unique_ptr<mif::FusionOptionsBase> methodOptions(const std::string& method) {
    if (method == "gff") return std::make_unique<mif::GuidedFilterFusionOptions>();
    if (method == "pyramid") return std::make_unique<mif::LaplacianPyramidFusionOptions>();
    if (method == "gfg") return std::make_unique<mif::GfgFgfFusionOptions>();
    if (method == "block") return std::make_unique<mif::BlockVarianceFusionOptions>();
    if (method == "dtcwt") return std::make_unique<mif::DtcwtFusionOptions>();
    throw std::invalid_argument("Method must be gff, pyramid, gfg, block or dtcwt");
}

/// 将实际参数值一起记录，避免默认配置变化被误认为同条件加速。
void writeOptions(cv::FileStorage& json, const mif::FusionOptionsBase& options) {
    json << "fusion_options" << "{" << "include_weight_maps" << static_cast<int>(options.include_weight_maps);
    const auto focus = [&](const mif::FocusMeasureOptions& value) {
        json << "focus_measure" << (value.measure == mif::FocusMeasure::Tenengrad ? "tenengrad" : "modified_laplacian")
             << "focus_window_size" << value.window_size;
    };
    if (const auto* gff = dynamic_cast<const mif::GuidedFilterFusionOptions*>(&options)) {
        focus(gff->focus);
        json << "base_radius" << gff->base_radius << "detail_radius" << gff->detail_radius
             << "base_epsilon" << gff->base_epsilon << "detail_epsilon" << gff->detail_epsilon;
    } else if (const auto* pyramid = dynamic_cast<const mif::LaplacianPyramidFusionOptions*>(&options)) {
        focus(pyramid->focus);
        json << "detail_radius" << pyramid->detail_radius << "detail_epsilon" << pyramid->detail_epsilon
             << "max_levels" << pyramid->max_levels;
    } else if (const auto* gfg = dynamic_cast<const mif::GfgFgfFusionOptions*>(&options)) {
        json << "local_mean_window_size" << gfg->local_mean_window_size << "selection_ratio" << gfg->selection_ratio
             << "gfg_threshold" << gfg->gfg_threshold << "guided_radius" << gfg->guided_radius
             << "guided_epsilon" << gfg->guided_epsilon << "guided_subsample_factor" << gfg->guided_subsample_factor;
    } else if (const auto* block = dynamic_cast<const mif::BlockVarianceFusionOptions*>(&options)) {
        json << "block_size" << block->block_size << "consistency_window_size" << block->consistency_window_size;
    } else if (const auto* dtcwt = dynamic_cast<const mif::DtcwtFusionOptions*>(&options)) {
        json << "max_levels" << dtcwt->max_levels << "activity_window_size" << dtcwt->activity_window_size;
    }
    json << "}";
}

int positiveInteger(const std::string& text, int maximum, const char* name) {
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed);
    if (consumed != text.size() || value < 1 || value > maximum)
        throw std::invalid_argument(std::string(name) + " is outside its supported range");
    return value;
}

void writeBytes(const fs::path& path, const unsigned char* data, std::size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Cannot write output: " + path.u8string());
    output.close();
    if (!output) throw std::runtime_error("Cannot finish output: " + path.u8string());
}

/// NPY v1.0 的原始逐行数据无需 NumPy 依赖，保留 float32 图像和 int32 来源索引的每个比特。
void writeNpy(const fs::path& path, const cv::Mat& image) {
    if (image.empty() || image.dims != 2) throw std::invalid_argument("NPY output must be a nonempty 2-D image");
    const std::uint16_t endian_probe = 1;
    const char endian = *reinterpret_cast<const unsigned char*>(&endian_probe) == 1 ? '<' : '>';
    std::string descriptor;
    switch (image.depth()) {
    case CV_8U: descriptor = "|u1"; break;
    case CV_16U: descriptor = std::string(1, endian) + "u2"; break;
    case CV_32F: descriptor = std::string(1, endian) + "f4"; break;
    case CV_32S: descriptor = std::string(1, endian) + "i4"; break;
    default: throw std::invalid_argument("Unsupported NPY output depth");
    }
    std::ostringstream dictionary;
    dictionary << "{'descr': '" << descriptor << "', 'fortran_order': False, 'shape': ("
               << image.rows << ", " << image.cols;
    if (image.channels() != 1) dictionary << ", " << image.channels();
    dictionary << "), }";
    auto header = dictionary.str();
    header.append((64 - (10 + header.size() + 1) % 64) % 64, ' ');
    header.push_back('\n');
    if (header.size() > 65535) throw std::runtime_error("NPY header is too large");
    const unsigned char prefix[]{0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0,
        static_cast<unsigned char>(header.size() & 255), static_cast<unsigned char>(header.size() >> 8)};
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(prefix), sizeof(prefix));
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    const auto row_bytes = static_cast<std::streamsize>(static_cast<std::size_t>(image.cols) * image.elemSize());
    for (int y = 0; y < image.rows; ++y)
        output.write(reinterpret_cast<const char*>(image.ptr(y)), row_bytes);
    output.close();
    if (!output) throw std::runtime_error("Cannot write NPY output: " + path.u8string());
}

fs::path suffixed(const fs::path& prefix, const char* suffix) {
    auto path = prefix;
    path += suffix;
    return path;
}

int run(const std::vector<fs::path>& arguments) {
    if (arguments.size() < 4 || arguments.size() > 7) {
        std::cerr << "Usage: mif_benchmark <batch-directory> <gff|pyramid|gfg|block|dtcwt> <output-prefix> [repeat=3] [threads=8] [profile=0|1]\n";
        return 2;
    }
    const auto directory = fs::canonical(arguments[1]);
    const auto method = arguments[2].u8string();
    const auto prefix = fs::absolute(arguments[3]).lexically_normal();
    const int repeat = arguments.size() > 4 ? positiveInteger(arguments[4].u8string(), 10000, "repeat") : 3;
    const int threads = arguments.size() > 5 ? positiveInteger(arguments[5].u8string(), 1024, "threads") : 8;
    const auto profile_argument = arguments.size() > 6 ? arguments[6].u8string() : "0";
    if (profile_argument != "0" && profile_argument != "1")
        throw std::invalid_argument("profile must be 0 or 1");
    const bool profile = profile_argument == "1";
    auto options = methodOptions(method);
    options->include_weight_maps = false;
    cv::setNumThreads(threads);
    cv::ocl::setUseOpenCL(false);
    const auto files = listImages(directory);
    fs::create_directories(prefix.parent_path());
    // 所有产物使用单独前缀；即使调用者误把前缀设在输入目录，也不覆盖任何源图。
    for (const auto* suffix : {".json", ".fused.png", ".fused.npy", ".indices.npy"}) {
        const auto output = suffixed(prefix, suffix);
        for (const auto& input : files)
            if (fs::exists(output) && fs::equivalent(output, input))
                throw std::invalid_argument("An output path would overwrite a source image");
    }
    std::vector<cv::Mat> images;
    std::vector<std::string> input_fingerprints;
    images.reserve(files.size());
    input_fingerprints.reserve(files.size());
    double decoded_bytes = 0;
    for (const auto& file : files) {
        images.push_back(readImage(file));
        const auto& image = images.back();
        if ((image.depth() != CV_8U && image.depth() != CV_16U && image.depth() != CV_32F) ||
            (image.channels() != 1 && image.channels() != 3) || image.dims != 2 ||
            image.size() != images.front().size() || image.type() != images.front().type())
            throw std::invalid_argument("Images must share dimensions, uint8/uint16/float32 depth and grayscale/BGR layout");
        decoded_bytes += static_cast<double>(image.total() * image.elemSize());
        input_fingerprints.push_back(fingerprint(image));
    }
    const mif::NoRegistrationOptions registration_options;
    const auto before_warmup = sampleMemory();
    double warmup_seconds = 0;
    {
        const auto begin = Clock::now();
        const auto warmup = mif::registerAndFuse(images, registration_options, *options);
        warmup_seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        if (warmup.fusion.image.empty()) throw std::runtime_error("Warmup returned an empty image");
    }
    const auto after_warmup = sampleMemory();
    std::vector<TimedRun> runs;
    runs.reserve(static_cast<std::size_t>(repeat));
    mif::PipelineResult last;
    for (int i = 0; i < repeat; ++i) {
        // 先释放上次结果，再测量下一次，避免多保留一幅结果导致峰值偏高。
        last = {};
        TimedRun current;
        current.before = sampleMemory();
        const auto begin = Clock::now();
        last = mif::registerAndFuse(images, registration_options, *options);
        current.seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        current.after = sampleMemory();
        runs.push_back(current);
    }
    // 在分阶段测量及文件写入前记录峰值，避免把额外测量和导出缓冲区计入核心处理峰值。
    const auto before_export = sampleMemory();
    // 分阶段计时另做三次带回调运行，不混入无回调的正式耗时和内存结果。
    // 每段时间归入上一个回调报告的阶段；同名阶段相加，包含该段内的回调记录开销。
    // 这些是公开阶段的估计值，不能代替内部滤波算子的独立计时。
    std::vector<std::map<std::string, double>> profiles;
    if (profile) {
        for (int run = 0; run < 3; ++run) {
            std::map<std::string, double> stages;
            std::string previous_stage = "entry";
            auto previous_time = Clock::now();
            const auto measured = mif::registerAndFuse(images, registration_options, *options,
                [&](int, const std::string& stage) {
                    const auto now = Clock::now();
                    stages[previous_stage] += std::chrono::duration<double>(now - previous_time).count();
                    previous_stage = stage;
                    previous_time = now;
                    return true;
                });
            stages[previous_stage] += std::chrono::duration<double>(Clock::now() - previous_time).count();
            if (cv::norm(measured.fusion.image, last.fusion.image, cv::NORM_INF) != 0 ||
                measured.fusion.source_index_map.empty() != last.fusion.source_index_map.empty() ||
                (!last.fusion.source_index_map.empty() &&
                 cv::norm(measured.fusion.source_index_map, last.fusion.source_index_map, cv::NORM_INF) != 0))
                throw std::runtime_error("Profiling callback changed the algorithm output");
            profiles.push_back(std::move(stages));
        }
    }
    for (std::size_t i = 0; i < images.size(); ++i)
        if (fingerprint(images[i]) != input_fingerprints[i])
            throw std::runtime_error("The algorithm modified a decoded input image");
    std::vector<double> seconds;
    for (const auto& run : runs) seconds.push_back(run.seconds);
    std::sort(seconds.begin(), seconds.end());
    const auto middle = seconds.size() / 2;
    const double median = seconds.size() % 2 ? seconds[middle] : (seconds[middle - 1] + seconds[middle]) / 2;

    writeNpy(suffixed(prefix, ".fused.npy"), last.fusion.image);
    if (!last.fusion.source_index_map.empty())
        writeNpy(suffixed(prefix, ".indices.npy"), last.fusion.source_index_map);
    cv::Mat png = last.fusion.image;
    if (png.depth() == CV_32F) last.fusion.image.convertTo(png, CV_16U, 65535.0);
    std::vector<unsigned char> encoded;
    if (!cv::imencode(".png", png, encoded)) throw std::runtime_error("Cannot encode the fused PNG");
    writeBytes(suffixed(prefix, ".fused.png"), encoded.data(), encoded.size());

    // JSON 先写到内存，再用 filesystem 路径落盘，避免 Windows 窄字符串路径的编码限制。
    cv::FileStorage json("benchmark.json", cv::FileStorage::WRITE | cv::FileStorage::MEMORY | cv::FileStorage::FORMAT_JSON);
    json << "schema_version" << 3 << "method" << method << "registration" << "none"
         << "text_encoding" << "Text fields longer than 512 UTF-8 bytes are arrays of strings; concatenate chunks without separators. opencv_build_information always uses chunks, preserving all original newlines."
         << "input_order" << "natural filename order; minus sign is a literal character"
         << "repeat" << repeat << "warmup_count" << 1 << "warmup_seconds" << warmup_seconds
         << "requested_threads" << threads << "opencv_threads" << cv::getNumThreads()
         << "opencv_cpu_count" << cv::getNumberOfCPUs() << "opencv_version" << CV_VERSION
         << "opencv_optimized" << static_cast<int>(cv::useOptimized())
         << "opencl_enabled" << static_cast<int>(cv::ocl::useOpenCL())
         << "project_version" << MIF_BENCHMARK_PROJECT_VERSION
         << "configuration" << MIF_BENCHMARK_CONFIGURATION;
    writeText(json, "input_directory", directory.u8string());
    writeText(json, "output_prefix", prefix.u8string());
    writeText(json, "core_library_path", coreLibraryPath());
    json << "opencv_build_information";
    writeTextChunks(json, cv::getBuildInformation());
    json << "timing_scope" << "registerAndFuse: no-registration stage, validation, normalization, fusion and output restoration; excludes decoding, hashing and export"
         << "memory_scope" << "process lifetime high-water working set, including decode and warmup, sampled before profiling and export; not an allocation count or per-call increment"
         << "decoded_input_bytes" << decoded_bytes << "input_count" << static_cast<int>(images.size());
    writeOptions(json, *options);
    writeMemory(json, "before_warmup", before_warmup);
    writeMemory(json, "after_warmup", after_warmup);
    writeMemory(json, "before_export", before_export);
    json << "inputs" << "[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& image = images[i];
        json << "{" << "index" << static_cast<int>(i);
        writeText(json, "file", files[i].u8string());
        json << "width" << image.cols << "height" << image.rows << "channels" << image.channels()
             << "depth" << depthName(image.depth()) << "opencv_type" << image.type()
             << "file_bytes" << static_cast<double>(fs::file_size(files[i]))
             << "decoded_fnv1a64" << input_fingerprints[i] << "}";
    }
    json << "]" << "runs" << "[";
    for (std::size_t i = 0; i < runs.size(); ++i) {
        json << "{" << "iteration" << static_cast<int>(i + 1) << "seconds" << runs[i].seconds;
        writeMemory(json, "before", runs[i].before);
        writeMemory(json, "after", runs[i].after);
        json << "}";
    }
    json << "]" << "profile_scope" << "Separate callback runs after timed runs; elapsed time assigned to the previous reported stage and includes callback bookkeeping; same stage names are summed"
         << "profile_runs" << "[";
    for (const auto& stages : profiles) {
        json << "{";
        for (const auto& stage : stages) json << stage.first << stage.second;
        json << "}";
    }
    json << "]" << "median_seconds" << median << "minimum_seconds" << seconds.front()
         << "maximum_seconds" << seconds.back() << "output" << "{"
         << "width" << last.fusion.image.cols << "height" << last.fusion.image.rows
         << "depth" << depthName(last.fusion.image.depth()) << "channels" << last.fusion.image.channels()
         << "png_representation" << (last.fusion.image.depth() == CV_32F ? "uint16 preview scaled by 65535; exact float32 pixels in NPY" : "lossless original depth")
         << "image_fnv1a64" << fingerprint(last.fusion.image)
         << "has_source_index_map" << static_cast<int>(!last.fusion.source_index_map.empty());
    writeText(json, "image_file", suffixed(prefix, ".fused.npy").u8string());
    writeText(json, "png_file", suffixed(prefix, ".fused.png").u8string());
    writeText(json, "source_index_map_file",
              last.fusion.source_index_map.empty() ? std::string() : suffixed(prefix, ".indices.npy").u8string());
    json << "}";
    const auto report = json.releaseAndGetString();
    writeBytes(suffixed(prefix, ".json"), reinterpret_cast<const unsigned char*>(report.data()), report.size());
    std::cout << method << ": " << images.size() << " images, " << images.front().cols << 'x' << images.front().rows
              << ", threads=" << cv::getNumThreads() << ", median=" << std::fixed << std::setprecision(6) << median
              << " s, peak=" << before_export.peak_working_set_bytes / (1024 * 1024) << " MiB\n";
    std::cout << "Report: " << suffixed(prefix, ".json").u8string() << '\n';
    return 0;
}

int guardedRun(const std::vector<fs::path>& arguments) {
    try { return run(arguments); }
    catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
} // 匿名命名空间

// Windows 使用宽字符入口，空格和中文路径均作为完整参数传递。
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<fs::path> arguments;
    for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
    return guardedRun(arguments);
}
