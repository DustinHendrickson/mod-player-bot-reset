# AzerothCore Module: Player Bot Level Reset

## Overview

This module for **AzerothCore** resets the level of **player bots** when they exceed a configurable maximum level. It supports **random player bots** and integrates with the **AutoMaintenanceOnLevelupAction** system to reinitialize the bot's state upon reset.

## Features

- **Configurable Maximum Level**: Set the maximum level a bot can reach before being reset (or disable level resets entirely).
- **Configurable Reset Level**: Specify the level bots will be reset to (defaults to level 1).
- **Level Skip Functionality**: Configure bots to jump from a specific level directly to another level.
- **Configurable Reset Chance**: Specify the percentage chance for a bot's level to reset upon reaching the maximum level.
- **Scaled Reset Chance**: Optionally enable per-level checks where the reset chance scales dynamically as the bot levels up. The chance increases as the bot approaches the maximum level, reaching the configured Reset Chance at the maximum.
- **Support for Random Bots**: Applies only to bots managed by `RandomPlayerbotMgr`.
- **Auto Equipment Reset**: Destroys all equipped items when resetting a bot.
- **Auto Maintenance Execution**: Executes `AutoMaintenanceOnLevelupAction` after a reset to ensure proper bot initialization.
- **Death Knight Support**: For Death Knight bots, resets the level to 55 or higher.
- **Time-Played Based Reset**: When enabled, bots at or above the maximum level are reset only if they have accumulated a minimum amount of played time at that level. This check is performed periodically via an OnUpdate handler.
- **Bot Name Exclusion**: Optionally exclude specific bots from reset processing by name.
- **Guild-Based Exclusion**: Optionally exclude bots that are in guilds with real (non-bot) players, even when those players are offline.
- **Debug Mode**: Provides optional detailed logging for debugging purposes.

## Installation

Ensure you have the **AzerothCore Playerbots fork** installed and running.

Clone the module into your AzerothCore modules directory:

```sh
cd /path/to/azerothcore/modules
git clone https://github.com/DustinHendrickson/mod-player-bot-reset.git
```

Recompile AzerothCore:

```sh
cd /path/to/azerothcore
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Update the module configuration by renaming the configuration file:

```sh
mv /path/to/azerothcore/modules/mod-player-bot-reset.conf.dist /path/to/azerothcore/modules/mod-player-bot-reset.conf
```

Restart the server:

```sh
./worldserver
```

## Configuration Options

Modify the following settings in `mod-player-bot-reset.conf` to customize the module's behavior:

| Setting                               | Description                                                                                                                             | Default  | Valid Values            |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- | -------- | ----------------------- |
| `ResetBotLevel.MaxLevel`              | Maximum level before checking for a reset.                                                                                             | `80`     | `2-80` (or `0` to disable) |
| `ResetBotLevel.ResetToLevel`          | The level bots will be reset to. For Death Knights, if this is below 55, they will be reset to 55 instead.                              | `1`      | `1-79` and < MaxLevel   |
| `ResetBotLevel.SkipFromLevel`         | When a bot reaches exactly this level, they will be sent directly to the level specified by SkipToLevel.                                | `0`      | `1-80` and < MaxLevel (or `0` to disable) |
| `ResetBotLevel.SkipToLevel`           | The level bots will be sent to when they reach SkipFromLevel. For Death Knights, if this is below 55, they will be reset to 55 instead. | `1`      | `1-80` and <= MaxLevel  |
| `ResetBotLevel.ResetChance`           | Percentage chance to reset upon reaching the maximum level.                                                                            | `100`    | `0-100`                 |
| `ResetBotLevel.ScaledChance`          | If enabled (1), the reset chance is evaluated on every level-up and scales based on the bot's current level relative to max level.       | `0`      | `0 (off) / 1 (on)`      |
| `ResetBotLevel.DebugMode`             | Enables detailed debug logging for module actions.                                                                                     | `0`      | `0 (off) / 1 (on)`      |
| `ResetBotLevel.RestrictTimePlayed`    | If enabled (1), bots will only be reset when they have played at least the specified minimum time at the current level when at max level.| `0`      | `0 (off) / 1 (on)`      |
| `ResetBotLevel.MinTimePlayed`         | The minimum time in seconds that a bot must have played at its current level before a reset can occur when at max level.                 | `86400`  | Positive Integer (3600 = 1 hour, 86400 = 1 day, 604800 = 1 week) |
| `ResetBotLevel.PlayedTimeCheckFrequency` | The frequency (in seconds) at which the time played check is performed for bots at or above the maximum level.                        | `864`    | Positive Integer (recommended: 1% of MinTimePlayed or 300 seconds, whichever is higher) |
| `ResetBotLevel.ExcludeNames`          | Comma-separated list of case insensitive bot names to exclude from reset processing.                                                   | `""`     | Comma-separated string  |
| `ResetBotLevel.IgnoreGuildBotsWithRealPlayers` | If enabled (1), bots that are in guilds with real (non-bot) players are excluded from reset processing, even when real players are offline. | `0`      | `0 (off) / 1 (on)`      |

## Debugging

To enable detailed debug logging, modify the configuration as follows:

```ini
ResetBotLevel.DebugMode = 1
```

This will output detailed logs for actions such as bot resets, item destruction, and auto maintenance execution.

## License

This module is released under the **GNU AGPLv3** license, in accordance with AzerothCore's licensing model.

## Contribution

Created by Dustin Hendrickson.


Pull requests and issues are welcome. Please ensure your contributions follow AzerothCore's coding standards.
