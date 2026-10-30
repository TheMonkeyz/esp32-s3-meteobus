// Weak stand-ins for espforge functions newer than its tag here (forge_compat.h): a linked espforge that has them
// (the P4 build's, patched) wins
#include "forge_compat.h"

__attribute__((weak)) bool net_dpp_supported(void) { return true; }      // espforge v0.3.0: Easy Connect everywhere
__attribute__((weak)) const char *web_port(void) { return ""; }          // and the settings page on 443
