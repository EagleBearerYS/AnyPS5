#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_BIGALLOCATIONS_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_BIGALLOCATIONS_HPP

#include <cstddef>
#include <string>

bool BigAllocationsEnabled_nid_no_patch();
void BigAllocationsNote_nid_no_patch(void* block, std::size_t bytes);
void BigAllocationsForget_nid_no_patch(void* block);
std::string BigAllocationsReport_nid_no_patch(const char* window);

#endif
