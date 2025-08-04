#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#define DHTTYPE DHT11

int ledPin = 5;
int ledValue = 10; // 10 de 255, convertido = 4
int dhtPin = 15;
int ldrPin = 34;
int ldrMax = 4045;
// int dhtMax = 4045;


DHT dht(dhtPin, DHTTYPE);

void setup() {
    Serial.begin(115200);
    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);
    dht.begin();
    delay(2000); // delay pro dht estabilizar
    processCommand("GET_TEMP");
    processCommand("GET_HUM");
    Serial.println("SmartLamp Initialized and Ready.");
}

void loop() { 
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        processCommand(command);

        // consome qualquer dado residual (eco/lixo) do buffer
        // para garantir que o comando não seja lido novamente em loop.
        while (Serial.available()) {
            Serial.read();
        }
    }
    
}

void processCommand(String command) {
    if (command.startsWith("SET_LED")) {
        // Extrai o valor após "SET_LED "
        int newValue = command.substring(8).toInt();
        if (newValue >= 0 && newValue <= 100) {
            ledUpdate(newValue);
            Serial.println("RES SET_LED 1");
        } else {
            Serial.println("RES SET_LED -1");
        }
    }
    else if (command == "GET_LED") {
        Serial.printf("RES GET_LED %d\n", ledGetValue());
    }
    else if (command == "GET_LDR") {
        Serial.printf("RES GET_LDR %d\n", ldrGetValue());
    }
    else if (command == "GET_TEMP") {
        Serial.printf("RES GET_TEMP %.2f\n", tempGetValue());
    }
    else if (command == "GET_HUM") {
        Serial.printf("RES GET_HUM %.2f\n", humGetValue());
    }
    else {
        Serial.println("ERR Unknown command.");
    }
}

void ledUpdate(int newLedValue) {
    ledValue = (newLedValue * 255) / 100;
    analogWrite(ledPin, ledValue);
}

int ledGetValue() {
    if (ledValue < 0) return 0;
    if (ledValue > 255) return 100;
    return (int)((ledValue * 100) + 127) / 255;
}

int ldrGetValue() {
    int ldrValue = analogRead(ldrPin);
    // if (ldrValue < 0) return 0;
    // if (ldrValue > ldrMax) return 100;
    // return (int)(ldrValue * 100) / 4000;
    int ldrNormalizedValue = map(ldrValue, 0, 4045, 0, 100);
    return ldrNormalizedValue;
}

float tempGetValue(){
  float temperature = dht.readTemperature();
  return temperature;
}

float humGetValue(){
  float humidity = dht.readHumidity();
  return humidity;
}
