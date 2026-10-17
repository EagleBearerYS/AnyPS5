#ifndef CORE_SHADER_RECOMPILER_HEAPHOLDERS_HPP
#define CORE_SHADER_RECOMPILER_HEAPHOLDERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ShaderRecompiler {

struct RecompileResult;
struct IrResourcePlan;
struct ShaderInfo;
struct CompiledVariant;
struct DescriptorBindingPlan;

namespace HeapHolders {

enum class Holder : std::uint8_t { Plans, Modules, VariantMetadata, Results, Sources, Registry, DrawCache, DispatchCache, Count };
inline constexpr std::size_t HolderCount = static_cast<std::size_t>(Holder::Count);

void Add(Holder holder, std::int64_t objects, std::int64_t bytes);

struct Totals {
    std::array<std::int64_t, HolderCount> objects{};
    std::array<std::int64_t, HolderCount> bytes{};
};
Totals Read();
std::string Line(const Totals& totals);
bool Profiled();
void MaybeReport();

struct Charge {
    Holder holder;
    std::uint64_t bytes;
    std::int64_t objects = 1;
};

template<typename T, typename Charges>
std::shared_ptr<T> Track(std::shared_ptr<T> object, Charges&& charges) {
    if (object == nullptr || !Profiled()) return object;
    const auto charged = charges(*object);
    for (const auto& charge : charged) Add(charge.holder, charge.objects, static_cast<std::int64_t>(charge.bytes));
    T* raw = object.get();
    return std::shared_ptr<T>(raw, [held = std::move(object), charged](T*) mutable {
        for (const auto& charge : charged) Add(charge.holder, -charge.objects, -static_cast<std::int64_t>(charge.bytes));
        held.reset();
    });
}

class Held {
public:
    Held() = default;
    Held(const Held&) = delete;
    Held& operator=(const Held&) = delete;
    ~Held() {
        if (held) Add(holder, -objects, -static_cast<std::int64_t>(bytes));
    }
    void Hold(Holder target, std::uint64_t amount, std::int64_t count = 1) {
        if (held || !Profiled()) return;
        holder = target;
        bytes = amount;
        objects = count;
        held = true;
        Add(holder, objects, static_cast<std::int64_t>(bytes));
    }

private:
    Holder holder = Holder::Count;
    std::uint64_t bytes = 0;
    std::int64_t objects = 0;
    bool held = false;
};

template<typename T>
std::uint64_t VectorBytes(const std::vector<T>& values) {
    return static_cast<std::uint64_t>(values.capacity()) * sizeof(T);
}

inline std::uint64_t VectorBytes(const std::vector<bool>& values) {
    return (static_cast<std::uint64_t>(values.capacity()) + 7u) / 8u;
}

inline std::uint64_t StringBytes(const std::string& text) {
    return text.capacity() > 15u ? text.capacity() + 1u : 0u;
}

std::uint64_t ResultBytes(const RecompileResult& result);
std::uint64_t PlanBytes(const IrResourcePlan& plan);
std::uint64_t ShaderInfoBytes(const ShaderInfo& info);
std::uint64_t VariantMetadataBytes(const CompiledVariant& variant);
std::uint64_t VariantBytes(const CompiledVariant& variant);
std::uint64_t BindingPlanBytes(const DescriptorBindingPlan& plan);

}

}

#endif
