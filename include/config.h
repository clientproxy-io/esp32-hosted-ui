#pragma once

// Hold GPIO0 LOW for three seconds at boot to clear Wi-Fi and admin password.
// Change this pin for boards whose BOOT button is wired elsewhere.
#define PROVISION_RESET_PIN 0
#define PROVISION_AP_PREFIX "esp32-hosted-ui"
