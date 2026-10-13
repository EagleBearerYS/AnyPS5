#ifndef CORE_SHADER_RECOMPILER_APERTURELAYOUT_HPP
#define CORE_SHADER_RECOMPILER_APERTURELAYOUT_HPP

#include <cstdint>

namespace ShaderRecompiler::ApertureLayout {

inline constexpr std::uint32_t SharedHigh = 0x80000000u;
inline constexpr std::uint32_t PrivateHigh = 0x70000000u;

}

#endif
