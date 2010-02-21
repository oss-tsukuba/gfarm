TARGETS = gfmd-thread-dependency.png

.SUFFIXES: .png .dot

.dot.png:
	dot -Tpng -o $@ $*.dot

all: $(TARGETS)

dep_libgfarm.png: dep_libgfarm.dot

clean:
	-rm -f $(TARGETS)
