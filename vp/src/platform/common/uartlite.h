#ifndef RISCV_ISA_UARTLITE_H
#define RISCV_ISA_UARTLITE_H

#include <tlm_utils/simple_target_socket.h>

#include <cstring>
#include <iostream>
#include <systemc>

/* Minimal Xilinx AXI UART-lite model for the XiangShan AM (nexus-am) runtime:
 *   0x0 RX_FIFO (reads 0), 0x4 TX_FIFO (write prints the byte),
 *   0x8 STAT_REG (reads 0: TX never full, RX empty), 0xC CTRL_REG (ignored). */
struct UartLite : public sc_core::sc_module {
	tlm_utils::simple_target_socket<UartLite> tsock;

	UartLite(sc_core::sc_module_name) {
		tsock.register_b_transport(this, &UartLite::transport);
	}

	void transport(tlm::tlm_generic_payload &trans, sc_core::sc_time &delay) {
		uint64_t addr = trans.get_address();
		unsigned char *data = trans.get_data_ptr();
		unsigned len = trans.get_data_length();
		if (trans.get_command() == tlm::TLM_WRITE_COMMAND) {
			if (addr == 0x4) {
				std::cout << (char)data[0];
				std::cout.flush();
			}
		} else if (trans.get_command() == tlm::TLM_READ_COMMAND) {
			std::memset(data, 0, len);
		}
		trans.set_response_status(tlm::TLM_OK_RESPONSE);
		delay += sc_core::sc_time(1, sc_core::sc_time_unit::SC_US);
	}
};

#endif  // RISCV_ISA_UARTLITE_H
