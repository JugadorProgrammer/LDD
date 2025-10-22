#!/bin/bash

# Создаем виртуальные Ethernet интерфейсы для тестирования
echo "Настройка тестовых интерфейсов..."  # Выводим сообщение о начале настройки

# Удаляем старые интерфейсы если существуют
sudo ip link delete eth0 2>/dev/null || true  # Удаляем eth0 если существует, игнорируем ошибки
sudo ip link delete eth1 2>/dev/null || true  # Удаляем eth1 если существует, игнорируем ошибки

# Создаем пару виртуальных Ethernet интерфейсов
sudo ip link add eth0 type veth peer name eth1  # Создаем связанную пару виртуальных интерфейсов

# Настраиваем интерфейсы
sudo ip link set eth0 up              # Активируем интерфейс eth0
sudo ip link set eth1 up              # Активируем интерфейс eth1

# Назначаем IP-адреса
sudo ip addr add 192.168.1.10/24 dev eth0  # Назначаем IP-адрес для eth0
sudo ip addr add 192.168.1.11/24 dev eth1  # Назначаем IP-адрес для eth1

# Включаем promiscuous mode для приема всех пакетов
sudo ip link set eth0 promisc on      # Включаем promiscuous mode для eth0
sudo ip link set eth1 promisc on      # Включаем promiscuous mode для eth1

# Показываем статус интерфейсов
echo "Статус интерфейсов:"            # Выводим заголовок
ip addr show eth0                     # Показываем информацию об eth0
ip addr show eth1                     # Показываем информацию об eth1

echo "Тестовые интерфейсы настроены:" # Выводим итоговое сообщение
echo "eth0: 192.168.1.10"             # Показываем настройки eth0
echo "eth1: 192.168.1.11"             # Показываем настройки eth1
echo "Promiscuous mode включен для обоих интерфейсов"  # Информируем о promiscuous mode