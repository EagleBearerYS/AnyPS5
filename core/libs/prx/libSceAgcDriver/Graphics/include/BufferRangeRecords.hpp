#ifndef AGC_GRAPHICS_BUFFER_RANGE_RECORDS_HPP
#define AGC_GRAPHICS_BUFFER_RANGE_RECORDS_HPP

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace AgcDriver::Graphics {

PFN_vkGetDeviceProcAddr BufferRangeDeviceProc(PFN_vkGetDeviceProcAddr real);
std::vector<std::string> BufferRangeReport(std::uint64_t address, std::uint64_t precision);
void NoteDeviceTable(std::uint64_t table, std::uint64_t tableBytes, std::uint64_t ring, std::uint64_t ringBytes);
void NoteDeviceTableHeader(std::uint32_t mask, std::uint32_t capacity);
void NoteTableMap(std::uint64_t identityHigh, std::uint64_t identityLow, std::uint32_t offset, std::uint32_t base, const std::uint32_t* written, const std::uint32_t* live);
std::vector<std::string> DeviceTableReport(std::uint64_t address);

}

#endif
