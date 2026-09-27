# ESP32 hosted UI demo

This demo puts a React control page on a clientproxy.io domain **without sending the page through an ESP32**. The proxy's App File Cache serves `index.html`, JavaScript and CSS. Small `/api/*` requests are forwarded through the regular `tunnel-client` to an ESP32 on the same LAN.

```text
browser ── HTTPS ── clientproxy.io proxy ── cached React files
                                  │
                                  └── /api/* ── tunnel-client ── LAN ── ESP32:80
```

The tunnel client can run on **any machine that can reach the ESP32**, including a NAS, home server, Raspberry Pi or desktop. It does not run on the ESP32. This avoids the slow ESP32 tunnel upload path; the ESP32 only handles small API calls and firmware chunks.

The firmware is a deliberately small PlatformIO/Arduino example, based on the captive-portal approach in `esp32-backend`. It includes Wi-Fi provisioning, an admin password, status, two persistent settings, and firmware updates. The React app has Overview, Settings and Firmware Update pages. It uses a local sample-data mode for UI development.

## 1. Build and provision the ESP32

Install [PlatformIO](https://platformio.org/) and flash the environment for your board:

```sh
pio run -e esp32-s3 --target upload
# or: pio run -e esp32dev --target upload
# or: pio run -e esp32c3 --target upload
pio device monitor -b 115200
```

On first boot, join the device's `esp32-hosted-ui-XXXXXX` Wi-Fi network. The captive portal should open; otherwise browse to `http://192.168.4.1`. Enter Wi-Fi credentials and a unique device admin password (8–64 characters). The device stores them in NVS and restarts. The serial monitor prints its LAN IP, for example `192.168.1.42`.

If a saved Wi-Fi network cannot be reached, the device enters setup mode again. Hold the board's BOOT button (GPIO0 on the supplied configuration) for three seconds at boot to clear provisioning. Change `PROVISION_RESET_PIN` in `include/config.h` if your board uses another pin.

The device admin password protects `/api/status`, `/api/settings` and firmware updates. It is **not** the clientproxy.io tunnel API key. Do not put either secret in the React source or cache ZIP.

## 2. Create a tunnel on a LAN machine

Create a tunnel in the clientproxy.io dashboard and get its Tunnel ID and API Key. Install the [tunnel-client](https://github.com/clientproxy-io/tunnel-client) on a NAS, Raspberry Pi, desktop or other machine on the ESP32's LAN. Start it with the region, tunnel ID and key from your account:

```sh
tunnel-client \
  --api-url https://api-eu.clientproxy.io/api \
  --tunnel-id YOUR_TUNNEL_ID \
  --api-key YOUR_API_KEY
```

Choose the API URL for your region (`api-us`, `api-eu` or `api-asia`). The machine running `tunnel-client` must be able to open `http://ESP32_LAN_IP:80`; verify from that machine before proceeding:

```sh
curl -i http://192.168.1.42:80/
```

The ESP32 answers with a small explanation page. API requests require the device admin password.

In **Client Tunnels → your tunnel → Domains**, create or edit a domain mapping. Set its **Local IP** to the ESP32's LAN address and port, for example `192.168.1.42:80`. Do not use `127.0.0.1:80`: that would point to the NAS/Raspberry Pi running `tunnel-client`, not to the ESP32. Reserve the ESP32's LAN IP in your router so this mapping stays valid. If the client runs in Docker on a NAS, it still targets the ESP32's LAN IP; host networking is the simplest setup.

## 3. Build the React UI and create the ZIP

Node.js 20 or newer and `zip`/`unzip` are needed for these commands:

```sh
cd ui
npm ci
npm run build
cd ..
bash scripts/package-ui.sh
```

Upload `dist/esp32-hosted-ui.zip`. The script packages the **contents** of `ui/dist/`, so `index.html` sits at the archive root, with `assets/` beside it. It checks the 10 MB upload limit and omits source maps. Do not ZIP the `dist` folder itself; an archive containing `dist/index.html` will not serve `/` correctly.

For local UI preview without hardware, use `cd ui && npm run dev`; this uses sample data. To develop against the real device, run `VITE_DEMO=0 ESP32_TARGET=http://192.168.1.42 npm run dev` from `ui/`. The dev server proxies `/api/*` to the ESP32.

## 4. Upload and link the cache

1. In the clientproxy.io dashboard, open **App File Cache** and create a cache entry for your subscription, for example `ESP32 hosted UI v1`.
2. Use **Upload Zip** on that entry and choose `dist/esp32-hosted-ui.zip`. The dashboard uploads the file to its storage bucket; no manual Firebase bucket operation is needed.
3. Open **Client Tunnels → your tunnel → Domains**. On the domain mapped to `ESP32_LAN_IP:80`, choose the new entry in **Link cache file**.
4. Reconnect `tunnel-client` so the proxy fetches and extracts the linked archive. Open the HTTPS domain. The page and `/assets/*` come from the proxy; `/api/status`, `/api/settings` and `/api/update/*` go to the ESP32.

For a new UI release, create a **new cache entry** and link its new ID, then reconnect the client. The proxy can keep files from an existing cache ID in memory across reconnects, so overwriting a ZIP under the same ID may continue serving the previous UI until the proxy restarts.

The cache serves `/` as `index.html` and files whose paths contain extensions. Keep live API paths extensionless under `/api/`; those requests fall through to the tunnel. The firmware update UI sends the `.bin` file in sequential 12 KB multipart requests, so the ESP32 receives manageable request bodies. Updates can take time on a slow link; use a firmware build for the exact board and keep the page open until completion.

## Verify the split

In browser DevTools → Network, load the domain and inspect requests: `/`, `.js`, and `.css` should load quickly from the proxy; `/api/status` returns live uptime and the device's LAN IP. Save a new device name in Settings and refresh the page to confirm it persisted in ESP32 NVS. Disconnect the ESP32: the cached UI should still load, while its API requests show a connection error. This is the behavior the demo is meant to show.

The sample-data preview is only for UI work; it does not flash firmware. A successful UI build and PlatformIO compile do not prove end-to-end behavior until tested with a real ESP32, tunnel client and cache mapping.
