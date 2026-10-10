## master

- CHANGE: [ui] sync with chat events to reduce load.
- ADD:    [ui] notification notice.
- FIX:    [matrix] own messages moving from right to left.
- FIX:    [ui,matrix] stop to refresh whole chat after matrix sync.
- FIX:    [ui] widget size in the account options.
- ADD:    [ui,matrix] show user avatar of the account in the roaster.
- ADD:    [ui,matrix,xmpp] send images via copy'n paste.

## v2.1.0

- ADD: protocol-neutral messaging capabilities for message status, reactions,
       history actions, and decoration routing, with Matrix integration.
- ADD: a developer guide for integrating new chat protocols.
- CHANGE: Modern Chat bubbles with stable content-based sizing, compact
          padding, and separate message-status indicators; fixed bubble resizing and
          duplicate text beneath bubbles.
- CHANGE: Matrix formatted-message handling by trimming trailing line endings
          while preserving internal line breaks.
- ADD: regression tests for message status, history policies, formatted
       messages, and message-decoration behavior.

## v2.0.0

- CHANGE: the application as Vacuum Chat and updated its displayed version to
          2.0.0.
- ADD: new plugin for matrix and meshcore chat

