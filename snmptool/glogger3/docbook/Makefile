DIR	= en ja

all:
	@for i in $(DIR); do \
	  (cd $$i; $(MAKE)); \
	done

install:
	@for i in $(DIR); do \
	  (cd $$i; $(MAKE) install); \
	done

clean:
	@for i in $(DIR); do \
	  (cd $$i; $(MAKE) clean); \
	done
