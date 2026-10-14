#include <codegen/x86/RedZoneFramer.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <set>

namespace Codegen {

namespace {

constexpr std::uint8_t kRegRsp = 4;
constexpr std::uint8_t kRegRbp = 5;
constexpr std::size_t kJumpBytes = 5;
constexpr std::int64_t kRedZoneBytes = 0x80;
constexpr std::uint8_t kLowerRsp[] = {0x48, 0x8d, 0x64, 0x24, 0x80};
constexpr std::uint8_t kRaiseRsp[] = {0x48, 0x8d, 0xa4, 0x24, 0x80, 0x00, 0x00, 0x00};
constexpr std::uint8_t kJumpBack[] = {0xe9, 0x00, 0x00, 0x00, 0x00};

struct Insn {
    std::size_t Offset;
    std::size_t Length;
    DecodedInstructionInfo Info;
};

std::optional<std::uint8_t> pushedRegister(const std::uint8_t* code, const Insn& insn) {
    if (insn.Length == 1 && code[0] >= 0x50 && code[0] <= 0x57 && code[0] != 0x50 + kRegRsp) return static_cast<std::uint8_t>(code[0] - 0x50);
    if (insn.Length == 2 && code[0] == 0x41 && code[1] >= 0x50 && code[1] <= 0x57) return static_cast<std::uint8_t>(code[1] - 0x50 + 8);
    return std::nullopt;
}

std::optional<std::uint8_t> poppedRegister(const std::uint8_t* code, const Insn& insn) {
    if (insn.Length == 1 && code[0] >= 0x58 && code[0] <= 0x5f && code[0] != 0x58 + kRegRsp) return static_cast<std::uint8_t>(code[0] - 0x58);
    if (insn.Length == 2 && code[0] == 0x41 && code[1] >= 0x58 && code[1] <= 0x5f) return static_cast<std::uint8_t>(code[1] - 0x58 + 8);
    return std::nullopt;
}

struct MemoryUse {
    bool UsesRsp = false;
    bool RegisterRsp = false;
    bool RbpBased = false;
    std::int64_t RbpDisp = 0;
    bool RspBased = false;
    std::int64_t RspDisp = 0;
    std::uint8_t Mod = 0;
    std::size_t DispOffset = 0;
};

void extensionBits(const std::uint8_t* code, const Insn& insn, bool& r, bool& b) {
    const auto& info = insn.Info;
    r = b = false;
    if (info.RexPrefix != 0) {
        r = (info.RexPrefix & 4) != 0;
        b = (info.RexPrefix & 1) != 0;
        return;
    }
    const auto lead = code[info.OpcodeOffset];
    if ((lead == 0xc4 || lead == 0x8f || lead == 0x62) && info.OpcodeOffset + 1 < insn.Length) {
        r = (code[info.OpcodeOffset + 1] & 0x80) == 0;
        b = (code[info.OpcodeOffset + 1] & 0x20) == 0;
    } else if (lead == 0xc5 && info.OpcodeOffset + 1 < insn.Length) {
        r = (code[info.OpcodeOffset + 1] & 0x80) == 0;
    }
}

bool twoByteRegisterIsGeneral(const std::uint8_t op) {
    return (op >= 0x40 && op <= 0x4f) || op == 0xa3 || op == 0xa4 || op == 0xa5 || op == 0xab || op == 0xac || op == 0xad || op == 0xaf || op == 0xb0 || op == 0xb1 || op == 0xb3 || op == 0xb6 || op == 0xb7 || op == 0xb8 || (op >= 0xbb && op <= 0xbf) || op == 0xc0 || op == 0xc1 || op == 0xc3;
}

bool twoByteRmIsGeneral(const std::uint8_t op) {
    return twoByteRegisterIsGeneral(op) || op == 0x6e || op == 0x7e || op == 0xba;
}

bool vexGeneralUsesRsp(const std::uint8_t* code, const Insn& insn, const std::uint8_t mod, const std::uint8_t reg, const std::uint8_t rm, const bool r, const bool b) {
    const auto at = insn.Info.OpcodeOffset;
    const auto lead = code[at];
    if ((lead != 0xc4 && lead != 0xc5) || at + 3 >= insn.Length) return false;
    const auto map = lead == 0xc5 ? std::uint8_t{1} : static_cast<std::uint8_t>(code[at + 1] & 0x1f);
    const auto vvvv = static_cast<std::uint8_t>((~code[lead == 0xc5 ? at + 1 : at + 2] >> 3) & 0xf);
    const auto op = code[lead == 0xc5 ? at + 2 : at + 3];
    const bool general = (map == 2 && (op == 0xf2 || op == 0xf3 || op == 0xf5 || op == 0xf6 || op == 0xf7)) || (map == 3 && op == 0xf0);
    return general && ((reg == kRegRsp && !r) || (mod == 3 && rm == kRegRsp && !b) || vvvv == kRegRsp);
}

MemoryUse memoryUse(const std::uint8_t* code, const Insn& insn) {
    MemoryUse use;
    const auto& info = insn.Info;
    if (!info.HasModRm || info.ModRmOffset == 0 || info.ModRmOffset >= insn.Length) return use;
    bool r = false;
    bool b = false;
    extensionBits(code, insn, r, b);
    const auto modrm = info.ModRmByte;
    const auto mod = static_cast<std::uint8_t>(modrm >> 6);
    const auto reg = static_cast<std::uint8_t>((modrm >> 3) & 7);
    const auto rm = static_cast<std::uint8_t>(modrm & 7);
    const bool vex = info.RexPrefix == 0 && (code[info.OpcodeOffset] == 0xc4 || code[info.OpcodeOffset] == 0xc5 || code[info.OpcodeOffset] == 0x62 || code[info.OpcodeOffset] == 0x8f);
    if (!vex && !info.IsTwoByteOpcode && reg == kRegRsp && !r) {
        const auto op = info.Opcode;
        if ((op <= 0x3f && (op & 7) <= 3) || (op >= 0x84 && op <= 0x8b) || op == 0x8d) use.UsesRsp = use.RegisterRsp = true;
    }
    if (!vex && info.IsTwoByteOpcode && ((reg == kRegRsp && !r && twoByteRegisterIsGeneral(info.Opcode)) || (mod == 3 && rm == kRegRsp && !b && twoByteRmIsGeneral(info.Opcode)))) use.UsesRsp = use.RegisterRsp = true;
    if (vex && vexGeneralUsesRsp(code, insn, mod, reg, rm, r, b)) use.UsesRsp = use.RegisterRsp = true;
    if (mod == 3) {
        const bool generalPurpose = !vex && !info.IsTwoByteOpcode && !(info.Opcode >= 0xd8 && info.Opcode <= 0xdf);
        if (generalPurpose && rm == kRegRsp && !b) use.UsesRsp = use.RegisterRsp = true;
        return use;
    }
    std::size_t at = info.ModRmOffset + 1;
    std::uint8_t base = rm;
    bool baseExtended = b;
    if (rm == 4) {
        if (at >= insn.Length) return use;
        const auto sib = code[at++];
        base = sib & 7;
        if (mod == 0 && base == 5) return use;
    } else if (mod == 0 && rm == 5) {
        return use;
    }
    std::int64_t disp = 0;
    if (mod == 1 && at < insn.Length) disp = static_cast<std::int8_t>(code[at]);
    if (mod == 2 && at + 4 <= insn.Length) {
        std::int32_t value = 0;
        std::memcpy(&value, code + at, 4);
        disp = value;
    }
    if (base == kRegRsp && !baseExtended) {
        use.UsesRsp = true;
        use.RspBased = true;
        use.RspDisp = disp;
        use.Mod = mod;
        use.DispOffset = at;
    }
    if (base == kRegRbp && !baseExtended && mod != 0) {
        use.RbpBased = true;
        use.RbpDisp = disp;
    }
    return use;
}

std::optional<std::vector<std::uint8_t>> movedDisplacement(const MemoryUse& use) {
    const auto moved = use.RspDisp + kRedZoneBytes;
    if (use.Mod == 1 && use.RspDisp < 0 && moved <= std::numeric_limits<std::int8_t>::max()) return std::vector<std::uint8_t>{static_cast<std::uint8_t>(moved)};
    if (use.Mod == 2 && moved <= std::numeric_limits<std::int32_t>::max()) {
        const auto value = static_cast<std::uint32_t>(static_cast<std::int32_t>(moved));
        return std::vector<std::uint8_t>{static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value >> 16), static_cast<std::uint8_t>(value >> 24)};
    }
    return std::nullopt;
}

void frameSegment(
    const std::vector<std::uint8_t>& fileBytes,
    const Domain::ProgramHeader& ph,
    const std::vector<TrampolineSite>& existing,
    std::vector<TrampolineSite>& sites,
    std::vector<RedZoneDisplacement>& displacements,
    RedZoneFrameReport& report
) {
    const auto segOffset = static_cast<std::size_t>(ph.Offset);
    const auto segSize = static_cast<std::size_t>(ph.FileSize);
    if (segOffset > fileBytes.size() || segSize > fileBytes.size() - segOffset) return;
    const std::vector<std::uint8_t> seg(fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset), fileBytes.begin() + static_cast<std::ptrdiff_t>(segOffset + segSize));
    const auto matches = MakeInstructionScanner()->ScanCodeSection(seg, 0, seg.size());
    const X64InstructionDecoder decoder;
    std::vector<Insn> insns;
    insns.reserve(matches.size());
    for (const auto& match : matches) insns.push_back({static_cast<std::size_t>(match.Offset), match.Length, decoder.DecodeInstruction(seg.data() + match.Offset, match.Length)});

    std::set<std::size_t> targets;
    std::set<std::size_t> starts;
    for (std::size_t i = 0; i < insns.size(); ++i) {
        const auto& insn = insns[i];
        if (insn.Info.HasBranchTarget && !insn.Info.HasRipRelativeDisp) {
            const auto target = static_cast<std::int64_t>(insn.Offset + insn.Length) + insn.Info.BranchDisp;
            if (target >= 0 && static_cast<std::size_t>(target) < seg.size()) {
                if (insn.Info.FlowKind == ControlFlowKind::Call) starts.insert(static_cast<std::size_t>(target));
                else targets.insert(static_cast<std::size_t>(target));
            }
        }
        if (i > 0 && insns[i - 1].Length == 1 && seg[insns[i - 1].Offset] == 0xcc && seg[insn.Offset] != 0xcc && insn.Offset % 16 == 0) starts.insert(insn.Offset);
    }
    std::vector<std::pair<std::size_t, std::size_t>> taken;
    for (const auto& site : existing) {
        if (site.Address < ph.MappedAddress || site.Address >= ph.MappedAddress + segSize) continue;
        const auto at = static_cast<std::size_t>(site.Address - ph.MappedAddress);
        taken.emplace_back(at, at + site.Length);
    }
    const auto overlapsExisting = [&](const std::size_t begin, const std::size_t end) {
        return std::any_of(taken.begin(), taken.end(), [&](const auto& range) { return begin < range.second && range.first < end; });
    };
    const auto targetInside = [&](const std::size_t begin, const std::size_t end) {
        const auto hit = targets.upper_bound(begin);
        return hit != targets.end() && *hit < end;
    };
    const auto indexOf = [&](const std::size_t offset) -> std::optional<std::size_t> {
        const auto found = std::lower_bound(insns.begin(), insns.end(), offset, [](const Insn& insn, const std::size_t value) { return insn.Offset < value; });
        if (found == insns.end() || found->Offset != offset) return std::nullopt;
        return static_cast<std::size_t>(found - insns.begin());
    };
    const auto padding = [&](const Insn& insn) {
        if (insn.Info.IsTwoByteOpcode) return insn.Info.Opcode == 0x1f;
        return seg[insn.Offset + insn.Info.OpcodeOffset] == 0x90 && (insn.Info.RexPrefix & 1) == 0;
    };
    const auto jumpTarget = [](const Insn& insn) -> std::optional<std::size_t> {
        if (insn.Info.FlowKind != ControlFlowKind::UnconditionalJump || !insn.Info.HasBranchTarget || insn.Info.HasRipRelativeDisp) return std::nullopt;
        const auto target = static_cast<std::int64_t>(insn.Offset + insn.Length) + insn.Info.BranchDisp;
        if (target < 0) return std::nullopt;
        return static_cast<std::size_t>(target);
    };

    for (auto it = starts.begin(); it != starts.end(); ++it) {
        const auto start = *it;
        const auto next = std::next(it);
        const auto limit = next == starts.end() ? seg.size() : *next;
        const auto first = indexOf(start);
        if (!first) continue;
        ++report.Functions;
        const auto skip = [&](const char* reason) { ++report.Skipped[reason]; };

        std::vector<std::size_t> body;
        bool afterReturn = false;
        std::optional<std::size_t> jumpedTo;
        for (auto i = *first; i < insns.size() && insns[i].Offset < limit; ++i) {
            if (seg[insns[i].Offset] == 0xcc && insns[i].Length == 1) break;
            if (!padding(insns[i])) {
                if (insns[i].Offset % 16 == 0 && (afterReturn || jumpedTo == insns[i].Offset)) break;
                afterReturn = insns[i].Info.FlowKind == ControlFlowKind::Return || insns[i].Info.FlowKind == ControlFlowKind::Trap;
                jumpedTo = jumpTarget(insns[i]);
            }
            body.push_back(i);
        }
        if (body.size() < 3) continue;
        const auto at = [&](const std::size_t k) { return seg.data() + insns[body[k]].Offset; };
        const bool rbpPrologue = insns[body[0]].Length == 1 && at(0)[0] == 0x55 && insns[body[1]].Length == 3 && std::memcmp(at(1), "\x48\x89\xe5", 3) == 0;
        std::vector<std::uint8_t> pushed;
        std::size_t p = rbpPrologue ? 2 : 0;
        while (p < body.size())
            if (const auto reg = pushedRegister(at(p), insns[body[p]])) { pushed.push_back(*reg); ++p; } else break;

        bool leaf = true;
        bool redZone = false;
        const char* reason = nullptr;
        std::vector<std::pair<std::size_t, std::size_t>> exits;
        std::vector<std::pair<std::size_t, std::vector<std::uint8_t>>> moves;
        const auto functionEnd = insns[body.back()].Offset + insns[body.back()].Length;
        for (std::size_t k = p; k < body.size(); ++k) {
            const auto& insn = insns[body[k]];
            const auto* code = at(k);
            const auto flow = insn.Info.FlowKind;
            if (flow == ControlFlowKind::Call || flow == ControlFlowKind::IndirectCall) { leaf = false; break; }
            const auto use = memoryUse(code, insn);
            if (use.RbpBased && rbpPrologue && use.RbpDisp < -static_cast<std::int64_t>(8 * pushed.size())) redZone = true;
            if (use.RspBased && use.RspDisp < 0) redZone = true;
            if (reason != nullptr) continue;
            if (code[insn.Info.OpcodeOffset] == 0xc9 || code[insn.Info.OpcodeOffset] == 0xc8) { reason = "leave or enter"; continue; }
            if (rbpPrologue) {
                if (use.UsesRsp) { reason = "other use of rsp"; continue; }
            } else {
                if (use.RegisterRsp) { reason = "other use of rsp"; continue; }
                if (use.RspBased) {
                    auto moved = code[insn.Info.OpcodeOffset] == 0x62 ? std::nullopt : movedDisplacement(use);
                    if (!moved) { reason = "rsp operand that cannot move with rsp"; continue; }
                    if (overlapsExisting(insn.Offset, insn.Offset + insn.Length)) { reason = "overlaps another stub"; continue; }
                    moves.emplace_back(insn.Offset + use.DispOffset, std::move(*moved));
                }
            }
            if (flow == ControlFlowKind::IndirectJump) { reason = "indirect jump"; continue; }
            if ((flow == ControlFlowKind::UnconditionalJump || flow == ControlFlowKind::ConditionalBranch) && insn.Info.HasBranchTarget) {
                const auto target = static_cast<std::int64_t>(insn.Offset + insn.Length) + insn.Info.BranchDisp;
                if (insn.Info.HasRipRelativeDisp || target < static_cast<std::int64_t>(start) || target >= static_cast<std::int64_t>(functionEnd)) { reason = "branch leaves the function"; continue; }
            }
            if (flow == ControlFlowKind::Return) {
                if (insn.Length != 1 || code[0] != 0xc3) { reason = "ret with an immediate"; continue; }
                std::vector<std::uint8_t> expected(pushed.begin(), pushed.end());
                if (rbpPrologue) expected.insert(expected.begin(), kRegRbp);
                std::size_t q = k;
                std::vector<std::uint8_t> popped;
                while (q > p && popped.size() < expected.size())
                    if (const auto reg = poppedRegister(at(q - 1), insns[body[q - 1]])) { popped.push_back(*reg); --q; } else break;
                if (popped != expected) { reason = "exit does not pop the prologue's registers"; continue; }
                const auto siteStart = insns[body[q]].Offset;
                const auto siteEnd = insn.Offset + 1;
                if (siteEnd - siteStart < kJumpBytes) { reason = "exit shorter than a jump"; continue; }
                if (targetInside(siteStart, siteEnd)) { reason = "branch into an exit"; continue; }
                exits.emplace_back(siteStart, siteEnd);
            }
        }
        if (!leaf || !redZone) continue;
        ++report.RedZoneFunctions;
        const auto prologueEnd = p > 0 ? insns[body[p - 1]].Offset + insns[body[p - 1]].Length : start;
        if (reason == nullptr && (pushed.empty() || prologueEnd - start < kJumpBytes)) reason = "prologue shorter than a jump";
        if (reason == nullptr && targetInside(start, prologueEnd)) reason = "branch into the prologue";
        if (reason == nullptr && exits.empty()) reason = "no ret exit";
        if (reason == nullptr && (overlapsExisting(start, prologueEnd) || std::any_of(exits.begin(), exits.end(), [&](const auto& exit) { return overlapsExisting(exit.first, exit.second); }))) reason = "overlaps another stub";
        if (reason != nullptr) { skip(reason); continue; }

        const auto addSite = [&](const std::size_t begin, const std::size_t end, const bool entry) {
            TrampolineSite site;
            site.Offset = ph.Offset + begin;
            site.Address = ph.MappedAddress + begin;
            site.Length = end - begin;
            site.OriginalBytes.assign(seg.begin() + static_cast<std::ptrdiff_t>(begin), seg.begin() + static_cast<std::ptrdiff_t>(end));
            if (entry) {
                site.Body = site.OriginalBytes;
                site.Body.insert(site.Body.end(), std::begin(kLowerRsp), std::end(kLowerRsp));
            } else {
                site.Body.assign(std::begin(kRaiseRsp), std::end(kRaiseRsp));
                site.Body.insert(site.Body.end(), site.OriginalBytes.begin(), site.OriginalBytes.end());
            }
            site.ReturnBranchOffset = site.Body.size();
            site.Body.insert(site.Body.end(), std::begin(kJumpBack), std::end(kJumpBack));
            sites.push_back(std::move(site));
        };
        addSite(start, prologueEnd, true);
        for (const auto& exit : exits) addSite(exit.first, exit.second, false);
        for (auto& [offset, bytes] : moves) displacements.push_back({ph.Offset + offset, std::move(bytes)});
        ++report.Framed;
        if (!rbpPrologue) ++report.FramedWithoutFramePointer;
        report.Exits += exits.size();
        report.MovedOperands += moves.size();
    }
}

}

std::vector<TrampolineSite> FrameRedZoneFunctions(
    const std::vector<std::uint8_t>& fileBytes,
    const std::vector<Domain::ProgramHeader>& codeSegments,
    const std::vector<TrampolineSite>& existing,
    RedZoneFrameReport& report,
    std::vector<RedZoneDisplacement>& displacements
) {
    std::vector<TrampolineSite> sites;
    for (const auto& ph : codeSegments) frameSegment(fileBytes, ph, existing, sites, displacements, report);
    return sites;
}

}
