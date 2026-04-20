# Host-side test build
#
# Usage:
#   cd firmware/test
#   make
#   ./test_runner

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -I../main -Istubs

# cJSON from ESP-IDF
IDF_PATH ?= $(HOME)/.espressif/v5.4.4/esp-idf
CJSON_DIR = $(IDF_PATH)/components/json/cJSON
CXXFLAGS += -I$(CJSON_DIR)

SOURCES = test_main.cc stubs.cc ../main/state/state_parser.cc ../main/protocol/line_protocol.cc $(CJSON_DIR)/cJSON.c
TARGET = test_runner

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all test clean
