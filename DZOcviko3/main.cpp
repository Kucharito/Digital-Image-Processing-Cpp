#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <opencv2/opencv.hpp>

struct ProgramOptions
{
    std::string inputPath = "images/input_image.png";
    int iterations = 1000;
    bool showWindows = true;
};

void printUsage()
{
    std::cout << "Usage: dzo_anisotropic [path-to-image] "
              << "[--iterations N] [--show|--no-show]\n";
}

ProgramOptions parseArguments(int argc, char* argv[])
{
    ProgramOptions options;
    bool customInputPath = false;

    for (int argument = 1; argument < argc; ++argument) {
        const std::string value = argv[argument];

        if (value == "--show") {
            options.showWindows = true;
        } else if (value == "--no-show") {
            options.showWindows = false;
        } else if (value == "--iterations") {
            if (argument + 1 >= argc) {
                throw std::invalid_argument("Missing value after --iterations.");
            }
            options.iterations = std::stoi(argv[++argument]);
        } else if (value == "--help" || value == "-h") {
            printUsage();
            std::exit(0);
        } else if (!customInputPath && !value.empty() && value[0] != '-') {
            options.inputPath = value;
            customInputPath = true;
        } else {
            throw std::invalid_argument("Unknown argument: " + value);
        }
    }

    if (options.iterations < 0) {
        throw std::invalid_argument("The iteration count must not be negative.");
    }

    return options;
}

// Perona-Malik anisotropic diffusion using the four direct neighbours.
// Every iteration reads only from current and writes to a separate matrix next.
cv::Mat anisotropicFilter(const cv::Mat& input,
                          int iterations,
                          double sigma,
                          double lambda)
{
    if (input.empty()) {
        throw std::invalid_argument("Input image is empty.");
    }
    if (input.type() != CV_64FC1) {
        throw std::invalid_argument("Input image must have type CV_64FC1.");
    }
    if (iterations < 0) {
        throw std::invalid_argument("The iteration count must not be negative.");
    }
    if (sigma <= 0.0) {
        throw std::invalid_argument("Sigma must be positive.");
    }
    if (lambda <= 0.0 || lambda > 0.25) {
        throw std::invalid_argument("Lambda must be in the interval (0, 0.25].");
    }

    cv::Mat current = input.clone();
    cv::Mat next = input.clone();
    const double sigmaSquared = sigma * sigma;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        // The border is kept unchanged because it does not have all four
        // neighbours required by the equation from the assignment.
        cv::parallel_for_(cv::Range(1, current.rows - 1),
                          [&](const cv::Range& rowRange) {
            for (int y = rowRange.start; y < rowRange.end; ++y) {
                const double* northRow = current.ptr<double>(y - 1);
                const double* currentRow = current.ptr<double>(y);
                const double* southRow = current.ptr<double>(y + 1);
                double* outputRow = next.ptr<double>(y);

                for (int x = 1; x < current.cols - 1; ++x) {
                    const double center = currentRow[x];
                    const double north = northRow[x];
                    const double south = southRow[x];
                    const double east = currentRow[x + 1];
                    const double west = currentRow[x - 1];

                    const double gradientNorth = north - center;
                    const double gradientSouth = south - center;
                    const double gradientEast = east - center;
                    const double gradientWest = west - center;

                    const double conductanceNorth = std::exp(
                        -(gradientNorth * gradientNorth) / sigmaSquared);
                    const double conductanceSouth = std::exp(
                        -(gradientSouth * gradientSouth) / sigmaSquared);
                    const double conductanceEast = std::exp(
                        -(gradientEast * gradientEast) / sigmaSquared);
                    const double conductanceWest = std::exp(
                        -(gradientWest * gradientWest) / sigmaSquared);

                    const double conductanceSum =
                        conductanceNorth + conductanceSouth +
                        conductanceEast + conductanceWest;

                    outputRow[x] =
                        center * (1.0 - lambda * conductanceSum) +
                        lambda * (conductanceNorth * north +
                                  conductanceSouth * south +
                                  conductanceEast * east +
                                  conductanceWest * west);
                }
            }
        });

        // The result becomes the input of the next iteration. The two buffers
        // are exchanged without copying their pixel data.
        std::swap(current, next);
    }

    return current;
}

int main(int argc, char* argv[])
{
    try {
        const ProgramOptions options = parseArguments(argc, argv);
        const double sigma = 0.015;
        const double lambda = 0.1;

        const cv::Mat input8 = cv::imread(options.inputPath, cv::IMREAD_GRAYSCALE);
        if (input8.empty()) {
            std::cerr << "Unable to read input image: " << options.inputPath << '\n';
            printUsage();
            return 1;
        }

        cv::Mat input64;
        input8.convertTo(input64, CV_64FC1, 1.0 / 255.0);

        std::cout << "Anisotropic filtering:\n"
                  << "  image: " << options.inputPath << '\n'
                  << "  iterations: " << options.iterations << '\n'
                  << "  sigma: " << sigma << '\n'
                  << "  lambda: " << lambda << '\n';

        const auto start = std::chrono::steady_clock::now();
        const cv::Mat result64 = anisotropicFilter(
            input64, options.iterations, sigma, lambda);
        const auto finish = std::chrono::steady_clock::now();

        cv::Mat result8;
        result64.convertTo(result8, CV_8UC1, 255.0);

        const std::string outputPath = "anisotropic_result.png";
        if (!cv::imwrite(outputPath, result8)) {
            std::cerr << "Unable to save output image: " << outputPath << '\n';
            return 1;
        }

        const std::chrono::duration<double> elapsed = finish - start;
        std::cout << "Finished in " << elapsed.count() << " seconds.\n"
                  << "Created: " << outputPath << '\n';

        if (options.showWindows) {
            cv::imshow("Original image", input8);
            cv::imshow("Anisotropic filter", result8);
            std::cout << "Press any key in an image window to close it.\n";
            cv::waitKey(0);
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        printUsage();
        return 1;
    }
}
