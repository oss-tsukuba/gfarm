build:
	docker build -t hpci-ubuntu20 --build-arg USERNAME=${USER} .

run:
	docker run -it --device /dev/fuse --privileged --rm \
		-u ${USER} -w /home/${USER} hpci-ubuntu20

