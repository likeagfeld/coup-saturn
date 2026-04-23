# TODO

Tracked ideas, bugs, and improvements across the project.

Legend: `[chat]` `[netplay]` `[multiplayer]` `[core]` `[pal/saturn]` `[pal/sdl]` `[infra]`

---

## Stabilization

Items to fix before building new features.

- [ ] `[pal/saturn]` TX send is fully blocking -- `saturn_uart_putc()` busy-waits per byte; a 68-byte chat message stalls ~24ms (1-2 dropped frames)
- [ ] `[pal/saturn]` No `is_connected` callback on Saturn transport -- connection loss is invisible until bytes fail to send
- [ ] `[pal/saturn]` Modem error recovery is manual -- disconnects require user to press A to retry, no automatic reconnect
- [ ] `[chat]` HISTORY message processing burst -- up to 50 messages parsed + logged in a single frame on connect
- [ ] `[pal/saturn]` BUP (backup RAM) storage not implemented -- `uuid_storage.c` is stubbed out, UUIDs don't persist across power cycles
- [ ] `[netplay]` Ping/latency calculation not implemented (`netplay.c:117`, `netplay.c:224`)

## Testing

Coverage gaps that should be filled.

- [ ] `[core]` Transport layer has no unit tests -- `cui_transport_t` helpers, send/recv paths untested
- [ ] `[core]` Protocol binary encode/decode edge cases -- oversized strings, zero-length fields, truncated frames
- [ ] `[core]` `sncp_rx_poll` state machine -- partial frames across multiple calls, overflow recovery, empty frames
- [ ] `[chat]` End-to-end offline mode test -- verify full update/tick/render cycle without transport
- [ ] `[pal/saturn]` UART loopback self-test has no automated validation -- `saturn_uart_loopback_test()` exists but nothing exercises it in a test suite
- [ ] `[pal/sdl]` `test_harness_integration.c` has two TODOs for filesystem verification (lines 307, 333)

## Performance / Async

Networking performance improvements, ordered from quick wins to larger efforts.

- [ ] `[core]` Cooperative TX budget -- queue outgoing bytes in a ring buffer, drain a fixed count per frame (no Slave SH-2 needed, immediate win)
- [ ] `[core]` Bounded RX processing -- limit `process_binary_message()` to N messages per frame (like `saturn_netlink_recv_bounded` does for XMP packets)
- [ ] `[pal/saturn]` Slave SH-2 ring buffer transport (Approach B) -- `slSlaveFunc()` dispatches `net_tick()` per frame; Master reads/writes ring buffers in cache-through RAM, never touches UART
- [ ] `[pal/saturn]` Slave SH-2 persistent I/O loop (Approach A) -- Slave runs continuously, bypasses SGL slave management; needed if Approach B is insufficient for multiplayer latency
- [ ] `[pal/saturn]` Interrupt-driven RX via SCU External Interrupt 12 -- ISR fills ring buffer on UART Data Ready; can complement either Slave approach
- [ ] `[pal/saturn]` Quantify A-Bus contention -- measure UART access latency while VDP2 is active vs idle

## Multiplayer

Future work toward real-time game state sync.

- [ ] `[core]` Define game state sync protocol -- fixed-size state snapshots vs delta encoding vs input relay
- [ ] `[core]` Evaluate XMP (`saturn_netlink.h`) vs raw UART (`saturn_uart16550.h`) for multiplayer -- XMP gives sockets/sessions but adds overhead; raw UART is leaner but needs more infrastructure
- [ ] `[netplay]` Integrate netplay demo with cui rendering -- display player list and button state visualization using cui components
- [ ] `[netplay]` Add rollback or input delay for latency compensation
- [ ] `[multiplayer]` Stress test auction game with simulated latency
- [ ] `[core]` Design lobby/matchmaking flow that works across chat and game contexts

## Infrastructure

Unified NetLink service and tooling.

- [ ] `[infra]` Fork eaudunord/Netlink tunnel -- work on unified routing locally, submit upstream when stable
- [ ] `[infra]` Unified dial-string routing -- parse ATDT dial string to select destination: peer IP → tunnel mode, matchmaking code → dreampipe.net, service code (e.g. `#900#`) → configured TCP server
- [ ] `[infra]` Route config file -- map dial codes to server addresses (e.g. `900 = chat.example.com:4821`), loaded at startup
- [ ] `[infra]` Merge bridge.py logic into forked tunnel -- bridge.py's transparent serial↔TCP relay is the same as tunnel's data phase; unify into one codebase
- [ ] `[infra]` Test coexistence -- verify existing NetLink games (Bomberman, VF Remix, etc.) still work through the same software after routing changes
- [ ] `[infra]` Submit upstream PR to eaudunord/Netlink when routing feature is proven

## Platform

Platform-specific improvements.

- [ ] `[pal/saturn]` SCI serial driver (`saturn_sci.h`) -- alternative to UART 16550 for some serial configurations
- [ ] `[pal/saturn]` XMP stubs need real implementation or build-time selection (`saturn_xmp_stubs.c:10`)
- [ ] `[pal/sdl]` SDL transport (TCP sockets) for development/testing parity with Saturn
- [ ] `[pal/n64]` Evaluate N64 networking options (if any)

## Ideas

Longer-term ideas not yet committed to.

- [ ] `[core]` Promise/future abstraction for async operations -- `cui_async_op_t` with status polling, usable across platforms
- [ ] `[core]` Transport statistics layer -- bytes sent/received, error counts, latency measurement at the `cui_transport_t` level
- [ ] `[chat]` Chat rooms / channels
- [ ] `[netplay]` Spectator mode
- [ ] `[multiplayer]` Lobby browser UI component
