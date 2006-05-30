check test:
	@echo ""
	@./regress.sh
	@echo ""

loop:
	@loop=1; fail=0; \
	while :; do \
	echo "############## loop = $$loop (fail = $$fail) #################";\
	if ./regress.sh; then :; else \
		echo "FAILURE @ loop $$loop"; \
		mv log log.$$loop; \
		fail=`expr $$fail + 1`; \
	fi; \
	loop=`expr $$loop + 1`; \
	done
	

