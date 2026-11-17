#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Complex = std::complex<double>;
constexpr double PI = 3.14159265358979323846;
struct Matrix {
    int rows, cols;
    std::vector<Complex> data;
    Matrix(int r, int c) : rows(r), cols(c), data(static_cast<size_t>(r) * c) {}
    Complex& operator()(int y, int x) { return data[y * cols + x]; }
    const Complex& operator()(int y, int x) const { return data[y * cols + x]; }
};

// Vlastna separabilna DFT/IDFT. Tabulka exponentov odstrani opakovanu trigonometriu.
// Unitarna normalizacia 1/sqrt(M*N) plati v oboch smeroch.
Matrix transform(const Matrix& input, int sign) {
    auto table = [sign](int n) {
        std::vector<Complex> values(static_cast<size_t>(n) * n);
        for (int k = 0; k < n; ++k)
            for (int j = 0; j < n; ++j)
                values[k * n + j] = std::polar(1.0 / std::sqrt(n), sign * 2 * PI * k * j / n);
        return values;
    };
    const auto wx = table(input.cols), wy = table(input.rows);
    Matrix tmp(input.rows, input.cols), output(input.rows, input.cols);
    for (int y = 0; y < input.rows; ++y)
        for (int k = 0; k < input.cols; ++k)
            for (int x = 0; x < input.cols; ++x)
                tmp(y, k) += input(y, x) * wx[k * input.cols + x];
    for (int x = 0; x < input.cols; ++x)
        for (int k = 0; k < input.rows; ++k)
            for (int y = 0; y < input.rows; ++y)
                output(k, x) += tmp(y, x) * wy[k * input.rows + y];
    return output;
}

// Na neparnych rozmeroch nie je fftshift sam sebe inverziou.
Matrix shift(const Matrix& input, bool inverse = false) {
    Matrix output(input.rows, input.cols);
    const int dy = inverse ? (input.rows + 1) / 2 : input.rows / 2;
    const int dx = inverse ? (input.cols + 1) / 2 : input.cols / 2;
    for (int y = 0; y < input.rows; ++y)
        for (int x = 0; x < input.cols; ++x)
            output((y + dy) % input.rows, (x + dx) % input.cols) = input(y, x);
    return output;
}

Matrix fromImage(const cv::Mat& image) {
    Matrix result(image.rows, image.cols);
    for (int y = 0; y < image.rows; ++y)
        for (int x = 0; x < image.cols; ++x) result(y, x) = image.at<double>(y, x);
    return result;
}
cv::Mat realImage(const Matrix& matrix) {
    cv::Mat image(matrix.rows, matrix.cols, CV_64F);
    for (int y = 0; y < matrix.rows; ++y)
        for (int x = 0; x < matrix.cols; ++x) image.at<double>(y, x) = matrix(y, x).real();
    return image;
}
cv::Mat circleMask(int rows, int cols, double radius, bool high = false) {
    cv::Mat mask(rows, cols, CV_64F);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            const double d = std::hypot(y - rows / 2, x - cols / 2);
            mask.at<double>(y, x) = (d <= radius) != high ? 1.0 : 0.0;
        }
    return mask;
}
Matrix applyMask(Matrix spectrum, const cv::Mat& mask) {
    for (int y = 0; y < spectrum.rows; ++y)
        for (int x = 0; x < spectrum.cols; ++x) spectrum(y, x) *= mask.at<double>(y, x);
    return spectrum;
}
cv::Mat reconstruct(const Matrix& centered, const cv::Mat& mask) {
    return realImage(transform(shift(applyMask(centered, mask), true), +1));
}

// Zvisle pruhy su konstantne v smere y: spicky lezia na riadku v=0.
// Porovnanie so susednymi riadkami odlisuje uzke spicky od obsahu fotografie.
std::vector<int> detectFrequencies(const Matrix& s) {
    const int cy = s.rows / 2, cx = s.cols / 2;
    double maximum = 0;
    for (int k = 7; k <= (s.cols - 1) / 2; ++k) maximum = std::max(maximum, std::abs(s(cy, cx + k)));
    std::vector<int> result;
    for (int k = 7; k <= (s.cols - 1) / 2; ++k) {
        const int x = cx + k;
        const double a = std::abs(s(cy, x));
        double background = 0;
        for (int dy : {-2, -1, 1, 2}) background += std::abs(s((cy + dy + s.rows) % s.rows, x));
        background /= 4;
        if (a > 8 * std::max(1.0, background) && a > 0.05 * maximum &&
            a > std::abs(s(cy, x - 1)) && (x + 1 == s.cols || a > std::abs(s(cy, x + 1))))
            result.push_back(k);
    }
    return result;
}
cv::Mat notchMask(int rows, int cols, const std::vector<int>& frequencies, int radius) {
    cv::Mat mask(rows, cols, CV_64F, cv::Scalar(1));
    const int cy = rows / 2, cx = cols / 2;
    for (int frequency : frequencies)
        for (int sign : {-1, 1}) {
            const int px = (cx + sign * frequency + cols) % cols;
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < cols; ++x) {
                    int dx = std::abs(x - px), dy = std::abs(y - cy);
                    dx = std::min(dx, cols - dx); dy = std::min(dy, rows - dy);
                    if (std::hypot(dx, dy) <= radius) mask.at<double>(y, x) = 0;
                }
        }
    mask.at<double>(cy, cx) = 1; // zachovanie DC, teda priemerneho jasu
    return mask;
}
cv::Mat bytes(const cv::Mat& image) {
    cv::Mat output; image.convertTo(output, CV_8U); return output;
}
cv::Mat powerImage(const Matrix& s) {
    cv::Mat image(s.rows, s.cols, CV_64F);
    for (int y = 0; y < s.rows; ++y)
        for (int x = 0; x < s.cols; ++x) image.at<double>(y, x) = std::log1p(std::norm(s(y, x)));
    cv::normalize(image, image, 0, 255, cv::NORM_MINMAX);
    return bytes(image);
}
void save(const std::string& name, const cv::Mat& image) {
    if (!cv::imwrite("output/" + name + ".png", bytes(image))) throw std::runtime_error("Cannot save " + name);
}
double mse(const cv::Mat& a, const cv::Mat& b) { return cv::norm(a, b, cv::NORM_L2SQR) / a.total(); }
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error("Test failed: " + message); }
void selfTest() {
    for (const cv::Size size : {cv::Size(8, 6), cv::Size(9, 7)}) {
        cv::Mat input(size, CV_64F); cv::RNG rng(42); rng.fill(input, cv::RNG::UNIFORM, 0, 255);
        const Matrix original = fromImage(input), s = shift(transform(original, -1));
        const Matrix back = shift(shift(original), true);
        require(back.data == original.data, "odd/even shift inversion");
        require(cv::norm(realImage(transform(shift(s, true), +1)), input, cv::NORM_INF) < 1e-9, "DFT round trip");
        cv::Mat lp = circleMask(size.height, size.width, 2), hp = circleMask(size.height, size.width, 2, true);
        require(cv::norm(reconstruct(s, lp) + reconstruct(s, hp), input, cv::NORM_INF) < 1e-9, "LP + HP = input");
    }
    cv::Mat stripes(31, 64, CV_64F);
    for (int y = 0; y < stripes.rows; ++y)
        for (int x = 0; x < stripes.cols; ++x) stripes.at<double>(y, x) = 100 + 30 * std::cos(2 * PI * 12 * x / stripes.cols);
    const Matrix s = shift(transform(fromImage(stripes), -1));
    const auto peaks = detectFrequencies(s);
    require(peaks == std::vector<int>{12}, "stripe frequency detection");
    const Matrix filtered = transform(shift(applyMask(s, notchMask(31, 64, peaks, 0)), true), +1);
    for (const auto& value : filtered.data) {
        require(std::abs(value.real() - 100) < 1e-9, "stripe removal / DC preservation");
        require(std::abs(value.imag()) < 1e-9, "conjugate symmetry");
    }
    std::cout << "Self-test: all checks passed.\n";
}

cv::Mat overview(const std::vector<std::pair<std::string, cv::Mat>>& panels, int columns) {
    const int tile = 256, header = 34, rows = (static_cast<int>(panels.size()) + columns - 1) / columns;
    cv::Mat canvas(rows * (tile + header), columns * tile, CV_8UC3, cv::Scalar(25, 25, 25));
    for (size_t i = 0; i < panels.size(); ++i) {
        const int x = (i % columns) * tile, y = (i / columns) * (tile + header);
        cv::Mat small, color;
        cv::resize(bytes(panels[i].second), small, cv::Size(tile, tile)); cv::cvtColor(small, color, cv::COLOR_GRAY2BGR);
        color.copyTo(canvas(cv::Rect(x, y + header, tile, tile)));
        cv::putText(canvas, panels[i].first, cv::Point(x + 8, y + 23), cv::FONT_HERSHEY_SIMPLEX, 0.48, cv::Scalar(240, 240, 240), 1, cv::LINE_AA);
    }
    return canvas;
}

int main(int argc, char** argv) {
    try {
        bool show = true, testOnly = false;
        int size = 256, notchRadius = 0;
        std::string path = "images/lena_gray.png", barsPath;
        std::vector<int> manualFrequencies;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() { if (++i >= argc) throw std::runtime_error("Missing value for " + arg); return std::string(argv[i]); };
            auto integer = [&]() { const std::string v = value(); size_t n; int result = std::stoi(v, &n); if (n != v.size()) throw std::runtime_error("Invalid integer: " + v); return result; };
            if (arg == "--no-show") show = false;
            else if (arg == "--self-test") testOnly = true;
            else if (arg == "--size") size = integer();
            else if (arg == "--input") path = value();
            else if (arg == "--bars-input") barsPath = value();
            else if (arg == "--frequency") manualFrequencies.push_back(integer());
            else if (arg == "--notch-radius") notchRadius = integer();
            else if (arg == "--help") {
                std::cout << "run.cmd [--no-show] [--self-test] [--size 64..512] [--input clean.png]\n"
                             "        [--bars-input bars.png] [--frequency k (repeatable)] [--notch-radius 0..5]\n"; return 0;
            } else throw std::runtime_error("Unknown argument: " + arg);
        }
        if (size < 64 || size > 512 || notchRadius < 0 || notchRadius > 5) throw std::runtime_error("Invalid size or notch radius.");
        selfTest(); if (testOnly) return 0;
        auto load = [size](const std::string& name) {
            cv::Mat image = cv::imread(name, cv::IMREAD_GRAYSCALE);
            if (image.empty()) throw std::runtime_error("Cannot read " + name);
            cv::resize(image, image, cv::Size(size, size), 0, 0, cv::INTER_AREA);
            image.convertTo(image, CV_64F); return image;
        };
        const cv::Mat clean = load(path);
        cv::Mat noise(clean.size(), CV_64F); cv::RNG rng(2026); rng.fill(noise, cv::RNG::NORMAL, 0, 30);
        const cv::Mat noisy = clean + noise; // bez clippingu pocas vypoctu
        const Matrix spectrum = shift(transform(fromImage(noisy), -1));
        std::filesystem::create_directories("output");
        save("clean", clean); save("noisy", noisy); save("noise_spectrum", powerImage(spectrum));
        std::ofstream metrics("output/metrics.csv");
        if (!metrics) throw std::runtime_error("Cannot write metrics.csv");
        metrics << "image,mse,psnr_db\n";
        auto metric = [&](const std::string& name, const cv::Mat& image) {
            const double error = mse(clean, image);
            metrics << name << ',' << std::setprecision(10) << error << ',' << 10 * std::log10(255 * 255 / error) << '\n';
            std::cout << name << ": MSE=" << error << ", PSNR=" << 10 * std::log10(255 * 255 / error) << " dB\n";
        };
        metric("noisy", noisy);
        std::vector<std::pair<std::string, cv::Mat>> panels{{"Original", clean}, {"Noise (sigma=30)", noisy}, {"Centered log power", powerImage(spectrum)}};
        for (int radius : {size / 16, size / 8, size / 4}) {
            const std::string suffix = std::to_string(radius);
            const cv::Mat mask = circleMask(size, size, radius), filtered = reconstruct(spectrum, mask);
            save("low_pass_r" + suffix, filtered); save("mask_r" + suffix, mask * 255);
            metric("low_pass_r" + suffix, filtered);
            panels.emplace_back("Low pass, diameter " + std::to_string(2 * radius), filtered);
        }
        const cv::Mat hpMask = circleMask(size, size, size / 8, true), hp = reconstruct(spectrum, hpMask);
        // High-pass ma zaporne hodnoty: pre zobrazenie posun nuly na sivu 128.
        save("high_pass", hp + 128); save("high_pass_mask", hpMask * 255);
        cv::FileStorage raw("output/high_pass.yml.gz", cv::FileStorage::WRITE);
        if (!raw.isOpened()) throw std::runtime_error("Cannot write high-pass data");
        raw << "high_pass_signed" << hp; raw.release();
        panels.emplace_back("Low-pass mask (middle)", circleMask(size, size, size / 8) * 255);
        panels.emplace_back("High pass (+128 display)", hp + 128);
        panels.emplace_back("High-pass mask", hpMask * 255);
        const cv::Mat noiseOverview = overview(panels, 3); save("noise_comparison", noiseOverview);

        cv::Mat bars = barsPath.empty() ? clean.clone() : load(barsPath);
        const int fundamental = std::max(8, size / 12);
        if (barsPath.empty())
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    bars.at<double>(y, x) += 40 * std::cos(2 * PI * fundamental * x / size) + 15 * std::cos(2 * PI * 2 * fundamental * x / size);
        const Matrix barsSpectrum = shift(transform(fromImage(bars), -1));
        const auto frequencies = manualFrequencies.empty() ? detectFrequencies(barsSpectrum) : manualFrequencies;
        for (int k : frequencies) if (k <= 0 || k > size / 2) throw std::runtime_error("Frequency must be in 1..size/2.");
        std::cout << "Removed horizontal frequency offsets:";
        for (int k : frequencies) std::cout << " +/-" << k;
        std::cout << '\n';
        if (frequencies.empty()) std::cout << "No clear peaks detected; use --frequency k after inspecting the spectrum.\n";
        const cv::Mat mask = notchMask(size, size, frequencies, notchRadius);
        const Matrix masked = applyMask(barsSpectrum, mask);
        const cv::Mat restored = realImage(transform(shift(masked, true), +1));
        save("bars_input", bars); save("bars_spectrum", powerImage(barsSpectrum)); save("notch_mask", mask * 255);
        save("bars_filtered_spectrum", powerImage(masked)); save("bars_removed", restored);
        if (barsPath.empty()) { metric("bars_input", bars); metric("bars_removed", restored); }
        std::ofstream peaks("output/frequencies.txt");
        if (!peaks) throw std::runtime_error("Cannot write frequencies.txt");
        for (int k : frequencies) peaks << k << '\n';
        const cv::Mat barsOverview = overview({{"Vertical bars input", bars}, {"Centered log power", powerImage(barsSpectrum)},
            {"Symmetric notch mask", mask * 255}, {"Bars removed", restored}, {"Filtered log power", powerImage(masked)},
            {barsPath.empty() ? "Clean reference" : "Removed component (+128)", barsPath.empty() ? clean : cv::Mat(bars - restored + 128)}}, 3);
        save("bars_comparison", barsOverview);
        std::cout << "Results saved in output/.\n";
        if (show) {
            cv::imshow("Frequency filtering - noise", noiseOverview); cv::imshow("Frequency filtering - bars", barsOverview);
            cv::waitKey(0); cv::destroyAllWindows();
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
