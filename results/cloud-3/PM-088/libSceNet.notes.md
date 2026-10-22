# libSceNet (159 todo: B-S 12, B-M 3, C 2, D 142)

What it is: the PS5 BSD-socket library. The sandbox-independent part is already implemented over host sockets (64 done):
- socket, bind, listen, accept, connect, send/recv/msg, shutdown and get/setsockopt with FreeBSD 11.4 semantics (debt), plus epoll and abort;
- the resolver over host `getaddrinfo`/`getnameinfo`, pton/ntop, byte order, errno, pools and memory, `GetSockInfo`, and MAC/DNS/IfName queries.

Errors follow FreeBSD errno with `NET_ERROR_BASE | errno` (`Export.cpp:228-257`, `fail()` at `:494`). The todo list is mostly system network administration that an app process does not normally reach.

Classes:
- B-S (12):
  - forwards with public semantics: `SocketInternal`/`GetsockoptEx`/`SetsockoptEx` to their public twins; `sceNetIoctl` FIONBIO/FIONREAD as libkernel `ioctl` does (`Socket/src/Udp.cpp:1333`); `sceNetSysctl` to libkernel `sysctl` (`SystemConfiguration.cpp:236`);
  - `EtherStrton` (ether_aton(3)) and the RFC 4007 scope-id pton/ntop;
  - `InitParam` (as `sceNetInit`), `ThreadExit` (`scePthreadExit`), and `GetIfnameNumList`/`ConfigGetDefaultRoute` from the NetCtl host interface.
- B-M (3): `ConfigGetIfFlags` (FreeBSD `IFF_*`), `ConfigGetDefaultRoute6` and `GetDns6Info`. The last two need an IPv6 host model in NetCtl; `sceNetCtlGetInfoV6` throws today.
- C (2): `ShowRouteWithMemory`/`ShowRoute6WithMemory` return 0 like `sceNetShowRoute(6)`, with the pool check of `ShowNetstatWithMemory`. These are silent stubs (debt) and need a title.
- D (142):
  - interface/route/ARP/address configuration (37), WLAN (25), DHCP/DHCPD/PPPoE/routing/addr6/dup-IP/info-dump daemons (16): private kernel payloads (debt), and the host network must not be reconfigured;
  - layout-incomplete queries: if-list, route, ARP, stats, mem-info, DHCP info, SockInfo6;
  - packet dump, emulation, bandwidth control, event callbacks, NetSync, ResolverConnect, DNS setters, Show*ForBuffer and Policy text, and `ThreadCreate`/`ThreadJoin`.

Third-party: none for the B rows. Honouring `sceNetSetDnsInfo` would need an own DNS client such as c-ares, because lookups use the host resolver. That is not proposed while the console result for an app is unknown. mbedtls does not help here.

Batch proposals:
1. "net: socket and text helpers" (10): SocketInternal, GetsockoptEx, SetsockoptEx, Ioctl, Sysctl, EtherStrton, InetNtopWithScopeId, InetPtonWithScopeId, InitParam, ThreadExit. Test: extend `guest_sce_net_tests` (`core/libs/tests/GuestSceNet.cpp`, `libSceNet/CMakeLists.txt:69`):
   - ether string round trip with `EtherNtostr`, and EINVAL on a bad string;
   - FIONBIO makes a receive return EAGAIN, and FIONREAD counts a queued datagram;
   - `fe80::1%2` round trip; `sceNetSysctl` and libkernel `sysctl` agree on one MIB;
   - Internal/Ex calls match their public twins; ThreadExit ends a thread joined with `scePthreadJoin`.
2. "net: host interface queries" (5, follow-up): GetIfnameNumList, ConfigGetDefaultRoute, ConfigGetIfFlags, ConfigGetDefaultRoute6, GetDns6Info. It needs IPv6 support in the NetCtl host model. Test: guest asserts that agree with `sceNetCtlGetInfo` (default route, MTU) and the loopback flags.
