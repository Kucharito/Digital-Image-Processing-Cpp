#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using Points = std::array<cv::Point2d, 4>;
enum class Interpolation { Nearest, Bilinear };

// Clockwise, starting at the top-left corner, as specified in the assignment.
const Points BUILDING_POINTS = {{{69, 107}, {227, 76}, {228, 122}, {66, 134}}};

Points imageCorners(const cv::Mat& image) {
    return {{{0, 0}, {double(image.cols - 1), 0},
             {double(image.cols - 1), double(image.rows - 1)},
             {0, double(image.rows - 1)}}};
}

void validateQuad(const Points& points) {
    double previous = 0;
    for (int i = 0; i < 4; ++i) {
        const auto a = points[i], b = points[(i + 1) % 4], c = points[(i + 2) % 4];
        if (!std::isfinite(a.x) || !std::isfinite(a.y))
            throw std::invalid_argument("Point coordinates must be finite.");
        const double cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (!std::isfinite(cross) || std::abs(cross) < 1e-10 ||
            (i > 0 && (cross > 0) != (previous > 0)))
            throw std::invalid_argument("Points must form a convex quadrilateral in boundary order.");
        previous = cross;
    }
}

// Solve equation (5): fix p11 = 1, leaving eight unknown coefficients.
// H maps the flag (x,y) to the building (u,v), not the other way around.
cv::Matx33d solvePerspective(const Points& source, const Points& destination) {
    validateQuad(source);
    validateQuad(destination);
    cv::Mat A = cv::Mat::zeros(8, 8, CV_64F);
    cv::Mat b = cv::Mat::zeros(8, 1, CV_64F);
    for (int i = 0; i < 4; ++i) {
        const double x = source[i].x, y = source[i].y;
        const double u = destination[i].x, v = destination[i].y;
        // Unknown vector: [p12, p13, p21, p22, p23, p31, p32, p33].
        const double first[]  = {y, 1, 0, 0, 0, -u*x, -u*y, -u};
        const double second[] = {0, 0, x, y, 1, -v*x, -v*y, -v};
        std::copy(first, first + 8, A.ptr<double>(2*i));
        std::copy(second, second + 8, A.ptr<double>(2*i + 1));
        b.at<double>(2*i) = -x;
    }
    cv::Mat p;
    if (!cv::solve(A, b, p, cv::DECOMP_LU) || !cv::checkRange(p))
        throw std::runtime_error("Cannot solve the homography with p11=1 for these points.");
    return {1, p.at<double>(0), p.at<double>(1),
            p.at<double>(2), p.at<double>(3), p.at<double>(4),
            p.at<double>(5), p.at<double>(6), p.at<double>(7)};
}

cv::Matx33d inversePerspective(const cv::Matx33d& H) {
    cv::Mat result;
    if (!cv::solve(cv::Mat(H), cv::Mat::eye(3, 3, CV_64F), result, cv::DECOMP_LU)
        || !cv::checkRange(result))
        throw std::runtime_error("The perspective matrix is singular.");
    cv::Matx33d inverse;
    std::copy(result.ptr<double>(), result.ptr<double>() + 9, inverse.val);
    return inverse;
}

// Homogeneous coordinates must be divided by their third component w.
bool project(const cv::Matx33d& H, double x, double y, cv::Point2d& result) {
    const cv::Vec3d p = H * cv::Vec3d(x, y, 1);
    const double scale = std::abs(H(2, 0)*x) + std::abs(H(2, 1)*y) + std::abs(H(2, 2));
    if (!std::isfinite(p[2]) || std::abs(p[2]) <= 1e-12 * scale) return false;
    result = {p[0] / p[2], p[1] / p[2]};
    return std::isfinite(result.x) && std::isfinite(result.y);
}

// Return false outside the source: the original building pixel is then preserved.
bool sample(const cv::Mat& image, double x, double y, Interpolation method,
            unsigned char* output) {
    constexpr double epsilon = 1e-8; // Tolerate roundoff at mapped corner pixels.
    if (!std::isfinite(x) || !std::isfinite(y) || x < -epsilon || y < -epsilon ||
        x > image.cols - 1 + epsilon || y > image.rows - 1 + epsilon) return false;
    x = std::clamp(x, 0.0, double(image.cols - 1));
    y = std::clamp(y, 0.0, double(image.rows - 1));
    const int channels = image.channels();
    if (method == Interpolation::Nearest) {
        const int sx = int(std::lround(x)), sy = int(std::lround(y));
        const auto* pixel = image.ptr<unsigned char>(sy) + sx * channels;
        std::copy(pixel, pixel + channels, output);
        return true;
    }
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const int x1 = std::min(x0 + 1, image.cols - 1);
    const int y1 = std::min(y0 + 1, image.rows - 1);
    const double ax = x - x0, ay = y - y0;
    for (int c = 0; c < channels; ++c) {
        const double top = (1-ax) * image.ptr<unsigned char>(y0)[x0*channels+c]
                          + ax * image.ptr<unsigned char>(y0)[x1*channels+c];
        const double bottom = (1-ax) * image.ptr<unsigned char>(y1)[x0*channels+c]
                             + ax * image.ptr<unsigned char>(y1)[x1*channels+c];
        output[c] = cv::saturate_cast<unsigned char>(int(std::lround((1-ay)*top + ay*bottom)));
    }
    return true;
}

struct Composite {
    cv::Mat image;
    cv::Mat warpedFlag;
    cv::Mat mask;
};

Composite placeImage(const cv::Mat& background, const cv::Mat& flag,
                     const cv::Matx33d& H, Interpolation method) {
    if (background.empty() || flag.empty() || background.type() != flag.type() ||
        flag.depth() != CV_8U || (flag.channels() != 1 && flag.channels() != 3))
        throw std::invalid_argument("Expected nonempty images of the same 8-bit gray/BGR type.");
    const cv::Matx33d inverse = inversePerspective(H);
    Composite result{background.clone(), cv::Mat::zeros(background.size(), background.type()),
                     cv::Mat::zeros(background.size(), CV_8UC1)};
    // Inverse mapping: visit each destination pixel once, avoiding forward-map holes.
    for (int y = 0; y < background.rows; ++y) {
        for (int x = 0; x < background.cols; ++x) {
            cv::Point2d source;
            auto* pixel = result.warpedFlag.ptr<unsigned char>(y) + x * flag.channels();
            if (project(inverse, x, y, source) && sample(flag, source.x, source.y, method, pixel)) {
                std::copy(pixel, pixel + flag.channels(),
                          result.image.ptr<unsigned char>(y) + x * flag.channels());
                result.mask.at<unsigned char>(y, x) = 255;
            }
        }
    }
    return result;
}

double cornerError(const cv::Matx33d& H, const Points& source, const Points& destination) {
    double maximum = 0;
    for (int i = 0; i < 4; ++i) {
        cv::Point2d projected;
        if (!project(H, source[i].x, source[i].y, projected))
            throw std::runtime_error("A corner maps to infinity.");
        maximum = std::max(maximum, cv::norm(projected - destination[i]));
    }
    return maximum;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("Verification failed: ") + message);
}

void selfTest() {
    // An independently specified homography checks all eight solved coefficients.
    const cv::Matx33d known(1, 0.2, 3, -0.1, 0.8, 2, 0.002, 0.003, 1);
    const Points source = {{{0, 0}, {10, 0}, {10, 8}, {0, 8}}};
    Points destination;
    for (int i = 0; i < 4; ++i)
        require(project(known, source[i].x, source[i].y, destination[i]), "known projection");
    const auto recovered = solvePerspective(source, destination);
    require(cv::norm(cv::Mat(recovered), cv::Mat(known), cv::NORM_INF) < 1e-10,
            "known non-affine homography");
    cv::Point2d p, restored;
    require(project(recovered, 4, 3, p) &&
            project(inversePerspective(recovered), p.x, p.y, restored) &&
            cv::norm(restored - cv::Point2d(4, 3)) < 1e-10, "inverse interior point");

    cv::Mat ramp(4, 5, CV_8UC1);
    for (int y = 0; y < ramp.rows; ++y)
        for (int x = 0; x < ramp.cols; ++x) ramp.at<unsigned char>(y, x) = 10 + 20*x + 40*y;
    unsigned char value = 0;
    require(sample(ramp, 1.25, 1.5, Interpolation::Bilinear, &value) && value == 95,
            "bilinear interpolation of an affine ramp");
    require(sample(ramp, 1.25, 1.5, Interpolation::Nearest, &value) && value == 110,
            "nearest neighbour rounding");
    require(sample(ramp, 4, 3, Interpolation::Bilinear, &value) && value == 210,
            "last row and column");
    require(!sample(ramp, -0.1, 0, Interpolation::Bilinear, &value), "outside sampling");
    for (int type : {CV_8UC1, CV_8UC3}) {
        cv::Mat input(7, 9, type);
        cv::RNG rng(2026);
        rng.fill(input, cv::RNG::UNIFORM, 0, 256);
        const auto identity = solvePerspective(imageCorners(input), imageCorners(input));
        for (auto method : {Interpolation::Nearest, Interpolation::Bilinear}) {
            const auto output = placeImage(cv::Mat::zeros(input.size(), type), input, identity, method);
            require(cv::norm(input, output.image, cv::NORM_INF) == 0 &&
                    cv::countNonZero(output.mask) == int(input.total()), "identity, including borders");
        }
    }
    // Fractional translation checks the entire inverse-map + interpolation path.
    const cv::Mat background(8, 10, CV_8UC1, cv::Scalar(17));
    const cv::Matx33d translation(1, 0, 0.75, 0, 1, 0.5, 0, 0, 1);
    const auto placed = placeImage(background, ramp, translation, Interpolation::Bilinear);
    require(placed.image.at<unsigned char>(2, 2) == 95 &&
            placed.image.at<unsigned char>(0, 0) == 17 &&
            placed.mask.at<unsigned char>(0, 0) == 0, "placement and unchanged background");
    bool rejected = false;
    try { solvePerspective(source, {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "collinear destination rejected");
    cv::Matx33d horizon = cv::Matx33d::eye();
    horizon(2, 2) = 0;
    require(!project(horizon, 0, 0, p), "zero homogeneous denominator");
    std::cout << "Self-test OK: known homography, inverse, interpolation, identity, borders, background.\n";
}

cv::Mat overview(const cv::Mat& building, const cv::Mat& flag,
                 const cv::Mat& nearest, const cv::Mat& bilinear) {
    constexpr int width = 520, height = 310, title = 32;
    cv::Mat canvas(2*(height + title), 2*width, CV_8UC3, cv::Scalar(32, 32, 32));
    const cv::Mat images[] = {building, flag, nearest, bilinear};
    const char* labels[] = {"Building input", "Flag input", "Result: nearest neighbour", "Result: bilinear"};
    for (int i = 0; i < 4; ++i) {
        const int left = (i % 2)*width, top = (i / 2)*(height + title);
        const double scale = std::min(double(width)/images[i].cols, double(height)/images[i].rows);
        cv::Mat panel;
        cv::resize(images[i], panel, {}, scale, scale, cv::INTER_NEAREST);
        panel.copyTo(canvas(cv::Rect(left + (width-panel.cols)/2, top+title+(height-panel.rows)/2,
                                    panel.cols, panel.rows)));
        cv::putText(canvas, labels[i], {left+12, top+23}, cv::FONT_HERSHEY_SIMPLEX,
                    0.6, cv::Scalar(245, 245, 245), 1, cv::LINE_AA);
    }
    return canvas;
}

struct Options {
    std::string building = "images/building.png", flag = "images/flag.png", output = ".";
    Points destination = BUILDING_POINTS;
    bool show = true, testsOnly = false;
};

double number(const std::string& value) {
    std::size_t consumed = 0;
    const double result = std::stod(value, &consumed);
    if (consumed != value.size() || !std::isfinite(result))
        throw std::invalid_argument("Expected a finite number: " + value);
    return result;
}

Options parseOptions(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--no-show") options.show = false;
        else if (arg == "--self-test") options.testsOnly = true;
        else if (arg == "--building" || arg == "--flag" || arg == "--output") {
            if (++i == argc) throw std::invalid_argument("Missing value after " + arg);
            if (arg == "--building") options.building = argv[i];
            else if (arg == "--flag") options.flag = argv[i];
            else options.output = argv[i];
        } else if (arg == "--points") {
            if (argc - i - 1 < 8) throw std::invalid_argument("--points needs x1 y1 x2 y2 x3 y3 x4 y4.");
            for (auto& p : options.destination) {
                p.x = number(argv[++i]);
                p.y = number(argv[++i]);
            }
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: dzo_perspective [--no-show] [--self-test]\n"
                      << "  [--building path] [--flag path] [--output directory]\n"
                      << "  [--points x1 y1 x2 y2 x3 y3 x4 y4]\n"
                      << "Point order: top-left, top-right, bottom-right, bottom-left.\n";
            std::exit(0);
        } else throw std::invalid_argument("Unknown argument: " + arg);
    }
    return options;
}

int main(int argc, char* argv[]) {
    try {
        const auto options = parseOptions(argc, argv);
        selfTest();
        if (options.testsOnly) return 0;
        const auto building = cv::imread(options.building, cv::IMREAD_COLOR);
        const auto flag = cv::imread(options.flag, cv::IMREAD_COLOR);
        if (building.empty() || flag.empty()) throw std::runtime_error("Cannot load the building or flag image.");
        if (flag.cols < 2 || flag.rows < 2) throw std::invalid_argument("Flag dimensions must be at least 2x2.");
        const Points source = imageCorners(flag);
        const auto H = solvePerspective(source, options.destination);
        const auto inverse = inversePerspective(H);
        const double forwardError = cornerError(H, source, options.destination);
        const double inverseError = cornerError(inverse, options.destination, source);
        require(forwardError < 1e-7 && inverseError < 1e-7, "four corner correspondences");
        const auto nearest = placeImage(building, flag, H, Interpolation::Nearest);
        const auto bilinear = placeImage(building, flag, H, Interpolation::Bilinear);
        require(cv::countNonZero(bilinear.mask) > 0, "flag intersects the building image");
        cv::Mat difference;
        cv::absdiff(building, bilinear.image, difference);
        difference.setTo(cv::Scalar::all(0), bilinear.mask);
        require(cv::norm(difference, cv::NORM_INF) == 0, "background preserved outside the flag");

        const auto directory = std::filesystem::path(options.output);
        std::filesystem::create_directories(directory);
        const auto save = [&](const char* name, const cv::Mat& image) {
            if (!cv::imwrite((directory/name).string(), image)) throw std::runtime_error("Cannot save image.");
        };
        const auto comparison = overview(building, flag, nearest.image, bilinear.image);
        save("image.png", building);
        save("perspective_nearest.png", nearest.image);
        save("perspective_bilinear.png", bilinear.image);
        save("warped_flag.png", bilinear.warpedFlag);
        save("mask.png", bilinear.mask);
        save("result_images.png", comparison);
        cv::FileStorage matrices((directory/"homography.yml").string(), cv::FileStorage::WRITE);
        if (!matrices.isOpened()) throw std::runtime_error("Cannot save homography.yml.");
        matrices << "direction" << "flag_to_building" << "fixed_coefficient" << "p11=1"
                 << "H" << cv::Mat(H) << "H_inverse" << cv::Mat(inverse);
        std::ofstream report(directory/"metrics.txt");
        if (!report) throw std::runtime_error("Cannot save metrics.txt.");
        report << std::setprecision(15) << "Max forward corner error [px]: " << forwardError
               << "\nMax inverse corner error [px]: " << inverseError
               << "\nPasted pixels: " << cv::countNonZero(bilinear.mask) << '\n';
        for (int i = 0; i < 4; ++i)
            report << "Corner " << i+1 << ": " << source[i] << " -> " << options.destination[i] << '\n';
        std::cout << "H (flag -> building), p11=1:\n" << cv::Mat(H)
                  << "\nMax corner errors [px]: forward=" << forwardError << ", inverse=" << inverseError
                  << "\nPasted pixels: " << cv::countNonZero(bilinear.mask)
                  << "\nSaved results in " << std::filesystem::absolute(directory).string() << '\n';
        if (options.show) {
            cv::imshow("DZO 9 - Perspective transform", comparison);
            std::cout << "Press any key in the image window to close.\n";
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
