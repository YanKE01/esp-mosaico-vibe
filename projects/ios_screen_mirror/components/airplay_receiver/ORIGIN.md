# Source provenance

RPiPlay is pinned to `64d0341ed3bef098c940c9ed0675948870a271f9` from
https://github.com/FD-/RPiPlay, matching the source application's gitlink.
`vendor/rpiplay/LICENSE` and source license notices are retained. RPiPlay uses
GPL-3.0-or-later; the combined application uses that license.

The binary plist implementation, Edwards arithmetic, and original crypto
adapter were copied from Yuzuki Tsuru's `zephyr-components/ios-screen-mirror`.
Their copyright and Apache-2.0 notices are retained. The ESP-IDF port uses
public ESP AES APIs and PSA SHA-512 because IDF 6.2 has mbedTLS 4.

`ios_aes.c` bounds each CTR/CBC hardware call to 128 bytes, selecting the pinned
S31 IDF's register path without per-packet DMA allocations. CTR state stays in
the AES context across payload boundaries. A failed operation invalidates that
context; a failed mirror decrypt closes the stream before NAL parsing/decoding.

Changes to vendored protocol sources:

- Advertise configured width, height, and refresh interval in `/info`.
- Namespace protocol crypto functions to avoid ESP-IDF WPA symbol collisions.
- Reuse bounded mirror receive/decrypt/configuration buffers, validate AVC NAL
  lengths and SPS/PPS records, and remove the untrusted-sized stack VLA.
- Close disconnected stream sockets, join exited protocol threads, and handle
  thread creation failures by releasing their sockets.
- Propagate CTR errors through pairing/mirror helpers, check mirror key-derivation
  allocations, and replace manual partial-block bookkeeping with the AES
  provider's continuous counter/offset state (including very short packets).

ESP-IDF pthreads/lwIP replace Zephyr shims. `esp_dnssd.c` implements RPiPlay's
DNS-SD interface using application-owned Espressif mDNS. The original OpenSSL
and Bonjour implementations in the vendor snapshot are not built.
