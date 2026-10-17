#include "SceTypes.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include "tests/VideoOutTestEnvironment.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
int APS5_VABI sceVideoOutOpen(int userId, int busType, int index, const void* param);
int APS5_VABI sceVideoOutClose(int handle);
int APS5_VABI sceVideoOutGetOutputStatus(int handle, VideoOutOutputStatus* status);
int APS5_VABI sceVideoOutGetOutputStatusWithVrr(int handle, void* status);
int APS5_VABI sceVideoOutGetResolutionStatus(int handle, VideoOutResolutionStatus* status);
void APS5_VABI sceVideoOutSetBufferAttribute2(VideoOutBufferAttribute2* attribute, uint64_t pixelFormat, uint32_t tilingMode, uint32_t width, uint32_t height, uint64_t option, uint32_t dccControl, uint64_t dccCbRegisterClearColor);
int APS5_VABI sceVideoOutRegisterBuffers2(int handle, int setIndex, int bufferIndexStart, const VideoOutBuffers* buffers, int bufferNum, const VideoOutBufferAttribute2* attribute, int category, void* option);
int APS5_VABI sceVideoOutSubmitChangeBufferAttribute2(int handle, int setIndex, const VideoOutBufferAttribute2* attribute, void* option);
void APS5_VABI sceVideoOutSetBufferAttribute(VideoOutBufferAttribute* attribute, uint32_t pixelFormat, uint32_t tilingMode, uint32_t aspectRatio, uint32_t width, uint32_t height, uint32_t pitchInPixel);
int APS5_VABI sceVideoOutRegisterBuffers(int handle, int bufferIndexStart, void* const* addresses, int bufferNum, const VideoOutBufferAttribute* attribute);
int APS5_VABI sceVideoOutUnregisterBuffers(int handle, int setIndex);
}

static constexpr int SYSTEM_USER = 255;
static constexpr int MAIN_BUS = 0;
static constexpr std::uint64_t PIXEL_FORMAT_B8G8R8A8_SRGB = 0x8000000000000000ull;
static constexpr std::uint32_t RESOLUTION_HD = 1;
static constexpr std::uint32_t RESOLUTION_ULTRA_HD = 2;
static constexpr std::size_t TILE_BLOCK_BYTES = 65536;
static constexpr std::uint32_t PS4_PIXEL_FORMAT_A8R8G8B8_SRGB = 0x80000000u;
static constexpr std::uint32_t PS4_TILING_LINEAR = 1;
static constexpr std::size_t PS4_BUFFER_ALIGNMENT = 4096;
static constexpr int VIDEO_OUT_ERROR_INVALID_RESOLUTION = static_cast<int>(0x80290005u);
static constexpr std::uint32_t RESOLUTION_DETECTION = 0x4u;
static constexpr std::uint32_t UNDECLARED_ATTRIBUTE3 = 0x8460440u;

static void Require(bool value, const char* reason) {
    if (!value) {
        std::fprintf(stderr, "%s\n", reason);
        std::abort();
    }
}

static void RequireReports(int handle, std::uint32_t width, std::uint32_t height, std::uint32_t resolution, const char* reason) {
    VideoOutResolutionStatus status;
    std::memset(&status, 0xff, sizeof(status));
    Require(sceVideoOutGetResolutionStatus(handle, &status) == 0, "sceVideoOutGetResolutionStatus failed");
    Require(status.fullWidth == width && status.fullHeight == height, reason);
    Require(status.paneWidth == width && status.paneHeight == height, reason);
    VideoOutOutputStatus output{};
    Require(sceVideoOutGetOutputStatus(handle, &output) == 0, "sceVideoOutGetOutputStatus failed");
    Require(output.resolution == resolution, reason);
    Require(status.refreshRate == output.refreshRate, "the resolution and output status refresh rates differ");
    std::array<std::byte, 80> vrr;
    vrr.fill(std::byte{0xff});
    Require(sceVideoOutGetOutputStatusWithVrr(handle, vrr.data()) == 0, "sceVideoOutGetOutputStatusWithVrr failed");
    Require(std::memcmp(vrr.data(), &output, sizeof(output)) == 0, "the VRR output status differs from sceVideoOutGetOutputStatus");
}

static int TryRegister(int handle, std::vector<std::byte>& allocation, std::uint32_t width, std::uint32_t height) {
    const auto address = reinterpret_cast<std::uintptr_t>(allocation.data());
    const auto offset = (TILE_BLOCK_BYTES - (address % TILE_BLOCK_BYTES)) % TILE_BLOCK_BYTES;
    VideoOutBuffers buffer{allocation.data() + offset, nullptr, {nullptr, nullptr}};
    VideoOutBufferAttribute2 attribute{};
    sceVideoOutSetBufferAttribute2(&attribute, PIXEL_FORMAT_B8G8R8A8_SRGB, 0, width, height, 0, 0, 0);
    return sceVideoOutRegisterBuffers2(handle, 0, 0, &buffer, 1, &attribute, 0, nullptr);
}

static void Register(int handle, std::vector<std::byte>& allocation, std::uint32_t width, std::uint32_t height) {
    Require(TryRegister(handle, allocation, width, height) == 0, "buffer registration failed");
}

static int RegisterPs4(int handle, int bufferIndexStart, std::vector<std::byte>& allocation, std::uint32_t width, std::uint32_t height) {
    const auto address = reinterpret_cast<std::uintptr_t>(allocation.data());
    const auto offset = (PS4_BUFFER_ALIGNMENT - (address % PS4_BUFFER_ALIGNMENT)) % PS4_BUFFER_ALIGNMENT;
    void* const addresses[] = {allocation.data() + offset};
    VideoOutBufferAttribute attribute{};
    sceVideoOutSetBufferAttribute(&attribute, PS4_PIXEL_FORMAT_A8R8G8B8_SRGB, PS4_TILING_LINEAR, 0, width, height, width);
    return sceVideoOutRegisterBuffers(handle, bufferIndexStart, addresses, 1, &attribute);
}

static void RequirePs4BuffersFollowTheSetting(int handle, std::vector<std::byte>& allocation) {
    RequireReports(handle, 1280, 720, RESOLUTION_HD, "1280x720 was not reported before a PS4 registration");
    Require(RegisterPs4(handle, 0, allocation, 1280, 720) == 0, "the 1280x720 PS4 registration did not take set 0");
    RequireReports(handle, 1280, 720, RESOLUTION_HD, "1280x720 was not reported after a 1280x720 PS4 registration");
    std::vector<std::byte> smaller(640 * 360 * 4 + PS4_BUFFER_ALIGNMENT);
    Require(RegisterPs4(handle, 1, smaller, 640, 360) == 1, "the 640x360 PS4 registration did not take set 1");
    RequireReports(handle, 1280, 720, RESOLUTION_HD, "1280x720 was not reported after a 640x360 PS4 registration");
    std::vector<std::byte> larger(2560 * 1440 * 4 + PS4_BUFFER_ALIGNMENT);
    std::string answer;
    try {
        answer = "taken as set " + std::to_string(RegisterPs4(handle, 2, larger, 2560, 1440));
    } catch (const std::runtime_error& error) {
        answer = error.what();
    }
    Require(answer.find("ANYPS5_OUTPUT_RESOLUTION") != std::string::npos, ("2560x1440 PS4 buffers on a 1280x720 output did not stop with an error naming ANYPS5_OUTPUT_RESOLUTION: " + answer).c_str());
    RequireReports(handle, 1280, 720, RESOLUTION_HD, "1280x720 was not reported after refused 2560x1440 PS4 buffers");
    Require(sceVideoOutUnregisterBuffers(handle, 1) == 0, "PS4 buffer unregistration failed");
}

static void RequireUndeclaredTitleIsTreatedAsUnset(int handle, std::vector<std::byte>& allocation) {
    RequireReports(handle, 1920, 1080, RESOLUTION_HD, "a title without resolution detection was not told 1920x1080 before registration, as without the setting");
    Require(TryRegister(handle, allocation, 3840, 64) == 0, "3840x64 buffers of a title without resolution detection were not taken on a 1280x720 output");
    RequireReports(handle, 3840, 64, RESOLUTION_ULTRA_HD, "the output of a title without resolution detection did not follow its 3840x64 buffers, as without the setting");
    VideoOutBufferAttribute2 attribute{};
    sceVideoOutSetBufferAttribute2(&attribute, PIXEL_FORMAT_B8G8R8A8_SRGB, 0, 3840, 128, 0, 0, 0);
    Require(sceVideoOutSubmitChangeBufferAttribute2(handle, 0, &attribute, nullptr) == 0, "the 3840x128 attribute change of a title without resolution detection failed on a 1280x720 output");
    RequireReports(handle, 3840, 64, RESOLUTION_ULTRA_HD, "the attribute change of a title without resolution detection changed the reported output, which it does not without the setting");
}

static void RequireUndeclaredPs4BuffersAreTaken(int handle) {
    RequireReports(handle, 1920, 1080, RESOLUTION_HD, "a title without resolution detection was not told 1920x1080 before a PS4 registration, as without the setting");
    std::vector<std::byte> larger(2560 * 1440 * 4 + PS4_BUFFER_ALIGNMENT);
    Require(RegisterPs4(handle, 0, larger, 2560, 1440) == 0, "2560x1440 PS4 buffers of a title without resolution detection were not taken as set 0 on a 1280x720 output");
    RequireReports(handle, 2560, 1440, RESOLUTION_HD, "the output of a title without resolution detection did not follow its 2560x1440 PS4 buffers, as without the setting");
}

static int RequireInvalidValueStopsOpen() {
    try {
        sceVideoOutOpen(SYSTEM_USER, MAIN_BUS, 0, nullptr);
    } catch (const std::runtime_error& error) {
        Require(std::string(error.what()).find("ANYPS5_OUTPUT_RESOLUTION") != std::string::npos, "the error for an invalid output resolution does not name ANYPS5_OUTPUT_RESOLUTION");
        return 0;
    }
    Require(false, "an invalid ANYPS5_OUTPUT_RESOLUTION did not stop sceVideoOutOpen");
    return 1;
}

int main(int argc, char** argv) {
    const std::string_view mode = argc == 2 ? argv[1] : "";
    VideoOutTestEnvironment environment(mode.find("undeclared") == std::string_view::npos ? RESOLUTION_DETECTION : UNDECLARED_ATTRIBUTE3);
    if (mode == "invalid") return RequireInvalidValueStopsOpen();
    int handle = 0;
    try {
        handle = sceVideoOutOpen(SYSTEM_USER, MAIN_BUS, 0, nullptr);
    } catch (const std::runtime_error& error) {
        if (std::getenv("ANYPS5_REQUIRE_DISPLAY") != nullptr) throw;
        std::printf("skipped, no display or Vulkan device: %s\n", error.what());
        return 77;
    }
    Require(handle > 0, "sceVideoOutOpen returned no handle");

    std::vector<std::byte> allocation(60 * TILE_BLOCK_BYTES + TILE_BLOCK_BYTES - 1);
    if (mode == "unset") {
        Require(std::getenv("ANYPS5_OUTPUT_RESOLUTION") == nullptr, "ANYPS5_OUTPUT_RESOLUTION is set for the unset case");
        RequireReports(handle, 1920, 1080, RESOLUTION_HD, "without the setting the output was not 1920x1080 before registration");
        Register(handle, allocation, 3840, 64);
        RequireReports(handle, 3840, 64, RESOLUTION_ULTRA_HD, "without the setting the output did not follow the registered buffers");
    } else if (mode == "1280x720") {
        RequireReports(handle, 1280, 720, RESOLUTION_HD, "1280x720 was not reported before registration");
        Register(handle, allocation, 1280, 720);
        RequireReports(handle, 1280, 720, RESOLUTION_HD, "1280x720 was not reported after a 1280x720 registration");
    } else if (mode == "undeclared_1280x720") {
        RequireUndeclaredTitleIsTreatedAsUnset(handle, allocation);
    } else if (mode == "ps4_undeclared_1280x720") {
        RequireUndeclaredPs4BuffersAreTaken(handle);
    } else if (mode == "ps4_1280x720") {
        RequirePs4BuffersFollowTheSetting(handle, allocation);
    } else if (mode == "1920x1080") {
        RequireReports(handle, 1920, 1080, RESOLUTION_HD, "1920x1080 was not reported before registration");
        Require(TryRegister(handle, allocation, 3840, 64) == VIDEO_OUT_ERROR_INVALID_RESOLUTION, "3840x64 buffers were not refused with INVALID_RESOLUTION on a 1920x1080 output");
        Register(handle, allocation, 1920, 64);
        RequireReports(handle, 1920, 1080, RESOLUTION_HD, "1920x1080 was not reported after a 1920x64 registration");
    } else if (mode == "2560x1440") {
        RequireReports(handle, 2560, 1440, RESOLUTION_HD, "2560x1440 was not reported before registration");
        Require(TryRegister(handle, allocation, 3840, 64) == VIDEO_OUT_ERROR_INVALID_RESOLUTION, "3840x64 buffers were not refused with INVALID_RESOLUTION on a 2560x1440 output");
        RequireReports(handle, 2560, 1440, RESOLUTION_HD, "2560x1440 was not reported after a refused 3840x64 registration");
        Register(handle, allocation, 2560, 64);
        RequireReports(handle, 2560, 1440, RESOLUTION_HD, "2560x1440 was not reported after a 2560x64 registration");
    } else if (mode == "3840x2160") {
        RequireReports(handle, 3840, 2160, RESOLUTION_ULTRA_HD, "3840x2160 was not reported before registration");
        Register(handle, allocation, 259, 137);
        RequireReports(handle, 3840, 2160, RESOLUTION_ULTRA_HD, "3840x2160 was not reported after a 259x137 registration");
    } else {
        Require(false, "unknown test mode");
    }

    Require(sceVideoOutUnregisterBuffers(handle, 0) == 0, "buffer unregistration failed");
    Require(sceVideoOutClose(handle) == 0, "sceVideoOutClose failed");
    LibcRunShutdown_nid_postfix();
}
