#!/usr/bin/env python3
import hashlib
import pathlib
import re
import shutil
import sys
import struct
import zlib


def fail(message):
	raise SystemExit("carrier verification failed: " + message)


def symbol(map_text, name):
	match = re.search(r"^\s*(0x[0-9a-fA-F]+)\s+" + re.escape(name) +
		r"(?:\s*=.*)?\s*$",
		map_text, re.MULTILINE)
	if not match:
		fail("missing map symbol " + name)
	return int(match.group(1), 16)


def main():
	if len(sys.argv) not in (6, 7, 8):
		fail("expected raw, payload, map, disassembly, output")
	raw_path, payload_path, map_path, dis_path, output_path = map(pathlib.Path, sys.argv[1:6])
	bank = sys.argv[6] if len(sys.argv) >= 7 else "B"
	aware = len(sys.argv) == 8 and sys.argv[7] == "1"
	if bank not in ("A", "B"):
		fail("bank must be A or B")
	base = {"A": 0x3000B000, "B": 0x300A6000}[bank]
	if bank == "A" and not aware:
		fail("Bank A requires bank-aware rollback")
	raw = raw_path.read_bytes()
	payload = payload_path.read_bytes()
	map_text = map_path.read_text(encoding="utf-8", errors="replace")
	dis_text = dis_path.read_text(encoding="utf-8", errors="replace")

	if raw[:4] != bytes.fromhex("48 00 00 02"):
		fail("Bank-B entry is not the exact stock-compatible branch header")
	if not payload:
		fail("embedded restore payload is empty")
	if raw.count(payload) != 1:
		fail("restore payload is not embedded exactly once")
	if len(raw) > 12288 - 32:
		fail("carrier leaves no room for the 32-byte identity trailer within 12 KiB")

	checks = {
		"_carrier_header": base,
		"__rollback_start__": 0x00007000,
		"sv6x66_rollback_entry": 0x00007000,
		"__stub_payload_start": None,
		"__stub_payload_end": None,
		"__rollback_load_start__": None,
		"__rollback_load_end__": None,
	}
	values = {}
	for name, expected in checks.items():
		values[name] = symbol(map_text, name)
		if expected is not None and values[name] != expected:
			fail(f"{name} is 0x{values[name]:08X}, expected 0x{expected:08X}")
	if aware and symbol(map_text, "SV6X66_CARRIER_BANK_ID") != (0 if bank == "A" else 1):
		fail("bank handoff identity does not match linked carrier")
	if values["__stub_payload_end"] - values["__stub_payload_start"] != len(payload):
		fail("map payload range does not match the current restore payload")
	rollback_size = values["__rollback_load_end__"] - values["__rollback_load_start__"]
	if rollback_size <= 0 or rollback_size > 0x1000:
		fail("rollback copy range is outside reserved ILM")
	if "<sv6x66_rollback_entry>:" not in dis_text:
		fail("rollback entry is absent from disassembly")

	# No direct call/branch from the SRAM routine may target Bank-B XIP. The
	# final indirect jump to 0x4000 is intentionally not encoded as a target.
	rollback_dis = dis_text.split("<sv6x66_rollback_entry>:", 1)[1]
	for target in re.findall(r"\b(?:jal|j|bal|beqz|bnez|bne|beq)\S*\s+([0-9a-fA-F]+)", rollback_dis):
		address = int(target, 16)
		if 0x30000000 <= address < 0x30200000:
			fail(f"SRAM rollback branch targets Bank-B XIP at 0x{address:08X}")

	if aware:
		# Host checks this trailer before staging; it is integrity metadata,
		# not a signature. It is never executed or copied into the RAM payload.
		raw += struct.pack("<8sIIII8s", b"SV6CARR1", 0 if bank == "A" else 1,
			base - 0x30000000, len(raw) + 32, zlib.crc32(raw), bytes(8))
		if len(raw) > 12288:
			fail("carrier plus identity trailer exceeds 12 KiB")
		output_path.write_bytes(raw)
	else:
		shutil.copyfile(raw_path, output_path)
	output_path.with_name(output_path.name + ".sha256").write_text(
		hashlib.sha256(raw).hexdigest() + "  " + output_path.name + "\n", encoding="ascii")
	print(f"carrier bytes: {len(raw)}")
	print(f"carrier sha256: {hashlib.sha256(raw).hexdigest()}")
	print(f"payload sha256: {hashlib.sha256(payload).hexdigest()}")
	print(f"payload XIP range: 0x{values['__stub_payload_start']:08X}-0x{values['__stub_payload_end']:08X}")
	print(f"rollback copy: 0x{values['__rollback_load_start__']:08X} -> 0x00007000 ({rollback_size} bytes)")


if __name__ == "__main__":
	main()
