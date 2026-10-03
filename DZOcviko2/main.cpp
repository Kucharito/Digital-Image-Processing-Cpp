#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

#include <opencv2/opencv.hpp>

// Manual 2-D convolution according to
// (f * h)(x, y) = sum_i sum_j f(x - i, y - j) * h(i, j).
//
// Pixels for which the kernel would reach outside the image are left zero.
// Both the image and the kernel must contain one channel of 32-bit floats.
cv::Mat convolution(const cv::Mat& input, const cv::Mat& kernel)
{
    if (input.empty()) {
        throw std::invalid_argument("Input image is empty.");
    }
    if (input.type() != CV_32FC1) {
        throw std::invalid_argument("Input image must have type CV_32FC1.");
    }
    if (kernel.empty() || kernel.type() != CV_32FC1) {
        throw std::invalid_argument("Kernel must be a non-empty CV_32FC1 matrix.");
    }
    if (kernel.rows % 2 == 0 || kernel.cols % 2 == 0) {
        throw std::invalid_argument("Kernel dimensions must be odd.");
    }
    if (kernel.rows > input.rows || kernel.cols > input.cols) {
        throw std::invalid_argument("Kernel must not be larger than the image.");
    }

    cv::Mat output = cv::Mat::zeros(input.size(), CV_32FC1);
    const int radiusY = kernel.rows / 2;
    const int radiusX = kernel.cols / 2;

    // Compute only pixels where the complete kernel lies inside the image.
    for (int y = radiusY; y < input.rows - radiusY; ++y) {
        for (int x = radiusX; x < input.cols - radiusX; ++x) {
            float sum = 0.0f;

            for (int j = -radiusY; j <= radiusY; ++j) {
                for (int i = -radiusX; i <= radiusX; ++i) {
                    const float pixel = input.at<float>(y - j, x - i);
                    const float weight = kernel.at<float>(j + radiusY,
                                                          i + radiusX);
                    sum += pixel * weight;
                }
            }

            output.at<float>(y, x) = sum;
        }
    }

    return output;
}

cv::Mat makeKernel(int rows,
                   int cols,
                   std::initializer_list<float> values,
                   float divisor)
{
    if (values.size() != static_cast<std::size_t>(rows * cols)) {
        throw std::invalid_argument("Incorrect number of kernel values.");
    }

    cv::Mat kernel(rows, cols, CV_32FC1);
    std::copy(values.begin(), values.end(), kernel.ptr<float>());
    kernel /= divisor;
    return kernel;
}

cv::Mat loadInputImage(const std::string& commandLinePath)
{
    if (!commandLinePath.empty()) {
        return cv::imread(commandLinePath, cv::IMREAD_COLOR);
    }

    // These alternatives allow the program to be launched from the workspace,
    // DZOcviko2, or DZOcviko2/build directory.
    const std::string candidates[] = {
        "images/lena.png",
        "../DZOcviko1/dzo_vsc/images/lena.png",
        "../../DZOcviko1/dzo_vsc/images/lena.png"
    };

    for (const std::string& path : candidates) {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file.good()) {
            continue;
        }

        cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
        if (!image.empty()) {
            return image;
        }
    }

    return cv::Mat();
}

int main(int argc, char* argv[])
{
    try {
        std::string inputPath;
        bool showWindows = true;

        for (int argument = 1; argument < argc; ++argument) {
            const std::string value = argv[argument];

            if (value == "--show") {
                showWindows = true;
            } else if (value == "--no-show") {
                showWindows = false;
            } else if (inputPath.empty()) {
                inputPath = value;
            } else {
                std::cerr << "Usage: dzo_convolution [path-to-image] "
                          << "[--show|--no-show]\n";
                return 1;
            }
        }

        const cv::Mat colorImage = loadInputImage(inputPath);

        if (colorImage.empty()) {
            std::cerr << "Unable to read the input image.\n"
                      << "Usage: dzo_convolution [path-to-image] "
                      << "[--show|--no-show]\n";
            return 1;
        }

        cv::Mat gray8;
        cv::Mat gray32;
        cv::cvtColor(colorImage, gray8, cv::COLOR_BGR2GRAY);
        gray8.convertTo(gray32, CV_32FC1, 1.0 / 255.0);

        const cv::Mat boxBlur3x3 = makeKernel(
            3, 3,
            {1, 1, 1,
             1, 1, 1,
             1, 1, 1},
            9.0f);

        const cv::Mat gaussianBlur3x3 = makeKernel(
            3, 3,
            {1, 2, 1,
             2, 4, 2,
             1, 2, 1},
            16.0f);

        const cv::Mat gaussianBlur5x5 = makeKernel(
            5, 5,
            {1,  4,  6,  4, 1,
             4, 16, 24, 16, 4,
             6, 24, 36, 24, 6,
             4, 16, 24, 16, 4,
             1,  4,  6,  4, 1},
            256.0f);

        const cv::Mat boxResult = convolution(gray32, boxBlur3x3);
        const cv::Mat gaussian3Result = convolution(gray32, gaussianBlur3x3);
        const cv::Mat gaussian5Result = convolution(gray32, gaussianBlur5x5);

        cv::Mat boxResult8;
        cv::Mat gaussian3Result8;
        cv::Mat gaussian5Result8;
        boxResult.convertTo(boxResult8, CV_8UC1, 255.0);
        gaussian3Result.convertTo(gaussian3Result8, CV_8UC1, 255.0);
        gaussian5Result.convertTo(gaussian5Result8, CV_8UC1, 255.0);

        const bool saved =
            cv::imwrite("box_blur_3x3.png", boxResult8) &&
            cv::imwrite("gaussian_blur_3x3.png", gaussian3Result8) &&
            cv::imwrite("gaussian_blur_5x5.png", gaussian5Result8);

        if (!saved) {
            std::cerr << "Unable to save one or more output images.\n";
            return 1;
        }

        std::cout << "Convolution finished. Created:\n"
                  << "  box_blur_3x3.png\n"
                  << "  gaussian_blur_3x3.png\n"
                  << "  gaussian_blur_5x5.png\n";

        if (showWindows) {
            cv::imshow("Original grayscale", gray8);
            cv::imshow("Box blur 3x3", boxResult8);
            cv::imshow("Gaussian blur 3x3", gaussian3Result8);
            cv::imshow("Gaussian blur 5x5", gaussian5Result8);
            std::cout << "Press any key in an image window to close it.\n";
            cv::waitKey(0);
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
