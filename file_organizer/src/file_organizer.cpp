// file_organizer.cpp

#include <iostream>
#include <filesystem>
#include <string>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <system_error>
#include <sstream>
#include <cstring>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <getopt.h>
#include <vector>
#include <algorithm>
#include <mutex>
#include <thread>
#include <atomic>

namespace fs = std::filesystem;

// Enumeration for time attributes
enum class TimeAttribute {
    Creation,
    Modification,
    Access
};

// Structure to hold size thresholds
struct SizeThresholds {
    uintmax_t small = 1 * 1024 * 1024;    // < 1 MB
    uintmax_t medium = 10 * 1024 * 1024;  // < 10 MB
    // 'large' is anything >= medium
};

// Function to categorize file size
std::string categorize_size(uintmax_t size, const SizeThresholds& thresholds) {
    if (size < thresholds.small) {
        return "small";
    } else if (size < thresholds.medium) {
        return "medium";
    } else {
        return "large";
    }
}

// Check if the system supports statx (Linux 4.11+)
#if defined(__linux__) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 17))
#define HAS_STATX 1
#else
#define HAS_STATX 0
#endif

#if HAS_STATX
#include <linux/stat.h>

// Define STATX_BTIME if not defined
#ifndef STATX_BTIME
#define STATX_BTIME 0x10000000000ULL
#endif

// Function to retrieve file creation time using statx
bool get_creation_time(const fs::path& file_path, std::chrono::system_clock::time_point& creation_time) {
    struct statx stx;
    memset(&stx, 0, sizeof(stx));

    int flags = AT_STATX_SYNC_TYPE;
    unsigned int mask = STATX_BTIME;

    // Convert fs::path to string
    std::string path_str = file_path.string();

    // Call statx syscall
    int ret = syscall(SYS_statx, AT_FDCWD, path_str.c_str(), flags, mask, &stx);
    if (ret == 0) {
        if (stx.stx_mask & STATX_BTIME) {
            // Convert stx_btime to time_point
            creation_time = std::chrono::system_clock::time_point(
                std::chrono::seconds(stx.stx_btime.tv_sec) +
                std::chrono::nanoseconds(stx.stx_btime.tv_nsec));
            return true;
        }
    }

    // If statx failed or birth time not available, return false
    return false;
}
#endif

// Function to retrieve file time based on user choice
std::chrono::system_clock::time_point get_file_time(const fs::path& file_path, TimeAttribute attr) {
    std::chrono::system_clock::time_point file_time;

    switch (attr) {
        case TimeAttribute::Creation:
#if HAS_STATX
            {
                bool success = get_creation_time(file_path, file_time);
                if (!success) {
                    std::cerr << "Warning: Creation time not available for \"" << file_path 
                              << "\". Falling back to last modification time.\n";
                }
                else {
                    return file_time;
                }
            }
#endif
            // Fallback to modification time
            // No break; continue to modification time
        case TimeAttribute::Modification:
            {
                std::error_code ec;
                auto ftime = fs::last_write_time(file_path, ec);
                if (ec) {
                    std::cerr << "Error: Unable to get modification time for \"" << file_path 
                              << "\": " << ec.message() << "\n";
                    return std::chrono::system_clock::now();
                }

                // Convert to time_point
                file_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                return file_time;
            }
            break;
        case TimeAttribute::Access:
            {
                struct stat sb;
                if (stat(file_path.c_str(), &sb) == 0) {
                    file_time = std::chrono::system_clock::time_point(
                        std::chrono::seconds(sb.st_atime) +
                        std::chrono::nanoseconds(sb.st_atim.tv_nsec));
                    return file_time;
                } else {
                    std::cerr << "Error: Unable to get access time for \"" << file_path 
                              << "\": " << strerror(errno) << "\n";
                    return std::chrono::system_clock::now();
                }
            }
            break;
        default:
            // Default to modification time
            {
                std::error_code ec;
                auto ftime = fs::last_write_time(file_path, ec);
                if (ec) {
                    std::cerr << "Error: Unable to get modification time for \"" << file_path 
                              << "\": " << ec.message() << "\n";
                    return std::chrono::system_clock::now();
                }

                // Convert to time_point
                file_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
                );
                return file_time;
            }
    }
}

// Function to get metadata-based directory path
std::string get_metadata_based_dir(const fs::path& file_path, TimeAttribute attr, const SizeThresholds& thresholds) {
    // Get the desired file time
    std::chrono::system_clock::time_point file_time = get_file_time(file_path, attr);

    // Convert to time_t for easy manipulation
    std::time_t cftime = std::chrono::system_clock::to_time_t(file_time);
    std::tm tm_ptr;
    localtime_r(&cftime, &tm_ptr); // Thread-safe version

    // Format date as YYYY/MM/DD
    std::ostringstream date_stream;
    date_stream << std::put_time(&tm_ptr, "%Y/%m/%d");

    // Get file size
    std::error_code ec;
    uintmax_t file_size = fs::file_size(file_path, ec);
    if (ec) {
        std::cerr << "Error: Unable to get file size for \"" << file_path 
                  << "\": " << ec.message() << "\n";
        file_size = 0;
    }

    // Categorize size
    std::string size_category = categorize_size(file_size, thresholds);

    // Construct metadata-based directory path
    fs::path metadata_subdir = fs::path(date_stream.str()) / size_category;

    return metadata_subdir.string();
}

// Function to display usage information
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] <source_directory>\n\n"
              << "Options:\n"
              << "  -h, --help                 Show this help message and exit\n"
              << "  -v, --verbose              Enable verbose output\n"
              << "  -d, --dry-run              Perform a trial run with no changes made\n"
              << "  -t, --time [creation|modification|access]\n"
              << "                             Specify the time attribute to organize by (default: creation)\n"
              << "  --small <size_in_MB>       Define the threshold for 'small' files (default: 1)\n"
              << "  --medium <size_in_MB>      Define the threshold for 'medium' files (default: 10)\n"
              << "\nExamples:\n"
              << "  " << program_name << " -v /path/to/source\n"
              << "  " << program_name << " --dry-run --time modification --small 2 --medium 20 /path/to/source\n";
}

// Function to parse size from string (in MB)
bool parse_size(const std::string& str, uintmax_t& size_out) {
    try {
        size_out = std::stoull(str) * 1024 * 1024;
        return true;
    } catch (...) {
        return false;
    }
}

// Function to move a single file
bool move_file(const fs::path& source_file, const fs::path& target_file, bool dry_run, bool verbose) {
    if (source_file == target_file) {
        if (verbose) {
            std::cout << "Skipping: \"" << source_file << "\" is already in the correct location.\n";
        }
        return true;
    }

    fs::path final_target = target_file;

    // Check if the target file already exists
    if (fs::exists(target_file)) {
        // Compare contents
        std::ifstream source_stream(source_file, std::ios::binary);
        std::ifstream target_stream(target_file, std::ios::binary);

        std::ostringstream source_contents;
        std::ostringstream target_contents;

        source_contents << source_stream.rdbuf();
        target_contents << target_stream.rdbuf();

        if (source_contents.str() != target_contents.str()) {
            // File contents differ, create a unique name
            int counter = 1;
            do {
                final_target = target_file.parent_path() /
                               (target_file.stem().string() + "_" + std::to_string(counter) + target_file.extension().string());
                counter++;
            } while (fs::exists(final_target));
        } else {
            if (verbose) {
                std::cout << "Skipping: \"" << source_file << "\" as it matches the existing file.\n";
            }
            return true; // Skip moving as the contents are identical
        }
    }

    if (dry_run) {
        std::cout << "[Dry-Run] Would move: \"" << source_file << "\" -> \"" << final_target << "\"\n";
        return true;
    }

    try {
        fs::rename(source_file, final_target);
        if (verbose) {
            std::cout << "Moved: \"" << source_file << "\" -> \"" << final_target << "\"\n";
        }
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error: Unable to move \"" << source_file << "\" -> \"" << final_target 
                  << "\": " << e.what() << "\n";
        return false;
    }
}


// Function to collect all files first
std::vector<fs::path> collect_all_files(const fs::path& src_directory, bool verbose) {
    std::vector<fs::path> files;
    try {
        for (auto const& entry : fs::recursive_directory_iterator(src_directory, fs::directory_options::skip_permission_denied)) {
            if (fs::is_regular_file(entry.status())) {
                files.emplace_back(entry.path());
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error during file collection: " << e.what() << "\n";
    }

    if (verbose) {
        std::cout << "Collected " << files.size() << " files for processing.\n";
    }

    return files;
}

// Main function to move files based on extension and metadata
void move_files_by_extension_and_metadata(const fs::path& src_directory, TimeAttribute attr, 
                                         const SizeThresholds& thresholds, bool dry_run, bool verbose) {
    // Collect all files first
    std::vector<fs::path> files = collect_all_files(src_directory, verbose);

    // Iterate over collected files
    for (const auto& file_path : files) {
        // Skip if the file no longer exists
        if (!fs::exists(file_path)) {
            if (verbose) {
                std::cout << "Skipping: \"" << file_path << "\" does not exist.\n";
            }
            continue;
        }

        std::string file_extension = file_path.has_extension() ? 
                                     file_path.extension().string().substr(1) : 
                                     "no_extension";

        // Define the extension directory
        fs::path extension_directory = src_directory / file_extension;

        // Get metadata-based subdirectory
        std::string metadata_subdir = get_metadata_based_dir(file_path, attr, thresholds);
        fs::path target_directory = extension_directory / metadata_subdir;

        // Define target file path
        fs::path target_file_path = target_directory / file_path.filename();

        // Create target directories if they don't exist
        std::error_code ec;
        if (!fs::exists(target_directory)) {
            if (dry_run) {
                if (verbose) {
                    std::cout << "[Dry-Run] Would create directory: \"" << target_directory << "\"\n";
                }
            } else {
                if (!fs::create_directories(target_directory, ec)) {
                    std::cerr << "Error: Unable to create directory \"" << target_directory 
                              << "\": " << ec.message() << "\n";
                    continue;
                } else if (verbose) {
                    std::cout << "Created directory: \"" << target_directory << "\"\n";
                }
            }
        }

        // Move the file
        move_file(file_path, target_file_path, dry_run, verbose);
    }
}

int main(int argc, char* argv[]) {
    // Default settings
    bool verbose = false;
    bool dry_run = false;
    TimeAttribute attr = TimeAttribute::Creation;
    SizeThresholds thresholds;

    // Define long options
    static struct option long_options[] = {
        {"help",        no_argument,       0, 'h'},
        {"verbose",     no_argument,       0, 'v'},
        {"dry-run",     no_argument,       0, 'd'},
        {"time",        required_argument, 0, 't'},
        {"small",       required_argument, 0,  1 },
        {"medium",      required_argument, 0,  2 },
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    // Parse command-line arguments
    while ((opt = getopt_long(argc, argv, "hvdt:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'v':
                verbose = true;
                break;
            case 'd':
                dry_run = true;
                break;
            case 't':
                if (std::string(optarg) == "creation") {
                    attr = TimeAttribute::Creation;
                } else if (std::string(optarg) == "modification") {
                    attr = TimeAttribute::Modification;
                } else if (std::string(optarg) == "access") {
                    attr = TimeAttribute::Access;
                } else {
                    std::cerr << "Error: Invalid time attribute \"" << optarg << "\". Choose from creation, modification, access.\n";
                    return 1;
                }
                break;
            case 1: // --small
                if (!parse_size(optarg, thresholds.small)) {
                    std::cerr << "Error: Invalid size for --small: \"" << optarg << "\"\n";
                    return 1;
                }
                break;
            case 2: // --medium
                if (!parse_size(optarg, thresholds.medium)) {
                    std::cerr << "Error: Invalid size for --medium: \"" << optarg << "\"\n";
                    return 1;
                }
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Check for source directory argument
    if (optind >= argc) {
        std::cerr << "Error: Source directory not specified.\n\n";
        print_usage(argv[0]);
        return 1;
    }

    fs::path src_directory = fs::absolute(argv[optind]);

    if (verbose) {
        std::cout << "Source Directory: \"" << src_directory << "\"\n";
        std::cout << "Verbose Mode: " << (verbose ? "Enabled" : "Disabled") << "\n";
        std::cout << "Dry-Run Mode: " << (dry_run ? "Enabled" : "Disabled") << "\n";
        std::cout << "Time Attribute: ";
        switch (attr) {
            case TimeAttribute::Creation:
                std::cout << "Creation Time\n";
                break;
            case TimeAttribute::Modification:
                std::cout << "Modification Time\n";
                break;
            case TimeAttribute::Access:
                std::cout << "Access Time\n";
                break;
        }
        std::cout << "Size Thresholds: Small < " << (thresholds.small / (1024 * 1024)) 
                  << " MB, Medium < " << (thresholds.medium / (1024 * 1024)) << " MB\n";
    }

    // Start organizing files
    move_files_by_extension_and_metadata(src_directory, attr, thresholds, dry_run, verbose);

    if (verbose) {
        std::cout << "File organization completed.\n";
    }

    return 0;
}

