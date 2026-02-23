# Compile with all CYD pin defines as build flags
arduino-cli compile \
  --fqbn esp32:esp32:esp32:CPUFreq=240,FlashFreq=80,FlashSize=4M,UploadSpeed=115200 \
  esp32_cyd_hello/
