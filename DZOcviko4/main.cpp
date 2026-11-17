#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>

using Complex = std::complex<double>;
const double PI = std::acos(-1.0);

struct Spectrum
{
    int rows;
    int cols;
    std::vector<Complex> values;

    Spectrum(int m, int n)
        : rows(m), cols(n), values(static_cast<std::size_t>(m) * n) {}

    Complex& at(int row, int col) { return values[row * cols + col]; }
    const Complex& at(int row, int col) const { return values[row * cols + col]; }
};

void validateInput(const cv::Mat& input)
{
    if (input.empty() || input.type() != CV_64FC1) {
        throw std::invalid_argument("DFT input must be a non-empty CV_64FC1 image.");
    }
}

// Equations (1)-(2): four loops, negative exponential, unitary normalization.
Spectrum dft2DDirect(const cv::Mat& input)
{
    validateInput(input);
    const int M = input.rows;
    const int N = input.cols;
    Spectrum output(M, N);
    const double scale = 1.0 / std::sqrt(static_cast<double>(M) * N);

    for (int k = 0; k < M; ++k) {
        for (int l = 0; l < N; ++l) {
            Complex sum(0.0, 0.0);
            for (int m = 0; m < M; ++m) {
                const double* row = input.ptr<double>(m);
                for (int n = 0; n < N; ++n) {
                    const double angle = -2.0 * PI *
                        (static_cast<double>(m) * k / M +
                         static_cast<double>(n) * l / N);
                    sum += row[n] * Complex(std::cos(angle), std::sin(angle));
                }
            }
            output.at(k, l) = sum * scale;
        }
    }
    return output;
}

// Equation (5): complex input is essential for the second (column) pass.
std::vector<Complex> dft1D(const std::vector<Complex>& input)
{
    const int L = static_cast<int>(input.size());
    if (L == 0) {
        throw std::invalid_argument("1D DFT input must not be empty.");
    }
    std::vector<Complex> output(L);
    const double scale = 1.0 / std::sqrt(static_cast<double>(L));
    for (int q = 0; q < L; ++q) {
        Complex sum(0.0, 0.0);
        for (int p = 0; p < L; ++p) {
            const double angle = -2.0 * PI * static_cast<double>(p) * q / L;
            sum += input[p] * Complex(std::cos(angle), std::sin(angle));
        }
        output[q] = sum * scale;
    }
    return output;
}

// Equations (3)-(4): rows first, then columns; total O(MN(M+N)).
Spectrum dft2DSeparable(const cv::Mat& input)
{
    validateInput(input);
    Spectrum intermediate(input.rows, input.cols);
    Spectrum output(input.rows, input.cols);

    for (int m = 0; m < input.rows; ++m) {
        std::vector<Complex> row(input.cols);
        for (int n = 0; n < input.cols; ++n) {
            row[n] = Complex(input.at<double>(m, n), 0.0);
        }
        const std::vector<Complex> transformed = dft1D(row);
        for (int l = 0; l < input.cols; ++l) {
            intermediate.at(m, l) = transformed[l];
        }
    }
    for (int l = 0; l < input.cols; ++l) {
        std::vector<Complex> column(input.rows);
        for (int m = 0; m < input.rows; ++m) {
            column[m] = intermediate.at(m, l);
        }
        const std::vector<Complex> transformed = dft1D(column);
        for (int k = 0; k < input.rows; ++k) {
            output.at(k, l) = transformed[k];
        }
    }
    return output;
}

double maximumDifference(const Spectrum& a, const Spectrum& b)
{
    if (a.rows != b.rows || a.cols != b.cols) {
        throw std::invalid_argument("Spectrum dimensions do not match.");
    }
    double difference = 0.0;
    for (std::size_t i = 0; i < a.values.size(); ++i) {
        difference = std::max(difference, std::abs(a.values[i] - b.values[i]));
    }
    return difference;
}

// Shift both complex components together. Also works for odd/rectangular sizes.
Spectrum fftshift(const Spectrum& input)
{
    Spectrum shifted(input.rows, input.cols);
    for (int k = 0; k < input.rows; ++k) {
        for (int l = 0; l < input.cols; ++l) {
            shifted.at((k + input.rows / 2) % input.rows,
                       (l + input.cols / 2) % input.cols) = input.at(k, l);
        }
    }
    return shifted;
}

cv::Mat normalizeForDisplay(const cv::Mat& input)
{
    double minimum = 0.0;
    double maximum = 0.0;
    cv::minMaxLoc(input, &minimum, &maximum);
    if (maximum == minimum) {
        return cv::Mat::zeros(input.size(), CV_8UC1);
    }
    cv::Mat output;
    input.convertTo(output, CV_8UC1, 255.0 / (maximum - minimum),
                    -minimum * 255.0 / (maximum - minimum));
    return output;
}

struct Visualization
{
    cv::Mat amplitude;
    cv::Mat phase;
    cv::Mat power;
};

Visualization visualize(const Spectrum& original)
{
    const Spectrum shifted = fftshift(original);
    cv::Mat amplitude(shifted.rows, shifted.cols, CV_64FC1);
    cv::Mat phase(shifted.rows, shifted.cols, CV_64FC1);
    cv::Mat logPower(shifted.rows, shifted.cols, CV_64FC1);
    for (int k = 0; k < shifted.rows; ++k) {
        for (int l = 0; l < shifted.cols; ++l) {
            const Complex value = shifted.at(k, l);
            amplitude.at<double>(k, l) = std::abs(value);
            phase.at<double>(k, l) = value == Complex(0.0, 0.0)
                ? 0.0 : std::atan2(value.imag(), value.real());
            logPower.at<double>(k, l) = std::log1p(std::norm(value));
        }
    }
    Visualization result;
    result.amplitude = normalizeForDisplay(amplitude);
    // Fixed mapping: -pi -> black, zero -> middle gray, +pi -> white.
    phase.convertTo(result.phase, CV_8UC1, 255.0 / (2.0 * PI), 127.5);
    result.power = normalizeForDisplay(logPower);
    return result;
}

// Presentation for comparison with the older screenshot, not the phase/power
// required by the equations. The original unitary complex spectrum is preserved.
Visualization referenceVisualization(const Spectrum& original)
{
    Visualization result = visualize(original);
    const Spectrum shifted = fftshift(original);
    cv::Mat foldedPhase(shifted.rows, shifted.cols, CV_64FC1);
    cv::Mat logPower(shifted.rows, shifted.cols, CV_64FC1);
    for (int k = 0; k < shifted.rows; ++k) {
        for (int l = 0; l < shifted.cols; ++l) {
            const Complex value = shifted.at(k, l);
            double angle = value == Complex(0, 0) ? 0.0
                : std::atan2(value.imag(), value.real());
            if (angle > PI / 2.0) angle -= PI;
            if (angle < -PI / 2.0) angle += PI;
            foldedPhase.at<double>(k, l) = angle;
            // Undo the unitary scale only for this optional display.
            logPower.at<double>(k, l) = std::log1p(
                std::norm(value) * static_cast<double>(original.rows) * original.cols);
        }
    }
    foldedPhase.convertTo(result.phase, CV_8UC1, 255.0 / PI, 127.5);
    result.power = normalizeForDisplay(logPower);
    return result;
}

void saveSpectrum(const Spectrum& spectrum)
{
    cv::Mat real(spectrum.rows, spectrum.cols, CV_64FC1);
    cv::Mat imaginary(spectrum.rows, spectrum.cols, CV_64FC1);
    for (int k = 0; k < spectrum.rows; ++k) {
        for (int l = 0; l < spectrum.cols; ++l) {
            real.at<double>(k, l) = spectrum.at(k, l).real();
            imaginary.at<double>(k, l) = spectrum.at(k, l).imag();
        }
    }
    cv::FileStorage file("spectrum.yml.gz", cv::FileStorage::WRITE);
    if (!file.isOpened()) {
        throw std::runtime_error("Cannot save the complex spectrum.");
    }
    file << "normalization" << "unitary 1/sqrt(M*N)";
    file << "shifted" << 0 << "real" << real << "imaginary" << imaginary;
}

// The reference uses enlarged pixel cells. Preserve them with nearest-neighbor.
cv::Mat displayPanel(const cv::Mat& input)
{
    cv::Mat output;
    const double scale = 256.0 / std::max(input.rows, input.cols);
    cv::resize(input, output, cv::Size(), scale, scale, cv::INTER_NEAREST);
    return output;
}

cv::Mat makeOverview(const cv::Mat& gray, const Visualization& visualization)
{
    const cv::Mat panels[] = {displayPanel(gray), displayPanel(visualization.phase),
                             displayPanel(visualization.power)};
    const char* titles[] = {"Image", "Phase", "Power"};
    const int width = panels[0].cols;
    cv::Mat overview(panels[0].rows + 30, width * 3, CV_8UC1, cv::Scalar(238));
    for (int i = 0; i < 3; ++i) {
        panels[i].copyTo(overview(cv::Rect(i * width, 30, width, panels[i].rows)));
        int baseline = 0;
        const cv::Size text = cv::getTextSize(titles[i], cv::FONT_HERSHEY_SIMPLEX,
                                            0.45, 1, &baseline);
        cv::putText(overview, titles[i],
                    cv::Point(i * width + (width - text.width) / 2, 20),
                    cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(140), 1, cv::LINE_AA);
    }
    return overview;
}

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(std::string("Verification failed: ") + message);
    }
}

void selfTest()
{
    const double tolerance = 1e-10;
    cv::Mat rectangular(3, 5, CV_64FC1);
    const double pixels[] = {1, 2, 4, 8, 16, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31};
    std::copy(pixels, pixels + 15, rectangular.ptr<double>());
    const Spectrum direct = dft2DDirect(rectangular);
    const Spectrum separable = dft2DSeparable(rectangular);
    const double difference = maximumDifference(direct, separable);
    require(difference < tolerance, "3x5 direct/separable comparison");
    double spectrumEnergy = 0.0;
    for (const Complex& value : separable.values) {
        spectrumEnergy += std::norm(value);
    }
    require(std::abs(spectrumEnergy - cv::sum(rectangular.mul(rectangular))[0])
            < tolerance, "unitary energy preservation");

    // An off-center impulse checks the sign, axes and normalization analytically.
    cv::Mat impulse = cv::Mat::zeros(3, 5, CV_64FC1);
    impulse.at<double>(1, 2) = 1.0;
    const Spectrum impulseTransforms[] = {dft2DDirect(impulse), dft2DSeparable(impulse)};
    for (const Spectrum& spectrum : impulseTransforms) {
        for (int k = 0; k < 3; ++k) {
            for (int l = 0; l < 5; ++l) {
                const double angle = -2.0 * PI * (k / 3.0 + 2.0 * l / 5.0);
                const Complex expected = Complex(std::cos(angle), std::sin(angle))
                    / std::sqrt(15.0);
                require(std::abs(spectrum.at(k, l) - expected) < tolerance,
                        "analytical rectangular impulse");
            }
        }
    }
    const cv::Mat constant(3, 5, CV_64FC1, cv::Scalar(7.0));
    const Spectrum constants[] = {dft2DDirect(constant), dft2DSeparable(constant)};
    for (const Spectrum& spectrum : constants) {
        require(std::abs(spectrum.at(0, 0) - Complex(7.0 * std::sqrt(15.0), 0.0))
                < tolerance, "constant DC = c*sqrt(M*N)");
        for (std::size_t i = 1; i < spectrum.values.size(); ++i) {
            require(std::abs(spectrum.values[i]) < tolerance, "constant non-DC coefficients");
        }
    }
    const std::vector<Complex> complexInput = {Complex(1, 2), Complex(3, 4)};
    const std::vector<Complex> complexOutput = dft1D(complexInput);
    require(std::abs(complexOutput[0] - Complex(4, 6) / std::sqrt(2.0)) < tolerance
            && std::abs(complexOutput[1] - Complex(-2, -2) / std::sqrt(2.0)) < tolerance,
            "complex 1D input");
    for (const cv::Size size : {cv::Size(5, 3), cv::Size(4, 2)}) {
        Spectrum labels(size.height, size.width);
        for (std::size_t i = 0; i < labels.values.size(); ++i) {
            labels.values[i] = Complex(static_cast<double>(i), -static_cast<double>(i));
        }
        const Spectrum shifted = fftshift(labels);
        for (int k = 0; k < labels.rows; ++k) {
            for (int l = 0; l < labels.cols; ++l) {
                require(shifted.at((k + labels.rows / 2) % labels.rows,
                                   (l + labels.cols / 2) % labels.cols) == labels.at(k, l),
                        "odd/even complex fftshift");
            }
        }
    }
    const Spectrum zero = dft2DSeparable(cv::Mat::zeros(3, 5, CV_64FC1));
    const Visualization zeroView = visualize(zero);
    require(cv::countNonZero(zeroView.power) == 0
            && cv::countNonZero(zeroView.amplitude) == 0,
            "constant display normalization");
    require(cv::norm(zeroView.phase, cv::Mat(3, 5, CV_8UC1, cv::Scalar(128)),
                     cv::NORM_INF) == 0.0, "zero coefficient phase");
    std::cout << "Verification passed: rectangular 3x5, analytical impulse, constant,\n"
              << "  complex 1D, energy, odd/even fftshift and zero visualization.\n"
              << "  3x5 maximum |direct - separable| = " << std::scientific
              << difference << std::defaultfloat << '\n';
}

struct TimedSpectrum
{
    Spectrum spectrum;
    double milliseconds;
};

TimedSpectrum timedTransform(const cv::Mat& input, bool direct)
{
    const auto start = std::chrono::steady_clock::now();
    Spectrum result = direct ? dft2DDirect(input) : dft2DSeparable(input);
    const auto end = std::chrono::steady_clock::now();
    return {std::move(result),
            std::chrono::duration<double, std::milli>(end - start).count()};
}

void benchmark(const cv::Mat& original)
{
    std::ofstream csv("benchmark.csv");
    if (!csv) {
        throw std::runtime_error("Cannot write benchmark.csv.");
    }
    csv << "size,direct_ms,separable_ms,speedup,max_absolute_difference\n";
    std::cout << "\nTransform timings (loading/resizing/visualization excluded):\n"
              << " Size    Direct ms   Separable ms   Speedup   Max difference\n";
    // Warm up code paths before measuring the required sizes.
    const cv::Mat warmup = cv::Mat::ones(4, 4, CV_64FC1);
    dft2DDirect(warmup);
    dft2DSeparable(warmup);
    for (int size : {8, 16, 32, 64}) {
        cv::Mat resized;
        cv::resize(original, resized, cv::Size(size, size), 0, 0, cv::INTER_AREA);
        cv::Mat input;
        resized.convertTo(input, CV_64FC1);
        const TimedSpectrum direct = timedTransform(input, true);
        const TimedSpectrum separable = timedTransform(input, false);
        const double difference = maximumDifference(direct.spectrum, separable.spectrum);
        require(difference < 1e-7, "benchmark direct/separable spectra");
        const double speedup = direct.milliseconds / separable.milliseconds;
        csv << std::setprecision(12) << size << ',' << direct.milliseconds << ','
            << separable.milliseconds << ',' << speedup << ',' << difference << '\n';
        std::cout << std::setw(5) << size << std::fixed << std::setprecision(3)
                  << std::setw(13) << direct.milliseconds
                  << std::setw(15) << separable.milliseconds
                  << std::setw(10) << speedup << "   " << std::scientific
                  << difference << std::defaultfloat << '\n';
    }
    std::cout << "Doubling the side: theoretical work x16 (direct O(N^4)),\n"
              << "  x8 (separable O(N^3)); timings also depend on hardware.\n";
}

struct Options
{
    std::string path = "images/lena_gray.png";
    int size = 64;
    bool show = true;
    bool testsOnly = false;
    bool runBenchmark = true;
    bool referenceStyle = false;
};

void usage()
{
    std::cout << "Usage: dzo_dft [image-path] [--size N|--original-size]\n"
              << "               [--no-show] [--no-benchmark] [--self-test]\n"
              << "               [--reference-style]\n"
              << "Default: Lena resized to 64x64; save results and show windows.\n";
}

Options parseOptions(int argc, char* argv[])
{
    Options options;
    bool hasPath = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--no-show") {
            options.show = false;
        } else if (argument == "--self-test") {
            options.testsOnly = true;
        } else if (argument == "--no-benchmark") {
            options.runBenchmark = false;
        } else if (argument == "--reference-style") {
            options.referenceStyle = true;
        } else if (argument == "--original-size") {
            options.size = 0;
        } else if (argument == "--size") {
            if (++i == argc) {
                throw std::invalid_argument("Missing value after --size.");
            }
            const std::string value = argv[i];
            std::size_t consumed = 0;
            options.size = std::stoi(value, &consumed);
            if (consumed != value.size() || options.size < 1) {
                throw std::invalid_argument("--size must be a positive integer.");
            }
        } else if (argument == "--help" || argument == "-h") {
            usage();
            std::exit(0);
        } else if (!hasPath && !argument.empty() && argument[0] != '-') {
            options.path = argument;
            hasPath = true;
        } else {
            throw std::invalid_argument("Unknown argument: " + argument);
        }
    }
    return options;
}

void saveImage(const char* path, const cv::Mat& image)
{
    if (!cv::imwrite(path, image)) {
        throw std::runtime_error(std::string("Cannot save ") + path);
    }
}

int main(int argc, char* argv[])
{
    try {
        const Options options = parseOptions(argc, argv);
        selfTest();
        if (options.testsOnly) {
            return 0;
        }
        // Keep values in 0..255: log(1+power) depends on the input brightness scale.
        const cv::Mat original = cv::imread(options.path, cv::IMREAD_GRAYSCALE);
        if (original.empty()) {
            throw std::runtime_error("Cannot load image: " + options.path);
        }
        cv::Mat gray = original;
        if (options.size > 0) {
            cv::resize(original, gray, cv::Size(options.size, options.size),
                       0, 0, cv::INTER_AREA);
        }
        cv::Mat input;
        gray.convertTo(input, CV_64FC1);
        std::cout << "\nImage: " << options.path << " (" << gray.rows << " rows x "
                  << gray.cols << " columns)\n";
        const TimedSpectrum separable = timedTransform(input, false);
        std::cout << "Separable DFT: " << separable.milliseconds << " ms\n";
        if (input.total() <= 64 * 64) {
            const TimedSpectrum direct = timedTransform(input, true);
            const double difference = maximumDifference(direct.spectrum, separable.spectrum);
            std::cout << "Direct DFT: " << direct.milliseconds << " ms\n"
                      << "Maximum |direct - separable| (unshifted): "
                      << std::scientific << difference << std::defaultfloat << '\n';
            require(difference < 1e-7, "input direct/separable spectra");
        } else {
            std::cout << "Full-image direct comparison limited to 4096 pixels;\n"
                      << "  required comparisons are performed on smaller images.\n";
        }
        if (options.runBenchmark) {
            benchmark(original);
        }
        const Visualization view = visualize(separable.spectrum);
        const Visualization referenceView = referenceVisualization(separable.spectrum);
        saveImage("image.png", gray);
        saveImage("amplitude.png", view.amplitude);
        saveImage("phase.png", view.phase);
        saveImage("power.png", view.power);
        saveImage("result_images.png", makeOverview(gray, view));
        saveImage("reference_style.png", makeOverview(gray, referenceView));
        saveSpectrum(separable.spectrum);
        std::cout << "\nSaved image.png, amplitude.png, phase.png, power.png,\n"
                  << "  result_images.png, reference_style.png and unshifted spectrum.yml.gz.\n";
        if (options.show) {
            const Visualization& display = options.referenceStyle ? referenceView : view;
            const char* titles[] = {"Image", "Phase", "Power", "Amplitude"};
            const cv::Mat images[] = {gray, display.phase, display.power, view.amplitude};
            if (options.referenceStyle) {
                std::cout << "Showing reference style: phase folded modulo pi, display power rescaled.\n"
                          << "  phase.png and power.png always follow the assignment equations.\n";
            }
            for (int i = 0; i < 4; ++i) {
                cv::imshow(titles[i], displayPanel(images[i]));
                cv::moveWindow(titles[i], 40 + (i % 3) * 270, 60 + (i / 3) * 310);
            }
            std::cout << "Press any key in an image window to close.\n";
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
