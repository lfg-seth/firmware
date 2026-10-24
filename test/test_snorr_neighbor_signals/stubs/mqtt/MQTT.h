#pragma once
inline struct {
    bool connected = false;
    bool isConnectedDirectly() { return connected; }
} testMqtt;
inline auto mqtt = &testMqtt;
