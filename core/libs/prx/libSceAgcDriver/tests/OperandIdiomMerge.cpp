#include "IntermediateRepresentation/IrProgram.hpp"
#include "Optimization/OperandIdiomMerger.hpp"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <initializer_list>

using namespace ShaderRecompiler;

namespace {

struct Builder {
    IrProgram program;
    IrBlock* block = nullptr;

    Builder() {
        block = &newBlock();
        program.SetEntryBlock(*block);
    }

    IrBlock& newBlock() {
        auto& created = program.CreateBlock();
        program.BlockOrder().push_back(&created);
        return created;
    }

    IrValue& constant(std::uint32_t value) {
        auto& created = program.CreateValue(IrOpcode::Void, IrType::U32);
        created.SetImmediateU32(value);
        return created;
    }

    IrValue& emit(IrOpcode opcode, IrType type, std::initializer_list<IrValue*> arguments) {
        auto& created = program.CreateValue(opcode, type);
        for (auto* argument : arguments) created.AddArgument(argument);
        block->AppendInstruction(&created);
        return created;
    }

    IrValue& unknown() {
        return emit(IrOpcode::LaneId, IrType::U32, {});
    }

    IrValue& flush(IrValue& bits) {
        auto& exponent = emit(IrOpcode::BitwiseAnd32, IrType::U32, {&bits, &constant(0x7f800000u)});
        auto& denormal = emit(IrOpcode::IEqual32, IrType::Bool, {&exponent, &constant(0u)});
        auto& sign = emit(IrOpcode::BitwiseAnd32, IrType::U32, {&bits, &constant(0x80000000u)});
        return emit(IrOpcode::SelectU32, IrType::U32, {&denormal, &sign, &bits});
    }

    IrValue& isNan(IrValue& bits) {
        auto& magnitude = emit(IrOpcode::BitwiseAnd32, IrType::U32, {&bits, &constant(0x7fffffffu)});
        return emit(IrOpcode::UGreaterThan32, IrType::Bool, {&magnitude, &constant(0x7f800000u)});
    }

    IrValue& negate(IrValue& bits) {
        return emit(IrOpcode::BitwiseXor32, IrType::U32, {&bits, &constant(0x80000000u)});
    }

    IrValue& absolute(IrValue& bits) {
        return emit(IrOpcode::BitwiseAnd32, IrType::U32, {&bits, &constant(0x7fffffffu)});
    }

    IrValue& view(IrValue& bits) {
        return emit(IrOpcode::BitCastF32U32, IrType::F32, {&bits});
    }

    void keep(IrValue& value) {
        (void)emit(value.Type() == IrType::Bool ? IrOpcode::Reference : IrOpcode::ReferenceU32, IrType::Void, {&value});
    }

    OperandIdiomMergeStats merge() {
        return OperandIdiomMerger{}.Merge(program);
    }
};

bool expect(const char* name, bool condition) {
    if (!condition) std::fprintf(stderr, "%s\n", name);
    return condition;
}

bool run(const char* name, const std::function<bool()>& test) {
    try {
        return expect(name, test());
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s: %s\n", name, error.what());
        return false;
    }
}

}

int main() {
    bool passed = true;

    passed &= run("a value used three times carries one flush, one NaN test and one f32 view", [] {
        Builder b;
        auto& value = b.unknown();
        IrValue* flushes[3];
        IrValue* tests[3];
        IrValue* views[3];
        for (std::uint32_t use = 0; use < 3u; ++use) {
            flushes[use] = &b.flush(value);
            tests[use] = &b.isNan(*flushes[use]);
            views[use] = &b.view(*flushes[use]);
            b.keep(*tests[use]);
            b.keep(*views[use]);
        }
        const auto stats = b.merge();
        return stats.mergedFlushes == 2u && stats.mergedNanTests == 2u && stats.mergedBitCasts == 2u && tests[0]->UseCount() == 3u && views[0]->UseCount() == 3u &&
            !tests[1]->HasUses() && !tests[2]->HasUses() && !views[1]->HasUses() && !views[2]->HasUses() && !flushes[1]->HasUses() && !flushes[2]->HasUses();
    });

    passed &= run("flushes of different values stay apart", [] {
        Builder b;
        auto& first = b.flush(b.unknown());
        auto& second = b.flush(b.unknown());
        b.keep(first);
        b.keep(second);
        return !b.merge().Changed() && first.HasUses() && second.HasUses();
    });

    passed &= run("a NaN test of a value, its negation and its absolute value is one test", [] {
        Builder b;
        auto& value = b.unknown();
        auto& plain = b.isNan(value);
        auto& negated = b.isNan(b.negate(value));
        auto& magnitude = b.isNan(b.absolute(b.negate(value)));
        b.keep(plain);
        b.keep(negated);
        b.keep(magnitude);
        return b.merge().mergedNanTests == 2u && plain.UseCount() == 3u && !negated.HasUses() && !magnitude.HasUses();
    });

    passed &= run("a flush in a dominated block is the dominating flush, flushes in sibling blocks stay", [] {
        Builder b;
        IrBlock& head = *b.block;
        IrBlock& left = b.newBlock();
        IrBlock& right = b.newBlock();
        IrBlock& merge = b.newBlock();
        head.AddBranch(&left);
        head.AddBranch(&right);
        left.AddBranch(&merge);
        right.AddBranch(&merge);
        auto& shared = b.unknown();
        auto& branched = b.unknown();
        auto& atHead = b.flush(shared);
        b.keep(atHead);
        b.block = &left;
        auto& inLeft = b.flush(branched);
        b.keep(inLeft);
        b.block = &right;
        auto& inRight = b.flush(branched);
        b.keep(inRight);
        b.block = &merge;
        auto& atMerge = b.flush(shared);
        auto& afterBranches = b.flush(branched);
        b.keep(atMerge);
        b.keep(afterBranches);
        return b.merge().mergedFlushes == 1u && atHead.UseCount() == 2u && !atMerge.HasUses() && inLeft.HasUses() && inRight.HasUses() && afterBranches.HasUses();
    });

    passed &= run("a flush in a loop body does not stand for one after the loop, the header's does", [] {
        Builder b;
        IrBlock& entry = *b.block;
        IrBlock& header = b.newBlock();
        IrBlock& body = b.newBlock();
        IrBlock& exit = b.newBlock();
        entry.AddBranch(&header);
        header.AddBranch(&body);
        header.AddBranch(&exit);
        body.AddBranch(&header);
        auto& looped = b.unknown();
        auto& headed = b.unknown();
        b.block = &header;
        auto& inHeader = b.flush(headed);
        b.keep(inHeader);
        b.block = &body;
        auto& inBody = b.flush(looped);
        b.keep(inBody);
        b.block = &exit;
        auto& afterLoop = b.flush(looped);
        auto& afterHeader = b.flush(headed);
        b.keep(afterLoop);
        b.keep(afterHeader);
        return b.merge().mergedFlushes == 1u && inBody.HasUses() && afterLoop.HasUses() && !afterHeader.HasUses() && inHeader.UseCount() == 2u;
    });

    passed &= run("a block the entry does not reach keeps its flushes", [] {
        Builder b;
        auto& value = b.unknown();
        auto& reached = b.flush(value);
        b.keep(reached);
        IrBlock& unreached = b.newBlock();
        b.block = &unreached;
        auto& stray = b.flush(value);
        b.keep(stray);
        return !b.merge().Changed() && stray.HasUses();
    });

    passed &= run("quieting of one value is merged, of its negation is not", [] {
        Builder b;
        auto& value = b.unknown();
        auto& first = b.emit(IrOpcode::BitwiseOr32, IrType::U32, {&value, &b.constant(0x00400000u)});
        auto& second = b.emit(IrOpcode::BitwiseOr32, IrType::U32, {&b.constant(0x00400000u), &value});
        auto& negated = b.emit(IrOpcode::BitwiseOr32, IrType::U32, {&b.negate(value), &b.constant(0x00400000u)});
        b.keep(first);
        b.keep(second);
        b.keep(negated);
        return b.merge().mergedNanTests == 1u && first.UseCount() == 2u && !second.HasUses() && negated.HasUses();
    });

    return passed ? 0 : 1;
}
