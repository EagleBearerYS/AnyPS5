#include "ApertureLayout.hpp"
#include "IntermediateRepresentation/IrProgram.hpp"
#include "RdnaDecoder/RdnaInstructionDecoder.hpp"
#include "RdnaDecoder/RdnaScalarOpDecoder.hpp"
#include "Translation/TranslationContext.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace ShaderRecompiler;

namespace {

struct ApertureSource {
    std::uint32_t code;
    RdnaOperandKind kind;
    const char* name;
};

const std::array<ApertureSource, 4> ApertureSources{{
    {235u, RdnaOperandKind::SrcSharedBase, "src_shared_base"},
    {236u, RdnaOperandKind::SrcSharedLimit, "src_shared_limit"},
    {237u, RdnaOperandKind::SrcPrivateBase, "src_private_base"},
    {238u, RdnaOperandKind::SrcPrivateLimit, "src_private_limit"},
}};

void Require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

RdnaInstruction ScalarMove(std::uint32_t opcode, std::uint32_t source) {
    const std::array<std::uint32_t, 2> words{0x80000000u | (0x7du << 23u) | (opcode << 8u) | source, 0u};
    return DecodeRdnaSop1(0u, words, 0u);
}

std::array<std::uint64_t, 2> TranslatedWords(const RdnaInstruction& instruction) {
    IrProgram program;
    auto& block = program.CreateBlock();
    program.SetEntryBlock(block);
    program.BlockOrder() = {&block};
    program.Metadata().blockInfo.resize(1);
    TranslationContext context(program, block, 256);
    context.TranslateInstruction(instruction);
    std::array<std::uint64_t, 2> words{~0ull, ~0ull};
    for (const auto* value : block.Instructions()) {
        if (value->Opcode() != IrOpcode::SetScalarRegister) continue;
        const auto reg = value->Argument(0)->Register();
        const auto* written = value->Argument(1)->Resolve();
        Require(reg.bank == RegisterBank::Scalar && reg.index < 2u, "the move wrote another register");
        Require(written->HasImmediate(), "the move wrote a value that is not a constant");
        words[reg.index] = written->ImmediateU32();
    }
    return words;
}

void CheckDecode() {
    for (const auto& source : ApertureSources) {
        const auto operand = DecodeRdnaScalarSource(source.code, 0u);
        Require(operand.kind == source.kind,
            "scalar source " + std::to_string(source.code) + " decoded to the wrong operand kind");
        Require(RdnaOperandToString(operand) == source.name,
            "scalar source " + std::to_string(source.code) + " is named " + RdnaOperandToString(operand));
    }
}

void CheckReadValues() {
    for (const auto& source : ApertureSources) {
        const bool base = source.kind == RdnaOperandKind::SrcSharedBase || source.kind == RdnaOperandKind::SrcPrivateBase;
        const bool shared = source.kind == RdnaOperandKind::SrcSharedBase || source.kind == RdnaOperandKind::SrcSharedLimit;
        const std::uint32_t low = base ? 0u : 0xffffffffu;
        const std::uint32_t high = shared ? ApertureLayout::SharedHigh : ApertureLayout::PrivateHigh;
        const RdnaInstruction move32 = ScalarMove(0x03u, source.code);
        Require(move32.op == RdnaOpcode::SMovB32 && move32.source0.kind == source.kind, std::string("s_mov_b32 of ") + source.name + " decoded wrongly");
        const auto words32 = TranslatedWords(move32);
        Require(words32[0] == low && words32[1] == ~0ull, std::string("s_mov_b32 of ") + source.name + " did not read the window's low word");
        const RdnaInstruction move64 = ScalarMove(0x04u, source.code);
        Require(move64.op == RdnaOpcode::SMovB64 && move64.source0.kind == source.kind, std::string("s_mov_b64 of ") + source.name + " decoded wrongly");
        const auto words64 = TranslatedWords(move64);
        Require(words64[0] == low && words64[1] == high, std::string("s_mov_b64 of ") + source.name + " did not read the window of the flat routing");
    }
}

}

int main() {
    CheckDecode();
    CheckReadValues();
    std::puts("aperture sources tests passed");
    return 0;
}
