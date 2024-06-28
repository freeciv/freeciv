FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
# Set the maintainer label
LABEL maintainer="mekchedy@gmail.com"

#install needed dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    curl \
    make \
    git \
    autoconf \
    automake \
    autotools-dev \
    python3 \
    gettext \
    libtool \
    sqlite3 \
    libsqlite3-dev \
    libpq-dev \
    pkg-config \
    libcurl4-openssl-dev \
    libsdl2-ttf-dev \
    libjson-c-dev \
    libicu-dev \
    libsdl2-mixer-dev \
    libgtk-3-dev \
    qtbase5-dev

COPY bootstrap/missing .

# Clean up the package list to reduce image size
RUN apt-get clean && rm -rf /var/lib/apt/lists/*
