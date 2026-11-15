#define BLYNK_TEMPLATE_ID "TMPL6o2zvo_Ht"
#define BLYNK_TEMPLATE_NAME "CANH BAO TE NGA"
#define BLYNK_AUTH_TOKEN "pNb4YfUYPUCD6z6yVQApqRwKU54I8jh9"

#include <BlynkSimpleEsp32.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <te_nga_inferencing.h>

// Thông tin Wi-Fi
char ssid[] = "Hello";
char pass[] = "vduy12052005";

#define BUZZER_PIN 4
#define LED_PIN 2

const int MPU_ADDR = 0x68;

// === BỘ ĐỆM AI TINYML (gom 1 giây rồi gọi run_classifier thật) ===
float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
int feature_ix = 0;
float ai_te_nga = 0.0;

// === BỘ ĐỆM VẬT LÝ 3D ===
#define HIST_SIZE 25
float a_hist[HIST_SIZE];
int hist_idx = 0;
int sample_count = 0;

float peak_acc = 0.0;
float peak_gyro = 0.0;
float peak_lateral_acc = 0.0;

// === TRẠNG THÁI HỆ THỐNG ===
bool is_fallen = false;
unsigned long fall_trigger_time = 0;
int recovery_count = 0;
bool sms_sent = false;
bool blynk_notified = false;

// === CẤU HÌNH MODULE SIM (UART2) ===
#define SIM_RX_PIN 16
#define SIM_TX_PIN 17
#define SIM_BAUD 115200
#define EMERGENCY_PHONE "0923834966"
#define SMS_MESSAGE "CANH BAO: Phat hien nguoi than bi TE NGA!"

int walk_score = 0;
int rest_score = 0;
String current_stable_status = "DUNG / NGOI YEN";
String last_status = "";

// === QUẢN LÝ CẢNH BÁO ÂM THANH TẠI CHỖ (ACTIVE BUZZER 5V - GPIO 4) ===
unsigned long last_buzzer_tick = 0;
bool buzzer_output_state = false;
unsigned long last_heartbeat_time = 0;

void batCoiBuzzer() {
  is_fallen = true;
  fall_trigger_time = millis();
  recovery_count = 0;
  Serial.println(
      ">>> [COI HU] Kich hoat bao dong am thanh tai cho (tu ngat sau 3 giay)!");
}

void tatCoiBuzzer() {
  buzzer_output_state = false;
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
}

void capNhatCoiBuzzer() {
  if (!is_fallen) {
    tatCoiBuzzer();
    return;
  }

  unsigned long current_time = millis();

  // Còi hú đúng 3 giây (3000ms) rồi tự động tắt, không cần tắt công tắc nguồn
  if (current_time - fall_trigger_time > 3000) {
    tatCoiBuzzer();
    return;
  }

  // Trong 3 giây: 0-1.2s bíp nhanh 70ms, 1.2s-3s còi hú khẩn cấp SOS 110ms
  unsigned long interval =
      (current_time - fall_trigger_time <= 1200) ? 70 : 110;

  if (current_time - last_buzzer_tick >= interval) {
    last_buzzer_tick = current_time;
    buzzer_output_state = !buzzer_output_state;
    digitalWrite(BUZZER_PIN, buzzer_output_state ? HIGH : LOW);
    digitalWrite(LED_PIN, buzzer_output_state ? HIGH : LOW);
  }
}

void wakeUpMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);
}

int sda_pin = 22;
int scl_pin = 21;

void initSIM() {
  Serial.println(
      ">>> [SIM] Khoi dong HardwareSerial2 (RX:16, TX:17, Baud:115200)...");
  Serial2.begin(SIM_BAUD, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  delay(1000);

  Serial2.println("AT");
  delay(300);
  Serial2.println("ATE0");
  delay(300);
  Serial2.println("AT+CMGF=1");
  delay(200);
  Serial2.println("AT+CSCS=\"GSM\"");
  delay(200);
  Serial2.println("AT+CSCA=\"+84900000022\",145");
  delay(200);
  Serial2.println("AT+CSMP=17,167,0,0");
  delay(200);
  Serial2.println("AT+CGSMS=1");
  delay(200);

  // Kiểm tra số trung tâm tin nhắn SMSC
  Serial2.println("AT+CSCA?");
  delay(300);
  while (Serial2.available()) {
    String s = Serial2.readStringUntil('\n');
    s.trim();
    if (s.length() > 0)
      Serial.println(">>> [SIM CSCA] " + s);
  }

  // Kiểm tra kênh truyền SMS CGSMS
  Serial2.println("AT+CGSMS?");
  delay(300);
  while (Serial2.available()) {
    String s = Serial2.readStringUntil('\n');
    s.trim();
    if (s.length() > 0)
      Serial.println(">>> [SIM CGSMS] " + s);
  }

  // Kiểm tra thẻ SIM có được nhận diện không
  Serial2.println("AT+CPIN?");
  delay(300);
  while (Serial2.available()) {
    String s = Serial2.readStringUntil('\n');
    s.trim();
    if (s.length() > 0)
      Serial.println(">>> [SIM CPIN] " + s);
  }

  // Kiểm tra cường độ sóng
  Serial2.println("AT+CSQ");
  delay(300);
  while (Serial2.available()) {
    String s = Serial2.readStringUntil('\n');
    s.trim();
    if (s.length() > 0)
      Serial.println(">>> [SIM CSQ] " + s);
  }

  // Kiểm tra đăng ký mạng GSM thoại/SMS
  Serial2.println("AT+CREG?");
  delay(300);
  while (Serial2.available()) {
    String s = Serial2.readStringUntil('\n');
    s.trim();
    if (s.length() > 0)
      Serial.println(">>> [SIM CREG] " + s);
  }

  // Kiểm tra tên nhà mạng
  Serial2.println("AT+COPS?");
  delay(300);
  while (Serial2.available()) {
    String s = Serial2.readStringUntil('\n');
    s.trim();
    if (s.length() > 0)
      Serial.println(">>> [SIM NHA MANG] " + s);
  }

  while (Serial2.available())
    Serial2.read();
  Serial.println(">>> [SIM] Khoi tao module SIM hoan tat!");
}

bool send_sms(const char *sdt, const char *text) {
  Serial.print(">>> [SIM] Dang gui SMS toi so: ");
  Serial.println(sdt);

  while (Serial2.available())
    Serial2.read();

  // Đảm bảo module luôn ở Text Mode và chuẩn mã hóa SMS trước khi gửi
  Serial2.println("AT+CMGF=1");
  delay(150);
  Serial2.println("AT+CSCS=\"GSM\"");
  delay(150);
  Serial2.println("AT+CSMP=17,167,0,0");
  delay(150);
  Serial2.println("AT+CSCA=\"+84900000022\",145");
  delay(150);
  Serial2.println("AT+CGSMS=1");
  delay(150);

  while (Serial2.available())
    Serial2.read();

  Serial2.print("AT+CMGS=\"");
  Serial2.print(sdt);
  Serial2.print("\"\r\n");

  unsigned long start_wait = millis();
  bool got_prompt = false;
  while (millis() - start_wait < 3000) {
    if (Serial2.available()) {
      char c = Serial2.read();
      if (c == '>') {
        got_prompt = true;
        break;
      }
    }
    delay(10);
  }

  if (!got_prompt) {
    Serial.println(">>> [SIM LOI] Khong nhan duoc dau '>' tu SIM (Kiem tra "
                   "nguon/anten/tai khoan)!");
    Serial2.write(27);
    return false;
  }

  Serial2.print(text);
  Serial2.write(26);

  start_wait = millis();
  bool success = false;
  while (millis() - start_wait < 15000) {
    if (Serial2.available()) {
      String resp = Serial2.readStringUntil('\n');
      resp.trim();
      if (resp.length() > 0) {
        Serial.print(">>> [SIM PHAN HOI]: ");
        Serial.println(resp);
      }
      if (resp.indexOf("OK") != -1 || resp.indexOf("+CMGS:") != -1) {
        success = true;
        break;
      }
      if (resp.indexOf("ERROR") != -1) {
        break;
      }
    }
    delay(20);
  }

  if (success) {
    Serial.println(
        ">>> [SIM THANH CONG] Da phat SMS canh bao te nga den nguoi than!");
  } else {
    Serial.println(
        ">>> [SIM CANH BAO] Khong gui duoc SMS hoac bi loi tu mang!");
  }
  return success;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n>>> [1/5] ESP32 DA KHOI DONG THANH CONG! <<<");

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  Serial.println(">>> [2/5] Da cau hinh chan Buzzer va LED.");

  Serial.println(">>> [3/5] Khoi tao module SIM 4G...");
  initSIM();

  // Thu phuong an 1: SDA=22, SCL=21
  Serial.println(">>> [4/5] Thu ket noi MPU voi Wire.begin(22, 21)...");
  Wire.begin(22, 21);
  Wire.setTimeOut(30);
  Wire.setClock(100000);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  byte test_err = Wire.endTransmission(true);

  if (test_err == 0) {
    sda_pin = 22;
    scl_pin = 21;
    Serial.println(">>> KET NOI THANH CONG voi Wire.begin(22, 21)! <<<");
  } else {
    Serial.print(">>> Wire.begin(22, 21) that bai (Loi I2C: ");
    Serial.print(test_err);
    Serial.println("). Dang thu dao sang Wire.begin(21, 22)...");

    Wire.end();
    delay(50);
    Wire.begin(21, 22);
    Wire.setTimeOut(30);
    Wire.setClock(100000);
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x6B);
    Wire.write(0x00);
    test_err = Wire.endTransmission(true);

    if (test_err == 0) {
      sda_pin = 21;
      scl_pin = 22;
      Serial.println(
          ">>> CHINH XAC! Wire.begin(21, 22) HOAT DONG THANH CONG 100%! <<<");
    } else {
      Serial.print(">>> Ca 2 cach deu bi Loi I2C: ");
      Serial.print(test_err);
      Serial.println(" (Kiem tra lai nguon 3.3V hoac tiep xuc chan MPU)!");
    }
  }

  // Bíp 1 tiếng báo khởi động thành công (BUZZER trên chân D4)
  digitalWrite(BUZZER_PIN, HIGH);
  delay(80);
  digitalWrite(BUZZER_PIN, LOW);
  for (int i = 0; i < HIST_SIZE; i++)
    a_hist[i] = 9.8;

  // Quét và kết nối Wi-Fi & Blynk IoT 2.0
  Serial.println(">>> [5/5] Dang quet cac mang Wi-Fi 2.4GHz xung quanh...");
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Tắt chế độ ngủ để giữ kết nối liên tục với router

  WiFi.onEvent(
      [](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.print("\n>>> [WIFI EVENT] Bi ngat ket noi! Ma ly do: ");
        Serial.println(info.wifi_sta_disconnected.reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  int n_wifi = WiFi.scanNetworks();
  bool thay_ssid = false;
  Serial.print(">>> Tim thay ");
  Serial.print(n_wifi);
  Serial.println(" mang Wi-Fi:");
  for (int i = 0; i < n_wifi; i++) {
    Serial.print("    [");
    Serial.print(i + 1);
    Serial.print("] ");
    Serial.print(WiFi.SSID(i));
    Serial.print(" (Song: ");
    Serial.print(WiFi.RSSI(i));
    Serial.println(" dBm)");
    if (WiFi.SSID(i) == ssid) {
      thay_ssid = true;
    }
  }

  if (!thay_ssid) {
    Serial.println(">>> [CANH BAO] KHONG TIM THAY mang Wi-Fi: " + String(ssid) +
                   " !");
    Serial.println(">>> (Luu y: ESP32 chi bat duoc 2.4GHz, neu router phat "
                   "5GHz ESP32 se khong thay)");
  } else {
    Serial.println(">>> [OK] Da tim thay mang: " + String(ssid) +
                   ", tien hanh ket noi...");
  }

  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  unsigned long start_wifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start_wifi < 10000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n>>> [WIFI THANH CONG] Da ket noi! IP: " +
                   WiFi.localIP().toString());
    Blynk.connect(5000);
    if (Blynk.connected()) {
      Serial.println(">>> [BLYNK THANH CONG] Da ket noi Cloud Blynk!");
      Blynk.virtualWrite(V0, "DUNG / NGOI YEN");
      Blynk.virtualWrite(V1, 0);
    }
  } else {
    Serial.print("\n>>> [WIFI THAT BAI] Ma trang thai loi: ");
    Serial.println(WiFi.status());
    Serial.println(">>> (1=Khong thay SSID, 4=Sai pass, 6=Mat ket noi). ESP32 "
                   "se tiep tuc thu o background!");
  }

  Serial.println("\n=======================================================");
  Serial.println("  HYBRID: BO LOC DONG HOC 3D + TINYML run_classifier()  ");
  Serial.println("=======================================================");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!Blynk.connected()) {
      static unsigned long last_blynk_retry = 0;
      if (millis() - last_blynk_retry > 5000) {
        last_blynk_retry = millis();
        Serial.println(">>> [BLYNK] Dang thu ket noi lai Cloud...");
        Blynk.connect(2000);
        if (Blynk.connected()) {
          Serial.println(">>> [BLYNK THANH CONG] Da ket noi Cloud Blynk!");
          Blynk.virtualWrite(V0, current_stable_status);
          Blynk.virtualWrite(V1, is_fallen ? 1 : 0);
        }
      }
    } else {
      Blynk.run();
    }
  }

  static unsigned long last_loop_tick = 0;
  if (millis() - last_loop_tick >= 1000) {
    last_loop_tick = millis();
    Serial.println(">>> [LOOP] Dang chay binh thuong...");
  }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  byte err = Wire.endTransmission(true);

  if (err != 0) {
    static unsigned long last_err_print = 0;
    if (millis() - last_err_print > 1000) {
      last_err_print = millis();
      Serial.print(">>> [CANH BAO] Mat ket noi MPU6050! Ma loi I2C: ");
      Serial.println(err);
    }
    Wire.end();
    delay(10);
    Wire.begin(sda_pin, scl_pin);
    wakeUpMPU();
    delay(20);
    return;
  }

  int n = Wire.requestFrom((uint16_t)MPU_ADDR, (uint8_t)14, true);
  if (n != 14) {
    static unsigned long last_n_print = 0;
    if (millis() - last_n_print > 1000) {
      last_n_print = millis();
      Serial.print(">>> [LOI DOC] Wire.requestFrom chi tra ve: ");
      Serial.print(n);
      Serial.println(" bytes (phai la 14 bytes)!");
    }
    return;
  }

  if (n == 14) {
    int16_t raw_ax = Wire.read() << 8 | Wire.read();
    int16_t raw_ay = Wire.read() << 8 | Wire.read();
    int16_t raw_az = Wire.read() << 8 | Wire.read();
    Wire.read();
    Wire.read();
    int16_t raw_gx = Wire.read() << 8 | Wire.read();
    int16_t raw_gy = Wire.read() << 8 | Wire.read();
    int16_t raw_gz = Wire.read() << 8 | Wire.read();

    float ax = (raw_ax / 16384.0) * 9.80665;
    float ay = (raw_ay / 16384.0) * 9.80665;
    float az = (raw_az / 16384.0) * 9.80665;
    float gx = raw_gx / 131.0;
    float gy = raw_gy / 131.0;
    float gz = raw_gz / 131.0;

    float total_a = sqrt(ax * ax + ay * ay + az * az);
    float total_g = sqrt(gx * gx + gy * gy + gz * gz);

    if (total_a < 2.0) {
      static unsigned long last_zero_print = 0;
      if (millis() - last_zero_print > 1000) {
        last_zero_print = millis();
        Serial.print(
            ">>> [CANH BAO] MPU bi ngu hoac doc toan so 0: total_a = ");
        Serial.println(total_a);
      }
      wakeUpMPU();
      return;
    }

    // Cập nhật đỉnh va đập
    if (total_a > peak_acc)
      peak_acc = total_a;
    if (total_g > peak_gyro)
      peak_gyro = total_g;
    if (abs(ax) > peak_lateral_acc)
      peak_lateral_acc = abs(ax);

    // Nạp vào bộ đệm Vật lý
    a_hist[hist_idx] = total_a;
    hist_idx = (hist_idx + 1) % HIST_SIZE;

    // Nạp vào bộ đệm AI TinyML
    if (feature_ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
      features[feature_ix++] = ax;
      features[feature_ix++] = ay;
      features[feature_ix++] = az;
      features[feature_ix++] = gx;
      features[feature_ix++] = gy;
      features[feature_ix++] = gz;
    }

    sample_count++;

    // === ĐÁNH GIÁ VẬT LÝ MỖI 100MS ===
    if (sample_count >= 5) {
      sample_count = 0;

      float sum = 0.0;
      for (int i = 0; i < HIST_SIZE; i++)
        sum += a_hist[i];
      float mean = sum / (float)HIST_SIZE;
      float variance = 0.0;
      for (int i = 0; i < HIST_SIZE; i++) {
        float diff = a_hist[i] - mean;
        variance += diff * diff;
      }
      float std_dev = sqrt(variance / (float)HIST_SIZE);

      // 1. PHÁT HIỆN TÉ NGÃ HYBRID:
      //    - Vật lý 3D: Bắt mọi cú ngã mạnh
      //    - AI TinyML: Hỗ trợ bắt ngã nhẹ KHI CÓ va đập kèm theo
      bool physics_fall =
          (peak_acc > 28.0 || peak_gyro > 250.0 || peak_lateral_acc > 22.0);
      bool ai_assisted_fall = (ai_te_nga >= 0.70 && peak_acc > 22.0);

      if (physics_fall || ai_assisted_fall) {
        // Kích hoạt ngã mới hoặc tiếp tục hú 3 giây nếu có va đập té ngã lần
        // nữa sau khi còi đã ngắt
        if (!is_fallen || (millis() - fall_trigger_time > 3000)) {
          batCoiBuzzer();
        }
      }

      String display_status = "";

      if (is_fallen) {
        if (millis() - fall_trigger_time > 1200) {
          if (std_dev >= 0.50) {
            recovery_count++;
          } else {
            recovery_count = 0; // Nằm im (còi tự ngắt sau 3s, gửi SMS 1 lần)

            // Kích hoạt phát SMS cảnh báo độc lập (thử lại mỗi 8s nếu chưa
            // thành công)
            if (!sms_sent) {
              static unsigned long last_sms_attempt = 0;
              if (millis() - last_sms_attempt >= 8000) {
                last_sms_attempt = millis();
                if (send_sms(EMERGENCY_PHONE, SMS_MESSAGE)) {
                  sms_sent = true;
                }
              }
            }

            // Kích hoạt cảnh báo lên App Blynk & Gửi Push Notification hú
            // chuông độc lập
            if (Blynk.connected()) {
              Blynk.virtualWrite(V0, "🚨 TE NGA 🚨");
              Blynk.virtualWrite(V1, 1);
              if (!blynk_notified) {
                Serial.println(">>> [BLYNK EVENT] Dang gui Push Notification "
                               "va am thanh ve dien thoai...");
                Blynk.logEvent("te_nga",
                               "CANH BAO: Phat hien nguoi than bi TE NGA!");
                blynk_notified = true;
              }
            }
          }

          if (recovery_count >= 15) {
            is_fallen = false;
            recovery_count = 0;
            sms_sent = false;       // Reset cờ gửi SMS khi nạn nhân đã phục hồi
            blynk_notified = false; // Reset cờ thông báo Blynk
            tatCoiBuzzer();
            walk_score = 16;
            rest_score = 0;
            current_stable_status = "DANG DI BO";
            display_status = "DANG DI BO";
            if (Blynk.connected()) {
              Blynk.virtualWrite(V0, display_status);
              Blynk.virtualWrite(V1, 0);
            }
          } else {
            display_status = "🚨 TE NGA 🚨";
          }
        } else {
          display_status = "🚨 TE NGA 🚨";
        }
      } else {
        tatCoiBuzzer();

        if (std_dev >= 0.45) {
          walk_score += 2;
          if (rest_score > 0)
            rest_score -= 2;
          if (walk_score >= 16) {
            current_stable_status = "DANG DI BO";
            walk_score = 16;
          }
        } else {
          rest_score += 2;
          if (walk_score > 0)
            walk_score -= 2;
          if (rest_score >= 16) {
            current_stable_status = "DUNG / NGOI YEN";
            rest_score = 16;
          }
        }
        display_status = current_stable_status;
      }

      // In trạng thái sạch đẹp và đồng bộ lên Blynk Cloud
      if (display_status != last_status ||
          millis() - last_heartbeat_time >= 2000) {
        last_status = display_status;
        last_heartbeat_time = millis();

        Serial.print("Trang thai: [ ");
        Serial.print(display_status);
        Serial.print(" ]  --- (Nang luong: ");
        Serial.print(std_dev, 2);
        Serial.print(" | Gia toc: ");
        Serial.print(total_a, 1);
        Serial.println(" m/s2)");

        if (Blynk.connected()) {
          Blynk.virtualWrite(V0, display_status);
          Blynk.virtualWrite(V1, is_fallen ? 1 : 0);
        }
      }

      peak_acc = 0.0;
      peak_gyro = 0.0;
      peak_lateral_acc = 0.0;
    }

    // === GỌI TINYML run_classifier() MỖI 1 GIÂY ===
    if (feature_ix >= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
      signal_t signal;
      numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE,
                                &signal);

      ei_impulse_result_t result = {0};
      EI_IMPULSE_ERROR r = run_classifier(&signal, &result, false);

      if (r == EI_IMPULSE_OK) {
        for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
          String lbl = String(result.classification[i].label);
          if (lbl == "te_nga") {
            ai_te_nga = result.classification[i].value;
          }
        }
      }

      feature_ix = 0;
    }
  }

  // Quản lý còi hú tại chỗ độc lập liên tục (non-blocking)
  capNhatCoiBuzzer();
  delay(20);
}
