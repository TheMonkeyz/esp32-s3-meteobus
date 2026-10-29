// ESP32-P4-Function-EV-Board: no motion sensor. forge_presence's accelerometer hooks report "not available".
#include "imu.h"

bool imu_init(i2c_master_bus_handle_t bus) { (void)bus; return false; }
bool imu_read(float g[3]) { g[0] = g[1] = g[2] = 0; return false; }
