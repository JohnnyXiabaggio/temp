CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O0 -g \
           -I../src/Common -I../src/PduR -I../src/Safety -I../src/OS \
           -I../src/Security -I../src/App

SRC = ../src/PduR/PduR.c ../src/PduR/PduR_Cfg.c \
      ../src/Safety/SafetyMgr.c ../src/Safety/SecOC_Gate.c \
      ../src/Safety/DeadlineMonitor.c \
      ../src/Security/IdsM.c ../src/Security/AnomalyDetector.c \
      test_pdur_routing.c

LIN_SRC = ../src/App/BodyRouting.c ../src/App/PartConfig.c \
          test_lin_can_routing.c

test_pdur_routing: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

test_lin_can_routing: $(LIN_SRC)
	$(CC) $(CFLAGS) -o $@ $(LIN_SRC)

run: test_pdur_routing test_lin_can_routing
	./test_pdur_routing
	./test_lin_can_routing

clean:
	rm -f test_pdur_routing test_lin_can_routing
