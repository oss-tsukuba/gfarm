build:
	docker build -t hpci-ubuntu22 --build-arg USERNAME=${USER} .

run:
	docker run -it --device /dev/fuse --privileged --rm \
		-u ${USER} -w /home/${USER} hpci-ubuntu22

