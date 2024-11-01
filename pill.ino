#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <time.h>

// Configuração do LCD
LiquidCrystal_I2C aLCD(0x27, 16, 2);  // Endereço 0x27, 16 colunas e 2 linhas

// Defina as credenciais do Wi-Fi
const char* ssid = "Isa";  // Altere conforme necessário
const char* password = "12345678";

// Defina as credenciais do Firebase
#define FIREBASE_PROJECT_ID "pilltrack-d82c6"
#define FIREBASE_API_KEY "AIzaSyDEKv7DGeDUKzL9rBMb0PYcxtIaSk8foD0"

// Configurações do NTP
#define NTP_SERVER "pool.ntp.org"
#define UTC_OFFSET -10800  // Fuso horário UTC-3 (Brasil)
#define UTC_OFFSET_DST 0   // Sem horário de verão
const int TOLERANCE_SECONDS = 300;  // 5 minutos

// Definições dos motores
struct Motor {
  int IN1;
  int IN2;
  int IN3;
  int IN4;
  bool motorActivated;
  time_t scheduledTime;
  int doseCount;
};

// Inicialização das instâncias de Motor com listas de inicialização
Motor Motor1 = {13, 12, 14, 27, false, 0, 1}; // Motor 1
Motor Motor5 = {19, 18, 5, 4, false, 0, 1};   // Motor 2
Motor Motor6 = {26, 25, 33, 32, false, 0, 1};   // Motor 3

// Definir o número de passos para 190 graus (baseado no motor 28BYJ-48)
#define STEPS_PER_360_DEGREES 2048

// Sequência de controle do motor de passo
const int stepSequence[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

String lastBoxName = "";  // Para armazenar o último nome exibido
String caixaRef = "";      // Para armazenar a referência da caixa

unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 60000; // 60 segundos
// Definição do pino do buzzer
const int buzzerPin = 23;
const int sensorPin = 21;
bool cupRemoved = false;
int currentQuantity = 0; // Inicializar como 0 ou usar um valor padrão


void setup() {
  Serial.begin(115200);

  // Inicializa os pinos dos motores
  pinMode(Motor1.IN1, OUTPUT);
  pinMode(Motor1.IN2, OUTPUT);
  pinMode(Motor1.IN3, OUTPUT);
  pinMode(Motor1.IN4, OUTPUT);
  
  pinMode(Motor5.IN1, OUTPUT);
  pinMode(Motor5.IN2, OUTPUT);
  pinMode(Motor5.IN3, OUTPUT);
  pinMode(Motor5.IN4, OUTPUT);

  pinMode(Motor6.IN1, OUTPUT);
  pinMode(Motor6.IN2, OUTPUT);
  pinMode(Motor6.IN3, OUTPUT);
  pinMode(Motor6.IN4, OUTPUT);

  // Inicializa o buzzer
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW); // Garante que o buzzer esteja desligado inicialmente

  // Inicializa o LCD
  aLCD.begin();
  aLCD.backlight();
  aLCD.setCursor(0, 0);
  aLCD.print("Connecting WiFi");

  // Conecta ao Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    aLCD.setCursor(0, 1);
    aLCD.print("."); // Indicador no LCD
  }
  aLCD.clear();
  aLCD.setCursor(0, 0);
  aLCD.print("WiFi Connected");

  // Configura o NTP para obter o horário
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  delay(1000);  // Espera 1 segundo para que a mensagem "WiFi Connected" seja visível no LCD
  aLCD.clear();
  aLCD.setCursor(0, 0);
  aLCD.print("Caixa: Carregando");


  fetchMedicationQuantity();  // Inicializa `currentQuantity` com o valor do Firestore

}

// Função para gerar som no buzzer passivo
void playTone(int frequency, int duration) {
  int period = 1000000 / frequency;  // Período em microsegundos
  int pulseWidth = period / 2;       // Largura do pulso em microsegundos

  long cycles = (duration * 1000L) / period; // Número de ciclos

  for (long i = 0; i < cycles; i++) {
    digitalWrite(buzzerPin, HIGH);
    delayMicroseconds(pulseWidth);
    digitalWrite(buzzerPin, LOW);
    delayMicroseconds(pulseWidth);
  }
}

// Função para registrar o evento no Firestore diretamente no documento do medicamento
void registerReport(const String& status) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Obtém o horário atual
    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    String dateTime = String(buffer);

    // URL para atualizar o campo `relatorios` no documento do medicamento
    String updateUrl = "https://firestore.googleapis.com/v1/projects/pilltrack-d82c6/databases/(default)/documents/medicamento/TRg3bom8PwIZGFrvMfLo?updateMask.fieldPaths=relatorios&key=" + FIREBASE_API_KEY;

    // Construir o JSON para adicionar um novo relatório ao array `relatorios`
    String payload = "{\"fields\": {\"relatorios\": {\"arrayValue\": {\"values\": [{\"mapValue\": {\"fields\": {\"horario\": {\"stringValue\": \"" + dateTime + "\"}, \"status\": {\"stringValue\": \"" + status + "\"}}}}]}}}}";

    http.begin(updateUrl);
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.PATCH(payload);

    if (httpResponseCode == 200) {
      Serial.println("Relatório de retirada/falta atualizado com sucesso!");
    } else {
      Serial.println("Erro ao registrar retirada/falta: Código HTTP " + String(httpResponseCode));
    }

    http.end();
  }
}// Função para monitorar o status do copo
void checkCupStatus() {
  int sensorValue = digitalRead(sensorPin);
  if (sensorValue == LOW && !cupRemoved) {
    cupRemoved = true;
    Serial.println("Copo retirado.");

    // Registra a retirada
    registerReport("Retirado");
  } else if (sensorValue == HIGH && cupRemoved) {
    cupRemoved = false;
    Serial.println("Copo não retirado.");
  }
}

// Função para verificar se o tempo limite foi atingido (5 minutos)
void checkDoseTiming() {
  time_t now;
  time(&now);
  
  if (!cupRemoved && (now >= (Motor1.scheduledTime + 300))) { // 300 segundos = 5 minutos
    registerReport("Não Retirado");  // Registra a falta de retirada
  }
}


void fetchMedicationQuantity() { 
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Constrói a URL para o compartimento específico
    String compartimentoUrl = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/caixa/lIaaYkIcCdtKWTQSTzFV?key=" + String(FIREBASE_API_KEY);
    http.begin(compartimentoUrl);
    int httpCode1 = http.GET();

    if (httpCode1 == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        // Obtém a referência do compartimento
        if (!doc["fields"]["compartimento1MedId"].isNull()) {
          caixaRef = doc["fields"]["compartimento1MedId"]["referenceValue"].as<String>();

          // Constrói a URL para o medicamento específico
          String medicamentoUrl = "https://firestore.googleapis.com/v1/" + caixaRef + "?key=" + FIREBASE_API_KEY;
          http.begin(medicamentoUrl);
          int httpCode2 = http.GET();

          if (httpCode2 == 200) {
            String medPayload = http.getString();
            DynamicJsonDocument medicamentoDoc(1024);
            DeserializationError medError = deserializeJson(medicamentoDoc, medPayload);
            
            if (!medError) {
              if (!medicamentoDoc["fields"]["quantidade"].isNull()) {
                currentQuantity = medicamentoDoc["fields"]["quantidade"]["integerValue"].as<int>();
                Serial.println("Quantidade: " + String(currentQuantity));
              } else {
                Serial.println("Campo 'quantidade' não encontrado.");
              }
            } else {
              Serial.println("Erro ao desserializar JSON do medicamento.");
            }
          } else {
            Serial.println("Erro ao obter quantidade do medicamento: Código HTTP " + String(httpCode2));
          }
        } else {
          Serial.println("Campo 'compartimento1MedId' não encontrado.");
        }
      } else {
        Serial.println("Erro ao desserializar JSON do compartimento.");
      }
    } else {
      Serial.println("Erro ao obter compartimento: Código HTTP " + String(httpCode1));
    }
    http.end();
  }
}

// Função para atualizar a quantidade de medicamentos no Firebase
void updateMedicationQuantity(Motor &motor) {
    if (caixaRef == "") {
        Serial.println("Erro: caixaRef está vazio. Execute fetchMedicationQuantity() primeiro.");
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String updateUrl = "https://firestore.googleapis.com/v1/" + caixaRef + "?updateMask.fieldPaths=quantidade&key=" + FIREBASE_API_KEY;

        // Subtrai apenas a dose do motor específico
        int doseToSubtract = motor.doseCount; 
        Serial.println("Dose a subtrair: " + String(doseToSubtract));
        Serial.println("Quantidade atual antes da subtração: " + String(currentQuantity));

        String payload = "{\"fields\": {\"quantidade\": {\"integerValue\": \"" + String(currentQuantity - doseToSubtract) + "\"}}}";

        http.begin(updateUrl);
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.PATCH(payload);

        if (httpResponseCode == 200) {
            Serial.println("Quantidade atualizada com sucesso!");
            currentQuantity -= doseToSubtract; // Atualiza a quantidade localmente
        } else {
            Serial.println("Erro ao atualizar a quantidade: Código HTTP " + String(httpResponseCode));
            Serial.println("Payload enviado: " + payload); // Para depuração
        }
        http.end();
    }
}
// Função para girar o motor
void rotateMotor(Motor &motor, int steps, bool direction) {
  // Ativa o buzzer com um tom
  playTone(1000, 2000); // Toca um tom de 1000 Hz por 200 ms

  unsigned long startTime = millis(); // Armazena o tempo de início
  for (int i = 0; i < steps; i++) {
    int stepIndex = direction ? i % 8 : (7 - i % 8);  // Direção do motor
    setStep(motor, stepSequence[stepIndex][0], stepSequence[stepIndex][1], stepSequence[stepIndex][2], stepSequence[stepIndex][3]);
    delay(1);  // Controle da velocidade do motor

    // Verifica se já se passaram 20 segundos
    if (millis() - startTime >= 100000) {
      break; // Para o motor se passar 20 segundos
    }
  }

  // Desativa o buzzer
  digitalWrite(buzzerPin, LOW);


}

void setStep(Motor &motor, int w1, int w2, int w3, int w4) {
  digitalWrite(motor.IN1, w1);
  digitalWrite(motor.IN2, w2);
  digitalWrite(motor.IN3, w3);
  digitalWrite(motor.IN4, w4);
}

// Função para analisar a string do horário
bool parseFirestoreTime(const String& timeString, time_t &epochTime) {
  int hour, minute;
  if (sscanf(timeString.c_str(), "%d:%d", &hour, &minute) == 2) {
    time_t now;
    time(&now);
    struct tm tm_time = *localtime(&now); // Usa uma cópia da estrutura tm
    tm_time.tm_hour = hour;   // Define a hora
    tm_time.tm_min = minute;  // Define o minuto
    tm_time.tm_sec = 0;       // Define os segundos como 0
    tm_time.tm_isdst = -1;    // Determina automaticamente horário de verão
    epochTime = mktime(&tm_time);
    return true;
  }
  return false;
}

// Função para obter e atualizar dados do Firestore
void fetchFirestoreData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Passo 1: Busca o nome da caixa e a referência dos medicamentos
    String caixaUrl = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/caixa/lIaaYkIcCdtKWTQSTzFV?key=" + String(FIREBASE_API_KEY);
    http.begin(caixaUrl);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(8192); // Buffer aumentado para 8192 bytes
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        // Passo 2: Busca o nome da caixa
        if (!doc["fields"]["NomeDaCaixa"].isNull()) {
          lastBoxName = doc["fields"]["NomeDaCaixa"]["stringValue"].as<String>();
          aLCD.setCursor(0, 0);
          aLCD.print("Caixa: " + lastBoxName + "   ");  // Adiciona espaços para limpar caracteres anteriores
        }

        // Passo 3: Verifica se há referências dos medicamentos nos compartimentos
        // Compartimento 4
        if (!doc["fields"]["compartimento1MedId"].isNull()) {
          String medicamentoRef1 = doc["fields"]["compartimento1MedId"]["referenceValue"].as<String>();  // Referência do medicamento 4
          Serial.println("Referência do medicamento 4: " + medicamentoRef1);

          // Passo 4: Usa a referência do medicamento 4 para buscar os horários
          String medicamentoUrl1 = "https://firestore.googleapis.com/v1/" + medicamentoRef1 + "?key=" + String(FIREBASE_API_KEY);
          http.begin(medicamentoUrl1);
          int httpCode1 = http.GET();

          if (httpCode1 == 200) {
            String medicamentoPayload1 = http.getString();
            DynamicJsonDocument medicamentoDoc1(4096); // Buffer de 4096 bytes
            DeserializationError medicamentoError1 = deserializeJson(medicamentoDoc1, medicamentoPayload1);

            if (!medicamentoError1) {
              // Passo 5: Busca os horários diretamente do campo 'horarios' do medicamento 4
              if (!medicamentoDoc1["fields"]["horarios"].isNull()) {
                JsonArray horariosArray1 = medicamentoDoc1["fields"]["horarios"]["arrayValue"]["values"].as<JsonArray>();

                for (JsonObject horarioObj : horariosArray1) {
                  String horario = horarioObj["mapValue"]["fields"]["horario"]["stringValue"].as<String>();

                  // Verifica se o campo 'dose' existe
                  int dose = 0; // Valor padrão
                  if (!horarioObj["mapValue"]["fields"]["dose"].isNull()) {
                    String doseStr = horarioObj["mapValue"]["fields"]["dose"]["stringValue"].as<String>();
                    dose = doseStr.toInt();
                  }

                  time_t parsedTime;

                  // Passo 6: Converte o horário para epoch time
                  if (parseFirestoreTime(horario, parsedTime)) {
                    if (parsedTime > Motor1.scheduledTime) {
                      Motor1.scheduledTime = parsedTime;
                      Motor1.doseCount = dose;  // Armazena a quantidade de doses
                      Motor1.motorActivated = false;  // Reseta o estado do motor
                      Serial.println("Novo horário agendado (Compartimento 4): " + String(Motor1.scheduledTime) + " com " + String(Motor1.doseCount) + " doses.");
                    }
                  }
                }
              } else {
                Serial.println("Campo 'horarios' do medicamento 4 está nulo ou ausente.");
              }
            } else {
              Serial.println("Erro ao fazer o parsing do JSON (medicamento 4): " + String(medicamentoError1.c_str()));
            }
          } else {
            Serial.println("Erro ao buscar dados do medicamento 4: Código HTTP " + String(httpCode1));
          }

          http.end();
        } else {
          Serial.println("Referência do medicamento 4 não encontrada.");
        }

        // Compartimento 5
        if (!doc["fields"]["compartimento2MedId"].isNull()) {
          String medicamentoRef2 = doc["fields"]["compartimento2MedId"]["referenceValue"].as<String>();  // Referência do medicamento 5
          Serial.println("Referência do medicamento 5: " + medicamentoRef2);

          // Passo 4: Usa a referência do medicamento 5 para buscar os horários
          String medicamentoUrl2 = "https://firestore.googleapis.com/v1/" + medicamentoRef2 + "?key=" + String(FIREBASE_API_KEY);
          http.begin(medicamentoUrl2);
          int httpCode2 = http.GET();

          if (httpCode2 == 200) {
            String medicamentoPayload2 = http.getString();
            DynamicJsonDocument medicamentoDoc2(4096); // Buffer de 4096 bytes
            DeserializationError medicamentoError2 = deserializeJson(medicamentoDoc2, medicamentoPayload2);

            if (!medicamentoError2) {
              // Passo 5: Busca os horários diretamente do campo 'horarios' do medicamento 5
              if (!medicamentoDoc2["fields"]["horarios"].isNull()) {
                JsonArray horariosArray2 = medicamentoDoc2["fields"]["horarios"]["arrayValue"]["values"].as<JsonArray>();

                for (JsonObject horarioObj : horariosArray2) {
                  String horario = horarioObj["mapValue"]["fields"]["horario"]["stringValue"].as<String>();

                  // Verifica se o campo 'dose' existe
                  int dose = 1; // Valor padrão
                  if (!horarioObj["mapValue"]["fields"]["dose"].isNull()) {
                    String doseStr = horarioObj["mapValue"]["fields"]["dose"]["stringValue"].as<String>();
                    dose = doseStr.toInt();
                  }

                  time_t parsedTime;

                  // Passo 6: Converte o horário para epoch time
                  if (parseFirestoreTime(horario, parsedTime)) {
                    if (parsedTime > Motor5.scheduledTime) {
                      Motor5.scheduledTime = parsedTime;
                      Motor5.doseCount = dose;  // Armazena a quantidade de doses
                      Motor5.motorActivated = false;  // Reseta o estado do motor
                      Serial.println("Novo horário agendado (Compartimento 5): " + String(Motor5.scheduledTime) + " com " + String(Motor5.doseCount) + " doses.");
                    }
                  }
                }
              } else {
                Serial.println("Campo 'horarios' do medicamento 5 está nulo ou ausente.");
              }
            } else {
              Serial.println("Erro ao fazer o parsing do JSON (medicamento 5): " + String(medicamentoError2.c_str()));
            }
          } else {
            Serial.println("Erro ao buscar dados do medicamento 5: Código HTTP " + String(httpCode2));
          }

          http.end();
        } else {
          Serial.println("Referência do medicamento 5 não encontrada.");
        }

         // Compartimento 6
        if (!doc["fields"]["compartimento3MedId"].isNull()) {
          String medicamentoRef3 = doc["fields"]["compartimento3MedId"]["referenceValue"].as<String>();  // Referência do medicamento 4
          Serial.println("Referência do medicamento 6: " + medicamentoRef3);

          // Passo 4: Usa a referência do medicamento 4 para buscar os horários
          String medicamentoUrl3 = "https://firestore.googleapis.com/v1/" + medicamentoRef3 + "?key=" + String(FIREBASE_API_KEY);
          http.begin(medicamentoUrl3);
          int httpCode3 = http.GET();

          if (httpCode3 == 200) {
            String medicamentoPayload3 = http.getString();
            DynamicJsonDocument medicamentoDoc3(4096); // Buffer de 4096 bytes
            DeserializationError medicamentoError3 = deserializeJson(medicamentoDoc3, medicamentoPayload3);

            if (!medicamentoError3) {
              // Passo 5: Busca os horários diretamente do campo 'horarios' do medicamento 6
              if (!medicamentoDoc3["fields"]["horarios"].isNull()) {
                JsonArray horariosArray3 = medicamentoDoc3["fields"]["horarios"]["arrayValue"]["values"].as<JsonArray>();

                for (JsonObject horarioObj : horariosArray3) {
                  String horario = horarioObj["mapValue"]["fields"]["horario"]["stringValue"].as<String>();

                  // Verifica se o campo 'dose' existe
                  int dose = 1; // Valor padrão
                  if (!horarioObj["mapValue"]["fields"]["dose"].isNull()) {
                    String doseStr = horarioObj["mapValue"]["fields"]["dose"]["stringValue"].as<String>();
                    dose = doseStr.toInt();
                  }

                  time_t parsedTime;

                  // Passo 6: Converte o horário para epoch time
                  if (parseFirestoreTime(horario, parsedTime)) {
                    if (parsedTime > Motor6.scheduledTime) {
                      Motor6.scheduledTime = parsedTime;
                      Motor6.doseCount = dose;  // Armazena a quantidade de doses
                      Motor6.motorActivated = false;  // Reseta o estado do motor
                      Serial.println("Novo horário agendado (Compartimento 6): " + String(Motor6.scheduledTime) + " com " + String(Motor6.doseCount) + " doses.");
                    }
                  }
                }
              } else {
                Serial.println("Campo 'horarios' do medicamento 6 está nulo ou ausente.");
              }
            } else {
              Serial.println("Erro ao fazer o parsing do JSON (medicamento 6): " + String(medicamentoError3.c_str()));
            }
          } else {
            Serial.println("Erro ao buscar dados do medicamento 6: Código HTTP " + String(httpCode3));
          }

          http.end();
        } else {
          Serial.println("Referência do medicamento 6 não encontrada.");
        }
      } else {
        Serial.println("Erro ao fazer o parsing do JSON (Caixa): " + String(error.c_str()));
      }
    } else {
      Serial.println("Erro ao buscar dados da Caixa: Código HTTP " + String(httpCode));
    }

    http.end();
  } else {
    WiFi.reconnect();
  }
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastFetchTime >= fetchInterval) {
    fetchFirestoreData();
    lastFetchTime = currentMillis;
  }

  time_t now;
  time(&now);
  
  // Exibe o horário no formato desejado no LCD
  char buffer[6];
  strftime(buffer, sizeof(buffer), "%H:%M", localtime(&now));
  aLCD.setCursor(0, 1);
  aLCD.print("Hora: ");
  aLCD.print(buffer);
  aLCD.print("  "); // Limpa caracteres anteriores se necessário

  // Verifica se o horário agendado para o Compartimento 4 foi atingido e o motor não foi ativado
  if (now >= Motor1.scheduledTime && !Motor1.motorActivated) {
    aLCD.setCursor(0, 1);
    aLCD.print("Motor1 girando  ");

    for (int i = 0; i < Motor1.doseCount; i++) {
      // Gira o motor 190 graus no sentido horário
      rotateMotor(Motor1, STEPS_PER_360_DEGREES * 360 / 360, true);  
      delay(3000); // Aguarda o motor completar a rotação

      // Retorna o motor para a posição original
      rotateMotor(Motor1, STEPS_PER_360_DEGREES * 360 / 360, false);  
      delay(1000); // Aguarda o motor retornar
    }

    aLCD.setCursor(0, 1);
    aLCD.print("Motor1 concluido ");

    Motor1.motorActivated = true;  // Define o flag para evitar ativação múltipla

    
  }

  // Verifica se o horário agendado para o Compartimento 5 foi atingido e o motor não foi ativado
  if (now >= Motor5.scheduledTime && !Motor5.motorActivated) {
    aLCD.setCursor(0, 1);
    aLCD.print("Motor5 girando  ");

    for (int i = 0; i < Motor5.doseCount; i++) {
      // Gira o motor 190 graus no sentido horário
      rotateMotor(Motor5, STEPS_PER_360_DEGREES * 360 / 360, true);  
      delay(3000); // Aguarda o motor completar a rotação

      // Retorna o motor para a posição original
      rotateMotor(Motor5, STEPS_PER_360_DEGREES * 360 / 360, false);  
      delay(1000); // Aguarda o motor retornar
    }

    aLCD.setCursor(0, 1);
    aLCD.print("Motor5 concluido ");

    Motor5.motorActivated = true;  // Define o flag para evitar ativação múltipla
  }

    // Verifica se o horário agendado para o Compartimento 6 foi atingido e o motor não foi ativado
  if (now >= Motor6.scheduledTime && !Motor6.motorActivated) {
    aLCD.setCursor(0, 1);
    aLCD.print("Motor6 girando  ");

    for (int i = 0; i < Motor6.doseCount; i++) {
      // Gira o motor 190 graus no sentido horário
      rotateMotor(Motor6, STEPS_PER_360_DEGREES * 360 / 360, true);  
      delay(3000); // Aguarda o motor completar a rotação

      // Retorna o motor para a posição original
      rotateMotor(Motor6, STEPS_PER_360_DEGREES * 360 / 360, false);  
      delay(1000); // Aguarda o motor retornar
    }

    aLCD.setCursor(0, 1);
    aLCD.print("Motor6 concluido ");

    Motor6.motorActivated = true;  // Define o flag para evitar ativação múltipla
  }
}   