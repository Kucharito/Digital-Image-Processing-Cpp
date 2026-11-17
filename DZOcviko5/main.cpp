#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Complex = std::complex<double>;
const double PI = std::acos(-1.0);

struct Matrix {
    int rows, cols;
    std::vector<Complex> data;
    Matrix(int r, int c) : rows(r), cols(c), data(r * c) {}
    Complex& operator()(int r, int c) { return data[r * cols + c]; }
    const Complex& operator()(int r, int c) const { return data[r * cols + c]; }
};

// Unitary normalization in BOTH directions; this is a sum, not an FFT.
std::vector<Complex> transform1D(const std::vector<Complex>& input, int sign) {
    if (input.empty() || (sign != -1 && sign != 1))
        throw std::invalid_argument("Invalid 1D transform input/direction.");
    const int length = static_cast<int>(input.size());
    std::vector<Complex> output(length);
    for (int q = 0; q < length; ++q) {
        Complex sum{};
        for (int p = 0; p < length; ++p) {
            const double angle = sign * 2.0 * PI * p * q / length;
            sum += input[p] * Complex(std::cos(angle), std::sin(angle));
        }
        output[q] = sum / std::sqrt(static_cast<double>(length));
    }
    return output;
}

Matrix transformSeparable(const Matrix& input, int sign) {
    Matrix intermediate(input.rows, input.cols), output(input.rows, input.cols);
    for (int r = 0; r < input.rows; ++r) {
        std::vector<Complex> row(input.cols);
        for (int c = 0; c < input.cols; ++c) row[c] = input(r, c);
        const auto transformed = transform1D(row, sign);
        for (int c = 0; c < input.cols; ++c) intermediate(r, c) = transformed[c];
    }
    for (int c = 0; c < input.cols; ++c) {
        std::vector<Complex> column(input.rows);
        for (int r = 0; r < input.rows; ++r) column[r] = intermediate(r, c);
        const auto transformed = transform1D(column, sign);
        for (int r = 0; r < input.rows; ++r) output(r, c) = transformed[r];
    }
    return output;
}

Matrix idftDirect(const Matrix& spectrum) {
    const int M = spectrum.rows, N = spectrum.cols;
    Matrix output(M, N);
    const double scale = 1.0 / std::sqrt(static_cast<double>(M) * N);
    for (int m = 0; m < M; ++m)
        for (int n = 0; n < N; ++n) {
            Complex sum{};
            for (int k = 0; k < M; ++k)
                for (int l = 0; l < N; ++l) {
                    const double angle = 2.0 * PI *
                        (static_cast<double>(m) * k / M + static_cast<double>(n) * l / N);
                    sum += spectrum(k, l) * Complex(std::cos(angle), std::sin(angle));
                }
            output(m, n) = sum * scale;
        }
    return output;
}

// fftshift moves toward increasing indices; ifftshift reverses that move.
// Using two forward shifts would be incorrect for odd dimensions.
Matrix shift(const Matrix& input, bool inverse) {
    Matrix output(input.rows, input.cols);
    const int dr = inverse ? input.rows - input.rows / 2 : input.rows / 2;
    const int dc = inverse ? input.cols - input.cols / 2 : input.cols / 2;
    for (int r = 0; r < input.rows; ++r)
        for (int c = 0; c < input.cols; ++c)
            output((r + dr) % input.rows, (c + dc) % input.cols) = input(r, c);
    return output;
}

double difference(const Matrix& a, const Matrix& b) {
    if (a.rows != b.rows || a.cols != b.cols) throw std::runtime_error("Shape mismatch.");
    double error = 0;
    for (std::size_t i = 0; i < a.data.size(); ++i)
        error = std::max(error, std::abs(a.data[i] - b.data[i]));
    return error;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Verification failed: " + message);
}

void report(const std::string& name, const Matrix& input, const Matrix& output) {
    double error = 0, imaginary = 0;
    for (std::size_t i = 0; i < input.data.size(); ++i) {
        require(std::isfinite(output.data[i].real()) && std::isfinite(output.data[i].imag()), name);
        error = std::max(error, std::abs(output.data[i].real() - input.data[i].real()));
        imaginary = std::max(imaginary, std::abs(output.data[i].imag()));
    }
    std::cout << name << ": E_max=" << std::scientific << error
              << ", I_max=" << imaginary << std::defaultfloat << '\n';
    require(error < 1e-7 && imaginary < 1e-7, name);
}

void selfTest() {
    for (const auto shape : {cv::Size(5, 3), cv::Size(4, 6), cv::Size(1, 1),
                            cv::Size(7, 1), cv::Size(1, 5)}) {
        for (bool constant : {false, true}) {
            Matrix input(shape.height, shape.width);
            for (std::size_t i = 0; i < input.data.size(); ++i)
                input.data[i] = constant ? 37.0 : static_cast<double>((i * 43 + 17) % 251) - 100.0;
            const auto spectrum = transformSeparable(input, -1);
            const auto direct = idftDirect(spectrum);
            const auto separable = transformSeparable(spectrum, +1);
            const std::string name = std::to_string(input.rows) + "x" + std::to_string(input.cols)
                                     + (constant ? " constant" : " nonconstant (signed)");
            report(name + " direct", input, direct);
            report(name + " separable", input, separable);
            require(difference(direct, separable) < 1e-7, name + " inverse agreement");
            const auto restored = shift(shift(spectrum, false), true);
            require(difference(spectrum, restored) == 0, name + " shift permutation");
            report(name + " shifted round trip", input, transformSeparable(restored, +1));
            if (constant) {
                require(std::abs(spectrum(0, 0) - 37.0 * std::sqrt(input.rows * input.cols)) < 1e-7,
                        "constant DC coefficient");
                for (std::size_t i = 1; i < spectrum.data.size(); ++i)
                    require(std::abs(spectrum.data[i]) < 1e-7, "constant AC coefficient");
            }
        }
    }
    // An arbitrary complex spectrum tests imaginary arithmetic independently of real images.
    Matrix complexInput(3, 5);
    for (std::size_t i = 0; i < complexInput.data.size(); ++i)
        complexInput.data[i] = Complex(static_cast<double>(i) - 7, 2.0 * i - 3);
    const auto spectrum = transformSeparable(complexInput, -1);
    require(difference(idftDirect(spectrum), complexInput) < 1e-7, "complex direct round trip");
    require(difference(transformSeparable(spectrum, +1), complexInput) < 1e-7, "complex round trip");
    std::cout << "All self-tests passed (tolerance 1e-7).\n";
}

Matrix fromImage(const cv::Mat& gray) {
    Matrix output(gray.rows, gray.cols);
    for (int r = 0; r < gray.rows; ++r)
        for (int c = 0; c < gray.cols; ++c) output(r, c) = gray.at<uchar>(r, c);
    return output;
}

cv::Mat toImage(const Matrix& input) {
    cv::Mat output(input.rows, input.cols, CV_8UC1);
    for (int r = 0; r < input.rows; ++r)
        for (int c = 0; c < input.cols; ++c)
            output.at<uchar>(r, c) = static_cast<uchar>(std::clamp(std::round(input(r, c).real()), 0.0, 255.0));
    return output;
}

template <typename Function>
Matrix timed(Function function, double& milliseconds) {
    const auto start = std::chrono::steady_clock::now();
    Matrix result = function();
    milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}

void benchmark(const cv::Mat& original) {
    std::ofstream csv("benchmark.csv");
    if (!csv) throw std::runtime_error("Cannot create benchmark.csv");
    csv << "size,direct_ms,separable_ms,direct_growth,separable_growth,max_complex_difference\n";
    std::cout << "\nIDFT benchmark (median of 3 runs; conversion and DFT excluded)\n"
              << "N    direct_ms    separable_ms    direct_growth    separable_growth\n";
    double previousD = 0, previousS = 0;
    for (int n : {8, 16, 32, 64}) {
        cv::Mat gray;
        cv::resize(original, gray, cv::Size(n, n), 0, 0, cv::INTER_AREA);
        const auto input = fromImage(gray);
        const auto spectrum = transformSeparable(input, -1);
        std::vector<double> directTimes, separableTimes;
        double maxDifference = 0;
        for (int repeat = 0; repeat < 3; ++repeat) {
            double d, s;
            const auto direct = timed([&] { return idftDirect(spectrum); }, d);
            const auto separable = timed([&] { return transformSeparable(spectrum, +1); }, s);
            maxDifference = std::max(maxDifference, difference(direct, separable));
            directTimes.push_back(d); separableTimes.push_back(s);
        }
        require(maxDifference < 1e-7, "benchmark inverse agreement");
        std::sort(directTimes.begin(), directTimes.end());
        std::sort(separableTimes.begin(), separableTimes.end());
        const double d = directTimes[1], s = separableTimes[1];
        const double dg = previousD ? d / previousD : 0, sg = previousS ? s / previousS : 0;
        std::cout << n << "    " << d << "    " << s << "    " << dg << "    " << sg << '\n';
        csv << n << ',' << d << ',' << s << ',' << dg << ',' << sg << ',' << maxDifference << '\n';
        previousD = d; previousS = s;
    }
    std::cout << "Expected growth when N doubles: direct 16x (O(N^4)), separable 8x (O(N^3)).\n";
}

void save(const std::string& path, const cv::Mat& image) {
    if (!cv::imwrite(path, image)) throw std::runtime_error("Cannot save " + path);
}

int main(int argc, char** argv) {
    try {
        bool show = true, testsOnly = false, measure = true, hasPath = false;
        std::string path = "images/lena_gray.png";
        int size = 64;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--no-show") show = false;
            else if (arg == "--self-test") testsOnly = true;
            else if (arg == "--no-benchmark") measure = false;
            else if (arg == "--size" && i + 1 < argc) {
                const std::string value = argv[++i];
                std::size_t consumed = 0;
                size = std::stoi(value, &consumed);
                if (consumed != value.size() || size < 1 || size > 128)
                    throw std::invalid_argument("--size must be 1..128 (direct IDFT is O(N^4)).");
            } else if (arg == "--help") {
                std::cout << "Usage: run.cmd [image] [--size 1..128] [--no-show] [--no-benchmark] [--self-test]\n";
                return 0;
            } else if (!hasPath && !arg.empty() && arg[0] != '-') { path = arg; hasPath = true; }
            else throw std::invalid_argument("Unknown or incomplete argument: " + arg);
        }
        selfTest();
        if (testsOnly) return 0;
        const cv::Mat original = cv::imread(path, cv::IMREAD_GRAYSCALE);
        if (original.empty()) throw std::runtime_error("Cannot load image: " + path);
        cv::Mat gray;
        cv::resize(original, gray, cv::Size(size, size), 0, 0, cv::INTER_AREA);
        const auto input = fromImage(gray);
        const auto spectrum = transformSeparable(input, -1);
        double directTime, separableTime;
        const auto direct = timed([&] { return idftDirect(spectrum); }, directTime);
        const auto separable = timed([&] { return transformSeparable(spectrum, +1); }, separableTime);
        std::cout << "\nImage: " << path << " (" << size << "x" << size << ")\n";
        report("Direct IDFT", input, direct);
        report("Separable IDFT", input, separable);
        report("Centered/uncentered IDFT", input, transformSeparable(shift(shift(spectrum, false), true), +1));
        const double delta = difference(direct, separable);
        std::cout << "Max |direct - separable| = " << std::scientific << delta << std::defaultfloat
                  << "\nDirect: " << directTime << " ms; separable: " << separableTime << " ms\n";
        require(delta < 1e-7, "image inverse agreement");
        const cv::Mat directImage = toImage(direct), separableImage = toImage(separable);
        require(cv::norm(gray, directImage, cv::NORM_INF) == 0 &&
                cv::norm(gray, separableImage, cv::NORM_INF) == 0, "exact 8-bit reconstruction");
        const auto centered = shift(spectrum, false);
        cv::Mat power(size, size, CV_64FC1);
        for (int r = 0; r < size; ++r)
            for (int c = 0; c < size; ++c) power.at<double>(r, c) = std::log1p(std::norm(centered(r, c)));
        cv::normalize(power, power, 0, 255, cv::NORM_MINMAX);
        power.convertTo(power, CV_8UC1);
        cv::Mat error;
        cv::absdiff(gray, separableImage, error);
        save("image.png", gray); save("power.png", power);
        save("idft_direct.png", directImage); save("idft_separable.png", separableImage);
        save("difference.png", error);
        cv::Mat overview(300, 4 * 276, CV_8UC1, cv::Scalar(255));
        const cv::Mat panels[] = {gray, power, directImage, separableImage};
        const char* titles[] = {"Original", "Log power (centered)", "Direct IDFT", "Separable IDFT"};
        for (int i = 0; i < 4; ++i) {
            cv::Mat enlarged;
            cv::resize(panels[i], enlarged, cv::Size(256, 256), 0, 0, cv::INTER_NEAREST);
            enlarged.copyTo(overview(cv::Rect(i * 276 + 10, 34, 256, 256)));
            cv::putText(overview, titles[i], cv::Point(i * 276 + 10, 23),
                        cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0), 1, cv::LINE_AA);
        }
        save("result_images.png", overview);
        if (measure) benchmark(original);
        std::cout << "\nSaved PNG results. Both 8-bit reconstructions match the input exactly.\n";
        if (show) {
            cv::imshow("IDFT - reconstruction", overview);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
