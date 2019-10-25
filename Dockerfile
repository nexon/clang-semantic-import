FROM ubuntu:bionic
MAINTAINER <alberto.lagos@gmail.com>

# Install packages.
RUN apt-get update  -y \
 && apt-get install -y git cmake vim make wget gnupg libz-dev \
    apt-utils gcc g++ openssh-server build-essential gdb gdbserver rsync

# Get LLVM apt repositories.
RUN wget -O - 'http://apt.llvm.org/llvm-snapshot.gpg.key' | apt-key add - \
 && echo 'deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main' \
    >> /etc/apt/sources.list \
 && echo 'deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main' \
    >> /etc/apt/sources.list

# Install clang.
RUN apt-get update -y && apt-get install -y \
  llvm-7 llvm-7-dev clang-7 python-clang-7 clang-tools-7 \
  libclang-7-dev libclang-common-7-dev python3-pip
 
ENV C clang-7
ENV CXX clang++-7
ENV PATH /usr/lib/llvm-7/bin/:${PATH}

# Symbolically link for convenience
RUN ln -s /usr/lib/llvm-7/ /llvm

# These volumes should be mounted on the host.
VOLUME /home/
WORKDIR /home