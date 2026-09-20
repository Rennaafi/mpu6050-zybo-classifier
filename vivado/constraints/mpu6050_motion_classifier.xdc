## Zybo Z7-10 constraints for this project's PS UART1 (EMIO) link to the ESP32.
## Extracted from Digilent's full Zybo-Z7-Master.xdc — see the README for
## the rest of the board's pin map if you need other peripherals.
##
## Pmod JC, pins 1-2 (top row). UART1's two EMIO-routed signals become
## top-level ports UART_1_0_txd / UART_1_0_rxd after Vivado's Connection
## Automation runs on the block design.

set_property -dict { PACKAGE_PIN V15   IOSTANDARD LVCMOS33 } [get_ports { UART_1_0_txd }]; #JC1 Sch=jc_p[1]
set_property -dict { PACKAGE_PIN W15   IOSTANDARD LVCMOS33 } [get_ports { UART_1_0_rxd }]; #JC2 Sch=jc_n[1]
