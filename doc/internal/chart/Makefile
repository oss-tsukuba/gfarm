TARGETS = gfmd-thread-dependency.png

.SUFFIXES: .png .dot

.dot.png:
	dot -Tpng -o $@ $*.dot

all: $(TARGETS)

clean:
	-rm -f $(TARGETS)
