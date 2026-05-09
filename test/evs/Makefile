CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -O2 -g -Wall -Wextra -Wpedantic -std=c11
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wpedantic -std=c++17 -DEVS_HOST_BUILD=1

SRC_MCU = ../../src/EVS/mcu
SRC_AP  = ../../src/EVS/ap
SRC_COM = ../../src/EVS/common

BIN_DIR = build

ALL_BINS = $(BIN_DIR)/test_evs_fsm \
           $(BIN_DIR)/test_camera_arbiter \
           $(BIN_DIR)/test_cutover_planner \
           $(BIN_DIR)/test_handover

.PHONY: all run clean
all: $(ALL_BINS)

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/test_evs_fsm: test_evs_fsm.c $(SRC_MCU)/evs_main.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -I$(SRC_COM) -o $@ $^

$(BIN_DIR)/test_camera_arbiter: test_camera_arbiter.cpp $(SRC_AP)/camera_arbiter.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -pthread -o $@ $^

$(BIN_DIR)/test_cutover_planner: test_cutover_planner.cpp $(SRC_AP)/cutover_planner.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BIN_DIR)/handover_agent.o: $(SRC_MCU)/handover_agent.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN_DIR)/test_handover: test_handover.cpp \
                          $(SRC_AP)/ap_handover_agent.cpp \
                          $(BIN_DIR)/handover_agent.o | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

run: all
	@set -e; for b in $(ALL_BINS); do echo "==> $$b"; $$b; done

clean:
	rm -rf $(BIN_DIR)
