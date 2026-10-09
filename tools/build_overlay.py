"""Build the room-5 asset overlay from the user's extracted Twilight Princess ROM."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import types
from io import BytesIO
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parent
sys.path.insert(0, str(WORKSPACE / "tools" / "gclib"))
sys.modules.setdefault("imagequant", types.ModuleType("imagequant"))

from gclib.rarc import RARC
from gclib.yaz0_yay0 import Yaz0


STAGE_ROOM = Path("files/res/Stage/F_SP121/R05_00.arc")
ACTOR_SIZE = 32
PASSER_NAME = b"Passer\0\0"
TAG_ESCAPE_NAME = b"TagEsc\0\0"
TAG_MESSAGE_NAME = b"TagKMsg\0"


def unpack_arc(path: Path) -> RARC:
    raw = BytesIO(path.read_bytes())
    if raw.getvalue()[:4] == b"Yaz0":
        raw = Yaz0.decompress(raw)
    if raw.getvalue()[:4] != b"RARC":
        raise ValueError(f"not a RARC archive: {path}")
    return RARC(raw)


def room_dzr(arc: RARC):
    entry = next(e for e in arc.file_entries if not e.is_dir and e.name == "room.dzr")
    return entry, entry.data.getvalue()


def actor_chunk(dzr: bytes) -> tuple[int, int, int]:
    count = struct.unpack_from(">I", dzr, 0)[0]
    for index in range(count):
        header = 4 + 12 * index
        if dzr[header : header + 4] == b"ACTR":
            return (
                header,
                struct.unpack_from(">I", dzr, header + 4)[0],
                struct.unpack_from(">I", dzr, header + 8)[0],
            )
    raise ValueError("ACTR chunk is missing")


def chunks(dzr: bytes) -> dict[bytes, tuple[int, int]]:
    return {
        dzr[4 + 12 * index : 8 + 12 * index]: struct.unpack_from(">II", dzr, 8 + 12 * index)
        for index in range(struct.unpack_from(">I", dzr, 0)[0])
    }


def records(dzr: bytes) -> list[bytes]:
    _, count, offset = actor_chunk(dzr)
    return [dzr[offset + i * ACTOR_SIZE : offset + (i + 1) * ACTOR_SIZE] for i in range(count)]


def route_endpoint_records(dzr: bytes, route_id: int) -> list[bytes]:
    tables = chunks(dzr)
    route_count, route_offset = tables[b"RPAT"]
    _, points_offset = tables[b"RPPN"]
    if route_id >= route_count:
        raise ValueError(f"road route {route_id} is absent")

    route = dzr[route_offset + route_id * 12 : route_offset + (route_id + 1) * 12]
    point_count, _, _, _, _, _, point_ptr = struct.unpack(">HHBBBBI", route)
    if point_count < 2:
        raise ValueError(f"road route {route_id} has fewer than two points")

    def point_xyz(index: int) -> tuple[float, float, float]:
        return struct.unpack_from(">fff", dzr, points_offset + point_ptr + index * 16 + 4)

    result = []
    for xyz in (point_xyz(0), point_xyz(point_count - 1)):
        result.append(
            TAG_ESCAPE_NAME
            + struct.pack(">IfffhhhH", route_id, *xyz, 0, 0, 0, 0xFFFF)
        )
    return result


def same_position(record: bytes, xyz: tuple[float, float, float], tolerance: float = 1.0) -> bool:
    current = struct.unpack_from(">fff", record, 12)
    return all(abs(current[i] - xyz[i]) < tolerance for i in range(3))


def make_message_tag(passer: bytes) -> bytes:
    xyz = struct.unpack_from(">fff", passer, 12)
    # Type 0 KMsg. Finite eye/attention offsets enable Link's ordinary A-button talk prompt.
    # The native code mod replaces the placeholder flow node with its runtime-allocated node ID.
    params = 0x0080A0FF
    return TAG_MESSAGE_NAME + struct.pack(">IfffhhhH", params, *xyz, 0, 0, 0, 0xFFFF)


def add_actor_records(dzr: bytes, additions: list[bytes]) -> bytes:
    header, count, offset = actor_chunk(dzr)
    insert = offset + count * ACTOR_SIZE
    result = bytearray(dzr[:insert] + b"".join(additions) + dzr[insert:])
    result[header + 4 : header + 8] = struct.pack(">I", count + len(additions))
    for index in range(struct.unpack_from(">I", result, 0)[0]):
        chunk_header = 4 + 12 * index
        if chunk_header == header:
            continue
        old_offset = struct.unpack_from(">I", result, chunk_header + 8)[0]
        if old_offset >= insert:
            struct.pack_into(">I", result, chunk_header + 8, old_offset + len(b"".join(additions)))
    return bytes(result)


def build(source: Path) -> dict[str, object]:
    if not source.is_file():
        raise FileNotFoundError(f"room archive not found: {source}")

    output = PROJECT / "overlay" / STAGE_ROOM
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(source.read_bytes())
    arc = unpack_arc(output)
    dzr_entry, dzr = room_dzr(arc)
    actor_records = records(dzr)
    passer = next((record for record in actor_records if record[:8] == PASSER_NAME), None)
    if passer is None:
        raise ValueError("room 5 does not contain the expected Passer actor")
    if struct.unpack_from(">I", passer, 8)[0] & 0xFF != 8:
        raise ValueError("room 5 Passer is not the expected MAN_a2 variant (type 8)")

    additions: list[bytes] = []
    existing = records(dzr)
    escapes = [record for record in existing if record[:8] == TAG_ESCAPE_NAME]
    for endpoint in route_endpoint_records(dzr, 1):
        xyz = struct.unpack_from(">fff", endpoint, 12)
        if not any(same_position(record, xyz) for record in escapes + additions):
            additions.append(endpoint)

    talk_tag = next((record for record in existing if record[:8] == TAG_MESSAGE_NAME), None)
    if talk_tag is None:
        additions.append(make_message_tag(passer))

    if additions:
        dzr = add_actor_records(dzr, additions)
        dzr_entry.data = BytesIO(dzr)
        dzr_entry.data_len = len(dzr)
        arc.save_changes()
        output.write_bytes(arc.data.getvalue())

    verified = unpack_arc(output)
    _, final_dzr = room_dzr(verified)
    final_records = records(final_dzr)
    labels = [record[:8].split(b"\0", 1)[0].decode("ascii") for record in final_records]
    tag_count = labels.count("TagEsc")
    message_count = labels.count("TagKMsg")
    if tag_count < 1 or message_count < 1:
        raise ValueError("room overlay verification failed: missing escape or talk tag")
    return {
        "overlay": str(output),
        "bytes": output.stat().st_size,
        "passer_params": f"0x{struct.unpack_from('>I', passer, 8)[0]:08X}",
        "actor_count": len(final_records),
        "escape_tags": tag_count,
        "talk_tags": message_count,
        "talk_tag_position_matches_passer": any(
            same_position(record, struct.unpack_from(">fff", passer, 12))
            for record in final_records
            if record[:8] == TAG_MESSAGE_NAME
        ),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--room-archive",
        type=Path,
        required=True,
        help="Prepared F_SP121/R05_00.arc containing the MAN_a2 passer and authored road route",
    )
    print(json.dumps(build(parser.parse_args().room_archive), indent=2))

