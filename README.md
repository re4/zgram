<p align="center">
  <img src="website/assets/og.png" alt="Zgram — Messaging, elevated." width="100%">
</p>

<h1 align="center">Zgram</h1>

<p align="center">
  <strong>Messaging, elevated.</strong><br>
  A power-user Telegram Desktop experience with private local archives,
  local bookmarks, faster navigation, deeper personalization, and a new visual identity.
</p>

<p align="center">
  <code>Windows 64-bit</code>&nbsp;&nbsp;·&nbsp;&nbsp;
  <code>Qt 6</code>&nbsp;&nbsp;·&nbsp;&nbsp;
  <code>Telegram Desktop based</code>&nbsp;&nbsp;·&nbsp;&nbsp;
  <code>GPLv3</code>
</p>

<p align="center">
  <a href="https://t.me/zgram_io"><strong>Download Zgram</strong></a>
  &nbsp;·&nbsp;
  <a href="https://t.me/zgram_io">Official updates</a>
  &nbsp;·&nbsp;
  <a href="#features">Features</a>
  &nbsp;·&nbsp;
  <a href="#building-zgram">Build from source</a>
</p>

> [!IMPORTANT]
> Zgram is an independent community modification of Telegram Desktop. It is
> not affiliated with, sponsored by, or endorsed by Telegram Messenger LLP.
> Telegram is a trademark of its respective owner.

## Download

The current community build targets **Windows 64-bit**. Get the newest verified
build, release notes, installation notes, and update announcements from the
[official Zgram updates channel](https://t.me/zgram_io).

Only install builds published through the official channel. Zgram is based on
the cross-platform Telegram Desktop source, but other operating systems do not
currently have an official Zgram binary distribution.

## What is Zgram?

Zgram keeps the familiar Telegram Desktop foundation and adds a focused layer
for people who want more control over their workspace and locally available
message context. The additions are integrated into the existing settings,
message menus, chat header, and media tools instead of living in a separate
companion app.

The project includes a complete angel-wing visual identity, a redesigned
Power User center, encrypted on-device archives and bookmarks, a searchable
command palette, compact layouts, custom chat styling, and automated Windows
development builds.

## Features

| Feature | What it adds |
| --- | --- |
| **Encrypted Local Archive** | Opt-in, per-chat snapshots and edit history stored only on this device |
| **Local Bookmarks** | Private, searchable message snapshots available from any chat |
| **Command Palette** | One keyboard-driven search for navigation, settings, local tools, and current-chat media |
| **Power User Center** | A dedicated home for appearance, layout, hover actions, workspaces, archives, and bookmarks |
| **Flexible interface** | Compact or comfortable density, adjustable sidebar width, bubble corners, and window transparency |
| **Expanded chat tools** | Chat appearance, mute controls, filtered media galleries, archive controls, and bookmark actions |
| **Modern Zgram UI** | Hero panels, action cards, status badges, refined empty states, and rounded chat-list states |
| **Angel-wing identity** | Custom Zgram artwork and application icons across supported desktop resources |
| **Windows auto-builds** | Automated GitHub Actions Debug builds with downloadable artifacts for development testing |

### Encrypted Local Archive

Local Archive is an opt-in history for selected chats. It records message
information that this Zgram installation actually observes and stores it in
the account's encrypted local storage.

- Enable archiving separately for each eligible chat.
- Choose a retention period: **7 days**, **30 days**, **1 year**, or **Forever**.
- Keep message text, sender, date, reply target, outgoing state, and media summaries.
- Preserve locally observed edit versions and deletion state.
- Browse all archived chats or open a dedicated timeline for one chat.
- See clear `MESSAGE`, `EDITED`, `DELETED`, and `MEDIA` status badges.
- Review per-chat message counts, storage size, and retention policy.
- Search normally or narrow results with:
  - `is:message`
  - `is:deleted`
  - `is:edited` or `has:edits`
  - `has:media`
  - `from:name`
- Export one chat as structured **JSON** or a readable standalone **HTML** timeline.
- Clear one chat's archive or delete every local archive for the current account.
- See a `LOCAL` badge in the chat header while archiving is active.

Secret chats, self-destructing messages, view-once media, and protected content
are never added to Local Archive. Because the archive only captures content
observed by this installation, it is not a way to retrieve earlier or unseen
messages from Telegram.

### Local Bookmarks

Local Bookmarks are private message snapshots that stay with this account on
this device.

- Add or remove a bookmark from a message's context menu.
- Search all bookmarks from a dedicated local view.
- Retain the last locally observed text and message context when a bookmarked
  Telegram message is later deleted.
- Keep useful snapshot details such as the chat, sender, date, reply target,
  outgoing state, and media summary.
- Mark deleted source messages clearly and remove them by selecting the entry.
- Clear all local bookmarks whenever you want.

Bookmarks do not sync through Telegram and are separate from Saved Messages.

### Command Palette

Press <kbd>Ctrl</kbd> + <kbd>K</kbd> to search Zgram commands from one place.
The palette can open:

- Telegram search, Saved Messages, and Contacts
- Main, Advanced, and Power User settings
- Chat workspaces, folders, and tabs
- Local Archive and Local Bookmarks
- The current chat's photos, videos, files, links, music, and voice/video messages
- Chat appearance controls when a chat is open

Results are grouped into **Navigation**, **Interface**, **Local data**, and
**Current chat** categories.

### Power User Center

Open **Settings → Power User** to shape Zgram around your workflow.

- **Layout density:** Comfortable or Compact
- **Message bubble radius:** Rounded or Compact corners
- **Sidebar width:** Narrow, Balanced, or Wide
- **Window transparency:** Off, Subtle, or Glass
- Quick links to accent colors, themes, chat wallpaper, and blur controls
- Independent toggles for quick reply and quick reactions on message hover
- Direct access to the Command Palette, chat workspaces, Local Archive, and
  Local Bookmarks

Compact density reaches beyond the main list to chat rows, forum topics, and
related sidebar surfaces for a consistently tighter workspace.

### Chat and media workflow

Zgram extends the existing chat menus with quicker access to the tools used
most often:

- A **Chat profile** submenu for mute controls, chat appearance, and media
  filters
- One-click filtered galleries for photos, videos, files, links, music, and
  voice/video messages
- Per-chat archive retention, viewing, and clearing controls
- A message context action for local bookmarks
- A visible local-archive status indicator in the chat header
- Refined compact rows and rounded active, selected, and hover states

### Modern interface system

The new Zgram settings use reusable hero panels, action cards, toggle cards,
badges, grouped panels, and informative empty states. Search keywords are
included throughout the new settings so features remain easy to discover.

### Telegram Desktop foundation

Zgram retains the core Telegram Desktop experience—including chats, groups,
channels, media, calls, notifications, themes, folders, and Telegram's normal
account and privacy controls—while layering its additions on top.

## Feature map

| What you want to do | Where to find it |
| --- | --- |
| Open the command palette | <kbd>Ctrl</kbd> + <kbd>K</kbd> |
| Customize Zgram | **Settings → Power User** |
| View every local archive | **Settings → Advanced → Local Archive** |
| Set retention for one chat | Open the chat menu → **Local archive…** |
| View local bookmarks | **Settings → Power User → Local Bookmarks** |
| Bookmark a message | Right-click the message → bookmark action |
| Filter a chat's media | Open the chat menu → **Chat profile → Filtered media gallery** |
| Change one chat's look | Open the chat menu → **Chat profile → Chat appearance** |

## Local data and privacy

Zgram's added data tools are intentionally local:

| Data | Storage behavior |
| --- | --- |
| Local archives | Encrypted for the signed-in account and kept only on this device |
| Local bookmarks | Encrypted for the signed-in account and kept only on this device |
| Telegram messages | Continue to follow Telegram's own cloud and secret-chat behavior |
| JSON/HTML exports | Written as readable files to the location you choose |

> [!CAUTION]
> Exported JSON and HTML files are no longer protected by Zgram's encrypted
> local storage. Store or share exports with the same care as any private chat
> history.

Local Archive and Local Bookmarks are not cloud backups. Removing local app
data, deleting archives, or losing the device can permanently remove them.

## Automated Windows builds

The repository includes a GitHub Actions workflow for Windows x64 development
builds. It runs manually and for configured pushes or pull requests, producing
an artifact whose name begins with `Zgram Windows Debug`.

- Qt 6 and Ninja Multi-Config
- Visual Studio x64 toolchain
- Debug configuration only
- Superseded runs are cancelled automatically
- Build artifacts are retained for 14 days

These artifacts are intended for development and testing. Public release
downloads are announced through [@zgram_io](https://t.me/zgram_io).

## Building Zgram

Zgram is a large native C++/Qt project. A configured Telegram Desktop build
environment and its external libraries are required; cloning this repository
alone is not enough.

### Windows Debug build

1. Install Visual Studio with the C++ desktop toolchain and prepare the
   Telegram Desktop Windows dependencies.
2. Open an **x64 Native Tools Command Prompt**.
3. From the repository root, build the existing configured tree:

```powershell
cmake --build out --config Debug --target Telegram
```

The executable is produced at:

```text
out/Debug/Telegram.exe
```

Use Debug builds for local development. Full upstream environment setup is
documented in:

- [Windows build guide](docs/building-win.md)
- [macOS build guide](docs/building-mac.md)
- [Linux build guide](docs/building-linux.md)

The custom GitHub workflow is also available under
[`.github/workflows`](.github/workflows) as a reference for the Windows build
environment.

## Website

The official Zgram landing page is a dependency-free static site built with
plain HTML, CSS, and JavaScript. Its source is in [`website/`](website/).

## Source, licensing, and credits

Zgram is based on
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop) and uses the
[Telegram API](https://core.telegram.org/api). The source is distributed under
the **GNU General Public License v3** with the OpenSSL exception described in
[LICENSE](LICENSE). Third-party notices and dependency licenses are listed in
[LEGAL](LEGAL).

This project is maintained independently. The Telegram name and original
Telegram Desktop project belong to their respective owners.

## Official links

- **Updates, downloads, and release notes:** [t.me/zgram_io](https://t.me/zgram_io)
- **Source code:** [github.com/re4/zgram](https://github.com/re4/zgram)

<p align="center">
  <img src="website/assets/zgram-angel.png" alt="Zgram angel-wing emblem" width="260">
</p>

<p align="center"><strong>ZGRAM · YOUR WAY</strong></p>
