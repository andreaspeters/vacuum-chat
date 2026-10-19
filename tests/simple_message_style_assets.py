#!/usr/bin/env python3
"""Validate that the built-in Modern Chat style is discoverable and complete."""

from html.parser import HTMLParser
from pathlib import Path
import plistlib


ROOT = Path(__file__).resolve().parents[1]
STYLE = ROOT / "resources" / "simplemessagestyles" / "modern-chat"


class MarkupValidator(HTMLParser):
    """HTMLParser catches malformed tag nesting and end-tag structure."""

    def error(self, message):
        raise AssertionError(message)


def main():
    info_path = STYLE / "Info.plist"
    info = plistlib.loads(info_path.read_bytes())
    assert info["Name"] == "Modern Chat"
    assert info["DefaultVariant"] == "Default"
    assert (STYLE / "Incoming" / "Content.html").is_file()

    required_templates = [
        "Incoming/Content.html",
        "Incoming/NextContent.html",
        "Outgoing/Content.html",
        "Outgoing/NextContent.html",
        "Status.html",
        "Topic.html",
        "MeCommand.html",
    ]
    for relative_path in required_templates:
        text = (STYLE / relative_path).read_text(encoding="utf-8")
        assert "%message%" in text, relative_path
        parser = MarkupValidator()
        parser.feed(text)
        parser.close()

    incoming = (STYLE / "Incoming/Content.html").read_text(encoding="utf-8")
    css = (STYLE / "Variants/Default.css").read_text(encoding="utf-8")
    assert "%sender%" in incoming and "%shortTime%" in incoming
    assert "%userIconPath%" in incoming
    assert ".outgoing .xxxmessage" in css
    assert ".date_separator .xxxmessage" in css
    assert "padding: 8px 12px" in css
    assert "background-color: #f1f2f4" in css
    assert "background-color: #e8f1ff" in css
    print("Modern Chat style assets are discoverable and contain the required layouts.")


if __name__ == "__main__":
    main()
