# libSceProprietaryVoiceChatHelperPrx (2 todo: D 2)

- **What it is:** a title with its own voice chat uses this to announce itself to the system voice chat, and to read the system's voice-chat state and usage.
- **Existing state** (`core/libs/prx/libSceProprietaryVoiceChatHelperPrx/Export.cpp`):
  - 3 done. `Helper` (`:10`: mutex, initialized) is reached through `State()` (`:15`).
    - Initialize (`:32`) and Terminate (`:40`) track initialization. Initializing twice, or a call outside Initialize/Terminate, throws (`Fail` `:20`, `RequireInitialized` `:24`).
    - SetVoiceChatState (`:48`) returns 0 and records nothing.
  - The debt files this under Silent stubs (`docs/dev/debt/libSceProprietaryVoiceChatHelperPrx.md:5`).
  - Test: `guest_proprietary_voice_chat_helper_tests` (`core/libs/tests/GuestProprietaryVoiceChatHelper.cpp`), built at `libSceProprietaryVoiceChatHelperPrx/CMakeLists.txt:6` and registered at `:10`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 2.
- **D:** GetVoiceChat (`:63`) and GetVoiceChatUsageState (`:55`). Both check initialization and then throw. The debt says the output form (returned or written) and the encoding are unknown, and no state is recorded to report.
- **Batch proposal:** none.
- **Caveats:** the host has no system voice chat, so once the output form and the "no system voice chat" value are known, both are C real behaviour.
