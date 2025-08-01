int ledPin = 5;
int ledValue = 10;
int ldrPin = 2;
int ldrMax = 4000;

void setup() {
    Serial.begin(115200);
    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);
    Serial.println("SmartLamp Initialized and Ready.");
}

void loop() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim(); // Remove espaços em branco extras
        processCommand(command);
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
    return (int)((ledValue * 100) + 127) / 255; // gambiarra pra truncar
}

int ldrGetValue() {
    int ldrValue = analogRead(ldrPin);
    if (ldrValue < 0) return 0;
    if (ldrValue > ldrMax) return 100;
    return (int)(ldrValue * 100) / 4000;
}
