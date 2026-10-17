#include "BdaShader.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "ControlFlow/RequestSerializer.hpp"
#include "SpirvBackend/SpirvBda.hpp"
#include <array>

namespace {

template<typename TAction>
void reject(TAction action, const char* expected) {
    try { action(); }
    catch (const std::runtime_error& error) {
        AgcDriver::Graphics::Require(std::string(error.what()).find(expected) != std::string::npos, std::string("unexpected contract error: ") + error.what());
        return;
    }
    throw std::runtime_error(std::string("missing contract rejection: ") + expected);
}

}

void RunBdaContractTests() {
    using namespace ShaderRecompiler;
    using AgcDriver::Graphics::SpirvStoresThroughAddresses;
    AgcDriver::Graphics::Require(!SpirvStoresThroughAddresses(MakeBdaTestShader(0x1000, 32)), "a BDA read was taken for a store through an address");
    AgcDriver::Graphics::Require(!SpirvStoresThroughAddresses(MakeBdaDwordReadTestShader(0x1000, 4, false, false)), "BDA dword reads were taken for a store through an address");
    AgcDriver::Graphics::Require(SpirvStoresThroughAddresses(MakeBdaStoreTestShader(0x1000, 7, 32, false)), "a BDA store was not seen");
    AgcDriver::Graphics::Require(SpirvStoresThroughAddresses(MakeBdaWriteTestShader(0x1000, 7, 32, false)), "a BDA write was not seen");
    const std::array<std::uint32_t, 2> stored{7u, 9u};
    AgcDriver::Graphics::Require(SpirvStoresThroughAddresses(MakeBdaDwordWriteTestShader(0x1000, static_cast<std::uint32_t>(stored.size()), stored.data())), "BDA dword writes were not seen");
    const std::array<std::uint32_t, 5> headerOnly{spv::MagicNumber, 0x00010300u, 0u, 1u, 0u};
    AgcDriver::Graphics::Require(!SpirvStoresThroughAddresses(headerOnly), "a module without physical pointers was taken for one storing through addresses");
    const std::array<std::uint32_t, 2> truncated{spv::MagicNumber, 0u};
    AgcDriver::Graphics::Require(SpirvStoresThroughAddresses(truncated), "an unreadable module was taken for one without stores");
    const std::array<std::uint32_t, 3> capabilities{spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess};
    const std::array<std::string_view, 2> extensions{"SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage"};
    SpirvTargetOptions target{0x00401000u, 0x00010300u, 32, BdaAbi::Version, capabilities, extensions};
    IrProgram program;
    program.Resources().stage = IrShaderStage::Compute;
    program.Info().usesDma = true;
    ValidateBdaTarget(program, target);
    target.bdaAbiVersion = 0;
    reject([&] { ValidateBdaTarget(program, target); }, "ABI version");
    target.bdaAbiVersion = BdaAbi::Version;
    target.supportedCapabilities = {};
    reject([&] { ValidateBdaTarget(program, target); }, "capability");
    target.supportedCapabilities = capabilities;
    target.supportedExtensions = {};
    reject([&] { ValidateBdaTarget(program, target); }, "extension");
    target.supportedExtensions = extensions;
    auto& block = program.CreateBlock();
    block.AppendInstruction(&program.CreateValue(IrOpcode::Barrier, IrType::Void));
    program.BlockOrder().push_back(&block);
    ValidateBdaTarget(program, target);
    AgcDriver::Graphics::Require(!BdaInvocationsMayStop(program), "a BDA program with barriers must keep its invocations running");
    program.Resources().stage = IrShaderStage::TessellationControl;
    ValidateBdaTarget(program, target);
    program.Resources().stage = IrShaderStage::Compute;
    RecompileRequest request{};
    request.target.bdaAbiVersion = BdaAbi::Version;
    request.target.supportedCapabilities = capabilities;
    request.target.supportedExtensions = extensions;
    request.target.fragmentShaderBarycentricEnabled = false;
    const RequestSerializer serializer;
    const auto encoded = serializer.Serialize(request);
    const auto decoded = serializer.Deserialize(encoded);
    AgcDriver::Graphics::Require(decoded.request.target.bdaAbiVersion == BdaAbi::Version && decoded.request.target.supportedCapabilities.size() == capabilities.size() && decoded.request.target.supportedExtensions[1] == extensions[1], "BDA request serialization changed target contract");
    auto invalid = encoded;
    invalid[0] = invalid[0] == 'A' ? 'B' : 'A';
    reject([&] { static_cast<void>(serializer.Deserialize(invalid)); }, "request signature");
    reject([&] { static_cast<void>(serializer.Deserialize("NVNQQWMAAAA=")); }, "serialization version");
}
