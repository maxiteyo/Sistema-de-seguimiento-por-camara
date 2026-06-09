#!/bin/bash
set -e

echo "=== Instalador para Raspberry Pi (Buster) ==="

# Fix repositorios viejos
echo "[1/3] Arreglando repositorios..."
sudo sed -i 's|raspbian.raspberrypi.org|archive.raspbian.org|g' /etc/apt/sources.list

sudo apt-get -o Acquire::AllowInsecureRepositories=true \
             -o Acquire::AllowDowngradeToInsecureRepositories=true update 2>/dev/null

# Instalar librería que necesita OpenCV
echo "[2/3] Instalando dependencias..."
sudo apt-get install -y --allow-unauthenticated libatlas3-base 2>/dev/null || {
    echo "Descargando libatlas3-base manualmente..."
    wget -O /tmp/libatlas.deb \
      http://archive.debian.org/debian/pool/main/a/atlas/libatlas3-base_3.10.3-8_armhf.deb
    sudo dpkg -i /tmp/libatlas.deb
}

# Instalar pigpio
sudo apt-get install -y --allow-unauthenticated pigpio 2>/dev/null || true

# Dependencias Python
echo "[3/3] Instalando paquetes Python..."
python3 -m venv .venv 2>/dev/null || true
source .venv/bin/activate
pip install --index-url https://www.piwheels.org/simple \
  opencv-python==4.5.5.64 numpy posix-ipc

# Compilar C
echo "Compilando controller.c..."
gcc controller.c -o controller -lpthread -lrt -lm || \
gcc controller.c -o controller -lpthread -lrt -lm -lpigpio

echo ""
echo "=== Instalación completa ==="
echo "Para ejecutar:"
echo "  Terminal 1: sudo ./controller"
echo "  Terminal 2: python3 colorv4.py"
