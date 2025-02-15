#include <main.h>
bool sendTelemetry(int totalSeen, int totalFpSeen, int totalFpQueried, int totalFpReported)
{
    if (!online)
    {
        if (sendOnline())
        {
            online = true;
            reconnectTries = 0;
        }
        else
        {
            log_e("Error sending status=online");
        }
    }

    if (discovery && !sentDiscovery)
    {
        if (sendDiscoveryConnectivity() && sendNumberDiscovery("Max Distance", "config") && sendSwitchDiscovery("Active Scan", "config") && sendSwitchDiscovery("Query", "config") && sendDiscoveryMotion() && sendDiscoveryHumidity() && sendDiscoveryTemperature() && sendDiscoveryLux())
        {
            sentDiscovery = true;
        }
        else
        {
            log_e("Error sending discovery");
        }
    }

    auto now = millis();

    if (now - lastTeleMillis < 15000)
        return false;

    lastTeleMillis = now;

    StaticJsonDocument<512> tele;
    tele["ip"] = localIp;
    tele["uptime"] = getUptimeSeconds();
    tele["firm"] = String(FIRMWARE);
    tele["rssi"] = WiFi.RSSI();
#ifdef MACCHINA_A0
    tele["batt"] = a0_read_batt_mv() / 1000.0f;
#endif
#ifdef VERSION
    tele["ver"] = String(VERSION);
#endif
    if (totalSeen > 0)
        tele["adverts"] = totalSeen;
    if (totalFpSeen > 0)
        tele["seen"] = totalFpSeen;
    if (totalFpQueried > 0)
        tele["queried"] = totalFpQueried;
    if (totalFpReported > 0)
        tele["reported"] = totalFpReported;

    if (teleFails > 0)
        tele["teleFails"] = teleFails;
    if (reconnectTries > 0)
        tele["reconnectTries"] = reconnectTries;

    tele["freeHeap"] = ESP.getFreeHeap();
    tele["minFreeHeap"] = ESP.getMinFreeHeap();
    tele["maxAllocHeap"] = ESP.getMaxAllocHeap();
    tele["resetReason"] = resetReason(rtc_get_reset_reason(0));

    char teleMessageBuffer[512];
    serializeJson(tele, teleMessageBuffer);

    for (int i = 0; i < 10; i++)
    {
        if (!publishTele || mqttClient.publish(teleTopic.c_str(), 0, 0, teleMessageBuffer))
            return true;
        delay(50);
    }

    teleFails++;
    log_e("Error after 10 tries sending telemetry (%d times since boot)", teleFails);
    return false;
}

void connectToWifi()
{
    Serial.printf("Connecting to WiFi (%s)...\n", WiFi.macAddress().c_str());
    Display.update();

    WiFiSettings.onConnect = []()
    {
        Display.connected(false, false);
    };

    WiFiSettings.onFailure = []()
    {
        Display.status("WiFi Portal...");
    };
    WiFiSettings.onWaitLoop = []()
    {
        Display.connecting();
        return 150;
    };
    WiFiSettings.onPortalWaitLoop = []()
    {
        if (getUptimeSeconds() > 600)
            ESP.restart();
    };

    Display.connected(true, false);
#ifdef VERSION
    WiFiSettings.info("ESPResense Version: " + String(VERSION));
#endif
    room = WiFiSettings.string("room", ESPMAC, "Room");

    WiFiSettings.heading("MQTT Connection");
    mqttHost = WiFiSettings.string("mqtt_host", DEFAULT_MQTT_HOST, "Server");
    mqttPort = WiFiSettings.integer("mqtt_port", DEFAULT_MQTT_PORT, "Port");
    mqttUser = WiFiSettings.string("mqtt_user", DEFAULT_MQTT_USER, "Username");
    mqttPass = WiFiSettings.string("mqtt_pass", DEFAULT_MQTT_PASSWORD, "Password");

    WiFiSettings.heading("Preferences");
    autoUpdate = WiFiSettings.checkbox("auto_update", DEFAULT_AUTO_UPDATE, "Automatically Update");
    otaUpdate = WiFiSettings.checkbox("ota_update", DEFAULT_OTA_UPDATE, "Arduino OTA Update");
    discovery = WiFiSettings.checkbox("discovery", true, "Home Assistant Discovery");
    activeScan = WiFiSettings.checkbox("active_scan", false, "Active scanning (uses more battery but more results)");
    allowQuery = WiFiSettings.checkbox("query", false, "Query devices for characteristics (helps apple fingerprints uniqueness)");
    publishTele = WiFiSettings.checkbox("pub_tele", true, "Send to telemetry topic");
    publishRooms = WiFiSettings.checkbox("pub_rooms", true, "Send to rooms topic");
    publishDevices = WiFiSettings.checkbox("pub_devices", true, "Send to devices topic");

    WiFiSettings.heading("Filter");
    whitelist = WiFiSettings.string("white_list", DEFAULT_WHITELIST, "Device Whitelist. Send only MQTT Data from these Devices: like apple:iphone10-6 apple:iphone13-2 ");
    blacklist = WiFiSettings.string("black_list", DEFAULT_BLACKLIST, "Device Blacklist. Filter these Devices. Like exp:20 apple:iphone10-6");

    WiFiSettings.heading("Calibration");
    maxDistance = WiFiSettings.floating("max_dist", 0, 100, DEFAULT_MAX_DISTANCE, "Maximum distance to report (in meters)");
    forgetMs = WiFiSettings.integer("forget_ms", 0, 3000000, DEFAULT_FORGET_MS, "Forget beacon if not seen for (in miliiseconds)");
    skipDistance = WiFiSettings.floating("skip_dist", 0, 10, DEFAULT_SKIP_DISTANCE, "Update mqtt if beacon has moved more than this distance since last report (in meters)");
    skipMs = WiFiSettings.integer("skip_ms", 0, 3000000, DEFAULT_SKIP_MS, "Update mqtt if this time has elapsed since last report (in ms)");
    refRssi = WiFiSettings.integer("ref_rssi", -100, 100, DEFAULT_REF_RSSI, "Rssi expected from a 0dBm transmitter at 1 meter");

    WiFiSettings.heading("Additional Sensors");
    pirPin = WiFiSettings.integer("pir_pin", 0, "PIR motion pin (0 for disable)");
    radarPin = WiFiSettings.integer("radar_pin", 0, "Radar motion pin (0 for disable)");
    dht11Pin = WiFiSettings.integer("dht11_pin", 0, "DHT11 sensor pin (0 for disable)");
    dht22Pin = WiFiSettings.integer("dht22_pin", 0, "DHT22 sensor pin (0 for disable)");
    BH1750_I2c = WiFiSettings.string("BH1750_I2c", "", "Ambient Light Sensor - I2C address of BH1750 Sensor, like 0x23 or 0x5C.");
    I2CDebug = WiFiSettings.checkbox("I2CDebug", false, "Debug I2C address. Look at the serial log to get the correct address.");

    WiFiSettings.hostname = "espresense-" + kebabify(room);

    if (!WiFiSettings.connect(true, 60))
        ESP.restart();

#ifdef VERSION
    Serial.println("Version:     " + String(VERSION));
#endif
    Serial.print("IP address:   ");
    Serial.println(WiFi.localIP());
    Serial.print("DNS address:  ");
    Serial.println(WiFi.dnsIP());
    Serial.print("Hostname:     ");
    Serial.println(WiFi.getHostname());
    Serial.print("Room:         ");
    Serial.println(room);
    Serial.printf("Max Distance: %.2f\n", maxDistance);
    Serial.print("Telemetry:    ");
    Serial.println(publishTele ? "enabled" : "disabled");
    Serial.print("Rooms:        ");
    Serial.println(publishRooms ? "enabled" : "disabled");
    Serial.print("Devices:      ");
    Serial.println(publishDevices ? "enabled" : "disabled");
    Serial.print("Discovery:    ");
    Serial.println(discovery ? "enabled" : "disabled");
    Serial.print("PIR Sensor:   ");
    Serial.println(pirPin ? "enabled" : "disabled");
    Serial.print("Radar Sensor: ");
    Serial.println(radarPin ? "enabled" : "disabled");
    Serial.print("DHT11 Sensor: ");
    Serial.println(dht11Pin ? "enabled" : "disabled");
    Serial.print("DHT22 Sensor: ");
    Serial.println(dht22Pin ? "enabled" : "disabled");
    Serial.print("BH1750_I2c Sensor: ");
    Serial.println(BH1750_I2c);
    Serial.print("Whitelist: ");
    Serial.println(whitelist);
    Serial.print("Blacklist: ");
    Serial.println(blacklist);

    localIp = WiFi.localIP().toString();
    id = slugify(room);
    roomsTopic = CHANNEL + "/rooms/" + id;
    statusTopic = roomsTopic + "/status";
    teleTopic = roomsTopic + "/telemetry";
    subTopic = roomsTopic + "/+/set";
}

void onMqttConnect(bool sessionPresent)
{
    xTimerStop(reconnectTimer, 0);
    mqttClient.subscribe(subTopic.c_str(), 2);
    Display.connected(true, true);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    Display.connected(true, false);
    log_e("Disconnected from MQTT; reason %d\n", reason);
    xTimerStart(reconnectTimer, 0);
    online = false;
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
    char new_payload[len + 1];
    new_payload[len] = '\0';
    strncpy(new_payload, payload, len);
    Serial.printf("%s: %s\n", topic, new_payload);

    String top = String(topic);
    String pay = String(new_payload);
    if (top == roomsTopic + "/max_distance/set")
    {
        maxDistance = pay.toFloat();
        spurt("/max_dist", pay);
        online = false;
    }
    else if (top == roomsTopic + "/active_scan/set")
    {
        activeScan = pay == "ON";
        spurt("/active_scan", String(activeScan));
        online = false;
    }
    else if (top == roomsTopic + "/query/set")
    {
        allowQuery = pay == "ON";
        spurt("/query", String(allowQuery));
        online = false;
    }

    fingerprints.setParams(refRssi, forgetMs, skipDistance, skipMs, maxDistance);
}

void reconnect(TimerHandle_t xTimer)
{
    if (updateInProgress) return;
    if (WiFi.isConnected() && mqttClient.connected()) return;

    if (reconnectTries++ > 10)
    {
        log_e("Too many reconnect attempts; Restarting");
        ESP.restart();
    }

    if (!WiFi.isConnected())
    {
        Serial.println("Reconnecting to WiFi...");
        if (!WiFiSettings.connect(true, 60))
            ESP.restart();
    }

    Serial.println("Reconnecting to MQTT...");
    mqttClient.connect();
}

void connectToMqtt()
{
    reconnectTimer = xTimerCreate("reconnectionTimer", pdMS_TO_TICKS(3000), pdTRUE, (void *)0, reconnect);
    Serial.printf("Connecting to MQTT %s %d\n", mqttHost.c_str(), mqttPort);
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqttHost.c_str(), mqttPort);
    mqttClient.setWill(statusTopic.c_str(), 0, 1, offline.c_str());
    mqttClient.setCredentials(mqttUser.c_str(), mqttPass.c_str());
    mqttClient.connect();
}

bool reportDevice(BleFingerprint *f)
{
    StaticJsonDocument<512> doc;
    if (!f->report(&doc))
        return false;

    char JSONmessageBuffer[512];
    serializeJson(doc, JSONmessageBuffer);

    String devicesTopic = CHANNEL + "/devices/" + f->getId() + "/" + id;
 
    // Whitelist
    if(whitelist.length() > 0)
    {
        int posStr_wh = whitelist.indexOf(f->getId());
        if(posStr_wh < 0)
        {     
            Serial.println(f->getId() + " - P: " + String(posStr_wh) + " not in whitelist. Cancel MQTT");
            return false;
        }
    }

    // Blacklist
    if(blacklist.length() > 0)
    {
        int posStr_bl = blacklist.indexOf(f->getId());  
        if(posStr_bl > -1)
        {     
            Serial.println(f->getId() + " - P: " + String(posStr_bl) + " found in Blacklist. Cancel MQTT");
            return false;
        }
    }

    bool p1 = false, p2 = false;
    for (int i = 0; i < 10; i++)
    {
        if (!mqttClient.connected())
            return false;

        if (!p1 && (!publishRooms || mqttClient.publish((char *)roomsTopic.c_str(), 0, 0, JSONmessageBuffer)))
            p1 = true;

        if (!p2 && (!publishDevices || mqttClient.publish((char *)devicesTopic.c_str(), 0, 0, JSONmessageBuffer)))
            p2 = true;

        if (p1 && p2)
            return true;
        delay(20);
    }
    teleFails++;
    return false;
}

void scanForDevices(void *parameter)
{
    fingerprints.setParams(refRssi, forgetMs, skipDistance, skipMs, maxDistance);
    BLEDevice::init(Stdprintf("ESPresense-%06" PRIx64, ESP.getEfuseMac() >> 24));
    for (esp_ble_power_type_t i = ESP_BLE_PWR_TYPE_CONN_HDL0; i <= ESP_BLE_PWR_TYPE_CONN_HDL8; i = esp_ble_power_type_t((int)i + 1))
        NimBLEDevice::setPower(ESP_PWR_LVL_P9, i);

    auto pBLEScan = BLEDevice::getScan();
    pBLEScan->setInterval(BLE_SCAN_INTERVAL);
    pBLEScan->setWindow(BLE_SCAN_WINDOW);
    pBLEScan->setAdvertisedDeviceCallbacks(&fingerprints, true);
    if (activeScan) pBLEScan->setActiveScan(true);
    pBLEScan->setMaxResults(0);
    if (!pBLEScan->start(0, nullptr, false))
        log_e("Error starting continuous ble scan");

    int totalSeen = 0;
    int totalFpSeen = 0;
    int totalFpQueried = 0;
    int totalFpReported = 0;

    while (1)
    {
        while (updateInProgress || !mqttClient.connected())
            delay(1000);

        sendTelemetry(totalSeen, totalFpSeen, totalFpQueried, totalFpReported);

        auto seen = fingerprints.getCopy();

        if (allowQuery)
        {
            for (auto it = seen.begin(); it != seen.end(); ++it)
            {
                auto f = (*it);
                if (f->query())
                    totalFpQueried++;
            }

            if (!pBLEScan->isScanning())
                if (!pBLEScan->start(0, nullptr, false))
                    log_e("Error re-starting continuous ble scan");
        }

        for (auto it = seen.begin(); it != seen.end(); ++it)
        {
            auto f = (*it);
            auto seen = f->getSeenCount();
            if (seen)
            {
                totalSeen += seen;
                totalFpSeen++;
            }
            if (reportDevice(f))
                totalFpReported++;
        }
    }
}

/**
 * Task to reads temperature from DHT11 sensor
 * @param pvParameters
 *		pointer to task parameters
 */
void tempTask(void *pvParameters)
{
    Serial.println("tempTask loop started");
    while (1) // tempTask loop
    {
        if (dhtTasksEnabled && !gotNewTemperature)
        {
            // Read temperature only if old data was processed already
            // Reading temperature for humidity takes about 250 milliseconds!
            // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
            dhtSensorData = dhtSensor.getTempAndHumidity(); // Read values from sensor 1
            gotNewTemperature = true;
        }
        vTaskSuspend(NULL);
    }
}

/**
 * triggerGetTemp
 * Sets flag dhtUpdated to true for handling in loop()
 * called by Ticker tempTicker
 */
void triggerGetTemp()
{
    if (dhtTempTaskHandle != NULL)
    {
        xTaskResumeFromISR(dhtTempTaskHandle);
    }
}

void setup()
{
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
#endif

    Serial.begin(115200);
    Serial.setDebugOutput(true);

#ifdef VERBOSE
    esp_log_level_set("*", ESP_LOG_DEBUG);
#endif

    spiffsInit();
    connectToWifi();
    if (pirPin) pinMode(pirPin, INPUT);
    if (radarPin) pinMode(radarPin, INPUT);
    if (dht11Pin) dhtSensor.setup(dht11Pin, DHTesp::DHT11);
    if (dht22Pin) dhtSensor.setup(dht22Pin, DHTesp::DHT22); //(AM2302)

    if (dht11Pin || dht22Pin)
    {
        // Start task to get temperature
        xTaskCreatePinnedToCore(
            tempTask,           /* Function to implement the task */
            "tempTask ",        /* Name of the task */
            4000,               /* Stack size in words */
            NULL,               /* Task input parameter */
            5,                  /* Priority of the task */
            &dhtTempTaskHandle, /* Task handle. */
            1);                 /* Core where the task should run */

        if (dhtTempTaskHandle == NULL)
        {
            Serial.println("[ERROR] Failed to start task for temperature update");
        }
        else
        {
            // Start update of environment data every 10 seconds
            tempTicker.attach(dhtUpdateTime, triggerGetTemp);
        }

        // Signal end of setup() to tasks
        dhtTasksEnabled = true;
    }

#if NTP
    setClock();
#endif

    // BH1750_I2c
    // BH1750_updateFr
    if (BH1750_I2c == "0x23" || BH1750_I2c == "0x5C")
    {
        // Init BH1750 (witch default l2c adres)
        int rc; // Returncode
        long m; // milli for calibration
        bool state = false;

        // if (! BH1750.begin(BH1750_TO_GROUND))
        if (BH1750_I2c == "0x23")
        {
            state = BH1750.begin(BH1750_TO_GROUND);
        }
        else if (BH1750_I2c == "0x5C")
        {
            state = BH1750.begin(BH1750_TO_VCC);
        }

        if (!state)
        {
            Serial.println("Error on initialisation BH1750 GY-302");
        }
        else
        {
            sendDiscoveryLux();
            Serial.println("initialisation BH1750 GY-302 success");
            m = millis();
            rc = BH1750.calibrateTiming();
            m = millis() - m;
            Serial.print("Lux Sensor BH1750 GY-302 calibrated (Time: ");
            Serial.print(m);
            Serial.print(" ms)");
            if (rc != 0)
            {
                Serial.print(" - Lux Sensor Error code ");
                Serial.print(rc);
            }
            Serial.println();

            // start first measure and timecount
            lux_BH1750 = -1; // nothing to compare
            BH1750.start(BH1750_QUALITY_HIGH, 1);
            ms_BH1750 = millis();
        }
    }

    if (I2CDebug)
    {
        Wire.begin();
        Serial.println("\nI2C Scanner");
    }

    connectToMqtt();
    xTaskCreatePinnedToCore(scanForDevices, "BLE Scan", 5120, nullptr, 1, &scannerTask, 1);
    configureOTA();
}

void pirLoop()
{
    if (!pirPin) return;
    int pirValue = digitalRead(pirPin);

    if (pirValue != lastPirValue)
    {
        if (pirValue == HIGH)
        {
            mqttClient.publish((roomsTopic + "/motion").c_str(), 0, 1, "ON");
            Serial.println("PIR MOTION DETECTED!!!");
        }
        else
        {
            mqttClient.publish((roomsTopic + "/motion").c_str(), 0, 1, "OFF");
            Serial.println("NO PIR MOTION DETECTED!!!");
        }

        lastPirValue = pirValue;
    }
}

void radarLoop()
{
    if (!radarPin) return;
    int radarValue = digitalRead(radarPin);

    if (radarValue != lastRadarValue)
    {
        if (radarValue == HIGH)
        {
            mqttClient.publish((roomsTopic + "/motion").c_str(), 0, 1, "ON");
            Serial.println("Radar MOTION DETECTED!!!");
        }
        else
        {
            mqttClient.publish((roomsTopic + "/motion").c_str(), 0, 1, "OFF");
            Serial.println("NO Radar MOTION DETECTED!!!");
        }

        lastRadarValue = radarValue;
    }
}

void dhtLoop()
{
    if (!dht11Pin && !dht22Pin) return;

    if (gotNewTemperature)
    {
        float humidity = dhtSensorData.humidity;
        float temperature = dhtSensorData.temperature;
        Serial.println("Temp: " + String(temperature, 2) + "'C Humidity: " + String(humidity, 1) + "%");

        mqttClient.publish((roomsTopic + "/humidity").c_str(), 0, 1, String(humidity).c_str());
        mqttClient.publish((roomsTopic + "/temperature").c_str(), 0, 1, String(temperature).c_str());

        gotNewTemperature = false;
    }
}

//non blocking ambient sensor
void luxLoop()
{
    if (BH1750_I2c == "0x23" || BH1750_I2c == "0x5C")
    {

        float lux;
        int lux_mqtt;

        if (BH1750.hasValue())
        {
            ms_BH1750 = millis() - ms_BH1750;
            if (!BH1750.saturated())
            {
                lux = BH1750.getLux();
                lux_mqtt = int(lux);

                if (lux != lux_BH1750)
                {
                    lux_BH1750 = lux;
                    // Serial.print("BH1750 (");
                    // Serial.print(ms_BH1750);
                    // Serial.print(" ms): ");
                    // Serial.print(lux);
                    // Serial.println(" lx");
                }

                //convert lx to integer to reduce mqtt traffic, send only if lx changed
                if (lux_mqtt != lux_BH1750_MQTT)
                {
                    lux_BH1750_MQTT = lux_mqtt;
                    Serial.print("BH1750 (");
                    Serial.print(ms_BH1750);
                    Serial.print(" ms): ");
                    Serial.print(lux_mqtt);
                    Serial.println(" lx");
                    mqttClient.publish((roomsTopic + "/lux").c_str(), 0, 1, String(lux_mqtt).c_str());
                }
            }

            BH1750.adjustSettings(90);
            BH1750.start();
            ms_BH1750 = millis();
        }
    }
}

void l2cScanner()
{
    if (!I2CDebug) return;

    byte error, address;
    int nDevices;
    Serial.println("Scanning I2C device...");
    nDevices = 0;

    for (address = 1; address < 127; address++)
    {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0)
        {
            Serial.print("I2C device found at address 0x");

            if (address < 16)
            {
                Serial.print("0");
            }

            Serial.println(address, HEX);
            nDevices++;
        }
        else if (error == 4)
        {
            Serial.print("Unknow error at address 0x");
            if (address < 16)
            {
                Serial.print("0");
            }
            Serial.println(address, HEX);
        }
    }

    if (nDevices == 0)
    {
        Serial.println("No I2C devices found\n");
    }
    else
    {
        Serial.println("done\n");
        I2CDebug = false;
    }
    delay(5000);
}

void loop()
{
    if (otaUpdate)
        ArduinoOTA.handle();
    firmwareUpdate();
    Display.update();
    pirLoop();
    radarLoop();
    dhtLoop();
    luxLoop();
    l2cScanner();
    WiFiSettings.httpLoop();
}
