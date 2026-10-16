# Saved expeditions

Normal live play remembers each place's found sparks and safe checkpoint, plus the last
visited level. Closing and reopening the game resumes there. Checkpoints use the current
authored position and facing: the save never restores an airborne or arbitrary player
pose. A gate still places you at its intended arrival checkpoint, while keeping that
place's found sparks.

Saving happens after a find, checkpoint change, gate crossing and clean exit. A brief
message confirms a save; persistent error text explains a problem. F2's **Expedition**
section shows status. **Start new expedition… → New expedition** clears all visited
places and returns to the starting level. Cancel keeps the hunt. With `--level`, the
starting level is that explicitly selected file; otherwise it is the playground.

## Profiles

The default is `progress.json` in SDL's per-user `microjump/microjump` preference folder:

- Linux: `$XDG_DATA_HOME/microjump/microjump/`, or `~/.local/share/microjump/microjump/`.
- Windows: `%APPDATA%\microjump\microjump\`.

`--save PATH` chooses a separate profile. `--no-save` disables all profile access even
when `--save` is present. Custom `--level PATH` launches save only with an explicit
`--save PATH`, so editing a test level cannot alter the normal expedition. Use different
profiles for unrelated custom level sets that reuse the same authored level names.
Spaces and Unicode in custom file paths are supported. Automatic last-world selection
uses only a plain gate-compatible `.json` basename; explicitly selected custom files
with other names still retain their named progress in their separate profile.

Recordings, replays, captures, frame-limited runs, smoke/camera/resize/travel scripts,
frame logs and pacing diagnostics never read or write a profile, including an explicit
one. They retain fresh reproducible initial conditions. The isolated progress fixture
is the sole test mode that opts in, and requires its own explicit profile.

## Updates and recovery

Sparks are identified by their authored names within a named level. Reordering arrays
keeps finds attached to the right spark; removed sparks are ignored. Checkpoints match
positions rather than array indices. A moved/removed checkpoint falls back to the
current spawn. Renaming a level or spark creates a new identity. If the saved world
file disappears or fails to load, the game starts in the playground and keeps the
other worlds' entries.

A missing profile starts a new expedition. An unreadable, malformed or newer-format
profile stays untouched, and automatic writes are disabled for that run. You may
recover or back up the file, or explicitly confirm a new expedition to replace it.
Failed writes preserve the previous save and report the error. Writes use a closed,
checked temporary file on the same filesystem followed by atomic replacement; on
Windows the existing file is replaced without first deleting it. This protects against
partial writes, but does not promise durability through power loss or merge concurrent
running game instances: the last successful writer wins.

Version 1 JSON contains `version`, `last_level` (plain filename or null), and `levels`.
Each level entry contains `name`, `checkpoint` (three finite authored coordinates), and
`sparks` (unique names). Limits are 64 levels, 128 sparks per level, 80 UTF-8 bytes per
name, 1 MiB per file and 16 nesting levels. Unknown/duplicate fields and unsupported
versions are rejected so future data is not silently discarded.
