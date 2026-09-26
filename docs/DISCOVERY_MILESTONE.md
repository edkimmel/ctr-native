# Discovery milestone

Design-and-status record for automatic peer discovery on the arcade link, on
branch `arcade`: two cabinets on one switch find each other with no
configured IP, elect their seats, and link. Read AGENTS.md and
docs/HANDOFF.md first. This document follows the pattern of
docs/SOLO_CAB_MILESTONE.md: a prospective plan with a slice list, updated to
record status as slices land. Every citation was checked against the tree
at bd9856001.

Status: plan only (DISC-S1). No code has landed.

## 1. Why

Owner direction:

- "Automatic discovery so this could potentially be open source shippable."
  The owner's priority list is to finish the last stretch goals so the port
  could ship as open source (docs/HANDOFF.md:885-886); discovery is item 2
  (:896-902).
- "One vs two is not a static config, it's whether the other cab(s) are
  discovered at startup or silent. This is a continuous background poll to
  check for wake."
- The cabinets sit on a dumb switch with fixed IPs. "Autodiscovery over the
  switch's gateway range would make it easier to handshake."

Today every linked cabinet needs a per-cabinet `arcade.cfg` naming its own
seat and port and the other cabinet's IP (tools/package/cab1.cfg:14-20). The
fleet generates it from each cabinet's identity
(C:\Arcade\scripts\setup-ctr-native.ps1:186-207, :272-316, read only). A new
install elsewhere must hand-edit it. The goal is one config file for every
cabinet with no network settings.

Scope: two cabinets, zero-config. Three or four cabinets and humans are a
later extension (stretch goal 8, docs/HANDOFF.md:76-84). The design must not
block it: `NativeMatchConfigV1` has two human roles, while match select is
already sized for 4 humans.

The solo milestone left the seam for this: the solo offer depends only on
the lobby status, never on the peer list (SOLO-10,
docs/SOLO_CAB_MILESTONE.md:292-296), and the lobby's caller-supplied
candidate list is where discovery plugs in (docs/LOBBY_MILESTONE.md:141-165,
"the candidate-list seam is where it would plug in", :160-161).

## 2. What exists

### 2.1 Options and config

- Link options (include/platform/native_arcade_link_options.h:100-116):
  `enabled`, `localRole`, `localPort`, `peers[8]`
  (NATIVE_ARCADE_LINK_OPTIONS_MAX_PEERS, :54), `preview`, and
  `selectEntropy`, which main.c fills and argv never sets (:109-115).
- All-or-none: an enabled link needs a port and at least one peer and no
  preview; a port or peer without `--arcade-link` is an error
  (platform/native_arcade_link_options.c:254-264).
- Config file keys `seat`, `port`, `peer` (include/platform/native_arcade_config.h:18-21).
  The link group is all-or-none by the command line's own parser (PK-5,
  :41-48; docs/PACKAGING.md:53-79). The whole group is probed once
  (platform/native_arcade_config.c:468-479). Each value is first probed at
  its own line with the other two members as fixed samples, the seat with
  the sample peer `127.0.0.1:1` (:95-110).
- Precedence (PK-4, docs/PACKAGING.md:39-51): if argv names
  `--arcade-link`, `--arcade-link-port`, `--arcade-link-peer`, or
  `--arcade-link-preview`, the file's whole link group is ignored. Code:
  NativeArcadeConfig_ParseArgs sets `namesLinkOption`
  (platform/native_arcade_config.c:600-605); main.c applies the file's group
  only when it is 0 (main.c:322-331).
- Package templates tools/package/cab1.cfg and cab2.cfg (seat cab1 port
  7001, seat cab2 port 7002, a peer IP to edit; PK-8,
  docs/PACKAGING.md:119-139), copied by tools/package-arcade.ps1:196-198.
- Firewall rule PK-9 (docs/PACKAGING.md:141-149): inbound UDP on the link
  port only, program-scoped, remote address the peer's fixed IP, all
  profiles.

### 2.2 Host and adapter: the role is fixed at init

- main.c reads the identity (NativeIdentity_Get, main.c:584) and draws
  `selectEntropy` from the wall clock and the performance counter
  (main.c:597) before NativeArcadeLinkHost_Configure (main.c:599).
- Configure copies the peers to the adapter's candidates and sets
  `localRole` once (platform/native_arcade_link_host.c:858-865). LINK mode
  starts dormant on screen OFF with no socket
  (include/platform/native_arcade_link_host.h:236-246).
- The adapter reads `config.localRole` at lobby Begin
  (platform/native_arcade_netplay.c:203-207), for the select nonce and the
  initial cursor (:430-438, :527), for the launch agreement (:733), and in
  its view (:1181). The host view's `localCab` follows it
  (native_arcade_link_host.c:1225); the layout prints the cabinet line from
  it (game/MAIN/MainArcadeLinkLayout.c:335, :350) and the race steering reads
  it (game/MAIN/MainArcadeRaceLaunch.c:592).
- NativeArcadeNetplay_Init rejects an empty candidate list
  (native_arcade_netplay.c:90-93) and a role without a fixture slot (:77-84).
- Lobby lifecycle in the adapter: PollLobby every tick (:183-193),
  BeginLobby (:198-208), CloseLobby (:211-222), RestartLobby (:228-245,
  RestartCycle when a lobby is open, else Close and Begin), BeginSolo with
  the listen-only link filtered on the candidates (:541-555).
- BeginLobby has five callers: Enter (:287-300), BeginRematch (:341), Relink
  (:482), EndSolo, the solo RESULTS "LOBBY" row (:597-607), and
  RestartLobby's fallback (:244). The flow emits RESTART_LOBBY from LOBBY
  (the retry pause, native_arcade_flow.c:157-170, :240-243; CONFIRM on
  REJECTED, :220-230), from MATCH_FOUND back to LOBBY (:249-255), and from
  the SELECT_RESULT relink wait (:356-358) and REMATCH_WAIT (:487-489).

### 2.3 Lobby

- include/platform/native_lobby_state.h: Begin (:111-114) copies up to
  NATIVE_LOBBY_STATE_MAX_CANDIDATES = 8 candidates (:31) and the role;
  RestartCycle reuses the list and role stored at Begin (:175-192);
  BeginListen opens the listen-only link (:116-129), mode LISTENING (:59)
  latches `peerHeard` (:89-92) on a well-formed HELLO from a candidate
  address; Poll (:135-173).
- A WAITING lobby holds no socket: the link socket exists only while a
  candidate is attempted or while listening.

### 2.4 Handshake and identity

- HELLO is 284 bytes (include/platform/native_lockstep_handshake.h:34,
  layout :38-57). A HELLO with our own role is rejected ROLE_CONFLICT
  (platform/native_lockstep_handshake.c:375-381). Otherwise the four
  versions and the full SHA-256 config digest must agree (:323-342,
  :382-388); the fixture carries the build and content identity
  (include/platform/native_arcade_link_options.h:30-36).
- Identity: two 32-byte SHA-256 digests, build and content
  (include/platform/native_identity.h:6-16). SHA-256 API:
  include/platform/native_sha256.h:17-19.
- The peer link routes by datagram size: 284 handshake, 128 bundle, 64 aux;
  any other size is dropped (include/platform/native_lockstep_peer_link.h:270-328).

### 2.5 Transport

- platform/native_udp_transport.c: Open binds INADDR_ANY:port and sets only
  FIONBIO; there is no SO_BROADCAST (:65-93). Receive returns the sender
  address (:145-186); any recvfrom error other than WSAEWOULDBLOCK and
  WSAEMSGSIZE is RECEIVE_ERROR (:162-177).
- The adapter never links the transport: it reaches it only through the
  lobby (tests/native_arcade_netplay_isolation_test.cmake rule 4, :108-150).
  The host glue may name no OS networking token
  (tests/native_arcade_link_host_isolation_test.cmake:93-94).

### 2.6 Tick model

- Link timings are 30 Hz game-loop ticks
  (include/platform/native_arcade_netplay.h:186-193, :228-233).
- The host ticks only when the frame hook owns or ticks the frame
  (game/MAIN/MainArcadeLink.c:465-535): on a race frame of a linked or solo
  race (game/MAIN/MainArcadeLinkPolicy.c:86-94), on an active arcade-link
  screen (:96-100), or on the ready title menu (:44-60). It does not tick
  during the boot intro, during the retail attract demo race, or during a
  level load outside a race.

### 2.7 Tests that constrain this stack

- Live: `arcade_link_launch` (CMakeLists.txt:2138-2149, ports 7101/7102,
  tools/arcade-link-launch-check.ps1), `arcade_solo_race` (:2167-2176,
  tools/arcade-solo-race-check.ps1, ports 7201-7204), and
  `package_arcade_smoke` (:2190-2199), which derives loopback copies of
  cab1.cfg and cab2.cfg. The fast suite's socket tests use 48000-48600
  (:2060-2062).
- Isolation: tests/native_udp_transport_isolation_test.cmake,
  native_arcade_link_options_isolation_test.cmake,
  native_arcade_config_isolation_test.cmake,
  native_arcade_link_host_isolation_test.cmake (network tokens, the include
  allow-list rule 3, the library pin rule 4 at :883), and
  native_arcade_netplay_isolation_test.cmake (includes :67-78, nine
  libraries, rule 4 :108-150).
- Tests that assert a peerless link group is invalid: config
  tests/native_arcade_config_test.c:284, :287, :496; options
  tests/native_arcade_link_options_test.c:397 (`linkNoPeer`).

### 2.8 Fleet (C:\Arcade, read only)

C:\Arcade\scripts\setup-ctr-native.ps1 maps cabinetId 1 and 2 to the wired
IPs 192.168.1.11 and 192.168.1.12 and ports 7001 and 7002 (:105-106). Step 4
writes `arcade.cfg` with `seat`, `port`, and `peer` (:280-300). Step 5 keeps
one rule, "Arcade CTR-native: link UDP $port from $peerIp" (:347), in the
group 'Arcade CTR-native' (:114): `New-NetFirewallRule ... -Direction Inbound
-Action Allow -Protocol UDP -LocalPort $port -RemoteAddress $peerIp -Program
$Exe -Profile Any` (:388-390). Test-CtrRule accepts only that shape: one
local port, one remote address (:320-340).

## 3. DISC defaults

The owner delegated these. Each gives the reason and the rejected
alternative.

1. DISC-1 (default): Scope. Two cabinets. The beacon, the peer table, and
   the election are written for N peers (a table of 8 entries, up to 3 echo
   entries per beacon), so 3-4 cabinets is an extension. Pairing more than
   two cabinets of one group is not supported and is a documented limit
   (risk 3). Rejected: a two-only wire format, which a later N-cabinet
   version would have to break.
2. DISC-2 (default): A separate discovery socket and port. Default UDP
   7000, bound on INADDR_ANY with SO_BROADCAST on, open from host Configure
   to Shutdown for the whole LINK run, in discovery mode only (DISC-11). The link socket is untouched: the
   peer link still opens it per candidate and it stays absent while
   WAITING. Rejected: sharing the link port. The link socket is opened and
   closed per candidate (section 2.3), and its size-based routing (section
   2.4) must never see beacons.
3. DISC-3 (default): Beacon. One fixed-size little-endian datagram of 96
   bytes, a size distinct from HELLO (284), bundle (128), and aux (64), so a
   stray beacon at a link port is dropped by size. IPv4 values are the
   host-order value (as NativeUdpTransportAddress: 192.168.1.11 is
   0xC0A8010B) written as u32 LE.

   | Offset | Size | Field | Decoder rule |
   | --- | --- | --- | --- |
   | 0 | 4 | magic, the bytes `CTRD` | exact |
   | 4 | 2 | version | 1 |
   | 6 | 2 | size | 96 |
   | 8 | 8 | instance nonce | nonzero |
   | 16 | 8 | group hash (DISC-9) | any |
   | 24 | 8 | identity digest (DISC-10) | any |
   | 32 | 2 | link port | 1..65535 |
   | 34 | 1 | seat preference: 0 auto, 1 cab1, 2 cab2 | 0..2 |
   | 35 | 1 | flags | 0 |
   | 36 | 1 | echo count | 0..3 |
   | 37 | 3 | reserved | 0 |
   | 40 | 16 | echo entry 0 | see below |
   | 56 | 16 | echo entry 1 | see below |
   | 72 | 16 | echo entry 2 | see below |
   | 88 | 8 | reserved | 0 |

   Echo entry: +0 u64 peer nonce, +8 u32 observed peer IPv4 (the source
   address of that peer's beacons), +12 u16 peer link port (as that peer
   advertises it), +14 u16 reserved 0. The header is padded to 40 bytes so
   the entries sit on 8-byte offsets. The decoder rejects a wrong size,
   magic, or version; nonce 0; link port 0; seat preference above 2; echo
   count above 3; nonzero flags or reserved bytes; a used echo entry with
   nonce 0 or port 0; an unused echo entry that is not all zero. The echo
   entries of a beacon are the live same-group, same-identity peers, lowest
   election key (DISC-8) first, at most 3. Rejected: a checksum trailer (UDP
   has its own, and the strict decoder plus the handshake already guard the
   pairing).
4. DISC-4 (default): Cadence, in 30 Hz host ticks, never wall clock. A
   beacon on the first tick and then every 30 ticks (1 s) to every target
   (DISC-5). A peer entry expires 300 ticks (10 s) after its last beacon,
   which tolerates level loads, when the host does not tick (section 2.6).
   The interface list is refreshed every 300 ticks, so a NIC or cable that
   comes up late is found. Rejected: wall-clock timers (the host glue may
   not read a clock, tests/native_arcade_link_host_isolation_test.cmake:95,
   and tick counts keep the core testable).
5. DISC-5 (default): Targets. 255.255.255.255 plus the directed broadcast
   (ip | ~mask) of every up, non-loopback IPv4 interface, from the OS
   adapter list, deduplicated and capped at 8. This is the owner's "gateway
   range": on the fleet's fixed-IP /24 it is 192.168.1.255. Rejected: a
   unicast sweep of the 254 host addresses (254 datagrams a second, it looks
   like a port scan, and on a dumb switch it reaches no more hosts);
   multicast (group joins to manage, no gain on one switch); the limited
   broadcast alone (Windows sends 255.255.255.255 out of one interface only
   on a multi-NIC host).
6. DISC-6 (default): Several NICs. Receive on INADDR_ANY. The peer's link
   address is the source IPv4 of its beacon (an address that demonstrably
   reached us) plus its advertised link port. Entries are keyed by nonce.
   The first address heard for a nonce is kept until the entry expires, so
   an entry never flaps between NICs on one segment. A beacon with a new
   nonce from the source IPv4 and link port of an existing entry replaces
   that entry at once (the process restarted; risk 4). Beacons carrying our
   own nonce (our broadcast looped back) are ignored.
7. DISC-7 (default): Eligibility. A peer is eligible when its latest beacon
   is well formed; its nonce is not ours; its group hash equals ours; its
   identity digest equals ours; and it echoes our nonce, so two-way
   reachability is proven before any HELLO is sent. An identity mismatch is
   ignored and logged once per nonce; the handshake still does the full
   build, content, and config check, unchanged. With several eligible peers
   the lowest election key wins, which both sides compute alike; more than
   two in a group is the documented limit.
8. DISC-8 (default): Seat election. The election key of a cabinet is
   (IPv4, link port) as the OTHER side observes it. Our key comes from the
   peer's echo of our nonce; the peer's key is its beacon source IPv4 plus
   its advertised link port. Both sides therefore compare the same two keys.
   - The lower key (IPv4, then port, as unsigned values) is cab1.
   - Equal keys (only under NAT or a misconfiguration): the lower nonce is
     cab1; equal nonces: no pair.
   - Seat preference overrides: if one side has a preference it gets it and
     the other takes the other seat; two different preferences are
     honoured; two equal preferences are a conflict: no pair, logged once
     per nonce.
   - Stable across restarts on fixed IPs, and correct when both start at
     once: it is a pure function of the pair. On the fleet (192.168.1.11 and
     .12, both on port 7001) it gives .11 cab1 and .12 cab2, today's seats
     (section 2.8).
   - Rejected: a random-nonce election (seats would swap on every restart).
9. DISC-9 (default): Group. An optional `group` key and
   `--arcade-link-group <name>`, 1..32 characters of [A-Za-z0-9._-], default
   `ctr-native`. The beacon carries its 64-bit FNV-1a hash (offset basis
   0xcbf29ce484222325, prime 0x100000001b3, over the name's bytes without a
   terminator). Different groups never pair. Same group plus same identity
   pairs: that is the documented meaning of "groups match". Rejected:
   carrying the group string (a larger beacon for no gain).
10. DISC-10 (default): Identity digest. The first 8 bytes of SHA-256 over
    the build identity then the content identity (64 bytes,
    include/platform/native_identity.h:12-16), computed once in host
    Configure from the identity it already receives
    (platform/native_arcade_link_host.c:819-820). The host reaches SHA-256
    through ctr_native_arcade_link_options, which links
    ctr_native_arcade_bot_rules PUBLIC, which links ctr_native_sha256
    PUBLIC (CMakeLists.txt:409, :456). It is a filter, not a check: the
    handshake stays the authority.
11. DISC-11 (default): Config and precedence.
    - `seat = cab1|cab2|auto` (and `--arcade-link cab1|cab2|auto`) is the one
      key that turns link mode on.
    - Static mode: `peer` given. It needs `seat` cab1 or cab2 and `port`,
      exactly today's all-or-none rule. Discovery is OFF: no discovery
      socket, no beacon. Byte-identical to the current fleet.
    - Discovery mode: no `peer`. `seat` auto, cab1, or cab2 (cab1 and cab2
      become the beacon's seat preference); `port` optional, default 7001;
      `group` optional.
    - `peer` with seat `auto` is an error. `group` or `port` without `seat`
      is LINK_INCOMPLETE, as today. `group` with `peer` is an error (it would
      be silently ignored).
    - Precedence stays PK-4: if argv names any link option, the file's whole
      link group is ignored. `--arcade-link-group` joins that list
      (native_arcade_config.c:600-605). The two DISC-18 flags do not: they
      are not group members, so a test can run a shipped `arcade.cfg` with
      them.
    - A fresh install ships ONE `arcade.cfg` for every cabinet with
      `seat = auto`: no per-cabinet network config.
    - Consequence, deliberate: a seat without a peer is valid now. S3
      changes the four test expectations that say otherwise (section 2.7)
      and the per-line seat probe, whose sample peer would reject `auto`
      (native_arcade_config.c:95-110).
    - A static cabinet and a discovery cabinet never link (the static one
      does not beacon). Both cabinets of a pair use the same mode.
    - Rejected: link on by default with no key (a plain unlinked run must
      stay possible); a separate `link = auto` key (`seat` already is the
      enabling key).
12. DISC-12 (default): Background poll, pairing, and solo.
    - The discovery service opens at host Configure, while LINK mode is
      still dormant on the title, and ticks on every host tick while LINK
      mode is on: title, lobby, select, race, results, and solo. So a peer
      is usually discovered before any player presses START, and a cabinet
      in solo keeps beaconing so a waking peer finds it.
    - The pairing is one peer address and the elected seat, or none.
      Pairing means the peer process runs; it does not mean its player is
      in the LOBBY. The solo offer still follows the lobby status only
      (SOLO-10).
    - The adapter takes the current pairing only at a lobby Begin that opens
      a new session: Enter (native_arcade_netplay.c:287-300), EndSolo
      (:597-607), and RESTART_LOBBY while the flow is on LOBBY (the retry
      pause and the REJECTED CONFIRM, and the MATCH_FOUND fall-back that
      lands on LOBBY). It then sets candidates = [peer address] and
      localRole = the elected seat. With no pairing the list is empty: the
      lobby is WAITING, and solo is offered after the solo offer delay, as
      today.
    - Relink (:482), rematch (:341), and their retries from SELECT_RESULT
      and REMATCH_WAIT keep the peer and role of the session that reached
      READY. The role never changes while a lobby attempt or a session is
      open.
    - While unpaired, localRole is the seat preference, or cab1 for `auto`;
      the LOBBY's cabinet line shows it.
    - If the pairing changed since the last Begin, a LOBBY retry is Close
      then Begin instead of RestartCycle (RestartCycle reuses the stored
      list and role).
    - While solo, the listen-only link's filter is the current paired peer,
      refreshed when the pairing changes (Close then BeginListen), so
      `peerHeard` keeps its SOLO-4 meaning: a HELLO from the peer's LOBBY.
    - Discovery never forces a screen change.
13. DISC-13 (default): Instance nonce. Its own host entropy draw in main.c,
    at the `selectEntropy` draw (main.c:597), hashed with SHA-256 under a
    domain string ("CTRN discovery nonce v1"), first 8 bytes LE, with a
    zero result mapped to one. It is fixed for the process lifetime and reaches the host as a new
    options field that argv never sets, as `selectEntropy` does. Reason: the
    nonce is broadcast in the clear, and `selectEntropy` feeds the exchanged
    select nonces (docs/MATCH_SELECT_MILESTONE.md section 2.6, :385-389,
    :401-410), so the nonce must not be a function of it. Rejected: reusing
    `selectEntropy` or its per-epoch mix (it changes on every Configure and
    AbortToTitle, native_arcade_link_host.c:238-243, and would be exposed).
14. DISC-14 (default): Isolation. Nothing from discovery enters simulation
    identity, the match config, replay, checkpoints, or canonical state.
    Only the peer address and the elected localRole reach the lobby: the
    same two values a static config supplies today. The topology lease is
    untouched. Structure:
    - The pure core may include only <stddef.h>, <stdint.h>, <string.h>, and
      its own header: no match config, lockstep, canonical, replay,
      checkpoint, lease, game/, heap, clock, or sockets.
    - The socket service may use only the UDP transport, the interface
      enumeration, and the core.
    - Only the link host and the netplay adapter may name the discovery
      modules. The host owns the service, because the adapter may not link
      the transport (section 2.5); the adapter takes the pairing as plain
      values through a setter and needs no discovery library, so its
      nine-library pin is unchanged.
    - Replay, checkpoint, and canonical code must not name them.
15. DISC-15 (default): Failure is never fatal. A discovery bind failure
    (port in use) is logged once and discovery stays off. An interface
    enumeration failure is logged once and beacons go to 255.255.255.255
    only. A receive error ends that tick's drain (risk 9). In every case the
    lobby stays WAITING and solo is offered.
16. DISC-16 (default): Logging, through the host's Platform_Log. One line
    when a pairing is found or lost, with the peer address and the elected
    seat. One line per nonce for a group, identity, or seat conflict. The
    table holds at most 8 entries; a beacon that finds it full is dropped
    and not logged, so a busy LAN cannot flood the log.
17. DISC-17 (default): Firewall and ports. The operator allows inbound UDP
    7000 (discovery) and the link port (default 7001) from the local subnet
    for ctr_native.exe; outbound broadcast needs nothing. PK-9 changes in
    S5 to a program-scoped rule for both ports with `-RemoteAddress
    LocalSubnet` and `-Profile Any` (a dumb-switch LAN may be classified
    Public, PK-9's own reason). The fleet follow-up, in C:\Arcade (not
    changed by this milestone), in scripts\setup-ctr-native.ps1:
    - step 5: the rule becomes `-LocalPort 7000,7001 -RemoteAddress
      LocalSubnet -Program $Exe -Profile Any` (today `-LocalPort $port
      -RemoteAddress $peerIp`, :388-390), with its name (:347) and
      Test-CtrRule (:320-340) changed to match;
    - step 4: `arcade.cfg` drops `peer` and `port` and writes
      `seat = auto` (or keeps `seat = cab<id>` as a fixed-seat override)
      (:280-300);
    - step 2: its runtime file list follows the S5 package (it names
      cab1.cfg and cab2.cfg, :37).
18. DISC-18 (default): Loopback testability. Two CLI-only test and
    diagnostic flags, not `arcade.cfg` keys:
    - `--arcade-discovery-port <p>`: the bind port, default 7000.
    - `--arcade-discovery-target a.b.c.d:port`: repeatable, up to 4. When
      given, beacons go only to those targets: no broadcast and no
      interface enumeration.
    - Either flag without discovery mode is an error (it would be silently
      ignored, the options' own rule).
    - Two processes on 127.0.0.1 bind different discovery ports and target
      each other. Their link ports differ, so the election is by port: the
      lower link port is cab1. The live test runs both with
      `--arcade-link auto` and NO `--arcade-link-peer`.

## 4. Slices

Order: S1 to S6. Each code slice runs the fast suite
(`ctest --test-dir build-msvc-x86 -C Debug --output-on-failure -LE live -j 8`)
plus the live labels named in its gate.

### DISC-S1 -- this plan

- docs/DISCOVERY_MILESTONE.md only. Gate: none (docs).

### DISC-S2 -- pure core

- Files: include/platform/native_arcade_discovery.h,
  platform/native_arcade_discovery.c, tests/native_arcade_discovery_test.c,
  tests/native_arcade_discovery_isolation_test.cmake, CMakeLists.txt (a
  C17 no-extensions library `ctr_native_arcade_discovery` that links
  nothing).
- Content: the beacon codec (DISC-3); the group hash and name check
  (DISC-9); the election (DISC-8); a caller-owned, tick-driven peer table
  of 8 (DISC-4, DISC-6, DISC-7): accept a decoded beacon with its source
  address, tick (expiry), build our next beacon (echo entries), read the
  pairing, and take the pending log events (DISC-16).
- Unit tests: codec round trip and every reject; FNV-1a known answers;
  election from both sides (opposite seats), equal keys, every preference
  pair; expiry at exactly 300 ticks; echo required; own nonce ignored;
  mismatches never eligible and reported once; restart replacement; full
  table; lowest key of three. Isolation test: the DISC-14 core rules.
- Gate: fast suite.

### DISC-S3 -- options, config, transport, interfaces, service

- Options and config: `auto`, `group`, `--arcade-link-group`, the DISC-18
  flags, the new nonce and group fields (DISC-11, DISC-13). The per-line
  seat probe learns `auto`; the four peerless-seat expectations change
  (section 2.7), each noted in its test. Every other option and config test
  keeps its outcome.
- Transport: NativeUdpTransport_EnableBroadcast (SO_BROADCAST) in
  platform/native_udp_transport.c; its isolation test gains the token.
- Interface enumeration behind platform/native_*: a new
  platform/native_net_interfaces.c (GetAdaptersAddresses, iphlpapi) with a
  pure directed-broadcast helper (ip | ~mask, loopback and down skipped,
  dedupe, cap 8) tested without the OS call.
- Service: platform/native_arcade_discovery_service.c and its header: open
  (bind, broadcast on), tick (drain, feed the core, beacon every 30 ticks,
  refresh targets every 300), pairing, close. Unit test with two real
  sockets on loopback, targets set explicitly, in a new port band
  48610-48629 (the CMakeLists comment at :2060-2062 updated): the two pair
  with opposite seats, lose the pairing 300 ticks after one closes, and a
  bind conflict is not fatal.
- Isolation: the DISC-14 service rule.
- Gate: fast suite plus `-L live-link` and `-L live-package` (the option and
  config parsers are on their path; behaviour there must not change).

### DISC-S4 -- host and adapter integration, live proof

- Files: platform/native_arcade_link_host.c, platform/native_arcade_netplay.c,
  their headers, main.c, CMakeLists.txt, the host and netplay isolation
  tests, a checker under tools/.
- Host: opens the service in Configure (discovery mode only), ticks it at
  the top of Tick before the adapter, hands the pairing to the adapter,
  logs (DISC-16), closes it in Shutdown; not in AbortToTitle.
- Adapter: a pairing setter; Init accepts an empty candidate list in
  discovery mode (today :90-93); the DISC-12 pairing points, the Close then
  Begin retry, and the listen filter refresh.
- Isolation updates: host include allow-list (rule 3) gains the service and
  SHA-256 headers; host library pin (rule 4, :883) gains the service
  library; netplay pins unchanged.
- Unit tests: tests/native_arcade_netplay_test.c (pairing applied at
  Begin, not during a session; unpaired WAITING then solo; pairing change
  during solo refreshes the filter) and tests/native_arcade_link_host_test.c
  (static mode opens no discovery socket).
- Live test `arcade_discovery_link`, labels `live;live-link`: two processes
  on 127.0.0.1 with `--arcade-link auto`, link ports 7301 and 7302,
  discovery ports 7303 and 7304 each targeting the other, NO peer and NO
  fixed seat, the linked autopilot with a short race-tick cap. The checker
  requires both PASS, one "pair found" line each naming the other's
  address, opposite seats, 7301 as cab1, and matching agreed-match lines.
  Add its line to the milestone agent's live test map
  (`.claude/agents/milestone.md`, local, not in the repo).
- Reviewer pass required (link flow).
- Gate: fast suite plus `-L live-link`.

### DISC-S5 -- package

- One template `arcade.cfg` with `seat = auto` replaces cab1.cfg and
  cab2.cfg (tools/package/, tools/package-arcade.ps1 file lists :48,
  :196-198, :209). tools/package-arcade-smoke.ps1 derives two loopback
  copies (distinct link ports, the DISC-18 flags on the command line).
- tools/package/README.txt and docs/PACKAGING.md: PK-3, PK-8, PK-9 (the
  DISC-17 rule), and operator notes (one config for every cabinet; `seat`
  as an optional override; `group` to keep two installations apart).
- Gate: Release build and `-L live-package`.

### DISC-S6 -- status and fleet acceptance

- docs/HANDOFF.md status notes (not "## Next work") and this document's
  status.
- Physical two-cabinet acceptance on the fleet after the DISC-17 C:\Arcade
  follow-up: both cabinets on `seat = auto`, pairing within seconds of
  boot, seats .11 cab1 and .12 cab2, one linked race; one cabinet powered
  off gives solo on the other; powering it back on links at the next
  LOBBY.

### Later -- 3-4 cabinets and humans

Roles beyond two, pairing of N, a match config V2 with more human roles.
The beacon (echo entries), the table (8), and the lobby's 8 candidates
leave room; nothing here blocks it.

## 5. Risks and open items

1. Firewall profile. A cabinet LAN on a dumb switch may be classified
   Public; a rule scoped to Private would drop every beacon. DISC-17 keeps
   `-Profile Any`. A dismissed "allow this app" prompt can leave a Block
   rule for the exe, which the fleet script already removes (step 5).
2. Windows sends the limited broadcast out of one interface only on a
   multi-NIC host. DISC-5 adds the directed broadcast of every interface.
3. A third cabinet of the same group and identity. Each side pairs with its
   lowest-key peer, so two may pair and the third keeps trying a busy peer
   (CONNECTING, then WAITING, with solo offered). Documented limit; the
   `group` key separates installations.
4. Restarts at different times. Seats are stable on fixed IPs (DISC-8). A
   restarted process has a new nonce; its old entry would stay eligible for
   up to 10 s, so DISC-6 replaces an entry at once when a new nonce arrives
   from the same address and link port.
5. Loopback does not exercise broadcast. `arcade_discovery_link` proves the
   pairing, election, and lobby wiring, not the broadcast path or the
   interface list (the lobby milestone's own reason for leaving broadcast
   out, docs/LOBBY_MILESTONE.md:157-159). The broadcast path needs a manual
   check or the S6 fleet acceptance.
6. The host does not tick during the boot intro, the retail attract demo
   race, or a load outside a race (section 2.6). A cabinet in a demo stops
   beaconing, and its peer drops it after 10 s; the pairing returns within
   about 2 s of the title. The LOBBY retry re-reads the pairing, so the
   cost is at worst a solo offer. S4 checks this live; if it matters, a
   discovery-only host entry called on every frame the hook does not tick
   is the fix.
7. A mixed pair (one static, one discovery) never links (DISC-11). The
   operator card says so.
8. Nonce entropy comes from the same clock sources as `selectEntropy`, so
   the two are correlated. Acceptable on a trusted kiosk LAN (SEL-13,
   docs/MATCH_SELECT_MILESTONE.md:642-643); the beacon carries no secret.
   A LAN host can forge beacons, but the handshake still checks identity.
9. Windows reports an ICMP port-unreachable for an earlier send as a
   recvfrom error (WSAECONNRESET), which the transport returns as
   RECEIVE_ERROR (native_udp_transport.c:162-177). With explicit targets
   this happens while the peer is not yet up. The service ends that tick's
   drain and carries on (DISC-15); S3 may instead turn it off with
   SIO_UDP_CONNRESET in the transport. Port 7000 taken by another program
   is DISC-15's bind failure: the cabinet stays usable, solo only.

## 6. Status

- DISC-S1, this plan: the commit that adds this file.
- DISC-S2 to DISC-S6: not started.
