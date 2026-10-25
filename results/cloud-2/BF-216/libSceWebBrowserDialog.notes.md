# libSceWebBrowserDialog (3 todo: D 3)

- **What it is:** the system web-browser common dialog: open (or open for predetermined content with an allow-list), status, close, result, cookies, and runtime control of an open browser (events, navigation, zoom).
- **Existing state** (`core/libs/prx/libSceWebBrowserDialog/Export.cpp`):
  - 10 done on the common-dialog state machine, with `g_status` and `g_open` (`:16-17`).
    - Open (`:96`) and OpenForPredeterminedContent (`:122`) finish at once (`:101`, `:138`).
    - GetResult (`:76`) reports USER_CANCELED after validating the result and extended-result reserved bytes.
    - SetCookie (`:112`) and ResetCookie (`:143`) check their parameters in the console module's order and store nothing (`docs/dev/debt/libSceWebBrowserDialog.md:6`).
  - The struct layouts are in `libSceCommonDialog/CommonDialog.hpp`. The debt files the state machine under Silent stubs (`:5`).
  - Tests: `guest_web_browser_dialog_tests` (`core/libs/tests/GuestWebBrowserDialog.cpp`, `libSceWebBrowserDialog/CMakeLists.txt:6`, `:9`) and `guest_web_browser_dialog_cookies_tests` (`core/libs/tests/GuestWebBrowserDialogCookies.cpp`, `:17`, `:20`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 3.
- **D:** GetEvent, Navigate and SetZoom act on an open browser. They are declared without parameters after shadPS4's stubs (debt `:9`).
  - GetEvent writes an event of unknown layout.
  - Navigate and SetZoom have unknown arguments and checks.
  - The dialog is never RUNNING here, but no done call gives the answer for steering a browser that is not running.
- **Batch proposal:** none. No A/B/C rows.
- **Caveats:** once prototypes are found, Navigate and SetZoom would be C (the not-running answer), and GetEvent would be C if the no-event answer is found. Each needs that answer measured or sourced.
