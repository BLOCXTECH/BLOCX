FROM phusion/baseimage:bionic-1.0.0
LABEL maintainer="BLOCX Developers <dev@blocx.org>"
LABEL description="Dockerised BLOCX, built from Travis"

ARG USER_ID
ARG GROUP_ID

ENV HOME /blocx

# add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} blocx
RUN useradd -u ${USER_ID} -g blocx -s /bin/bash -m -d /blocx blocx
RUN mkdir /blocx/.blocx
RUN chown blocx:blocx -R /blocx

COPY bin/* /usr/local/bin/

RUN chmod a+x /usr/local/bin/*

USER blocx

VOLUME ["/blocx"]

EXPOSE 12971 9999 22971 22972

WORKDIR /blocx
