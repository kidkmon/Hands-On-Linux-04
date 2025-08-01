#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/delay.h>

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
    } else {
        printk(KERN_INFO "SmartLamp: UART do CP210x ativada com sucesso.\n");
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

    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    kfree(usb_in_buffer);
    kfree(usb_out_buffer);
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
    int val;
    while (retries-- > 0) {
        ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), usb_in_buffer, usb_max_size - 1, &actual_size, 1000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados (tentativa %d). Codigo: %d\n", retries, ret);
            continue;
        }
        usb_in_buffer[actual_size] = '\0';
        if (sscanf(usb_in_buffer, "RES GET_LDR %d", &val) == 1) {
            return val;
        }
    }
    return -1;
}