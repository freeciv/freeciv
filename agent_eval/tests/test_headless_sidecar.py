from __future__ import annotations

import os
import socket
import stat
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from agent_eval import headless_sidecar
from agent_eval.headless_sidecar import (
    FramedIPC,
    HeadlessSidecar,
    SidecarActionAmbiguous,
    SidecarActionNotAccepted,
    SidecarError,
)
from agent_eval.v2_control import NATIVE_OBSERVATION_ACTION_SCHEMA_ID


FAKE_CHILD = r'''#!/usr/bin/env python3
import signal
import socket
import struct
import sys
import time

def send(sock, text, fragmented=False):
    payload = text.encode("utf-8")
    frame = struct.pack(">I", len(payload)) + payload
    if fragmented:
        for byte in frame:
            sock.sendall(bytes([byte]))
            time.sleep(0.001)
    else:
        sock.sendall(frame)

def receive(sock):
    def exact(size):
        value = b""
        while len(value) < size:
            chunk = sock.recv(size - len(value))
            if not chunk:
                raise EOFError
            value += chunk
        return value
    length = struct.unpack(">I", exact(4))[0]
    return exact(length).decode("utf-8")

def percent(text):
    raw = text.encode("utf-8")
    return "".join(
        chr(byte) if (
            48 <= byte <= 57 or 65 <= byte <= 90 or 97 <= byte <= 122
            or byte in b"._~-"
        ) else "%%%02X" % byte
        for byte in raw
    )

fd = int(sys.argv[sys.argv.index("--ipc-fd") + 1])
player = sys.argv[sys.argv.index("--player") + 1]
sock = socket.socket(fileno=fd)
send(sock, "HELLO\t1\tfreeciv-agent", player == "Fragmented")
if player == "EarlyEOF":
    sock.close()
    sys.exit(4)
if receive(sock) != "HELLO\t1":
    sys.exit(5)
if player == "HandshakeTimeout":
    time.sleep(5)
    sys.exit(6)
send(sock, "HELLO\tOK\t1")
if player == "ReadyBeforeCaps":
    send(sock, "READY\t" + player)
    time.sleep(.05)
    sys.exit(19)
schema = "__NATIVE_SCHEMA_ID__"
caps = (
    "CAPS\t2\tACT,ACT_CAP,ACT_RELATION_CAP,OBS_OPEN,OBS_PAGE,"
    "PHASE_AVAILABLE,SCOPE_OPEN,SCOPE_PAGE,STATE_AVAILABLE,"
    "STATE_SCOPE_OPEN,STATE_SCOPE_PAGE,TARGET_ACTION,"
    "RELATION_SCOPE_OPEN,RELATION_SCOPE_PAGE"
    "\tpercent-tab\t8192\t" + schema
)
if player == "StateBeforeCaps":
    send(sock, "STATE_AVAILABLE\t1")
    time.sleep(.05)
    sys.exit(11)
if player == "BadCaps":
    send(sock, "CAPS\t2\tOBS_OPEN,ACT\tpercent-tab\t8192\t" + schema)
    time.sleep(.05)
    sys.exit(12)
if player == "OldCaps":
    send(sock, "CAPS\t2\tACT,OBS_OPEN,OBS_PAGE,PHASE_AVAILABLE,STATE_AVAILABLE\tpercent-tab\t8192")
    time.sleep(.05)
    sys.exit(14)
if player == "WrongSchema":
    send(sock, caps.rsplit("\t", 1)[0] + "\tsha256-" + "0" * 64)
    time.sleep(.05)
    sys.exit(15)
if player == "ExtraCaps":
    send(sock, caps + "\textra")
    time.sleep(.05)
    sys.exit(16)
if player == "DuplicateCaps":
    send(sock, (
        "CAPS\t2\tACT,ACT,OBS_OPEN,OBS_PAGE,PHASE_AVAILABLE,STATE_AVAILABLE"
        "\tpercent-tab\t8192\t" + schema
    ))
    time.sleep(.05)
    sys.exit(17)
if player == "OversizedCapsField":
    send(sock, caps.rsplit("\t", 1)[0] + "\t" + "x" * 129)
    time.sleep(.05)
    sys.exit(18)
if player == "MissingCaps":
    time.sleep(5)
    sys.exit(13)
send(sock, caps)
if player == "ReadyAfterCaps":
    send(sock, "READY\t" + player)
revision = 0
if player != "NoBootstrapState":
    revision = 1
    send(sock, "STATE_AVAILABLE\t1")
if receive(sock) != "TAKE":
    sys.exit(7)
if player == "TakeFailed":
    send(sock, "TAKE_FAILED\tNOT_ACQUIRED")
    time.sleep(.05)
    sys.exit(8)
if player == "ReadyAfterCaps":
    send(sock, "TAKE\tREADY")
else:
    send(sock, "TAKE\tQUEUED")
    if player != "NoBootstrapState":
        revision = 2
        send(sock, "STATE_AVAILABLE\t2")
    send(sock, "TAKE\tCOMMAND_SENT")
    if player == "PhaseReadyBeforeClient":
        send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t1")
    send(sock, "READY\tOtherPlayer" if player == "WrongReady" else "READY\t" + player)
if player == "WrongReady":
    time.sleep(.05)
    sys.exit(9)
if player == "KillOnly":
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
if player in {
    "PhaseInitial", "PhaseSequence", "PhaseCommandLock", "PhaseDuplicate",
}:
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t1", True)
if player == "PhaseDuplicate":
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t1")
if player == "PhaseSequence":
    time.sleep(.08)
    send(sock, "PHASE_AVAILABLE\t3\t2\t0\tplayers_alternate\t2\t0\t1\t0\t0", True)
if player == "PhaseContradiction":
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t1")
    send(sock, "PHASE_AVAILABLE\t2\t2\t0\tconcurrent\t1\t1\t1\t0\t1")
if player == "PhaseRegression":
    send(sock, "PHASE_AVAILABLE\t3\t2\t0\tconcurrent\t1\t1\t1\t0\t1")
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t1")
if player == "PhaseMalformed":
    send(sock, "PHASE_AVAILABLE\t02\t1\t0\tconcurrent\t1\t1\t1\t0\t1")
if player == "PhaseBadImplication":
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t0\t1\t0\t1")
if player == "PhaseBadMode":
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tfuture_mode\t1\t1\t1\t0\t1")
if player == "PhaseBadFlag":
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t2")
if player == "PhaseBadRange":
    send(sock, "PHASE_AVAILABLE\t2\t1\t1\tconcurrent\t1\t1\t1\t0\t1")
if player == "PhaseConcurrentContradiction":
    send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t2\t1\t1\t0\t1")
if player == "PhaseHugeTurn":
    send(sock, "PHASE_AVAILABLE\t2\t2147483648\t0\tconcurrent\t1\t1\t1\t0\t1")
snapshots = []
snapshot_rows = {}
snapshot_revision = {}
snapshot_serial = 0
native_request_id = 40
forced_snapshot_expiries = 0
scope_views = {}
scope_serial = 0
state_scope_views = {}
state_scope_serial = 0
relation_views = {}
relation_serial = 0
while True:
    try:
        command = receive(sock)
    except EOFError:
        sys.exit(10)
    if command == "STATUS":
        if player == "PhaseNotificationFlood":
            for value in range(revision + 1, revision + 301):
                send(sock, "PHASE_AVAILABLE\t%d\t%d\t0\tteams_alternate\t1\t1\t1\t0\t1" % (
                    value, value,
                ))
            revision += 300
        elif player == "PhaseGlobalRegression":
            send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t1")
            send(sock, "STATE_AVAILABLE\t3")
            send(sock, "PHASE_AVAILABLE\t2\t1\t0\tconcurrent\t1\t1\t1\t0\t1")
        elif player == "NotificationFlood":
            for value in range(revision + 1, revision + 301):
                send(sock, "STATE_AVAILABLE\t%d" % value)
            revision += 300
        elif player == "StateRegression":
            send(sock, "STATE_AVAILABLE\t1")
            continue
        else:
            revision += 1
            send(sock, "STATE_AVAILABLE\t%d" % revision)
        send(
            sock,
            "STATUS\tstate=running\tserver=1\tseat=ready"
            "\tplayer=0\tlifecycle=1",
        )
    elif command.startswith("PING\t"):
        revision += 1
        send(sock, "STATE_AVAILABLE\t%d" % revision)
        send(sock, "PONG\t" + command.split("\t", 1)[1])
    elif command.startswith("OBS_OPEN\t"):
        fields = command.split("\t")
        request = fields[1] if len(fields) > 1 else "-"
        if player == "ObservationSlowMultiPage":
            time.sleep(.04)
        snapshot_serial += 1
        snapshot = "s%d-%d" % (revision, snapshot_serial)
        if player in {
            "ObservationMultiPage", "ObservationMultiPageMismatch",
            "ObservationNotificationFlood", "ObservationSlowMultiPage",
        }:
            rows = ["tile index=%d terrain=Grassland" % index
                    for index in range(35)]
        else:
            rows = [
                "meta turn=7 label=Alpha Beta",
                "action slot=a0123456789ABCDEF kind=phase.end",
                "unit ref=u:3:1 name=Warrior",
            ]
        snapshots.append(snapshot)
        snapshot_rows[snapshot] = rows
        snapshot_revision[snapshot] = revision
        if len(snapshots) > 2:
            expired = snapshots.pop(0)
            snapshot_rows.pop(expired, None)
            snapshot_revision.pop(expired, None)
        revision += 1
        send(sock, "STATE_AVAILABLE\t%d" % revision)
        send(sock, "OBS_OPENED\t%s\t%s\t%d\t%d" % (
            request, snapshot, snapshot_revision[snapshot], len(rows),
        ))
    elif command.startswith("OBS_PAGE\t"):
        fields = command.split("\t")
        request = fields[1]
        snapshot = fields[2]
        offset = int(fields[3])
        limit = int(fields[4])
        expiry_limit = {
            "SnapshotRetry": 1,
            "SnapshotRetryFails": 2,
        }.get(player, 0)
        if offset == 0 and forced_snapshot_expiries < expiry_limit:
            forced_snapshot_expiries += 1
            snapshot_rows.pop(snapshot, None)
            snapshot_revision.pop(snapshot, None)
            if snapshot in snapshots:
                snapshots.remove(snapshot)
            send(sock, "ERR\t%s\tSNAPSHOT_GONE\t%s" % (
                request, percent("snapshot is not pinned"),
            ))
            continue
        if snapshot not in snapshot_rows:
            send(sock, "ERR\t%s\tSNAPSHOT_GONE\t%s" % (
                request, percent("snapshot is not pinned"),
            ))
            continue
        if player == "ObservationSlowMultiPage":
            time.sleep(.04)
        rows = snapshot_rows[snapshot]
        page = rows[offset:offset + limit]
        page_revision = snapshot_revision[snapshot]
        begin_revision = (
            page_revision + 1
            if player == "MalformedPage" or (
                player == "ObservationMultiPageMismatch" and offset >= 16
            )
            else page_revision
        )
        if player == "ObservationNotificationFlood":
            for value in range(revision + 1, revision + 301):
                send(sock, "STATE_AVAILABLE\t%d" % value)
            revision += 300
        send(sock, "PAGE_BEGIN\t%s\t%s\t%d\t%d\t%d\t%d" % (
            request, snapshot, begin_revision, offset, len(page), len(rows),
        ))
        revision += 1
        send(sock, "STATE_AVAILABLE\t%d" % revision)
        for index, row in enumerate(page, offset):
            sent_index = index + 1 if player == "OutOfOrderPage" else index
            encoded_row = "%zz" if player == "BadPercentRow" else percent(row)
            send(sock, "ROW\t%s\t%s\t%d\t%s" % (
                request, snapshot, sent_index, encoded_row,
            ))
        send(sock, "PAGE_END\t%s\t%s\t%d" % (
            request, snapshot, offset + len(page),
        ))
    elif command.startswith("STATE_SCOPE_OPEN\t"):
        fields = command.split("\t")
        request = fields[1]
        expected_revision = int(fields[2])
        section, encoded_selector = fields[3], fields[4]
        if expected_revision != revision:
            send(sock, "ERR\t%s\tSTALE_REVISION\t%s" % (
                request, percent("state revision is not current"),
            ))
            continue
        state_scope_serial += 1
        view = "q%d-%d" % (revision, state_scope_serial)
        row_count = 35 if player == "StateScopeMultiPage" else 2
        rows = [
            "tile index=%d x=%d y=0 known=2 terrain=Grassland owner=none"
            % (index, index)
            for index in range(row_count)
        ]
        state_scope_views[view] = (section, encoded_selector, rows)
        send(sock, (
            "STATE_SCOPE_OPENED\t%s\t%s\t%d\t%s\t%s\t%d\t1\t0"
            % (request, view, revision, section, encoded_selector, len(rows))
        ))
    elif command.startswith("STATE_SCOPE_PAGE\t"):
        fields = command.split("\t")
        request, view = fields[1], fields[2]
        offset, limit = int(fields[3]), int(fields[4])
        section, encoded_selector, rows = state_scope_views[view]
        page = rows[offset:offset + limit]
        send(sock, (
            "STATE_SCOPE_BEGIN\t%s\t%s\t%d\t%s\t%s\t%d\t%d\t%d"
            % (request, view, revision, section, encoded_selector,
               offset, len(page), len(rows))
        ))
        for index, row in enumerate(page, offset):
            send(sock, "STATE_SCOPE_ROW\t%s\t%s\t%d\t%s" % (
                request, view, index, percent(row),
            ))
        send(sock, "STATE_SCOPE_END\t%s\t%s\t%d" % (
            request, view, offset + len(page),
        ))
    elif command.startswith("RELATION_SCOPE_OPEN\t"):
        fields = command.split("\t")
        request = fields[1]
        expected_revision = int(fields[2])
        encoded_actor, encoded_counterpart = fields[3], fields[4]
        if player == "RelationScopeOverflow":
            send(sock, (
                "RELATION_SCOPE_OPENED\t%s\t-\t%d\t%s\t%s\t0\t0\t1"
                % (request, expected_revision, encoded_actor,
                   encoded_counterpart)
            ))
            continue
        if expected_revision != revision:
            send(sock, "ERR\t%s\tSTALE_REVISION\t%s" % (
                request, percent("relation revision is not current"),
            ))
            continue
        relation_serial += 1
        view = "r%d-%d" % (revision, relation_serial)
        rows = [
            "action slot=a0000000000000095 kind=diplomacy.accept "
            "actor=p:1:10 counterpart=p:2:20"
        ]
        relation_views[view] = (encoded_actor, encoded_counterpart, rows)
        send(sock, (
            "RELATION_SCOPE_OPENED\t%s\t%s\t%d\t%s\t%s\t%d\t1\t0"
            % (request, view, revision, encoded_actor, encoded_counterpart,
               len(rows))
        ))
    elif command.startswith("RELATION_SCOPE_PAGE\t"):
        fields = command.split("\t")
        request, view = fields[1], fields[2]
        offset, limit = int(fields[3]), int(fields[4])
        encoded_actor, encoded_counterpart, rows = relation_views[view]
        page = rows[offset:offset + limit]
        send(sock, (
            "RELATION_SCOPE_BEGIN\t%s\t%s\t%d\t%s\t%s\t%d\t%d\t%d"
            % (request, view, revision, encoded_actor, encoded_counterpart,
               offset, len(page), len(rows))
        ))
        for index, row in enumerate(page, offset):
            send(sock, "RELATION_SCOPE_ACTION\t%s\t%s\t%d\t%s" % (
                request, view, index, percent(row),
            ))
        send(sock, "RELATION_SCOPE_END\t%s\t%s\t%d" % (
            request, view, offset + len(page),
        ))
    elif command.startswith("SCOPE_OPEN\t"):
        fields = command.split("\t")
        request = fields[1]
        expected_revision = int(fields[2])
        encoded_actor = fields[3]
        if player == "ScopeOverflow":
            send(sock, "SCOPE_OPENED\t%s\t-\t%d\t%s\t0\t0\t1" % (
                request, expected_revision, encoded_actor,
            ))
            continue
        if expected_revision != revision:
            send(sock, "ERR\t%s\tSTALE_REVISION\t%s" % (
                request, percent("scope revision is not current"),
            ))
            continue
        scope_serial += 1
        view = "v%d-%d" % (revision, scope_serial)
        if encoded_actor == percent("c:20:200"):
            rows = [
                "action slot=a0000000000000065 kind=city.set_production "
                "actor=c:20:200 target_tile=-1 target_tech=-1 "
                "target_government=-1 max_rate=0 "
                "target_build_kind=improvement target_build=5 target_extra=-1 "
                "activity=none target_name=Granary "
                "native_rule=city.set_production target_kind=Production "
                "result=Production%20Changed actor_consuming_always=0 "
                "legality=legal probability_kind=exact probability_min=200 "
                "probability_max=200 args=none",
            ]
        elif encoded_actor == percent("u:10:100"):
            rows = [
                "action slot=a0000000000000068 kind=unit.start_activity "
                "actor=u:10:100 target_tile=-1 target_tech=-1 "
                "target_government=-1 max_rate=0 "
                "target_build_kind=none target_build=-1 target_extra=7 "
                "activity=pillage target_name=Irrigation "
                "native_rule=unit.start_activity target_kind=Worker%20Activity "
                "result=Activity%20Installed actor_consuming_always=0 "
                "legality=legal probability_kind=exact probability_min=200 "
                "probability_max=200 args=none",
            ]
        else:
            rows = [
                "action slot=a0123456789ABCDEF kind=phase.end actor=none "
                "target_tile=-1 target_tech=-1 vote_no=-1 target_government=-1 "
                "max_rate=0 "
                "target_build_kind=none target_build=-1 target_extra=-1 "
                "activity=none target_name=none "
                "native_rule=phase.end target_kind=player result=phase_end "
                "actor_consuming_always=0 legality=legal "
                "probability_kind=exact probability_min=200 "
                "probability_max=200 args=none",
            ]
        scope_views[view] = (encoded_actor, rows)
        send(sock, "SCOPE_OPENED\t%s\t%s\t%d\t%s\t%d\t1\t0" % (
            request, view, revision, encoded_actor, len(rows),
        ))
    elif command.startswith("SCOPE_PAGE\t"):
        fields = command.split("\t")
        request, view = fields[1], fields[2]
        offset, limit = int(fields[3]), int(fields[4])
        encoded_actor, rows = scope_views[view]
        page = rows[offset:offset + limit]
        send(sock, "SCOPE_BEGIN\t%s\t%s\t%d\t%s\t%d\t%d\t%d" % (
            request, view, revision, encoded_actor, offset, len(page), len(rows),
        ))
        for index, row in enumerate(page, offset):
            send(sock, "SCOPE_ACTION\t%s\t%s\t%d\t%s" % (
                request, view, index, percent(row),
            ))
        send(sock, "SCOPE_END\t%s\t%s\t%d" % (
            request, view, offset + len(page),
        ))
    elif command.startswith("TARGET_ACTION\t"):
        fields = command.split("\t")
        request, expected_revision = fields[1], int(fields[2])
        encoded_actor, native_tile = fields[3], int(fields[4])
        if expected_revision != revision:
            send(sock, "ERR\t%s\tSTALE_REVISION\t%s" % (
                request, percent("target revision is not current"),
            ))
            continue
        if player == "TargetMalformed":
            send(sock, "TARGET_BEGIN\t%s\t%d\t%s\t%d\t1" % (
                request, revision, encoded_actor, native_tile,
            ))
            send(sock, "TARGET_ROW\t%s\t1\t%s" % (
                request, percent("bad row index"),
            ))
            continue
        if player == "TargetDesync":
            send(sock, "ERR\t%s\tSTREAM_DESYNC\t%s" % (
                request, percent("fresh sidecar required"),
            ))
            continue
        if native_tile != 42:
            send(sock, "TARGET_BEGIN\t%s\t%d\t%s\t%d\t0" % (
                request, revision, encoded_actor, native_tile,
            ))
            send(sock, "TARGET_END\t%s\t0" % request)
            continue
        rows = ((
            "action slot=t0000002A0123456789ABCDEF kind=unit.goto "
            "actor=u:10:100 target_tile=42"
        ), (
            "action slot=t0000002AFEDCBA9876543210 kind=unit.special "
            "actor=u:10:100 target_tile=42"
        ))
        send(sock, "TARGET_BEGIN\t%s\t%d\t%s\t%d\t%d" % (
            request, revision, encoded_actor, native_tile, len(rows),
        ))
        for index, row in enumerate(rows):
            send(sock, "TARGET_ROW\t%s\t%d\t%s" % (
                request, index, percent(row),
            ))
        send(sock, "TARGET_END\t%s\t%d" % (request, len(rows)))
    elif (
        command.startswith("ACT\t") or command.startswith("ACT_CAP\t")
        or command.startswith("ACT_RELATION_CAP\t")
    ):
        fields = command.split("\t")
        request = fields[1]
        relation_scoped = fields[0] == "ACT_RELATION_CAP"
        scoped = fields[0] == "ACT_CAP"
        slot = fields[5] if relation_scoped else (fields[4] if scoped else fields[2])
        arguments = fields[6] if relation_scoped else (fields[5] if scoped else fields[3])
        if player == "ActionEOFBeforeAck":
            sock.close()
            sys.exit(14)
        if player == "ActionAckTimeout":
            time.sleep(5)
            sys.exit(15)
        if player in {
            "ActionMalformedAck", "ActionWrongSlotAck",
            "ActionZeroRequestAck", "ActionZeroRevisionAck",
        }:
            ack_request = (
                "wrong-request" if player == "ActionMalformedAck" else request
            )
            ack_slot = (
                "aFFFFFFFFFFFFFFFF"
                if player == "ActionWrongSlotAck" else slot
            )
            ack_native_request = 0 if player == "ActionZeroRequestAck" else 41
            ack_revision = 0 if player == "ActionZeroRevisionAck" else revision
            send(sock, "ACT_ACCEPTED\t%s\t%s\t%d\t%d" % (
                ack_request, ack_slot, ack_native_request, ack_revision,
            ))
            continue
        if player == "ActionUncorrelatedRejection":
            send(sock, "ERR\twrong-request\tBUSY\t%s" % percent("busy"))
            continue
        if player == "ActionBusy":
            send(sock, "ERR\t%s\tBUSY\t%s" % (
                request, percent("private server detail"),
            ))
            continue
        if player == "ActionUnknownRejection":
            send(sock, "ERR\t%s\tFUTURE_CODE\t%s" % (
                request, percent("future private detail"),
            ))
            continue
        if player == "ActionRevalidationDesync":
            send(sock, "ERR\t%s\tREVALIDATION_DESYNC\t%s" % (
                request, percent("fresh sidecar required"),
            ))
            continue
        if player == "ActionMalformedErrCode":
            send(sock, "ERR\t%s\tbad-code\tprivate" % request)
            continue
        if player == "ActionMalformedErrDetail":
            send(sock, "ERR\t%s\tBUSY\t%%zz" % request)
            continue
        if player == "ActionAckDelay":
            time.sleep(.08)
        if request == "req-city" and arguments != "city_name%3DNew%20Rome":
            send(sock, "ERR\t%s\tBAD_ENCODING\tbad-arguments" % request)
            continue
        if slot == "aFFFFFFFFFFFFFFFF":
            send(sock, "ERR\t%s\tSTALE_SLOT\t%s" % (
                request, percent("action slot is not current"),
            ))
            continue
        native_request_id += 1
        send(sock, "ACT_ACCEPTED\t%s\t%s\t%d\t%d" % (
            request, slot, native_request_id, revision,
        ))
        revision += 1
        send(sock, "STATE_AVAILABLE\t%d" % revision)
        if player == "NoActionResult":
            continue
        if "boundary" in request:
            status, reason = "rejected", "PROCESSING_BOUNDARY_MISMATCH"
        elif "epoch" in request:
            status, reason = "rejected", "SEAT_EPOCH_CHANGED"
        elif "rejected" in request:
            status, reason = "rejected", "POSTCONDITION_NOT_MET"
        elif "timeout" in request:
            status, reason = "timeout", "PROCESSING_TIMEOUT"
        else:
            status, reason = "applied", "POSTCONDITION_VERIFIED"
        if player == "BadAppliedReason":
            status, reason = "applied", "PROCESSING_BOUNDARY_MISMATCH"
        elif player == "BadTimeoutReason":
            status, reason = "timeout", "POSTCONDITION_NOT_MET"
        result_id = native_request_id + 1 if player == "BadActionCorrelation" else native_request_id
        observation_selector = (
            "i0123456789abcdef" if "investigation" in request else "-"
        )
        send(sock, "ACT_RESULT\t%s\t%s\t%s\t%s\t%d\t%d\t%s" % (
            request, slot, status, reason, result_id, revision,
            observation_selector,
        ))
    elif command == "SHUTDOWN":
        if player == "KillOnly":
            continue
        send(sock, "BYE\tSHUTDOWN")
        sys.exit(0)
    else:
        send(sock, "ERROR\tBAD_COMMAND\ttest")
'''.replace(
    "__NATIVE_SCHEMA_ID__", NATIVE_OBSERVATION_ACTION_SCHEMA_ID,
)


class FramedIPCTests(unittest.TestCase):
    def setUp(self):
        self.left, self.right = socket.socketpair()
        self.ipc = FramedIPC(self.left)

    def tearDown(self):
        self.left.close()
        self.right.close()

    def test_fragmented_round_trip_and_strict_frame_validation(self):
        payload = "HELLO\t1\tfreeciv-agent".encode()
        frame = struct.pack(">I", len(payload)) + payload

        def fragmented_write():
            for byte in frame:
                self.right.sendall(bytes([byte]))

        thread = threading.Thread(target=fragmented_write)
        thread.start()
        self.assertEqual(
            self.ipc.receive(time.monotonic() + 1),
            "HELLO\t1\tfreeciv-agent",
        )
        thread.join(1)
        for raw, code in (
            (struct.pack(">I", 0), "invalid_frame"),
            (struct.pack(">I", 1) + b"\xff", "invalid_utf8"),
            (struct.pack(">I", 1) + b"\n", "invalid_control"),
        ):
            with self.subTest(code=code):
                self.right.sendall(raw)
                with self.assertRaises(SidecarError) as raised:
                    self.ipc.receive(time.monotonic() + 1)
                self.assertEqual(raised.exception.code, code)

    def test_deadline_and_outgoing_control_rules(self):
        with self.assertRaises(SidecarError) as timed_out:
            self.ipc.receive(time.monotonic() + 0.02)
        self.assertEqual(timed_out.exception.code, "deadline_exceeded")
        for text in ("", "bad\nframe", "x" * 8193):
            with self.subTest(text=text[:12]), self.assertRaises(SidecarError):
                self.ipc.send(text, time.monotonic() + 1)


class HeadlessSidecarTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.child = self.root / "fake-freeciv-agent"
        self.child.write_text(FAKE_CHILD, encoding="utf-8")
        self.child.chmod(0o700)
        self.sidecars: list[HeadlessSidecar] = []

    def tearDown(self):
        for sidecar in self.sidecars:
            sidecar.stop()
        self.temporary.cleanup()

    def make(self, player: str, **overrides):
        run_root = self.root / f"run-{len(self.sidecars)}"
        callbacks = overrides.pop("callbacks", [])
        sidecar = HeadlessSidecar(
            binary=self.child,
            run_root=run_root,
            game_id="game_test-sidecar-1234567890",
            seat_id="place-1",
            player_name=player,
            host="127.0.0.1",
            port=5555,
            generation=len(self.sidecars) + 1,
            on_exit=lambda generation, health: callbacks.append(
                (generation, health),
            ),
            handshake_timeout_s=overrides.pop("handshake_timeout_s", 1),
            stop_timeout_s=overrides.pop("stop_timeout_s", 0.1),
            **overrides,
        )
        self.sidecars.append(sidecar)
        return sidecar, callbacks

    def test_native_errors_have_deterministic_detail_free_mapping(self):
        expected = {
            "BAD_REQUEST": "native_bad_request",
            "BAD_ENCODING": "native_bad_encoding",
            "OBS_TOO_LARGE": "observation_too_large",
            "STATE_SCOPE_TOO_LARGE": "state_scope_too_large",
            "SNAPSHOT_GONE": "snapshot_gone",
            "BAD_OFFSET": "native_bad_offset",
            "ENCODE_FAILED": "native_encode_failed",
            "BUSY": "native_busy",
            "STALE_SLOT": "stale_slot",
            "NOT_READY": "native_not_ready",
            "STALE_ENTITY": "stale_entity",
            "BAD_ARGUMENT": "native_bad_argument",
            "NOT_SENT": "native_not_sent",
            "REVALIDATION_DESYNC": "protocol_error",
            "FUTURE_CODE": "native_error",
        }
        for native, mapped in expected.items():
            with self.subTest(native=native), self.assertRaises(
                SidecarError,
            ) as raised:
                HeadlessSidecar._raise_native_error(
                    f"ERR\treq-error\t{native}\tprivate%20path%20secret",
                    "req-error",
                )
            self.assertEqual(raised.exception.code, mapped)
            self.assertNotIn("private", str(raised.exception))
        with self.assertRaises(SidecarError) as uncorrelated:
            HeadlessSidecar._raise_native_error(
                "ERR\t-\tNOT_READY\tnot%20ready", "req-error",
            )
        self.assertEqual(uncorrelated.exception.code, "protocol_error")

        for native, mapped in expected.items():
            with self.subTest(action_native=native), self.assertRaises(
                SidecarActionNotAccepted,
            ) as rejected:
                HeadlessSidecar._raise_native_action_not_accepted(
                    f"ERR\treq-action\t{native}\tprivate%20path%20secret",
                    "req-action",
                )
            self.assertEqual(rejected.exception.code, mapped)
            self.assertNotIn("private", str(rejected.exception))
        with self.assertRaises(SidecarError) as action_uncorrelated:
            HeadlessSidecar._raise_native_action_not_accepted(
                "ERR\tother\tBUSY\tprivate", "req-action",
            )
        self.assertNotIsInstance(
            action_uncorrelated.exception, SidecarActionNotAccepted,
        )
        self.assertEqual(action_uncorrelated.exception.code, "protocol_error")

    def test_exact_fragmented_handshake_status_ping_and_graceful_stop(self):
        sidecar, callbacks = self.make("Fragmented")
        health = sidecar.start_and_take()
        self.assertEqual(health["state"], "ready")
        self.assertTrue(sidecar.ping("probe-1"))
        self.assertIn("state=running", sidecar.status())
        health = sidecar.public_health()
        self.assertEqual(health["client_state"], "running")
        self.assertTrue(health["server_connected"])
        self.assertEqual(health["seat_state"], "ready")
        self.assertEqual(sidecar.private_native_identity(), (0, 1))
        self.assertNotIn("native_player", health)
        self.assertNotIn("lifecycle", health)
        self.assertEqual(health["protocol_version"], 2)
        self.assertGreaterEqual(health["native_revision"], 4)
        self.assertTrue(health["capabilities_available"])
        stopped = sidecar.stop()
        self.assertEqual(stopped["state"], "stopped")
        self.assertFalse(sidecar._reader_thread.is_alive())
        self.assertFalse(sidecar._monitor_thread.is_alive())
        sidecar.stop()
        self.assertEqual(len(callbacks), 1)

    def test_ready_order_is_bound_to_valid_caps_not_redundant_take(self):
        before_caps, _ = self.make("ReadyBeforeCaps")
        with self.assertRaises(SidecarError) as rejected:
            before_caps.start_and_take()
        self.assertEqual(rejected.exception.code, "protocol_error")
        self.assertIsNone(before_caps.public_health()["ready_at"])

        # The native client can already own its exact target when HELLO
        # completes.  It then emits READY immediately after valid CAPS and
        # acknowledges the supervisor's redundant TAKE as TAKE READY.
        after_caps, _ = self.make("ReadyAfterCaps")
        health = after_caps.start_and_take()
        self.assertEqual(health["state"], "ready")
        self.assertEqual(health["player_name"], "ReadyAfterCaps")
        self.assertEqual(health["protocol_version"], 2)
        self.assertTrue(health["capabilities_available"])

    def test_status_ping_observation_and_action_share_one_command_lock(self):
        sidecar, _ = self.make("Concurrent")
        sidecar.start_and_take()
        opened = sidecar._obs_open("req-base")
        results: list[object] = []
        barrier = threading.Barrier(6)

        def status():
            barrier.wait()
            results.append(sidecar.status())

        def ping():
            barrier.wait()
            results.append(sidecar.ping("parallel"))

        def observation_open():
            barrier.wait()
            results.append(sidecar._obs_open("req-parallel-open"))

        def observation_page():
            barrier.wait()
            results.append(sidecar._obs_page(
                "req-parallel-page", opened["snapshot_id"],
                opened["revision"], opened["row_count"], 0, 2,
            ))

        def action():
            barrier.wait()
            results.append(sidecar._act(
                "req-parallel-act", "a0123456789ABCDEF",
            ))

        threads = [
            threading.Thread(target=status), threading.Thread(target=ping),
            threading.Thread(target=observation_open),
            threading.Thread(target=observation_page),
            threading.Thread(target=action),
        ]
        for thread in threads:
            thread.start()
        barrier.wait()
        for thread in threads:
            thread.join(2)
        self.assertEqual(len(results), 5)
        self.assertIn(True, results)
        self.assertTrue(any(isinstance(value, str) for value in results))
        self.assertFalse(any(thread.is_alive() for thread in threads))

    def test_wrong_player_take_failure_eof_and_timeout_fail_closed(self):
        for player, code, timeout in (
            ("WrongReady", "wrong_player", 1),
            ("TakeFailed", "take_failed", 1),
            ("EarlyEOF", "unexpected_eof", 1),
            ("HandshakeTimeout", "deadline_exceeded", 0.05),
            ("MissingCaps", "deadline_exceeded", 0.05),
            ("BadCaps", "protocol_error", 1),
            ("ExtraCaps", "protocol_error", 1),
            ("DuplicateCaps", "protocol_error", 1),
            ("OversizedCapsField", "protocol_error", 1),
            ("StateBeforeCaps", "protocol_error", 1),
        ):
            with self.subTest(player=player):
                sidecar, callbacks = self.make(
                    player, handshake_timeout_s=timeout,
                )
                with self.assertRaises(SidecarError) as raised:
                    sidecar.start_and_take()
                self.assertEqual(raised.exception.code, code)
                self.assertEqual(sidecar.public_health()["state"], "failed")
                self.assertEqual(len(callbacks), 1)

    def test_old_or_wrong_native_schema_fails_before_ready_without_echo(self):
        for player in ("OldCaps", "WrongSchema"):
            with self.subTest(player=player):
                sidecar, callbacks = self.make(player)
                with self.assertRaises(SidecarError) as raised:
                    sidecar.start_and_take()
                self.assertEqual(raised.exception.code, "schema_mismatch")
                self.assertEqual(str(raised.exception), "schema mismatch")
                health = sidecar.public_health()
                self.assertEqual(health["state"], "failed")
                self.assertEqual(health["error_code"], "schema_mismatch")
                self.assertIsNone(health["ready_at"])
                self.assertIsNone(health["protocol_version"])
                self.assertFalse(health["capabilities_available"])
                self.assertNotIn("000000", repr(health))
                self.assertEqual(len(callbacks), 1)

    def test_state_notifications_are_coalesced_and_never_evict_response(self):
        sidecar, _ = self.make("NotificationFlood")
        sidecar.start_and_take()
        self.assertIn("state=running", sidecar.status())
        health = sidecar.public_health()
        self.assertGreaterEqual(health["native_revision"], 302)
        self.assertEqual(len(sidecar._messages), 0)
        self.assertEqual(health["state"], "ready")

        regressed, _ = self.make("StateRegression")
        regressed.start_and_take()
        with self.assertRaises(SidecarError) as raised:
            regressed.status(timeout_s=0.5)
        self.assertEqual(raised.exception.code, "protocol_error")
        self.assertEqual(regressed.public_health()["state"], "failed")

    def test_phase_evidence_is_immutable_sanitized_and_coalesced(self):
        sidecar, _ = self.make("PhaseDuplicate")
        health = sidecar.start_and_take()
        evidence = sidecar.wait_phase_evidence(0, 1)
        self.assertEqual(dict(evidence), {
            "generation": sidecar.generation,
            "revision": 2,
            "turn": 1,
            "phase": 0,
            "mode": "concurrent",
            "phase_count": 1,
            "active": True,
            "alive": True,
            "done": False,
            "ready": True,
        })
        with self.assertRaises(TypeError):
            evidence["revision"] = 99
        self.assertEqual(dict(sidecar.phase_evidence()), dict(evidence))
        self.assertTrue(health["phase_evidence_available"] or
                        sidecar.public_health()["phase_evidence_available"])
        self.assertEqual(
            set(sidecar.public_health()) & {
                "turn", "phase", "mode", "phase_count", "active", "alive",
                "done", "ready",
            },
            set(),
        )
        serialized = repr(dict(evidence)).casefold()
        for secret_word in ("player", "name", "ref", "slot", "token"):
            self.assertNotIn(secret_word, serialized)

    def test_phase_wait_is_fragment_safe_and_never_takes_command_gate(self):
        sidecar, _ = self.make("PhaseSequence")
        sidecar.start_and_take()
        first = sidecar.wait_phase_evidence(0, 1)
        self.assertEqual(first["revision"], 2)
        with sidecar._command_lock:
            started = time.monotonic()
            second = sidecar.wait_phase_evidence(first["revision"], 1)
            self.assertLess(time.monotonic() - started, 0.5)
        self.assertEqual(dict(second), {
            "generation": sidecar.generation,
            "revision": 3,
            "turn": 2,
            "phase": 0,
            "mode": "players_alternate",
            "phase_count": 2,
            "active": False,
            "alive": True,
            "done": False,
            "ready": False,
        })
        with self.assertRaises(SidecarError) as timed_out:
            sidecar.wait_phase_evidence(3, 0.02)
        self.assertEqual(timed_out.exception.code, "deadline_exceeded")
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_phase_notification_flood_cannot_evict_command_response(self):
        sidecar, _ = self.make("PhaseNotificationFlood")
        sidecar.start_and_take()
        self.assertIn("state=running", sidecar.status(timeout_s=2))
        evidence = sidecar.phase_evidence()
        self.assertIsNotNone(evidence)
        self.assertEqual(evidence["revision"], 302)
        self.assertEqual(evidence["turn"], 302)
        self.assertEqual(len(sidecar._messages), 0)
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_phase_regression_contradiction_and_malformed_frames_fail_closed(self):
        for player in (
            "PhaseContradiction", "PhaseRegression", "PhaseMalformed",
            "PhaseBadImplication", "PhaseReadyBeforeClient", "PhaseBadMode",
            "PhaseBadFlag", "PhaseBadRange", "PhaseConcurrentContradiction",
            "PhaseHugeTurn",
        ):
            with self.subTest(player=player):
                sidecar, _ = self.make(player)
                try:
                    sidecar.start_and_take()
                except SidecarError as raised:
                    self.assertEqual(raised.code, "protocol_error")
                deadline = time.monotonic() + 1
                while (
                    sidecar.public_health()["state"] != "failed"
                    and time.monotonic() < deadline
                ):
                    time.sleep(.005)
                health = sidecar.public_health()
                self.assertEqual(health["state"], "failed")
                self.assertEqual(health["error_code"], "protocol_error")
                self.assertFalse(health["phase_evidence_available"])
                self.assertIsNone(sidecar.phase_evidence())

        global_regression, _ = self.make("PhaseGlobalRegression")
        global_regression.start_and_take()
        with self.assertRaises(SidecarError) as regressed:
            global_regression.status(timeout_s=1)
        self.assertEqual(regressed.exception.code, "protocol_error")
        self.assertEqual(
            global_regression.public_health()["state"], "failed",
        )

    def test_phase_validation_is_exact_and_does_not_mutate_latest_evidence(self):
        sidecar, _ = self.make("PhaseInitial")
        sidecar.start_and_take()
        evidence = sidecar.wait_phase_evidence(0, 1)
        for after, timeout in (
            (True, 1), (-1, 1), (1 << 65, 1), (0, True), (0, 0),
            (0, float("inf")),
            (0, 1e20),
        ):
            with self.subTest(after=after, timeout=timeout):
                with self.assertRaises(SidecarError) as invalid:
                    sidecar.wait_phase_evidence(after, timeout)
                self.assertEqual(invalid.exception.code, "invalid_argument")
        self.assertEqual(dict(sidecar.phase_evidence()), dict(evidence))
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_phase_wait_deadline_includes_condition_lock_acquisition(self):
        sidecar, _ = self.make("PhaseInitial")
        sidecar.start_and_take()
        sidecar.wait_phase_evidence(0, 1)
        locked = threading.Event()
        release = threading.Event()

        def hold_lifecycle_lock():
            with sidecar._lock:
                locked.set()
                release.wait(1)

        holder = threading.Thread(target=hold_lifecycle_lock)
        holder.start()
        self.assertTrue(locked.wait(1))
        started = time.monotonic()
        try:
            with self.assertRaises(SidecarError) as timed_out:
                sidecar.wait_phase_evidence(2, 0.02)
            self.assertEqual(timed_out.exception.code, "deadline_exceeded")
            self.assertLess(time.monotonic() - started, 0.1)
        finally:
            release.set()
            holder.join(1)

    def test_phase_evidence_is_cleared_and_waiters_wake_on_stop(self):
        sidecar, _ = self.make("PhaseInitial")
        sidecar.start_and_take()
        current = sidecar.wait_phase_evidence(0, 1)
        results = []

        def wait_for_next():
            try:
                sidecar.wait_phase_evidence(current["revision"], 1)
            except SidecarError as exc:
                results.append(exc.code)

        waiter = threading.Thread(target=wait_for_next)
        waiter.start()
        time.sleep(.02)
        sidecar.stop()
        waiter.join(1)
        self.assertFalse(waiter.is_alive())
        self.assertEqual(results, ["sidecar_unavailable"])
        self.assertIsNone(sidecar.phase_evidence())
        self.assertFalse(
            sidecar.public_health()["phase_evidence_available"],
        )

    def test_phase_reads_fail_closed_if_stop_is_already_requested(self):
        sidecar, _ = self.make("PhaseInitial")
        sidecar.start_and_take()
        current = sidecar.wait_phase_evidence(0, 1)
        with sidecar._lock:
            sidecar._stop_requested = True
        self.assertIsNone(sidecar.phase_evidence())
        self.assertFalse(
            sidecar.public_health()["phase_evidence_available"],
        )
        with self.assertRaises(SidecarError) as stopped:
            sidecar.wait_phase_evidence(current["revision"] - 1, 1)
        self.assertEqual(stopped.exception.code, "sidecar_unavailable")

    def test_bootstrap_and_take_do_not_require_state_notification(self):
        sidecar, _ = self.make("NoBootstrapState")
        health = sidecar.start_and_take()
        self.assertEqual(health["state"], "ready")
        self.assertIsNone(health["native_revision"])
        self.assertTrue(sidecar.ping("after-ready"))
        self.assertEqual(sidecar.public_health()["native_revision"], 1)

    def test_observation_open_and_complete_page_are_strict_and_decoded(self):
        sidecar, _ = self.make("Observation")
        sidecar.start_and_take()
        opened = sidecar._obs_open("req-open")
        self.assertEqual(opened["row_count"], 3)
        self.assertRegex(opened["snapshot_id"], r"^s[0-9]+-[0-9]+$")
        page = sidecar._obs_page(
            "req-page", opened["snapshot_id"], opened["revision"],
            opened["row_count"], 0, 16,
        )
        self.assertEqual(page["count"], 3)
        self.assertEqual(page["next_offset"], 3)
        self.assertEqual(page["total_count"], 3)
        self.assertEqual(page["rows"], [
            "meta turn=7 label=Alpha Beta",
            "action slot=a0123456789ABCDEF kind=phase.end",
            "unit ref=u:3:1 name=Warrior",
        ])
        self.assertGreater(
            sidecar.public_health()["native_revision"], opened["revision"],
        )
        with self.assertRaises(SidecarError) as invalid:
            sidecar._obs_page(
                "req-invalid", opened["snapshot_id"], opened["revision"],
                opened["row_count"], 0, 17,
            )
        self.assertEqual(invalid.exception.code, "invalid_page")
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_public_observation_reads_one_coherent_multipage_snapshot(self):
        sidecar, _ = self.make("ObservationMultiPage")
        sidecar.start_and_take()
        observed = sidecar.read_observation("req-public")
        self.assertEqual(
            set(observed), {"generation", "native_revision", "rows"},
        )
        self.assertEqual(observed["generation"], sidecar.generation)
        self.assertIsInstance(observed["native_revision"], int)
        self.assertIsInstance(observed["rows"], tuple)
        self.assertEqual(len(observed["rows"]), 35)
        self.assertEqual(
            observed["rows"][0], "tile index=0 terrain=Grassland",
        )
        self.assertEqual(
            observed["rows"][-1], "tile index=34 terrain=Grassland",
        )
        self.assertNotIn("snapshot_id", observed)
        self.assertNotIn("request_id", observed)
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_public_observation_retries_one_snapshot_expiry_once(self):
        sidecar, _ = self.make("SnapshotRetry")
        sidecar.start_and_take()
        observed = sidecar.read_observation("req-retry")
        self.assertEqual(len(observed["rows"]), 3)
        self.assertGreaterEqual(observed["native_revision"], 3)
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_public_observation_second_snapshot_expiry_is_nonterminal(self):
        sidecar, _ = self.make("SnapshotRetryFails")
        sidecar.start_and_take()
        with self.assertRaises(SidecarError) as expired:
            sidecar.read_observation("req-retry-fails")
        self.assertEqual(expired.exception.code, "snapshot_gone")
        self.assertNotIn("pinned", str(expired.exception))
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_public_observation_one_deadline_includes_lock_and_all_pages(self):
        sidecar, _ = self.make("ObservationSlowMultiPage")
        sidecar.start_and_take()
        lock_held = threading.Event()

        def briefly_hold_command_lock():
            with sidecar._command_lock:
                lock_held.set()
                time.sleep(.07)

        holder = threading.Thread(target=briefly_hold_command_lock)
        holder.start()
        self.assertTrue(lock_held.wait(1))
        started = time.monotonic()
        with self.assertRaises(SidecarError) as timed_out:
            sidecar.read_observation("req-total-deadline", timeout_s=.12)
        elapsed = time.monotonic() - started
        holder.join(1)
        self.assertEqual(timed_out.exception.code, "deadline_exceeded")
        self.assertLess(elapsed, .18)
        self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_public_observation_survives_async_notification_flood(self):
        sidecar, _ = self.make("ObservationNotificationFlood")
        sidecar.start_and_take()
        observed = sidecar.read_observation(
            "req-notification-flood", timeout_s=5,
        )
        self.assertEqual(len(observed["rows"]), 35)
        self.assertEqual(len(sidecar._messages), 0)
        self.assertGreaterEqual(
            sidecar.public_health()["native_revision"], 903,
        )
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_public_observation_validates_later_page_revision(self):
        sidecar, _ = self.make("ObservationMultiPageMismatch")
        sidecar.start_and_take()
        with self.assertRaises(SidecarError) as malformed:
            sidecar.read_observation("req-page-mismatch")
        self.assertEqual(malformed.exception.code, "protocol_error")
        self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_terminal_observation_hook_runs_before_exit_cleanup(self):
        events = []
        sidecar, _ = self.make("ObservationMultiPageMismatch")
        sidecar.on_exit = lambda _generation, _health: events.append("exit")
        sidecar.start_and_take()
        with self.assertRaises(SidecarError):
            sidecar.read_observation(
                "req-page-mismatch",
                on_terminal_error=lambda _error: events.append("trace"),
            )
        self.assertEqual(events, ["trace", "exit"])

    def test_two_snapshot_expiry_maps_error_without_leaking_native_detail(self):
        sidecar, _ = self.make("SnapshotExpiry")
        sidecar.start_and_take()
        first = sidecar._obs_open("req-open-one")
        sidecar._obs_open("req-open-two")
        sidecar._obs_open("req-open-three")
        with self.assertRaises(SidecarError) as expired:
            sidecar._obs_page(
                "req-expired", first["snapshot_id"], first["revision"],
                first["row_count"], 0, 1,
            )
        self.assertEqual(expired.exception.code, "snapshot_gone")
        self.assertNotIn("pinned", str(expired.exception))
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_actor_scope_open_page_and_overflow_are_strict(self):
        sidecar, _ = self.make("Scope")
        sidecar.start_and_take()
        revision = sidecar.public_health()["native_revision"]
        for suffix, actor_ref, action_kind in (
            ("player", "p:0:1", "phase.end"),
            ("city", "c:20:200", "city.set_production"),
            ("unit", "u:10:100", "unit.start_activity"),
        ):
            with self.subTest(actor_ref=actor_ref):
                page = sidecar.read_actor_scope(
                    f"req-scope-{suffix}", revision, actor_ref, limit=1,
                )
                self.assertEqual(page["native_revision"], revision)
                self.assertEqual(page["actor_ref"], actor_ref)
                self.assertEqual(page["offset"], 0)
                self.assertEqual(page["count"], 1)
                self.assertEqual(page["total_count"], 1)
                self.assertEqual(len(page["rows"]), 1)
                self.assertIn(f"kind={action_kind} ", page["rows"][0])

        overflow, _ = self.make("ScopeOverflow")
        overflow.start_and_take()
        with self.assertRaises(SidecarError) as too_large:
            overflow.read_actor_scope(
                "req-overflow",
                overflow.public_health()["native_revision"],
                "u:7:9",
            )
        self.assertEqual(too_large.exception.code, "actor_scope_too_large")
        self.assertEqual(overflow.public_health()["state"], "ready")

    def test_state_scope_catalog_is_fully_drained_from_one_pinned_view(self):
        sidecar, _ = self.make("StateScopeMultiPage")
        sidecar.start_and_take()
        revision = sidecar.public_health()["native_revision"]

        catalog = sidecar.read_state_scope_catalog(
            "req-state-catalog", revision, "known_tiles", "-",
            timeout_s=2,
        )

        self.assertEqual(catalog["native_revision"], revision)
        self.assertEqual(catalog["section"], "known_tiles")
        self.assertEqual(catalog["selector"], "-")
        self.assertRegex(catalog["view_id"], rf"^q{revision}-[1-9][0-9]*$")
        self.assertEqual(catalog["offset"], 0)
        self.assertEqual(catalog["count"], 35)
        self.assertEqual(catalog["total_count"], 35)
        self.assertEqual(catalog["next_offset"], 35)
        self.assertTrue(catalog["complete"])
        self.assertFalse(catalog["overflow"])
        self.assertEqual(len(catalog["rows"]), 35)
        self.assertIn("index=0 ", catalog["rows"][0])
        self.assertIn("index=34 ", catalog["rows"][-1])

    def test_city_governor_is_a_valid_state_scope_and_byte_cap_is_nonterminal(self):
        sidecar, _ = self.make("StateScope")
        sidecar.start_and_take()
        revision = sidecar.public_health()["native_revision"]
        catalog = sidecar.read_state_scope_catalog(
            "req-city-governor", revision, "city_governor", "c:20:200",
        )
        self.assertEqual(catalog["section"], "city_governor")
        with patch.object(headless_sidecar, "MAX_STATE_SCOPE_BYTES", 1):
            with self.assertRaises(SidecarError) as too_large:
                sidecar.read_state_scope_catalog(
                    "req-state-bytes", revision, "known_tiles", "-",
                )
        self.assertEqual(too_large.exception.code, "state_scope_too_large")
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_relation_scope_and_pair_bound_execution_are_strict(self):
        sidecar, _ = self.make("RelationScope")
        sidecar.start_and_take()
        revision = sidecar.public_health()["native_revision"]
        page = sidecar.read_relation_scope(
            "req-relation", revision, "p:1:10", "p:2:20", limit=1,
        )
        self.assertEqual(page["native_revision"], revision)
        self.assertEqual(page["actor_ref"], "p:1:10")
        self.assertEqual(page["counterpart_ref"], "p:2:20")
        self.assertRegex(page["view_id"], rf"^r{revision}-[1-9][0-9]*$")
        self.assertEqual(page["count"], 1)
        self.assertIn("kind=diplomacy.accept", page["rows"][0])
        applied = sidecar.execute_relation_scoped_action(
            "req-relation-act", revision, "p:1:10", "p:2:20",
            "a0000000000000095",
        )
        self.assertTrue(applied["accepted"])
        self.assertTrue(applied["applied"])

        overflow, _ = self.make("RelationScopeOverflow")
        overflow.start_and_take()
        with self.assertRaises(SidecarError) as too_large:
            overflow.read_relation_scope(
                "req-relation-overflow",
                overflow.public_health()["native_revision"],
                "p:1:10", "p:2:20",
            )
        self.assertEqual(
            too_large.exception.code, "relation_scope_too_large",
        )
        self.assertEqual(overflow.public_health()["state"], "ready")

        with self.assertRaises(SidecarError) as same_pair:
            sidecar.execute_relation_scoped_action(
                "req-same-pair", revision, "p:1:10", "p:1:10",
                "a0000000000000095",
            )
        self.assertEqual(same_pair.exception.code, "invalid_argument")

    def test_target_action_read_streams_bounded_catalog(self):
        sidecar, _ = self.make("Target")
        sidecar.start_and_take()
        revision = sidecar.public_health()["native_revision"]
        found = sidecar.read_target_action(
            "req-target-one", revision, "u:10:100", 42,
        )
        self.assertEqual(found, {
            "generation": sidecar.generation,
            "native_revision": revision,
            "actor_ref": "u:10:100",
            "native_tile": 42,
            "count": 2,
            "rows": (
                "action slot=t0000002A0123456789ABCDEF kind=unit.goto "
                "actor=u:10:100 target_tile=42",
                "action slot=t0000002AFEDCBA9876543210 kind=unit.special "
                "actor=u:10:100 target_tile=42",
            ),
        })
        empty = sidecar.read_target_action(
            "req-target-empty", revision, "u:10:100", 43,
        )
        self.assertEqual(empty["count"], 0)
        self.assertEqual(empty["rows"], ())

        malformed, _ = self.make("TargetMalformed")
        malformed.start_and_take()
        with self.assertRaises(SidecarError) as raised:
            malformed.read_target_action(
                "req-target-malformed",
                malformed.public_health()["native_revision"],
                "u:10:100", 42,
            )
        self.assertEqual(raised.exception.code, "protocol_error")
        self.assertEqual(malformed.public_health()["state"], "failed")

        desynchronized, _ = self.make("TargetDesync")
        desynchronized.start_and_take()
        with self.assertRaises(SidecarError) as raised:
            desynchronized.read_target_action(
                "req-target-desync",
                desynchronized.public_health()["native_revision"],
                "u:10:100", 42,
            )
        self.assertEqual(raised.exception.code, "protocol_error")
        self.assertEqual(desynchronized.public_health()["state"], "failed")

    def test_malformed_out_of_order_and_bad_percent_pages_fail_closed(self):
        for player in ("MalformedPage", "OutOfOrderPage", "BadPercentRow"):
            with self.subTest(player=player):
                sidecar, _ = self.make(player)
                sidecar.start_and_take()
                opened = sidecar._obs_open(f"req-open-{player}")
                with self.assertRaises(SidecarError) as raised:
                    sidecar._obs_page(
                        f"req-page-{player}", opened["snapshot_id"],
                        opened["revision"], opened["row_count"], 0, 2,
                    )
                self.assertEqual(raised.exception.code, "protocol_error")
                self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_action_waits_for_correlated_result_and_preserves_receipt_status(self):
        sidecar, _ = self.make("Actions")
        sidecar.start_and_take()
        applied = sidecar._act("req-applied", "a0123456789ABCDEF")
        self.assertTrue(applied["accepted"])
        self.assertTrue(applied["applied"])
        self.assertEqual(applied["status"], "applied")
        rejected = sidecar._act("req-rejected", "a0123456789ABCDEF")
        self.assertTrue(rejected["accepted"])
        self.assertFalse(rejected["applied"])
        self.assertEqual(rejected["status"], "rejected")
        city = sidecar._act(
            "req-city", "a0123456789ABCDEF", "city_name=New Rome",
        )
        self.assertTrue(city["applied"])
        investigation = sidecar._act(
            "req-investigation", "a0123456789ABCDEF",
        )
        self.assertEqual(
            investigation["observation_selector"], "i0123456789abcdef",
        )
        with self.assertRaises(SidecarActionNotAccepted) as stale:
            sidecar._act("req-stale", "aFFFFFFFFFFFFFFFF")
        self.assertEqual(stale.exception.code, "stale_slot")
        self.assertEqual(sidecar.public_health()["state"], "ready")

        for suffix, actor_ref, slot in (
            ("player", "p:0:1", "a0123456789ABCDEF"),
            ("city", "c:20:200", "a0000000000000065"),
            ("unit", "u:10:100", "a0000000000000068"),
        ):
            with self.subTest(actor_ref=actor_ref):
                scoped = sidecar.execute_scoped_action(
                    f"req-scoped-{suffix}",
                    sidecar.public_health()["native_revision"],
                    actor_ref, slot,
                )
                self.assertTrue(scoped["accepted"])
                self.assertTrue(scoped["applied"])

        mismatched, _ = self.make("BadActionCorrelation")
        mismatched.start_and_take()
        with self.assertRaises(SidecarActionAmbiguous) as correlation:
            mismatched._act("req-correlation", "a0123456789ABCDEF")
        self.assertEqual(
            correlation.exception.code, "action_accepted_ambiguous",
        )
        self.assertEqual(
            correlation.exception.ambiguity_reason, "result_unavailable",
        )
        self.assertEqual(mismatched.public_health()["state"], "failed")

        for player in ("BadAppliedReason", "BadTimeoutReason"):
            with self.subTest(player=player):
                invalid, _ = self.make(player)
                invalid.start_and_take()
                with self.assertRaises(SidecarActionAmbiguous) as bad_reason:
                    invalid._act("req-result", "a0123456789ABCDEF")
                self.assertEqual(
                    bad_reason.exception.code, "action_accepted_ambiguous",
                )
                self.assertEqual(invalid.public_health()["state"], "failed")

    def test_correlated_native_rejection_proves_not_accepted(self):
        for player, expected_code in (
            ("ActionBusy", "native_busy"),
            ("ActionUnknownRejection", "native_error"),
        ):
            with self.subTest(player=player):
                sidecar, _ = self.make(player)
                sidecar.start_and_take()
                callbacks = []
                with self.assertRaises(SidecarActionNotAccepted) as raised:
                    sidecar.execute_action(
                        "req-definitive", "a0123456789ABCDEF",
                        expected_revision=2,
                        on_accepted=lambda receipt: callbacks.append(receipt),
                    )
                self.assertEqual(raised.exception.code, expected_code)
                self.assertNotIn("private", str(raised.exception))
                self.assertEqual(callbacks, [])
                self.assertEqual(sidecar.public_health()["state"], "ready")

        poisoned, _ = self.make("ActionRevalidationDesync")
        poisoned.start_and_take()
        with self.assertRaises(SidecarActionNotAccepted) as desynchronized:
            poisoned.execute_scoped_action(
                "req-preflight-desync", 2, "u:10:100",
                "t0000002AFEDCBA9876543210",
            )
        self.assertEqual(desynchronized.exception.code, "protocol_error")
        self.assertEqual(poisoned.public_health()["state"], "failed")

    def test_full_write_then_pre_accept_failures_are_ambiguous(self):
        for player, timeout in (
            ("ActionEOFBeforeAck", 0.5),
            ("ActionAckTimeout", 0.03),
            ("ActionMalformedAck", 0.5),
            ("ActionWrongSlotAck", 0.5),
            ("ActionZeroRequestAck", 0.5),
            ("ActionZeroRevisionAck", 0.5),
            ("ActionUncorrelatedRejection", 0.5),
            ("ActionMalformedErrCode", 0.5),
            ("ActionMalformedErrDetail", 0.5),
        ):
            with self.subTest(player=player):
                sidecar, _ = self.make(player)
                sidecar.start_and_take()
                callbacks = []
                with self.assertRaises(SidecarActionAmbiguous) as raised:
                    sidecar.execute_action(
                        "req-pre-accept", "a0123456789ABCDEF",
                        timeout_s=timeout,
                        expected_revision=2,
                        on_accepted=lambda receipt: callbacks.append(receipt),
                    )
                self.assertEqual(
                    raised.exception.code, "action_delivery_ambiguous",
                )
                self.assertIsNone(raised.exception.acceptance)
                self.assertEqual(
                    raised.exception.ambiguity_reason,
                    "acceptance_unavailable",
                )
                self.assertEqual(raised.exception.stage, "pre_accept")
                self.assertFalse(raised.exception.stream_synchronized)
                self.assertNotIn("req-pre-accept", str(raised.exception))
                self.assertNotIn("a0123456789ABCDEF", str(raised.exception))
                self.assertEqual(callbacks, [])
                self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_ambiguity_hook_runs_before_unsynchronized_exit_cleanup(self):
        events = []
        sidecar, _ = self.make("ActionMalformedAck")
        sidecar.on_exit = lambda _generation, _health: events.append("exit")
        sidecar.start_and_take()
        with self.assertRaises(SidecarActionAmbiguous):
            sidecar.execute_action(
                "req-pre-accept", "a0123456789ABCDEF",
                expected_revision=2,
                on_ambiguous=lambda _error: events.append("trace"),
            )
        self.assertEqual(events, ["trace", "exit"])

    def test_every_partial_action_write_failure_is_ambiguous(self):
        command = "ACT\treq-fragment\ta0123456789ABCDEF\t-"
        payload = command.encode("utf-8")
        frame = struct.pack(">I", len(payload)) + payload
        for sent_length in (0, 1, 3, 4, 5, len(frame) // 2, len(frame) - 1):
            with self.subTest(sent_length=sent_length):
                sidecar, _ = self.make(f"FragmentWrite{sent_length}")
                sidecar.start_and_take()
                ipc = sidecar._ipc
                self.assertIsNotNone(ipc)

                def fail_during_write(value, deadline):
                    self.assertEqual(value, command)
                    if sent_length:
                        ipc.stream.sendall(frame[:sent_length])
                    raise SidecarError(
                        "ipc_write_failed", "private fragmented write detail",
                    )

                with patch.object(sidecar, "_send", fail_during_write):
                    with self.assertRaises(SidecarActionAmbiguous) as raised:
                        sidecar.execute_action(
                            "req-fragment", "a0123456789ABCDEF",
                            expected_revision=2,
                        )
                self.assertEqual(
                    raised.exception.code, "action_delivery_ambiguous",
                )
                self.assertIsNone(raised.exception.acceptance)
                self.assertNotIn("private", str(raised.exception))
                self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_unknown_send_failure_is_sanitized_ambiguity(self):
        sidecar, _ = self.make("UnknownWriteFailure")
        sidecar.start_and_take()
        with patch.object(
            sidecar, "_send",
            side_effect=RuntimeError("private transport exception"),
        ):
            with self.assertRaises(SidecarActionAmbiguous) as raised:
                sidecar.execute_action(
                    "req-unknown-write", "a0123456789ABCDEF",
                    expected_revision=2,
                )
        self.assertEqual(raised.exception.code, "action_delivery_ambiguous")
        self.assertIsNone(raised.exception.acceptance)
        self.assertNotIn("private transport exception", str(raised.exception))
        self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_unknown_failure_after_full_write_is_sanitized_ambiguity(self):
        sidecar, _ = self.make("UnknownWaitFailure")
        sidecar.start_and_take()
        with patch.object(
            sidecar, "_wait_message",
            side_effect=RuntimeError("private receive exception"),
        ):
            with self.assertRaises(SidecarActionAmbiguous) as raised:
                sidecar.execute_action(
                    "req-unknown-wait", "a0123456789ABCDEF",
                    expected_revision=2,
                )
        self.assertEqual(raised.exception.code, "action_delivery_ambiguous")
        self.assertIsNone(raised.exception.acceptance)
        self.assertNotIn("private receive exception", str(raised.exception))
        self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_validation_unavailable_and_lock_deadline_are_pre_send_errors(self):
        sidecar, _ = self.make("PreSendValidation")
        sidecar.start_and_take()
        original_send = sidecar._send
        sends = []

        def count_send(value, deadline):
            sends.append(value)
            return original_send(value, deadline)

        invalid_calls = (
            ("bad request", "a0123456789ABCDEF", "-", 1, None, None),
            ("req-valid", "bad-slot", "-", 1, None, None),
            ("req-valid", "a0123456789ABCDEF", "", 1, None, None),
            ("req-valid", "a0123456789ABCDEF", "-", 0, None, None),
            ("req-valid", "a0123456789ABCDEF", "-", 1, 0, None),
            ("req-valid", "a0123456789ABCDEF", "-", 1, 2, "bad"),
        )
        with patch.object(sidecar, "_send", count_send):
            for request, slot, arguments, timeout, revision, callback in invalid_calls:
                with self.subTest(request=request, slot=slot, timeout=timeout):
                    with self.assertRaises(SidecarError) as raised:
                        sidecar.execute_action(
                            request, slot, arguments, timeout,
                            expected_revision=revision,
                            on_accepted=callback,
                        )
                    self.assertNotIsInstance(
                        raised.exception, SidecarActionAmbiguous,
                    )
            self.assertEqual(sends, [])

            lock_held = threading.Event()
            release_lock = threading.Event()

            def hold_lock():
                with sidecar._command_lock:
                    lock_held.set()
                    release_lock.wait(1)

            holder = threading.Thread(target=hold_lock)
            holder.start()
            self.assertTrue(lock_held.wait(1))
            try:
                with self.assertRaises(SidecarError) as deadline:
                    sidecar.execute_action(
                        "req-lock-deadline", "a0123456789ABCDEF",
                        timeout_s=0.02, expected_revision=2,
                    )
                self.assertNotIsInstance(
                    deadline.exception, SidecarActionAmbiguous,
                )
                self.assertEqual(deadline.exception.code, "deadline_exceeded")
                self.assertEqual(sends, [])
                self.assertEqual(sidecar.public_health()["state"], "ready")
            finally:
                release_lock.set()
                holder.join(1)

        sidecar.stop()
        with self.assertRaises(SidecarError) as unavailable:
            sidecar.execute_action(
                "req-unavailable", "a0123456789ABCDEF",
                expected_revision=2,
            )
        self.assertNotIsInstance(
            unavailable.exception, SidecarActionAmbiguous,
        )
        self.assertEqual(unavailable.exception.code, "sidecar_unavailable")

    def test_native_unknown_terminals_are_accepted_but_ambiguous(self):
        for request, expected_reason in (
            ("req-boundary", "processing_boundary_mismatch"),
            ("req-epoch", "seat_epoch_changed"),
            ("req-timeout", "processing_timeout"),
        ):
            with self.subTest(request=request):
                sidecar, _ = self.make("UnknownTerminal")
                sidecar.start_and_take()
                accepted = []
                with self.assertRaises(SidecarActionAmbiguous) as raised:
                    sidecar.execute_action(
                        request, "a0123456789ABCDEF",
                        expected_revision=2,
                        on_accepted=lambda receipt: accepted.append(receipt),
                    )
                self.assertEqual(len(accepted), 1)
                self.assertEqual(raised.exception.acceptance, accepted[0])
                self.assertEqual(
                    raised.exception.ambiguity_reason,
                    expected_reason,
                )
                self.assertEqual(
                    raised.exception.stage, "correlated_terminal",
                )
                self.assertTrue(raised.exception.stream_synchronized)
                self.assertEqual(sidecar.public_health()["state"], "ready")
                self.assertEqual(
                    sidecar.status(),
                    "STATUS\tstate=running\tserver=1\tseat=ready"
                    "\tplayer=0\tlifecycle=1",
                )
                later = sidecar.execute_action(
                    "req-after-correlated-terminal",
                    "a0123456789ABCDEF",
                    expected_revision=sidecar.public_health()[
                        "native_revision"
                    ],
                )
                self.assertTrue(later["applied"])
                self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_action_acceptance_callback_is_exactly_once_and_sanitized(self):
        sidecar, _ = self.make("AcceptanceCallback")
        sidecar.start_and_take()
        accepted = []
        result = sidecar.execute_action(
            "req-callback", "a0123456789ABCDEF",
            expected_revision=2,
            on_accepted=lambda receipt: accepted.append(receipt),
        )
        self.assertTrue(result["applied"])
        self.assertNotIn("native_request_id", result)
        self.assertNotIn("action_slot", result)
        self.assertEqual(accepted, [{
            "request_id": "req-callback",
            "accepted": True,
            "accepted_revision": 2,
        }])
        self.assertNotIn("native_request_id", accepted[0])
        self.assertNotIn("action_slot", accepted[0])

    def test_acceptance_callback_command_reentry_fails_fast(self):
        sidecar, _ = self.make("AcceptanceReentry")
        sidecar.start_and_take()
        callback_results = []

        def reenter(receipt):
            started = time.monotonic()
            try:
                sidecar.status(timeout_s=0.5)
            except SidecarError as exc:
                callback_results.append((
                    receipt, exc.code, time.monotonic() - started,
                ))

        result = sidecar.execute_action(
            "req-callback-reentry", "a0123456789ABCDEF",
            expected_revision=2, on_accepted=reenter,
        )
        self.assertTrue(result["applied"])
        self.assertEqual(len(callback_results), 1)
        receipt, code, elapsed = callback_results[0]
        self.assertEqual(code, "command_in_progress")
        self.assertLess(elapsed, 0.1)
        self.assertEqual(
            set(receipt),
            {"request_id", "accepted", "accepted_revision"},
        )
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_acceptance_callback_cross_thread_reentry_fails_fast(self):
        sidecar, _ = self.make("AcceptanceThreadReentry")
        sidecar.start_and_take()
        worker_results = []

        def reenter_from_worker(receipt):
            def worker():
                try:
                    sidecar.status(timeout_s=0.5)
                except SidecarError as exc:
                    worker_results.append((receipt, exc.code))

            thread = threading.Thread(target=worker)
            thread.start()
            thread.join(0.2)
            self.assertFalse(thread.is_alive())

        started = time.monotonic()
        result = sidecar.execute_action(
            "req-thread-reentry", "a0123456789ABCDEF",
            expected_revision=2, on_accepted=reenter_from_worker,
        )
        self.assertLess(time.monotonic() - started, 0.5)
        self.assertTrue(result["applied"])
        self.assertEqual(len(worker_results), 1)
        receipt, code = worker_results[0]
        self.assertEqual(code, "command_in_progress")
        self.assertEqual(receipt["request_id"], "req-thread-reentry")
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_acceptance_callback_breaks_external_lock_inversion(self):
        sidecar, _ = self.make("ActionAckDelay", handshake_timeout_s=2)
        sidecar.start_and_take()
        external_lock = threading.Lock()
        send_started = threading.Event()
        waiter_acquiring = threading.Event()
        waiter_results = []
        original_send = sidecar._send
        original_acquire = sidecar._command_lock.acquire

        def signal_send(value, deadline):
            send_started.set()
            return original_send(value, deadline)

        def observe_acquire(*args, **kwargs):
            if threading.current_thread().name == "callback-lock-waiter":
                waiter_acquiring.set()
            return original_acquire(*args, **kwargs)

        def waiter():
            self.assertTrue(send_started.wait(0.5))
            with external_lock:
                try:
                    sidecar.status(timeout_s=0.5)
                except SidecarError as exc:
                    waiter_results.append(exc.code)

        waiter_thread = threading.Thread(
            target=waiter, name="callback-lock-waiter",
        )
        waiter_thread.start()

        def callback(unused_receipt):
            self.assertTrue(waiter_acquiring.wait(0.5))
            acquired = external_lock.acquire(timeout=0.2)
            self.assertTrue(acquired)
            if acquired:
                external_lock.release()

        with (
            patch.object(sidecar, "_send", signal_send),
            patch.object(sidecar._command_lock, "acquire", observe_acquire),
        ):
            started = time.monotonic()
            result = sidecar.execute_action(
                "req-lock-inversion", "a0123456789ABCDEF",
                expected_revision=2, on_accepted=callback,
            )
            elapsed = time.monotonic() - started
        waiter_thread.join(0.2)
        self.assertFalse(waiter_thread.is_alive())
        self.assertLess(elapsed, 0.5)
        self.assertTrue(result["applied"])
        self.assertEqual(waiter_results, ["command_in_progress"])
        self.assertEqual(sidecar.public_health()["state"], "ready")

    def test_terminal_on_exit_command_reentry_fails_fast(self):
        callback_results = []
        caller_thread = threading.get_ident()

        def reenter_on_exit(generation, health):
            started = time.monotonic()
            try:
                sidecar.ping("exit-reentry", timeout_s=0.5)
            except SidecarError as exc:
                callback_results.append((
                    generation, health["state"], exc.code,
                    threading.get_ident(), time.monotonic() - started,
                ))

        sidecar, _ = self.make("ExitReentry")
        sidecar.on_exit = reenter_on_exit
        sidecar.start_and_take()
        with patch.object(
            sidecar, "_send", side_effect=RuntimeError("private write failure"),
        ):
            started = time.monotonic()
            with self.assertRaises(SidecarActionAmbiguous):
                sidecar.execute_action(
                    "req-exit-reentry", "a0123456789ABCDEF",
                    expected_revision=2,
                )
            elapsed = time.monotonic() - started
        self.assertLess(elapsed, 0.2)
        self.assertEqual(len(callback_results), 1)
        generation, state, code, callback_thread, callback_elapsed = (
            callback_results[0]
        )
        self.assertEqual(generation, sidecar.generation)
        self.assertEqual(state, "failed")
        self.assertEqual(code, "sidecar_unavailable")
        self.assertEqual(callback_thread, caller_thread)
        self.assertLess(callback_elapsed, 0.1)

    def test_accepted_revision_mismatch_is_durable_then_ambiguous(self):
        sidecar, _ = self.make("AcceptedRevisionMismatch")
        sidecar.start_and_take()
        accepted = []
        with self.assertRaises(SidecarActionAmbiguous) as raised:
            sidecar.execute_action(
                "req-revision", "a0123456789ABCDEF",
                expected_revision=3,
                on_accepted=lambda receipt: accepted.append(receipt),
            )
        self.assertEqual(len(accepted), 1)
        self.assertEqual(raised.exception.acceptance, accepted[0])
        self.assertEqual(
            raised.exception.ambiguity_reason,
            "accepted_revision_mismatch",
        )
        self.assertEqual(raised.exception.stage, "post_accept")
        self.assertFalse(raised.exception.stream_synchronized)
        self.assertEqual(sidecar.public_health()["state"], "failed")
        with self.assertRaises(SidecarError) as replay:
            sidecar.execute_action(
                "req-revision", "a0123456789ABCDEF",
                expected_revision=3,
            )
        self.assertEqual(replay.exception.code, "sidecar_unavailable")

    def test_post_accept_deadline_is_accepted_but_ambiguous(self):
        sidecar, _ = self.make("NoActionResult")
        sidecar.start_and_take()
        accepted = []
        with self.assertRaises(SidecarActionAmbiguous) as raised:
            sidecar.execute_action(
                "req-lost-result", "a0123456789ABCDEF", timeout_s=0.03,
                expected_revision=2,
                on_accepted=lambda receipt: accepted.append(receipt),
            )
        self.assertEqual(len(accepted), 1)
        self.assertEqual(raised.exception.acceptance, accepted[0])
        self.assertEqual(
            raised.exception.ambiguity_reason, "result_unavailable",
        )
        self.assertEqual(raised.exception.stage, "post_accept")
        self.assertFalse(raised.exception.stream_synchronized)
        self.assertNotIn("native_request_id", raised.exception.acceptance)
        self.assertNotIn("action_slot", raised.exception.acceptance)
        self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_acceptance_callback_failure_is_sanitized_ambiguity(self):
        sidecar, _ = self.make("CallbackFailure")
        sidecar.start_and_take()
        calls = []

        def fail(receipt):
            calls.append(receipt)
            raise RuntimeError("secret-native-detail")

        with self.assertRaises(SidecarActionAmbiguous) as raised:
            sidecar.execute_action(
                "req-callback-fail", "a0123456789ABCDEF",
                expected_revision=2, on_accepted=fail,
            )
        self.assertEqual(len(calls), 1)
        self.assertEqual(
            raised.exception.ambiguity_reason,
            "acceptance_callback_failed",
        )
        self.assertEqual(raised.exception.stage, "post_accept")
        self.assertFalse(raised.exception.stream_synchronized)
        self.assertEqual(
            set(raised.exception.acceptance),
            {"request_id", "accepted", "accepted_revision"},
        )
        self.assertNotIn("secret-native-detail", str(raised.exception))
        self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_private_files_and_argv_environment_secret_hygiene(self):
        captured: dict[str, object] = {}

        def launch(argv, **kwargs):
            captured["argv"] = list(argv)
            captured["env"] = dict(kwargs["env"])
            captured["shell"] = kwargs["shell"]
            captured["close_fds"] = kwargs["close_fds"]
            captured["start_new_session"] = kwargs["start_new_session"]
            captured["pass_fds"] = kwargs["pass_fds"]
            return subprocess.Popen(argv, **kwargs)

        with patch.dict(os.environ, {
            "AGENT_EVAL_JOIN_TOKEN": "join-secret-value",
            "API_KEY": "api-secret-value",
            "MY_PASSWORD": "password-secret-value",
            "PATH": os.environ.get("PATH", ""),
        }, clear=False):
            sidecar, _ = self.make("Hygiene", process_factory=launch)
            sidecar.start_and_take()
        argv_text = " ".join(captured["argv"])
        environment = captured["env"]
        self.assertNotIn("join-secret-value", argv_text)
        self.assertNotIn("api-secret-value", argv_text)
        self.assertFalse(captured["shell"])
        self.assertTrue(captured["close_fds"])
        self.assertTrue(captured["start_new_session"])
        self.assertEqual(len(captured["pass_fds"]), 1)
        self.assertFalse(any(
            part in name.casefold()
            for name in environment
            for part in ("agent_eval", "password", "secret", "token", "key")
        ))
        self.assertEqual(stat.S_IMODE(sidecar.run_directory.stat().st_mode), 0o700)
        self.assertEqual(stat.S_IMODE(sidecar.home_directory.stat().st_mode), 0o700)
        for path in (
            sidecar.options_path, sidecar.stdout_path, sidecar.stderr_path,
        ):
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(environment["HOME"], str(sidecar.home_directory))
        self.assertEqual(environment["FREECIV_OPT"], str(sidecar.options_path))
        self.assertTrue(environment["FREECIV_DATA_PATH"].endswith("/data"))

    def test_uncooperative_child_is_killed_and_stop_is_idempotent(self):
        sidecar, callbacks = self.make("KillOnly", stop_timeout_s=0.05)
        sidecar.start_and_take()
        health = sidecar.stop()
        self.assertEqual(health["state"], "stopped")
        self.assertIsNotNone(health["exit_code"])
        self.assertEqual(sidecar.stop()["state"], "stopped")
        self.assertEqual(len(callbacks), 1)

    def test_stop_during_process_launch_cannot_resurrect_sidecar(self):
        launch_entered = threading.Event()
        release_launch = threading.Event()

        def launch(argv, **kwargs):
            launch_entered.set()
            self.assertTrue(release_launch.wait(2))
            return subprocess.Popen(argv, **kwargs)

        sidecar, _ = self.make(
            "HandshakeTimeout", process_factory=launch,
            handshake_timeout_s=1, stop_timeout_s=0.05,
        )
        start_result: list[object] = []
        stop_result: list[object] = []

        def start():
            try:
                start_result.append(sidecar.start_and_take())
            except Exception as exc:
                start_result.append(exc)

        start_thread = threading.Thread(target=start)
        stop_thread = threading.Thread(
            target=lambda: stop_result.append(sidecar.stop()),
        )
        start_thread.start()
        self.assertTrue(launch_entered.wait(1))
        stop_thread.start()
        release_launch.set()
        start_thread.join(3)
        stop_thread.join(3)
        self.assertFalse(start_thread.is_alive())
        self.assertFalse(stop_thread.is_alive())
        self.assertEqual(len(start_result), 1)
        self.assertIsInstance(start_result[0], SidecarError)
        self.assertEqual(len(stop_result), 1)
        self.assertNotEqual(sidecar.public_health()["state"], "ready")
        self.assertIsNotNone(sidecar._process.poll())

    def test_terminal_failure_cannot_be_overwritten_by_queued_ready(self):
        sidecar, _ = self.make("QueuedReady")

        class DeadProcess:
            @staticmethod
            def poll():
                return 9

        with sidecar._lock:
            sidecar._process = DeadProcess()
            sidecar._set_state("taking")
        sidecar._terminal("failed", "process_exited")
        with self.assertRaises(SidecarError) as raised:
            sidecar._commit_ready("QueuedReady")
        self.assertEqual(raised.exception.code, "process_exited")
        self.assertEqual(sidecar.public_health()["state"], "failed")

    def test_popen_failure_closes_both_socketpair_ends(self):
        sockets: list[socket.socket] = []
        real_socketpair = socket.socketpair

        def tracked_socketpair(*args, **kwargs):
            pair = real_socketpair(*args, **kwargs)
            sockets.extend(pair)
            return pair

        def fail_launch(*_args, **_kwargs):
            raise OSError("deterministic launch failure")

        sidecar, _ = self.make("LaunchFailure", process_factory=fail_launch)
        with patch(
            "agent_eval.headless_sidecar.socket.socketpair",
            side_effect=tracked_socketpair,
        ), self.assertRaises(SidecarError) as raised:
            sidecar.start_and_take()
        self.assertEqual(raised.exception.code, "launch_failed")
        self.assertEqual(sidecar.public_health()["state"], "failed")
        self.assertEqual(len(sockets), 2)
        self.assertTrue(all(stream.fileno() == -1 for stream in sockets))

    def test_unresolved_kill_stays_failed_and_cleanup_can_retry(self):
        class StubbornProcess:
            def __init__(self):
                self.terminate_count = 0
                self.kill_count = 0

            @staticmethod
            def poll():
                return None

            @staticmethod
            def wait(timeout=None):
                raise subprocess.TimeoutExpired("stubborn", timeout)

            def terminate(self):
                self.terminate_count += 1

            def kill(self):
                self.kill_count += 1

        sidecar, callbacks = self.make("Stubborn", stop_timeout_s=0.01)
        process = StubbornProcess()
        with sidecar._lock:
            sidecar._process = process
            sidecar._set_state("ready")
        first = sidecar.stop()
        self.assertEqual(first["state"], "failed")
        self.assertEqual(first["error_code"], "stop_failed")
        self.assertIsNone(first["exit_code"])
        first_kills = process.kill_count
        sidecar.stop()
        self.assertGreater(process.kill_count, first_kills)
        self.assertEqual(sidecar.public_health()["state"], "failed")
        self.assertEqual(len(callbacks), 1)


if __name__ == "__main__":
    unittest.main()
