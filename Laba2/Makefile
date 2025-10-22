obj-m += span_driver.o               # Указываем объектные файлы для сборки модуля

KDIR := /lib/modules/$(shell uname -r)/build  # Определяем путь к исходникам ядра текущей системы
PWD := $(shell pwd)                  # Получаем текущую директорию

all:                                 # Цель для сборки всего
	$(MAKE) -C $(KDIR) M=$(PWD) modules  # Вызываем make в директории ядра для сборки модуля

clean:                               # Цель для очистки
	$(MAKE) -C $(KDIR) M=$(PWD) clean    # Вызываем очистку в директории ядра

install:                             # Цель для установки модуля
	sudo insmod span_driver.ko source_if=eth0 mirror_if=eth1  # Загружаем модуль с параметрами

uninstall:                           # Цель для удаления модуля
	sudo rmmod span_driver              # Выгружаем модуль из ядра