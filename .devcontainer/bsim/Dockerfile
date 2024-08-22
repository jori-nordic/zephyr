FROM ghcr.io/zephyrproject-rtos/ci-repo-cache:v0.26.13.20240601

# Install gdbserver for enabling GUI debugging in VSCode
RUN apt-get -y update && \
	apt-get -y upgrade && \
	apt-get install --no-install-recommends -y \
	gdbserver

RUN usermod -s /usr/bin/bash user

USER user

CMD /usr/bin/bash
