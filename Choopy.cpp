#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "Audio.h"
#include "driver/i2s.h"

// ================= WIFI =================
const char* ssid     = "YOUR_WIFI_SSID";         // <-- Add your WiFi Name here
const char* password = "YOUR_WIFI_PASSWORD";     // <-- Add your WiFi Password here

// ================= API KEYS =================
const char* OPENAI_API_KEY = "YOUR_OPENAI_API_KEY"; // <-- Add your OpenAI API Key here 

// ================= PINS =================
#define I2S_MIC_WS      25
#define I2S_MIC_SD      32
#define I2S_MIC_SCK     33

#define SPK_BCLK        27
#define SPK_LRC         26
#define SPK_DOUT        22  // ✅ ขา 22

#define BUTTON_PIN      4   

// ================= SETTINGS =================
#define SAMPLE_RATE     16000
#define RECORD_TIME     4       
const int headerSize = 44;
const int waveDataSize = RECORD_TIME * SAMPLE_RATE * 2; 

uint8_t micro_buffer[1024]; 

// Global Pointers
WiFiClientSecure *client = NULL;
Audio *audio = NULL; 

bool isSpeaking = false;
bool isMicInitialized = false;

// ฟังก์ชัน Reset Hardware
void reset_i2s_pins() {
  gpio_reset_pin((gpio_num_t)SPK_BCLK);
  gpio_reset_pin((gpio_num_t)SPK_LRC);
  gpio_reset_pin((gpio_num_t)SPK_DOUT);
  gpio_reset_pin((gpio_num_t)I2S_MIC_WS);
  gpio_reset_pin((gpio_num_t)I2S_MIC_SD);
  gpio_reset_pin((gpio_num_t)I2S_MIC_SCK);
  delay(50);
}

String urlEncode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encodedString += c;
    } else {
      encodedString += '%';
      code0 = (c >> 4) & 0xf;
      encodedString += (char)(code0 > 9 ? code0 + 55 : code0 + 48);
      code1 = c & 0xf;
      encodedString += (char)(code1 > 9 ? code1 + 55 : code1 + 48);
    }
  }
  return encodedString;
}

bool i2s_mic_init() {
  i2s_driver_uninstall(I2S_NUM_0); 
  reset_i2s_pins();
  delay(200); // พักนานขึ้นเพื่อให้ Hardware เคลียร์

  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    // ⚠️ เพิ่มขนาด Buffer เพื่อป้องกัน Crash (xQueueSemaphoreTake)
    .dma_buf_len = 512, 
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) return false;
  
  i2s_zero_dma_buffer(I2S_NUM_0);
  return true;
}

void CreateWavHeader(uint8_t* header, int waveDataSize) {
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  unsigned int fileSize = waveDataSize + headerSize - 8;
  header[4] = (uint8_t)(fileSize & 0xFF);
  header[5] = (uint8_t)((fileSize >> 8) & 0xFF);
  header[6] = (uint8_t)((fileSize >> 16) & 0xFF);
  header[7] = (uint8_t)((fileSize >> 24) & 0xFF);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 0x10; header[17] = 0x00; header[18] = 0x00; header[19] = 0x00;
  header[20] = 0x01; header[21] = 0x00; header[22] = 0x01; header[23] = 0x00;
  header[24] = 0x80; header[25] = 0x3E; header[26] = 0x00; header[27] = 0x00;
  header[28] = 0x00; header[29] = 0x7D; header[30] = 0x00; header[31] = 0x00;
  header[32] = 0x02; header[33] = 0x00; header[34] = 0x10; header[35] = 0x00;
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (uint8_t)(waveDataSize & 0xFF);
  header[41] = (uint8_t)((waveDataSize >> 8) & 0xFF);
  header[42] = (uint8_t)((waveDataSize >> 16) & 0xFF);
  header[43] = (uint8_t)((waveDataSize >> 24) & 0xFF);
}

String RecordAndTranscribe() {
  Serial.println("🎤 Init Mic...");
  isMicInitialized = i2s_mic_init();

  if (!isMicInitialized) {
    Serial.println("❌ Mic Error!");
    return "";
  }
  
  Serial.println("🎤 Recording...");
  
  if (client) delete client;
  client = new WiFiClientSecure();
  client->setInsecure();
  client->setTimeout(20000); 

  String transcript = "";
  long totalVolume = 0; 

  if (client->connect("api.openai.com", 443)) {
    Serial.println("Connected OpenAI");
    
    String boundary = "boundary123";
    String part1 = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n";
    String part2Header = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
    String footer = "\r\n--" + boundary + "--\r\n";
    int totalContentLength = part1.length() + part2Header.length() + headerSize + waveDataSize + footer.length();

    client->println("POST /v1/audio/transcriptions HTTP/1.1");
    client->println("Host: api.openai.com");
    client->println("Authorization: Bearer " + String(OPENAI_API_KEY));
    client->println("Content-Type: multipart/form-data; boundary=" + boundary);
    client->println("Content-Length: " + String(totalContentLength));
    client->println("Connection: close");
    client->println();
    client->print(part1);
    client->print(part2Header);
    uint8_t header[headerSize];
    CreateWavHeader(header, waveDataSize);
    client->write(header, headerSize);

    int bytesRecorded = 0;
    size_t bytesRead = 0;

    while (bytesRecorded < waveDataSize) {
      if (isMicInitialized) {
          esp_err_t result = i2s_read(I2S_NUM_0, (char*)micro_buffer, 1024, &bytesRead, 100);
          if (result == ESP_OK && bytesRead > 0) {
            for (int i=0; i<bytesRead; i+=2) {
               int16_t sample = (micro_buffer[i+1] << 8) | micro_buffer[i];
               totalVolume += abs(sample);
            }
            client->write(micro_buffer, bytesRead);
            bytesRecorded += bytesRead;
          }
      }
    }
    client->print(footer);
    
    float avgVolume = totalVolume / (bytesRecorded/2.0);
    Serial.print("🔊 Avg Vol: "); Serial.println(avgVolume);
    
    if (avgVolume < 30) { 
       Serial.println("⚠️ Too quiet.");
       i2s_driver_uninstall(I2S_NUM_0);
       return ""; 
    }
    
    Serial.println("✅ Sent. Waiting...");
    while (client->connected()) {
      String line = client->readStringUntil('\n');
      if (line == "\r") break; 
    }
    String responseBody = client->readString();
    
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, responseBody);
    if (doc.containsKey("text")) transcript = doc["text"].as<String>();
  } else {
    Serial.println("❌ Connect Failed");
  }

  delete client; 
  client = NULL;

  i2s_driver_uninstall(I2S_NUM_0); 
  delay(100); 
  return transcript;
}

String askOpenAI(String text) {
  if (text == "") return "";
  
  HTTPClient *http = new HTTPClient();
  WiFiClientSecure *secClient = new WiFiClientSecure();
  secClient->setInsecure();
  
  String ans = "";

  if (http->begin(*secClient, "https://api.openai.com/v1/chat/completions")) {
    http->addHeader("Content-Type", "application/json");
    http->addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));
    
    DynamicJsonDocument docPayload(2048);
    docPayload["model"] = "gpt-4o-mini"; 
    docPayload["stream"] = false;
    JsonArray messages = docPayload.createNestedArray("messages");
    JsonObject systemMsg = messages.createNestedObject();
    systemMsg["role"] = "system";
    systemMsg["content"] = "คุณเป็นผู้ช่วยคนแก่ชื่อ ชูปี้ เน้นคำพูดหวาน เอาใจใส่ ตอบสั้นๆ ไม่เกิน 15 คำ ห้ามใช้อีโมจิเด็ดขาด";
    JsonObject userMsg = messages.createNestedObject();
    userMsg["role"] = "user";
    userMsg["content"] = text;
    String jsonString;
    serializeJson(docPayload, jsonString);
    
    int httpCode = http->POST(jsonString);
    if (httpCode == 200) {
      String response = http->getString();
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, response);
      ans = doc["choices"][0]["message"]["content"].as<String>();
      ans.replace("*", ""); ans.replace("#", ""); 
      ans.replace("❤️", ""); 
    }
    http->end();
  }
  
  delete http;
  delete secClient;
  return ans;
}

void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("\n--- System Starting ---");
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" Connected!");

  reset_i2s_pins();

  audio = new Audio();
  audio->setPinout(SPK_BCLK, SPK_LRC, SPK_DOUT);
  audio->setVolume(20);
  
  String hello = urlEncode("พร้อม");
  // ⚠️ URL นี้ใช้ได้แน่นอน
  audio->connecttohost(("http://translate.google.com/translate_tts?ie=UTF-8&tl=th&client=tw-ob&q=" + hello).c_str());
  
  Serial.println("--- Setup Done ---");
}

void loop() {
  if (audio != NULL) {
    audio->loop();
  }

  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("\n🔘 Button Pressed!");
    delay(50); 

    if (digitalRead(BUTTON_PIN) == LOW) {
      isSpeaking = true;

      // 1. ดับเสียง & ลบ Audio (Step นี้สำคัญมาก ต้องทำก่อนยุ่งกับ Mic)
      if (audio != NULL) {
        if (audio->isRunning()) {
            audio->stopSong(); 
        }
        delay(200); // รอให้หยุดจริงๆ
        delete audio;
        audio = NULL; 
      }
      
      // 2. เคลียร์ Hardware I2S
      i2s_driver_uninstall(I2S_NUM_0);
      reset_i2s_pins(); 
      delay(300); // พักนานขึ้นหน่อย กัน Crash

      // 3. เริ่มอัดเสียง
      String userText = RecordAndTranscribe();
      
      if (userText.length() > 0) {
        Serial.println("User: " + userText);
        String aiReply = askOpenAI(userText);
        Serial.println("AI: " + aiReply);

        // 4. เตรียมเล่นเสียง
        i2s_driver_uninstall(I2S_NUM_0); 
        reset_i2s_pins(); 
        delay(300);

        audio = new Audio();
        audio->setPinout(SPK_BCLK, SPK_LRC, SPK_DOUT);
        audio->setVolume(21);
        
        String encodedReply = urlEncode(aiReply);
        String url = "http://translate.google.com/translate_tts?ie=UTF-8&tl=th&client=tw-ob&q=" + encodedReply;
        
        Serial.print("Play URL: "); Serial.println(url);
        
        if (!audio->connecttohost(url.c_str())) {
           Serial.println("❌ Play Failed");
        }
      } else {
        Serial.println("❌ Silence/Error");
        isSpeaking = false; 
        
        // กู้คืน Audio object มารอ
        i2s_driver_uninstall(I2S_NUM_0); 
        reset_i2s_pins(); 
        delay(200);
        
        audio = new Audio();
        audio->setPinout(SPK_BCLK, SPK_LRC, SPK_DOUT);
        audio->setVolume(21);
      }
      
      while(digitalRead(BUTTON_PIN) == LOW) { delay(10); }
    }
  }
}

void audio_eof_mp3(const char *info){ 
  Serial.print("✅ End: "); Serial.println(info);
  isSpeaking = false; 
}
