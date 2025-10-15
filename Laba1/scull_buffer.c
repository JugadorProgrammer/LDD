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
#define DEVICE_NAME "scull_buffer"
// Размер кольцевого буфера в байтах
#define BUFFER_SIZE 1024
// Количество создаваемых устройств (два драйвера)
#define NUM_DEVICES 2

// Структура данных для каждого устройства
struct scull_buffer {
    struct cdev cdev;           // Структура символьного устройства
    dev_t devno;                // Номер устройства (major + minor)
    char *buffer;               // Указатель на кольцевой буфер в памяти ядра
    int read_index;             // Индекс для чтения из буфера
    int write_index;            // Индекс для записи в буфер
    int data_size;              // Текущее количество данных в буфере
    struct mutex lock;          // Мьютекс для защиты от гонок данных
    wait_queue_head_t read_queue;   // Очередь ожидания для процессов чтения
    wait_queue_head_t write_queue;  // Очередь ожидания для процессов записи
};

// Массив структур устройств (два драйвера)
static struct scull_buffer devices[NUM_DEVICES];
// Старший номер устройства (будет назначен динамически)
static int major_num = 0;
// Класс устройств для sysfs
static struct class *scull_class = NULL;

// Объявления функций файловых операций (предварительные объявления)
static int scull_open(struct inode *inode, struct file *filp);
static int scull_release(struct inode *inode, struct file *filp);
static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos);
static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

// Структура файловых операций - связывает системные вызовы с нашими функциями
static struct file_operations scull_fops = {
    .owner = THIS_MODULE,    // Владелец модуля (предотвращает выгрузку при использовании)
    .open = scull_open,      // Вызывается при open() из пользовательского пространства
    .release = scull_release, // Вызывается при close() из пользовательского пространства
    .read = scull_read,      // Вызывается при read() из пользовательского пространства
    .write = scull_write,    // Вызывается при write() из пользовательского пространства
    .unlocked_ioctl = scull_ioctl
};

// Функция открытия устройства
static int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_buffer *dev; // Указатель на нашу структуру устройства
    int minor = iminor(inode); // Получаем minor номер из inode

    // Проверяем, что minor номер в допустимом диапазоне
    if (minor >= NUM_DEVICES) {
        return -ENODEV; // Возвращаем ошибку "Устройство не найдено"
    }

    // Получаем указатель на структуру устройства по minor номеру
    dev = &devices[minor];
    // Сохраняем указатель в private_data для использования в других функциях
    filp->private_data = dev;

    // Выводим информационное сообщение в журнал ядра
    pr_info("scull_buffer: Device %d opened\n", minor);
    return 0; // Успешное завершение
}

// Функция закрытия устройства
static int scull_release(struct inode *inode, struct file *filp)
{
    // Просто сообщаем о закрытии устройства
    pr_info("scull_buffer: Device %d closed\n", iminor(inode));
    return 0; // Успешное завершение
}

// Функция чтения из устройства
static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_buffer *dev = filp->private_data; // Получаем наше устройство
    ssize_t retval = 0;          // Возвращаемое значение (количество прочитанных байт)
    int bytes_to_read;           // Сколько байт будем читать в этой операции
    int bytes_read_first_part;   // Сколько байт прочитаем из первой части буфера

    // Захватываем мьютекс. Если получен сигнал, возвращаем ошибку
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; // Процесс был прерван сигналом

    // Ждем, пока в буфере появятся данные для чтения
    while (dev->data_size == 0) {
        // Временно отпускаем мьютекс перед ожиданием
        mutex_unlock(&dev->lock);

        // Проверяем, открыто ли устройство в неблокирующем режиме
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN; // Возвращаем ошибку "Попробуйте снова"

        // Сообщаем, что процесс идет спать из-за пустого буфера
        pr_info("scull_buffer: Buffer empty, process %d (%s) going to sleep\n",
                current->pid, current->comm);

        // Усыпляем процесс в очереди чтения. Проснется когда data_size > 0
        // wait_event_interruptible проверяет условие после пробуждения
        if (wait_event_interruptible(dev->read_queue, (dev->data_size > 0)))
            return -ERESTARTSYS; // Было прерывание (например, Ctrl+C)

        // Проснулись, снова пытаемся захватить мьютекс
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
    }

    // Определяем, сколько байт можем прочитать (минимум из запрошенного и доступного)
    bytes_to_read = min(count, (size_t)dev->data_size);

    // Чтение может быть в две части из-за кольцевой структуры:
    // 1. От read_index до конца буфера
    // 2. С начала буфера (если нужно)
    bytes_read_first_part = min(bytes_to_read, BUFFER_SIZE - dev->read_index);

    // Копируем данные из ядра в пользовательское пространство (первая часть)
    if (copy_to_user(buf, dev->buffer + dev->read_index, bytes_read_first_part)) {
        retval = -EFAULT; // Ошибка копирования
        goto out; // Переходим к метке выхода
    }

    // Копируем данные из ядра в пользовательское пространство (вторая часть, если есть)
    if (bytes_to_read > bytes_read_first_part) {
        if (copy_to_user(buf + bytes_read_first_part, dev->buffer, 
                        bytes_to_read - bytes_read_first_part)) {
            retval = -EFAULT; // Ошибка копирования
            goto out; // Переходим к метке выхода
        }
    }

    // Обновляем индекс чтения с учетом кольцевой структуры
    dev->read_index = (dev->read_index + bytes_to_read) % BUFFER_SIZE;
    // Уменьшаем количество данных в буфере
    dev->data_size -= bytes_to_read;
    // Устанавливаем возвращаемое значение
    retval = bytes_to_read;

    // Информационное сообщение о успешном чтении
    pr_info("scull_buffer: Read %zu bytes from device %d. Data size: %d\n",
            retval, iminor(filp->f_path.dentry->d_inode), dev->data_size);

    // После чтения в буфере точно появилось свободное место
    // Будим все процессы, ждущие в очереди записи
    wake_up_interruptible(&dev->write_queue);

// Метка выхода из функции
out:
    // Всегда отпускаем мьютекс перед выходом
    mutex_unlock(&dev->lock);
    return retval; // Возвращаем результат операции
}

// Функция записи в устройство
static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_buffer *dev = filp->private_data; // Получаем наше устройство
    ssize_t retval = 0;          // Возвращаемое значение (количество записанных байт)
    int space_available;         // Свободное место в буфере
    int bytes_to_write;          // Сколько байт будем записывать в этой операции
    int bytes_write_first_part;  // Сколько байт запишем в первую часть буфера

    // Захватываем мьютекс. Если получен сигнал, возвращаем ошибку
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS; // Процесс был прерван сигналом

    // Вычисляем свободное место в буфере
    space_available = BUFFER_SIZE - dev->data_size;

    // Ждем, пока в буфере появится свободное место для записи
    while (space_available == 0) {
        // Временно отпускаем мьютекс перед ожиданием
        mutex_unlock(&dev->lock);

        // Проверяем, открыто ли устройство в неблокирующем режиме
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN; // Возвращаем ошибку "Попробуйте снова"
        // Сообщаем, что процесс идет спать из-за полного буфера
        pr_info("scull_buffer: Buffer full, process %d (%s) going to sleep\n",
                current->pid, current->comm);

        // Усыпляем процесс в очереди записи. Проснется когда появится место
        // Вычисляем space_available снова после пробуждения
        if (wait_event_interruptible(dev->write_queue, 
            (space_available = (BUFFER_SIZE - dev->data_size)) > 0))
            return -ERESTARTSYS; // Было прерывание

        // Проснулись, снова пытаемся захватить мьютекс
        if (mutex_lock_interruptible(&dev->lock))
            return -ERESTARTSYS;
    }

    // Определяем, сколько байт можем записать (минимум из запрошенного и доступного)
    bytes_to_write = min(count, (size_t)space_available);

    // Запись может быть в две части из-за кольцевой структуры:
    // 1. От write_index до конца буфера
    // 2. С начала буфера (если нужно)
    bytes_write_first_part = min(bytes_to_write, BUFFER_SIZE - dev->write_index);

    // Копируем данные из пользовательского пространства в ядро (первая часть)
    if (copy_from_user(dev->buffer + dev->write_index, buf, bytes_write_first_part)) {
        retval = -EFAULT; // Ошибка копирования
        goto out; // Переходим к метке выхода
    }

    // Копируем данные из пользовательского пространства в ядро (вторая часть, если есть)
    if (bytes_to_write > bytes_write_first_part) {
        if (copy_from_user(dev->buffer, buf + bytes_write_first_part, 
                          bytes_to_write - bytes_write_first_part)) {
            retval = -EFAULT; // Ошибка копирования
            goto out; // Переходим к метке выхода
        }
    }

    // Обновляем индекс записи с учетом кольцевой структуры
    dev->write_index = (dev->write_index + bytes_to_write) % BUFFER_SIZE;
    // Увеличиваем количество данных в буфере
    dev->data_size += bytes_to_write;
    // Устанавливаем возвращаемое значение
    retval = bytes_to_write;

    // Информационное сообщение о успешной записи
    pr_info("scull_buffer: Wrote %zu bytes to device %d. Data size: %d\n",
            retval, iminor(filp->f_path.dentry->d_inode), dev->data_size);

    // После записи в буфере точно появились новые данные
    // Будим все процессы, ждущие в очереди чтения
    wake_up_interruptible(&dev->read_queue);

// Метка выхода из функции
out:
    // Всегда отпускаем мьютекс перед выходом
    mutex_unlock(&dev->lock);
    return retval; // Возвращаем результат операции
}

// Функция инициализации модуля (вызывается при загрузке)
static int __init scull_init(void)
{
    int i, err;           // Счетчик и переменная для ошибок
    dev_t dev_num = 0;    // Номер устройства

    // Запрашиваем динамическое выделение диапазона номеров устройств
    // dev_num будет содержать первый номер, NUM_DEVICES - количество устройств
    err = alloc_chrdev_region(&dev_num, 0, NUM_DEVICES, DEVICE_NAME);
    if (err < 0) {
        pr_err("scull_buffer: Failed to allocate device numbers\n");
        return err; // Возвращаем ошибку
    }
    // Сохраняем старший номер из выделенного диапазона
    major_num = MAJOR(dev_num);

    // Создаем класс устройств для автоматического создания узлов в /dev
    // scull_class = class_create(THIS_MODULE, DEVICE_NAME);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    scull_class = class_create(DEVICE_NAME);
#else
    scull_class = class_create(THIS_MODULE, DEVICE_NAME);
#endif

    if (IS_ERR(scull_class)) {
        pr_err("scull_buffer: Failed to create device class\n");
        err = PTR_ERR(scull_class); // Получаем код ошибки
        goto fail_class; // Переходим к обработке ошибки
    }

    // Инициализируем каждое устройство в цикле
    for (i = 0; i < NUM_DEVICES; i++) {
        struct scull_buffer *dev = &devices[i]; // Текущее устройство

        // Выделяем память под кольцевой буфер в пространстве ядра
        dev->buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
        if (!dev->buffer) {
            pr_err("scull_buffer: Failed to allocate buffer for device %d\n", i);
            err = -ENOMEM; // Ошибка "Недостаточно памяти"
            goto fail_device; // Переходим к обработке ошибки
        }

        // Инициализируем мьютекс для синхронизации
        mutex_init(&dev->lock);
        // Инициализируем очереди ожидания для читателей и писателей
        init_waitqueue_head(&dev->read_queue);
        init_waitqueue_head(&dev->write_queue);

        // Инициализируем переменные буфера
        dev->read_index = 0;   // Начинаем чтение с начала
        dev->write_index = 0;  // Начинаем запись с начала
        dev->data_size = 0;    // Буфер initially пуст

        // Создаем полный номер устройства (major + minor)
        // minor = i (0, 1 для двух устройств)
        dev->devno = MKDEV(major_num, i);

        // Инициализируем структуру cdev и связываем с файловыми операциями
        cdev_init(&dev->cdev, &scull_fops);
        dev->cdev.owner = THIS_MODULE; // Устанавливаем владельца

        // Добавляем символьное устройство в систему
        err = cdev_add(&dev->cdev, dev->devno, 1);
        if (err) {
            pr_err("scull_buffer: Error %d adding device %d\n", err, i);
            kfree(dev->buffer); // Освобождаем память буфера
            goto fail_device; // Переходим к обработке ошибки
        }

        // Создаем устройство в /dev через sysfs
        // Автоматически создается /dev/scull_buffer0, /dev/scull_buffer1 и т.д.
        device_create(scull_class, NULL, dev->devno, NULL, "scull_buffer%d", i);

        // Сообщаем об успешном создании устройства
        pr_info("scull_buffer: Device /dev/scull_buffer%d created\n", i);
    }

    // Финальное сообщение об успешной загрузке модуля
    pr_info("scull_buffer: Module loaded successfully (major number = %d)\n", major_num);
    return 0; // Успешное завершение инициализации

// Метка обработки ошибок при создании устройств
fail_device:
    // Откат: удаляем все созданные устройства в обратном порядке
    while (--i >= 0) {
        // Удаляем устройство из /dev
        device_destroy(scull_class, devices[i].devno);
        // Удаляем символьное устройство из системы
        cdev_del(&devices[i].cdev);
        // Освобождаем память буфера
        kfree(devices[i].buffer);
    }
    // Удаляем класс устройств
    class_destroy(scull_class);

// Метка обработки ошибок при создании класса
fail_class:
    // Освобождаем выделенные номера устройств
    unregister_chrdev_region(dev_num, NUM_DEVICES);
    return err; // Возвращаем код ошибки
}

// Функция выгрузки модуля (вызывается при удалении)
static void __exit scull_exit(void)
{
    int i; // Счетчик

    // Удаляем все устройства в цикле
    for (i = 0; i < NUM_DEVICES; i++) {
        // Удаляем устройство из /dev
        device_destroy(scull_class, devices[i].devno);
        // Удаляем символьное устройство из системы
        cdev_del(&devices[i].cdev);
        // Освобождаем память буфера
        kfree(devices[i].buffer);
    }

    // Удаляем класс устройств
    class_destroy(scull_class);
    // Освобождаем номера устройств
    unregister_chrdev_region(MKDEV(major_num, 0), NUM_DEVICES);

    // Сообщение о успешной выгрузке модуля
    pr_info("scull_buffer: Module unloaded\n");
}

// Добавим ioctl для Process C, чтобы получать состояние буфера
static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct scull_buffer *dev = filp->private_data; // Получаем наше устройство
    int retval = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    switch (cmd) {
    case 0: // Команда для получения размера данных в буфере
        if (copy_to_user((int __user *)arg, &dev->data_size, sizeof(dev->data_size))) {
            retval = -EFAULT;
        }
        break;
    // Можно добавить другие команды, например, для чтения всего содержимого без извлечения
    default:
        retval = -ENOTTY;
    }

    mutex_unlock(&dev->lock);
    return retval;
}

// Указываем функции инициализации и очистки
module_init(scull_init); // Функция scull_init будет вызвана при загрузке модуля
module_exit(scull_exit); // Функция scull_exit будет вызвана при выгрузке модуля

// Информация о модуле
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Илья Хомченков");
MODULE_DESCRIPTION("Scull Driver with Circular Buffer and Locking"); // Описание
