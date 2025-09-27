CFLAGS  += -Wall -fPIC -I. -MMD
LDFLAGS += -L. -Wl,--hash-style=gnu

# Назви файлів
LIB_NAME    = misc
SRC         = shmem.c utils.c
OBJ         = $(SRC:.c=.o)
HDRS        = shmem.h utils.h crsf_protocol.h

# Назви бібліотек
STATIC_LIB  = lib$(LIB_NAME).a
SHARED_LIB  = lib$(LIB_NAME).so

# Каталоги встановлення
INSTALL_DIR ?= target
LIB_INSTALL_DIR = $(INSTALL_DIR)/usr/lib
INC_INSTALL_DIR = $(INSTALL_DIR)/usr/include/libmisc

CRC8_TABLE = 1
ifeq ($(CRC8_TABLE), 1)
CFLAGS += -DCRC8_TABLE
endif

DEPS = $(OBJ:.o=.d)

.PHONY: all clean install
-include $(DEPS)

all: $(STATIC_LIB) $(SHARED_LIB)

# -fPIC (Position Independent Code) needs for dynamic libraries
%.o: %.c mylib.h
	$(CC) $(CFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJ)
	$(AR) rcs $@ $^

$(SHARED_LIB): $(OBJ)
	$(CC) -shared $(LDFLAGS) -o $@ $^

# ===============================================
# Install for Yocto (do_install)
# ===============================================
install: all
	@echo "Install libraries and headers to $(INSTALL_DIR)"
	@echo Creating directories
	mkdir -p $(LIB_INSTALL_DIR)
	mkdir -p $(INC_INSTALL_DIR)
	@echo Install Dynamic library
	install -m 755 $(SHARED_LIB) $(LIB_INSTALL_DIR)
	@echo Install static library
	install -m 644 $(STATIC_LIB) $(LIB_INSTALL_DIR)
	@echo Install header file
	for h in ${HDRS} ; do install -m 644 $$h $(INC_INSTALL_DIR) ; done

clean:
	-rm -f $(OBJ) $(STATIC_LIB) $(SHARED_LIB)
	-rm *.d
	-rm -rf $(INSTALL_DIR)
