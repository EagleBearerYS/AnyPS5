#ifndef AGC_GRAPHICS_BUFFER_RANGE_RECORDS_HPP
#define AGC_GRAPHICS_BUFFER_RANGE_RECORDS_HPP

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace AgcDriver::Graphics {

PFN_vkGetDeviceProcAddr BufferRangeDeviceProc(PFN_vkGetDeviceProcAddr real);
std::vector<std::string> BufferRangeReport(std::uint64_t address, std::uint64_t precision);

}

#endif
