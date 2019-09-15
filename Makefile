
# disabled (not compiling on 5.0+): misc-modules sbull snull sculld scullp scullv short shortprint simple tty 
SUBDIRS =  misc-progs \
           skull scull scullc \
	   pci usb lddbus

all: subdirs

subdirs:
	for n in $(SUBDIRS); do $(MAKE) -C $$n || exit 1; done

clean:
	for n in $(SUBDIRS); do $(MAKE) -C $$n clean; done
