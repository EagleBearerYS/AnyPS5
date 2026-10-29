#pragma once

#include "prx/libSceFreeType/include/GuestFreeType.hpp"

#include <cstdint>

extern "C" {

void* APS5_VABI ft_mem_alloc_nid_postfix(FreeType::GuestMemory* memory, std::int64_t size, std::int32_t* error);
void* APS5_VABI ft_mem_qalloc_nid_postfix(FreeType::GuestMemory* memory, std::int64_t size, std::int32_t* error);
void* APS5_VABI ft_mem_realloc_nid_postfix(FreeType::GuestMemory* memory, std::int64_t itemSize, std::int64_t currentCount, std::int64_t newCount, void* block, std::int32_t* error);
void* APS5_VABI ft_mem_qrealloc_nid_postfix(FreeType::GuestMemory* memory, std::int64_t itemSize, std::int64_t currentCount, std::int64_t newCount, void* block, std::int32_t* error);
void APS5_VABI ft_mem_free_nid_postfix(FreeType::GuestMemory* memory, const void* block);
void* APS5_VABI ft_mem_dup_nid_postfix(FreeType::GuestMemory* memory, const void* address, std::uint64_t size, std::int32_t* error);
void* APS5_VABI ft_mem_strdup_nid_postfix(FreeType::GuestMemory* memory, const char* string, std::int32_t* error);
std::int32_t APS5_VABI ft_mem_strcpyn_nid_postfix(char* destination, const char* source, std::uint64_t size);

}
