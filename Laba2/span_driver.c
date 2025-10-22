#include <linux/module.h>          // Заголовочный файл для работы с модулями ядра
#include <linux/kernel.h>          // Заголовочный файл для функций ядра (printk и др.)
#include <linux/netdevice.h>       // Заголовочный файл для работы с сетевыми устройствами
#include <linux/skbuff.h>          // Заголовочный файл для работы с сетевыми пакетами (sk_buff)
#include <linux/if_ether.h>        // Заголовочный файл для Ethernet-заголовков
#include <linux/ip.h>              // Заголовочный файл для IP-заголовков
#include <linux/string.h>          // Заголовочный файл для строковых функций

// Структура для хранения информации о SPAN-интерфейсах
struct span_device {
    struct net_device *source_dev;    // Указатель на структуру исходного сетевого устройства
    struct net_device *mirror_dev;    // Указатель на структуру зеркального сетевого устройства
    char source_name[IFNAMSIZ];       // Буфер для хранения имени исходного интерфейса
    char mirror_name[IFNAMSIZ];       // Буфер для хранения имени зеркального интерфейса
    bool is_active;                   // Флаг, указывающий активен ли драйвер
};

static struct span_device span_dev;   // Статическая переменная для хранения данных SPAN

// Функция обработки входящих пакетов
static rx_handler_result_t span_rx_handler(struct sk_buff **pskb) {
    struct sk_buff *skb = *pskb;      // Получаем указатель на сетевой пакет
    struct sk_buff *skb_copy;         // Объявляем указатель для копии пакета

    // Проверяем активность драйвера и наличие зеркального интерфейса
    if (!span_dev.is_active || !span_dev.mirror_dev) {
        return RX_HANDLER_PASS;       // Если неактивен - пропускаем пакет дальше
    }

    // Проверяем, что пакет пришел с исходного интерфейса
    if (skb->dev != span_dev.source_dev) {
        return RX_HANDLER_PASS;       // Если не с исходного - пропускаем
    }

    // Проверяем, что зеркальный интерфейс активен
    if (!(span_dev.mirror_dev->flags & IFF_UP)) {
        printk(KERN_WARNING "SPAN: Зеркальный интерфейс %s не активен\n", span_dev.mirror_name);
        return RX_HANDLER_PASS;
    }

    // Клонируем SKB (буфер сокета) для отправки через зеркальный интерфейс
    skb_copy = skb_clone(skb, GFP_ATOMIC);  // Создаем копию пакета в атомарном контексте
    if (!skb_copy) {                  // Проверяем успешность клонирования
        printk(KERN_ERR "SPAN: Не удалось клонировать skb\n");  // Выводим ошибку в системный лог
        return RX_HANDLER_PASS;       // Пропускаем оригинальный пакет
    }

    // Устанавливаем устройство назначения для клонированного пакета
    skb_copy->dev = span_dev.mirror_dev;  // Назначаем зеркальный интерфейс как устройство назначения

    // Сбрасываем заголовки пакета
    skb_reset_mac_header(skb_copy);   // Сбрасываем MAC-заголовок
    skb_reset_network_header(skb_copy);  // Сбрасываем сетевой заголовок

    // Очищаем контрольную сумму
    skb_copy->ip_summed = CHECKSUM_NONE;  // Указываем, что контрольную сумму нужно пересчитать

    // Отправляем пакет через зеркальный интерфейс
    if (dev_queue_xmit(skb_copy) != NETDEV_TX_OK) {  // Пытаемся отправить пакет
        printk(KERN_ERR "SPAN: Ошибка отправки пакета через зеркальный интерфейс\n");  // Логируем ошибку
    } else {
        printk(KERN_DEBUG "SPAN: Пакет зеркалирован с %s на %s, размер: %u\n",  // Логируем успешное зеркалирование
               span_dev.source_name, span_dev.mirror_name, skb->len);           // с указанием интерфейсов и размера
    }

    return RX_HANDLER_PASS;           // Пропускаем оригинальный пакет для дальнейшей обработки
}

// Функция настройки SPAN-интерфейсов
static int setup_span_interfaces(const char *source, const char *mirror) {
    int ret = 0;                      // Переменная для хранения кода возврата

    // Получаем указатели на сетевые устройства по их именам
    span_dev.source_dev = dev_get_by_name(&init_net, source);  // Ищем исходное устройство
    span_dev.mirror_dev = dev_get_by_name(&init_net, mirror);  // Ищем зеркальное устройство

    if (!span_dev.source_dev) {       // Проверяем найден ли исходный интерфейс
        printk(KERN_ERR "SPAN: Исходный интерфейс %s не найден\n", source);  // Логируем ошибку
        return -ENODEV;               // Возвращаем код ошибки "устройство не найдено"
    }

    if (!span_dev.mirror_dev) {       // Проверяем найден ли зеркальный интерфейс
        printk(KERN_ERR "SPAN: Зеркальный интерфейс %s не найден\n", mirror);  // Логируем ошибку
        dev_put(span_dev.source_dev); // Освобождаем ссылку на исходное устройство
        return -ENODEV;               // Возвращаем код ошибки
    }

    // Копируем имена интерфейсов в структуру
    strncpy(span_dev.source_name, source, IFNAMSIZ - 1);  // Копируем имя исходного интерфейса
    span_dev.source_name[IFNAMSIZ - 1] = '\0';            // Гарантируем нулевое завершение строки
    strncpy(span_dev.mirror_name, mirror, IFNAMSIZ - 1);  // Копируем имя зеркального интерфейса
    span_dev.mirror_name[IFNAMSIZ - 1] = '\0';            // Гарантируем нулевое завершение строки

    // Проверяем, что интерфейсы активны
    if (!(span_dev.source_dev->flags & IFF_UP)) {
        printk(KERN_WARNING "SPAN: Исходный интерфейс %s не активен\n", source);
    }

    if (!(span_dev.mirror_dev->flags & IFF_UP)) {
        printk(KERN_WARNING "SPAN: Зеркальный интерфейс %s не активен\n", mirror);
    }

    // Регистрируем обработчик приема пакетов для исходного интерфейса
    ret = netdev_rx_handler_register(span_dev.source_dev, span_rx_handler, NULL);  // Регистрируем наш обработчик
    if (ret) {                        // Проверяем успешность регистрации
        printk(KERN_ERR "SPAN: Не удалось зарегистрировать RX handler: %d\n", ret);  // Логируем ошибку
        dev_put(span_dev.source_dev); // Освобождаем ссылку на исходное устройство
        dev_put(span_dev.mirror_dev); // Освобождаем ссылку на зеркальное устройство
        return ret;                   // Возвращаем код ошибки
    }

    span_dev.is_active = true;        // Устанавливаем флаг активности драйвера
    printk(KERN_INFO "SPAN: Драйвер запущен. Зеркалирование с %s на %s\n",  // Логируем успешный запуск
           span_dev.source_name, span_dev.mirror_name);                    // с указанием интерфейсов

    return 0;                         // Возвращаем успешный статус
}

// Функция очистки SPAN-драйвера
static void cleanup_span_interfaces(void) {
    if (span_dev.is_active && span_dev.source_dev) {  // Проверяем активен ли драйвер и есть ли исходное устройство
        netdev_rx_handler_unregister(span_dev.source_dev);  // Отменяем регистрацию обработчика
        span_dev.is_active = false;   // Сбрасываем флаг активности
    }

    if (span_dev.source_dev) {        // Проверяем существует ли исходное устройство
        dev_put(span_dev.source_dev); // Освобождаем ссылку на исходное устройство
    }

    if (span_dev.mirror_dev) {        // Проверяем существует ли зеркальное устройство
        dev_put(span_dev.mirror_dev); // Освобождаем ссылку на зеркальное устройство
    }

    printk(KERN_INFO "SPAN: Драйвер остановлен\n");  // Логируем остановку драйвера
}

// Параметры модуля (имена интерфейсов)
static char *source_if = "eth0";      // Параметр по умолчанию для исходного интерфейса
static char *mirror_if = "eth1";      // Параметр по умолчанию для зеркального интерфейса

module_param(source_if, charp, 0644); // Регистрируем параметр модуля для исходного интерфейса
MODULE_PARM_DESC(source_if, "Исходный сетевой интерфейс");  // Описание параметра
module_param(mirror_if, charp, 0644); // Регистрируем параметр модуля для зеркального интерфейса
MODULE_PARM_DESC(mirror_if, "Зеркальный сетевой интерфейс"); // Описание параметра

// Функция инициализации модуля
static int __init span_init(void) {
    printk(KERN_INFO "SPAN: Инициализация SPAN-драйвера\n");  // Логируем начало инициализации

    // Настраиваем SPAN-интерфейсы с переданными параметрами
    return setup_span_interfaces(source_if, mirror_if);  // Вызываем функцию настройки
}

// Функция выгрузки модуля
static void __exit span_exit(void) {
    cleanup_span_interfaces();        // Вызываем функцию очистки при выгрузке модуля
}

module_init(span_init);               // Указываем функцию инициализации модуля
module_exit(span_exit);               // Указываем функцию очистки модуля

MODULE_LICENSE("GPL");                // Указываем лицензию модуля
MODULE_AUTHOR("SPAN Driver Developer"); // Указываем автора модуля
MODULE_DESCRIPTION("SPAN драйвер для зеркалирования сетевых пакетов"); // Описание модуля
MODULE_VERSION("1.0");                // Версия модуля
