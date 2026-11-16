# Changelog

All notable changes to EHS Link. Versions follow `major.minor.patch`.

Aircraft profiles are versioned separately (`[Update] version` in `profiles.ini`) and reach
pilots automatically at every sim start, so new or fixed aircraft don't need a new release.

## 0.2.0 — 2026-11-16

**Who needs to update:** everyone (pilots: both files; controllers: `EhsLink.dll`).

### Pilots
- **No more FSUIPC.** A small module in the MSFS Community folder reads the aircraft directly;
  installation is now two drag-and-drops.
- **PMDG needs no setup:** the 777 and 737 are read from the aircraft's own variables, so the
  `EnableDataBroadcast` / read-only options file step is gone (custom 777 builds work too).
- **Aircraft profiles** for 16 verified aircraft and ~19 drafts, **updated automatically** from
  GitHub at every sim start.
- **Request your aircraft:** dial ALT 12300 / HDG 123 / SPD 234 and the module saves
  `snapshot.txt` to attach to a GitHub issue.
- **New data sent:** altimeter setting (hPa/inHg) and current IAS/Mach (about every 5 s).
- **Flight plan marker:** `RMK/EHSLINK` in the remarks as an alternative to `CODE/FE5000`, so you
  can keep your real Mode S code.
- Managed (dashed) FCU/MCP windows are sent as `---` instead of the managed target.
- Fixed: vPilot could crash when closing.
- MSFS 2024: the module no longer declares a 2020-only minimum version (2024 still untested).

### Controllers
- **Three colours:** green = exactly as assigned, blue = close, red = off (or wrong unit).
  All colours and "close" margins are configurable (`.ehs color`, `.ehs tol`).
- **Altimeter check:** STD above the transition altitude, QNH from the METAR of the nearer
  of departure/destination below it or when cleared to an altitude. hPa shown like an altimeter
  (decimals cut, not rounded).
- **EHS Link list** (`.ehs list`): your clearance next to the pilot's selection, with
  EuroScope's usual edit popups; rows refresh live.
- New tag items: altimeter setting, current IAS, current Mach.
- Draft-profile data is marked `EHS~` with dimmed colours (`.ehs unverified off` to hide it).
- Pilots filed with `RMK/EHSLINK` or `CODE/FE5…` but not sending data show a grey `ehs`.
- Sound alerts are off by default (`.ehs sound on`).
- Only subscribes to aircraft in range, so it scales with the number of pilots.

## 0.1.0 — 2026-11-15

First test release: vPilot plugin reading the autopilot through FSUIPC, public MQTT broker,
EuroScope plugin with tag items and mismatch alerts.
