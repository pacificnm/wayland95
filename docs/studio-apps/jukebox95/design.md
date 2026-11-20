# Jukebox 95: a music player built in Wayland Studio

A Winamp-style music player for Wayland 95, built in Wayland Studio by
Claude through `win95studio-mcp`. It's the first real test of the
MCP tools on an app that isn't a plain records form, and it should
show where the Studio's elements stop being enough for this kind of app.

- Project: [jukebox95.w95proj](jukebox95.w95proj) (opens in the Studio:
  **File > Open Project**)
- Database: `jukebox95.db` (SQLite, beside the project; made on the first run)
- Program: `jukebox95` (Make Program's file, beside the project; not
  kept in git)

## What it's for

A small, always-open window that plays a playlist of your music files:
play, pause, stop, previous, next, seek, volume, shuffle and repeat. A
playlist you can search, play by double-clicking, rate and tidy up.
Winamp's look is the model: a dark display with green text above the
buttons, and the playlist under it.

## Winamp's features, and how this app does them

| Winamp | Jukebox 95 | How |
|---|---|---|
| Play, pause, stop, previous, next | Yes | Buttons with Wayland 95's `play`, `pause`, `stop`, `previous`, `next` icons, and a Play menu with shortcuts |
| Seek bar and elapsed / total time | Yes | The MediaPlayer element's own control bar (it has a seek bar, the time and a mute button) |
| Track title in the display | Yes | `Artist - Title` in green, going round (a Timer moves it) when it's too long to fit |
| Elapsed time, big | Yes | A Timer (every 250 ms) puts `player1.clock(player1.position)` in a big Label |
| Playing / paused / stopped | Yes | A status Label, set from the MediaPlayer's started, paused, stopped and finished events |
| Volume | Yes | A Slider (0 to 100) |
| Shuffle and repeat | Yes | Two CheckBoxes; the play order is worked out in the form's code |
| Goes on to the next song | Yes | The MediaPlayer's finished event |
| Playlist editor | Yes | A DataGrid of the Tracks table under the player: double-click plays, a search box filters |
| Add files | Yes | **Files...**: the Win95 Open box (`App.chooseFiles`), several at once |
| Add a folder | Yes | **Folder...**: Select Folder (`App.chooseFolder`), then every song in it and its folders (`App.listFiles`) |
| Title, artist, album, length from the file | Yes | `App.readTags` reads each file's own tags as it's added (a guess from its path when it has none) |
| Edit a song's details, rate it | Yes | **Song** form on the Tracks table: title, artist, album, rating, file |
| Play count, last played, length | Yes | Saved by code when a song starts |
| Video files | Sound only | The MediaPlayer is just its controls here; a video's sound plays |
| Spectrum analyser / oscilloscope | Yes | A Visualizer in the display; click it for Spectrum, Oscilloscope or Meter |
| Several saved playlists (.m3u) | Not yet | Possible with a Playlists table; left out to keep the first version small |
| Media keys, the sound menu | Yes | MediaKeys on the MediaPlayer: Play/Pause, Next, Previous and Stop work from anywhere, and the tray's speaker shows the song and ⏮ ⏯ ⏭ |
| Album art | Yes | The song's own picture (`player1.coverArt`) in an Image in the display, and on the sound menu |
| Equalizer | Yes | View > Equalizer (Ctrl+G), or the EQ in the display: ten bands, the preamp and presets under the playlist; kept in the Settings table for next time |
| Skins | No | Outside what Studio apps can do now |

## The data

Two tables: **Tracks**, one record per song in the playlist, and
**Settings** (`Name`, unique, and `Value`): the equalizer (`equalizer`:
on, the bands and the preamp as JSON) and whether its panel shows
(`showEqualizer`).

| Field | Type | Settings | What it's for |
|---|---|---|---|
| `Title` | text | required, max 120 | The song's name |
| `Artist` | text | max 120 | |
| `Album` | text | max 120 | |
| `File` | text | required, unique, max 255, must start with `/`, `~/` or `http(s)://` | Where the song is: a path or a web address |
| `Length` | text | max 10 | `3:45`, from its tags as it's added (or the first time it plays) |
| `Rating` | integer | 0 to 5, default 0 | Stars, shown as `★★★☆☆` in the display |
| `Plays` | integer | 0 or more, default 0 | How many times it's been played |
| `LastPlayed` | datetime | | When it last started |
| `Position` | integer | 1 or more | The playlist's order |
| `Added` | datetime | auto: created | |

`File` being unique stops the same song going in twice; adding skips
(and counts) the ones that are there already.

## The forms

### PlayerForm (the start form)

```
┌ Jukebox 95 ─────────────────────────────────────────┐
│ File  Play  Help                                     │
│ ┌──────────────────────────────────────────────────┐ │
│ │ ▶ PLAYING            Track 8 of 17   ▂▅▇▆▄▃▂▁    │ │  black, green text
│ │ 2:14     Iron Maiden - Hallowed Be…  ▃▆█▇▅▃▂▁    │ │  big clock, title going round
│ │                      ★★★★☆   7:11    (Visualizer)│ │
│ └──────────────────────────────────────────────────┘ │
│ ▶ ❚❚ ■ ─────────●──────────────────  2:14 / 7:11  🔊 │  MediaPlayer (its bar)
│ [⏮][▶][❚❚][■][⏭]  ☐ Shuffle ☐ Repeat  Volume ──▼── │  Slider
│ ──────────────────────────────────────────────────── │
│ Search [________] [Files…][Folder…][Edit…][Remove]   │
│ ┌#─┬ Title ────────────┬ Artist ───┬ Album ────┬Len┐ │
│ │ 1│ Invaders         │ Iron Maid… │ The Numb… │3:23│ │  DataGrid, column widths
│ │ …                                                │ │
│ └──────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

- **Menu**: File (Add Files… Ctrl+O, Add Folder… Ctrl+L, Paste Paths…,
  New Song…, Edit Song… Ctrl+E, Remove Song, Exit), Play (Play/Pause
  Ctrl+P, Stop Ctrl+T, Previous Ctrl+B, Next Ctrl+N, Shuffle Ctrl+H,
  Repeat Ctrl+R; Shuffle and Repeat ticked when on), Help (About Jukebox 95).
- **Files…** and **Folder…** add songs; each one's title, artist, album
  and length come from its own tags as it goes in.
- Double-clicking a row plays it. Choosing a row (one click) only selects it.
- The search box filters the grid as you type.
- Click the Visualizer to change it: Spectrum, Oscilloscope, Meter.
- A Timer (`clock`, every 250 ms) keeps the big time and the scrolling
  title going; it's only seen in the Studio.
- **View > Equalizer** (Ctrl+G), or a click on the **EQ** in the display
  (lit while the equalizer is on), opens the Equalizer under the playlist
  and makes the window taller to hold it. What's set on it is kept in
  Settings and comes back when Jukebox 95 opens again.
- Space plays and pauses, Left and Right skip five seconds, while the
  MediaPlayer has the keyboard (its own keys).

### SongForm (Edit Song / one new song)

A data form on Tracks: File (with **…** to choose it), Title, Artist,
Album, Rating. **From the File** fills Title, Artist, Album and Length
from the file's own tags; a file with none gets a guess from its path
(`~/Music/iron-maiden/1982-the-number-of-the-beast/05-the-number-of-the-beast.mp3`
gives *The Number Of The Beast*, *Iron Maiden*, *The Number Of The Beast*).
**Save** saves and closes; the player's list updates by itself (the grid
re-reads its table whenever it changes).

Opened with `{ id }` it edits that song; without, it's a new one.

### AddForm (Paste Paths)

A box to paste paths into, one per line (from a file manager, or a
Terminal's `find`), and **Add**. They go through the player's own
`addPaths`, so it's the same as Files…: tags read, duplicates skipped,
the table's rules applied, and a message with how many went in.

### AboutForm

The name, version, and a line about where it was built.

## How it plays (PlayerForm's code)

(General) keeps the play order and where it is:

- `order`: the track ids in the order they'll play: by Position, or
  shuffled when Shuffle is on (the playing song stays where it is).
- `at`: where in `order` the playing song is (-1: none).
- `playId(id)`: loads the song into the MediaPlayer, plays it, shows it
  and selects its row.
- `next()` / `previous()`: move through `order`. At the end: back to the
  start with Repeat, else stop. Previous after three seconds starts the
  song again instead, as Winamp does.
- `reorder()`: makes `order` again (after Shuffle changes, or songs are
  added or removed).

Events: the MediaPlayer's **started** counts the play (Plays, LastPlayed;
once per song, not after a pause) and saves Length the first time,
**finished** goes to the next song, **failed** shows why in the display
and goes on to the next song (it stops after every song has failed in a
row, so a playlist of missing files doesn't spin forever).

## What Studio gained for it

The first version showed what the Studio couldn't do yet. These were
added to it (2026-11-18), and this version uses them:

1. **Choosing files**: `App.chooseFile`, `chooseFiles`, `chooseFolder`,
   `chooseSaveFile` (the Win95 boxes in a window of their own), and
   `App.listFiles`, `App.fileInfo`.
2. **Timer** element: Interval, Enabled, a `timer` event.
3. **Slider** element: Win95's trackbar, across or up and down.
4. **MediaPlayer tags and events**: `artist`, `album`, `albumArtist`,
   `genre`, `trackNumber`, `year`; `loaded` and `positionChanged`; and
   `App.readTags(path, callback)` to read a file's tags without playing it.
5. **Visualizer** element: Spectrum, Oscilloscope or Meter of a
   MediaPlayer's sound (C++ AudioAnalyzer through Qt's
   QAudioBufferOutput; this needs `qt6-multimedia-dev` to build).
6. **DataGrid column widths**: `Title:180` in Columns.

Also fixed on the way: the DataGrid's `Binding loop detected` warning,
and `put_form` naming a missing form twice.

Then (2026-11-19), the rest of what was missing:

7. **Media keys and the sound menu**: a MediaPlayer's **MediaKeys** makes
   it an MPRIS player; the compositor gives the media keys to the shell,
   which sends them to the program that's playing, and the tray's
   speaker shows its title, artist and cover with ⏮ ⏯ ⏭. Next and
   Previous are its `nextTrack` and `previousTrack` events.
8. **Cover art**: `player1.coverArt` and `App.readTags(...).coverArt`.
9. **`run_test`** in the MCP: the app driven off the screen, on a copy of
   its database, with checks. [jukebox95.test.js](jukebox95.test.js) is
   this app's.

Still not there:

- **Tags Qt drops**: a year written as just `1986` (it wants a whole
  date) and a track number written `3/8`. Most of `~/Music` has `3/8`, so
  the playlist keeps its order by `#` (the order songs were added in,
  which for a folder is its files' order).
- **Skins.**

Then (2026-11-19, later) the equalizer:

10. **Equalizer**: a MediaPlayer's ten bands (31 Hz to 16 kHz, -12 to +12
    dB), preamp and presets (Equalizer, EqPreset, EqPreamp; `eqGains`,
    `setEqGain`, `applyEqPreset` in code), and an **Equalizer** element
    that shows them. The sound goes through the filters (C++) to a stream
    of its own, about a tenth of a second behind; the Visualizer shows it
    after the equalizer.

## How it was built and checked

All through `win95studio-mcp` (over stdio): `new_project`, `put_table`,
`put_form` for each form, `set_app`, `remove_form` (the starting Form1),
`check_project`, `show_form` on each form, `build_program`.

Checked headless (`win95apprun`, offscreen, the player muted):

- Adding: the 17 songs of the review playlist through `addPaths` (Files…,
  Folder… and Paste Paths all use it), with their tags: *Sinner's Swing!
  (2015 Remastered)*, *The Best of The Doors - Disc One of Two*, lengths
  like 7:11; again, two skipped as already there and `relative/x.mp3`
  refused by File's pattern
- Playing one: the big clock moves, the long title goes round, the
  Visualizer's level rises (real music, muted)
- From the first version, on test tones: the next song at the end of
  each, a missing file skipped, the end of the list stopping; Plays and
  LastPlayed; Repeat both ways; Shuffle; Pause and Play; SongForm's
  fields, a rating, a bad File refused

The Studio's own tests cover the new pieces with real windows
(`tests/run.sh studioplayer`): the Open box clicked through (a click, a
Shift+click, Open; Escape), the tags read, the Visualizer's bar on
screen, the Slider clicked and moved with a key, and in the Studio the
Toolbox, the Visualizer's Player chosen in Properties and following a
rename.

Its own test, [jukebox95.test.js](jukebox95.test.js), through `run_test`
(21 checks, 8 seconds, on a copy of the playlist): a double-click plays
the 8th song; the display, its cover and the Visualizer; the title going
round as the Timer ticks; the Next and Previous keys (its nextTrack and
previousTrack events); the Slider; Shuffle and its menu tick; Search;
Remove answered No, then Yes; Add Folder (the Select Folder box answered)
putting back the one that was taken off; View > Equalizer, Rock kept in
Settings and back on the next opening, the EQ in the display hiding it.

The media keys and the sound menu are checked with real windows in the
Studio's tests (`tests/run.sh studioplayer`: the keys through the
compositor and the shell, ⏭ and Show the Player on the sound menu).

Not checked: the sound heard through speakers, and Jukebox 95's own
windows clicked through by hand.

The project's `jukebox95.db` has 17 songs from `~/Music` (Iron Maiden,
The Doors, Van Halen) for trying it from the Studio. A program file
starts with an empty playlist of its own.

## Review checklist

- [ ] The layout and the look (green on black) are what you want
- [ ] The Tracks fields are the right ones (Genre? Year? Track number?)
- [ ] Files… and Folder… add what you expect from your library
- [ ] The Visualizer's look (bars, colours, falling speed)
- [ ] The equalizer, by ear: a preset, a band all the way up (it's the
      one thing a test can't hear)
- [ ] The media keys and the sound menu with your keyboard and your
      music (after `sudo ./install.sh` and logging in again: the
      compositor and the shell changed)
- [ ] Which of the remaining gaps to do next
