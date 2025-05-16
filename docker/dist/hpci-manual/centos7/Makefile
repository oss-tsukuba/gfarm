build:
	docker build -t hpci-centos7 --build-arg USERNAME=${USER} .

run:
	docker run -it --device /dev/fuse --privileged --rm \
		-u ${USER} -w /home/${USER} hpci-centos7

