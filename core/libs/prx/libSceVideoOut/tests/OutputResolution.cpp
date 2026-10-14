#include "prx/libSceVideoOut/include/Output.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

static void Require(bool value, const char* reason) {
    if (!value) {
        std::fprintf(stderr, "%s\n", reason);
        std::abort();
    }
}

static bool Parses(const char* value, std::uint32_t width, std::uint32_t height) {
    const auto parsed = ParseVideoOutOutputResolution_nid_no_patch(value);
    return parsed.has_value() && parsed->width == width && parsed->height == height;
}

static bool RejectsNamingTheVariable(const char* value) {
    try {
        ParseVideoOutOutputResolution_nid_no_patch(value);
    } catch (const std::runtime_error& error) {
        return std::string(error.what()).find("ANYPS5_OUTPUT_RESOLUTION") != std::string::npos;
    }
    return false;
}

int main() {
    Require(!ParseVideoOutOutputResolution_nid_no_patch(nullptr).has_value(), "an unset value selected an output resolution");
    Require(!ParseVideoOutOutputResolution_nid_no_patch("").has_value(), "an empty value selected an output resolution");
    Require(Parses("1280x720", 1280, 720), "1280x720 was not accepted");
    Require(Parses("1920x1080", 1920, 1080), "1920x1080 was not accepted");
    Require(Parses("2560x1440", 2560, 1440), "2560x1440 was not accepted");
    Require(Parses("3840x2160", 3840, 2160), "3840x2160 was not accepted");
    for (const char* value : {"1024x768", "1920x1200", "7680x4320", "720x1280", "1920X1080", "1920 x 1080", " 1920x1080", "1920x1080 ", "1920x1080\n", "1920x1080p", "1080p", "4k", "x", "1920x", "x1080", "0x0", "-1920x1080", "01920x1080", "3840x2160x1"}) {
        Require(RejectsNamingTheVariable(value), value);
    }
}
