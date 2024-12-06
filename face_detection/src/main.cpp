#include <iostream>
#include <filesystem>
#include <opencv2/opencv.hpp>
#include <opencv2/objdetect.hpp>
#include <algorithm>

namespace fs = std::filesystem;

// Function to check if the file is an image based on its extension (case-insensitive)
bool is_image(const fs::path &file_path)
{
    const std::vector<std::string> image_extensions = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tiff"};
    std::string ext = file_path.extension().string();
    // Convert extension to lowercase for case-insensitive comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    for (const auto &ext_str : image_extensions)
    {
        if (ext == ext_str)
        {
            return true;
        }
    }
    return false;
}

// Function to resize image while maintaining aspect ratio, with a max height of 960
cv::Mat resize_image_with_max_height(const cv::Mat &image, int max_height)
{
    // Get the original dimensions of the image
    int original_width = image.cols;
    int original_height = image.rows;

    // Calculate the scaling factor to maintain aspect ratio
    float scale_factor = static_cast<float>(max_height) / original_height;

    // If the scaling factor is less than 1, we need to resize
    if (scale_factor < 1.0)
    {
        int new_width = static_cast<int>(original_width * scale_factor);
        cv::Size new_size(new_width, max_height);

        // Resize the image
        cv::Mat resized_image;
        cv::resize(image, resized_image, new_size);
        return resized_image;
    }
    else
    {
        // If the image is already smaller than the max height, no resizing is needed
        return image;
    }
}

void detect_faces(const cv::Mat &image, cv::CascadeClassifier &face_cascade, std::vector<cv::Rect> &faces)
{
    // Convert image to grayscale for face detection
    cv::Mat gray;
    gray = resize_image_with_max_height(gray, 960);
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    // Apply Gaussian Blur to reduce noise and improve detection accuracy
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    // Equalize the histogram of the image to improve detection in varying lighting conditions
    cv::equalizeHist(gray, gray);

    // cv::resize(gray, resized_image, cv::Size(960, 540));

    // Detect faces in the image using adjusted parameters
    face_cascade.detectMultiScale(
        gray,
        faces,
        1.1,                         // scaleFactor: smaller value for more accurate scaling
        10,                          // minNeighbors: increased to reduce false positives
        0 | cv::CASCADE_SCALE_IMAGE, // flags
        cv::Size(60, 60)             // minSize: increased to ignore smaller detections
    );
}

// Recursive function to process files in a directory
void process_directory(const fs::path &dir_path, cv::CascadeClassifier &face_cascade, const fs::path &save_dir)
{
    try
    {
        for (const auto &entry : fs::recursive_directory_iterator(dir_path))
        {
            if (fs::is_regular_file(entry))
            {
                const fs::path &file_path = entry.path();

                // Check if the file is an image
                if (is_image(file_path))
                {
                    std::cout << "Processing image: " << file_path << std::endl;

                    // Load the image
                    cv::Mat image = cv::imread(file_path.string());
                    image = resize_image_with_max_height(image, 600);
                    if (image.empty())
                    {
                        std::cerr << "Could not open or find the image: " << file_path << std::endl;
                        continue;
                    }

                    // Detect faces in the image
                    std::vector<cv::Rect> faces;
                    detect_faces(image, face_cascade, faces);

                    // Draw rectangles around detected faces
                    for (const auto &face : faces)
                    {
                        cv::rectangle(image, face, cv::Scalar(255, 0, 0), 2);
                    }

                    // Output the results
                    if (!faces.empty())
                    {
                        std::cout << "Faces detected: " << faces.size() << std::endl;
                        for (const auto &face : faces)
                        {
                            std::cout << "Face at: x=" << face.x << ", y=" << face.y
                                      << ", width=" << face.width << ", height=" << face.height << std::endl;
                        }
                        // cv::Mat resized_image;
                        // cv::resize(image, resized_image, cv::Size(960, 540));
                        // cv::imshow("Detected Faces", resized_image);
                        cv::imshow("Detected Faces", image);

                        std::cout << "Press any key to continue to the next image...\n";
                        cv::waitKey(0); // Wait for a key press
                    }
                    else
                    {
                        std::cout << "No faces detected.\n";
                    }

                    // Display the image with detected faces

                    // Optional: Save the image with detected faces to the save directory
                    /*
                    if (!faces.empty()) {
                        fs::create_directories(save_dir); // Create the directory if it doesn't exist
                        fs::path save_path = save_dir / file_path.filename();
                        if (!cv::imwrite(save_path.string(), image)) {
                            std::cerr << "Failed to save the image to: " << save_path << std::endl;
                        } else {
                            std::cout << "Saved processed image to: " << save_path << std::endl;
                        }
                    }
                    */
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error processing directory: " << e.what() << std::endl;
    }
}

int main(int argc, char *argv[])
{
    // Check if the directory path is provided as a command-line argument
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <directory_path> [<save_directory>]\n";
        return -1;
    }

    fs::path dir_path = argv[1];
    fs::path save_dir;

    // Optional: Specify a directory to save processed images
    if (argc >= 3)
    {
        save_dir = argv[2];
    }

    // Check if the directory exists
    if (!fs::exists(dir_path) || !fs::is_directory(dir_path))
    {
        std::cerr << "The provided path is not a valid directory.\n";
        return -1;
    }

    // Load the pre-trained Haar Cascade classifier for face detection
    cv::CascadeClassifier face_cascade;
    std::string cascade_path = "haarcascade_frontalface_default.xml"; // Adjust the path if necessary
    if (!face_cascade.load(cv::samples::findFile(cascade_path)))
    {
        std::cerr << "Error loading face cascade from: " << cascade_path << std::endl;
        return -1;
    }

    // Start processing the directory
    process_directory(dir_path, face_cascade, save_dir);

    std::cout << "Processing completed.\n";
    return 0;
}
