ARG DOCKER_TAG=latest
FROM espressif/idf:${DOCKER_TAG}

ENV LC_ALL=C.UTF-8 \
    LANG=C.UTF-8

RUN apt-get update -y && apt-get install -y bash-completion sudo udev vim \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN usermod -a -G dialout,plugdev ubuntu \
    && echo "source /opt/esp/idf/export.sh > /dev/null 2>&1" >> /home/ubuntu/.bashrc \
    && echo "ubuntu ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers \
    && touch /home/ubuntu/.sudo_as_admin_successful \
    && chown -R ubuntu /opt/esp/python_env

ENTRYPOINT [ "/opt/esp/entrypoint.sh" ]
