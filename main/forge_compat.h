#pragma once
// espforge functions this app uses that the espforge tag in main/idf_component.yml (v0.3.0) doesn't have yet: the
// Guition JC1060P470C build (ESP32-P4) gets them from espforge with p4/espforge-p4.patch, the AMOLED build from the
// weak stand-ins in forge_compat.c (what v0.3.0 does). Remove both files once the tag has them (espforge PR / BACKLOG).
#include <stdbool.h>

bool net_dpp_supported(void);           // false: no Easy Connect here (Wi-Fi through a co-processor)
const char *web_port(void);             // "" on 443, else ":<port>": after the host in the settings page's address
