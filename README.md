# EHS Link

A free, simplified **Enhanced Mode S downlink** for VATSIM: pilots share their autopilot
selections (selected altitude, heading/track, IAS/Mach, V/S), and controllers see them in
EuroScope tags, with an alert when they don't match what was assigned.

Nothing travels over the VATSIM network. Data goes through a free public MQTT broker
(see [PROTOCOL.md](PROTOCOL.md)). No accounts, no servers to run, no paid software.

## Pilots (vPilot + MSFS)

Requirements: MSFS, vPilot. Nothing else (no FSUIPC).

1. **Drag `Community\ehslink-module` into your MSFS Community folder.**
2. **Drag `vPilot\EhsLink.vPilot.dll` into vPilot's `Plugins` folder** (e.g. `C:\vPilot\Plugins`).
3. Restart MSFS and vPilot.
4. **PMDG 777 / 737 only:** PMDG publishes its MCP data only when you allow it, once per
   aircraft. With the aircraft *not* loaded, add these lines to the end of the aircraft's
   options file, then set the file to **read-only** (PMDG rewrites it otherwise):
   ```
   [SDK]
   EnableDataBroadcast=1
   ```
   MS Store: `%LOCALAPPDATA%\Packages\Microsoft.FlightSimulator_8wekyb3d8bbwe\LocalState\packages\pmdg-aircraft-77w\work\777_Options.ini`
   (same for `-77er`, `-77f`; the 737 uses `pmdg-aircraft-738\work\737_Options.ini`).
5. **Announce it in your flight plan (SimBrief):** set the airframe's **Mode S code** to
   `FE5` followed by any 3 characters from `0-9 A-F`, e.g. **`FE5000`**. SimBrief files it as
   `CODE/FE5000`, and controllers see you as EHS Link equipped even before your data arrives.
   `F…` codes aren't allocated to real aircraft, so a real airframe's code can never be
   mistaken for the marker.
6. Connect to VATSIM. vPilot's message window shows which aircraft profile EHS Link is using.

**Aircraft support** comes from `profiles.ini` inside `ehslink-module`: one section per
aircraft, using the aircraft's own variables (A:vars, L:vars) or PMDG's data. Aircraft without
a profile use the simulator's standard autopilot variables, which suit default aircraft but
not study-level add-ons with their own FCU/MCP logic; those need a profile. Profiles marked
`verified = yes` have been checked against the real cockpit. New profiles are welcome as pull
requests (see the comments at the top of `profiles.ini`).

**Profiles update themselves.** At every sim start the module downloads
`msfs-module/package/profiles.ini` from this repository and uses it when its `[Update] version`
is higher than the one it has. It keeps the copy in its work folder, so it also works offline.
Pilots never need to re-download the package for new aircraft. For maintainers:
- Bump `version` in `[Update]` whenever you change profiles.
- Downloaded files must parse, contain a fallback profile and be **read-only**: any expression
  that writes to the aircraft (`(>L:…`, `(>K:…`) makes the module reject the whole file.
- The download URL is only taken from the copy shipped inside the package, never from a
  downloaded file.

Optional `EhsLink.ini` next to the DLL:
```
brokers = broker.hivemq.com:1883, broker.emqx.io:1883
min_interval_ms = 1000
heartbeat_ms = 10000
```

## Controllers (EuroScope)

1. Copy `EhsLink.dll` anywhere (e.g. `%APPDATA%\EuroScope\PlugIn`) and load it in
   *Other SET > Plug-ins*.
2. Add the TAG items in the TAG editor:
   - `EHS selected altitude`: `240`
   - `EHS selected heading`: `270`, or `270T` in TRK mode
   - `EHS selected speed`: `250` or `M78` (`---` = window blank / managed)
   - `EHS selected V/S`: `-15` = -1500 ft/min (`---` = not in V/S mode)
   - `EHS summary (ALT HDG SPD)`: `240 270 M78`
   - `EHS altimeter setting`: `1013` (hPa) or `29.92` (inHg), in the pilot's own unit by default (`.ehs baro` to force one). STD shows as 1013 / 29.92
   - `EHS status`: `EHS` data arriving, `ehs` (grey) filed `CODE/FE5…` but no data arriving (pilot's plugin not running), `EHS!` mismatch has lasted past the grace period, `EHS?` data position doesn't match the radar target (ignored)
3. **Colours (live):** each item compares the pilot's selection with your **cleared altitude,
   assigned heading, speed/Mach or rate**, and the altimeter setting with the expected one:

   | Colour | Meaning |
   |---|---|
   | **Green** (110,190,120) | exactly what you assigned |
   | **Blue** (100,160,235) | close: within 300 ft, 5°, 5 kt, M0.05, 200 ft/min, 1 hPa / 0.01 inHg |
   | **Red** (230,110,110) | further off, or wrong unit (Mach selected when you assigned IAS, and vice versa) |

   **Altimeter reference:** STD (1013 / 29.92) is expected above your transition altitude;
   QNH is expected below it, or when you've cleared the aircraft to an altitude below it.
   The QNH comes from the METAR of the nearer of the flight's origin and destination, so
   those airports need to be in your METAR list. No METAR means the altimeter stays in
   the normal tag colour. The comparison happens in the pilot's own unit.

   Items with nothing assigned keep the normal tag colour. The summary and status items show
   the worst colour of all fields. All three colours and all margins are yours to change
   (`.ehs color`, `.ehs tol`).
4. **Lists:**
   - The plugin adds an **"EHS Link" list** (`.ehs list` to open it) with every equipped
     aircraft in range, assigned value next to the pilot's selection:
     `C/S  TYPE  CFL SEL  AHDG SHDG  ASPD SSPD  ARTE SV/S  QNH  EHS`.
     (Controllers who already opened the list keep their saved layout: add the
     *EHS altimeter setting* column by hand.)
     The pilot columns use the same green/red colours. Clicking CFL, AHDG, ASPD (right-click
     for Mach) or ARTE opens EuroScope's usual edit popup, and clicking the callsign opens the
     flight plan. You can rearrange the columns, and EuroScope saves the layout.
   - The EHS items can also be added as columns to **any standard EuroScope list** (departure,
     arrival, SIL/SEL…): in the list's column setup, pick the *EHS Link* item provider.
5. **Notifications:** if a red mismatch lasts longer than the grace period (10 s), a message
   appears in the *EHS Link* chat tab (by default only for aircraft you're tracking).
   **Sound is off by default** (`.ehs sound on` to enable).

Commands:
```
.ehs                          status
.ehs list                     open the EHS Link list
.ehs code FE5                 flight plan marker prefix (CODE/FE5xxx)
.ehs baro pilot|hpa|inhg      altimeter display unit (default: pilot's unit)
.ehs color match 110,190,120  exact-match colour (R,G,B, or 'default')
.ehs color near 100,160,235   close-but-not-exact colour
.ehs color mismatch 230,110,110  mismatch colour
.ehs notify mine|all|off      who triggers chat notifications
.ehs sound on|off|C:\x.wav    notification sound (default off)
.ehs grace 10                 seconds a mismatch must last before alerting
.ehs tol alt 300              blue margins: alt (ft), hdg (deg), spd (kt), mach, vs (ft/min), qnh (hPa), inhg
.ehs brokers host[:port],...  broker list (must match the pilots')
```

## Building

- Everything + release zip: `package.ps1 -Version 0.1.0` → `release\EHS-Link-0.1.0.zip`.
- MSFS module: `msfs-module\build.ps1` (needs the free MSFS SDK; uses `MSFS_SDK`). Profile parser tests:
  `msfs-module\test\profiles_test.cpp` (`cl /std:c++17 /EHsc profiles_test.cpp`, run with `..\package\profiles.ini`).
- vPilot plugin: `dotnet build vpilot-plugin -c Release` (needs vPilot installed at `C:\vPilot`, or pass `-p:VpilotDir=...`).
- EuroScope plugin: `euroscope-plugin\build.bat` (needs VS 2022 Build Tools with C++; uses the EuroScope SDK in `%APPDATA%\EuroScope\PlugIn`, or set `ES_SDK`).
- Tests: `euroscope-plugin\build\ehs_test.exe` (unit tests), `ehs_test.exe listen 30 [CALLSIGN]` (watch the live broker, all pilots or one),
  `tools\ehs-tool`: `EhsTool sim CALLSIGN 30 [lat lon]` simulates a pilot; `EhsTool probe [s]` shows what the MSFS module reports; `EhsTool load <plugin.dll>` loads the vPilot plugin in a fake vPilot.

## Capacity

- Each pilot sends one ~300-byte message on change (max 1/s) plus a heartbeat every 10 s.
- Each controller only subscribes to the aircraft its EuroScope can see, typically 20–100, so a
  controller's traffic doesn't depend on how many pilots are online worldwide.
- Broker load grows with the number of pilots. A free public broker comfortably handles
  hundreds of pilots, but makes no promises. For a large rollout, run your own broker
  (e.g. Mosquitto, free) and point both plugins at it with the broker list setting.

## Limitations

- Only pilots running the plugin send data: treat it as an aid, never as a basis for separation.
- Public brokers have no uptime guarantee and anyone can publish; the controller plugin rejects
  data whose position is more than 10 NM from the radar target.

## License

MIT, see [LICENSE](LICENSE). EuroScope's plugin SDK and vPilot's plugin interface belong to
their authors and aren't included in this repository.
