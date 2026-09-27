#pragma once

void loadParams();
bool paramsLoaded();
void checkProvisioningReset();
void runProvisioning();  // captive portal; reboots after successful setup
const char* getWifiSsid();
const char* getWifiPass();
const char* getAdminKey();
