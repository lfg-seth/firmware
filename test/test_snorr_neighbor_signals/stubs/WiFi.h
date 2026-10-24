#pragma once
constexpr int WL_CONNECTED = 3;
inline struct {
    int connection = 0;
    int status() { return connection; }
} WiFi;
