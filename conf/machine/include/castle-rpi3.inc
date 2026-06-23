# Castle gimbal Raspberry Pi 3 board settings
ENABLE_I2C = "1"
ENABLE_UART = "1"

RPI_EXTRA_CONFIG += "\
dtparam=i2c_arm=on\n\
dtparam=i2c1=on\n\
dtoverlay=miniuart-bt\n\
uart_2ndstage=1\n\
dtoverlay=pwm-2chan\n\
camera_auto_detect=1\n\
start_x=1\n\
gpu_mem=128\n\
"

KERNEL_MODULE_AUTOLOAD += "i2c-dev pwm-bcm2835"
RPI_KERNEL_DEVICETREE_OVERLAYS:append = " overlays/pwm-2chan.dtbo"
