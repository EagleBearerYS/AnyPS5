# libSceUsbd (9 todo: A 0, B-S 9, B-M 0, B-L 0, C 0, D 0)

Upstream main 2742004ed2.

- **What it is:** the console's libusb-1.0-style USB host API without the context argument: device lists, open/close, descriptors, configuration and interface claims, synchronous and asynchronous transfers, and event handling.
- **Existing state:** 52 done.
  - Every call passes through to the host libusb built from the `3rdparty/libusb-cmake` submodule (`core/libs/prx/libSceUsbd/CMakeLists.txt:3` links `usb-1.0`). One process-wide `UsbdState` (`core/libs/prx/libSceUsbd/Export.cpp:44`, `State()` `:56`) holds the libusb context, reference-counted devices, handles, claimed interfaces, config descriptors and in-flight transfers.
  - Errors go through `ToGuestError` (`:71`): `0x80240000 - code`, `LIBUSB_ERROR_OTHER` as `0x802400FF`.
  - `sceUsbdInit` (`:156`) leaves the context null when the host has no USB access; `sceUsbdHandleEventsTimeout` (`:216`) then sleeps for the timeout (`:224-227`), otherwise calls `libusb_handle_events_timeout_completed`. Completed transfers call the title's callback from there (`CompleteTransfer`, `:107`).
  - `sceUsbdEventHandlingOk` (`:360`) returns 1.
  - Debt `docs/dev/debt/libSceUsbd.md:5`: the library follows FreeBSD 11.4 libusb (`libusb10.c`, `libusb10_desc.c`, `libusb10_io.c`, linked there) over the host libusb; `:9`: the 9 todo signatures follow orbital's console type database, "but their behavior is unknown and each call throws".
- **Counts:** A 0, B-S 9, B-M 0, B-L 0, C 0, D 0.
- **B-S (9): the libusb event-lock API.** EventHandlerActive, HandleEvents, HandleEventsLocked, LockEvents, TryLockEvents, UnlockEvents, LockEventWaiters, UnlockEventWaiters, WaitForEvent.
  - Each is the libusb function of the same name in FreeBSD 11.4 `libusb10_io.c`, which the debt already cites, on the library's single context. The host libusb provides each one, so a context-carrying forward with `ToGuestError` sits beside `sceUsbdHandleEventsTimeout`.
  - Semantics to keep: TryLockEvents returns 0 on success and 1 when another thread is the handler; WaitForEvent returns 0 when woken and 1 on timeout; HandleEventsLocked is one pass by the lock holder; HandleEvents has no timeout in FreeBSD (`handle_events_timeout_completed(ctx, NULL, NULL)`).
  - What it needs: a small stand-in (one mutex and condition variable, plus a handler thread id) for the no-USB-access case, where `State().context` is null and host libusb must not be handed a null context. The timeout checks reuse `:217`.
- **D families:** none.
- **Batch proposal: `feat(libSceUsbd): libusb event-lock API` (9 functions, B-S).**
  - Semantics source: FreeBSD 11.4 `libusb10_io.c` (debt `:5`), signatures from orbital (debt `:9`).
  - Tests: extend `guest_usbd_tests` (`core/libs/tests/GuestUsbd.cpp`, built at `core/libs/prx/libSceUsbd/CMakeLists.txt:10`, registered at `:14` with `add_test(NAME guest_usbd COMMAND guest_usbd_tests)`), which runs with or without host USB access:
    - TryLockEvents returns 0, EventHandlerActive then returns 1, a second thread's TryLockEvents returns 1, and after UnlockEvents EventHandlerActive returns 0;
    - LockEventWaiters then WaitForEvent with a 10 ms timeout returns 1; with a second thread calling UnlockEvents it returns 0;
    - HandleEventsLocked under LockEvents with a zero timeout returns 0, and with a null or out-of-range timeout returns `0x80240002`, as `GuestUsbd.cpp:260-262` checks for HandleEventsTimeout;
    - HandleEvents returns 0 once the transfer-callback round in `GuestUsbd.cpp:55` completes, when a device is present.
  - Record in the debt that the behaviour follows FreeBSD, not a console run, as the other Usbd entries do.
- **Caveats:**
  - The host libusb differs from FreeBSD in details: host `libusb_handle_events` uses a 60-second timeout, and host libusb keeps separate events and waiters locks where FreeBSD uses one context lock. The existing entries already note host-versus-FreeBSD differences (debt `:13-15`); these need the same note.
  - `sceUsbdEventHandlingOk` always returning 1 (`:360`) stops matching libusb once these locks are real; FreeBSD returns whether the caller is the handler. It is done, so changing it is a separate decision.
  - With no host USB access, HandleEvents with no timeout would block until another thread unlocks events, as FreeBSD's infinite poll does; the debt should say so.
