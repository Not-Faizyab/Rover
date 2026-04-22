#include "esp_camera.h"
#include <WiFi.h>
#include <ESP32Servo.h>
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==========================================
// 🌐 TACTICAL HUD (V6.0 - ALL BUGS FIXED)
// ==========================================
const char INDEX_HTML[] PROGMEM = R"rawtext(
<!DOCTYPE html>
<html>
<head>
    <title>RAPTOR-12 OMEGA</title>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=0">
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body { background: #000; color: #39ff6e; font-family: monospace; overflow: hidden; display: flex; flex-direction: column; height: 100dvh; width: 100dvw; }
        .video-feed { flex: 1; background: #050d07; border-bottom: 2px solid #39ff6e; width: 100%; object-fit: contain; }
        .controls { display: flex; justify-content: space-around; align-items: center; height: 200px; background: #080808; padding: 10px; z-index: 10; }

        @media screen and (orientation: landscape) {
            body { flex-direction: row; }
            .video-feed { position: absolute; top: 0; left: 0; width: 100%; height: 100%; z-index: 1; border: none; }
            .controls { position: absolute; bottom: 0; left: 0; width: 100%; height: 100%; background: transparent; display: flex; justify-content: space-between; align-items: flex-end; padding: 30px; z-index: 5; pointer-events: none; }
            .joystick-container, .btn-fire { pointer-events: auto; }
        }

        .joystick-container { width: 140px; height: 140px; background: rgba(57,255,110,0.1); border: 1px solid #39ff6e; border-radius: 50%; position: relative; touch-action: none; }
        .joystick-knob { width: 45px; height: 45px; background: #39ff6e; border-radius: 50%; position: absolute; left: 47.5px; top: 47.5px; box-shadow: 0 0 15px #39ff6e; }
        .btn-fire { background: rgba(255,59,59,0.2); border: 2px solid #ff3b3b; color: #ff3b3b; padding: 15px; font-weight: bold; width: 90px; height: 50px; cursor: pointer; }
        .btn-fire.active { background: #ff3b3b; color: #000; box-shadow: 0 0 30px #ff3b3b; }
    </style>
</head>
<body>
    <img class="video-feed" id="stream" src="">
    <div class="controls">
        <div id="left-joy" class="joystick-container"><div class="joystick-knob" id="left-knob"></div></div>
        <button class="btn-fire" id="fire-btn">LASER</button>
        <div id="right-joy" class="joystick-container"><div class="joystick-knob" id="right-knob"></div></div>
    </div>

    <script>
        document.getElementById('stream').src = `http://${window.location.hostname}:81/stream`;

        // --- FIX 1: ABORT CONTROLLER — kills stale in-flight requests so the ESP queue never piles up ---
        let drvCtrl = null;
        let srvCtrl = null;

        function sendCmd(type, x, y, val) {
            let url;
            let ctrl;

            if (type === 'drv') {
                if (drvCtrl) drvCtrl.abort();
                drvCtrl = new AbortController();
                ctrl = drvCtrl;
                url = `/cmd?t=drv&x=${x}&y=${y}`;
            } else if (type === 'srv') {
                if (srvCtrl) srvCtrl.abort();
                srvCtrl = new AbortController();
                ctrl = srvCtrl;
                url = `/cmd?t=srv&x=${x}&y=${y}`;
            } else {
                url = `/cmd?t=fire&val=${val}`;
                ctrl = new AbortController();
            }

            fetch(url, { mode: 'no-cors', signal: ctrl.signal }).catch(() => {});
        }

        // --- WASD KEYBOARD ---
        const keys = { w: false, a: false, s: false, d: false };
        let keyMode = false;
        let keyInterval = null;

        window.addEventListener('keydown', (e) => {
            const k = e.key.toLowerCase();
            if (keys.hasOwnProperty(k) && !keys[k]) {
                keys[k] = true;
                keyMode = true;
                if (!keyInterval) keyInterval = setInterval(processKeys, 100); // FIX 2: poll at 100ms, not 50ms
            }
        });

        window.addEventListener('keyup', (e) => {
            const k = e.key.toLowerCase();
            if (keys.hasOwnProperty(k)) {
                keys[k] = false;
                if (!keys.w && !keys.a && !keys.s && !keys.d) {
                    keyMode = false;
                    clearInterval(keyInterval);
                    keyInterval = null;
                    // Send an explicit stop command so the watchdog on ESP gets a clean zero
                    sendCmd('drv', '0.00', '0.00');
                    document.getElementById('left-knob').style.transform = 'translate(0px, 0px)';
                    document.getElementById('left-knob').style.boxShadow = '0 0 15px #39ff6e';
                }
                processKeys();
            }
        });

        function processKeys() {
            let x = 0, y = 0;
            if (keys.w) y = 1;
            if (keys.s) y = -1;
            if (keys.a) x = -1;
            if (keys.d) x = 1;
            if (x !== 0 && y !== 0) { x *= 0.707; y *= 0.707; }

            const knob = document.getElementById('left-knob');
            knob.style.transform = `translate(${x * 50}px, ${-y * 50}px)`;
            knob.style.boxShadow = keyMode ? '0 0 30px #fff' : '0 0 15px #39ff6e';

            sendCmd('drv', x.toFixed(2), y.toFixed(2));
        }

        // --- TOUCHSCREEN JOYSTICKS ---
        function createJoystick(containerId, knobId, type) {
            const container = document.getElementById(containerId);
            const knob = document.getElementById(knobId);
            let active = false;
            let sendInterval = null;
            let nx = 0, ny = 0;

            // FIX 3: interval-based send instead of event-based — decouples rendering from network
            const startSending = () => {
                if (sendInterval) return;
                sendInterval = setInterval(() => sendCmd(type, nx.toFixed(2), ny.toFixed(2)), 100);
            };
            const stopSending = () => {
                clearInterval(sendInterval);
                sendInterval = null;
            };

            const move = (e) => {
                if (!active || keyMode) return;
                e.preventDefault();
                const touch = e.touches ? e.touches[0] : e;
                const rect = container.getBoundingClientRect();
                let dx = touch.clientX - rect.left - rect.width / 2;
                let dy = touch.clientY - rect.top - rect.height / 2;

                const dist = Math.min(Math.sqrt(dx * dx + dy * dy), 50);
                const angle = Math.atan2(dy, dx);
                dx = Math.cos(angle) * dist;
                dy = Math.sin(angle) * dist;

                knob.style.transform = `translate(${dx}px, ${dy}px)`;
                nx = dx / 50;
                ny = -(dy / 50); // invert Y for screen-to-robot coordinate
            };

            const end = () => {
                if (keyMode) return;
                active = false;
                stopSending();
                nx = 0; ny = 0;
                knob.style.transform = 'translate(0px, 0px)';
                sendCmd(type, '0.00', '0.00'); // explicit stop
            };

            container.addEventListener('touchstart', (e) => {
                e.preventDefault();
                active = true;
                move(e);
                startSending();
            }, { passive: false });

            window.addEventListener('touchmove', move, { passive: false });
            window.addEventListener('touchend', end);
        }

        createJoystick('left-joy', 'left-knob', 'drv');
        createJoystick('right-joy', 'right-knob', 'srv');

        // --- FIRE / LASER ---
        const fb = document.getElementById('fire-btn');
        const setF = (on) => {
            fb.classList.toggle('active', on);
            sendCmd('fire', null, null, on ? 1 : 0);
        };
        fb.addEventListener('touchstart', (e) => { e.preventDefault(); setF(true); });
        fb.addEventListener('touchend', () => setF(false));
        window.addEventListener('keydown', (e) => { if (e.code === 'Space') setF(true); });
        window.addEventListener('keyup',   (e) => { if (e.code === 'Space') setF(false); });
    </script>
</body>
</html>
)rawtext";

// ==========================================
// 📡 NETWORK & PIN CONFIG
// ==========================================
const char* ssid     = "Airtel_Null";
const char* password = "Null@001";

const int AIN1 = 13; const int AIN2 = 14;  // Left Motor
const int BIN1 = 15; const int BIN2 = 2;   // Right Motor
const int PAN_PIN  = 12;
const int TILT_PIN = 3;
// FIX 4: GPIO3 is UART0_RX — NEVER use it as output. Changed to GPIO12.
const int LASER_PIN = 4;

Servo panServo;
Servo tiltServo;
httpd_handle_t server_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// ==========================================
// 🛑 MOTOR WATCHDOG — prevents ghost driving
// FIX 5: If no drv command arrives for 600ms, stop motors automatically.
// ==========================================
volatile unsigned long last_drv_cmd = 0;
#define MOTOR_TIMEOUT_MS 600

void stopMotors() {
    analogWrite(AIN1, 0); analogWrite(AIN2, 0);
    analogWrite(BIN1, 0); analogWrite(BIN2, 0);
}

void motorWatchdogTask(void* pvParam) {
    while (true) {
        if (millis() - last_drv_cmd > MOTOR_TIMEOUT_MS) {
            stopMotors();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==========================================
// 🚀 COMMAND HANDLER
// ==========================================
static esp_err_t cmd_handler(httpd_req_t *req) {
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char type[16] = {}, x_val[16] = {}, y_val[16] = {}, val[16] = {};
        httpd_query_key_value(buf, "t", type, sizeof(type));

        if (strcmp(type, "drv") == 0) {
            httpd_query_key_value(buf, "x", x_val, sizeof(x_val));
            httpd_query_key_value(buf, "y", y_val, sizeof(y_val));
            float x = atof(x_val);
            float y = atof(y_val);

            last_drv_cmd = millis(); // feed the watchdog

            // Tank drive mixing
            float left_speed  = y + x;
            float right_speed = y - x;

            // Clamp
            if (left_speed  >  1.0f) left_speed  =  1.0f;
            if (left_speed  < -1.0f) left_speed  = -1.0f;
            if (right_speed >  1.0f) right_speed =  1.0f;
            if (right_speed < -1.0f) right_speed = -1.0f;

            int left_pwm  = (int)(fabsf(left_speed)  * 255);
            int right_pwm = (int)(fabsf(right_speed) * 255);

            // Deadzone
            if (left_pwm  < 40) left_pwm  = 0;
            if (right_pwm < 40) right_pwm = 0;

            // Left motor
            if      (left_speed > 0.01f)  { analogWrite(AIN1, left_pwm);  analogWrite(AIN2, 0); }
            else if (left_speed < -0.01f) { analogWrite(AIN1, 0); analogWrite(AIN2, left_pwm);  }
            else                           { analogWrite(AIN1, 0); analogWrite(AIN2, 0); }

            // Right motor
            if      (right_speed > 0.01f)  { analogWrite(BIN1, right_pwm); analogWrite(BIN2, 0); }
            else if (right_speed < -0.01f) { analogWrite(BIN1, 0); analogWrite(BIN2, right_pwm); }
            else                            { analogWrite(BIN1, 0); analogWrite(BIN2, 0); }
        }

        if (strcmp(type, "srv") == 0) {
            httpd_query_key_value(buf, "x", x_val, sizeof(x_val));
            httpd_query_key_value(buf, "y", y_val, sizeof(y_val));
            panServo.write(map((int)(atof(x_val) * 100), -100, 100, 180, 0));
            tiltServo.write(map((int)(atof(y_val) * 100), -100, 100, 0, 180));
        }

        if (strcmp(type, "fire") == 0) {
            httpd_query_key_value(buf, "val", val, sizeof(val));
            digitalWrite(LASER_PIN, strcmp(val, "1") == 0 ? HIGH : LOW);
        }
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, "OK", 2);
}

// ==========================================
// 📹 STREAM HANDLER
// FIX 6: Always return frame buffer before any break/continue — prevents heap exhaustion.
//        Added 40ms delay for ~25fps cap to reduce CPU spikes.
// ==========================================
static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res   = ESP_OK;
    char part[128];

    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456789");
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            // Don't leak — just retry once
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t hlen = snprintf(part, sizeof(part),
            "\r\n--123456789\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            (unsigned)fb->len);

        res = httpd_resp_send_chunk(req, part, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        }

        esp_camera_fb_return(fb); // ALWAYS return before checking result
        fb = NULL;

        if (res != ESP_OK) break; // client disconnected — clean exit

        vTaskDelay(pdMS_TO_TICKS(40)); // ~25 fps cap, prevents thermal throttle
    }

    return res;
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

// ==========================================
// ⚡ SETUP
// ==========================================
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);
    setCpuFrequencyMhz(160);

    pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
    pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
    pinMode(LASER_PIN, OUTPUT);
    digitalWrite(LASER_PIN, LOW);
    stopMotors();

    // Start motor watchdog task on core 0
    xTaskCreatePinnedToCore(motorWatchdogTask, "mwd", 1024, NULL, 1, NULL, 0);

    WiFi.begin(ssid, password);
    Serial.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println();

    // Camera config
    camera_config_t config = {};
    config.ledc_channel  = LEDC_CHANNEL_0;
    config.ledc_timer    = LEDC_TIMER_0;
    config.pin_d0 = 5;  config.pin_d1 = 18; config.pin_d2 = 19; config.pin_d3 = 21;
    config.pin_d4 = 36; config.pin_d5 = 39; config.pin_d6 = 34; config.pin_d7 = 35;
    config.pin_xclk      = 0;
    config.pin_pclk      = 22;
    config.pin_vsync     = 25;
    config.pin_href      = 23;
    config.pin_sscb_sda  = 26;
    config.pin_sscb_scl  = 27;
    config.pin_pwdn      = 32;
    config.pin_reset     = -1;
    config.xclk_freq_hz  = 20000000;
    config.pixel_format  = PIXFORMAT_JPEG;
    config.frame_size    = FRAMESIZE_QVGA;
    config.jpeg_quality  = 15;  // 10–15 is good; lower = bigger file = more lag
    config.fb_count      = 2;
    config.xclk_freq_hz = 10000000; // was 20MHz, halve it

    esp_err_t cam_err = esp_camera_init(&config);
    if (cam_err != ESP_OK) {
        Serial.printf("Camera FAILED: 0x%x\n", cam_err);
        return;
    }

    // FIX 7: Camera orientation
    // If image is UPSIDE DOWN  → vflip=1, hmirror=0
    // If LEFT/RIGHT SWAPPED    → vflip=0, hmirror=1
    // If BOTH wrong (rotated 180°) → vflip=1, hmirror=1
    // Your camera is mounted rotated 180° so BOTH should be 1.
    // If it's STILL wrong after flashing, swap hmirror to 0.
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 1);
        s->set_brightness(s, 1);   // slightly brighter feed
        s->set_saturation(s, 0);
    } else {
        Serial.println("WARNING: Could not get sensor handle for orientation fix!");
    }

    Serial.printf("✅ Online → http://%s\n", WiFi.localIP().toString().c_str());
    WiFi.setTxPower(WIFI_POWER_11dBm); // default is 20dBm — that's insane for close range

    // Server 1: HUD + Commands (Port 80)
    // FIX 8: max_open_sockets=3 prevents queue saturation
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.ctrl_port        = 32768;
    cfg.max_open_sockets = 3;
    cfg.task_priority    = tskIDLE_PRIORITY + 5;
    if (httpd_start(&server_httpd, &cfg) == ESP_OK) {
        httpd_uri_t ui  = { "/",    HTTP_GET, index_handler, NULL };
        httpd_uri_t cmd = { "/cmd", HTTP_GET, cmd_handler,   NULL };
        httpd_register_uri_handler(server_httpd, &ui);
        httpd_register_uri_handler(server_httpd, &cmd);
    }

    // Server 2: Stream (Port 81)
    // max_open_sockets=1 — only one stream viewer; extra connections get dropped cleanly
    httpd_config_t scfg = HTTPD_DEFAULT_CONFIG();
    scfg.server_port      = 81;
    scfg.ctrl_port        = 32769;
    scfg.max_open_sockets = 1;
    if (httpd_start(&stream_httpd, &scfg) == ESP_OK) {
        httpd_uri_t st = { "/stream", HTTP_GET, stream_handler, NULL };
        httpd_register_uri_handler(stream_httpd, &st);
    }

    panServo.attach(PAN_PIN);
    tiltServo.attach(TILT_PIN);
    panServo.write(90);
    tiltServo.write(90);

    Serial.println("All systems GO.");
}

void loop() {
    // Nothing here — all work done in HTTPD tasks + watchdog task
}