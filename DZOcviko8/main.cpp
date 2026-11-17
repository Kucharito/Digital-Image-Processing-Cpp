#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

constexpr int LEVELS = 256;
using Counts = std::array<std::uint64_t, LEVELS>;
using LookupTable = std::array<unsigned char, LEVELS>;

Counts histogram(const cv::Mat& image) {
    if (image.empty() || image.type() != CV_8UC1)
        throw std::invalid_argument("Expected a nonempty 8-bit grayscale image.");
    Counts counts{};
    for (int y = 0; y < image.rows; ++y) {
        const auto* row = image.ptr<unsigned char>(y);
        for (int x = 0; x < image.cols; ++x) ++counts[row[x]];
    }
    return counts;
}

Counts cumulativeDistribution(const Counts& counts) {
    Counts cdf{};
    std::partial_sum(counts.begin(), counts.end(), cdf.begin());
    return cdf;
}

LookupTable makeLookupTable(const Counts& cdf) {
    const auto first = std::find_if(cdf.begin(), cdf.end(), [](std::uint64_t n) { return n > 0; });
    if (first == cdf.end()) throw std::invalid_argument("Empty cumulative histogram.");
    const std::uint64_t total = cdf.back(), cdfMin = *first;
    LookupTable lut{};
    if (total == cdfMin) {
        // A constant image has a zero denominator; preserve it unchanged.
        std::iota(lut.begin(), lut.end(), 0);
        return lut;
    }
    for (int v = 0; v < LEVELS; ++v) {
        // Absent intensities below the first occupied bin must not underflow.
        if (cdf[v] < cdfMin) lut[v] = 0;
        else {
            const double mapped = static_cast<double>(cdf[v] - cdfMin)
                                / static_cast<double>(total - cdfMin) * (LEVELS - 1);
            lut[v] = static_cast<unsigned char>(std::clamp(std::lround(mapped), 0L, 255L));
        }
    }
    return lut;
}

cv::Mat applyLookupTable(const cv::Mat& image, const LookupTable& lut) {
    if (image.empty() || image.type() != CV_8UC1)
        throw std::invalid_argument("Expected a nonempty 8-bit grayscale image.");
    cv::Mat result(image.size(), image.type());
    for (int y = 0; y < image.rows; ++y) {
        const auto* inputRow = image.ptr<unsigned char>(y);
        auto* outputRow = result.ptr<unsigned char>(y);
        for (int x = 0; x < image.cols; ++x) outputRow[x] = lut[inputRow[x]];
    }
    return result;
}

struct Equalization {
    Counts inputHistogram, inputCdf, outputHistogram, outputCdf;
    LookupTable lut;
    cv::Mat image;
};

Equalization equalize(const cv::Mat& input) {
    Equalization result;
    result.inputHistogram = histogram(input);
    result.inputCdf = cumulativeDistribution(result.inputHistogram);
    result.lut = makeLookupTable(result.inputCdf);
    result.image = applyLookupTable(input, result.lut);
    result.outputHistogram = histogram(result.image);
    result.outputCdf = cumulativeDistribution(result.outputHistogram);
    return result;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Verification failed: " + message);
}

void verify(const cv::Mat& input, const Equalization& result) {
    const auto total = static_cast<std::uint64_t>(input.total());
    require(result.inputCdf.back() == total && result.outputCdf.back() == total,
            "CDF must end at the number of pixels");
    require(std::is_sorted(result.inputCdf.begin(), result.inputCdf.end()) &&
            std::is_sorted(result.outputCdf.begin(), result.outputCdf.end()) &&
            std::is_sorted(result.lut.begin(), result.lut.end()), "monotonic CDF and LUT");
    Counts expected{};
    for (int v = 0; v < LEVELS; ++v) expected[result.lut[v]] += result.inputHistogram[v];
    require(expected == result.outputHistogram, "histogram mass transferred by LUT");
}

void selfTest() {
    cv::Mat small(1, 4, CV_8UC1);
    const std::array<unsigned char, 4> originalValues{50, 50, 100, 150};
    std::copy(originalValues.begin(), originalValues.end(), small.ptr<unsigned char>(0));
    const auto result = equalize(small);
    require(result.inputHistogram[50] == 2 && result.inputHistogram[100] == 1 &&
            result.inputHistogram[150] == 1, "known histogram");
    require(result.inputCdf[49] == 0 && result.inputCdf[50] == 2 &&
            result.inputCdf[100] == 3 && result.inputCdf[150] == 4, "known cumulative sum");
    require(result.lut[0] == 0 && result.lut[50] == 0 && result.lut[100] == 128 &&
            result.lut[150] == 255, "CDF minimum subtraction and rounding of 127.5");
    const std::array<unsigned char, 4> expectedValues{0, 0, 128, 255};
    require(std::equal(expectedValues.begin(), expectedValues.end(), result.image.ptr<unsigned char>(0)),
            "known equalized pixels");
    verify(small, result);
    for (int value : {0, 73, 255}) {
        cv::Mat constant(3, 5, CV_8UC1, cv::Scalar(value));
        const auto unchanged = equalize(constant);
        require(cv::norm(constant, unchanged.image, cv::NORM_INF) == 0,
                "constant image preserved without division by zero");
        verify(constant, unchanged);
    }
    cv::Mat ramp(1, LEVELS, CV_8UC1);
    for (int v = 0; v < LEVELS; ++v) ramp.at<unsigned char>(0, v) = static_cast<unsigned char>(v);
    const auto uniform = equalize(ramp);
    require(cv::norm(ramp, uniform.image, cv::NORM_INF) == 0, "uniform histogram is unchanged");
    verify(ramp, uniform);
    cv::Mat pair(2, 1, CV_8UC1);
    pair.at<unsigned char>(0, 0) = 30;
    pair.at<unsigned char>(1, 0) = 200;
    const auto endpoints = equalize(pair);
    require(endpoints.image.at<unsigned char>(0, 0) == 0 &&
            endpoints.image.at<unsigned char>(1, 0) == 255, "two levels span full range");
    cv::Mat backing(9, 13, CV_8UC1);
    cv::RNG rng(42);
    rng.fill(backing, cv::RNG::UNIFORM, 40, 160);
    const cv::Mat roi = backing(cv::Rect(2, 1, 7, 5));
    require(!roi.isContinuous(), "test fixture has a row stride");
    const auto strided = equalize(roi);
    require(cv::norm(strided.image, equalize(roi.clone()).image, cv::NORM_INF) == 0,
            "non-contiguous input handled correctly");
    verify(roi, strided);
    const cv::Mat single(1, 1, CV_8UC1, cv::Scalar(128));
    require(equalize(single).image.at<unsigned char>(0, 0) == 128, "one pixel");
    bool rejected = false;
    try { histogram(cv::Mat()); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "reject empty input");
    std::cout << "Self-test OK: histogram, CDF, LUT, rounding, constants, uniform ramp, ROI.\n";
}

cv::Mat chart(const Counts& counts, std::uint64_t maximum, bool cdf,
              const std::string& title, const cv::Scalar& color) {
    const int width = 512, height = 220;
    const int left = 74, top = 42, plotWidth = 412, plotHeight = 139;
    const int bottom = top + plotHeight;
    cv::Mat canvas(height, width, CV_8UC3, cv::Scalar(250, 250, 250));
    cv::putText(canvas, title, {14, 26}, cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(30, 30, 30), 1, cv::LINE_AA);
    for (int tick = 0; tick <= 2; ++tick) {
        const int y = bottom - tick * plotHeight / 2;
        cv::line(canvas, {left, y}, {left + plotWidth, y}, cv::Scalar(222, 222, 222));
        const auto value = static_cast<std::uint64_t>(maximum * (tick / 2.0));
        cv::putText(canvas, std::to_string(value), {4, y + 4}, cv::FONT_HERSHEY_SIMPLEX,
                    0.35, cv::Scalar(70, 70, 70), 1, cv::LINE_AA);
    }
    cv::line(canvas, {left, top}, {left, bottom}, cv::Scalar(80, 80, 80));
    cv::line(canvas, {left, bottom}, {left + plotWidth, bottom}, cv::Scalar(80, 80, 80));
    cv::Point previous;
    for (int v = 0; v < LEVELS; ++v) {
        const int x = left + static_cast<int>(std::lround(v * plotWidth / 255.0));
        const int y = bottom - static_cast<int>(std::lround(
            static_cast<double>(counts[v]) / std::max<std::uint64_t>(maximum, 1) * plotHeight));
        if (cdf) {
            if (v > 0) cv::line(canvas, previous, {x, previous.y}, color, 2, cv::LINE_AA);
            cv::line(canvas, {x, v > 0 ? previous.y : bottom}, {x, y}, color, 2, cv::LINE_AA);
        } else cv::line(canvas, {x, bottom}, {x, y}, color, 1, cv::LINE_AA);
        previous = {x, y};
    }
    for (int tick : {0, 64, 128, 192, 255})
        cv::putText(canvas, std::to_string(tick), {left + tick * plotWidth / 255 - 10, bottom + 18},
                    cv::FONT_HERSHEY_SIMPLEX, 0.35, cv::Scalar(70, 70, 70), 1, cv::LINE_AA);
    cv::putText(canvas, cdf ? "Intensity / cumulative pixel count" : "Intensity / pixel count",
                {left, height - 6}, cv::FONT_HERSHEY_SIMPLEX, 0.38,
                cv::Scalar(70, 70, 70), 1, cv::LINE_AA);
    return canvas;
}

cv::Mat imagePanel(const cv::Mat& image, const std::string& title) {
    cv::Mat panel(424, 512, CV_8UC3, cv::Scalar(250, 250, 250));
    cv::putText(panel, title, {14, 27}, cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(30, 30, 30), 1, cv::LINE_AA);
    const double scale = std::min(512.0 / image.cols, 384.0 / image.rows);
    const cv::Size size(std::max(1, static_cast<int>(std::lround(image.cols * scale))),
                        std::max(1, static_cast<int>(std::lround(image.rows * scale))));
    cv::Mat preview, color;
    cv::resize(image, preview, size, 0, 0, scale < 1 ? cv::INTER_AREA : cv::INTER_NEAREST);
    cv::cvtColor(preview, color, cv::COLOR_GRAY2BGR);
    color.copyTo(panel(cv::Rect((512 - size.width) / 2, 40 + (384 - size.height) / 2,
                               size.width, size.height)));
    return panel;
}

void save(const std::filesystem::path& path, const cv::Mat& image) {
    if (!cv::imwrite(path.string(), image)) throw std::runtime_error("Cannot save " + path.string());
}

std::string statistics(const cv::Mat& image) {
    double minimum, maximum;
    cv::minMaxLoc(image, &minimum, &maximum);
    cv::Scalar mean, deviation;
    cv::meanStdDev(image, mean, deviation);
    return "min=" + std::to_string(static_cast<int>(minimum)) +
           ", max=" + std::to_string(static_cast<int>(maximum)) +
           ", mean=" + std::to_string(mean[0]) + ", stddev=" + std::to_string(deviation[0]);
}

int main(int argc, char** argv) {
    try {
        std::string inputPath;
        std::filesystem::path outputDirectory = ".";
        bool show = true, onlyTest = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--no-show") show = false;
            else if (arg == "--self-test") onlyTest = true;
            else if (arg == "--help") {
                std::cout << "Usage: dzo_histogram.exe [image] [--no-show] [--output directory]\n"
                          << "       dzo_histogram.exe --self-test\n"
                          << "Default: low-contrast demonstration derived from images/lena_gray.png.\n";
                return 0;
            } else if (arg == "--output") {
                if (++i == argc) throw std::invalid_argument("Missing output directory.");
                outputDirectory = argv[i];
            } else if (arg.rfind("--", 0) == 0 || !inputPath.empty())
                throw std::invalid_argument("Unexpected argument: " + arg);
            else inputPath = arg;
        }
        selfTest();
        if (onlyTest) return 0;
        cv::Mat input;
        if (inputPath.empty()) {
            const cv::Mat base = cv::imread("images/lena_gray.png", cv::IMREAD_GRAYSCALE);
            if (base.empty()) throw std::runtime_error("Cannot read images/lena_gray.png");
            input = cv::Mat(base.size(), CV_8UC1);
            for (int y = 0; y < input.rows; ++y)
                for (int x = 0; x < input.cols; ++x)
                    input.at<unsigned char>(y, x) = static_cast<unsigned char>(
                        90 + std::lround(base.at<unsigned char>(y, x) * 70.0 / 255.0));
            save("images/lena_low_contrast.png", input);
            std::cout << "Low-contrast Lena demonstration (90 + round(70*v/255)).\n";
        } else {
            input = cv::imread(inputPath, cv::IMREAD_GRAYSCALE);
            if (input.empty()) throw std::runtime_error("Cannot read image: " + inputPath);
        }
        const auto result = equalize(input);
        verify(input, result);
        std::filesystem::create_directories(outputDirectory);
        save(outputDirectory / "image.png", input);
        save(outputDirectory / "equalized.png", result.image);
        const auto histogramMax = std::max(*std::max_element(result.inputHistogram.begin(), result.inputHistogram.end()),
                                          *std::max_element(result.outputHistogram.begin(), result.outputHistogram.end()));
        const cv::Scalar blue(170, 100, 35), orange(35, 125, 220);
        const auto histogramInput = chart(result.inputHistogram, histogramMax, false, "Input histogram", blue);
        const auto histogramOutput = chart(result.outputHistogram, histogramMax, false, "Equalized histogram", orange);
        const auto cdfInput = chart(result.inputCdf, result.inputCdf.back(), true, "Input CDF", blue);
        const auto cdfOutput = chart(result.outputCdf, result.outputCdf.back(), true, "Equalized CDF", orange);
        save(outputDirectory / "histogram_input.png", histogramInput);
        save(outputDirectory / "histogram_equalized.png", histogramOutput);
        save(outputDirectory / "cdf_input.png", cdfInput);
        save(outputDirectory / "cdf_equalized.png", cdfOutput);
        cv::Mat top, middle, bottom, overview;
        cv::hconcat(imagePanel(input, "Original image"), imagePanel(result.image, "Histogram equalization"), top);
        cv::hconcat(histogramInput, histogramOutput, middle);
        cv::hconcat(cdfInput, cdfOutput, bottom);
        cv::vconcat(top, middle, overview);
        cv::vconcat(overview, bottom, overview);
        save(outputDirectory / "result_images.png", overview);
        std::ofstream csv(outputDirectory / "histogram.csv");
        if (!csv) throw std::runtime_error("Cannot write histogram.csv");
        csv << "intensity,input_count,input_cdf,lut,output_count,output_cdf\n";
        for (int v = 0; v < LEVELS; ++v)
            csv << v << ',' << result.inputHistogram[v] << ',' << result.inputCdf[v] << ','
                << static_cast<int>(result.lut[v]) << ',' << result.outputHistogram[v] << ','
                << result.outputCdf[v] << '\n';
        const auto first = std::find_if(result.inputCdf.begin(), result.inputCdf.end(),
                                        [](std::uint64_t n) { return n > 0; });
        const std::string report = "Size: " + std::to_string(input.cols) + "x" + std::to_string(input.rows) +
            "\nPixels: " + std::to_string(input.total()) + "\ncdf_min: " + std::to_string(*first) +
            "\nInput: " + statistics(input) + "\nEqualized: " + statistics(result.image) + "\n";
        std::ofstream metrics(outputDirectory / "metrics.txt");
        if (!metrics) throw std::runtime_error("Cannot write metrics.txt");
        metrics << report;
        std::cout << report << "Saved images, histogram.csv and metrics.txt to " << outputDirectory.string() << '\n';
        if (show) {
            cv::namedWindow("DZO 8 - Histogram equalization", cv::WINDOW_NORMAL);
            cv::imshow("DZO 8 - Histogram equalization", overview);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
