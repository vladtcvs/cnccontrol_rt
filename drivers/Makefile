DRIVERS := 

all: drvs

ifdef CONFIG_ETHERNET_DEVICE_ENC28J60
DRIVERS += build_ethernet_enc28j60

build_ethernet_enc28j60: 
	$(MAKE) -C ethernet_enc28j60/
endif

drvs: $(DRIVERS)

clean:
	$(MAKE) -C ethernet_enc28j60/ clean

