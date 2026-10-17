# Dedicated Heltec V3 receive sniffer

Build `heltec-v3-sniffer` only for the owner's dedicated debug V3. This is not a gateway or repeater image. It inherits the pinned Heltec V3 board definition and preserves on-device Meshtastic configuration/identity when installed as an application update.

- `LORA_DISABLE_SENDING` blocks the normal transmit queue; `SNORR_SNIFFER` also rejects the final `startSend` entry point. Wi-Fi, Bluetooth and MQTT are excluded.
- On each radio receive interrupt, read the FIFO and retain status, raw bytes, RSSI, SNR, device milliseconds and estimated time on air. Rearm reception before serial logging. Return before normal routing, ignore lists, duplicate filtering, decrypting and module handlers. The host decodes optional known channel keys.
- Chunk records fit the existing 160-byte serial log formatter: `SNORR_CFG`, `SNORR_RX`, zero or more `SNORR_DATA` chunks of at most 40 bytes, then `SNORR_END` with standard CRC32. Sequence increments per handled receive interrupt; random boot ID distinguishes resets. Each RX includes length/status/airtime. Chunks/end carry the sequence. No key/configuration secret is included in these records.
- Failed reads retain the reported status and zero-initialized buffer bytes as untrusted evidence. These bytes must not be treated as authenticated or successfully decoded content. CRC32 protects serial reconstruction, not RF authenticity.

This captures receive interrupts on one selected frequency, bandwidth, spreading factor, coding rate and Meshtastic sync word. It cannot recover collided/undetected packets, arbitrary RF energy or packets on other frequencies/modems. No single-radio completeness guarantee is possible. Sequence/CRC checks reveal host-stream loss after a reception is handled; they cannot measure packets lost before that point. Airtime is the existing packet-length/modem estimate, not a spectrum measurement.

Host GUI/CLI and tests live in the parent project at `scripts/airtime-sniffer/`. The host opens serial at 115200 with DTR/RTS inactive and sends no serial commands. Closing/reopening the port may still reset some USB bridge/driver combinations. Close the host before any device API or flashing operation.

Portable framing check, from this firmware repository:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Isrc test/test_snorr_sniffer/test_main.cpp -o /tmp/snorr-sniffer-test
/tmp/snorr-sniffer-test
```

The check covers the standard CRC vector, zero/max payload, all byte values, chunk offsets, reconstruction and worst-width metadata under the formatter limit. A target build and hardware reception/silence checks remain separate requirements.
