#include "SceTypes.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include "tests/VideoOutTestEnvironment.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
int APS5_VABI sceVideoOutOpen(int userId, int busType, int index, const void* param);
int APS5_VABI sceVideoOutClose(int handle);
int APS5_VABI sceVideoOutGetOutputStatus(int handle, VideoOutOutputStatus* status);
int APS5_VABI sceVideoOutGetResolutionStatus(int handle, VideoOutResolutionStatus* status);
int APS5_VABI sceVideoOutInitializeOutputOptions(VideoOutOutputOptions* options);
int APS5_VABI sceVideoOutIsOutputSupported(int handle, uint64_t mode, const VideoOutOutputOptions* options, void* reservedPtr, uint64_t reserved);
int APS5_VABI sceVideoOutConfigureOutput(int handle, uint64_t mode, const VideoOutOutputOptions* options, void* reservedPtr, uint64_t reserved);
void APS5_VABI sceVideoOutSetBufferAttribute2(VideoOutBufferAttribute2* attribute, uint64_t pixelFormat, uint32_t tilingMode, uint32_t width, uint32_t height, uint64_t option, uint32_t dccControl, uint64_t dccCbRegisterClearColor);
int APS5_VABI sceVideoOutRegisterBuffers2(int handle, int setIndex, int bufferIndexStart, const VideoOutBuffers* buffers, int bufferNum, const VideoOutBufferAttribute2* attribute, int category, void* option);
int APS5_VABI sceVideoOutSubmitChangeBufferAttribute2(int handle, int setIndex, const VideoOutBufferAttribute2* attribute, void* option);
int APS5_VABI sceVideoOutUnregisterBuffers(int handle, int setIndex);
}

static constexpr int SYSTEM_USER = 255;
static constexpr int MAIN_BUS = 0;
static constexpr std::uint64_t PIXEL_FORMAT_B8G8R8A8_SRGB = 0x8000000000000000ull;
static constexpr std::uint32_t RESOLUTION_ULTRA_HD = 2;
static constexpr std::size_t TILE_BLOCK_BYTES = 65536;
static constexpr int INVALID_RESOLUTION = static_cast<int>(0x80290005u);
static constexpr int UNKNOWN_OUTPUT_MODE = static_cast<int>(0x8029001Eu);
static constexpr std::uint32_t RESOLUTION_DETECTION = 0x4u;
static constexpr std::uint32_t UNDECLARED_ATTRIBUTE3 = 0x8460440u;

struct Size {
    std::uint32_t width;
    std::uint32_t height;
};

static constexpr Size FullHd{1920, 1080};
static constexpr Size UltraHd{3840, 2160};

struct Expectation {
    bool check;
    Size output;
    bool takesUltraHd;
};

static void Require(bool value, const std::string& reason) {
    if (!value) {
        std::fprintf(stderr, "%s\n", reason.c_str());
        std::abort();
    }
}

static std::string Text(Size size) {
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

static std::string Code(int value) {
    if (value == INVALID_RESOLUTION) return "INVALID_RESOLUTION";
    if (value == UNKNOWN_OUTPUT_MODE) return "UNKNOWN_OUTPUT_MODE";
    char text[32];
    std::snprintf(text, sizeof(text), value < 0 ? "0x%08x" : "%d", static_cast<unsigned>(value));
    return text;
}

class Buffers {
    std::vector<std::byte> bytes;

public:
    explicit Buffers(Size size) : bytes(static_cast<std::size_t>((size.width + 255u) / 256u * 256u) * ((size.height + 255u) / 256u * 256u) * 4u + 2u * TILE_BLOCK_BYTES) {}

    void* Aligned() {
        const auto address = reinterpret_cast<std::uintptr_t>(bytes.data());
        return bytes.data() + (TILE_BLOCK_BYTES - (address % TILE_BLOCK_BYTES)) % TILE_BLOCK_BYTES;
    }
};

static VideoOutBufferAttribute2 Attribute(Size size) {
    VideoOutBufferAttribute2 attribute{};
    sceVideoOutSetBufferAttribute2(&attribute, PIXEL_FORMAT_B8G8R8A8_SRGB, 0, size.width, size.height, 0, 0, 0);
    return attribute;
}

static int Register(int handle, int setIndex, Buffers& buffers, Size size) {
    VideoOutBuffers buffer{buffers.Aligned(), nullptr, {nullptr, nullptr}};
    const auto attribute = Attribute(size);
    return sceVideoOutRegisterBuffers2(handle, setIndex, setIndex, &buffer, 1, &attribute, 0, nullptr);
}

static int Open() {
    const int handle = sceVideoOutOpen(SYSTEM_USER, MAIN_BUS, 0, nullptr);
    Require(handle > 0, "sceVideoOutOpen returned no handle");
    return handle;
}

static void Close(int handle, std::initializer_list<int> sets) {
    for (const int set : sets) Require(sceVideoOutUnregisterBuffers(handle, set) == 0, "buffer unregistration failed");
    Require(sceVideoOutClose(handle) == 0, "sceVideoOutClose failed");
}

static void StatusSized(const Expectation& expect) {
    const int handle = Open();
    VideoOutResolutionStatus status{};
    Require(sceVideoOutGetResolutionStatus(handle, &status) == 0, "sceVideoOutGetResolutionStatus failed");
    const Size reported{status.fullWidth, status.fullHeight};
    Buffers buffers(reported);
    const int result = Register(handle, 0, buffers, reported);
    std::printf("sequence status: sceVideoOutGetResolutionStatus %s -> sceVideoOutRegisterBuffers2 %s = %s\n", Text(reported).c_str(), Text(reported).c_str(), Code(result).c_str());
    if (expect.check) Require(reported.width == expect.output.width && reported.height == expect.output.height && result == 0, "the status-sized registration did not take the reported size");
    Close(handle, {0});
}

static void ClassSized(const Expectation& expect) {
    const int handle = Open();
    VideoOutOutputStatus status{};
    Require(sceVideoOutGetOutputStatus(handle, &status) == 0, "sceVideoOutGetOutputStatus failed");
    const Size chosen = status.resolution == RESOLUTION_ULTRA_HD ? UltraHd : FullHd;
    Buffers buffers(chosen);
    const int result = Register(handle, 0, buffers, chosen);
    std::printf("sequence class: sceVideoOutGetOutputStatus resolution %u -> sceVideoOutRegisterBuffers2 %s = %s\n", status.resolution, Text(chosen).c_str(), Code(result).c_str());
    if (expect.check) Require(result == 0 && (status.resolution == RESOLUTION_ULTRA_HD) == (expect.output.width >= 3840), "the class-sized registration did not follow the reported class");
    Close(handle, {0});
}

static void ModeProbes(const Expectation& expect) {
    const int handle = Open();
    VideoOutOutputOptions options{};
    Require(sceVideoOutInitializeOutputOptions(&options) == 0, "sceVideoOutInitializeOutputOptions failed");
    std::string line = "sequence modes: sceVideoOutIsOutputSupported";
    for (const std::uint64_t mode : {0x1ull, 0xFull, 0xD000000Aull, 0x4ull, 0x7ull, 0x8ull, 0xCull, 0xDull, 0xEull, 0x10ull, 0x11ull, 0x13ull}) {
        const int result = sceVideoOutIsOutputSupported(handle, mode, &options, nullptr, 0);
        char item[64];
        std::snprintf(item, sizeof(item), " 0x%llx=%s", static_cast<unsigned long long>(mode), Code(result).c_str());
        line += item;
        if (expect.check && mode == 0x1) Require(result == 1, "the default output mode is not supported");
        if (expect.check && mode == 0xD000000Aull) Require(result == UNKNOWN_OUTPUT_MODE, "0xd000000a is not an unknown output mode");
    }
    const int configured = sceVideoOutConfigureOutput(handle, 0x1, &options, nullptr, 0);
    std::printf("%s; sceVideoOutConfigureOutput 0x1 = %s\n", line.c_str(), Code(configured).c_str());
    if (expect.check) Require(configured == 0, "the default output mode was not configured");
    Require(sceVideoOutClose(handle) == 0, "sceVideoOutClose failed");
}

static void UltraHdThenFullHd(const Expectation& expect) {
    const int handle = Open();
    Buffers ultra(UltraHd);
    const int first = Register(handle, 0, ultra, UltraHd);
    int second = 0;
    Buffers full(FullHd);
    if (first == INVALID_RESOLUTION) second = Register(handle, 0, full, FullHd);
    const std::string fallback = first == INVALID_RESOLUTION ? ", then 1920x1080 = " + Code(second) : "";
    std::printf("sequence fallback: sceVideoOutRegisterBuffers2 3840x2160 = %s%s\n", Code(first).c_str(), fallback.c_str());
    if (expect.check) Require(expect.takesUltraHd ? first == 0 : first == INVALID_RESOLUTION && second == 0, "the 3840x2160 registration was not answered as a " + Text(expect.output) + " output answers it");
    Close(handle, {0});
}

static void FullHdThenUltraHd(const Expectation& expect) {
    const int handle = Open();
    Buffers full(FullHd);
    Buffers ultra(UltraHd);
    const int first = Register(handle, 0, full, FullHd);
    const int second = Register(handle, 1, ultra, UltraHd);
    std::printf("sequence two sets: sceVideoOutRegisterBuffers2 set 0 1920x1080 = %s, set 1 3840x2160 = %s\n", Code(first).c_str(), Code(second).c_str());
    if (expect.check) Require(first == 0 && second == (expect.takesUltraHd ? 0 : INVALID_RESOLUTION), "the second set was not answered as a " + Text(expect.output) + " output answers it");
    if (second == 0) Close(handle, {0, 1});
    else Close(handle, {0});
}

static void AttributeChange(const Expectation& expect) {
    const int handle = Open();
    Buffers full(UltraHd);
    Require(Register(handle, 0, full, FullHd) == 0, "the 1920x1080 registration failed");
    const auto attribute = Attribute(UltraHd);
    std::string answer;
    bool threw = false;
    try {
        answer = Code(sceVideoOutSubmitChangeBufferAttribute2(handle, 0, &attribute, nullptr));
    } catch (const std::runtime_error& error) {
        threw = true;
        answer = std::string("stops: ") + error.what();
        if (expect.check) Require(std::string(error.what()).find("ANYPS5_OUTPUT_RESOLUTION") != std::string::npos, "the refused attribute change does not name ANYPS5_OUTPUT_RESOLUTION");
    }
    std::printf("sequence change: sceVideoOutRegisterBuffers2 1920x1080 = 0, sceVideoOutSubmitChangeBufferAttribute2 3840x2160 %s\n", answer.c_str());
    if (expect.check) Require(expect.takesUltraHd ? !threw && answer == "0" : threw, "the 3840x2160 attribute change was not answered as expected for a " + Text(expect.output) + " output");
    Close(handle, {0});
}

int main(int argc, char** argv) {
    const std::string_view mode = argc >= 2 ? argv[1] : "";
    VideoOutTestEnvironment environment(mode.find("undeclared") == std::string_view::npos ? RESOLUTION_DETECTION : UNDECLARED_ATTRIBUTE3);
    const bool check = !(argc == 3 && std::string_view(argv[2]) == "print");
    Expectation expect{check, FullHd, true};
    if (mode == "unset") Require(std::getenv("ANYPS5_OUTPUT_RESOLUTION") == nullptr, "ANYPS5_OUTPUT_RESOLUTION is set for the unset case");
    else if (mode == "1280x720") expect = {check, {1280, 720}, false};
    else if (mode == "1920x1080") expect = {check, FullHd, false};
    else if (mode == "undeclared_1920x1080") expect = {check, FullHd, true};
    else if (mode == "2560x1440") expect = {check, {2560, 1440}, false};
    else if (mode == "3840x2160") expect = {check, UltraHd, true};
    else Require(false, "unknown test mode");
    try {
        Require(sceVideoOutClose(Open()) == 0, "sceVideoOutClose failed");
    } catch (const std::runtime_error& error) {
        if (std::getenv("ANYPS5_REQUIRE_DISPLAY") != nullptr) throw;
        std::printf("skipped, no display or Vulkan device: %s\n", error.what());
        return 77;
    }
    StatusSized(expect);
    ClassSized(expect);
    ModeProbes(expect);
    UltraHdThenFullHd(expect);
    FullHdThenUltraHd(expect);
    AttributeChange(expect);
    LibcRunShutdown_nid_postfix();
    std::printf("output resolution sequences %s\n", check ? "passed" : "printed");
}
