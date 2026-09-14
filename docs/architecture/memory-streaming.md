# Zero-Copy Streaming & Memory

Serving dynamic web interfaces and real-time graphs on a microcontroller often leads to severe heap fragmentation and Out-Of-Memory (OOM) crashes. The ESP32-SwitchBot avoids heap exhaustion through chunked HTTP streaming and memory segregation.

---

## Chunked HTTP Streaming Engine (`sendWrappedPageStream`)

Traditional Arduino web servers construct entire HTML documents into dynamic `String` objects on the heap before calling `server.send()`. For complex pages containing JavaScript and SVG graphics (8–20 KB), this causes frequent allocations, moves, and heap fragmentation.

The ESP32-SwitchBot uses an optimized lambda-based chunked streaming pipeline:

```cpp
template <typename F>
inline void sendWrappedPageStream(WebServer &server, const char* title, const char* icon, F bodyWriter, const char* extraScript = "", bool centered = false) {
  server.sendHeader("Connection", "close");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  char headBuf[600];
  // Format standard <head> and container on the stack
  server.sendContent(headBuf);

  // Stream dynamic body content via closure callback
  bodyWriter();

  server.sendContent(F("</div>"));
  if (extraScript && extraScript[0]) {
    server.sendContent(extraScript);
  }
  server.sendContent(F("</body></html>"));
  server.sendContent(""); // Terminate chunked transfer
}
```

### Advantages:
1. **Zero Dynamic Heap Allocations:** Small stack buffers (`snprintf`) format each HTML card, which is transmitted directly into the network buffer via `server.sendContent()`.
2. **Infinite Payload Scalability:** Pages of any size can be delivered without consuming RAM.
3. **No Truncation:** Large embedded scripts (such as the 8 KB calibration JavaScript bundle) stream completely to the browser without risk of buffer clipping.

---

## Memory Segregation (Internal SRAM vs. Octal PSRAM)

The ESP32-S3 (N16R8) combines ~360 KB of ultra-fast internal SRAM with 8 MB of Octal SPI PSRAM (`MALLOC_CAP_SPIRAM`):

* **Internal SRAM:** Reserved exclusively for high-frequency FreeRTOS kernel stacks, Wi-Fi DMA packet buffers, and the WebServer task.
* **Octal PSRAM:** Utilized by [microlink](https://github.com/CamM2325/microlink) and WireGuard for large crypto buffers, packet queues, and TLS handshake states.
* **Heap Threshold (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`):** Allocations smaller than 4 KB always stay in internal SRAM for maximum execution speed, while large buffers automatically spill into PSRAM.
