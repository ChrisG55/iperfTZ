# SPDX-License-Identifier: GPL-3.0-or-later
export V?=0

OUTPUT_DIR := $(CURDIR)/out

# If _HOST or _TA specific compilers are not specified, then use CROSS_COMPILE
CA_CROSS_COMPILE ?= $(CROSS_COMPILE)
SERVER_CROSS_COMPILE ?= $(CROSS_COMPILE)
TA_CROSS_COMPILE ?= $(CROSS_COMPILE)

PHONY := all
all: prepare-for-rootfs

PHONY += all_iperfTZ
all_iperfTZ:
	$(MAKE) -C ca CROSS_COMPILE="$(CA_CROSS_COMPILE)" -rR
	$(MAKE) -C server CROSS_COMPILE="$(SERVER_CROSS_COMPILE)" -rR
	$(MAKE) -C ta CROSS_COMPILE="$(TA_CROSS_COMPILE)" LDFLAGS=""

PHONY += clean
clean: prepare-for-rootfs-clean
	$(MAKE) -C ca clean
	$(MAKE) -C server clean
	$(MAKE) -C ta clean

PHONY += prepare-for-rootfs
prepare-for-rootfs: all_iperfTZ
	@echo "Copying CA and TA binaries to $(OUTPUT_DIR)..."
	@mkdir -p $(OUTPUT_DIR)
	@mkdir -p $(OUTPUT_DIR)/ca
	@mkdir -p $(OUTPUT_DIR)/server
	@mkdir -p $(OUTPUT_DIR)/ta
	if [ -e ca/iperfTZ-ca ]; then \
		cp -p ca/iperfTZ-ca $(OUTPUT_DIR)/ca/; \
	fi; \
	if [ -e server/iperfTZ ]; then \
		cp -p server/iperfTZ $(OUTPUT_DIR)/server/; \
	fi; \
	cp -pr ta/*.ta $(OUTPUT_DIR)/ta/; \

PHONY += prepare-for-rootfs-clean
prepare-for-rootfs-clean:
	@rm -rf $(OUTPUT_DIR)/ca
	@rm -rf $(OUTPUT_DIR)/server
	@rm -rf $(OUTPUT_DIR)/ta
	@rmdir $(OUTPUT_DIR) || test ! -e $(OUTPUT_DIR)

.PHONY: $(PHONY)
