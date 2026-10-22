# libSceDataTransfer (60 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 60)

What it is: the PS4-to-PS5 data transfer service used by the system's data transfer app: discovering a PS4 on the network, authenticating, reading its users, titles and save data, and copying them in the foreground or background. The `sceDataTransferTarget*` exports are the target (receiving console) side of that network protocol.

Families and decisions:
- Initialize/Terminate, Abort/Get progress, Request*PS4 (17): service setup and the PS4 discovery/transfer requests.
- Target Request/Abort/Event/Get (43): the target-side protocol steps (auth, IPv6, power request, SSO exchange, reboot data, save data binding).
- All 60 are D. The debt file states that no credible prototypes exist; the declarations have no parameters only to resolve imports. The protocol is private, and there is no PS4 to transfer from on a PC.

A C answer ("nothing to transfer") would be real behaviour, but it needs at least the signatures and the console's error codes, which are not in the tree or in public sources.

New third-party dependencies: none.

Batch proposals: none from this library (no A/B/C rows). Titles are not expected to import it, so it has low priority.
