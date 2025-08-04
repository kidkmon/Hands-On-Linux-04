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
static int  activate_cp210x_uart(void);
static int  usb_write_serial(const char* command);
static int  usb_read_serial(void);
// Adicionados protótipos para as funções do sysfs
static ssize_t led_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t led_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t temp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);
static ssize_t hum_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf);


// --- Definições do Sysfs (Adicionado) ---
static struct kobj_attribute led_attribute = __ATTR(led, 0664, led_show, led_store);

static struct attribute *attrs[] = {
    &led_attribute.attr,
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

// Função para enviar o comando de ativação para a UART do CP210x
// so funcionou assim, aparentemente o cp210x que eh o driver padrao faz isso automatico
static int activate_cp210x_uart(void) {
    int ret;
    ret = usb_control_msg(smartlamp_device, usb_sndctrlpipe(smartlamp_device, 0),
                          CP210X_IFC_ENABLE, 0x41, UART_ENABLE,
                          smartlamp_device->actconfig->interface[0]->altsetting[0].desc.bInterfaceNumber,
                          NULL, 0, 1000);
    if (ret < 0) {
        printk(KERN_ERR "SmartLamp: Falha ao ativar a UART do CP210x. Erro: %d\n", ret);
   // } else {
       // printk(KERN_INFO "SmartLamp: UART do CP210x ativada com sucesso.\n");
    }
    return ret;
}

// Executado quando o dispositivo é conectado na USB
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *usb_endpoint_in, *usb_endpoint_out;
    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    smartlamp_device = interface_to_usbdev(interface);

    // 1. ATIVA A UART DO CHIP
    if (activate_cp210x_uart() < 0) {
        return -EIO; // Se não conseguir ativar, não adianta continuar.
    }

    // 2. Encontra os endpoints e aloca os buffers
    if (usb_find_common_endpoints(interface->cur_altsetting, &usb_endpoint_in, &usb_endpoint_out, NULL, NULL)) {
        printk(KERN_ERR "SmartLamp: Endpoints nao encontrados\n"); return -EIO;
    }
    usb_max_size = usb_endpoint_maxp(usb_endpoint_in);
    usb_in = usb_endpoint_in->bEndpointAddress;
    usb_out = usb_endpoint_out->bEndpointAddress;
    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    // 3. Inicia a comunicação enviando o comando
    msleep(200); // Espera o dispositivo se estabilizar
    if (usb_write_serial("GET_LDR\n") == 0) {
        printk(KERN_INFO "SmartLamp: Comando 'GET_LDR' enviado.\n");
        msleep(100); // Espera o dispositivo processar e responder
        LDR_value = usb_read_serial();
    } else {
        printk(KERN_ERR "SmartLamp: Falha ao enviar comando GET_LDR.\n");
        LDR_value = -1;
    }

    // 4. Imprime o resultado final
    if (LDR_value >= 0) {
        printk(KERN_INFO "SmartLamp: SUCESSO! Valor do LDR lido: %d\n", LDR_value);
    } else {
        printk(KERN_WARNING "SmartLamp: Nao foi possivel ler o valor do LDR.\n");
    }

    // 5. Cria a interface sysfs (Adicionado)
    smartlamp_kobj = kobject_create_and_add("smartlamp", kernel_kobj);
    if (!smartlamp_kobj) {
        printk(KERN_ERR "SmartLamp: Falha ao criar kobject no sysfs\n");
        return -ENOMEM;
    }

    if (sysfs_create_group(smartlamp_kobj, &attr_group)) {
        printk(KERN_ERR "SmartLamp: Falha ao criar grupo de atributos no sysfs\n");
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

// Função para enviar um comando para o dispositivo
static int usb_write_serial(const char* command) {
    int ret, actual_size;
    int len = strlen(command);
    strncpy(usb_out_buffer, command, usb_max_size);
    ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out), usb_out_buffer, len, &actual_size, 1000);
    if (ret) {
        printk(KERN_ERR "SmartLamp: Erro ao enviar comando. Codigo: %d\n", ret);
        return ret;
    }
    return 0;
}

// Função para ler a resposta do dispositivo
static int usb_read_serial(void) {
    int ret, actual_size;
    int retries = 5;
    int val = -1;
    while (retries-- > 0) {
        ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), usb_in_buffer, usb_max_size - 1, &actual_size, 1000);
        if (ret == 0) { // Alterado para checar sucesso antes de continuar
            usb_in_buffer[actual_size] = '\0';
            if (sscanf(usb_in_buffer, "RES GET_LDR %d", &val) == 1) {
                return val;
            }
        }
        msleep(20); // Pausa antes de tentar novamente
    }
    printk(KERN_ERR "SmartLamp: Falha ao ler dados para GET_LDR. Erro: %d\n", ret);
    return -1;
}

// --- Funções do Sysfs (Adicionadas) ---
// Função chamada quando algo é escrito no arquivo /sys/kernel/smartlamp/led
static ssize_t led_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int new_value;
    char command[32];

    if (!smartlamp_device) return -ENODEV; // Verificação de segurança

    if (kstrtoint(buf, 10, &new_value) != 0) return -EINVAL;

    printk(KERN_INFO "SmartLamp: Alterando valor do LED para %d\n", new_value);

    if (activate_cp210x_uart() < 0) return -EIO;

    scnprintf(command, sizeof(command), "SET_LED %d\n", new_value);
    
    if (usb_write_serial(command) == 0) {
        // Consome a resposta de confirmação ("RES SET_LED 1") para não travar
        msleep(50);
        usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in),
                     usb_in_buffer, usb_max_size, NULL, 500);
    }
    
    return count;
}

// Função chamada quando o arquivo /sys/kernel/smartlamp/led é lido
static ssize_t led_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    int value = -1;
    int ret, actual_size;
    int retries = 5;

    if (!smartlamp_device) return -ENODEV; // Verificação de segurança

    if (activate_cp210x_uart() < 0) return sprintf(buf, "%d\n", -1);

    if (usb_write_serial("GET_LED\n") == 0) {
        msleep(100);
        while (retries-- > 0) {
            ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), 
                               usb_in_buffer, MAX_RECV_LINE - 1, &actual_size, 1000);
            if (ret == 0) {
                usb_in_buffer[actual_size] = '\0';
                sscanf(usb_in_buffer, "RES GET_LED %d", &value);
                printk(KERN_INFO "SmartLamp: Lendo valor do LED: %d\n", value);
                goto end_show;
            }
            msleep(20);
        }
        printk(KERN_ERR "SmartLamp: sysfs 'led_show' falhou ao ler resposta. Erro: %d\n", ret);
    }
end_show:
    return sprintf(buf, "%d\n", value);
}

// Função chamada quando o arquivo /sys/kernel/smartlamp/temp é lido
static ssize_t temp_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    int value = -1;
    int ret, actual_size;
    int retries = 5;

    if (!smartlamp_device) return -ENODEV; // Verificação de segurança

    if (activate_cp210x_uart() < 0) return sprintf(buf, "%d\n", -1);

    if (usb_write_serial("GET_TEMP\n") == 0) {
        msleep(100);
        while (retries-- > 0) {
            ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), 
                               usb_in_buffer, MAX_RECV_LINE - 1, &actual_size, 1000);
            if (ret == 0) {
                usb_in_buffer[actual_size] = '\0';
                sscanf(usb_in_buffer, "RES GET_TEMP %d", &value);
                printk(KERN_INFO "SmartLamp: Lendo valor da Temperatura: %d\n", value);
                goto end_show;
            }
            msleep(20);
        }
        printk(KERN_ERR "SmartLamp: sysfs 'temp_show' falhou ao ler resposta. Erro: %d\n", ret);
    }
end_show:
    return sprintf(buf, "%d\n", value);
}

// Função chamada quando o arquivo /sys/kernel/smartlamp/hum é lido
static ssize_t hum_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    int value = -1;
    int ret, actual_size;
    int retries = 5;

    if (!smartlamp_device) return -ENODEV; // Verificação de segurança

    if (activate_cp210x_uart() < 0) return sprintf(buf, "%d\n", -1);

    if (usb_write_serial("GET_HUM\n") == 0) {
        msleep(100);
        while (retries-- > 0) {
            ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), 
                               usb_in_buffer, MAX_RECV_LINE - 1, &actual_size, 1000);
            if (ret == 0) {
                usb_in_buffer[actual_size] = '\0';
                sscanf(usb_in_buffer, "RES GET_HUM %d", &value);
                printk(KERN_INFO "SmartLamp: Lendo valor da Umidade: %d\n", value);
                goto end_show;
            }
            msleep(20);
        }
        printk(KERN_ERR "SmartLamp: sysfs 'hum_show' falhou ao ler resposta. Erro: %d\n", ret);
    }
end_show:
    return sprintf(buf, "%d\n", value);
}