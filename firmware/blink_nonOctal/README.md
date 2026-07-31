Použil jsem tyto dva příkazy:


ls -la /dev/ttyACM* /dev/ttyUSB*
— zjištění, na jakém portu je deska připojená (vyšlo /dev/ttyACM0, nativní USB; /dev/ttyUSB* nebyl přítomen).


cd /home/pe/Documents/repos/ieap_motor/firmware/blink_nonOctal
pio run -t upload --upload-port /dev/ttyACM0
— standardní PlatformIO build + upload (přeloží blink_nonoctal env a nahraje přes zabudovaný esptool 4.5.1, který v platformio.ini komentář zmiňuje jako nefunkční jen pro desku esp32stepper, ne pro tenhle devkit — zde prošel bez problémů).





---------------nebo--------------------



Použil jsem:


cd firmware/blink_nonOctal
pio run                                          # build bez uploadu

# nový esptool z venv (5.3.1, stejný, co používá flash.sh pro octal desku)
~/venv-esptool/bin/python -m esptool \
    --port /dev/ttyACM0 --baud 460800 \
    --before default-reset --connect-attempts 5 \
    write-flash --flash-mode keep --flash-freq keep --flash-size keep \
    0x0     .pio/build/blink_nonoctal/bootloader.bin \
    0x8000  .pio/build/blink_nonoctal/partitions.bin \
    0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
    0x10000 .pio/build/blink_nonoctal/firmware.bin
Jedna odchylka od flash.sh: --before usb-reset (co používá flash.sh pro esp32stepper) na tomhle devkitu selhalo s "No serial data received". Musel jsem přepnout na --before default-reset — tenhle devkit má klasický auto-reset přes RTS/DTR, ne přes signalizaci nativního USB-Serial/JTAG periferie.