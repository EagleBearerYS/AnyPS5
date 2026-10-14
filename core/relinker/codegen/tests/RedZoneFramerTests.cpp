#include <codegen/x86/RedZoneFramer.hpp>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::uint64_t Base = 0x1000;

std::vector<std::uint8_t> padTo(std::vector<std::uint8_t> bytes, const std::size_t size) {
    bytes.resize(size, 0xcc);
    return bytes;
}

void append(std::vector<std::uint8_t>& bytes, const std::vector<std::uint8_t>& more) {
    bytes.insert(bytes.end(), more.begin(), more.end());
}

std::vector<std::uint8_t> callTo(const std::size_t from, const std::size_t target) {
    const auto displacement = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(from + 5);
    return {0xe8, static_cast<std::uint8_t>(displacement), static_cast<std::uint8_t>(displacement >> 8), static_cast<std::uint8_t>(displacement >> 16), static_cast<std::uint8_t>(displacement >> 24)};
}

const std::vector<std::uint8_t> Prologue{0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x53};
const std::vector<std::uint8_t> Exit{0x5b, 0x41, 0x5f, 0x5d, 0xc3};
const std::vector<std::uint8_t> StoreBelowPushes{0x48, 0x89, 0x7d, 0xe8};
const std::vector<std::uint8_t> LoadBelowPushes{0x48, 0x8b, 0x45, 0xe8};
const std::vector<std::uint8_t> LoadAbovePushes{0x48, 0x8b, 0x45, 0x08};

std::vector<std::uint8_t> leaf(const std::vector<std::uint8_t>& body) {
    std::vector<std::uint8_t> bytes = Prologue;
    append(bytes, body);
    append(bytes, Exit);
    return bytes;
}

struct Image {
    std::vector<std::uint8_t> bytes;
    std::vector<Domain::ProgramHeader> segments;
};

Image image(const std::vector<std::vector<std::uint8_t>>& functions) {
    Image result;
    std::vector<std::size_t> starts;
    std::size_t at = 0x40;
    for (const auto& function : functions) {
        starts.push_back(at);
        at += (function.size() + 15) / 16 * 16 + 16;
    }
    std::vector<std::uint8_t> caller;
    for (const auto start : starts) append(caller, callTo(caller.size(), start));
    caller.push_back(0xc3);
    result.bytes = padTo(caller, 0x40);
    for (std::size_t index = 0; index < functions.size(); ++index) {
        result.bytes = padTo(result.bytes, starts[index]);
        append(result.bytes, functions[index]);
    }
    result.bytes = padTo(result.bytes, (result.bytes.size() + 15) / 16 * 16 + 16);
    result.segments.push_back({1, 5, 0, Base, Base, result.bytes.size(), result.bytes.size(), 16});
    return result;
}

void framesRedZoneLeaf() {
    std::vector<std::uint8_t> body = StoreBelowPushes;
    append(body, LoadBelowPushes);
    const auto input = image({leaf(body)});
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(report.RedZoneFunctions == 1 && report.Framed == 1 && report.Exits == 1, "a red-zone leaf was not framed");
    require(sites.size() == 2, "a framed leaf needs one prologue and one exit site");
    require(displacements.empty() && report.FramedWithoutFramePointer == 0, "a leaf with a frame pointer had rsp operands moved");

    const auto& entry = sites[0];
    require(entry.Address == Base + 0x40 && entry.Length == Prologue.size(), "the prologue site does not cover the pushes");
    require(entry.OriginalBytes == Prologue, "the prologue site lost its bytes");
    std::vector<std::uint8_t> entryBody = Prologue;
    append(entryBody, {0x48, 0x8d, 0x64, 0x24, 0x80, 0xe9, 0, 0, 0, 0});
    require(entry.Body == entryBody && entry.ReturnBranchOffset == Prologue.size() + 5, "the prologue stub does not lower rsp after the pushes");

    const auto& exit = sites[1];
    require(exit.Address == Base + 0x40 + Prologue.size() + body.size() && exit.Length == Exit.size(), "the exit site does not cover the pops and ret");
    std::vector<std::uint8_t> exitBody{0x48, 0x8d, 0xa4, 0x24, 0x80, 0, 0, 0};
    append(exitBody, Exit);
    append(exitBody, {0xe9, 0, 0, 0, 0});
    require(exit.Body == exitBody && exit.ReturnBranchOffset == 8 + Exit.size(), "the exit stub does not raise rsp before the pops");
}

const std::vector<std::uint8_t> FramelessPrologue{0x53, 0x41, 0x56, 0x41, 0x57};
const std::vector<std::uint8_t> FramelessExit{0x41, 0x5f, 0x41, 0x5e, 0x5b, 0xc3};
const std::vector<std::uint8_t> StoreBelowRsp{0x48, 0x89, 0x7c, 0x24, 0xf0};
const std::vector<std::uint8_t> LoadBelowRsp{0x48, 0x8b, 0x44, 0x24, 0xf0};
const std::vector<std::uint8_t> LoadAboveRsp{0x48, 0x8b, 0x44, 0x24, 0x08};

std::vector<std::uint8_t> framelessLeaf(const std::vector<std::uint8_t>& prologue, const std::vector<std::uint8_t>& body, const std::vector<std::uint8_t>& exit) {
    std::vector<std::uint8_t> bytes = prologue;
    append(bytes, body);
    append(bytes, exit);
    return bytes;
}

void framesLeafWithoutFramePointer() {
    std::vector<std::uint8_t> body = StoreBelowRsp;
    append(body, LoadBelowRsp);
    const auto input = image({framelessLeaf(FramelessPrologue, body, FramelessExit)});
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(report.RedZoneFunctions == 1 && report.Framed == 1 && report.FramedWithoutFramePointer == 1 && report.MovedOperands == 2, "a leaf that only pushes was not framed");
    require(sites.size() == 2, "a framed leaf needs one prologue and one exit site");
    const auto& entry = sites[0];
    require(entry.Address == Base + 0x40 && entry.Length == FramelessPrologue.size(), "the prologue site does not cover the pushes");
    std::vector<std::uint8_t> entryBody = FramelessPrologue;
    append(entryBody, {0x48, 0x8d, 0x64, 0x24, 0x80, 0xe9, 0, 0, 0, 0});
    require(entry.Body == entryBody, "the prologue stub does not lower rsp after the pushes");
    const auto& exit = sites[1];
    require(exit.Address == Base + 0x40 + FramelessPrologue.size() + body.size() && exit.OriginalBytes == FramelessExit, "the exit site does not cover the pops and ret");
    const auto storeDisplacement = 0x40 + FramelessPrologue.size() + 4;
    const auto loadDisplacement = storeDisplacement + StoreBelowRsp.size();
    require(displacements.size() == 2, "both rsp operands must move");
    require(displacements[0].Offset == storeDisplacement && displacements[0].Bytes == std::vector<std::uint8_t>{0x70}, "the store below rsp was not moved above the lowered rsp");
    require(displacements[1].Offset == loadDisplacement && displacements[1].Bytes == std::vector<std::uint8_t>{0x70}, "the load below rsp was not moved above the lowered rsp");
}

void skipsLeavesWithOperandsThatCannotMove() {
    std::vector<std::uint8_t> body = StoreBelowRsp;
    append(body, LoadAboveRsp);
    const auto input = image({framelessLeaf(FramelessPrologue, body, FramelessExit), framelessLeaf({0x41, 0x56, 0x41, 0x57}, StoreBelowRsp, {0x41, 0x5f, 0x41, 0x5e, 0xc3})});
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(sites.empty() && displacements.empty() && report.RedZoneFunctions == 2, "a leaf whose rsp operands cannot all move, or whose prologue is too short, was framed");
    require(report.Skipped.at("rsp operand that cannot move with rsp") == 1 && report.Skipped.at("prologue shorter than a jump") == 1, "the skipped leaves were not reported with their reasons");
}

void skipsLeavesThatCopyRsp() {
    std::vector<std::uint8_t> withCmov = StoreBelowRsp;
    append(withCmov, {0x48, 0x0f, 0x45, 0xc4});
    std::vector<std::uint8_t> withShlx = StoreBelowRsp;
    append(withShlx, {0xc4, 0xe2, 0xf1, 0xf7, 0xc4});
    const auto input = image({framelessLeaf(FramelessPrologue, withCmov, FramelessExit), framelessLeaf(FramelessPrologue, withShlx, FramelessExit)});
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(sites.empty() && displacements.empty() && report.Skipped.at("other use of rsp") == 2, "a leaf that copies rsp into another register was framed");
}

Image calledThenUncalled(const std::vector<std::uint8_t>& called, const std::vector<std::uint8_t>& uncalled) {
    Image result;
    result.bytes = callTo(0, 0x40);
    result.bytes.push_back(0xc3);
    result.bytes = padTo(result.bytes, 0x40);
    append(result.bytes, called);
    result.bytes.resize(0x60, 0x90);
    append(result.bytes, uncalled);
    result.bytes = padTo(result.bytes, 0x80);
    result.segments.push_back({1, 5, 0, Base, Base, result.bytes.size(), result.bytes.size(), 16});
    return result;
}

void endsBodiesAtAlignedCodeAfterARet() {
    std::vector<std::uint8_t> body = StoreBelowRsp;
    append(body, LoadBelowRsp);
    const auto input = calledThenUncalled(framelessLeaf(FramelessPrologue, body, FramelessExit), framelessLeaf(FramelessPrologue, StoreBelowRsp, FramelessExit));
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(report.Framed == 1 && report.Exits == 1 && sites.size() == 2 && displacements.size() == 2, "the called leaf was not framed on its own");
    for (const auto& site : sites) require(site.Address < Base + 0x60, "a leaf reached without a call was framed as part of the leaf before it");
    for (const auto& displacement : displacements) require(displacement.Offset < 0x60, "an rsp operand of a leaf reached without a call was moved");
}

void endsBodiesAtTailCallsToTheNextFunction() {
    std::vector<std::uint8_t> called = FramelessPrologue;
    append(called, StoreBelowRsp);
    append(called, {0x85, 0xff, 0x75, 0x06});
    append(called, FramelessExit);
    append(called, {0x41, 0x5f, 0x41, 0x5e, 0x5b, 0xe9, 0x02, 0x00, 0x00, 0x00});
    const auto input = calledThenUncalled(called, framelessLeaf(FramelessPrologue, StoreBelowRsp, FramelessExit));
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(sites.empty() && displacements.empty(), "a leaf that tail-calls the function after it was framed together with that function");
    require(report.Skipped.at("branch leaves the function") == 1, "the tail call was not reported as leaving the function");
}

void keepsLoopsAlignedAfterAJump() {
    std::vector<std::uint8_t> loop = FramelessPrologue;
    append(loop, StoreBelowRsp);
    append(loop, {0x31, 0xc0, 0xeb, 0x04, 0x66, 0x90, 0xff, 0xc0, 0x3b, 0x44, 0x24, 0xf0, 0x72, 0xf8});
    append(loop, FramelessExit);
    const auto input = image({loop});
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(report.Framed == 1 && sites.size() == 2 && displacements.size() == 2, "a leaf whose loop header is aligned after a jump was not framed");
}

void leavesOtherFunctions() {
    std::vector<std::uint8_t> nonLeaf = Prologue;
    append(nonLeaf, StoreBelowPushes);
    append(nonLeaf, {0xff, 0xd0});
    append(nonLeaf, Exit);
    const auto input = image({leaf(LoadAbovePushes), nonLeaf});
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {}, report, displacements);
    require(sites.empty() && report.RedZoneFunctions == 0, "a leaf without red-zone use or a function that calls was framed");
}

void skipsSitesTakenByOtherStubs() {
    const auto input = image({leaf(StoreBelowPushes)});
    Codegen::TrampolineSite other{};
    other.Address = Base + 0x40;
    other.Length = 5;
    Codegen::RedZoneFrameReport report;
    std::vector<Codegen::RedZoneDisplacement> displacements;
    const auto sites = Codegen::FrameRedZoneFunctions(input.bytes, input.segments, {other}, report, displacements);
    require(sites.empty() && report.Skipped.at("overlaps another stub") == 1, "a site over another stub was framed");
}

}

int main() {
    try {
        framesRedZoneLeaf();
        leavesOtherFunctions();
        skipsSitesTakenByOtherStubs();
        framesLeafWithoutFramePointer();
        skipsLeavesWithOperandsThatCannotMove();
        skipsLeavesThatCopyRsp();
        endsBodiesAtAlignedCodeAfterARet();
        endsBodiesAtTailCallsToTheNextFunction();
        keepsLoopsAlignedAfterAJump();
        std::cout << "red-zone framer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "red-zone framer tests failed: " << error.what() << '\n';
        return 1;
    }
}
