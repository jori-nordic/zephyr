FROM ghcr.io/zephyrproject-rtos/ci-repo-cache:v0.26.13.20240601

# RUN addgroup --gid 1000 localuser && adduser --uid 1000 --ingroup localuser localuser
RUN usermod -s /usr/bin/bash user

# Such meaning, wow
USER user

ENTRYPOINT /usr/bin/bash
