#include <linux/module.h>    // Основные определения для модулей ядра
#include <linux/init.h>      // Макросы __init и __exit
#include <linux/kernel.h>    // Функции печати в ядро (printk, pr_info)
#include <linux/fs.h>        // Файловые операции, регистрация устройств
#include <linux/cdev.h>      // Структура cdev для символьных устройств
#include <linux/slab.h>      // Функции выделения памяти в ядре (kmalloc, kfree)
#include <linux/uaccess.h>   // Функции копирования между ядром и пользователем
#include <linux/wait.h>      // Очереди ожидания для синхронизации
#include <linux/sched.h>     // Определения структур процессов
#include <linux/mutex.h>     // Мьютексы для взаимного исключения
#include <linux/device/class.h> //for class_create/class_destroy
#include <linux/device.h> // for device_create/device_destroy

#include <linux/version.h> // for kenel version

// Имя устройства для регистрации в системе
#define DEVICE_NAME "my_rand"

struct node {

    long value;
    struct node* next;
};

// Структура данных для каждого устройства
struct rand{
    struct cdev cdev;           // Структура символьного устройства
    dev_t devno;                // Номер устройства (major + minor)
    struct node *entropy;       // Указатель на энтропию
    struct mutex lock;          // Мьютекс для защиты от гонок данных
};

// Массив структур устройств (два драйвера)
static struct rand device;
// Старший номер устройства (будет назначен динамически)
static int major_num = 0;
// Класс устройств для sysfs
static struct class *rand_class = NULL;

// Объявления функций файловых операций (предварительные объявления)
static int rand_open(struct inode *inode, struct file *filp);
static int rand_release(struct inode *inode, struct file *filp);
static long rand_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

static ssize_t rand_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos){return -ENOSYS;}
static ssize_t rand_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos){return -ENOSYS;}

static long random_number(struct node *entropy)
{
    static long seed = 123;
    if(!entropy)
    {
        goto _exit;
    }

    struct node* next = entropy;
    while(!next)
    {
        seed = seed * 1103515245 + next->value;
        next = next->next;
    }

_exit:
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) & 0x7FFFFFFF;
}

// Структура файловых операций - связывает системные вызовы с нашими функциями
static struct file_operations rand_fops = {
    .owner = THIS_MODULE,    // Владелец модуля (предотвращает выгрузку при использовании)
    .open = rand_open,      // Вызывается при open() из пользовательского пространства
    .release = rand_release, // Вызывается при close() из пользовательского пространства
    .read = rand_read,      // Вызывается при read() из пользовательского пространства
    .write = rand_write,    // Вызывается при write() из пользовательского пространства
    .unlocked_ioctl = rand_ioctl
};

// Функция открытия устройства
static int rand_open(struct inode *inode, struct file *filp)
{
    struct rand *dev; // Указатель на нашу структуру устройства
    int minor = iminor(inode); // Получаем minor номер из inode

    // Проверяем, что minor номер в допустимом диапазоне
    if (minor >= 1) {
        return -ENODEV; // Возвращаем ошибку "Устройство не найдено"
    }

    // Получаем указатель на структуру устройства по minor номеру
    dev = &device;
    // Сохраняем указатель в private_data для использования в других функциях
    filp->private_data = dev;

    // Выводим информационное сообщение в журнал ядра
    pr_info("rand: Device %d opened\n", minor);
    return 0; // Успешное завершение
}

// Функция закрытия устройства
static int rand_release(struct inode *inode, struct file *filp)
{
    // Просто сообщаем о закрытии устройства
    pr_info("rand: Device %d closed\n", iminor(inode));
    return 0; // Успешное завершение
}

// Функция инициализации модуля (вызывается при загрузке)
static int __init rand_init(void)
{
    int err;           // Счетчик и переменная для ошибок
    dev_t dev_num = 0;    // Номер устройства

    // Запрашиваем динамическое выделение диапазона номеров устройств
    err = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (err < 0) {
        pr_err("rand: Failed to allocate device numbers\n");
        return err; // Возвращаем ошибку
    }
    // Сохраняем старший номер из выделенного диапазона
    major_num = MAJOR(dev_num);

// Создаем класс устройств для автоматического создания узлов в /dev

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    rand_class = class_create(DEVICE_NAME);
#else
    scull_class = class_create(THIS_MODULE, DEVICE_NAME);
#endif

    if (IS_ERR(rand_class)) {
        pr_err("rand: Failed to create device class\n");
        err = PTR_ERR(rand_class); // Получаем код ошибки
        goto fail_class; // Переходим к обработке ошибки
    }

    struct rand *dev = &device; // Текущее устройство

    // Инициализируем мьютекс для синхронизации
    mutex_init(&dev->lock);

    // Создаем полный номер устройства (major + minor)
    dev->devno = MKDEV(major_num, 0);

    // Инициализируем структуру cdev и связываем с файловыми операциями
    cdev_init(&dev->cdev, &rand_fops);
    dev->cdev.owner = THIS_MODULE; // Устанавливаем владельца

    // Добавляем устройство в систему
    err = cdev_add(&dev->cdev, dev->devno, 1);
    if (err) {
        pr_err("rand: Error %d adding device\n", err);
        goto fail_device; // Переходим к обработке ошибки
    }

    // Создаем устройство в /dev через sysfs
    // Автоматически создается /dev/rand
    device_create(rand_class, NULL, dev->devno, NULL, "rand");

    // Сообщаем об успешном создании устройства
    pr_info("rand: Device /dev/rand created\n");

    // Финальное сообщение об успешной загрузке модуля
    pr_info("rand: Module loaded successfully (major number = %d)\n", major_num);
    return 0; // Успешное завершение инициализации

// Метка обработки ошибок при создании устройств
fail_device:
    // Откат: удаляем
    // Удаляем устройство из /dev
    device_destroy(rand_class, device.devno);
    // Удаляем символьное устройство из системы
    cdev_del(&device.cdev);
    // Освобождаем память буфера
    kfree(device.entropy);
    // Удаляем класс устройств
    class_destroy(rand_class);

// Метка обработки ошибок при создании класса
fail_class:
    // Освобождаем выделенные номера устройств
    unregister_chrdev_region(dev_num, 1);
    return err; // Возвращаем код ошибки
}

// Функция выгрузки модуля (вызывается при удалении)
static void __exit rand_exit(void)
{
    // Удаляем устройство из /dev
    device_destroy(rand_class, device.devno);
    // Удаляем символьное устройство из системы
    cdev_del(&device.cdev);
    // Освобождаем память буфера
    kfree(device.entropy);

    // Удаляем класс устройств
    class_destroy(rand_class);
    // Освобождаем номера устройств
    unregister_chrdev_region(MKDEV(major_num, 0), 1);

    // Сообщение о успешной выгрузке модуля
    pr_info("rand: Module unloaded\n");
}

static long rand_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    void __user *user_arg = (void __user *)arg;
    struct rand *dev = filp->private_data; // Получаем наше устройство
    int retval = 0;

    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    switch (cmd) {
    case 0: // Команда для получения рандомного числа
    {
        long value = random_number(dev->entropy);
        pr_info("rand: value = %ld\n", value);
        if (copy_to_user(user_arg, &value, sizeof(long))) {
            retval = -EFAULT;
        }
        break;
    }
    case 1: // Команда для добавления энтропии
    {
        long value;
        if (copy_from_user(&value, user_arg, sizeof(long))) {
            retval = -EFAULT;
            break;
        }

        if(!dev->entropy)
        {
            // Выделяем память под энтропию в пространстве ядра
            dev->entropy = kmalloc(sizeof(struct node), GFP_KERNEL);
            if (!dev->entropy) {
                pr_err("rand: Failed to allocate buffer for device\n");
                retval = -ENOMEM; // Ошибка "Недостаточно памяти"
                break;
            }

            dev->entropy->next = 0;
            dev->entropy->value = value;
            break;
        }

        struct node* next = dev->entropy;
        while(next->next != 0)
        {
            next = next->next;
        }

        // Выделяем память под энтропию в пространстве ядра
        next->next = kmalloc(sizeof(struct node), GFP_KERNEL);
        if (!next->next) {
            pr_err("rand: Failed to allocate buffer for device\n");
            retval = -ENOMEM; // Ошибка "Недостаточно памяти"
            break;
        }

        next->next->next= 0;
        next->next->value = value;

        pr_info("rand: new value is %ld\n", value);
        break;
    }
    default:
        retval = -ENOTTY;
    }

    mutex_unlock(&dev->lock);
    return retval;
}

// Указываем функции инициализации и очистки
module_init(rand_init); // Функция scull_init будет вызвана при загрузке модуля
module_exit(rand_exit); // Функция scull_exit будет вызвана при выгрузке модуля

// Информация о модуле
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Илья Хомченков");
MODULE_DESCRIPTION("Rand driver"); // Описание
