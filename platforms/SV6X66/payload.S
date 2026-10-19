.section .stub_payload, "a", @progbits
.align 2
.global __stub_payload_start
.global __stub_payload_end

__stub_payload_start:
#ifndef SV6X66_PAYLOAD_PATH
#define SV6X66_PAYLOAD_PATH "build/SV6X66/SV6X66_stub_code.bin"
#endif
	.incbin SV6X66_PAYLOAD_PATH
__stub_payload_end:

