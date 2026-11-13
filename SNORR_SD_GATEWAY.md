# Screenless Heltec V4 recording gateway

The `heltec-v4-sd-gateway` build extends the S3R2 / QSPI 2 MB PSRAM,
16 MB V4 base. The radio uses the global FSPI on SCK9/MISO11/MOSI10/CS8,
reset12, DIO1 14 and busy13. The manufacturer's FEM controls remain on
2/5/7/46 and Vext on 36. INA219 uses Wire on 17/18 at 0x40. SD uses its
own HSPI on 47/6/48/4 at 1 MHz. The display, secondary I2C, GPS, standard
NeighborInfo and RF rebroadcast are disabled. The MQTT receive entry point
is disabled. Own reports use normal LoRa admission. Local publish acceptance
establishes neither broker receipt nor server ingestion. The selected
private profile requires encryption, TLS, no JSON or proxy, no downlinks,
no sleep, and 300-second device, power, SNNS and SNGH baselines.

## SNSD v1

Exactly 52 bytes, little-endian. Bytes 0–3 are ASCII SNSD, byte 4 is 1.
Byte 5 bits 0–3 mean card detected, mounted, last write healthy, and valid
mounted-filesystem counts. No card-detect switch is fitted. An unsuccessful
probe may mean an absent or unreadable card. Byte 6 is the source clock
(0 unknown, 2 mesh, 3 NTP, 4 GPS). Byte 7 is reserved zero. Six uint32
values at offsets 8–31 hold the originating node, nonzero boot ID, persistent
report sequence, uptime seconds, source UTC (zero if unknown) and the effective
report interval in seconds.
At offsets 32 and 40 are uint64 used and total filesystem bytes from one
FatFs free-cluster query. Unknown counts encode as two zeros. At 48 is a
uint32 count of discarded event records in this boot. A near-full volume
can be recording successfully. SNGH remains its independent 32-byte format.

The first eligible report can go on a new installation. The reservation
is persisted before handoff so rapid reboots cannot repeat it. Subsequent
sends follow `moduleConfig.telemetry.device_update_interval`, with the same
180-second minimum, zero-value fallback and timer ceiling as SNGH. G001's
saved 300-second setting therefore applies to SD too. The packet carries this
effective interval; no separate SD cadence setting is needed. Shortening the
interval preserves the prior outstanding cooldown. After reboot, a credible
elapsed source interval also qualifies. Without a comparable clock the gateway
waits one full interval of uptime. The existing `snorr-sd/daily` reservation key
is retained for upgrade compatibility. A power cut after reservation can lose a report.
Channel/airtime admission is retried on the collector's scheduler. Queue
or transport failure after handoff is recorded and does not establish delivery.

The cross-language fixture uses node 9ea13cc0, boot 42, sequence 7, uptime
100, UTC 1700000000, used 8589934592, total 9007199254740993, dropped 3.
The expected hex is `534e5344010f0300c03ca19e2a000000070000006400000000f15365805101000000000002000000010000000000200003000000`.

## SML1 card format

A per-node directory SN followed by eight hexadecimal digits contains files
with eight hexadecimal serial digits and extension SML. Sequence is derived
from matching owned segment headers on the card. Segments are at most one MiB.
A 24-byte header starts with SML1, followed by the uint32 node, segment,
boot, schema 1, and CRC32 of the preceding 20 bytes. All integers are
little-endian. CRCs use IEEE CRC32, the same as the SNORR sniffer.

Each EVT1 starts with magic, uint16 payload length (0–640), uint8 event ID
and uint8 RTC source. Offset 8 is uint32 event sequence, 12 is uint64 uptime
ms, 20 is uint32 observed UTC, 24 is packet source, 28 packet ID, 32 related
event, 36 signed detail, 40 signed RSSI dBm, 42 signed quarter-dB SNR, 44
boot ID, then payload and uint32 CRC of header and payload. Raw RX stores
the radio bytes, read status and airtime milliseconds in related. Decode
and duplicate records reference the exact raw sequence. A negative decoded
detail denotes opaque/failure, and a nonnegative decoded detail is the
application port. Local/decoded payloads retain original application protobuf
bytes, including packet measurement times. Decoded admin contents are
excluded to protect credentials. RF TX start records retain the encrypted
frame and RadioLib return value. TX complete is observed at hardware IRQ.
Transport callbacks only submit bounded copies to the 32-slot queue. Packets
that did not reach the radio read callback, emissions suppressed before a
module creates a packet, and remote server outcomes are outside this record.

The reader's event table follows the enum in SnorrRecorder.h. Local publish
results use 1 for accepted and 0 for failed. Link bits represent Wi-Fi/MQTT.
Gap records carry the boot's discarded count, previous count and uncertain
unsynced records. Evictions carry deleted segment serial and cumulative count
for the boot. Boot records include version, nonsecret radio settings and reset
reason. Each clock sample carries its source; anchor records mark clock changes.
Processing order can differ from sequence assignment for events from concurrent
tasks. An END1 footer holds frame count and CRC of the first eight bytes.

The writer attempts sync every five seconds and retries mount every 15 seconds.
The writer reserves the 12-byte footer with each bounded write. It reclaims
the oldest fully CRC-validated closed segment only if the FAT allocation for
the pending write requires a cluster. There is no age limit or percentage reserve.
Unrelated, malformed and torn files are retained. On mount a complete unclosed
tail may be sealed after validating every frame. A card with only damaged
files can require offline recovery. Power loss can discard the unsynced tail.
Remove power before physically removing the card. Copy segments to a private
computer and retain the originals for recovery. Raw records can contain
member locations and messages.

## Host checks

Build with PlatformIO using the environment above. For native fixture tests,
compile and run test/test_snorr_sd/contract.cpp under C++17. Compile rotation.cpp
with -Isrc and -Itest/test_snorr_sd from the firmware root. The latter exercises
the recorder's actual append, validate and reclaim functions on a disposable
cluster-limited filesystem, including malformed files and failed writes.
