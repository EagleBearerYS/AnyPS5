#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_BUFFERCOPY_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_BUFFERCOPY_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace AgcDriver::DriverDetail {

enum CopyPath { CopyCpu = 0, CopyGpu = 1, CopyNotImported = 2, CopyAliased = 3, CopyRecordPending = 4, CopyShape = 5, CopyOverlapping = 6, CopyInaccessible = 7, CopyOverGpuMax = 8, CopyPaths = 9 };

}

#endif
