#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// Definições das credenciais da rede WiFi
const char *ssid = "SSID";
const char *password = "12345678";

// configurações de conexão MQTT
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
const char *mqttServer = "broker.hivemq.com";
int mqttPort = 1883;

// Definições dos pinos do hardware
const int trigPin = 25;       // Pino de trigger do sensor ultrassônico
const int echoPin = 26;       // Pino de eco do sensor ultrassônico
const int Led = 2;            // Pino do LED indicador
const int servoPin = 27;      // Pino do servo motor
const int rfidResetPin = 22;  // Pino de reset do módulo RFID
const int rfidSspin = 21;     // Pino do slave select do módulo RFID

// Instância do leitor RFID e do servo motor
MFRC522 mfrc522(rfidSspin, rfidResetPin);
Servo servo1;

bool lixeiraAberta = true;  // de inicio consideramos que a lixeira está aberta
long lastPublishMillisUltrasonic = 0;
long duracao;
float distancia;
const char *garbageid = "123a4";

void ConectaNoWiFi() {
  Serial.print("Conectando ao WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("Conectado.");
}

void setupMQTT() {
  mqttClient.setServer(mqttServer, mqttPort);
}

// o callback é chamado toda vez que uma mensagem é recebida em um dos tópicos que o dispositivo está inscrito.  é responsável por processar as mensagens MQTT
// recebidas.

// o callback processa a mensagem recebida convertendo o payload (dados) em uma string.em seguida, imprime o tópico e a mensagem recebida no monitor serial.
// Por fim, chama duas funções de controle: controlarAtuadores e controlarAcessoRFID, passando a mensagem recebida como argumento para cada uma delas.
void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Mensagem recebida no tópico: ");
  Serial.println(topic);

  // converte o payload em uma string
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // imprime a mensagem recebida
  Serial.print("Mensagem: ");
  Serial.println(message);

  controlarAtuadores(atoi(message.c_str()));  // converte a mensagem para um inteiro e passa para a função
  controlarAcessoRFID(message[0]);            // passa o primeiro caractere da mensagem para a função
}

void conectaBrokerMQTT() {
  Serial.println("Conectando ao broker MQTT");
  while (!mqttClient.connected()) {
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("Conectado ao Broker MQTT.");
    }
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(Led, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  servo1.attach(servoPin);
  SPI.begin();
  mfrc522.PCD_Init();

  ConectaNoWiFi();
  setupMQTT();
}

void loop() {
  if (!mqttClient.connected()) {
    conectaBrokerMQTT();
  } else {
    unsigned long currentMillis = millis();
    if (currentMillis - lastPublishMillisUltrasonic >= 5000) {
      lastPublishMillisUltrasonic = currentMillis;
      digitalWrite(trigPin, LOW);
      delayMicroseconds(3);
      digitalWrite(trigPin, HIGH);
      delayMicroseconds(10);
      digitalWrite(trigPin, LOW);

      duracao = pulseIn(echoPin, HIGH);
      distancia = duracao * 0.034 / 2;
      StaticJsonDocument <200> doc;
      doc["distance"] = distancia;
      doc["garbageid"] = "123a4";
      char jsonBuffer[512];
      serializeJson(doc, jsonBuffer);
      mqttClient.publish("ultrasonic/garbageflux", jsonBuffer);
      Serial.printf("distancia: %f\n", distancia);
    }
    if (mfrc522.PICC_IsNewCardPresent()) {
      mfrc522.PICC_ReadCardSerial();
      String rfidData;
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        rfidData.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
        rfidData.concat(String(mfrc522.uid.uidByte[i], HEX));
      }
      Serial.println(rfidData);
      StaticJsonDocument <200> doc;
      doc["id"] = rfidData;
      doc["garbageid"] = "123a4";
      char jsonBuffer[512];
      serializeJson(doc, jsonBuffer);
      mqttClient.publish("rfid/garbageflux", jsonBuffer);
      // mqttClient.publish("rfid/garbageflux", rfidData.c_str());
    }

    mqttClient.subscribe("actuator/garbageflux");
    mqttClient.setCallback(callback);

    mqttClient.loop();
  }
}

void controlarAtuadores(int estadoAtuador) {
  if (estadoAtuador == 1) {
    // enviado ordens para fechar a lixeira
    servo1.write(180);
    digitalWrite(Led, HIGH);
    lixeiraAberta = false;  // Lixeira está fechada
  } else if (estadoAtuador == 0) {
    // Foi permitido deixar a lixeira aberta
    servo1.write(0);
    digitalWrite(Led, LOW);
    lixeiraAberta = true;  // Lixeira está aberta
  } else {
  }
}

void controlarAcessoRFID(char estadoAtuador) {
  if (estadoAtuador == 'y' && !lixeiraAberta) {
    // Acesso permitido em uma lixeira fechada
    servo1.write(0);
    digitalWrite(Led, LOW);
    lixeiraAberta = true;
    delay(120000);  // 2 minutos para permitir retirada do lixo
  } else if (estadoAtuador == 'y' && lixeiraAberta) {
    // Acesso permitido mas lixeira já está aberta, não precisa fazer nada
  } else if (estadoAtuador == 'n' && !lixeiraAberta) {
    // Acesso bloqueado e lixeira fechada, não é permitido fazer nada
  } else if (estadoAtuador == 'n' && lixeiraAberta) {
    // Acesso bloqueado e lixeira aberta, não precisa fazer nada
  } else {
  }
}
