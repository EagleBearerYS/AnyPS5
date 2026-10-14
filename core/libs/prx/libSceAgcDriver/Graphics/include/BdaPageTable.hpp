#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDAPAGETABLE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDAPAGETABLE_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "BdaAbi.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace AgcDriver::Graphics {

class Buffer;

void ValidateBdaRangeList(std::span<const ShaderRecompiler::BdaAbi::Range> ranges);
void ValidateBdaGrants(std::span<const ShaderRecompiler::BdaAbi::Grant> grants);
void ValidateBdaPageRanges(std::span<const ShaderRecompiler::BdaAbi::Range> ranges);
std::vector<ShaderRecompiler::BdaAbi::PageEntry> EncodeBdaWindow(std::uint32_t window, std::span<const ShaderRecompiler::BdaAbi::Range> ranges);

class BdaPageTable {
public:
    struct Counts {
        std::uint64_t tables = 0;
        std::uint64_t encodedWindows = 0;
        std::uint64_t sharedWindows = 0;
    };
    static std::shared_ptr<const BdaPageTable> Build(const Context& context, std::span<const ShaderRecompiler::BdaAbi::Range> ranges);
    static Counts Totals();
    VkDeviceAddress Address() const;
    VkDeviceAddress LeafAddress(std::uint32_t window) const;

private:
    struct Leaf {
        std::uint32_t window = 0;
        std::vector<ShaderRecompiler::BdaAbi::Range> ranges;
        std::shared_ptr<Buffer> buffer;
    };
    BdaPageTable() = default;
    std::shared_ptr<const Leaf> find(std::uint32_t window) const;
    std::vector<std::shared_ptr<const Leaf>> leaves;
    std::shared_ptr<Buffer> first;
};

}

#endif
