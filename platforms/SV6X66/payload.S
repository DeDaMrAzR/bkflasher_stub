.section .stub_payload, "a", @progbits
.align 2
.global __stub_payload_start
.global __stub_payload_end

__stub_payload_start:
	.incbin "build/SV6X66/SV6X66_stub_code.bin"
__stub_payload_end:

