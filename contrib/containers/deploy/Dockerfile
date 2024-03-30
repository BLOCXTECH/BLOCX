FROM phusion/baseimage:bionic-1.0.0
LABEL maintainer="BLOCX Developers <dev@blocx.org>"
LABEL description="Dockerised BLOCXCore, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /home/blocx

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} blocx && \
    useradd -u ${USER_ID} -g blocx -s /bin/bash -m -d /home/blocx blocx && \
    mkdir /home/blocx/.blocxcore && \
    chown ${USER_ID}:${GROUP_ID} -R /home/blocx

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER blocx

VOLUME ["/home/blocx"]

EXPOSE 12971 12972 19998 19999

WORKDIR /home/blocx
