#include "SerialConsole.h"
#include "Default.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Throttle.h"
#include "configuration.h"
#include "modules/DirectSurvey.h"
#include "modules/SiteHandheld.h"
#include "modules/SnorrWifi.h"
#include "time.h"

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
#define IS_USB_SERIAL
#ifdef SERIAL_HAS_ON_RECEIVE
#undef SERIAL_HAS_ON_RECEIVE
#endif
#include "HWCDC.h"
#endif

#ifdef RP2040_SLOW_CLOCK
#define Port Serial2
#else
#ifdef USER_DEBUG_PORT // change by WayenWeng
#define Port USER_DEBUG_PORT
#else
#define Port Serial
#endif
#endif
// Defaulting to the formerly removed phone_timeout_secs value of 15 minutes
#define SERIAL_CONNECTION_TIMEOUT (15 * 60) * 1000UL

SerialConsole *console;

void consoleInit()
{
    auto sc = new SerialConsole(); // Must be dynamically allocated because we are now inheriting from thread

#if defined(SERIAL_HAS_ON_RECEIVE)
    // onReceive does only exist for HardwareSerial not for USB CDC serial
    Port.onReceive([sc]() { sc->rxInt(); });
#else
    (void)sc;
#endif
    DEBUG_PORT.rpInit(); // Simply sets up semaphore
}

void consolePrintf(const char *format, ...)
{
    va_list arg;
    va_start(arg, format);
    console->vprintf(nullptr, format, arg);
    va_end(arg);
    console->flush();
}

SerialConsole::SerialConsole() : StreamAPI(&Port), RedirectablePrint(&Port), concurrency::OSThread("SerialConsole")
{
    api_type = TYPE_SERIAL;
    assert(!console);
    console = this;
    canWrite = false; // We don't send packets to our port until it has talked to us first

#ifdef RP2040_SLOW_CLOCK
    Port.setTX(SERIAL2_TX);
    Port.setRX(SERIAL2_RX);
#endif
    Port.begin(SERIAL_BAUD);
#if defined(ARCH_NRF52) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(ARCH_RP2040) ||   \
    defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
    time_t timeout = millis();
    while (!Port) {
        if (Throttle::isWithinTimespanMs(timeout, FIVE_SECONDS_MS)) {
            delay(100);
        } else {
            break;
        }
    }
#endif
#if !ARCH_PORTDUINO
    emitRebooted();
#endif
}

#ifdef SNORR_SD_GATEWAY
void SerialConsole::onUnframedByte(uint8_t c)
{
    if (!config.has_lora || !config.security.serial_enabled) return;
    if (c == 0x94) { survey = false; surveyPos = 0; return; } // native client takes over
    if (c == '\r' || c == '\n') {
        if (surveyPos < sizeof(surveyLine)) {
            surveyLine[surveyPos] = 0;
            if (!strcmp(surveyLine, "WIFI ON")) {
                usingProtobufs = canWrite = false;
                survey = true; surveyStart = millis(); surveyLast = surveyStart - 1000;
            } else if (!strcmp(surveyLine, "WIFI OFF")) survey = false;
        }
        surveyPos = 0;
    } else if (surveyPos < sizeof(surveyLine)) surveyLine[surveyPos++] = c;
}
#endif

int32_t SerialConsole::runOnce()
{
#ifdef HELTEC_MESH_SOLAR
    // After enabling the mesh solar serial port module configuration, command processing is handled by the serial port module.
    if (moduleConfig.serial.enabled && moduleConfig.serial.override_console_serial_port &&
        moduleConfig.serial.mode == meshtastic_ModuleConfig_SerialConfig_Serial_Mode_MS_CONFIG) {
        return 250;
    }
#endif

    int32_t delay = runOncePart();
#ifdef SNORR_DIRECT_SURVEY
    survey::poll();
    if (config.has_lora && config.security.serial_enabled && Port) {
        for (unsigned i = 0; i < 4; ++i) {
            size_t n = survey::pop(txBuf + 4, sizeof(txBuf) - 4);
            if (!n) break;
            emitTxBuffer(n);
        }
    }
    return delay < 50 ? delay : 50; // Timers still run with the USB cable removed.
#endif
#ifdef SNORR_SD_GATEWAY
    if (survey && (!config.security.serial_enabled || millis() - surveyStart >= 1800000)) survey = false;
    if (survey) {
        if (millis() - surveyLast >= 1000) {
            surveyLast = millis();
            const auto s = snorr::sampleWifi();
            snorr::recordSurvey(s);
            if (s.state == 2)
                Port.printf("SNWF measured %lu ms %d dBm ch %u association %lu\r\n", (unsigned long)surveyLast, s.rssi, s.channel, (unsigned long)s.association);
            else
                Port.printf("SNWF %s %lu ms\r\n", s.state ? "unavailable" : "disconnected", (unsigned long)surveyLast);
        }
        return delay < 250 ? delay : 250;
    }
#endif
#if defined(SERIAL_HAS_ON_RECEIVE) || defined(CONFIG_IDF_TARGET_ESP32S2)
    return Port.available() ? delay : INT32_MAX;
#elif defined(IS_USB_SERIAL)
    return HWCDC::isPlugged() ? delay : (1000 * 20);
#else
    return delay;
#endif
}

void SerialConsole::flush()
{
    Port.flush();
}

// trigger tx of serial data
void SerialConsole::onNowHasData(uint32_t fromRadioNum)
{
    setIntervalFromNow(0);
}

// trigger rx of serial data
void SerialConsole::rxInt()
{
    setIntervalFromNow(0);
}

// For the serial port we can't really detect if any client is on the other side, so instead just look for recent messages
bool SerialConsole::checkIsConnected()
{
    return Throttle::isWithinTimespanMs(lastContactMsec, SERIAL_CONNECTION_TIMEOUT);
}

/**
 * we override this to notice when we've received a protobuf over the serial
 * stream.  Then we shut off debug serial output.
 */
bool SerialConsole::handleToRadio(const uint8_t *buf, size_t len)
{
    // only talk to the API once the configuration has been loaded and we're sure the serial port is not disabled.
    if (config.has_lora && config.security.serial_enabled) {
        // Switch to protobufs for log messages
        usingProtobufs = true;
        canWrite = true;

#ifdef SNORR_DIRECT_SURVEY
#ifdef SNORR_SITE_HANDHELD
        size_t response = site::usb(buf, len, txBuf + 4, sizeof(txBuf) - 4);
        if (response) {
            emitTxBuffer(response);
            return true;
        }
#endif
        if (survey::command(buf, len)) return true;
#endif
        return StreamAPI::handleToRadio(buf, len);
    } else {
        return false;
    }
}

void SerialConsole::log_to_serial(const char *logLevel, const char *format, va_list arg)
{
    if (usingProtobufs && config.security.debug_log_api_enabled) {
        meshtastic_LogRecord_Level ll = RedirectablePrint::getLogLevel(logLevel);
        auto thread = concurrency::OSThread::currentThread;
        emitLogRecord(ll, thread ? thread->ThreadName.c_str() : "", format, arg);
    } else
        RedirectablePrint::log_to_serial(logLevel, format, arg);
}
