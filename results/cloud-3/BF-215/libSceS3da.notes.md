# libSceS3da (4 todo: D 4)

**What it is:** a port-based data channel (open, close, get info, send data) that the native module drives through a kernel driver.

**Existing state:**
- 0 done. Prototypes were recovered from the native module: `port`, and `data`/`size` for SendData (`core/libs/prx/libSceS3da/include/Api.hpp:9-12`).
- `S3daInfo` is an incomplete type (`:5`): GetInfo forwards its output pointer to a kernel ioctl without touching the fields, so the layout needs the kernel handler (`docs/dev/debt/libSceS3da.md:5`).
- The tree has no S3da device, error code or test; it appears only in the sysmodule table (`core/libs/prx/libSceSysmodule/ModuleTable.hpp:250`). There is no CMakeLists; the library is built by the generic loop in `core/libs/CMakeLists.txt:159`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4.

## Families

- **D (4): needs a kernel service the tree does not model.**
  - Open, Close and SendData go to a driver the tree does not emulate. The console's answer when the device or port is absent is not in the tree.
  - GetInfo also needs the `S3daInfo` layout.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Needs the console's results measured: the Open error for a port with no device (which would make the four rows C-real candidates) and the `S3daInfo` layout. Otherwise it needs a modelled driver.
