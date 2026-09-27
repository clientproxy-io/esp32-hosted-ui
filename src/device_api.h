#pragma once

// Local HTTP backend for the proxy-hosted React UI. The tunnel-client runs on
// another LAN machine and forwards only requests not found in the proxy cache.
void startDeviceApi();
