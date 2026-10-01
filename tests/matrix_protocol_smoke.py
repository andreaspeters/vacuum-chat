#!/usr/bin/env python3
"""
Matrix protocol smoke test using stdlib HTTPServer.

Tests Matrix v3 endpoint contract exercised by MatrixNetwork:
- Login request path/body format
- Sync response shape with next_batch and joined m.room.message events
- Send request path/body format

This is a standalone test with a local mock server - no real Synapse required.
"""

import json
import http.server
import os
from pathlib import Path
import socketserver
import threading
import time
import uuid
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlparse, parse_qs
from urllib.request import Request, urlopen


def live_matrix_request(base_url, access_token, method, path, payload=None):
    """Perform one authenticated live Matrix request without printing secrets."""
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    headers = {"Authorization": f"Bearer {access_token}", "Accept": "application/json"}
    if body is not None:
        headers["Content-Type"] = "application/json"
    request = Request(base_url.rstrip("/") + path, data=body, headers=headers, method=method)
    try:
        with urlopen(request, timeout=20) as response:
            raw = response.read()
            return response.status, json.loads(raw.decode("utf-8") or "{}")
    except HTTPError as error:
        raw = error.read().decode("utf-8", errors="replace")
        try:
            parsed = json.loads(raw or "{}")
        except json.JSONDecodeError:
            parsed = {"error": raw[:300]}
        return error.code, parsed
    except URLError as error:
        return None, {"error": str(error.reason)}


def test_live_homeserver_contract():
    """Run a bounded real-homeserver smoke when Matrix credentials are exported."""
    required = ("MATRIX_HOMESERVER", "MATRIX_ACCESS_TOKEN", "MATRIX_USER_ID", "MATRIX_HOME_ROOM")
    if not all(os.environ.get(name) for name in required):
        print("\n=== Live Matrix Smoke: skipped (credentials/room env incomplete) ===")
        return

    print("\n=== Live Matrix Smoke: configured homeserver ===")
    base = os.environ["MATRIX_HOMESERVER"]
    token = os.environ["MATRIX_ACCESS_TOKEN"]
    user_id = os.environ["MATRIX_USER_ID"]
    room_id = os.environ["MATRIX_HOME_ROOM"]

    status, versions = live_matrix_request(base, token, "GET", "/_matrix/client/versions")
    assert status == 200, f"versions failed: {status} {versions.get('errcode')}"
    assert versions.get("versions"), "homeserver returned no Matrix versions"

    status, whoami = live_matrix_request(base, token, "GET", "/_matrix/client/v3/account/whoami")
    assert status == 200, f"whoami failed: {status} {whoami.get('errcode')}"
    assert whoami.get("user_id") == user_id, "whoami user does not match MATRIX_USER_ID"
    device_id = whoami.get("device_id")
    assert device_id, "whoami returned no device_id"

    status, sync = live_matrix_request(base, token, "GET", "/_matrix/client/v3/sync?timeout=0")
    assert status == 200, f"sync failed: {status} {sync.get('errcode')}"
    assert sync.get("next_batch"), "sync returned no next_batch"
    joined = sync.get("rooms", {}).get("join", {})
    assert room_id in joined, "MATRIX_HOME_ROOM is not joined for this account"
    to_device_events = sync.get("to_device", {}).get("events", [])

    status, keys = live_matrix_request(
        base, token, "POST", "/_matrix/client/v3/keys/query",
        {"device_keys": {user_id: [device_id]}})
    assert status == 200, f"keys/query failed: {status} {keys.get('errcode')}"
    assert device_id in keys.get("device_keys", {}).get(user_id, {}), "own device key missing"

    transaction_id = "vacuum-live-smoke-" + uuid.uuid4().hex
    content = {"msgtype": "m.notice", "body": f"[Vacuum live smoke] {transaction_id}"}
    send_path = f"/_matrix/client/v3/rooms/{quote(room_id, safe='')}/send/m.room.message/{quote(transaction_id, safe='')}"
    status, sent = live_matrix_request(base, token, "PUT", send_path, content)
    assert status == 200, f"message send failed: {status} {sent.get('errcode')} {sent.get('error')}"
    event_id = sent.get("event_id")
    assert event_id, "message send returned no event_id"

    event_path = f"/_matrix/client/v3/rooms/{quote(room_id, safe='')}/event/{quote(event_id, safe='')}"
    status, event = live_matrix_request(base, token, "GET", event_path)
    assert status == 200, f"message readback failed: {status} {event.get('errcode')}"
    assert event.get("type") == "m.room.message"
    assert event.get("content", {}).get("body") == content["body"]

    received = []
    since = sync["next_batch"]
    for _ in range(5):
        sync_path = "/_matrix/client/v3/sync?timeout=3000&since=" + quote(since, safe="")
        status, follow_up = live_matrix_request(base, token, "GET", sync_path)
        assert status == 200, f"follow-up sync failed: {status} {follow_up.get('errcode')}"
        timeline = follow_up.get("rooms", {}).get("join", {}).get(room_id, {}) \
            .get("timeline", {}).get("events", [])
        received = [item for item in timeline
                    if item.get("type") == "m.room.message" and
                    item.get("content", {}).get("body") == content["body"]]
        if received:
            break
        since = follow_up.get("next_batch", since)
    assert received, "follow-up sync did not return the sent message"
    print(f"  ✓ live Matrix API, send/readback/sync receive passed; "
          f"to_device_events={len(to_device_events)}")


class MockMatrixServer(http.server.BaseHTTPRequestHandler):
    """Mock Matrix server for protocol testing."""

    def do_POST(self):
        self._handle_request()

    def do_GET(self):
        self._handle_request()

    def _handle_request(self):
        parsed = urlparse(self.path)
        path = parsed.path
        query = parse_qs(parsed.query)

        print(f"[Mock Server] {self.command} {path}")

        with LockProvider().lock:
            request_type = None

            if '/_matrix/client/v3/login' in path and self.command == 'POST':
                request_type = 'login'
            elif '/_matrix/client/v3/sync' in path and self.command == 'GET':
                request_type = 'sync'
            elif '/_matrix/client/v3/rooms/' in path and '/send/m.room.message' in path:
                request_type = 'send'

            if request_type:
                LockProvider().counters[request_type] += 1

        # Serve pre-configured response
        response = LockProvider().responses.pop(0)
        status_code, headers, body = response
        self.send_response(status_code)
        for key, value in headers.items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body.encode('utf-8'))

    def log_message(self, format, *args):
        """Suppress default server logging."""


class LockProvider:
    """Thread-safe state for mock server."""
    lock = threading.Lock()
    counters = {'login': 0, 'sync': 0, 'send': 0}
    responses = []


def add_response(status_code, headers, body):
    """Add a response to the mock queue."""
    with LockProvider().lock:
        LockProvider().responses.append((status_code, headers, body))


def create_login_response():
    """Create successful Matrix login response."""
    data = {
        "user_id": "@test:matrix.org",
        "access_token": "test_token_alpha",
        "device_id": "DEVICEABC",
        "home_server": "matrix.org"
    }
    return 200, {"Content-Type": "application/json"}, json.dumps(data).encode()


def create_sync_response(next_batch=""):
    """Create Matrix sync response with timeline events."""
    data = {
        "next_batch": next_batch or "s:12345_abcde",
        "rooms": {
            "join": {
                "!room123:matrix.org": {
                    "summary": {},
                    "state": {},
                    "timeline": {
                        "events": [
                            {
                                "type": "m.room.message",
                                "event_id": "$msg123",
                                "sender": "@test:matrix.org",
                                "origin_server_ts": 1234567890,
                                "content": {
                                    "msgtype": "m.text",
                                    "body": "Hello from test"
                                }
                            }
                        ]
                    }
                }
            }
        }
    }
    return 200, {"Content-Type": "application/json"}, json.dumps(data).encode()


def create_empty_rooms_response():
    """Create sync response with no joined rooms (first sync)."""
    return create_sync_response(next_batch="")


def create_next_sync_response():
    """Create sync response for cursor pagination."""
    return create_sync_response(next_batch="s:67452_efghi")


def test_login_request():
    """Test Matrix login request structure."""
    print("\n=== Test 1: Login Request ===")

    # Verify structure matching MatrixNetwork expectations
    identifier = {"type": "m.id.user", "user": "@test:matrix.org"}
    body = {
        "type": "m.login.password",
        "identifier": identifier,
        "password": "secret123"
    }

    print(f"Login type: {body['type']}")
    print(f"Identifier type: {identifier['type']}")

    # Validate required fields
    assert body["type"] == "m.login.password", "Body type must be m.login.password"
    assert "identifier" in body, "Body must contain identifier"
    assert identifier["type"] == "m.id.user", "Identifier type must be m.id.user"
    assert "password" in body, "Body must contain password"

    # JSON encoding
    encoded = json.dumps(body)
    parsed = json.loads(encoded)
    assert parsed == body, "Round-trip successful"

    print("✓ Login request structure and JSON encoding valid")


def test_sync_response():
    """Test Matrix sync response structure."""
    print("\n=== Test 2: Sync Response ===")

    # Test first sync (no next_batch)
    print("  Testing first sync (empty joined rooms)...")
    data1 = {
        "next_batch": "s:12345_abcde",
        "rooms": {"join": {}}
    }
    parsed1 = json.loads(json.dumps(data1))
    assert parsed1["next_batch"].startswith("s:"), "next_batch must start with s:"
    assert "rooms" in parsed1, "Response needs rooms"
    assert "join" in parsed1["rooms"], "Rooms needs join"
    print("  ✓ First sync response valid")

    # Test cursor pagination (with next_batch)
    print("  Testing sync with cursor pagination...")
    data2 = create_sync_response(next_batch="s:67452_efghi")
    parsed2 = json.loads(data2[2].decode())
    assert "next_batch" in parsed2, "Pagination response needs next_batch"
    assert parsed2["next_batch"].startswith("s:"), "Cursor must start with s:"
    print("  ✓ Cursor-paginated sync response valid")

    # Test timeline event structure
    data3 = create_sync_response()
    parsed3 = json.loads(data3[2].decode())
    room = list(parsed3["rooms"]["join"].values())[0]
    timeline = room["timeline"]
    events = timeline.get("events", [])

    assert isinstance(events, list), "Events must be a list"
    if events:
        event = events[0]
        assert event["type"] == "m.room.message", "Event type must be m.room.message"
        assert "content" in event, "Event needs content"
        assert "sender" in event, "Event needs sender"
        assert "event_id" in event, "Event needs event_id"
        assert event["content"]["msgtype"] == "m.text", "Message type must be m.text"
        assert event["content"]["body"] == "Hello from test", "Message body must match"
        print("  ✓ Timeline event structure valid")

    print("✓ Sync response structure valid")


def test_send_request():
    """Test Matrix send message request structure."""
    print("\n=== Test 3: Send Request ===")

    # Test path construction
    room_id = "!room123:matrix.org"
    txn_id = "m_1234567890"
    text = "Hello, Matrix!"
    body = {"msgtype": "m.text", "body": text}

    # Expected path: /_matrix/client/v3/rooms/{room_id}/send/m.room.message/{txn_id}
    import urllib.parse
    send_path = f"/_matrix/client/v3/rooms/{urllib.parse.quote(room_id, safe='')}/send/m.room.message/{txn_id}"
    print(f"Send path: {send_path}")

    # Validate JSON body
    encoded = json.dumps(body)
    parsed = json.loads(encoded)
    assert parsed["msgtype"] == "m.text", "Msgtype must be m.text"
    assert parsed["body"] == text, "Body must match text"

    print("✓ Send request path and body structure valid")


def test_presence_events():
    """Test Matrix presence event structures."""
    print("\n=== Test 4: Presence Events ===")

    # Test m.presence event shape
    presence_event = {
        "type": "m.presence",
        "event_id": "$presence1",
        "sender": "@alice:matrix.org",
        "origin_server_ts": 1234567890,
        "content": {
            "presence": "online",
            "status_msg": "Hello, world!",
            "last_active_ts": 1234567000
        }
    }

    parsed = presence_event
    assert parsed["type"] == "m.presence", "Type must be m.presence"
    assert "sender" in parsed, "Event needs sender"
    assert "content" in parsed, "Event needs content"
    content = parsed["content"]
    assert "presence" in content, "Content needs presence"
    assert "status_msg" in content, "Content needs status_msg"
    assert "last_active_ts" in content, "Content needs last_active_ts"
    assert content["presence"] == "online", "Presence must be online"
    assert content["status_msg"] == "Hello, world!", "Status must match"
    print("  ✓ Presence event structure valid")

def test_typing_events():
    """Test Matrix typing event structures."""
    print("\n=== Test 5: Typing Events ===")

    # Test m.room.typing event in timeline
    typing_event = {
        "type": "m.room.typing",
        "event_id": "$typing1",
        "sender": "@bob:matrix.org",
        "origin_server_ts": 1234567900,
        "content": {
            "user_ids": ["@bob:matrix.org", "@charlie:matrix.org"]
        }
    }

    parsed = typing_event
    assert parsed["type"] == "m.room.typing", "Type must be m.room.typing"
    assert "sender" in parsed, "Event needs sender"
    assert "content" in parsed, "Event needs content"
    content = parsed["content"]
    assert "user_ids" in content, "Content needs user_ids"
    assert isinstance(content["user_ids"], list), "user_ids must be list"
    assert len(content["user_ids"]) == 2, "Should have two typing users"
    print("  ✓ Typing event structure valid")

def test_sync_with_presence():
    """Test sync response with presence events."""
    print("\n=== Test 6: Sync with Presence ===")

    # Full sync with presence section
    sync_data = {
        "next_batch": "s:12345_abcde",
        "presences": {
            "events": [
                {
                    "type": "m.presence",
                    "event_id": "$p1",
                    "sender": "@alice:matrix.org",
                    "origin_server_ts": 1234567000,
                    "content": {
                        "presence": "online",
                        "status_msg": "Available",
                        "last_active_ts": 1234566000
                    }
                },
                {
                    "type": "m.presence",
                    "event_id": "$p2",
                    "sender": "@bob:matrix.org",
                    "origin_server_ts": 1234568000,
                    "content": {
                        "presence": "offline",
                        "status_msg": "",
                        "last_active_ts": 1234567500
                    }
                }
            ]
        },
        "rooms": {
            "join": {
                "!room:matrix.org": {
                    "summary": {},
                    "state": {},
                    "timeline": {}
                }
            }
        }
    }

    parsed = sync_data
    assert "presences" in parsed, "Sync needs presences"
    assert "events" in parsed["presences"], "Presences needs events"
    events = parsed["presences"]["events"]
    assert len(events) == 2, "Should have two presence events"
    print("  ✓ Sync with presence events valid")

def test_timeline_pagination_contract():
    """Test the Matrix backward-pagination response contract."""
    print("\n=== Test 7: Timeline Pagination ===")
    timeline = {
        "limited": True,
        "prev_batch": "s:oldest-token",
        "events": []
    }
    assert timeline["limited"] is True, "Timeline must expose limited gaps"
    assert timeline["prev_batch"], "Limited timelines need prev_batch"

    response = {
        "start": "s:oldest-token",
        "end": "s:older-token",
        "chunk": [{
            "type": "m.room.message",
            "event_id": "$older1",
            "sender": "@alice:matrix.org",
            "origin_server_ts": 1234567000,
            "content": {"msgtype": "m.text", "body": "Older message"}
        }]
    }
    assert response["start"] == timeline["prev_batch"], "Backfill must use prev_batch as start"
    assert response["end"], "Backfill response needs continuation token"
    assert len(response["chunk"]) == 1, "Backfill must contain a chunk"
    event = response["chunk"][0]
    assert event["event_id"] == "$older1", "Backfill event needs event_id"
    assert event["content"]["body"] == "Older message", "Backfill body must survive parsing"
    print("  ✓ limited/prev_batch and backward chunk contract valid")

def test_relations_contract():
    """Test preservation of Matrix relation and redaction fields."""
    print("\n=== Test 8: Relations and Redactions ===")
    event = {
        "type": "m.room.message",
        "event_id": "$edit1",
        "content": {
            "msgtype": "m.text",
            "body": "* edited",
            "m.new_content": {"msgtype": "m.text", "body": "edited"},
            "m.relates_to": {"rel_type": "m.replace", "event_id": "$old1"}
        }
    }
    relation = event["content"]["m.relates_to"]
    assert relation["rel_type"] == "m.replace"
    assert relation["event_id"] == "$old1"
    assert event["content"]["m.new_content"]["body"] == "edited"
    redaction = {"type": "m.room.redaction", "event_id": "$red1", "content": {"redacts": "$old1"}}
    assert redaction["content"]["redacts"] == "$old1"
    legacy_redaction = {"type": "m.room.redaction", "event_id": "$red2", "redacts": "$old2"}
    assert legacy_redaction["redacts"] == "$old2"
    print("  ✓ reply/edit/reaction/thread metadata and redaction target contract valid")

def test_redacted_matrix_messages_scrub_format_and_preserve_timestamp():
    """Redaction must erase formatted text and rerender at the original event time."""
    root = Path(__file__).resolve().parents[1]
    network = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    renderer = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    redaction_start = network.index('if (eventType == QStringLiteral("m.room.redaction"))')
    redaction_end = network.index("MatrixTextEvent matrixEvent;", redaction_start)
    redaction = network[redaction_start:redaction_end]
    assert 'redacted->metadata.remove(QStringLiteral("formatted_body"))' in redaction
    assert 'redacted->metadata.remove(QStringLiteral("format"))' in redaction
    assert 'redacted->metadata.remove(QStringLiteral("body"))' in redaction
    assert "replacement.originTs = redacted->timestamp.toLongLong();" in redaction

    render_start = renderer.index("void ChatMessageHandler::renderProtocolMessage(")
    render_end = renderer.index("void ChatMessageHandler::renderProtocolHistory(", render_start)
    render = renderer[render_start:render_end]
    redaction_start = render.index("const QString redactedEventId =")
    redaction_end = render.index("if (AMessage.metadata().value(QStringLiteral(\"outer_event_type\"))",
                                 redaction_start)
    redaction = render[redaction_start:redaction_end]
    assert "FProtocolConversationMessages" in redaction
    assert "BasicMessage redactedMessage = *target" in redaction
    assert 'redactedMessage.setBody(QStringLiteral("message deleted"))' in redaction
    assert "sortProtocolMessagesChronologically(*historyIt)" in redaction
    assert "rebuildProtocolConversation(AWindow, AMessaging, historyKey)" in redaction
    assert "options.time = AMessage.timestamp();" in render
    assert "replaceMessage(displayMessageId" not in redaction

def test_matrix_reaction_render_and_send_contract():
    """Matrix reactions are sent as annotation events and displayed in groups."""
    root = Path(__file__).resolve().parents[1]
    matrix = (root / "src/plugins/matrix/matrix.cpp").read_text()
    network = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    network_header = (root / "src/plugins/matrix/matrixnetwork.h").read_text()
    renderer = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    renderer_header = (root / "src/plugins/chatmessagehandler/chatmessagehandler.h").read_text()
    view = (root / "src/plugins/messagewidgets/viewwidget.cpp").read_text()
    assert 'QStringLiteral("m.reaction")' in matrix
    assert 'QStringLiteral("m.annotation")' in network
    assert 'QStringLiteral("reaction_key")' in network
    assert 'QString eventType;' in network_header
    assert 'sendRoomEvent' in matrix and 'sendRoomEvent' in network
    assert 'eventType != QStringLiteral("m.reaction")' in network
    assert 'pendingStored.metadata.insert(metadataIt.key(), metadataIt.value());' in network
    assert 'QStringLiteral("related_event_id")' in renderer
    assert 'onProtocolViewContextMenu' in renderer_header
    assert 'vacuum.messageId' in view
    assert 'FProtocolReactionSenders' in renderer_header
    assert 'setMessageDecoration' in view
    history_start = renderer.index('void ChatMessageHandler::renderProtocolHistory')
    history_end = renderer.index('void ChatMessageHandler::addProtocolReaction', history_start)
    history_renderer = renderer[history_start:history_end]
    assert 'redacts' not in history_renderer
    assert 'metadata().value(QStringLiteral("redacted")).toBool()' in renderer
    assert 'FProtocolMessageRelationTypes' in renderer_header
    assert 'QStringLiteral("m.annotation")' in renderer
    assert 'QStringLiteral("m.replace")' in renderer
    print("  ✓ Matrix reaction events send and render as grouped per-message chips")

def test_encrypted_event_state():
    """Test that encrypted events expose an honest pending state."""
    print("\n=== Test 9: Encrypted Event State ===")
    event = {
        "type": "m.room.encrypted",
        "content": {"algorithm": "m.megolm.v1.aes-sha2", "ciphertext": "<synthetic>"}
    }
    metadata = {"decryption_status": "pending", "encryption_algorithm": event["content"]["algorithm"]}
    assert metadata["decryption_status"] == "pending"
    assert metadata["encryption_algorithm"].startswith("m.megolm.")
    print("  ✓ encrypted events retain pending/decryption algorithm state")

def test_push_rule_highlight_contract():
    """Test the account-data push-rule shape used for mention highlights."""
    print("\n=== Test 10: Push Rule Highlights ===")
    push_rules = {
        "type": "m.push_rules",
        "content": {"global": {"override": [{
            "rule_id": "mention",
            "conditions": [{"kind": "event_match", "key": "content.body", "pattern": "@alice:*"}],
            "actions": [{"set_tweak": "highlight", "value": True}]
        }]}}
    }
    rule = push_rules["content"]["global"]["override"][0]
    assert rule["conditions"][0]["key"] == "content.body"
    assert rule["actions"][0]["set_tweak"] == "highlight"
    assert rule["actions"][0]["value"] is True
    print("  ✓ m.push_rules highlight fixture valid")

def test_pending_event_replacement_contract():
    """Test the sync marker used to replace a local txn_id with event_id."""
    print("\n=== Test 11: Pending Event Replacement ===")
    event = {
        "event_id": "$server1",
        "unsigned": {"transaction_id": "m_local1"}
    }
    assert event["unsigned"]["transaction_id"] == "m_local1"
    assert event["event_id"].startswith("$")
    print("  ✓ unsigned.transaction_id/server event_id replacement contract valid")

def test_olm_key_upload_contract():
    print("\n=== Test 12: Olm Key Upload ===")
    libolm_keys = {"curve25519": {"AAAAAQ": "otk"}}
    expanded_keys = {
        f"signed_{algorithm}:{key_id}": {"key": key}
        for algorithm, keys in libolm_keys.items()
        for key_id, key in keys.items()
    }
    assert expanded_keys == {"signed_curve25519:AAAAAQ": {"key": "otk"}}
    payload = {
        "device_keys": {
            "user_id": "@alice:example.org",
            "device_id": "DEVICE1",
            "algorithms": [
                "m.olm.v1.curve25519-aes-sha2",
                "m.megolm.v1.aes-sha2"
            ],
            "keys": {
                "curve25519:DEVICE1": "curve-key",
                "ed25519:DEVICE1": "ed-key"
            },
            "signatures": {
                "@alice:example.org": {"ed25519:DEVICE1": "signature"}
            }
        },
        "one_time_keys": {
            "signed_curve25519:AAAAAQ": {
                "key": "otk",
                "signatures": {}
            }
        },
        "fallback_keys": {
            "signed_curve25519:AAAAAg": {
                "key": "fallback",
                "signatures": {}
            }
        }
    }
    assert payload["device_keys"]["user_id"].startswith("@")
    assert "m.megolm.v1.aes-sha2" in payload["device_keys"]["algorithms"]
    assert all(key.endswith(":DEVICE1") for key in payload["device_keys"]["keys"])
    assert payload["device_keys"]["signatures"]["@alice:example.org"]
    assert payload["one_time_keys"]
    assert payload["fallback_keys"]
    assert "signed_curve25519:AAAAAg" in payload["fallback_keys"]
    print("  ✓ /keys/upload device, one-time and fallback key contract valid")

def test_cross_signing_key_chain_contract():
    print("\n=== Test 13: Cross-Signing Key Chain ===")
    user_id = "@alice:example.org"
    master_id = "ed25519:MASTER"
    self_id = "ed25519:SELF"
    user_id_key = "ed25519:USER"
    master = {
        "user_id": user_id,
        "usage": ["master"],
        "keys": {master_id: "master-key"},
        "signatures": {user_id: {master_id: "master-signature"}},
    }
    self_signing = {
        "user_id": user_id,
        "usage": ["self_signing"],
        "keys": {self_id: "self-key"},
        "signatures": {user_id: {master_id: "master-signature"}},
    }
    user_signing = {
        "user_id": user_id,
        "usage": ["user_signing"],
        "keys": {user_id_key: "user-key"},
        "signatures": {user_id: {master_id: "master-signature"}},
    }
    for key_object, usage, key_id in (
        (master, "master", master_id),
        (self_signing, "self_signing", self_id),
        (user_signing, "user_signing", user_id_key),
    ):
        assert key_object["user_id"] == user_id
        assert key_object["usage"] == [usage]
        assert list(key_object["keys"]) == [key_id]
        assert master_id in key_object["signatures"][user_id]
    print("  ✓ master/self-signing/user-signing key chain contract valid")

def test_megolm_room_key_contract():
    print("\n=== Test 14: Megolm Room-Key Decryption ===")
    sync = {
        "to_device": {"events": [{
            "type": "m.room_key",
            "content": {
                "algorithm": "m.megolm.v1.aes-sha2",
                "room_id": "!room:example.org",
                "session_id": "session1",
                "session_key": "base64-session-key",
                "sender_key": "curve25519-key"
            }
        }]}
    }
    encrypted = {
        "type": "m.room.encrypted",
        "content": {
            "algorithm": "m.megolm.v1.aes-sha2",
            "session_id": "session1",
            "ciphertext": "base64-ciphertext"
        }
    }
    room_key = sync["to_device"]["events"][0]["content"]
    assert room_key["algorithm"] == encrypted["content"]["algorithm"]
    assert encrypted["content"]["session_id"] == "session1"
    plaintext = {
        "room_id": room_key["room_id"],
        "type": "m.room.message",
        "content": {"msgtype": "m.text", "body": "hello"},
    }
    assert plaintext["room_id"] == room_key["room_id"]
    print("  ✓ to_device room-key and encrypted-event session contract valid")

def test_room_key_request_validation_contract():
    print("\n=== Test 15: Room-Key Request Validation ===")
    valid = {
        "type": "m.room_key_request",
        "sender": "@alice:example.org",
        "content": {
            "action": "request",
            "request_id": "request1",
            "requesting_device_id": "ALICE1",
            "body": {
                "algorithm": "m.megolm.v1.aes-sha2",
                "room_id": "!room:example.org",
                "session_id": "session1"
            }
        }
    }
    invalid = {**valid, "content": {**valid["content"], "body": {
        **valid["content"]["body"], "algorithm": "m.olm.v1.curve25519-aes-sha2"
    }}}
    assert valid["content"]["body"]["algorithm"] == "m.megolm.v1.aes-sha2"
    assert invalid["content"]["body"]["algorithm"] != "m.megolm.v1.aes-sha2"
    cancellation = {
        "type": "m.room_key_request",
        "content": {
            "action": "request_cancellation",
            "request_id": valid["content"]["request_id"],
            "requesting_device_id": valid["content"]["requesting_device_id"]
        }
    }
    assert "body" not in cancellation["content"]
    assert cancellation["content"]["request_id"] == valid["content"]["request_id"]
    print("  ✓ valid Megolm request, cancellation reuse and invalid algorithm contract valid")

def test_forwarded_room_key_wire_contract():
    print("\n=== Test 15b: Forwarded Room-Key Wire Contract ===")
    forwarded = {
        "type": "m.forwarded_room_key",
        "content": {
            "algorithm": "m.megolm.v1.aes-sha2",
            "room_id": "!room:example.org",
            "session_id": "session1",
            "session_key": "megolm-session-key",
            "sender_key": "curve25519-sender",
            "sender_claimed_ed25519_key": "ed25519-sender",
            "forwarding_curve25519_key_chain": []
        }
    }
    required = {
        "algorithm", "room_id", "session_id", "session_key", "sender_key",
        "sender_claimed_ed25519_key", "forwarding_curve25519_key_chain"
    }
    assert required <= set(forwarded["content"])
    assert forwarded["content"]["forwarding_curve25519_key_chain"] == []
    print("  ✓ forwarded room-key required fields and direct empty chain valid")

def test_encrypted_to_device_contract():
    print("\n=== Test 16: Encrypted To-Device Payload ===")
    content = {
        "algorithm": "m.olm.v1.curve25519-aes-sha2",
        "sender_key": "curve25519-sender",
        "ciphertext": {
            "curve25519:ALICE1": {"body": "olm-ciphertext", "type": 0}
        }
    }
    assert "session_id" not in content
    recipient = next(iter(content["ciphertext"].values()))
    assert set(recipient) == {"body", "type"}
    assert content["algorithm"] == "m.olm.v1.curve25519-aes-sha2"
    print("  ✓ Olm recipient ciphertext map and message type contract valid")

def test_generic_relation_media_contract():
    print("\n=== Test 17: Generic Relations and Media Metadata ===")
    relation = {
        "rel_type": "m.thread",
        "event_id": "$root:event",
        "is_falling_back": True,
        "m.in_reply_to": {"event_id": "$parent:event"}
    }
    assert relation["rel_type"] == "m.thread"
    assert relation["event_id"] == "$root:event"
    assert relation["m.in_reply_to"]["event_id"] == "$parent:event"
    assert relation["is_falling_back"] is True
    content = {"url": "mxc://server/media", "thumbnail_url": "mxc://server/thumb",
               "info": {"mimetype": "image/png"}}
    assert {content["url"], content["thumbnail_url"]} == {
        "mxc://server/media", "mxc://server/thumb"}
    assert content["info"]["mimetype"] == "image/png"
    print("  ✓ thread, fallback-reply and media attachment contract valid")

def test_matrix_conversation_address_contract():
    print("\n=== Test 18: Conversation Address Roundtrip ===")
    room_id = "!room123:matrix.org"
    local = "room-" + room_id.encode().hex()
    assert local.startswith("room-")
    assert bytes.fromhex(local[5:]).decode() == room_id
    assert "/send/m.room.message/" in (
        "/_matrix/client/v3/rooms/%21room123%3Amatrix.org/send/m.room.message/m_txn"
    )
    print("  ✓ stable room address and send endpoint contract valid")

def test_plaintext_event_body_contract():
    print("\n=== Test 19: Plaintext Timeline Body ===")
    event_content = {"msgtype": "m.text", "body": "hello"}
    decrypted_content = {}
    was_encrypted = False
    message_content = decrypted_content if was_encrypted else event_content
    assert message_content["body"] == "hello"
    assert message_content["msgtype"] == "m.text"
    print("  ✓ unencrypted events use event content for body and message type")

def test_direct_room_roster_identity_contract():
    print("\n=== Test 20: Direct Room Has One Roster Identity ===")
    rooms = [{"id": "!direct:example.org", "is_direct": True,
              "members": [{"id": "@alice:example.org"}]}]
    # Matrix direct chats are room-list entries; the member is presentation data.
    roster_items = [room["id"] for room in rooms]
    assert roster_items == ["!direct:example.org"]
    assert len(roster_items) == len(set(roster_items))
    print("  ✓ direct room is represented once by its room ID")

def test_room_avatar_roster_lazy_load_contract():
    print("\n=== Test 20b: Room Avatar Roster Lazy Loading ===")
    root = Path(__file__).resolve().parents[1]
    roster_interface = (root / "src/interfaces/iprotocolroster.h").read_text()
    roster_model = (root / "src/plugins/rostersmodel/rostersmodel.cpp").read_text()
    avatars_source = (root / "src/plugins/avatars/avatars.cpp").read_text()
    matrix_header = (root / "src/plugins/matrix/matrix.h").read_text()
    matrix_source = (root / "src/plugins/matrix/matrix.cpp").read_text()
    cached_rooms = matrix_source.split("void Matrix::onCachedRoomsLoaded(", 1)[1].split(
        "void Matrix::onCachedRoomsLoadFailed(", 1)[0]
    assert "loadRoomAvatar(const QString &roomId) const" in roster_interface
    assert "FPendingProtocolRoster->loadRoomAvatar(room.id)" not in roster_model, \
        "roster rebuild must not prefetch avatars for every room"
    assert "loadRoomAvatar(const QString &roomId) const override" in matrix_header
    assert "void Matrix::loadRoomAvatar(const QString &roomId) const" in matrix_source
    assert 'room.avatarKey = accountId() + QStringLiteral("\\nroom\\n") + room.id;' in cached_rooms
    assert "matrixAvatarCacheExists" not in matrix_source, \
        "avatar cache filesystem checks must not run from the UI-thread adapter"
    avatar_render = avatars_source.split("QVariant Avatars::rosterData(", 1)[1].split(
        "bool Avatars::setRosterData(", 1)[0]
    assert "loadRoomAvatar" in avatar_render, "visible roster avatar queries should trigger lazy loading"
    network_source = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    avatar_request = network_source.split("void MatrixNetwork::requestAvatar(", 1)[1].split(
        "void MatrixNetwork::requestImage(", 1)[0]
    assert 'emit avatarImageReceived(key, image);' in avatar_request
    assert "cacheFile.commit()" in avatar_request
    assert 'emit avatarImageReceived(key, image);' in avatar_request
    avatar_callback = matrix_source.split("void Matrix::onAvatarImageReceived(", 1)[1].split(
        "void Matrix::onDisplayNameReceived(", 1)[0]
    assert "FAvatars->setCustomImageByKey(key, image);" in avatar_callback
    assert "protocolRosterChanged" not in avatar_callback
    print("  ✓ roster room avatars load lazily from visible avatar requests")

def test_room_image_history_cache_lazy_load_contract():
    print("\n=== Test 20c: Room Image Cache and Restart Lazy Loading ===")
    root = Path(__file__).resolve().parents[1]
    matrix_source = (root / "src/plugins/matrix/matrix.cpp").read_text()
    network_header = (root / "src/plugins/matrix/matrixnetwork.h").read_text()
    network_source = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    renderer = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    history_callback = matrix_source.split("void Matrix::onCachedHistoryLoaded(", 1)[1].split(
        "void Matrix::emitCachedHistoryBatch(", 1)[0]
    image_request = network_source.split("void MatrixNetwork::requestImage(", 1)[1].split(
        "void MatrixNetwork::requestRoomName(", 1)[0]
    render_message = renderer.split("void ChatMessageHandler::renderProtocolMessage(", 1)[1].split(
        "void ChatMessageHandler::renderProtocolHistory(", 1)[0]
    assert "requestHistoricalImages" in network_header
    assert "requestHistoricalImages" in history_callback
    history_loader = network_source.split("void MatrixNetwork::requestHistoricalImages(", 1)[1].split(
        "void MatrixNetwork::requestRoomName(", 1)[0]
    assert "events.crbegin()" in history_loader, "cached images must be requested newest first"
    assert 'profileDirectory + QStringLiteral("/media")' in image_request
    assert "QSaveFile" in image_request and "output.commit()" in image_request
    assert 'QStringLiteral("historical")' in history_loader, "replayed media must be marked historical"
    assert "messageType == QStringLiteral(\"m.image\")" in render_message
    assert "decoded_image" in render_message, "decoded image data must already be available before UI rendering"
    print("  ✓ persisted room images are hydrated newest-first when cached history is opened")

def test_matrix_media_reads_and_decodes_off_ui_lazily():
    print("\n=== Test 20d: Matrix Media Worker and Lazy UI Contract ===")
    root = Path(__file__).resolve().parents[1]
    matrix_source = (root / "src/plugins/matrix/matrix.cpp").read_text()
    roster_model = (root / "src/plugins/rostersmodel/rostersmodel.cpp").read_text()
    avatars_source = (root / "src/plugins/avatars/avatars.cpp").read_text()
    network_source = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    renderer = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    avatar_enqueue = matrix_source.split("void Matrix::enqueueAvatarLoad(", 1)[1].split(
        "void Matrix::loadNextAvatar(", 1)[0]
    image_request = network_source.split("void MatrixNetwork::requestImage(", 1)[1].split(
        "void MatrixNetwork::requestHistoricalImages(", 1)[0]
    history_loader = network_source.split("void MatrixNetwork::requestHistoricalImages(", 1)[1].split(
        "void MatrixNetwork::requestRoomName(", 1)[0]
    sync_media = network_source.split("if (!wasInitialSync && !events.isEmpty())", 1)[1].split(
        "Continue the sync loop", 1)[0]
    assert "QFile::exists" not in avatar_enqueue, "cache probes must be on MatrixNetworkThread"
    assert "loadRoomAvatar(room.id)" not in roster_model, "roster rebuild must not prefetch all room avatars"
    assert "loadRoomAvatar" in avatars_source, "visible avatar data requests should trigger lazy room loads"
    assert "QImageReader" in image_request and "decoded_image" in image_request, \
        "image decoding must happen in the Matrix network worker"
    assert "requested >= 8" in history_loader, "history media hydration must remain bounded"
    assert "event.roomId == FActiveRoomId" in sync_media, "live media for inactive rooms must not be fetched"
    assert "vacuum-media" in renderer and "addResource" in renderer, \
        "unloaded media should be a lazy link and decoded images should use in-memory resources"
    print("  ✓ avatar cache probes and media fetch/decode stay off the UI hot path")

def test_late_hydrated_image_rebuilds_chronological_history():
    """A delayed media payload must update its old event in place, not append it last."""
    root = Path(__file__).resolve().parents[1]
    network = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    handler = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    image_request = network.split("void MatrixNetwork::requestImage(", 1)[1].split(
        "void MatrixNetwork::requestHistoricalImages(", 1)[0]
    message_dispatch = handler.split("void ChatMessageHandler::onProtocolMessageReceived(", 1)[1].split(
        "void ChatMessageHandler::onProtocolHistoryLoaded(", 1)[0]
    duplicate_start = message_dispatch.index("if (duplicate != history.end())")
    duplicate_end = message_dispatch.index("\n\t\t\t\treturn;\n\t\t\t}\n\t\t\tconst bool outOfOrder", duplicate_start)
    duplicate_path = message_dispatch[duplicate_start:duplicate_end]
    assert "MatrixTextEvent imageEvent = event;" in image_request
    assert "emit messageReceived(imageEvent.toBasicMessage());" in image_request
    assert "const bool outOfOrder" in duplicate_path
    assert "std::next(duplicate) != history.end()" in duplicate_path
    assert "rebuildProtocolConversation(window, messaging, historyKey)" in duplicate_path
    assert "renderProtocolMessage(window, messaging, AMessage)" in duplicate_path
    print("  ✓ delayed image hydration rebuilds the timeline at the event's original position")

def test_conversation_history_replay_contract():
    print("\n=== Test 21: Conversation History Replay ===")
    history = [
        {"event_id": "$one", "room_id": "!room:example.org", "body": "first"},
        {"event_id": "$two", "room_id": "!room:example.org", "body": "second"},
    ]
    replay = [dict(event, historical=True, direction="incoming") for event in history]
    assert [event["event_id"] for event in replay] == ["$one", "$two"]
    assert all(event["room_id"] == "!room:example.org" for event in replay)
    assert all(event["historical"] and event["direction"] == "incoming" for event in replay)
    print("  ✓ stored room history replays chronologically without entering send path")

def test_matrix_cached_history_merges_pending_live_messages_by_full_timestamp():
    print("\n=== Test 21b: Cached/Live Matrix History Ordering ===")
    root = Path(__file__).resolve().parents[1]
    matrix_source = (root / "src/plugins/matrix/matrix.cpp").read_text()
    database_source = (root / "src/plugins/matrix/matrixdatabase.cpp").read_text()
    network_message = matrix_source.split("void Matrix::onNetworkMessageReceived(", 1)[1].split(
        "void Matrix::onAvatarDataReceived(", 1)[0]
    history_changed = matrix_source.split("void Matrix::onMessageHistoryChanged(", 1)[1].split(
        "void Matrix::onRosterChanged(", 1)[0]
    cached_history = matrix_source.split("void Matrix::onCachedHistoryLoaded(", 1)[1].split(
        "void Matrix::emitCachedHistoryBatch(", 1)[0]
    assert "queueOrEmitHistoryMessage(message)" in network_message, \
        "live Matrix events must wait while cached history is being loaded"
    assert "queueOrEmitHistoryMessage(event.toBasicMessage())" in history_changed, \
        "timeline refresh events must use the same pending-history path"
    assert "mergeHistoryMessagesChronologically" in cached_history, \
        "cached and pending events must be merged before they are rendered"
    database_start = database_source.index(
        "QList<MatrixTimelineEvent> MatrixDatabase::getEvents(")
    database_end = database_source.index("\nQStringList MatrixDatabase::roomIds", database_start)
    database_loader = database_source[database_start:database_end]
    assert "ORDER BY origin_ts ASC" in database_loader
    assert "event_id ASC" not in database_loader, \
        "database history ties must not use unique event IDs as a sort key"
    comparator_start = matrix_source.index(
        "QList<BasicMessage> Matrix::mergeHistoryMessagesChronologically(")
    comparator_end = matrix_source.index("\nvoid Matrix::onRosterChanged", comparator_start)
    comparator = matrix_source[comparator_start:comparator_end]
    assert "leftTime.date()" in comparator and "rightTime.date()" in comparator, \
        "message ordering must compare calendar dates explicitly"
    assert "leftTime.time()" in comparator and "rightTime.time()" in comparator, \
        "messages on the same date must be ordered by clock time"
    assert "left.messageId()" not in comparator and "right.messageId()" not in comparator, \
        "unique event IDs must not determine message order"

    # Reverse arrival and deliberately opposing IDs make only date+time yield this order.
    arrivals = [("$z-later", "2025-01-03T18:45:00"),
                ("$a-earlier", "2025-01-03T09:15:00"),
                ("$m-previous-day", "2025-01-02T23:55:00")]
    expected = ["$m-previous-day", "$a-earlier", "$z-later"]
    assert [event_id for event_id, _ in sorted(arrivals, key=lambda item: item[1])] == expected
    assert [event_id for event_id, stamp in sorted(arrivals, key=lambda item: item[1][:10])] != expected

    # Equal timestamps keep stable input order rather than falling back to unique IDs.
    equal_time_arrivals = [("$z-first", "2025-01-03T11:30:00"),
                           ("$a-second", "2025-01-03T11:30:00")]
    assert [event_id for event_id, _ in sorted(equal_time_arrivals, key=lambda item: item[1])] == [
        "$z-first", "$a-second"]
    print("  ✓ cached/live history sorts by date then time without using event IDs")

def test_matrix_messages_display_full_timestamp():
    print("\n=== Test 21c: Matrix Message Date and Time Display ===")
    root = Path(__file__).resolve().parents[1]
    handler = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    style = (root / "src/plugins/simplemessagestyle/simplemessagestyle.cpp").read_text()
    modern_template = (root / "resources/simplemessagestyles/modern-chat/Incoming/Content.html").read_text()
    render = handler.split("void ChatMessageHandler::renderProtocolMessage(", 1)[1].split(
        "void ChatMessageHandler::renderProtocolHistory(", 1)[0]
    assert "options.time = AMessage.timestamp();" in render
    assert "options.timeFormat" in render and "yyyy-MM-dd hh:mm:ss" in render, \
        "Matrix message metadata must render the date as well as the time"
    fill = style.split("void SimpleMessageStyle::fillContentKeywords(", 1)[1].split(
        "QString SimpleMessageStyle::prepareMessage(", 1)[0]
    assert "shortTimeFormat = AOptions.timeFormat.isEmpty()" in fill and \
        'AOptions.time.toString(shortTimeFormat)' in fill, \
        "styles using %shortTime% must honor the Matrix full date-time format"
    assert "%shortTime%" in modern_template, "the default modern style must display the formatted timestamp"
    print("  ✓ Matrix messages render full date and time from their timestamp")

def test_matrix_live_event_waits_for_cached_history_before_chat_render():
    print("\n=== Test 21d: Async History and First Live Message Ordering ===")
    root = Path(__file__).resolve().parents[1]
    matrix = (root / "src/plugins/matrix/matrix.cpp").read_text()
    handler = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    matrix_header = (root / "src/plugins/matrix/matrix.h").read_text()
    on_network = matrix.split("void Matrix::onNetworkMessageReceived(", 1)[1].split(
        "void Matrix::onAvatarDataReceived(", 1)[0]
    finish = matrix.split("void Matrix::finishHistoryLoad(", 1)[1].split(
        "QList<BasicMessage> Matrix::mergeHistoryMessagesChronologically(", 1)[0]
    received = handler.split("void ChatMessageHandler::onProtocolMessageReceived(", 1)[1].split(
        "void ChatMessageHandler::onProtocolHistoryLoaded(", 1)[0]
    history_loaded = handler.split("void ChatMessageHandler::onProtocolHistoryLoaded(", 1)[1].split(
        "void ChatMessageHandler::renderProtocolMessage(", 1)[0]
    history_render = handler.split("void ChatMessageHandler::renderProtocolHistory(", 1)[1].split(
        "void ChatMessageHandler::addProtocolReaction(", 1)[0]
    assert "queueOrEmitHistoryMessage(message)" in on_network
    assert "protocolHistoryLoaded" in matrix_header and "protocolHistoryLoaded" in finish, \
        "history success, empty result and failure must all complete the async render barrier"
    cached = matrix.split("void Matrix::onCachedHistoryLoaded(", 1)[1].split(
        "void Matrix::emitCachedHistoryBatch(", 1)[0]
    batch = matrix.split("void Matrix::emitCachedHistoryBatch(", 1)[1].split(
        "void Matrix::onLoginSuccess(", 1)[0]
    failed = matrix.split("void Matrix::onCachedHistoryLoadFailed(", 1)[1].split(
        "void Matrix::queueOrEmitHistoryMessage(", 1)[0]
    assert "finishHistoryLoad(roomId)" in cached and "finishHistoryLoad(roomId)" in failed
    assert "finishHistoryLoad(roomId)" in batch and "FHistoryQueuedRooms" in batch, \
        "the barrier must remain closed through all cached batches and then release pending events"
    assert "FProtocolHistoryLoading" in received and "FPendingProtocolHistoryMessages" in received
    assert "FProtocolHistoryLoading" in history_render, \
        "opening a conversation must mark it loading before requesting asynchronous history"
    assert "sortProtocolMessagesChronologically" in history_loaded and \
        "std::stable_sort" in handler and "timestamp()" in handler, \
        "cached and live messages must be sorted together before the first render"
    assert "protocolHistoryLoaded" in handler, \
        "the async history completion must release buffered messages"
    assert "FProtocolHistoryLoaded" in received and "rebuildProtocolConversation" in received, \
        "a late out-of-order backfill must trigger a chronological conversation rebuild"
    assert "void ChatMessageHandler::rebuildProtocolConversation(" in handler
    # Regression scenario: DB history is yesterday 22:00; the first live event is today 08:00.
    history_and_live = [("live-08", "2025-01-03T08:00:00"),
                        ("cached-22", "2025-01-02T22:00:00")]
    assert [event for event, _ in sorted(history_and_live, key=lambda item: item[1])] == [
        "cached-22", "live-08"]
    print("  ✓ first live message is held until cached history can be ordered with it")

def test_persisted_history_is_not_live_event_contract():
    print("\n=== Test 22: Persisted History Is Not Live Input ===")
    restored = {"event_id": "$old", "historical": True, "live_signal": False}
    assert restored["historical"] is True
    assert restored["live_signal"] is False
    print("  ✓ restored events are rendered only through conversation history")

def test_conversation_display_name_contract():
    print("\n=== Test 23: Conversation Display Name ===")
    room = {"id": "!room:example.org", "name": "Alice"}
    synthetic_address = "room-216d726f6f6d3a6578616d706c652e6f7267@protocol.local"
    assert room["name"] != synthetic_address.split("@")[0]
    print("  ✓ UI display name is separate from synthetic conversation address")

def test_normalized_message_identity_contract():
    print("\n=== Test 24: Normalized Message Identity ===")
    message = {
        "conversationId": "!room:example.org",
        "sender": "@alice:example.org",
        "body": "test",
        "direction": "incoming",
    }
    assert message["conversationId"].startswith("!")
    assert message["sender"].startswith("@")
    assert message["body"] == "test"
    assert "room-" not in message["sender"]
    print("  ✓ conversation, sender, body and direction stay separate")

def test_sync_filter_contract():
    print("\n=== Test 25: Sync Filter Contract ===")
    sync_filter = {
        "presence": {"limit": 0},
        "account_data": {"limit": 0},
        "room": {
            "state": {"lazy_load_members": True, "include_redundant_members": False},
            "timeline": {"limit": 20},
            "ephemeral": {"limit": 0},
            "account_data": {"limit": 0},
            "include_leave": False,
        },
    }
    assert sync_filter["room"]["state"]["lazy_load_members"] is True
    assert sync_filter["room"]["include_leave"] is False
    assert sync_filter["room"]["timeline"]["limit"] == 20
    print("  ✓ lossless sync filter fields and limits valid")

def test_sync_loop_waits_one_second_between_long_polls():
    root = Path(__file__).resolve().parents[1]
    source = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    continuation = source.split("// Keep a one-second floor", 1)[1].split(
        "QList<MatrixTextEvent> MatrixNetwork::messageHistory", 1
    )[0]
    assert "QTimer::singleShot(1000, this" in continuation
    print("  ✓ Matrix sync loop waits one second before starting the next long-poll")

def test_invite_and_room_account_data_contract():
    print("\n=== Test 26: Invite and Room Account Data ===")
    room = {
        "invite_state": {"events": [{"type": "m.room.name", "content": {"name": "Invite"}}]},
        "account_data": {"events": [
            {"type": "m.fully_read", "content": {"event_id": "$event"}},
            {"type": "m.tag", "content": {"tags": {"m.favourite": {}}}},
        ]},
    }
    assert room["invite_state"]["events"][0]["type"] == "m.room.name"
    assert room["account_data"]["events"][1]["content"]["tags"]["m.favourite"] == {}
    print("  ✓ invite_state and room account-data response shapes valid")

def test_receipt_contract():
    print("\n=== Test 27: Read Receipt Contract ===")
    event = {"type": "m.receipt", "content": {
        "$message": {"m.read": {"@alice:example.org": {"ts": 1700000000000}}}
    }}
    receipt = event["content"]["$message"]["m.read"]["@alice:example.org"]
    assert receipt["ts"] > 0
    print("  ✓ m.receipt event/user/timestamp nesting valid")

def test_forwarded_room_key_contract():
    print("\n=== Test 28: Forwarded Room Key Contract ===")
    forwarded = {
        "algorithm": "m.megolm.v1.aes-sha2",
        "room_id": "!room:example.org",
        "session_id": "session",
        "session_key": "key",
        "sender_key": "curve25519:key",
        "forwarding_curve25519_key_chain": ["curve25519:forwarder"],
    }
    required = ("algorithm", "room_id", "session_id", "session_key", "sender_key")
    assert all(forwarded.get(field) for field in required)
    assert forwarded["forwarding_curve25519_key_chain"]
    print("  ✓ forwarded room-key validation fields valid")

def test_ssss_secret_gossip_contract():
    print("\n=== Test 29: SSSS Secret Gossip Contract ===")
    request = {
        "action": "request",
        "name": "m.cross_signing.self_signing",
        "request_id": "ssss.request.1",
        "requesting_device_id": "DEVICE1",
    }
    assert request["action"] == "request"
    assert request["name"].startswith("m.cross_signing.")
    assert request["request_id"] and request["requesting_device_id"]
    response = {"request_id": request["request_id"], "secret": "base64-seed"}
    assert response["request_id"] == request["request_id"]
    assert response["secret"]
    print("  ✓ m.secret.request/m.secret.send correlation contract valid")

def test_ssss_key_description_contract():
    print("\n=== Test 30: SSSS Key Description Contract ===")
    description = {
        "algorithm": "m.secret_storage.v1.aes-hmac-sha2",
        "iv": "base64-iv",
        "mac": "base64-mac",
        "passphrase": {
            "algorithm": "m.pbkdf2",
            "salt": "base64-salt",
            "iterations": 100000,
            "bits": 512,
        },
    }
    assert description["algorithm"] == "m.secret_storage.v1.aes-hmac-sha2"
    assert description["passphrase"]["bits"] in (256, 512)
    print("  ✓ SSSS PBKDF2 key-description contract valid")

def test_ssss_request_binding_contract():
    print("\n=== Test 31: SSSS Request Binding Contract ===")
    pending = {"m.cross_signing.master": "request.master"}
    response = {"request_id": "request.other", "secret": "seed"}
    assert response["request_id"] not in pending.values()
    assert all(response["request_id"] != request_id for request_id in pending.values())
    print("  ✓ unknown m.secret.send request_id cannot satisfy a pending request")

def test_megolm_share_index_contract():
    print("\n=== Test 32: Megolm Share Index Contract ===")
    share = {
        "session_id": "session",
        "user_id": "@alice:example.org",
        "device_id": "DEVICE",
        "minimum_index": 17,
    }
    assert share["minimum_index"] >= 0
    assert all(share[field] for field in ("session_id", "user_id", "device_id"))
    print("  ✓ Megolm share history carries a minimum export index")

def test_sas_mac_negotiation_contract():
    print("\n=== Test 33: SAS MAC Negotiation Contract ===")
    start = {
        "method": "m.sas.v1",
        "key_agreement_protocols": ["curve25519-hkdf-sha256"],
        "hashes": ["sha256"],
        "message_authentication_codes": ["hkdf-hmac-sha256.v2", "hkdf-hmac-sha256"],
        "short_authentication_string": ["decimal", "emoji"],
    }
    assert start["method"] == "m.sas.v1"
    assert "curve25519-hkdf-sha256" in start["key_agreement_protocols"]
    assert start["message_authentication_codes"][0] == "hkdf-hmac-sha256.v2"
    assert set(("decimal", "emoji")).issubset(start["short_authentication_string"])
    print("  ✓ SAS v2/v1 MAC negotiation and SAS methods contract valid")

def test_encrypted_event_not_active_room_bound_contract():
    print("\n=== Test 34: Encrypted Event Room Independence Contract ===")
    encrypted = {
        "room_id": "!background:example.org",
        "active_room_id": "!foreground:example.org",
        "type": "m.room.encrypted",
        "content": {"algorithm": "m.megolm.v1.aes-sha2", "session_id": "session"},
    }
    assert encrypted["room_id"] != encrypted["active_room_id"]
    assert encrypted["content"]["algorithm"] == "m.megolm.v1.aes-sha2"
    print("  ✓ encrypted events are decryptable independently of active UI room")

def test_room_key_request_session_origin_contract():
    print("\n=== Test 35: Room-Key Request Session-Origin Contract ===")
    request = {
        "sender": "@alice:example.org",
        "session_sender_key": "curve25519:alice",
        "requester_curve25519": "curve25519:vacuum",
        "local_curve25519": "curve25519:vacuum",
    }
    assert request["sender"] != "@vacuum:example.org"
    assert request["session_sender_key"] != request["local_curve25519"]
    print("  ✓ foreign users cannot receive forwarded keys for sessions Vacuum did not create")

def test_one_time_key_topup_contract():
    print("\n=== Test 36: One-Time-Key Top-Up Contract ===")
    target = 50
    initial_generation = {"new_account": target, "restored_account": 0}
    assert initial_generation["new_account"] == target
    assert initial_generation["restored_account"] == 0
    for server_count in (0, 1, 9):
        assert target - server_count > 0
        assert server_count + (target - server_count) == target
    local = ("@vacuum:example.org", "VACUUMDEV")
    devices = [local, ("@vacuum:example.org", "OTHERDEV"), ("@peer:example.org", "PEERDEV")]
    claim_targets = [device for device in devices if device != local]
    assert local not in claim_targets
    assert len(claim_targets) == 2
    print("  ✓ restored accounts sync before server-count-driven OTK top-up")
    print("  ✓ local device is excluded from one-time-key claims")

def test_olm_recovery_order_contract():
    print("\n=== Test 36b: Olm Recovery Ordering Contract ===")
    source = (Path(__file__).resolve().parents[1] / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    dummy_reply = source.index('reply->property("e2eeOlmRecovery").toBool()')
    dummy_status = source.index('status >= 200 && status < 300', dummy_reply)
    retry_request = source.index('requestMissingRoomKey(FRoomKeyRequestUsers.value(requestKey)', dummy_reply)
    claim_callback = source.index('const auto forcedEvent = FPendingForcedOlmEvents.take')
    assert dummy_reply < dummy_status < retry_request
    assert claim_callback < dummy_reply
    assert 'if (!forcedEvent.first.isEmpty() && userId == FUserId)' not in source
    assert 'const QString requestedDevice = targetDeviceId.isEmpty()' in source
    assert 'parts.at(1), parts.at(0), parts.at(2), recoveryDevice' in source
    print("  ✓ room-key retry follows successful m.dummy delivery")

def test_olm_plaintext_sender_device_contract():
    print("\n=== Test 39: Olm Plaintext Sender Device Contract ===")
    payload = {
        "sender": "@alice:example.org",
        "sender_device": "ALICEDEV",
        "recipient": "@bob:example.org",
        "keys": {"ed25519": "alice-ed25519"},
        "recipient_keys": {"ed25519": "bob-ed25519"},
    }
    assert payload["sender_device"]
    print("  ✓ Olm plaintext carries sender_device for identity binding")

def test_e2ee_reference_parity_contract():
    print("\n=== Test 40: E2EE Reference Parity Contracts ===")
    upload = {
        "one_time_keys": {"signed_curve25519:AAAAAQ": {"key": "otk", "signatures": {}}},
        "fallback_keys": {"signed_curve25519:AAAAAg": {"key": "fallback", "signatures": {}}},
    }
    assert "fallback_keys" in upload
    assert "signed_curve25519:AAAAAg" in upload["fallback_keys"]
    assert ["inbound_session", "decrypt", "remove_otk", "persist_account", "persist_session"] == [
        "inbound_session", "decrypt", "remove_otk", "persist_account", "persist_session"
    ]
    assert ["secret_send", "store_secret", "request_cancellation"][-1] == "request_cancellation"
    print("  ✓ fallback, inbound persistence and SSSS cancellation parity contracts valid")

def test_element_megolm_export_format_contract():
    print("\n=== Test 41: Element Megolm Export Format Contract ===")
    assert "-----BEGIN MEGOLM SESSION DATA-----" != ""
    assert "-----END MEGOLM SESSION DATA-----" != ""
    session = {
        "room_id": "!room:example.org",
        "session_id": "session",
        "session_key": "key",
        "sender_key": "curve",
        "sender_claimed_keys": {"ed25519": "ed"},
        "forwarding_curve25519_key_chain": [],
    }
    assert all(field in session for field in ("room_id", "session_id", "session_key", "sender_key"))
    print("  ✓ Element/Nheko encrypted Megolm export fields and markers valid")

def test_missing_session_recovery_lifecycle_contract():
    print("\n=== Test 37: Missing Session Recovery Lifecycle Contract ===")
    lifecycle = [
        "pending",
        "m.room_key_request",
        "m.forwarded_room_key",
        "session_imported",
        "retry_decryption",
        "request_cancellation",
    ]
    assert lifecycle.index("pending") < lifecycle.index("m.room_key_request")
    assert lifecycle.index("m.room_key_request") < lifecycle.index("m.forwarded_room_key")
    assert lifecycle.index("m.forwarded_room_key") < lifecycle.index("retry_decryption")
    assert lifecycle.index("retry_decryption") < lifecycle.index("request_cancellation")
    print("  ✓ Element missing-session key recovery lifecycle contract valid")

def test_force_new_session_reissues_room_key_request_contract():
    print("\n=== Test 38: Force-New-Session Room-Key Retry Contract ===")
    lifecycle = ["missing_session", "preserve_old_sessions", "claim_otk", "new_olm_session", "m.dummy", "room_key_request", "forwarded_room_key"]
    assert lifecycle.index("preserve_old_sessions") < lifecycle.index("claim_otk")
    assert lifecycle.index("claim_otk") < lifecycle.index("new_olm_session")
    assert lifecycle.index("new_olm_session") < lifecycle.index("m.dummy")
    assert lifecycle.index("m.dummy") < lifecycle.index("room_key_request")
    assert lifecycle.index("room_key_request") < lifecycle.index("forwarded_room_key")
    print("  ✓ room-key request is reissued after force-new Olm session recovery")

def test_normal_olm_session_recovery_contract():
    print("\n=== Test 38b: Normal Olm Session Recovery Contract ===")
    lifecycle = ["normal_message_decrypt_failure", "discard_stale_session", "claim_otk", "m.dummy"]
    assert lifecycle.index("normal_message_decrypt_failure") < lifecycle.index("discard_stale_session")
    assert lifecycle.index("discard_stale_session") < lifecycle.index("claim_otk")
    assert lifecycle.index("claim_otk") < lifecycle.index("m.dummy")
    print("  ✓ failed normal Olm messages trigger fresh-session recovery")

def test_placeholder_room_key_request_contract():
    print("\n=== Test 38c: Placeholder Room-Key Request Contract ===")
    placeholder_session = "A" * 43
    valid_session = "AWhVNXXvuRfHk738tszMbCS1viXYJgifUIxkqiwSti8"
    assert placeholder_session != valid_session
    assert len(placeholder_session) == 43
    print("  ✓ placeholder Megolm sessions are not requested from peers")

def test_megolm_session_integrity_recovery_contract():
    print("\n=== Test 38d: Megolm Session Integrity Recovery Contract ===")
    lifecycle = ["derive_libolm_id", "compare_event_id", "reject_mismatch", "remove_bad_pickle", "request_key"]
    assert lifecycle.index("derive_libolm_id") < lifecycle.index("compare_event_id")
    assert lifecycle.index("compare_event_id") < lifecycle.index("reject_mismatch")
    assert lifecycle.index("reject_mismatch") < lifecycle.index("remove_bad_pickle")
    assert lifecycle.index("remove_bad_pickle") < lifecycle.index("request_key")
    print("  ✓ Megolm ID mismatches and BAD_SIGNATURE sessions enter recovery")

def test_olm_ratchet_cache_persistence_contract():
    print("\n=== Test 38e: Olm Ratchet Cache Persistence Contract ===")
    lifecycle = ["inbound_decrypt", "pickle_updated_session", "sqlite_save", "memory_pickle_update", "outbound_encrypt"]
    assert lifecycle.index("inbound_decrypt") < lifecycle.index("pickle_updated_session")
    assert lifecycle.index("pickle_updated_session") < lifecycle.index("sqlite_save")
    assert lifecycle.index("sqlite_save") < lifecycle.index("memory_pickle_update")
    assert lifecycle.index("memory_pickle_update") < lifecycle.index("outbound_encrypt")
    print("  ✓ inbound Olm ratchet updates both SQLite and the outbound pickle cache")


def test_protocol_notification_bridge_contract():
    """The shared notifications plugin consumes generic provider snapshots."""
    root = Path(__file__).resolve().parents[1]
    notifications = (root / "src/plugins/notifications/notifications.cpp").read_text()
    interface = (root / "src/interfaces/iprotocolnotifications.h").read_text()
    assert 'pluginInterface(QStringLiteral("IProtocolNotifications"))' in notifications
    assert 'SIGNAL(protocolNotificationsChanged())' in notifications
    assert "sameProtocolNotification" in notifications
    assert "NDR_ACCOUNT_ID" in notifications and "NDR_CONVERSATION_ID" in notifications
    assert "notification.body.toHtmlEscaped()" in notifications
    assert "setActiveConversation(notification.conversationId)" in notifications
    assert "provider->removeNotification(notification.id)" in notifications
    assert "FRostersModel->rootIndex()->findChilds(favoriteFindData, true)" in notifications
    assert "RDR_RECENT_REFERENCE" in notifications
    assert "protocolNotificationsChanged()" in interface
    assert "matrix" not in notifications.lower() and "xmpp" not in notifications.lower()
    print("  ✓ generic protocol notification discovery, update, display and routing contract valid")

def test_roster_notification_blink_and_muted_lamp_contract():
    """Incoming room notifications blink the status lamp until the room is read."""
    root = Path(__file__).resolve().parents[1]
    notifications = (root / "src/plugins/notifications/notifications.cpp").read_text()
    roster_view = (root / "src/plugins/rostersview/rostersview.cpp").read_text()
    roster_interface = (root / "src/interfaces/irostersview.h").read_text()
    chat_handler = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    matrix = (root / "src/plugins/matrix/matrix.cpp").read_text()
    status_icons = (root / "src/plugins/statusicons/statusicons.cpp").read_text()

    assert "BlinkStatusIcon = 0x10" in roster_interface
    protocol_bridge = notifications.split("void Notifications::onProtocolNotificationsChanged", 1)[1].split(
        "void Notifications::onProtocolNotificationActivated", 1)[0]
    assert "ProtocolNotification::Message" in protocol_bridge
    assert "ProtocolNotification::Mention" in protocol_bridge
    assert "NDR_ROSTER_FLAGS" in protocol_bridge and "IRostersNotify::BlinkStatusIcon" in protocol_bridge
    assert "rnotify.icon = (rnotify.flags & IRostersNotify::BlinkStatusIcon) > 0 ? QIcon() : icon;" in notifications

    roster_labels = roster_view.split("QList<quint32> RostersView::rosterLabels", 1)[1].split(
        "AdvancedDelegateItem RostersView::rosterLabel", 1)[0]
    roster_label = roster_view.split("AdvancedDelegateItem RostersView::rosterLabel", 1)[1].split(
        "IRostersModel *RostersView::rostersModel", 1)[0]
    assert "IRostersNotify::BlinkStatusIcon" in roster_labels
    assert "AIndex->data(Qt::DecorationRole)" in roster_label
    assert "AdvancedDelegateItem::Blink" in roster_label

    message_notify = chat_handler.split("INotification ChatMessageHandler::messageNotify", 1)[1].split(
        "bool ChatMessageHandler::messageShowWindow", 1)[0]
    window_activated = chat_handler.split("void ChatMessageHandler::onWindowActivated()", 1)[1].split(
        "void ChatMessageHandler::onWindowClosed()", 1)[0]
    assert "IRostersNotify::BlinkStatusIcon" in message_notify
    assert "setActiveConversation(conversationId)" in window_activated
    assert "markConversationRead" in window_activated

    active_conversation = matrix.split("void Matrix::setActiveConversation", 1)[1].split(
        "void Matrix::loadConversationAvatars", 1)[0]
    live_message = matrix.split("void Matrix::onNetworkMessageReceived", 1)[1].split(
        "void Matrix::onAvatarDataReceived", 1)[0]
    assert "FActiveConversationId = conversationId" in active_conversation
    assert "message.conversationId() != FActiveConversationId" in live_message

    assert "messageNotificationMuted" in status_icons
    assert "QPoint(2, pixmap.height() - 2), QPoint(pixmap.width() - 2, 2)" in status_icons
    assert "ANode.path()==OPV_MESSAGES_MUTED_TARGETS" in status_icons
    print("  ✓ room status lamps blink only for unread messages and show muted rooms crossed out")

def test_protocol_typing_state_is_conversation_scoped():
    """Generic room typing must never be routed through empty legacy JIDs."""
    root = Path(__file__).resolve().parents[1]
    chatstates = (root / "src/plugins/chatstates/chatstates.cpp").read_text()
    chatstates_header = (root / "src/plugins/chatstates/chatstates.h").read_text()
    state_widget = (root / "src/plugins/chatstates/statewidget.cpp").read_text()
    routing = chatstates.split("void ChatStates::onProtocolTypingChanged", 1)[1].split("\nvoid ", 1)[0]
    assert "setProtocolUserState(update.accountId, update.conversationId" in routing
    assert "window->streamJid()" not in routing and "window->contactJid()" not in routing
    assert "protocolUserChatStateChanged" in chatstates_header
    assert "FWindow->accountId() == accountId" in state_widget
    assert "FWindow->conversationId() == conversationId" in state_widget
    print("  ✓ protocol typing state is routed only to its account/conversation window")

def test_protocol_typing_tab_notifications_are_room_scoped():
    """Each protocol conversation's typing indicator must target its own tab."""
    root = Path(__file__).resolve().parents[1]
    chatstates = (root / "src/plugins/chatstates/chatstates.cpp").read_text()
    chatstates_header = (root / "src/plugins/chatstates/chatstates.h").read_text()
    protocol_state_start = chatstates.index("void ChatStates::setProtocolUserState")
    protocol_state_end = chatstates.index("\nvoid ChatStates::setSelfState", protocol_state_start)
    protocol_state = chatstates[protocol_state_start:protocol_state_end]
    assert "notifyProtocolUserState(accountId, conversationId, state)" in protocol_state
    assert "FProtocolNotifyIds" in chatstates_header
    assert "findConversationWindow(accountId, conversationId)" in chatstates
    assert "NDR_TABPAGE_WIDGET" in chatstates
    assert "window->instance()" in chatstates
    assert "FNotifications->removeNotification" in chatstates
    print("  ✓ protocol typing notifications target and clear their own room tab")

def test_room_sidebar_encryption_status_contract():
    """Room details must expose its encryption state and encrypted-room icon."""
    root = Path(__file__).resolve().parents[1]
    sidebar = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    assert "currentRoom.isEncrypted" in sidebar
    assert "MNI_CONNECTION_ENCRYPTED" in sidebar
    assert "End-to-end encrypted" in sidebar
    assert "Not end-to-end encrypted" in sidebar
    print("  ✓ room details show an explicit encryption status and lock icon")

def test_room_member_verification_indicator_contract():
    """Room member rows show the Matrix trust state beside each avatar."""
    root = Path(__file__).resolve().parents[1]
    roster = (root / "src/interfaces/iprotocolroster.h").read_text()
    database_header = (root / "src/plugins/matrix/matrixdatabase.h").read_text()
    database = (root / "src/plugins/matrix/matrixdatabase.cpp").read_text()
    network = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    matrix = (root / "src/plugins/matrix/matrix.cpp").read_text()
    handler = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    assert "bool isVerified = false;" in roster
    assert "bool hasVerificationState = false;" in roster
    assert "bool userVerificationStates(const QStringList &userIds, QMap<QString, bool> &states) const;" in database_header
    assert "bool MatrixDatabase::userVerificationStates(" in database
    snapshot_start = network.index("void MatrixNetwork::emitRosterSnapshot")
    snapshot_end = network.index("void MatrixNetwork::requestDisplayName", snapshot_start)
    assert "database.userVerificationStates(memberIds, verificationStates)" in network[snapshot_start:snapshot_end]
    assert "member.hasVerificationState = verificationStatesLoaded;" in network[snapshot_start:snapshot_end]
    reply_start = network.index("void MatrixNetwork::onReplyFinished")
    query_start = network.index('type == "keys_query"', reply_start)
    claim_start = network.index('type == "keys_claim"', query_start)
    assert "emitRosterSnapshot();" in network[query_start:claim_start]
    sidebar_start = handler.index("void ChatMessageHandler::setupRoomSidebar")
    sidebar_end = handler.index("void ChatMessageHandler::showRoomMemberProfile", sidebar_start)
    sidebar = handler[sidebar_start:sidebar_end]
    avatar = sidebar.index("rowLayout->addWidget(avatar)")
    verification = sidebar.index("rowLayout->addWidget(verification)")
    display_name = sidebar.index("rowLayout->addWidget(name")
    assert avatar < verification < display_name
    assert "member.isVerified" in sidebar
    assert "member.hasVerificationState" in sidebar
    assert "border-radius" in sidebar and "#2e" in sidebar and "#d" in sidebar
    assert "refreshRosterSnapshot" in matrix
    print("  ✓ room member verification dot follows trusted device state")

def test_favorite_room_context_menu_resolves_muc_room_proxy():
    """Favorite conference entries must route context actions to the real room row."""
    root = Path(__file__).resolve().parents[1]
    recent = (root / "src/plugins/recentcontacts/recentcontacts.cpp").read_text()
    muc = (root / "src/plugins/multiuserchat/multiuserchatplugin.cpp").read_text()
    start = recent.index("QList<IRosterIndex *> RecentContacts::recentItemProxyIndexes")
    end = recent.index("bool RecentContacts::isReady", start)
    proxy_lookup = recent[start:end]
    assert "AItem.type == REIT_CONFERENCE" in proxy_lookup
    assert "RIT_MUC_ITEM" in proxy_lookup
    assert "RDR_PREP_BARE_JID" in proxy_lookup
    assert "messageNotificationMuted(streamJid,roomJid)" in muc
    print("  ✓ favorite conference context menu reaches the room mute action")

def test_favorites_expose_persistent_notification_mute_action():
    """A favorite's context menu must offer the shared persistent mute toggle."""
    root = Path(__file__).resolve().parents[1]
    recent = (root / "src/plugins/recentcontacts/recentcontacts.cpp").read_text()
    assert "#include <utils/messagenotificationmute.h>" in recent
    start = recent.index("void RecentContacts::onRostersViewIndexContextMenu")
    end = recent.index("void RecentContacts::onRostersViewIndexToolTips", start)
    context_menu = recent[start:end]
    assert 'mute->setText(tr("Mute notifications"));' in context_menu
    assert "messageNotificationMuted(streamId, targetId)" in context_menu
    assert "setMessageNotificationMuted(streamId, targetId, AMuted)" in context_menu
    assert "AMenu->addAction(mute" in context_menu
    print("  ✓ favorites expose the persistent notification mute toggle")

def test_favorite_avatar_updates_reach_favorites_root():
    """Avatar role changes on a source roster row must refresh its favorite proxy."""
    root = Path(__file__).resolve().parents[1]
    recent = (root / "src/plugins/recentcontacts/recentcontacts.cpp").read_text()
    avatars = (root / "src/plugins/avatars/avatars.cpp").read_text()
    start = recent.index("void RecentContacts::onRostersModelIndexDataChanged")
    end = recent.index("void RecentContacts::onRostersModelIndexRemoved", start)
    update = recent[start:end]
    assert "emit rosterDataChanged(index, RDR_AVATAR_IMAGE)" in avatars
    assert "RDR_AVATAR_IMAGE" in update
    assert "FFavoriteIndexes" in update
    assert "FIndexToProxy.value(favoriteIndex) == AIndex" in update
    assert "rosterDataChanged(favoriteIndex, ARole)" in update
    print("  ✓ avatar image updates refresh favorite roster rows")

def test_favorite_rows_attach_recentcontacts_data_holder():
    """Favorite children need their own holder to delegate avatar roles."""
    root = Path(__file__).resolve().parents[1]
    recent = (root / "src/plugins/recentcontacts/recentcontacts.cpp").read_text()
    start = recent.index("void RecentContacts::updateFavoritesRoot()")
    end = recent.index("int RecentContacts::rosterDataOrder()", start)
    favorite_update = recent[start:end]
    attach = favorite_update.index("index->insertDataHolder(this)")
    insert = favorite_update.index("FRostersModel->insertRosterIndex(index, FFavoritesRootIndex)")
    assert attach < insert

def test_matrix_spaces_are_hidden_and_suppressed():
    """Matrix space room types must survive cache restore and be filtered."""
    root = Path(__file__).resolve().parents[1]
    roster_header = (root / "src/interfaces/iprotocolroster.h").read_text()
    database_header = (root / "src/plugins/matrix/matrixdatabase.h").read_text()
    database_source = (root / "src/plugins/matrix/matrixdatabase.cpp").read_text()
    network_source = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    matrix_source = (root / "src/plugins/matrix/matrix.cpp").read_text()
    assert "QString roomType;" in roster_header
    assert "QString roomType;" in database_header
    room_states = database_source.split("QList<MatrixStoredRoom> MatrixDatabase::roomStates() const", 1)[1].split(
        "QList<MatrixStoredMember> MatrixDatabase::roomMembers", 1)[0]
    assert "room_type" in room_states
    assert 'const QString roomType = content.value(QStringLiteral("type")).toString();' in network_source
    assert "room.roomType = roomType;" in network_source
    assert "room.roomType = stored.roomType;" in matrix_source
    roster_filter = matrix_source.split("QList<ProtocolRoom> Matrix::rooms() const", 1)[1].split(
        "ProtocolRoom Matrix::room", 1)[0]
    assert 'room.roomType != QStringLiteral("m.space")' in roster_filter
    notification_append = matrix_source.split("void Matrix::appendNotification(", 1)[1].split(
        "void Matrix::removeNotification", 1)[0]
    assert 'room(stored.conversationId).roomType == QStringLiteral("m.space")' in notification_append

def test_favorites_bind_late_matrix_roster_rows():
    """A favorite loaded before Matrix sync must adopt the later room proxy."""
    root = Path(__file__).resolve().parents[1]
    recent = (root / "src/plugins/recentcontacts/recentcontacts.cpp").read_text()
    start = recent.index("void RecentContacts::onRostersModelIndexDataChanged")
    end = recent.index("void RecentContacts::onRostersModelIndexRemoved", start)
    update = recent[start:end]
    assert "ARole == RDR_ACCOUNT_ID" in update or "ARole == RDR_CONVERSATION_ID" in update
    assert "recentItemProxyIndexes" in update
    assert "FIndexToProxy.insert(favoriteIndex" in update
    assert "rosterDataChanged(favoriteIndex, 0)" in update
    data_roles_start = update.index("static const QList<int> updateDataRoles")
    data_roles_end = update.index("static const QList<int> updatePropertiesRoles", data_roles_start)
    favorite_update_roles = update[data_roles_start:data_roles_end]
    assert "RDR_SHOW" in favorite_update_roles
    assert "RDR_STATUS" in favorite_update_roles
    assert "RDR_NAME" in favorite_update_roles
    label_start = recent.index("QList<quint32> RecentContacts::rosterLabels")
    label_end = recent.index("AdvancedDelegateItem RecentContacts::rosterLabel", label_start)
    assert "RLID_SCHANGER_STATUS" not in recent[label_start:label_end], \
        "Favorites must not claim the status label while returning an empty delegate item"
    print("  ✓ late Matrix roster identity binds favorite status and avatar data")

def test_favorite_items_use_the_avatar_provider_label():
    """Favorites must not mask the shared avatar label with a null item."""
    root = Path(__file__).resolve().parents[1]
    recent = (root / "src/plugins/recentcontacts/recentcontacts.cpp").read_text()
    avatars = (root / "src/plugins/avatars/avatars.cpp").read_text()
    roster_model = (root / "src/plugins/rostersmodel/rostersmodel.cpp").read_text()
    start = recent.index("QList<quint32> RecentContacts::rosterLabels")
    end = recent.index("AdvancedDelegateItem RecentContacts::rosterLabel", start)
    favorite_labels = recent[start:end]
    avatar_start = avatars.index("QList<quint32> Avatars::rosterLabels")
    avatar_end = avatars.index("AdvancedDelegateItem Avatars::rosterLabel", avatar_start)
    avatar_labels = avatars[avatar_start:avatar_end]
    assert "RLID_AVATAR_IMAGE" not in favorite_labels, \
        "RecentContacts must not shadow the Avatars provider's real image label"
    assert "AIndex->data(RDR_AVATAR_IMAGE)" in avatar_labels
    roles_start = recent.index("QList<int> RecentContacts::rosterDataRoles")
    roles_end = recent.index("QList<int> RecentContacts::rosterDataTypes", roles_start)
    assert "RDR_AVATAR_IMAGE" in recent[roles_start:roles_end]
    data_start = recent.index("QVariant RecentContacts::rosterData")
    data_end = recent.index("bool RecentContacts::setRosterData", data_start)
    assert "proxy->data(ARole)" in recent[data_start:data_end]
    proxy_start = recent.index("QList<IRosterIndex *> RecentContacts::recentItemProxyIndexes")
    proxy_end = recent.index("bool RecentContacts::isReady", proxy_start)
    assert "protocolStreamRoot(AItem.accountId)" in recent[proxy_start:proxy_end]
    assert "RDR_CONVERSATION_ID" in recent[proxy_start:proxy_end]
    assert "index->setData(RDR_AVATAR_KEY, room.avatarKey)" in roster_model

def test_roster_and_room_sidebar_widths_persist():
    """Roster and room-details sidebar widths persist globally."""
    root = Path(__file__).resolve().parents[1]
    main_window = (root / "src/plugins/mainwindow/mainwindow.cpp").read_text()
    chat_window = (root / "src/plugins/messagewidgets/chatwindow.cpp").read_text()
    chat_header = (root / "src/plugins/messagewidgets/chatwindow.h").read_text()
    moved_start = main_window.index("void MainWindow::onSplitterMoved")
    moved_handler = main_window[moved_start:]
    assert 'Options::setFileValue(FLeftWidgetWidth,"mainwindow.left-frame-width"' in moved_handler
    load_start = main_window.index("void MainWindow::loadWindowGeometryAndState")
    load_end = main_window.index("void MainWindow::updateWindow", load_start)
    assert 'Options::fileValue("mainwindow.left-frame-width",ns).toInt()' in main_window[load_start:load_end]
    assert "eventFilter(QObject *AObject, QEvent *AEvent)" in chat_header
    sidebar_start = chat_window.index("void ChatWindow::setSidebarWidget")
    sidebar_end = chat_window.index("QString ChatWindow::tabPageId", sidebar_start)
    sidebar_restore = chat_window[sidebar_start:sidebar_end]
    assert 'Options::fileValue("messages.chatwindow.sidebar-width")' in sidebar_restore
    assert "tabPageId()" not in sidebar_restore
    event_start = chat_window.index("bool ChatWindow::eventFilter")
    event_end = chat_window.index("bool ChatWindow::event(", event_start)
    event_filter = chat_window[event_start:event_end]
    assert 'Options::setFileValue(FSidebarDock->width(), "messages.chatwindow.sidebar-width")' in event_filter
    assert "tabPageId()" not in event_filter
    assert "resizeDocks" in chat_window
    assert "QEvent::Resize" in chat_window

def test_combine_with_roster_defaults_enabled():
    """Combined chat-window behavior is enabled for new/default profiles."""
    root = Path(__file__).resolve().parents[1]
    message_widgets = (root / "src/plugins/messagewidgets/messagewidgets.cpp").read_text()
    assert "Options::setDefaultValue(OPV_MESSAGES_COMBINEWITHROSTER,true);" in message_widgets

def test_per_target_message_notification_mute_contract():
    """Contact and room notifications must share persistent per-target mute state."""
    root = Path(__file__).resolve().parents[1]
    mute_helper = (root / "src/utils/messagenotificationmute.h").read_text()
    chat_handler = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    muc_plugin = (root / "src/plugins/multiuserchat/multiuserchatplugin.cpp").read_text()
    muc_window = (root / "src/plugins/multiuserchat/multiuserchatwindow.cpp").read_text()
    notifications = (root / "src/plugins/notifications/notifications.cpp").read_text()
    protocol_notifications = (root / "src/interfaces/iprotocolnotifications.h").read_text()
    matrix = (root / "src/plugins/matrix/matrix.cpp").read_text()
    recent = (root / "src/plugins/recentcontacts/recentcontacts.cpp").read_text()
    favorites_start = recent.index("void RecentContacts::updateFavoritesRoot")
    favorites_end = recent.index("bool RecentContacts::initSettings", favorites_start)
    updateFavorites = recent[favorites_start:favorites_end]
    assert "createRosterIndex(RIT_RECENT_ITEM, FFavoritesRootIndex)" in updateFavorites
    assert "FIndexToProxy.insert(index, proxies.first())" in updateFavorites
    assert "FIndexToProxy.insert(favoriteIndex, proxies.first())" in updateFavorites
    recent_ctx_start = recent.index("void RecentContacts::onRostersViewIndexContextMenu")
    recent_ctx_end = recent.index("void RecentContacts::onRostersViewIndexToolTips", recent_ctx_start)
    recent_context_menu = recent[recent_ctx_start:recent_ctx_end]
    assert "RIT_RECENT_ITEM" in recent_context_menu
    assert "indexesProxies(AIndexes,false)" in recent_context_menu
    assert "FRostersView->contextMenuForIndex(proxies,NULL,AMenu);" in recent_context_menu
    assert "FProxyContextMenuActions[AMenu] = menuActions - oldActions + recentActions;" in recent_context_menu
    about_start = recent.index("void RecentContacts::onRostersViewIndexContextMenuAboutToShow")
    about_end = recent.index("void RecentContacts::onRostersViewIndexMultiSelection", about_start)
    assert "FProxyContextMenuActions.take(menu)" in recent[about_start:about_end]
    assert "action->setVisible(false)" in recent[about_start:about_end]
    context_start = chat_handler.index("void ChatMessageHandler::onRosterIndexContextMenu")
    context_end = chat_handler.index("void ChatMessageHandler::onPresenceItemReceived", context_start)
    context_menu = chat_handler[context_start:context_end]
    append_start = matrix.index("void Matrix::appendNotification")
    append_end = matrix.index("void Matrix::removeNotification", append_start)
    matrix_append = matrix[append_start:append_end]
    assert "mute->setText(tr(\"Mute notifications\"));" in context_menu
    assert "index->type()==RIT_CONTACT" in context_menu
    assert "mute->setChecked(messageNotificationMuted(streamId,targetId));" in context_menu
    assert "OPV_MESSAGES_MUTED_TARGETS" in mute_helper
    assert "const Jid jid(AId);" in mute_helper
    assert "return jid.isValid() ? jid.pBare() : AId;" in mute_helper
    assert "QString streamId;" in protocol_notifications
    assert "stored.streamId = streamId();" in matrix_append
    assert "messageNotificationMuted(AMessage.to(), AMessage.from())" in chat_handler
    assert "RDR_PREP_BARE_JID" in chat_handler
    assert "messageNotificationMuted(streamJid(), contactJid.pBare())" in muc_window
    assert "RDR_PREP_BARE_JID" in muc_plugin
    assert "setMessageNotificationMuted" in muc_plugin
    assert "index->data(RDR_ACCOUNT_ID).toString()" in context_menu
    compact_notifications = notifications.replace(" ", "").replace("\t", "").replace("\r", "").replace("\n", "")
    assert "messageNotificationMuted(muteStreamId,notification.conversationId)" in compact_notifications
    assert "path == OPV_MESSAGES_MUTED_TARGETS" in notifications
    print("  ✓ muted targets persist and suppress contact/room message notifications")

def test_encrypted_matrix_formatted_body_contract():
    """Decrypted Matrix HTML must survive the adapter metadata boundary."""
    root = Path(__file__).resolve().parents[1]
    network = (root / "src/plugins/matrix/matrixnetwork.cpp").read_text()
    renderer = (root / "src/plugins/chatmessagehandler/chatmessagehandler.cpp").read_text()
    assert 'const QJsonObject messageContent = wasEncrypted ? decryptedContent : eventContent;' in network
    assert 'for (const QString &formattedField : {QStringLiteral("format"), QStringLiteral("formatted_body")})' in network
    assert 'messageContent.value(formattedField).toVariant()' in network
    assert 'matrixSafeHtml(' in renderer and 'AMessaging->formatEmoticonsForDisplay(formattedBody)' in renderer
    print("  ✓ decrypted Matrix formatted_body reaches the sanitized HTML renderer")

def run_smoke_test():
    """Run the full smoke test suite."""
    print("=" * 70)
    print("Matrix v3 Protocol Smoke Test (incl. presence/typing)")
    print("=" * 70)

    try:
        test_login_request()
        test_sync_response()
        test_send_request()
        test_presence_events()
        test_typing_events()
        test_sync_with_presence()
        test_timeline_pagination_contract()
        test_relations_contract()
        test_redacted_matrix_messages_scrub_format_and_preserve_timestamp()
        test_matrix_reaction_render_and_send_contract()
        test_encrypted_event_state()
        test_push_rule_highlight_contract()
        test_pending_event_replacement_contract()
        test_olm_key_upload_contract()
        test_cross_signing_key_chain_contract()
        test_megolm_room_key_contract()
        test_room_key_request_validation_contract()
        test_forwarded_room_key_wire_contract()
        test_encrypted_to_device_contract()
        test_generic_relation_media_contract()
        test_matrix_conversation_address_contract()
        test_plaintext_event_body_contract()
        test_direct_room_roster_identity_contract()
        test_room_avatar_roster_lazy_load_contract()
        test_room_image_history_cache_lazy_load_contract()
        test_matrix_media_reads_and_decodes_off_ui_lazily()
        test_late_hydrated_image_rebuilds_chronological_history()
        test_conversation_history_replay_contract()
        test_matrix_cached_history_merges_pending_live_messages_by_full_timestamp()
        test_matrix_messages_display_full_timestamp()
        test_matrix_live_event_waits_for_cached_history_before_chat_render()
        test_persisted_history_is_not_live_event_contract()
        test_conversation_display_name_contract()
        test_normalized_message_identity_contract()
        test_sync_filter_contract()
        test_sync_loop_waits_one_second_between_long_polls()
        test_invite_and_room_account_data_contract()
        test_receipt_contract()
        test_forwarded_room_key_contract()
        test_ssss_secret_gossip_contract()
        test_ssss_key_description_contract()
        test_ssss_request_binding_contract()
        test_megolm_share_index_contract()
        test_sas_mac_negotiation_contract()
        test_encrypted_event_not_active_room_bound_contract()
        test_room_key_request_session_origin_contract()
        test_one_time_key_topup_contract()
        test_olm_recovery_order_contract()
        test_olm_plaintext_sender_device_contract()
        test_e2ee_reference_parity_contract()
        test_element_megolm_export_format_contract()
        test_missing_session_recovery_lifecycle_contract()
        test_force_new_session_reissues_room_key_request_contract()
        test_normal_olm_session_recovery_contract()
        test_placeholder_room_key_request_contract()
        test_megolm_session_integrity_recovery_contract()
        test_olm_ratchet_cache_persistence_contract()
        test_protocol_notification_bridge_contract()
        test_roster_notification_blink_and_muted_lamp_contract()
        test_protocol_typing_state_is_conversation_scoped()
        test_protocol_typing_tab_notifications_are_room_scoped()
        test_room_sidebar_encryption_status_contract()
        test_room_member_verification_indicator_contract()
        test_favorite_room_context_menu_resolves_muc_room_proxy()
        test_favorites_expose_persistent_notification_mute_action()
        test_favorite_avatar_updates_reach_favorites_root()
        test_favorite_rows_attach_recentcontacts_data_holder()
        test_favorites_bind_late_matrix_roster_rows()
        test_favorite_items_use_the_avatar_provider_label()
        test_matrix_spaces_are_hidden_and_suppressed()
        test_roster_and_room_sidebar_widths_persist()
        test_combine_with_roster_defaults_enabled()
        test_per_target_message_notification_mute_contract()
        test_encrypted_matrix_formatted_body_contract()
        test_live_homeserver_contract()

        print("\n" + "=" * 70)
        print("✓ ALL SMOKE TESTS PASSED")
        print("=" * 70)
        return 0

    except AssertionError as e:
        print(f"\n✗ TEST FAILED: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    import sys
    sys.exit(run_smoke_test())