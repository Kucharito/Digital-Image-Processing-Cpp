#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

enum class Interpolation { Nearest, Bilinear };

// Coordinate convention from the assignment: cu = w/2, cv = h/2.
struct RadialModel {
    double cu, cv, R, k1, k2;
    RadialModel(int width, int height, double first, double second)
        : cu(width / 2.0), cv(height / 2.0), R(std::hypot(cu, cv)),
          k1(first), k2(second) {
        if (width <= 0 || height <= 0 || !std::isfinite(k1) || !std::isfinite(k2))
            throw std::invalid_argument("Invalid image size or coefficients.");
        // r^2 is in [0,1]. Test the minimum of the quadratic phi on this interval.
        double minimum = std::min(phi(0), phi(1));
        if (k2 > 0) {
            const double vertex = -k1 / (2 * k2);
            if (vertex > 0 && vertex < 1) minimum = std::min(minimum, phi(vertex));
        }
        if (!std::isfinite(phi(1)) || minimum <= 1e-8)
            throw std::invalid_argument("phi(r^2) must be positive on [0,1].");
    }
    double phi(double r2) const { return 1.0 + k1 * r2 + k2 * r2 * r2; }

    cv::Point2d source(double xn, double yn) const {
        const double dx = xn - cu, dy = yn - cv;
        const double r2 = (dx * dx + dy * dy) / (R * R);
        // Equation (3): phi^-1 means the reciprocal radial scale.
        const double scale = 1.0 / phi(r2);
        return {cu + dx * scale, cv + dy * scale};
    }
};

// Manual interpolation for an 8-bit grayscale or BGR image.
// Out-of-image coordinates are black; at the last row/column clamp the neighbour.
void sample(const cv::Mat& input, double x, double y, Interpolation method,
            unsigned char* output) {
    const int channels = input.channels();
    std::fill(output, output + channels, 0);
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < 0 || y < 0 || x > input.cols - 1 || y > input.rows - 1) return;
    if (method == Interpolation::Nearest) {
        const int sx = static_cast<int>(std::lround(x));
        const int sy = static_cast<int>(std::lround(y));
        const auto* pixel = input.ptr<unsigned char>(sy) + sx * channels;
        std::copy(pixel, pixel + channels, output);
        return;
    }
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, input.cols - 1);
    const int y1 = std::min(y0 + 1, input.rows - 1);
    const double ax = x - x0, ay = y - y0;
    for (int c = 0; c < channels; ++c) {
        const double top = (1 - ax) * input.ptr<unsigned char>(y0)[x0 * channels + c]
                         + ax * input.ptr<unsigned char>(y0)[x1 * channels + c];
        const double bottom = (1 - ax) * input.ptr<unsigned char>(y1)[x0 * channels + c]
                            + ax * input.ptr<unsigned char>(y1)[x1 * channels + c];
        output[c] = cv::saturate_cast<unsigned char>((1 - ay) * top + ay * bottom);
    }
}

cv::Mat removeDistortion(const cv::Mat& input, double k1, double k2,
                         Interpolation method) {
    if (input.empty() || input.depth() != CV_8U ||
        (input.channels() != 1 && input.channels() != 3))
        throw std::invalid_argument("Expected an 8-bit grayscale or BGR image.");
    const RadialModel model(input.cols, input.rows, k1, k2);
    cv::Mat output(input.size(), input.type());
    for (int yn = 0; yn < output.rows; ++yn) {
        auto* row = output.ptr<unsigned char>(yn);
        for (int xn = 0; xn < output.cols; ++xn) {
            const auto p = model.source(xn, yn);
            sample(input, p.x, p.y, method, row + xn * input.channels());
        }
    }
    return output;
}

cv::Mat makeGrid() {
    cv::Mat grid(540, 720, CV_8UC3, cv::Scalar(238, 238, 238));
    for (int y = 0; y < grid.rows; y += 60)
        for (int x = 0; x < grid.cols; x += 60)
            if ((x / 60 + y / 60) % 2)
                cv::rectangle(grid, {x, y, 60, 60}, cv::Scalar(205, 220, 230), cv::FILLED);
    for (int x = 0; x < grid.cols; x += 60)
        cv::line(grid, {x, 0}, {x, grid.rows - 1}, cv::Scalar(45, 45, 45), 2, cv::LINE_AA);
    for (int y = 0; y < grid.rows; y += 60)
        cv::line(grid, {0, y}, {grid.cols - 1, y}, cv::Scalar(45, 45, 45), 2, cv::LINE_AA);
    cv::line(grid, {360, 0}, {360, 539}, cv::Scalar(60, 60, 205), 3, cv::LINE_AA);
    cv::line(grid, {0, 270}, {719, 270}, cv::Scalar(60, 60, 205), 3, cv::LINE_AA);
    cv::circle(grid, {360, 270}, 100, cv::Scalar(175, 95, 25), 3, cv::LINE_AA);
    return grid;
}

// Create the demonstration INPUT, using the numerical inverse of equation (3).
// Solve rd = rn / phi(rn^2). Negating k1/k2 would not be an exact inverse.
cv::Mat distortDemo(const cv::Mat& reference, double k1, double k2) {
    const RadialModel model(reference.cols, reference.rows, k1, k2);
    // d(rd)/d(rn) has numerator 1-k1*t-3*k2*t^2, t=rn^2.
    double derivativeMinimum = std::min(1.0, 1.0 - k1 - 3 * k2);
    if (k2 < 0) {
        const double vertex = -k1 / (6 * k2);
        if (vertex > 0 && vertex < 1)
            derivativeMinimum = std::min(derivativeMinimum,
                                        1 - k1 * vertex - 3 * k2 * vertex * vertex);
    }
    if (derivativeMinimum <= 0)
        throw std::invalid_argument("Demo requires an invertible radial model on [0,1].");
    cv::Mat distorted(reference.size(), reference.type(), cv::Scalar::all(0));
    const double maxRd = 1.0 / model.phi(1);
    for (int y = 0; y < distorted.rows; ++y)
        for (int x = 0; x < distorted.cols; ++x) {
            const double dx = x - model.cu, dy = y - model.cv;
            const double rd = std::hypot(dx, dy) / model.R;
            if (rd > maxRd) continue;
            double low = 0, high = 1;
            for (int iteration = 0; iteration < 55; ++iteration) {
                const double rn = (low + high) / 2;
                if (rn / model.phi(rn * rn) < rd) low = rn;
                else high = rn;
            }
            const double scale = rd > 0 ? ((low + high) / 2) / rd : 1;
            sample(reference, model.cu + dx * scale, model.cv + dy * scale,
                   Interpolation::Bilinear, distorted.ptr<unsigned char>(y) + x * 3);
        }
    return distorted;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("Verification failed: " + message);
}

void selfTest() {
    // Independent known value: center=(4,3), R=5, r^2(0,0)=1, phi=1.5.
    const RadialModel model(8, 6, 0.25, 0.25);
    const auto corner = model.source(0, 0), center = model.source(4, 3);
    require(std::abs(corner.x - 4.0 / 3) < 1e-12 &&
            std::abs(corner.y - 1) < 1e-12, "equation (3), reciprocal and normalization");
    require(center == cv::Point2d(4, 3), "image center stays fixed");
    // Bilinear interpolation must exactly reproduce an affine intensity plane.
    cv::Mat ramp(4, 5, CV_8UC1);
    for (int y = 0; y < ramp.rows; ++y)
        for (int x = 0; x < ramp.cols; ++x) ramp.at<unsigned char>(y, x) = 10 + 20*x + 40*y;
    unsigned char value = 0;
    sample(ramp, 1.25, 1.5, Interpolation::Bilinear, &value);
    require(value == 95, "bilinear weights on affine plane");
    sample(ramp, 1.25, 1.5, Interpolation::Nearest, &value);
    require(value == 110, "nearest neighbour rounds coordinates");
    sample(ramp, 4, 3, Interpolation::Bilinear, &value);
    require(value == 210, "last row and column");
    sample(ramp, -0.01, 2, Interpolation::Bilinear, &value);
    require(value == 0, "black outside the image");
    // Whole correction on an independently predictable BGR affine image.
    cv::Mat colorRamp(6, 8, CV_8UC3);
    for (int y = 0; y < colorRamp.rows; ++y)
        for (int x = 0; x < colorRamp.cols; ++x)
            colorRamp.at<cv::Vec3b>(y, x) = cv::Vec3b(12*x + 24*y, 8*x + 16*y, 6*x + 12*y);
    const auto corrected = removeDistortion(colorRamp, 0.25, 0.25, Interpolation::Bilinear);
    require(corrected.at<cv::Vec3b>(0, 0) == cv::Vec3b(40, 27, 20),
            "nonzero radial correction and all three BGR channels");
    for (const auto size : {cv::Size(7, 5), cv::Size(8, 6), cv::Size(1, 1),
                            cv::Size(1, 9), cv::Size(9, 1)}) {
        for (int type : {CV_8UC1, CV_8UC3}) {
            cv::Mat image(size, type);
            cv::RNG rng(123);
            rng.fill(image, cv::RNG::UNIFORM, 0, 256);
            for (auto method : {Interpolation::Nearest, Interpolation::Bilinear})
                require(cv::norm(image, removeDistortion(image, 0, 0, method), cv::NORM_INF) == 0,
                        "zero coefficients must give pixel-identical output");
        }
    }
    bool rejected = false;
    try { const RadialModel invalid(8, 6, -4, 4); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "reject interior zero of phi");
    std::cout << "Self-test OK: mapping, interpolation, boundaries, grayscale/BGR, identity.\n";
}

void save(const std::string& path, const cv::Mat& image) {
    if (!cv::imwrite(path, image)) throw std::runtime_error("Cannot save " + path);
}

cv::Mat overview(const cv::Mat& input, const cv::Mat& nearest, const cv::Mat& bilinear,
                 double k1, double k2) {
    const int width = 480;
    const int height = std::max(1, static_cast<int>(std::lround(input.rows * 480.0 / input.cols)));
    cv::Mat canvas(height + 80, width * 3, CV_8UC3, cv::Scalar(250, 250, 250));
    const cv::Mat images[] = {input, nearest, bilinear};
    const char* labels[] = {"Distorted input", "Corrected: nearest", "Corrected: bilinear"};
    for (int i = 0; i < 3; ++i) {
        cv::Mat preview;
        cv::resize(images[i], preview, {width, height}, 0, 0, cv::INTER_AREA);
        preview.copyTo(canvas(cv::Rect(i * width, 42, width, height)));
        cv::putText(canvas, labels[i], {i * width + 12, 28}, cv::FONT_HERSHEY_SIMPLEX,
                    0.65, cv::Scalar(40, 40, 40), 1, cv::LINE_AA);
    }
    cv::putText(canvas, "k1=" + std::to_string(k1) + "  k2=" + std::to_string(k2),
                {12, height + 68}, cv::FONT_HERSHEY_SIMPLEX, 0.55,
                cv::Scalar(40, 40, 40), 1, cv::LINE_AA);
    return canvas;
}

double number(const std::string& text) {
    std::size_t end = 0;
    const double value = std::stod(text, &end);
    if (end != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Invalid numeric argument: " + text);
    return value;
}

int main(int argc, char** argv) {
    try {
        std::string inputPath;
        double k1 = 0.45, k2 = 0.10;
        bool show = true, onlyTest = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--no-show") show = false;
            else if (arg == "--self-test") onlyTest = true;
            else if (arg == "--help") {
                std::cout << "Usage: dzo_radial.exe [image] [--k1 value] [--k2 value]\n"
                          << "       [--no-show] [--self-test]\n"
                          << "No image: synthetic grid demo, default k1=0.45, k2=0.10.\n"
                          << "With image: supply coefficients appropriate to that image.\n";
                return 0;
            } else if (arg == "--k1" || arg == "--k2") {
                if (++i == argc) throw std::invalid_argument("Missing value for " + arg);
                (arg == "--k1" ? k1 : k2) = number(argv[i]);
            } else if (arg.rfind("--", 0) == 0 || !inputPath.empty())
                throw std::invalid_argument("Unexpected argument: " + arg);
            else inputPath = arg;
        }
        selfTest();
        if (onlyTest) return 0;
        cv::Mat input, reference;
        if (inputPath.empty()) {
            reference = makeGrid();
            input = distortDemo(reference, k1, k2);
            std::filesystem::create_directories("images");
            save("images/grid_undistorted.png", reference);
            save("images/distorted_grid.png", input);
            std::cout << "Synthetic grid demonstration.\n";
        } else {
            input = cv::imread(inputPath, cv::IMREAD_COLOR);
            if (input.empty()) throw std::runtime_error("Cannot read image: " + inputPath);
        }
        const cv::Mat nearest = removeDistortion(input, k1, k2, Interpolation::Nearest);
        const cv::Mat bilinear = removeDistortion(input, k1, k2, Interpolation::Bilinear);
        save("image.png", input);
        save("corrected_nearest.png", nearest);
        save("corrected_bilinear.png", bilinear);
        const cv::Mat panel = overview(input, nearest, bilinear, k1, k2);
        save("result_images.png", panel);
        std::cout << "Size: " << input.cols << 'x' << input.rows
                  << ", k1=" << k1 << ", k2=" << k2 << '\n';
        if (!reference.empty()) {
            const double samples = static_cast<double>(reference.total()) * reference.channels();
            const double maeNearest = cv::norm(reference, nearest, cv::NORM_L1) / samples;
            const double maeBilinear = cv::norm(reference, bilinear, cv::NORM_L1) / samples;
            std::ofstream report("demo_metrics.csv");
            if (!report) throw std::runtime_error("Cannot write demo_metrics.csv");
            report << std::setprecision(10) << "method,mae_to_reference\nnearest," << maeNearest
                   << "\nbilinear," << maeBilinear << '\n';
            std::cout << "MAE to original grid (0..255): nearest=" << maeNearest
                      << ", bilinear=" << maeBilinear << '\n';
            cv::Mat difference;
            cv::absdiff(reference, bilinear, difference);
            save("demo_difference.png", difference);
        }
        std::cout << "Saved image.png, corrected_nearest.png, corrected_bilinear.png, result_images.png\n";
        if (show) {
            cv::namedWindow("DZO 7 - Radial lens distortion", cv::WINDOW_NORMAL);
            cv::imshow("DZO 7 - Radial lens distortion", panel);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
