# SNORR passive neighbor signal reports

The `snorr/develop` fork adds a companion collector on RAK4631 and dedicated Heltec V3 gateway builds. It retains the targeted AHT sensor fix. Standard GPS, standard NeighborInfo (port 71), its schema and its existing interval clamps remain unchanged.

## Activation and limits

- Compiled into `rak4631` and `heltec-v3-gateway` with `USERPREFS_SNORR_NEIGHBOR_SIGNALS=1`. Ordinary `heltec-v3` and dedicated `heltec-v3-sniffer` builds do not instantiate it by default.
- Uses the existing `neighbor_info.enabled` setting, disabled by default. Enable it and reboot through the normal configuration workflow. No stored configuration is changed by this source update.
- `neighbor_info.transmit_over_lora=false` selects local app/MQTT reports; an isolated RAK needs it true to reach a gateway over radio. No automatic role, channel, power or broker changes.
- For a receive-only V3 gateway, build `heltec-v3-gateway`, enable `neighbor_info.enabled`, keep `neighbor_info.transmit_over_lora=false`, and preserve `lora.tx_enabled=false`. Existing Wi-Fi/MQTT, encrypted protobuf uplink and primary-channel keys must remain configured. The gateway is the report observer and MQTT gateway; report rows identify the direct senders it heard. Local reports require no radio probes or transmit permission.
- Direct receptions on the primary channel only, after normal decoding/duplicate filtering. Require a nonzero nonlocal sender, supported zero-hop metadata, non-MQTT origin, negative valid RSSI and finite valid SNR. RAK's SX126x receive path supplies both readings; **0 dB SNR is valid**. Unknown/relayed senders are omitted rather than attributed to an original source or one-byte relay ID.
- Store 32 entries by default, replacing the least recently heard when full. `USERPREFS_SNORR_NEIGHBOR_CAPACITY` allows 1–255 at build time. Expire entries after 30 minutes without reception, independent of reporting cadence, using unsigned monotonic age. No wall-clock synchronization needed.
- Counts are accepted module observations since that entry was created, saturating at uint32 maximum. Duplicates filtered upstream, undecodable packets and excluded traffic do not count. Counts are not RF packet totals or delivery percentages.

## Reporting behavior

The first opportunity is 60–75 seconds after boot. Subsequent opportunities run on a 15-second scheduler tick. Changed observations coalesce; empty reports are suppressed. At most eight rows (152 payload bytes) are sent per opportunity. Larger tables rotate across packets; they continue rotating until entries expire even without new traffic. Rows are read at each report's creation, not a frozen multi-packet snapshot. An omitted row never deletes a link or proves it is unavailable.

Radio reports read the saved `telemetry.device_update_interval` on each eligible scheduler pass. Zero/unset uses `USERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS` as the fallback: **180 seconds** on ordinary `rak4631`, 300 seconds on the generic/gateway build. Nonzero values below 180 seconds are clamped to 180. Values above **2,147,468 seconds** (about 24.85 days) saturate at that baseline before conversion to milliseconds; the cooldown including maximum jitter stays below half the uint32 timer range. There is no one-hour cap. This couples device and radio SNNS baselines; environment and power retain their separate saved intervals. The ordinary NeighborInfo update interval does not control SNNS and retains its four-hour minimum/six-hour default.

Local reports retain their **60-second** baseline independently of device telemetry. Each report samples 0–14,999 milliseconds of jitter once; the 15-second scheduler tick can add further delay. A shorter setting or mode change preserves the previous report's baseline cooldown plus its sampled jitter once; a longer setting extends the cooldown with that same jitter. The next report starts a new cooldown using the current baseline. Missed opportunities never cause burst catch-up. An enqueue at timer value zero still starts a cooldown, including across rollover.

Admin telemetry writes replace the in-memory telemetry configuration. Outside an edit transaction they save and schedule the normal reboot; commit saves/reboots an open transaction. Until reboot, the module follows the cooldown rules above. After reboot it uses the saved setting, a new boot identifier/sequence and the normal 60–75-second startup opportunity; the previous boot's cooldown is not persisted. The first report still requires fresh observations. Admin authorization, persistence and over-radio readback require their separate selected-board acceptance.

The shared radio path uses this saved setting on both enabled targets, but gateway local mode ignores it and does not alter `lora.tx_enabled=false`. No gateway profile or transmit permission is changed by this implementation.

Radio reports require a nondefault primary channel, LoRa transmit enabled, a role other than CLIENT_MUTE, and both existing channel/airtime admission checks. Failed admission defers 30 seconds; a later check can defer again. Reports have BACKGROUND priority, no requested ACK and no requested response. No active probes or additional GPS transmissions are generated. Local mode uses `NODENUM_BROADCAST_NO_LORA`; the existing router performs MQTT handling before the radio discards that destination. MQTT still requires its normal connection/uplink settings.

The send is a best-effort enqueue attempt. A report sequence identifies attempts; it does not acknowledge gateway/backend delivery. Congestion, forwarding filters, broker ACLs and radio outages can prevent reception. This build does not add retries or a reliable transport. A stock gateway can export the opaque private payload through its protobuf MQTT path, provided it permits private-port traffic (`CORE_PORTNUMS_ONLY` rejects it); this requires bench verification.

## Version 1 wire contract

Port **256** (`PRIVATE_APP`), payload magic **SNNS**. This is a project-local use of the shared private port, not a global port assignment. All multibyte integers are little endian; signed values use two's complement. There is no struct packing or native-float dependency on the wire.

| Offset | Bytes | Meaning                                                                                                           |
| ------ | ----- | ----------------------------------------------------------------------------------------------------------------- |
| 0      | 4     | ASCII `SNNS`                                                                                                      |
| 4      | 1     | Version = 1                                                                                                       |
| 5      | 1     | Flags: bit 0 = table exceeds per-report row cap; bit 1 = capacity eviction occurred during this boot; others zero |
| 6      | 1     | Rows in this report, 1–8                                                                                          |
| 7      | 1     | Current unexpired table count, 1–configured capacity                                                              |
| 8      | 4     | Full observer node ID                                                                                             |
| 12     | 4     | Random boot identifier (nonzero 31-bit correlation token, not authentication)                                     |
| 16     | 4     | Report attempt sequence, modulo uint32                                                                            |
| 20     | 4     | `millis()/1000` at encoding; resets at boot and the millisecond timer rollover                                    |
| 24+16n | 4     | Full heard node ID                                                                                                |
| 28+16n | 4     | Seconds since this neighbor was heard when report was encoded                                                     |
| 32+16n | 2     | Signed RSSI, dBm                                                                                                  |
| 34+16n | 2     | Signed SNR in quarter dB; divide by 4                                                                             |
| 36+16n | 4     | Accepted observation count for this entry                                                                         |

Exact payload length must equal `24 + rows * 16`. Every exported row has both readings; missing measurements are not encoded as zero. Consumers must reject unknown versions/flags, malformed lengths/counts and observer IDs inconsistent with packet provenance. Preserve the gateway receipt independently of the observer and heard identities. Age describes the observer's state **at encoding**; mesh/backhaul delay adds age that this payload cannot determine. Never label gateway/backend receipt time as the neighbor's last hearing time. Deduplicate gateway copies by packet/content identity while retaining distinct receipt evidence.

This change supplies firmware reports only. The current portal adapter archives opaque packets but does not decode `SNNS`; connecting these rows to the neighbor table needs a separately tested ingestion change. Do not relabel demo rows as measured data.

## Portable regression checks

From this firmware directory on a host with GCC:

```sh
python3 test/test_snorr_neighbor_signals/run.py
```

The runner uses GCC with AddressSanitizer and UndefinedBehaviorSanitizer. It checks both 180/300-second fallback builds and collector capacities 32/10. The collector also has a normal Unity/native entry point. Its checks cover filtering, actual byte encoding (including zero/negative SNR), update counts, bounded packet rotation, least-recently-heard eviction and expiry across timer rollover. They do not prove RF delivery or runtime.

The module harness compiles the actual module implementation and `src/mesh/Throttle.cpp` with test doubles for packet transport, airtime, time, settings and the scheduler boundary. It checks unset/below-minimum/180/300/900-second settings, longer-than-hour/day settings, saturation boundaries and uint32 extremes, shortening/lengthening, retained jitter, reboot reconstruction, timer rollover (including a report at zero), both local/radio transitions, no catch-up, empty reports, primary-channel filtering, module enablement, public-channel/transmit/role/both airtime gates and report priority/ACK behavior. It records constructor startup delay and checks returned scheduler delays; it does not execute the real scheduler, admin transport, flash persistence, router, radio or MQTT implementation. A configured interval remains subject to congestion and airtime admission. NodeInfo retains its existing suppression policy.

Before rollout: preserve current profiles/keys, identity-check a bench RAK, flash the built image with owner authorization, explicitly configure/read back NeighborInfo settings, then verify private-port receipt through both stock gateways, decoding and reporting/expiry under congestion/outages. Compare passive GPS freshness, airtime and runtime against the stock baseline. Sensor calibration, placement, solar sufficiency and field coverage remain separate measurements.
