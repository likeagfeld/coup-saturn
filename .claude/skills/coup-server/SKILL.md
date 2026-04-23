---
name: coup-server
description: Start the Coup game server with bot players for testing. Use when the user wants to play Coup, test the server, or needs bots.
argument-hint: "[num_bots]"
---

# Coup Local Server with Bots

Start the Coup game server on port 4821 with bot players. By default, no bot will auto-start the game — the human player must start it manually. Pass `--auto-start` as an extra argument to have the first bot auto-start when all players are ready.

Arguments: $ARGUMENTS (number of bots, default 5; optional `--auto-start` flag)

## Steps

1. Kill any existing server/bot processes on port 4821
2. Build the C rule engine: `./build.sh coup-lib`
3. Start the server: `python3 tools/coup_server/server.py --port 4821` (background)
4. Wait for server to be listening
5. If `--auto-start` was passed: start the first bot with `--auto-start <num_bots+1>`. Otherwise: start with `--auto-start 0` (disabled). Bot name: "SaturnBot". (background)
6. Start remaining bots with names BotAlpha, BotBravo, BotCharlie, BotDelta, BotEcho (background, all with `--auto-start 0`)
7. Confirm all bots are in the lobby and ready
8. Report: server IP (use `ipconfig getifaddr en0`), port, number of bots, and whether auto-start is enabled or the human needs to start the game manually

## Important

- ALL processes must be run in the background using `run_in_background`
- The first bot MUST connect before other bots so it becomes the host
- Wait ~1 second between starting the host bot and the others
- Bots have a 2-second think delay before each action
- The human connects via the DreamPi bridge: `--server <mac_ip>:4821`
- Do NOT restart the server while a human might be connected unless explicitly asked
