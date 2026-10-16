#!/usr/bin/env python3
import hashlib
import pathlib
import re
import shutil
import sys


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
	if len(sys.argv) != 6:
		fail("expected raw, payload, map, disassembly, output")
	raw_path, payload_path, map_path, dis_path, output_path = map(pathlib.Path, sys.argv[1:])
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
	if len(raw) > 8192:
		fail("carrier exceeds the 8 KiB first-validation ceiling")

	checks = {
		"_carrier_header": 0x300A6000,
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
		if 0x300A6000 <= address < 0x300C0000:
			fail(f"SRAM rollback branch targets Bank-B XIP at 0x{address:08X}")

	shutil.copyfile(raw_path, output_path)
	print(f"carrier bytes: {len(raw)}")
	print(f"carrier sha256: {hashlib.sha256(raw).hexdigest()}")
	print(f"payload sha256: {hashlib.sha256(payload).hexdigest()}")
	print(f"payload XIP range: 0x{values['__stub_payload_start']:08X}-0x{values['__stub_payload_end']:08X}")
	print(f"rollback copy: 0x{values['__rollback_load_start__']:08X} -> 0x00007000 ({rollback_size} bytes)")


if __name__ == "__main__":
	main()
