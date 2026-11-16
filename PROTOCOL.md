# EHS Link protocol v1

A simplified "Enhanced Mode S" downlink for VATSIM. Pilots running the vPilot plugin
publish their autopilot selections; controllers running the EuroScope plugin receive them.
Nothing goes over the VATSIM network itself: data travels over a public MQTT broker.

## Transport

- MQTT 3.1.1, plain TCP, port 1883, QoS 0.
- Default broker: `broker.hivemq.com`, failover `broker.emqx.io`. Both sides use the
  same ordered list, so they converge on the same broker. Configurable on both sides.
- Topic: `ehslink/v1/<CALLSIGN>` (callsign upper-case, `A-Z 0-9 _` only).
- Messages are **retained**, so a controller who connects later gets current values at once.
- Controllers subscribe **per callsign**, only to aircraft their EuroScope currently sees
  (re-synced every second), never to a wildcard. Broker load therefore grows with the
  number of pilots, not pilots × controllers. A new subscription immediately delivers the
  retained values.
- Pilot sets a **Last Will**: an empty retained message on its topic. If the pilot's
  client dies, the broker clears the topic. A clean disconnect does the same explicitly.
  An empty payload means "no data for this callsign".

## Flight plan marker

Pilots announce EHS Link in their flight plan in either of two ways (controllers accept both):
- the word `EHSLINK` in the remarks, typically `RMK/EHSLINK` (whole word, case-insensitive),
  which lets pilots keep their real Mode S code; or
- a Mode S code (ICAO 24-bit address) of `FE5` + 3 hex digits, e.g. `CODE/FE5000` in item 18
  (SimBrief's "Mode S code" airframe field). The `0xF…` block isn't allocated to real aircraft,
  so real airframe addresses never match.

A pilot is recognized by live data *or* the marker; neither requires the other.

## Timing

| What | When |
|---|---|
| Sim polled | every 250 ms |
| Publish on change | immediately, at most 1 per second |
| Current IAS/Mach changed (≥ 2 kt or 0.01 M) | at most every 5 s (like a radar sweep) |
| Heartbeat (unchanged values) | every 10 s |
| Controller treats data as stale | no message for 30 s |

## Payload (UTF-8 JSON, flat object)

```json
{"v":1,"cs":"BAW117","t":1763215200,"lat":51.4706,"lon":-0.4619,
 "src":"PMDG777","alt":24000,"hdg":270,"trk":false,"ias":null,"mach":0.78,"vs":null,
 "baro":1013.2,"baroUnit":"inHg"}
```

| Field | Type | Meaning |
|---|---|---|
| `v` | int | Protocol version (1) |
| `cs` | string | Callsign (same as topic) |
| `t` | int | Sender Unix time, seconds |
| `lat`, `lon` | number | Aircraft position, degrees. Lets the controller side reject data whose position doesn't match the radar target (anti-spoofing) |
| `src` | string | Data source: `PMDG777`, `PMDG737`, `SIM` (standard simulator variables) |
| `alt` | int or null | Selected altitude, feet |
| `hdg` | int or null | Selected heading (or track, see `trk`), degrees 1–360 |
| `trk` | bool | `true` when the heading window is in TRK mode |
| `ias` | int or null | Selected IAS, knots. `null` when Mach is selected or the window is blank |
| `mach` | number or null | Selected Mach. `null` when IAS is selected or the window is blank |
| `vs` | int or null | Selected vertical speed, ft/min, signed. `null` when not in V/S mode (window blank, FPA mode) |
| `baro` | number or null | Altimeter setting, hPa, 1 decimal (1013.2 when STD is selected) |
| `baroUnit` | string or null | Unit the pilot has selected on the EFIS: `"hPa"` or `"inHg"`; `null` if unknown |
| `cias` | int or null | **Current** indicated airspeed, kt (actual, not selected) |
| `cmach` | number or null | **Current** Mach (actual, not selected) |
| `ver` | bool | `true` when the aircraft profile has been verified against the real cockpit |

Receivers must ignore unknown fields.
