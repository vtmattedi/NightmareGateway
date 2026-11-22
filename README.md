# NightMare Network Gateway v1

Target: generic ESP32-S3 N16R8, native ESP-IDF.

## Scope

```text
ESP-NOW IN
  -> distribute locally to all known peers except sender
  -> publish to MQTT

MQTT IN
  -> distribute to all known ESP-NOW peers
```

The gateway is protocol-transparent. It transports the current NightMare Network logical message:

```text
topic + payload + retained
```

It does not parse Resources, manifests, `/set`, `/invoke`, or other NMNW semantics.

## MQTT loop prevention

The MQTT client uses MQTT v5 and subscribes with `No Local = true`, so a compliant broker does not send the gateway's own publications back to the same client subscription.

This is the rule we want:

```text
ESP-NOW -> gateway -> MQTT
                    X
             no echo back
                    X
                 ESP-NOW
```

## ESP-NOW peer discovery

Any valid frame teaches the gateway the sender MAC. Endpoints should also send a periodic HELLO frame, recommended every 10 seconds, so peers are rediscovered after a gateway reboot.

The peer registry is RAM-only in v1.

## ESP-NOW transport

The transport-only frame header is:

```cpp
struct EspNowFrameHeader {
    uint8_t version;
    uint8_t type;          // HELLO or MESSAGE
    uint8_t flags;         // retained, etc.
    uint8_t fragmentIndex;
    uint16_t fragmentCount;
    uint16_t topicLength;
    uint32_t payloadLength;
    uint32_t messageId;
};
```

`messageId` exists only for ESP-NOW reassembly/deduplication. It does not become part of the NMNW MQTT protocol.

Logical data is serialized as:

```text
topic bytes | payload bytes
```

and fragmented across ESP-NOW packets.

## Local-first routing

For ESP-NOW ingress the router deliberately does:

```text
1. local ESP-NOW fan-out except source
2. MQTT publish
```

so local devices do not depend on the broker round trip to communicate.

## Configuration

Credentials live in `include/creds.h` (untracked):

```text
DEFAULT_SSID, DEFAULT_PASSWORD     first-boot Wi-Fi profile; NETWORK WIFI CHANGE saves another
REMOTE_MQTT_URL, REMOTE_MQTT_PORT  the uplink broker (TLS, ROOT_CA), or
LOCAL_MQTT_HOST, LOCAL_MQTT_PORT   a LAN broker
MQTT_USER, MQTT_PASSWD             optional
NM_ESPNOW_PSK                      the ESP-NOW network key, same on every device
```

Feature selection is `include/NightMareConfig.h`.

## The gateway as a device

Holmes runs the full NightMare device stack on the library's `GATEWAY`
connection: status, info, telemetry, Resources, console, jobs, the Wi-Fi
station and SNTP all come from NightMare Network. Its address (topic root,
MQTT client id and ESP-NOW beacon id) is its hardware signature; `LABEL SET
<label>` gives it a readable name.

Every gateway publishes the standard `gateway:*` Resources (clients, uplink,
drop and message counters; enable, ESP-NOW pause, hardware trace), see the
library's `docs/modules/gateway.md`. The application adds the chip
temperature and memory sensors, the `HEAP` and `TEMP` console commands, and a
web dashboard with mDNS (`nightmare-gateway.local`) while the station has an
address.

## Build

```bash
pio run -d gateways/Holmes
pio run -d gateways/Holmes -t upload --upload-port COMx
```

The library is the `include/nightmarenetwork` submodule.
