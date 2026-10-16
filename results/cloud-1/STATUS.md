READY
RESULT:
- PM-045 done: results/cloud-1/PM-045.md (path table, ratings, red/green lines, checks).
- Fork fix/agc-abi-decode-defer-siblings @ f1ad37c3a, four fixes on 661d3cba8d, each with a PreparedShaders.cpp mode:
  - 19a3e0710: a stored registration failure;
  - 502085c15: vertex inputs at the pipeline ABI;
  - e986ee0f7: rect-list pairs;
  - f1ad37c3a: unsupported stage inputs at registration.
- For the PM: 19a3e0710 reverses #6366's stated "a stored registration failure still propagates". The same exception is rethrown at first use, and the commit can be dropped on its own.
- Checks: CI mirror (configure, registration, build, libs) exit 0; ^agc 606/606 (41 lavapipe skips); new modes plus prepared_publication ×10, 60/60; conventions passed; MinGW 5 files rc 0.
