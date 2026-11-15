/*
 * MASTER FIRMWARE: ĐAI ĐEO BỤNG PHÁT HIỆN TÉ NGÃ KHÔNG DÂY (WIRELESS SENSOR NODE)
 * - Cảm biến không dây: MPU6050 (Gia tốc 3 trục + Con quay hồi chuyển 6-DOF)
 * - Xử lý AI Biên: Bộ lọc Động học 3D + TinyML Edge Impulse (run_classifier)
 * - Actuator: Còi Buzzer 1 (chân D4) hú dồn dập tại chỗ khi ngã + LED (chân D2)
 * - Kết nối: Wi-Fi LAN Viện dưỡng lão + Local MQTT Broker :1883 trên PC Y tá
 */

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <te_nga_inferencing.h>

#define BUZZER_PIN 4
#define LED_PIN    2

const int MPU_ADDR = 0x68;

// ================= CẤU HÌNH KẾT NỐI MẠNG =================
const char* ssid        = "Hello";        // Tên Hotspot điện thoại
const char* password    = "vduy12052005";       // Mật khẩu Hotspot
const char* mqtt_server = "172.20.10.11";        // IP của Laptop chạy Local MQTT Broker
const int   mqtt_port   = 1883;

const char* mqtt_topic_fall = "elderly/node_wireless/fall";
const char* mqtt_topic_cmd  = "elderly/actuator/buzzer_control";

WiFiClient espClient;
PubSubClient client(espClient);

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

int walk_score = 0;
int rest_score = 0;
String current_stable_status = "DUNG / NGOI YEN";
String last_status = "";

unsigned long last_beep_time = 0;
bool beep_state = false;
unsigned long last_heartbeat_time = 0;
unsigned long last_mqtt_reconnect = 0;

void wakeUpMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);
}

// Xử lý lệnh điều khiển Actuator từ Web Server gửi về qua MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.print("[MQTT Wireless Node] Nhận lệnh: ");
  Serial.println(msg);

  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, msg) == DeserializationError::Ok) {
    const char* command = doc["command"];
    if (command && strcmp(command, "BUZZER_OFF") == 0) {
      is_fallen = false;
      recovery_count = 0;
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      Serial.println("🔇 Đã nhận lệnh TẮT CÒI từ Web Dashboard!");
    } else if (command && strcmp(command, "BUZZER_ON") == 0) {
      is_fallen = true;
      fall_trigger_time = millis();
      Serial.println("📢 Kích hoạt còi báo động khẩn cấp!");
    }
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - last_mqtt_reconnect > 5000) {
    last_mqtt_reconnect = millis();
    String clientId = "ESP32_BeltWireless_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("✅ [MQTT] Node Đai Đeo Đã Kết Nối MQTT Broker!");
      client.subscribe(mqtt_topic_cmd);
    }
  }
}

// Gửi gói tin cảnh báo té ngã lên Server
void publishFallAlert(float impact, float ai_confidence) {
  if (client.connected()) {
    StaticJsonDocument<256> doc;
    doc["device_id"] = "ESP32_BELT_WIRELESS_01";
    doc["sensor_type"] = "WIRELESS_MPU6050_TINYML";
    doc["event"] = "FALL_DETECTED";
    doc["impact_g"] = impact;
    doc["ai_score"] = ai_confidence;
    doc["timestamp"] = millis();

    char buffer[256];
    serializeJson(doc, buffer);
    client.publish(mqtt_topic_fall, buffer);
    Serial.println("📡 [MQTT] ĐÃ BẮN BẢN TIN CẢNH BÁO TÉ NGÃ LÊN SERVER!");
  } else {
    Serial.println("⚠️ Mất mạng, còi vẫn hú tại chỗ (Edge Fail-Safe)!");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  Wire.begin(22, 21);
  Wire.setTimeOut(30);
  Wire.setClock(100000);
  wakeUpMPU();

  // Bíp 1 tiếng báo khởi động thành công (BUZZER trên chân D4)
  digitalWrite(BUZZER_PIN, HIGH); delay(80); digitalWrite(BUZZER_PIN, LOW);
  for (int i = 0; i < HIST_SIZE; i++) a_hist[i] = 9.8;

  Serial.println("\n=======================================================");
  Serial.println("  HYBRID: BO LOC DONG HOC 3D + TINYML + WIFI & MQTT    ");
  Serial.println("=======================================================");

  // Khởi tạo Wi-Fi (non-blocking để không làm trễ thuật toán)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Đang kết nối Wi-Fi: ");
  Serial.println(ssid);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  // Duy trì kết nối MQTT
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnectMQTT();
    client.loop();
  }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  byte err = Wire.endTransmission(false);

  if (err != 0) {
    Wire.end(); delay(10);
    Wire.begin(22, 21); wakeUpMPU(); delay(20);
    return;
  }

  int n = Wire.requestFrom((uint16_t)MPU_ADDR, (uint8_t)14, true);
  if (n == 14) {
    int16_t raw_ax = Wire.read()<<8 | Wire.read();
    int16_t raw_ay = Wire.read()<<8 | Wire.read();
    int16_t raw_az = Wire.read()<<8 | Wire.read();
    Wire.read(); Wire.read();
    int16_t raw_gx = Wire.read()<<8 | Wire.read();
    int16_t raw_gy = Wire.read()<<8 | Wire.read();
    int16_t raw_gz = Wire.read()<<8 | Wire.read();

    float ax = (raw_ax / 16384.0) * 9.80665;
    float ay = (raw_ay / 16384.0) * 9.80665;
    float az = (raw_az / 16384.0) * 9.80665;
    float gx = raw_gx / 131.0;
    float gy = raw_gy / 131.0;
    float gz = raw_gz / 131.0;

    float total_a = sqrt(ax*ax + ay*ay + az*az);
    float total_g = sqrt(gx*gx + gy*gy + gz*gz);

    if (total_a < 2.0) { wakeUpMPU(); return; }

    // Cập nhật đỉnh va đập
    if (total_a > peak_acc) peak_acc = total_a;
    if (total_g > peak_gyro) peak_gyro = total_g;
    if (abs(ax) > peak_lateral_acc) peak_lateral_acc = abs(ax);

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
      for (int i = 0; i < HIST_SIZE; i++) sum += a_hist[i];
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
      bool physics_fall = (peak_acc > 28.0 || peak_gyro > 250.0 || peak_lateral_acc > 22.0);
      bool ai_assisted_fall = (ai_te_nga >= 0.70 && peak_acc > 22.0);

      if (physics_fall || ai_assisted_fall) {
        if (!is_fallen) {
          is_fallen = true;
          fall_trigger_time = millis();
          recovery_count = 0;

          // Bắn cảnh báo ngã qua MQTT ngay khi phát hiện
          publishFallAlert(peak_acc, ai_te_nga);
        }
      }

      String display_status = "";

      if (is_fallen) {
        // Còi BUZZER hú nhấp nháy liên tục (không đơ chip nhờ millis)
        if (millis() - last_beep_time >= 80) {
          last_beep_time = millis();
          beep_state = !beep_state;
          digitalWrite(BUZZER_PIN, beep_state ? HIGH : LOW);
          digitalWrite(LED_PIN, beep_state ? HIGH : LOW);
        }

        if (millis() - fall_trigger_time > 1200) {
          if (std_dev >= 0.50) {
            recovery_count++;
          } else {
            recovery_count = 0; // Nằm im -> Còi hú mãi mãi
          }

          if (recovery_count >= 15) {
            is_fallen = false;
            recovery_count = 0;
            walk_score = 16; rest_score = 0;
            current_stable_status = ">>> DANG DI BO <<<";
            display_status = ">>> DANG DI BO <<<";
            digitalWrite(BUZZER_PIN, LOW);
            digitalWrite(LED_PIN, LOW);
          } else {
            display_status = "🚨 TE NGA NGUY HIEM 🚨";
          }
        } else {
          display_status = "🚨 TE NGA NGUY HIEM 🚨";
        }
      }
      else {
        digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(LED_PIN, LOW);

        if (std_dev >= 0.45) {
          walk_score += 2;
          if (rest_score > 0) rest_score -= 2;
          if (walk_score >= 16) { current_stable_status = ">>> DANG DI BO <<<"; walk_score = 16; }
        }
        else {
          rest_score += 2;
          if (walk_score > 0) walk_score -= 2;
          if (rest_score >= 16) { current_stable_status = "DUNG / NGOI YEN"; rest_score = 16; }
        }
        display_status = current_stable_status;
      }

      // In trạng thái sạch đẹp
      if (display_status != last_status || millis() - last_heartbeat_time >= 2000) {
        last_status = display_status;
        last_heartbeat_time = millis();

        Serial.print("Trang thai: [ ");
        Serial.print(display_status);
        Serial.print(" ]  --- (Nang luong: ");
        Serial.print(std_dev, 2);
        Serial.print(" | Gia toc: ");
        Serial.print(total_a, 1);
        Serial.println(" m/s2)");
      }

      peak_acc = 0.0;
      peak_gyro = 0.0;
      peak_lateral_acc = 0.0;
    }

    // === GỌI TINYML run_classifier() MỖI 1 GIÂY ===
    if (feature_ix >= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
      signal_t signal;
      numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);

      ei_impulse_result_t result = { 0 };
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
  delay(20);
}
