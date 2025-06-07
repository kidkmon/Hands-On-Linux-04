// Defina os pinos de LED e LDR
const int ledPin = 25; // LED conectado ao GPIO25
const int ldrPin = 34; // LDR conectado ao GPIO34

int ledValue = 10; // O valor de intensidade do led

// Fazer testes no sensor ldr para encontrar o valor maximo e atribua a variável ldrMax
// Calibrar 'ldrMax' apontando uma luz forte para o LDR e observando o valor máximo de 'analogRead()'.
int ldrMax = 4095; // Valor máximo

// Configurações PWM para o LED (usando a API ledc do ESP32 para controle de intensidade)
const int ledChannel = 0;        // canal PWM
const int freq = 5000;           // Frequência do PWM em Hz
const int resolution = 8;        // Resolução do PWM em bits (8 bits = 0-255 valores)

void setup() {
    Serial.begin(9600); // Velocidade de comunicação serial

    pinMode(ledPin, OUTPUT);
    pinMode(ldrPin, INPUT);
    
    // Configurações do PWM para o LED
    ledcSetup(ledChannel, freq, resolution); // Configura o canal PWM com frequência e resolução
    ledcAttachPin(ledPin, ledChannel);       // Atribui o pino do LED ao canal PWM

    // Inicializa o LED com o valor padrão de intensidade (10)
    ledUpdate(); // Garante que o LED inicie com a intensidade correta

    Serial.printf("SmartLamp Initialized.\n");
    Serial.printf("LED initial intensity: %d (0-100)\n", ledValue);
    Serial.printf("LDR Max expected value: %d ({tem que calibrar)\n", ldrMax);
}

// Função loop será executada infinitamente pelo ESP32
void loop() {
    // Obtenha os comandos enviados pela serial
    // e processe-os com a função processCommand
    if (Serial.available()) { // Verifica se há dados disponíveis na serial
        String command = Serial.readStringUntil('\n'); // Lê a linha completa do comando
        command.trim(); // Remove espaços em branco (início/fim) e caracteres de nova linha/retorno de carro

        // Chama a função para processar o comando recebido
        processCommand(command);
    }
}


void processCommand(String command) {
    // SET_LED X - e X = intensidade de luminosidade do led entre 0 e 100,
    // qualquer valor fora disso vai retornar invalido (-1).

    if (command.startsWith("SET_LED ")) { // Verifica se o comando começa com "SET_LED "
        String valueStr = command.substring(8); // Extrai a parte numérica do comando (após "SET_LED ")
        int value = valueStr.toInt(); // Converte a string numérica para um inteiro

        // A cada comando recebido o esp deverá escrever na saida do monitor serial se o comando foi executado com sucesso ou se houve algum erro
        // saidas: RES SET_LED 1 - quando o valor inserido estiver no intervalo 0 a 100
        // saidas: RES SET_LED -1- qualquer entrada inválida do comando SET_LED
        if (value >= 0 && value <= 100) { // Valida se o valor de X está entre 0 e 100
            ledValue = value; // Atualiza a variável global com o novo valor do LED
            ledUpdate(); // Chama a função para aplicar a nova intensidade ao LED
            Serial.println("RES SET_LED 1"); // Sinaliza sucesso
        } else {
            Serial.println("RES SET_LED -1"); // Sinaliza entrada inválida
        }
    }
    // GET_LED - deve retornar o valor da intensidade atual do led
    // saidas: RES GET_LED Y - resposta para o comando GET_LED onde Y é o valor atual do led
    else if (command == "GET_LED") {
        Serial.print("RES GET_LED "); // Inicia a resposta
        Serial.println(ledValue);      // Retorna o valor atual do LED
    }
    // GET_LDR - deve retornar o valor da leitura do ldr atual
    // saidas: RES GET_LDR Y - resposta para o comando GET_LDR onde Y é o valor atual do ldr
    else if (command == "GET_LDR") {
        int ldrReadValue = ldrGetValue(); // Chama a função para ler e normalizar o valor do LDR
        Serial.print("RES GET_LDR ");     // Inicia a resposta
        Serial.println(ldrReadValue);     // Retorna o valor atual do LDR
    }
    // ERR Unknown command. - resposta para qualquer comando inválido
    else {
        Serial.println("ERR Unknown command."); // Resposta para comando não reconhecido
    }
}

// Função para atualizar o valor do LED
void ledUpdate() {
    // Normalize o valor de intensidade entre 0 e 255 antes de gravar a informação na porta do LED
    // O valor deve converter o valor recebido pelo comando SET_LED para 0 e (2^resolution - 1)
    // Para resolution = 8, isso é 0 a 255.
    int pwmValue = map(ledValue, 0, 100, 0, (1 << resolution) - 1);
    ledcWrite(ledChannel, pwmValue); // Escreve o valor PWM no canal do LED
}

// Função para ler o valor do LDR
int ldrGetValue() {
    // Leia o sensor LDR e retorne o valor normalizado entre 0 e 100
    int rawValue = analogRead(ldrPin); // Lê o valor bruto do LDR
    
    // Normaliza o valor bruto do LDR (de 0 a ldrMax) para um intervalo de 0 a 100.
    int normalized = map(rawValue, 0, ldrMax, 0, 100);
    
    // Garante que o valor normalizado esteja sempre dentro do intervalo 0-100
    // útil em caso de leituras extremas ou calibração imperfeita.
    normalized = constrain(normalized, 0, 100); 

    return normalized;
}
