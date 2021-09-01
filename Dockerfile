FROM ubuntu:18.04

RUN apt-get update && apt-get install -y git wget libncurses-dev flex bison gperf \
  python python-pip python-setuptools python-serial python-click \
  python-cryptography python-future python-pyparsing \
  python-pyelftools cmake ninja-build ccache libusb-1.0

WORKDIR /workspace

# Download and checkout known good esp-idf commit
RUN git clone https://github.com/espressif/esp-idf.git
RUN cd esp-idf && git checkout afbe1ba8789bda2937a65fa58217c843d80c255b && git submodule update --init --recursive
RUN cd esp-idf && git checkout 91b421c35f2c3513ac62c090586381e9bcfb06ff tools/cmake/utilities.cmake

# Download GCC 5.2.0
RUN wget -O - https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-1.22.0-80-g6c4433a-5.2.0.tar.gz | tar -xzf -

# Setup PATH to use esp-idf and gcc-5.2.0
RUN echo export PATH=\$PATH:$PWD/xtensa-esp32-elf/bin >> /root/.bashrc &&\
    echo export PATH=\$PATH:$PWD/esp-idf/tools >> /root/.bashrc &&\
    echo export IDF_PATH=$PWD/esp-idf >> /root/.bashrc

# OPTIONAL: Install some text editor
#RUN apt-get update && apt-get install -y vim
#RUN apt-get update && apt-get install -y nano
#RUN apt-get update && apt-get install -y emacs

WORKDIR /workspace/squeezelite-esp32
CMD ["bash"]
