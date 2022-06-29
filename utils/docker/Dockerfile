FROM crops/poky:ubuntu-18.04
USER root

RUN apt-get install -y gcc git wget make libncurses-dev flex bison gperf python python-pip python-setuptools python-serial python-cryptography python-future python-pyparsing
RUN apt-get install -y libncurses5 libncurses5-dev libncursesw5 libncursesw5-dev

RUN mkdir -p /opt/esp32-toolchain
ADD https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz /opt/esp32-toolchain
RUN tar -C /opt/esp32-toolchain -xf /opt/esp32-toolchain/xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz

USER usersetup
