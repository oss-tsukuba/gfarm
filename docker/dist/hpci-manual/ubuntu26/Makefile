build:
	docker build -t hpci-ubuntu26 --build-arg USERNAME=${USER} .

run:
	docker run -it --device /dev/fuse --privileged --rm \
		-u ${USER} -w /home/${USER} hpci-ubuntu26

