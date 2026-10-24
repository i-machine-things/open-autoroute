# Build and install the plugin into a Flatpak OpenCPN (see #54). Modeled on radar_pi's flatpak/Makefile.
PLUGIN = org.opencpn.OpenCPN.Plugin.openautoroute

all: build

build:
	flatpak-builder --force-clean --keep-build-dirs app $(PLUGIN).yaml

install:
	flatpak build-export repo app master
	flatpak install --user --reinstall -y $(CURDIR)/repo $(PLUGIN)

uninstall:
	flatpak uninstall --user -y $(PLUGIN)

clean:
	rm -rf app repo .flatpak-builder

.PHONY: all build install uninstall clean
