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

Edit:

```text
main/gateway_config.h
```

Set:

```cpp
WIFI_SSID
WIFI_PASSWORD
MQTT_BROKER_URI
MQTT_USERNAME        // optional
MQTT_PASSWORD        // optional
```

The default subscription is `#` for a transparent v1 bridge.

## Build

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

ESP-NOW is initialized only after Wi-Fi STA obtains an IP so it shares the AP-selected channel.

## Not in v1

- local MQTT broker
- web UI
- database / SD
- gateway-owned Resource model
- Arduino framework

## Next paired task

NightMare endpoints need an ESP-NOW transport adapter that can:

```text
send HELLO
encode topic + payload + retained
fragment messages
reassemble gateway messages
feed received topic/payload into the normal NightMare ingress path
```

That adapter should live beside MQTT as another transport, not inside `ResourcesManager`.
