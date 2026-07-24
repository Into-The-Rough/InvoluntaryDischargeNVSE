# Involuntary Discharge NVSE

An xNVSE plugin for Fallout: New Vegas that makes dropped weapons discharge on
impact. When an NPC dies or is disarmed, their weapon can fire off rounds as it
hits the ground. Automatic weapons spray a short burst.

No ESP is required.

## Features

- Weapons fire on landing based on condition and dropper's Luck stat
- Automatic weapons fire random bursts (configurable round count and interval)
- Recoil impulse kicks the weapon on each shot, scaled by ammo weight
- Gunshots create detection events so nearby NPCs react
- Handles NPC deaths, disarms, player drops, and scripted drops (via xNVSE OnDrop)
- Deliberate player inventory drops are exempt by default (`bIgnorePlayerDrops`)

## Requirements

- Fallout: New Vegas
- xNVSE

## Installation

Install the release files into the normal Fallout New Vegas data paths:

- `Data/NVSE/Plugins/`
- `Data/config/InvoluntaryDischargeNVSE.ini`

## Configuration

Settings live in `Data/config/InvoluntaryDischargeNVSE.ini`.

Reload config at runtime with:

```text
ReloadPluginConfig InvoluntaryDischargeNVSE
```

## Building

Open `InvoluntaryDischargeNVSE.vcxproj` in Visual Studio 2022 and build `Win32` `Release`.

The project file contains local post-build copy steps. If you are building in a
different environment, update or remove those paths first.

## License

Involuntary Discharge NVSE is licensed under the MIT license. See [LICENSE](LICENSE).
