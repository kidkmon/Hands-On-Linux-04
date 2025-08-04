#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/kobject.h> // Adicionado para sysfs
#include <linux/sysfs.h>   // Adicionado para sysfs

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102)");
MODULE_LICENSE("GPL");

#define MAX_RECV_LINE 100

// --- Variáveis Globais ---
static struct usb_device *smartlamp_device;
static uint usb_in, usb_out;
static char *usb_in_buffer, *usb_out_buffer;
static int usb_max_size;
int LDR_value = 0;
static struct kobject *smartlamp_kobj; // Adicionado para sysfs

// --- Comandos de Controle para o Chip CP210x ---
#define CP210X_IFC_ENABLE 0x00
#define UART_ENABLE 0x01

// --- Configuração do Dispositivo USB ---
#define VENDOR_ID   4292 // valor decimal, hex: 0x10c4
#define PRODUCT_ID  60000 // valor decimal, hex: 0xea60
static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };
MODULE_DEVICE_TABLE(usb, id_table);

// --- Protótipos das Funções ---
static int  usb_probe(struct usb_interface *ifce, const struct usb_device_id *id);
static void usb_disconnect(struct usb_interface *ifce);
static int  smartlamp_transaction(const char* command, char *response_buf); // funcao de comunicacao unificada
static ssize_t led_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t led_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t temp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t hum_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t ldr_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);


// --- Definições do Sysfs (Adicionado) ---
static struct kobj_attribute led_attribute = __ATTR(led, 0664, led_show, led_store);
static struct kobj_attribute temp_attribute = __ATTR(temp, 0444, temp_show, NULL);  // TAREFA 5: attr de temperatura
static struct kobj_attribute hum_attribute = __ATTR(hum, 0444, hum_show, NULL);   // TAREFA 5: attr de umidade
static struct kobj_attribute ldr_attribute = __ATTR(ldr, 0444, ldr_show, NULL);   // TAREFA 5: attr do ldr


static struct attribute *attrs[] = {
    &led_attribute.attr,
    &temp_attribute.attr, // TAREFA 5: Adiciona temp a lista
    &hum_attribute.attr,  // TAREFA 5: Adiciona hum a lista
    &ldr_attribute.attr, // TAREFA 5: Adiciona ldr a lista
    NULL, // Fim da lista
};

static struct attribute_group attr_group = {
    .attrs = attrs,
};


// --- Estrutura do Driver USB ---
static struct usb_driver smartlamp_driver = {
    .name        = "smartlamp",
    .probe       = usb_probe,
    .disconnect  = usb_disconnect,
    .id_table    = id_table,
};
module_usb_driver(smartlamp_driver);

/*

Comandos sysfs:
ls -l /sys/kernel/smartlamp/                       = verificar se tudo foi inicialziado corretamente
cat /sys/kernel/smartlamp/led                      = LER o valor do led
echo "75" | sudo tee /sys/kernel/smartlamp/led     = ALTERAR o valor do led
cat /sys/kernel/smartlamp/temp                     = LER o valro da temperatura
cat /sys/kernel/smartlamp/hum                      = LER o valor da umidade
cat /sys/kernel/smartlamp/ldr                      = LER o valor do LDR

*/

// TAREFA 5: Função unificada para enviar um comando e receber a resposta
// Tentativa de simplificar o codigo
static int smartlamp_transaction(const char* command, char *response_buf) {
    int ret, actual_size;
    int retries = 5;

    // Ativa a UART para garantir que o dispositivo está pronto
    ret = usb_control_msg(smartlamp_device, usb_sndctrlpipe(smartlamp_device, 0),
                          CP210X_IFC_ENABLE, 0x41, UART_ENABLE,
                          smartlamp_device->actconfig->interface[0]->altsetting[0].desc.bInterfaceNumber,
                          NULL, 0, 1000);
    if (ret < 0) { printk(KERN_ERR "SmartLamp: Falha ao ativar a UART. Erro: %d\n", ret); return ret; }

    // Envia o comando
    strncpy(usb_out_buffer, command, usb_max_size);
    ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out),
                       usb_out_buffer, strlen(command), &actual_size, 1000);
    if (ret) { printk(KERN_ERR "SmartLamp: Falha ao enviar comando '%s'. Erro: %d\n", command, ret); return ret; }

    msleep(100);

    // Tenta ler a resposta em um laço para lidar com erros como EAGAIN (-11)
    while (retries-- > 0) {
        ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in),
                           response_buf, MAX_RECV_LINE - 1, &actual_size, 1000);
        if (ret == 0) {
            response_buf[actual_size] = '\0';
            return 0; // Sucesso
        }
        msleep(50);
    }
    printk(KERN_ERR "SmartLamp: Falha ao ler resposta para '%s'. Erro final: %d\n", command, ret);
    return ret;
}

// Executado quando o dispositivo é conectado na USB
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *usb_endpoint_in, *usb_endpoint_out;
    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    smartlamp_device = interface_to_usbdev(interface);

    // Encontra os endpoints e aloca os buffers
    if (usb_find_common_endpoints(interface->cur_altsetting, &usb_endpoint_in, &usb_endpoint_out, NULL, NULL)) {
        printk(KERN_ERR "SmartLamp: Endpoints nao encontrados\n"); return -EIO;
    }
    usb_max_size = usb_endpoint_maxp(usb_endpoint_in);
    usb_in = usb_endpoint_in->bEndpointAddress;
    usb_out = usb_endpoint_out->bEndpointAddress;
    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    // Inicia a comunicação enviando o comando
    msleep(200);
    // ALTERAÇÃO TAREFA 5: Usa a função de transação para ler o LDR
    if (smartlamp_transaction("GET_LDR\n", usb_in_buffer) == 0) {
        sscanf(usb_in_buffer, "RES GET_LDR %d", &LDR_value);
        printk(KERN_INFO "SmartLamp: SUCESSO! Valor do LDR lido: %d\n", LDR_value);
    } else {
        printk(KERN_WARNING "SmartLamp: Nao foi possivel ler o valor do LDR.\n");
        LDR_value = -1;
    }

    // Cria a interface sysfs
    smartlamp_kobj = kobject_create_and_add("smartlamp", kernel_kobj);
    if (!smartlamp_kobj) {
        return -ENOMEM;
    }

    if (sysfs_create_group(smartlamp_kobj, &attr_group)) {
        kobject_put(smartlamp_kobj);
        return -ENOMEM;
    }
    printk(KERN_INFO "SmartLamp: Interface sysfs criada em /sys/kernel/smartlamp\n");

    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    kobject_put(smartlamp_kobj); // Adicionado para remover a interface sysfs
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    kfree(usb_in_buffer);
    kfree(usb_out_buffer);
    smartlamp_device = NULL; // Adicionado para segurança e prevenção de crashes
}

// --- Funcao do Sysfs adicionadas ---

// Função chamada quando o arquivo /sys/kernel/smartlamp/led é lido
static ssize_t led_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    int value = -1;
    if (!smartlamp_device) return -ENODEV;

    // TAREFA 5: Simplificado para usar a função de transação
    if (smartlamp_transaction("GET_LED\n", usb_in_buffer) == 0) {
        sscanf(usb_in_buffer, "RES GET_LED %d", &value);
        printk(KERN_INFO "SmartLamp: Lendo valor do LED: %d\n", value);
    }
    return sprintf(buf, "%d\n", value);
}

// Função chamada quando algo é escrito no arquivo /sys/kernel/smartlamp/led
static ssize_t led_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int new_value; // A variável 'ret' não é mais necessária aqui
    char command[32];

    if (!smartlamp_device) return -ENODEV;
    if (kstrtoint(buf, 10, &new_value) != 0) return -EINVAL;

    printk(KERN_INFO "SmartLamp: Alterando valor do LED para %d\n", new_value);

    // O bloco antigo de usb_control_msg e usb_bulk_msg foi substituido 
    // pela chamada a função de transação unificada, que é mais robusta.

    // Monta o comando a ser enviado
    snprintf(command, sizeof(command), "SET_LED %d\n", new_value);
    
    // Envia o comando e recebe a resposta usando a função unificada
    if (smartlamp_transaction(command, usb_in_buffer) < 0) {
        // Se a comunicação falhar, retorna um erro de I/O (Input/Output)
        return -EIO;
    }

    return count;
}

// TAREFA 5: Implementação da função para ler a temperatura
static ssize_t temp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    char *value_str = "-1";
    char *response_prefix = "RES GET_TEMP ";

    if (!smartlamp_device) return -ENODEV;

    if (smartlamp_transaction("GET_TEMP\n", usb_in_buffer) == 0) {
        // Encontra o início do valor (após o prefixo) e o copia para o buffer de saída
        if (strstr(usb_in_buffer, response_prefix)) {
            value_str = usb_in_buffer + strlen(response_prefix);
        }
    }
    printk(KERN_INFO "SmartLamp: Lendo valor da Temperatura: %s\n", value_str);
    return sprintf(buf, "%s", value_str); // Retorna o valor como string, incluindo o \n já vindo do dispositivo
}

// TAREFA 5: Implementação da função para ler a umidade
static ssize_t hum_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    char *value_str = "-1";
    char *response_prefix = "RES GET_HUM ";

    if (!smartlamp_device) return -ENODEV;

    if (smartlamp_transaction("GET_HUM\n", usb_in_buffer) == 0) {
        if (strstr(usb_in_buffer, response_prefix)) {
            value_str = usb_in_buffer + strlen(response_prefix);
        }
    }
    printk(KERN_INFO "SmartLamp: Lendo valor da Umidade: %s\n", value_str);
    return sprintf(buf, "%s", value_str);
}

// TAREFA 5: Implementação da função para ler o ldr
static ssize_t ldr_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    int value = -1;
    if (!smartlamp_device) return -ENODEV;

    if (smartlamp_transaction("GET_LDR\n", usb_in_buffer) == 0) {
        sscanf(usb_in_buffer, "RES GET_LDR %d", &value);
        printk(KERN_INFO "SmartLamp: Lendo valor do LDR: %d\n", value);
    }
    return sprintf(buf, "%d\n", value);
}